#include "runner.hpp"

#include "isoexport.hpp"
#include "pcsx2_config.hpp"
#include "texbake.hpp"
#include "wavconvert.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace fs = std::filesystem;

Runner::~Runner() {
    join();
    killPs2Client();
}

void Runner::join() {
    if (thread_.joinable()) thread_.join();
}

std::string Runner::log() const {
    std::lock_guard<std::mutex> lock(logMutex_);
    return log_;
}

void Runner::clearLog() {
    std::lock_guard<std::mutex> lock(logMutex_);
    log_.clear();
}

void Runner::appendLine(const std::string& line) {
    // Timestamp every line - over the network host: filesystem load times are
    // dominated by asset sizes, and the [ps2] log stream is the only way to
    // profile where a slow console boot actually spends its time.
    SYSTEMTIME st;
    GetLocalTime(&st);
    char stamp[16];
    snprintf(stamp, sizeof(stamp), "%02d:%02d:%02d ", st.wHour, st.wMinute,
             st.wSecond);
    std::lock_guard<std::mutex> lock(logMutex_);
    log_ += stamp;
    log_ += line;
    log_ += '\n';
}

void Runner::buildAndRun(const Project& p, bool runEmulator) {
    if (busy()) return;
    join();
    cancelRequested_ = false;
    state_ = State::Running;
    thread_ = std::thread(&Runner::worker, this, p, true, runEmulator, false);
}

void Runner::runEmulatorOnly(const Project& p) {
    if (busy()) return;
    join();
    cancelRequested_ = false;
    state_ = State::Running;
    thread_ = std::thread(&Runner::worker, this, p, false, true, false);
}

void Runner::buildAndRunPs2(const Project& p, bool build) {
    if (busy()) return;
    join();
    cancelRequested_ = false;
    state_ = State::Running;
    thread_ = std::thread(&Runner::worker, this, p, build, true, true);
}

void Runner::clean(const Project& p) {
    if (busy()) return;
    join();
    cancelRequested_ = false;
    state_ = State::Running;
    thread_ = std::thread([this, p] {
        appendLine("[editor] === Clean: " + p.name + " ===");
        // bin\ is locked by anything running out of it: ps2client keeps it as
        // its cwd (file server), PCSX2 holds the ELF. Kill ours by handle AND
        // strays by name - an orphan from a previous editor instance survives
        // killPs2Client() and made remove_all fail with "Access is denied".
        killPs2Client();
        exec("taskkill /F /IM ps2client.exe 2>nul & taskkill /F /IM pcsx2-qt.exe 2>nul & "
             "taskkill /F /IM pcsx2.exe 2>nul & exit 0",
             "");
        // Container game volume (obj + bin). Failure is fine - a stopped
        // container just means there is nothing cached there to clean.
        if (exec("docker compose exec -T compiler sh -c \"rm -rf /src/obj /src/bin\"",
                 p.dir) != 0)
            appendLine("[editor] Container not running - cleaned the host side only.");

        // Host bin\: per-file, clearing read-only first (remove_all refuses
        // those on Windows), retrying a few times (taskkill returns before
        // the killed process actually releases its handles), and naming the
        // file that stays locked - "Access is denied" alone is undebuggable.
        const fs::path bin = fs::path(p.dir) / "bin";
        std::string stuck;
        for (int attempt = 0; attempt < 4; attempt++) {
            if (attempt) Sleep(500);
            stuck.clear();
            std::error_code ec;
            for (fs::recursive_directory_iterator it(bin, ec), end; it != end;
                 it.increment(ec)) {
                if (it->is_regular_file(ec)) {
                    fs::permissions(it->path(), fs::perms::owner_write,
                                    fs::perm_options::add, ec);
                    fs::remove(it->path(), ec);
                    if (ec && stuck.empty()) stuck = it->path().string();
                }
            }
            std::error_code rmEc;
            fs::remove_all(bin, rmEc);  // now-empty tree (dirs + leftovers)
            if (!rmEc && !fs::exists(bin, rmEc)) {
                appendLine("[editor] Removed bin\\ - run a Build to regenerate.");
                state_ = State::Success;
                return;
            }
        }
        appendLine("[editor] Clean failed - still locked: " +
                   (stuck.empty() ? bin.string() : stuck) +
                   " (which program has it open? Explorer preview, an audio "
                   "player, the Debug window of another editor instance...)");
        state_ = State::Failed;
    });
}

void Runner::exportIso(const Project& p) {
    if (busy()) return;
    join();
    state_ = State::Running;
    thread_ = std::thread([this, p] {
        appendLine("[editor] === ISO export: " + p.name + " ===");
        const std::string err =
            isoexport::build(p, [this](const std::string& l) { appendLine(l); });
        if (!err.empty()) appendLine("[editor] ISO export failed: " + err);
        state_ = err.empty() ? State::Success : State::Failed;
    });
}

void Runner::cancel() {
    if (!busy()) return;
    cancelRequested_ = true;
    appendLine("[editor] Cancelling...");
    std::lock_guard<std::mutex> lock(execProcMutex_);
    if (execProc_) TerminateProcess((HANDLE)execProc_, 1);
}

int Runner::exec(const std::string& cmdline, const std::string& cwd) {
    if (cancelRequested_) return -1;
    appendLine("> " + cmdline);

    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE readPipe = nullptr, writePipe = nullptr;
    if (!CreatePipe(&readPipe, &writePipe, &sa, 0)) {
        appendLine("[editor] Failed to create pipe");
        return -1;
    }
    SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOA si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = writePipe;
    si.hStdError = writePipe;
    si.hStdInput = INVALID_HANDLE_VALUE;

    PROCESS_INFORMATION pi{};
    // /S: strip only the outermost quotes, regardless of quotes inside
    std::string full = "cmd.exe /S /C \"" + cmdline + "\"";

    BOOL ok = CreateProcessA(nullptr, full.data(), nullptr, nullptr, TRUE,
                             CREATE_NO_WINDOW, nullptr,
                             cwd.empty() ? nullptr : cwd.c_str(), &si, &pi);
    CloseHandle(writePipe);
    if (!ok) {
        CloseHandle(readPipe);
        appendLine("[editor] Failed to start: " + cmdline);
        return -1;
    }
    {
        std::lock_guard<std::mutex> lock(execProcMutex_);
        execProc_ = pi.hProcess;
    }

    std::string pending;
    char buf[4096];
    DWORD n = 0;
    while (ReadFile(readPipe, buf, sizeof(buf), &n, nullptr) && n > 0) {
        pending.append(buf, n);
        size_t nl;
        while ((nl = pending.find('\n')) != std::string::npos) {
            std::string line = pending.substr(0, nl);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            appendLine(line);
            pending.erase(0, nl + 1);
        }
    }
    if (!pending.empty()) appendLine(pending);
    CloseHandle(readPipe);

    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 0;
    GetExitCodeProcess(pi.hProcess, &code);
    {
        std::lock_guard<std::mutex> lock(execProcMutex_);
        execProc_ = nullptr;
    }
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return (int)code;
}

static std::string findPCSX2() {
    std::vector<fs::path> dirs;
    if (const char* pf = getenv("ProgramFiles")) dirs.push_back(fs::path(pf) / "PCSX2");
    if (const char* pf86 = getenv("ProgramFiles(x86)")) dirs.push_back(fs::path(pf86) / "PCSX2");
    for (const auto& dir : dirs) {
        for (const char* exe : {"pcsx2-qt.exe", "pcsx2.exe"}) {
            fs::path p = dir / exe;
            std::error_code ec;
            if (fs::exists(p, ec)) return p.string();
        }
    }
    return "";
}

// The PCSX2 executable to launch: the project's configured path when set (and
// it exists), otherwise the Program Files auto-detect. Empty when neither is
// available (a configured-but-missing path resolves to empty, not the default).
static std::string resolveEmulator(const Project& p) {
    if (!p.emulatorPath.empty()) {
        std::error_code ec;
        return fs::exists(p.emulatorPath, ec) ? p.emulatorPath : "";
    }
    return findPCSX2();
}

std::string Runner::emulatorLogPath(const Project& p) const {
    const std::string exe = resolveEmulator(p);
    if (exe.empty()) return "";
    // emulog.txt lives in the "logs" folder next to PCSX2.ini's "inis" folder.
    const fs::path ini = pcsx2::findIni(exe);
    if (ini.empty()) return "";
    return (ini.parent_path().parent_path() / "logs" / "emulog.txt").string();
}

bool Runner::launchPCSX2(const Project& p) {
    const std::string exe = resolveEmulator(p);
    if (exe.empty()) {
        if (!p.emulatorPath.empty())
            appendLine("[editor] Configured emulator not found: " + p.emulatorPath +
                       " - check the path in Edit > Preferences.");
        else
            appendLine("[editor] PCSX2 not found in Program Files. Install it or set the "
                       "emulator path in Edit > Preferences.");
        return false;
    }
    std::error_code ec;
    if (!fs::exists(p.elfPath(), ec)) {
        appendLine("[editor] ELF not found: " + p.elfPath() + " - build the project first.");
        return false;
    }

    // Kill a previous emulator instance, if any (ignore errors).
    exec("taskkill /F /IM pcsx2-qt.exe 2>nul & taskkill /F /IM pcsx2.exe 2>nul & exit 0", "");

    // The game appends its TYRA_LOG output to bin/log.txt (host fs); drop the
    // stale file so the Debug window shows only this run's log. The
    // ps2link.run marker must go too - a leftover from a PS2 deploy would
    // make this PCSX2 boot skip the IOP reset (see the generated main.cpp).
    std::error_code logEc;
    fs::remove(fs::path(p.dir) / "bin" / "log.txt", logEc);
    fs::remove(fs::path(p.dir) / "bin" / "ps2link.run", logEc);
    // Live Debugger channel (docs/live-debugger.md): both files describe a
    // session that is over. A stale livedbg.cmd would freeze the fresh boot at
    // whatever the last session was doing, and a stale livedbg.bin would read
    // as a game already reporting; the editor re-sends its breakpoints within
    // a tick of the new game coming up.
    fs::remove(fs::path(p.dir) / "bin" / "livedbg.bin", logEc);
    fs::remove(fs::path(p.dir) / "bin" / "livedbg.cmd", logEc);
    // Live Logic: the fresh build compiles every graph natively again, so
    // a leftover patch would make the game interpret a stale program.
    fs::remove(fs::path(p.dir) / "bin" / "livelogic.bin", logEc);

    // Without "Host Filesystem" the ELF boots but every host: fopen fails,
    // so Tyra asserts on the first asset load. PCSX2 rewrites its ini on
    // exit - fix the setting now, right after the instance was killed.
    const fs::path ini = pcsx2::findIni(exe);
    if (ini.empty()) {
        appendLine("[editor] PCSX2.ini not found - if asset loading fails, enable "
                   "Settings > Emulation > 'Enable Host Filesystem' in PCSX2.");
    } else {
        switch (pcsx2::ensureHostFs(ini)) {
            case pcsx2::HostFsResult::Enabled:
                appendLine("[editor] Enabled 'Host Filesystem' in PCSX2 config (needed for host: asset loading).");
                break;
            case pcsx2::HostFsResult::WriteFailed:
                appendLine("[editor] Could not update " + ini.string() + " - enable "
                           "Settings > Emulation > 'Enable Host Filesystem' in PCSX2 manually.");
                break;
            case pcsx2::HostFsResult::AlreadyEnabled:
                break;
        }
        // Keyboard & mouse preference: point PCSX2's emulated USB ports at
        // the host keyboard/pointer so the ps2kbd/ps2mouse drivers the game
        // loads see real devices. Only touched while the preference is on.
        if (p.settings.keyboardMouse) {
            switch (pcsx2::ensureUsbKbdMouse(ini)) {
                case pcsx2::HostFsResult::Enabled:
                    appendLine(
                        "[editor] Configured PCSX2 USB ports for keyboard & "
                        "mouse (USB1 = keyboard, USB2 = mouse).");
                    break;
                case pcsx2::HostFsResult::WriteFailed:
                    appendLine("[editor] Could not update " + ini.string() +
                               " - set USB Port 1 to 'HID Keyboard' and USB "
                               "Port 2 to 'HID Mouse' in PCSX2 manually.");
                    break;
                case pcsx2::HostFsResult::AlreadyEnabled:
                    break;
            }
        }
    }

    appendLine("[editor] Launching PCSX2: " + exe);
    std::string cmd = "\"" + exe + "\" -elf \"" + p.elfPath() + "\"";

    STARTUPINFOA si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    std::string mutableCmd = cmd;
    BOOL ok = CreateProcessA(nullptr, mutableCmd.data(), nullptr, nullptr, FALSE,
                             DETACHED_PROCESS, nullptr, nullptr, &si, &pi);
    if (!ok) {
        appendLine("[editor] Failed to launch PCSX2.");
        return false;
    }
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return true;
}

// The PS2 deploy tools ship in the repo (tools/...); the editor exe lives in
// build/, so probe upward from the exe for the tools folder. Returns "" when
// the tool is nowhere to be found.
static std::string findTool(const char* relUnderTools) {
    char exe[MAX_PATH];
    if (GetModuleFileNameA(nullptr, exe, MAX_PATH)) {
        fs::path dir = fs::path(exe).parent_path();
        for (int up = 0; up < 3; up++) {
            fs::path candidate = dir / "tools" / relUnderTools;
            std::error_code ec;
            if (fs::exists(candidate, ec)) return candidate.string();
            dir = dir.parent_path();
        }
    }
    return "";
}

// Falls back to PATH so a system-wide ps2client install also works.
static std::string findPs2Client() {
    const std::string tool = findTool("ps2client\\bin\\ps2client.exe");
    return tool.empty() ? "ps2client.exe" : tool;
}

void Runner::killPs2Client() {
    if (HANDLE h = (HANDLE)ps2ClientProc_) {
        ps2ClientProc_ = nullptr;  // before closing - ps2ClientAlive() polls it
        TerminateProcess(h, 1);
        CloseHandle(h);
    }
    // The pump exits once the process (and thus its pipe) is gone.
    if (ps2Pump_.joinable()) ps2Pump_.join();
}

void Runner::stopPs2(const Project& p) {
    if (busy()) return;
    join();
    cancelRequested_ = false;
    state_ = State::Running;
    thread_ = std::thread([this, p] {
        appendLine("[editor] Stopping the game on the PS2...");
        // Kill the file server (ours by handle, strays by name) - the game
        // loses host: - then reset ps2link so the console reboots back into
        // its listening state instead of hanging on dead file handles.
        killPs2Client();
        exec("taskkill /F /IM ps2client.exe 2>nul & exit 0", "");
        if (!p.ps2LinkIp.empty()) {
            const std::string client = findPs2Client();
            exec("\"" + client + "\" -h " + p.ps2LinkIp + " -t 10 reset", "");
            // The SPU2 keeps looping voices and the stalled autodma buffer
            // independently of the IOP, so the reset alone leaves sfx
            // playing. Run the silencer for a moment: it loads audsrv on
            // the fresh IOP and audsrv_init() keys everything off.
            const std::string silencer = findTool("silencer\\silencer.elf");
            if (!silencer.empty()) {
                for (int i = 0; i < 12 && !cancelRequested_; i++) Sleep(250);
                const std::string dir = fs::path(silencer).parent_path().string();
                // Spawn the execee file server DIRECTLY, not via exec() + a
                // "start /B" wrapper. That wrapper let ps2client inherit exec()'s
                // stdout pipe; because the silencer's file server never exits on
                // its own, the pipe never reached EOF and exec()'s read loop
                // blocked forever - hanging Stop at "Executing file host:...elf"
                // and leaving the build stuck Running. Here nothing is inherited,
                // so we just launch, give the silencer a few seconds to load
                // audsrv and reset the SPU, then kill the file server ourselves.
                appendLine("[editor] Silencing the SPU (host:silencer.elf)...");
                std::string cmd = "\"" + client + "\" -h " + p.ps2LinkIp +
                                  " execee host:silencer.elf";
                STARTUPINFOA si{};
                si.cb = sizeof(si);
                PROCESS_INFORMATION pi{};
                if (CreateProcessA(nullptr, cmd.data(), nullptr, nullptr, FALSE,
                                   CREATE_NO_WINDOW, nullptr, dir.c_str(), &si,
                                   &pi)) {
                    for (int i = 0; i < 16 && !cancelRequested_; i++) Sleep(250);
                    TerminateProcess(pi.hProcess, 1);
                    CloseHandle(pi.hProcess);
                    CloseHandle(pi.hThread);
                } else {
                    appendLine("[editor] Failed to start: " + cmd);
                }
                // Belt and suspenders: reap the just-killed server and any stray.
                exec("taskkill /F /IM ps2client.exe 2>nul & exit 0", "");
                appendLine("[editor] SPU silenced; ps2link is listening again.");
            } else {
                appendLine("[editor] ps2link reset - the console is listening "
                           "again (tools/silencer/silencer.elf not found, so "
                           "looping sounds keep playing until the next deploy).");
            }
        }
        state_ = State::Success;
    });
}

void Runner::stopEmulator() {
    if (busy()) return;
    join();
    cancelRequested_ = false;
    state_ = State::Running;
    thread_ = std::thread([this] {
        appendLine("[editor] Stopping PCSX2...");
        exec("taskkill /F /IM pcsx2-qt.exe 2>nul & taskkill /F /IM pcsx2.exe "
             "2>nul & exit 0",
             "");
        state_ = State::Success;
    });
}

bool Runner::deployToPs2(const Project& p) {
    if (p.ps2LinkIp.empty()) {
        appendLine("[editor] No PS2 address configured - set 'PS2 (ps2link) IP' in "
                   "Edit > Preferences.");
        return false;
    }
    std::error_code ec;
    if (!fs::exists(p.elfPath(), ec)) {
        appendLine("[editor] ELF not found: " + p.elfPath() + " - build the project first.");
        return false;
    }
    const std::string client = findPs2Client();
    const std::string binDir = (fs::path(p.dir) / "bin").string();

    // One file server at a time: kill the previous deploy's ps2client (ours
    // by handle, strays by name - a leftover from a crashed editor would hold
    // the TCP 18193 listener and starve this one).
    killPs2Client();
    exec("taskkill /F /IM ps2client.exe 2>nul & exit 0", "");

    // Same as the PCSX2 path: the game appends TYRA_LOG to bin/log.txt over
    // host: (here: over the network); drop the stale file for the Debug window.
    std::error_code logEc;
    fs::remove(fs::path(binDir) / "log.txt", logEc);
    fs::remove(fs::path(binDir) / "livedbg.bin", logEc);
    fs::remove(fs::path(binDir) / "livedbg.cmd", logEc);
    fs::remove(fs::path(binDir) / "livelogic.bin", logEc);

    // ps2link passes execee arguments in a non-standard way that the game's
    // toolchain crt0 does not deliver, so "-ps2link" alone cannot be relied
    // on. This marker is the deploy signal instead: the game probes it over
    // host: before booting the engine (launchPCSX2 deletes it, so emulator
    // runs keep the stock IOP-reset path).
    if (std::ofstream marker(fs::path(binDir) / "ps2link.run"); marker)
        marker << "deployed by TyraX\n";
    else
        appendLine("[editor] Warning: could not write bin/ps2link.run marker.");

    appendLine("[editor] Resetting ps2link at " + p.ps2LinkIp + "...");
    if (exec("\"" + client + "\" -h " + p.ps2LinkIp + " -t 10 reset", binDir) != 0) {
        appendLine("[editor] Could not reach ps2link at " + p.ps2LinkIp +
                   " - is the PS2 on and running PS2LINK.ELF? (Check the IP in "
                   "Edit > Preferences.)");
        return false;
    }
    // ps2link reboots the IOP and reloads itself; give it a moment before the
    // execee connect, or the command lands on a half-initialized listener.
    for (int i = 0; i < 12 && !cancelRequested_; i++) Sleep(250);
    if (cancelRequested_) return false;

    // The execee process is the host: file server for the whole game session -
    // it must outlive this build. -ps2link tells the game to skip the IOP
    // reset that would unload ps2link (see the generated main.cpp). cwd is
    // bin/, so the game's "host:" cwd maps to bin/ exactly like a PCSX2 run.
    appendLine("[editor] Launching on PS2: host:" + p.elfName() + " (assets served from " +
               binDir + ")");

    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    HANDLE readPipe = nullptr, writePipe = nullptr;
    if (!CreatePipe(&readPipe, &writePipe, &sa, 0)) {
        appendLine("[editor] Failed to create pipe");
        return false;
    }
    SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOA si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = writePipe;
    si.hStdError = writePipe;
    si.hStdInput = INVALID_HANDLE_VALUE;
    PROCESS_INFORMATION pi{};
    std::string cmd = "\"" + client + "\" -h " + p.ps2LinkIp + " execee host:" +
                      p.elfName() + " -ps2link";
    BOOL ok = CreateProcessA(nullptr, cmd.data(), nullptr, nullptr, TRUE,
                             CREATE_NO_WINDOW, nullptr, binDir.c_str(), &si, &pi);
    CloseHandle(writePipe);
    if (!ok) {
        CloseHandle(readPipe);
        appendLine("[editor] Failed to start: " + cmd);
        return false;
    }
    CloseHandle(pi.hThread);
    ps2ClientProc_ = pi.hProcess;
    ps2Lines_ = 0;

    // Pump ps2client's output - including the console's printf/TYRA log
    // arriving over UDP 18194 - into the Output panel for the session's
    // lifetime. Reads block until the process dies and the pipe breaks.
    ps2Pump_ = std::thread([this, readPipe] {
        std::string pending;
        char buf[4096];
        DWORD n = 0;
        while (ReadFile(readPipe, buf, sizeof(buf), &n, nullptr) && n > 0) {
            pending.append(buf, n);
            size_t nl;
            while ((nl = pending.find('\n')) != std::string::npos) {
                std::string line = pending.substr(0, nl);
                if (!line.empty() && line.back() == '\r') line.pop_back();
                appendLine("[ps2] " + line);
                ps2Lines_++;
                pending.erase(0, nl + 1);
            }
        }
        if (!pending.empty()) {
            appendLine("[ps2] " + pending);
            ps2Lines_++;
        }
        CloseHandle(readPipe);
    });

    // ps2link commands are UDP fire-and-forget: against a dead IP both reset
    // and execee "succeed" and ps2client waits forever. The only real
    // liveness signal is the console's log output (ps2link narrates every
    // execee over UDP), so wait for the first line before claiming success.
    for (int waited = 0; ps2Lines_ == 0; waited += 250) {
        if (cancelRequested_) {
            killPs2Client();
            return false;
        }
        if (WaitForSingleObject(pi.hProcess, 0) != WAIT_TIMEOUT) {
            appendLine("[editor] ps2client exited during startup - see its output above.");
            killPs2Client();
            return false;
        }
        if (waited >= 15000) {
            appendLine("[editor] No response from " + p.ps2LinkIp + " within 15s - is "
                       "the PS2 on and running PS2LINK.ELF? (Check the IP in Edit > "
                       "Preferences and the firewall rules for ps2client.exe.)");
            killPs2Client();
            return false;
        }
        Sleep(250);
    }
    appendLine("[editor] Game running on PS2. Keep the editor open - it is "
               "serving the game's files over the network.");
    return true;
}

void Runner::worker(Project p, bool build, bool run, bool ps2) {
    bool ok = true;

    if (build) {
        appendLine("[editor] === Build started: " + p.name + " ===");

        // Keep docker files and generated sources in sync with the project
        // data (also migrates projects created with older editor versions).
        if (auto err = project::refreshGenerated(p); !err.empty())
            appendLine("[editor] Warning: could not refresh generated files: " + err);

        // Live Link handshake: drop any stale snapshot (the rebuilt game bakes
        // the current scene state - an old livelink.bin would re-apply
        // pre-build values at boot) and record the as-built structure this
        // build bakes in. The editor streams livelink.bin only while the
        // project still matches bin/livelink.sig (see project.hpp). With the
        // preference off (or a release profile) the poller isn't compiled, so
        // remove the sig too - the editor then shows no session at all.
        {
            std::error_code ec;
            fs::create_directories(fs::path(p.dir) / "bin", ec);
            fs::remove(fs::path(p.dir) / "bin" / "livelink.bin", ec);
            // texture hot-reload manifest: the fresh build re-bakes every
            // texture, so a stale reload list must not replay at boot
            fs::remove(fs::path(p.dir) / "bin" / "livetex.bin", ec);
            if (p.settings.buildProfile == "debug" && p.settings.liveLink) {
                std::ofstream sig(fs::path(p.dir) / "bin" / "livelink.sig",
                                  std::ios::trunc);
                if (sig) sig << project::liveLinkSigFile(p);
            } else {
                fs::remove(fs::path(p.dir) / "bin" / "livelink.sig", ec);
            }
        }

        // Texture bake: res/ -> .res-baked/ (PNG quantization per the project
        // policy; the generated Makefile copies .res-baked next to the ELF).
        if (auto err = texbake::bake(p, [this](const std::string& l) { appendLine(l); });
            !err.empty())
            appendLine("[editor] Warning: texture bake failed: " + err);

        // Old template used a fixed container name shared by all projects;
        // remove such a leftover so `compose up` cannot hit a name conflict.
        exec("docker rm -f tyra-game-compiler 2>nul & exit 0", p.dir);

        appendLine("[editor] Starting docker container (first run may download the Tyra image)...");
        if (exec("docker compose up -d --build", p.dir) != 0) {
            appendLine("[editor] Failed to start docker container. Is Docker Desktop running?");
            ok = false;
        }

        const std::string dc = "docker compose exec -T compiler sh -c ";

        // The Tyra engine is maintained inside the editor repo (vendor/tyra,
        // with the editor's fixes applied directly) and bind-mounted read-only
        // at /engine-src. Sync it into the shared build volume; when anything
        // changed (checksum compare - mounts have unreliable mtimes), rebuild
        // libtyra, force the VU1 microprograms (outside make's dependency
        // tracking) and drop the game ELF so it relinks against the new lib.
        if (ok && exec(dc + "\"test -d /engine-src/engine\"", p.dir) != 0) {
            appendLine("[editor] Engine sources not mounted at /engine-src - the editor "
                       "must run from its repo (vendor/tyra). Recreate the container "
                       "if docker-compose.yml just changed.");
            ok = false;
        }
        if (ok) {
            ok = exec(dc + "\"mkdir -p /tyra/engine && "
                           "cp /engine-src/Makefile.base /tyra/Makefile.base && "
                           // Overlay the custom audsrv (per-channel L/R panning for sound
                           // emitters) over the image's PS2SDK copies, so the engine embeds
                           // this IRX, links this EE lib and compiles against this header
                           // (see vendor/tyra/audsrv-pan/README.md). Idempotent; reapplied
                           // every build so it survives container rebuilds. The md5 stamp
                           // (kept outside the rsync target) forces a libtyra rebuild when
                           // the vendored IRX changes, so the new one gets re-embedded.
                           "cp /engine-src/audsrv-pan/audsrv.irx /usr/local/ps2dev/ps2sdk/iop/irx/audsrv.irx && "
                           "cp /engine-src/audsrv-pan/libaudsrv.a /usr/local/ps2dev/ps2sdk/ee/lib/libaudsrv.a && "
                           "cp /engine-src/audsrv-pan/audsrv.h /usr/local/ps2dev/ps2sdk/ee/include/audsrv.h && "
                           "md5sum /engine-src/audsrv-pan/audsrv.irx > /tmp/audsrv.stamp; "
                           "if ! cmp -s /tmp/audsrv.stamp /tyra/.audsrv-stamp 2>/dev/null; then "
                           // obj/irx/audsrv.o must go too: the .irx-em make rule depends only
                           // on the .irx-em file, not the IRX binary it embeds, so without
                           // this bin2s never re-runs and the OLD irx stays inside libtyra.
                           "rm -f /tyra/engine/bin/libtyra.a /tyra/engine/obj/irx/audsrv.o; "
                           "cp /tmp/audsrv.stamp /tyra/.audsrv-stamp; fi && "
                           "rsync -rlci --delete --exclude=obj --exclude=bin "
                           "/engine-src/engine/ /tyra/engine/ "
                           "| grep -v '^.d' > /tmp/engine-sync.txt; "
                           "if [ -s /tmp/engine-sync.txt ] || "
                           "[ ! -f /tyra/engine/bin/libtyra.a ]; then "
                           "echo '[editor] Engine sources changed - rebuilding "
                           "libtyra (takes a minute)...' && "
                           "find /tyra/engine/obj -name '*vu1.o*' -delete 2>/dev/null; "
                           "cd /tyra/engine && make -j$(nproc) && rm -f /src/bin/*.elf; "
                           "fi\"",
                      p.dir) == 0;
            if (!ok) appendLine("[editor] Engine sync/build failed.");
        }

        // Export PS2SDK headers for VS Code IntelliSense (one-time per machine).
        if (ok) {
            if (const char* lad = getenv("LOCALAPPDATA")) {
                fs::path sdkCache = fs::path(lad) / "tyra-editor" / "ps2sdk";
                std::error_code ec;
                if (!fs::exists(sdkCache / "ee" / "include", ec)) {
                    appendLine("[editor] Exporting PS2SDK headers for IntelliSense...");
                    fs::create_directories(sdkCache / "ee", ec);
                    fs::create_directories(sdkCache / "common", ec);
                    // Failure is non-fatal - IntelliSense just has fewer headers.
                    exec("docker compose cp compiler:/usr/local/ps2dev/ps2sdk/ee/include \"" +
                             (sdkCache / "ee" / "include").string() + "\"",
                         p.dir);
                    exec("docker compose cp "
                         "compiler:/usr/local/ps2dev/ps2sdk/common/include \"" +
                             (sdkCache / "common" / "include").string() + "\"",
                         p.dir);
                }
            }
        }

        if (ok) {
            appendLine("[editor] Syncing sources into container...");
            ok = exec(dc + "\"rsync -ac --delete --exclude=.git --exclude=.vscode "
                           "--exclude=obj --exclude=bin /host/ /src/\"",
                      p.dir) == 0;
        }

        if (ok) {
            appendLine("[editor] Compiling (PS2DEV toolchain)...");
            ok = exec(dc + "\"cd /src && make\"", p.dir) == 0;
        }

        // Sound effects: res/sfx/*.wav -> bin/sfx/*.adpcm (PS2SDK adpenc).
        // Skipped per file when the .adpcm is already newer than its .wav.
        // IFS= disables word splitting so filenames with spaces survive the
        // per-file loop. Inner double-quotes around $f/$o would be cleaner but
        // cannot survive the cmd.exe /S + docker.exe argv unquoting (see exec);
        // single quotes would block the expansion we need, so empty IFS it is.
        // Globs cover two levels of sfx subfolders (res/sfx/steps/wood.wav);
        // an unmatched glob stays a literal word, which the -e test skips.
        if (ok) {
            ok = exec(dc + "\"cd /src && IFS= && for f in res/sfx/*.wav "
                           "res/sfx/*/*.wav res/sfx/*/*/*.wav; do "
                           "[ -e $f ] || continue; "
                           "o=${f%.wav}.adpcm && o=bin/${o#res/} && "
                           "mkdir -p $(dirname $o); "
                           "if [ ! $o -nt $f ]; then "
                           "echo [editor] adpenc $f && adpenc $f $o || exit 1; "
                           "fi; done\"",
                      p.dir) == 0;
            if (!ok) appendLine("[editor] Sound conversion (adpenc) failed.");
        }

        // The Makefile's resources step (`cp -r res/*`) also drops the source
        // WAVs into bin/sfx next to the adpenc output. The game only loads
        // the .adpcm, so the WAV copies are dead weight that would bloat the
        // exported ISO - drop them. Orphaned .adpcm (source WAV deleted from
        // res/sfx, e.g. removed in the Sounds panel) must go too: the game
        // volume keeps them forever otherwise and the copy-back rsync
        // resurrects them on the host after every build.
        if (ok)
            exec(dc + "\"cd /src && rm -f bin/sfx/*.wav bin/sfx/*/*.wav "
                      "bin/sfx/*/*/*.wav; IFS= && for o in bin/sfx/*.adpcm "
                      "bin/sfx/*/*.adpcm bin/sfx/*/*/*.adpcm; do "
                      "[ -e $o ] || continue; s=res/${o#bin/} && "
                      "s=${s%.adpcm}.wav; [ -e $s ] || rm -f $o; done\"",
                 p.dir);

        if (ok) {
            appendLine("[editor] Copying binaries back to host...");
            ok = exec(dc + "\"rsync -zac --include=*/ --include=bin/** --exclude=* "
                           "/src/ /host/\"",
                      p.dir) == 0;
        }

        // Per-track music build conversion (Music panel "PS2 build"): the
        // copy-back just refreshed bin/audio from the untouched res/ source,
        // so re-convert the copy the game actually streams. Lower rate/mono
        // halve the byte rate - the levers when a track stutters on a real
        // console over the network deploy.
        if (ok) {
            for (const auto& [rel, opt] : p.musicBuild) {
                if (opt.rate == 0 && !opt.mono) continue;
                std::string binRel = rel;
                if (binRel.rfind("res/", 0) == 0) binRel = binRel.substr(4);
                const fs::path wav = fs::path(p.dir) / "bin" / fs::path(binRel);
                std::error_code ec;
                if (!fs::exists(wav, ec)) continue;
                std::string err;
                if (wavconvert::convertTo16(wav, opt.rate, err, opt.mono))
                    appendLine("[editor] music: " + binRel + " -> 16-bit " +
                               (opt.rate ? std::to_string(opt.rate) + " Hz" : "source rate") +
                               (opt.mono ? " mono" : "") + " (build copy only)");
                else
                    appendLine("[editor] WARNING: music build conversion failed for " +
                               binRel + ": " + err);
            }
        }

        if (ok) {
            // The in-container rm of the copied source WAVs never reaches the
            // host (the copy-back rsync has no --delete), so stale WAVs pile
            // up in bin/sfx and would ship in exported ISOs - drop them here.
            // While at it, flag ADPCM one-shots that cannot work: sound
            // effects load whole into the 2 MB SPU RAM at boot (music belongs
            // in res/audio, which streams), and over a network PS2 deploy
            // every extra megabyte is roughly a minute of boot time.
            std::error_code ec;
            const fs::path sfx = fs::path(p.dir) / "bin" / "sfx";
            for (fs::recursive_directory_iterator it(sfx, ec), end; it != end;
                 it.increment(ec)) {
                if (!it->is_regular_file(ec)) continue;
                const fs::path& f = it->path();
                if (f.extension() == ".wav") {
                    fs::remove(f, ec);
                } else if (f.extension() == ".adpcm" &&
                           it->file_size(ec) > 1024 * 1024) {
                    appendLine("[editor] WARNING: " +
                               fs::relative(f, p.dir, ec).string() + " is " +
                               std::to_string(it->file_size(ec) / (1024 * 1024)) +
                               " MB - sound effects load whole into the 2 MB "
                               "SPU RAM and slow every PS2 network boot. Long "
                               "tracks belong in Music (streamed), not Sounds.");
                }
            }
        }

        if (ok) {
            // Static models ship as .tmdl (docs/model-pipeline.md), so texbake
            // stops mirroring their .obj - but bin/ is additive (no --delete on
            // the copy-back), and a project built before this would keep an
            // orphaned ASCII copy that still lands in an exported ISO. Drop
            // any bin/ .obj whose .tmdl sits next to it.
            std::error_code ec;
            const fs::path models = fs::path(p.dir) / "bin" / "models";
            for (fs::recursive_directory_iterator it(models, ec), end; it != end;
                 it.increment(ec)) {
                if (!it->is_regular_file(ec) || it->path().extension() != ".obj")
                    continue;
                const fs::path stem = it->path().parent_path() /
                                      it->path().stem();
                bool superseded = fs::exists(fs::path(stem).concat(".tmdl"), ec);
                if (!superseded) {  // "<stem>__ovr<hash>.tmdl" (material override)
                    const std::string pre = it->path().stem().string() + "__ovr";
                    std::error_code sec;
                    for (const auto& s :
                         fs::directory_iterator(it->path().parent_path(), sec)) {
                        if (s.path().extension() != ".tmdl") continue;
                        if (s.path().stem().string().rfind(pre, 0) == 0)
                            superseded = true;
                    }
                }
                if (superseded) fs::remove(it->path(), ec);
            }
        }

        appendLine(ok ? "[editor] === Build OK ==="
                   : cancelRequested_ ? "[editor] === Build CANCELLED ==="
                                      : "[editor] === Build FAILED ===");
    }

    if (ok && run && !cancelRequested_) ok = ps2 ? deployToPs2(p) : launchPCSX2(p);

    state_ = (ok && !cancelRequested_) ? State::Success : State::Failed;
}

bool Runner::ps2ClientAlive() const {
    return ps2ClientProc_ &&
           WaitForSingleObject((HANDLE)ps2ClientProc_, 0) == WAIT_TIMEOUT;
}
