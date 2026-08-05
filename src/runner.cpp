#include "runner.hpp"

#include "isoexport.hpp"
#include "elfsym.hpp"
#include "pcsx2_config.hpp"
#include "platform.hpp"
#include "texbake.hpp"
#include "wavconvert.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace {

// Process names to reap when clearing the field before a launch. The emulator
// is looked up by the basename of whatever path we would actually start, so a
// PCSX2 AppImage or a distro binary is covered as well as the stock names.
std::vector<std::string> emulatorProcessNames(const std::string& exe) {
    std::vector<std::string> names{"pcsx2-qt", "pcsx2"};
    if (!exe.empty()) {
        std::string base = fs::path(exe).filename().string();
        if (!base.empty() && base != "pcsx2-qt" && base != "pcsx2")
            names.push_back(base);
    }
    return names;
}

// Every object built by the VU chain (vclpp -> vcl -> dvp-as), for the two cases
// make cannot see by itself: an #included .i/.h changed, or the assembler itself
// did. Named explicitly because the naming is not uniform - most microprograms
// are *_vu1, the draw-finish helper and the VU0 raytracer kernel are not, and
// leaving those two out is how an included-file change used to rebuild 23 of the
// 25 programs. The .vcl/.vsm intermediates go with them; make regenerates both.
constexpr const char* kPurgeVuObjects =
    "find /tyra/engine/obj \\( -name '*vu1.o*' -o -name 'draw_finish.o*' "
    "-o -name 'vu0_rt_kernel.o*' \\) -delete 2>/dev/null; ";

}  // namespace

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
    const std::string stamp = platform::logTimeStamp();
    std::lock_guard<std::mutex> lock(logMutex_);
    log_ += stamp;
    log_ += line;
    log_ += '\n';
}

void Runner::buildAndRun(const Project& p, bool runEmulator, bool rebuild) {
    if (busy()) return;
    join();
    cancelRequested_ = false;
    state_ = State::Running;
    thread_ = std::thread(&Runner::worker, this, p, true, runEmulator, false, rebuild);
}

void Runner::runEmulatorOnly(const Project& p) {
    if (busy()) return;
    join();
    cancelRequested_ = false;
    state_ = State::Running;
    thread_ = std::thread(&Runner::worker, this, p, false, true, false, false);
}

void Runner::buildAndRunPs2(const Project& p, bool build, bool rebuild) {
    if (busy()) return;
    join();
    cancelRequested_ = false;
    state_ = State::Running;
    thread_ = std::thread(&Runner::worker, this, p, build, true, true, rebuild);
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
        // (POSIX has no such locking, but reaping the strays is right there
        // too: they would otherwise keep serving a half-deleted bin/.)
        killPs2Client();
        {
            std::vector<std::string> names = emulatorProcessNames(lastEmulator_);
            names.push_back("ps2client");
            exec(platform::killByName(names), "");
        }
        // Container game volume (obj + bin). Failure is fine - a stopped
        // container just means there is nothing cached there to clean.
        if (exec("docker compose exec -T compiler sh -c " +
                     platform::shellArg("rm -rf /src/obj /src/bin"),
                 p.dir) != 0)
            appendLine("[editor] Container not running - cleaned the host side only.");

        // Host bin\: per-file, clearing read-only first (remove_all refuses
        // those on Windows), retrying a few times (taskkill returns before
        // the killed process actually releases its handles), and naming the
        // file that stays locked - "Access is denied" alone is undebuggable.
        const fs::path bin = fs::path(p.dir) / "bin";
        std::string stuck;
        for (int attempt = 0; attempt < 4; attempt++) {
            if (attempt) platform::sleepMs(500);
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
    // Kills the whole tree, not just the shell wrapper - the process doing the
    // work is docker/make, and orphaning it would leave the container busy.
    if (execProc_) execProc_->kill();
}

int Runner::exec(const std::string& cmdline, const std::string& cwd) {
    if (cancelRequested_) return -1;
    appendLine("> " + cmdline);

    platform::Process::Options opts;
    opts.cwd = cwd;
    opts.capture = true;
    std::unique_ptr<platform::Process> proc = platform::Process::start(cmdline, opts);
    if (!proc) {
        appendLine("[editor] Failed to start: " + cmdline);
        return -1;
    }
    platform::Process* raw = proc.get();
    {
        std::lock_guard<std::mutex> lock(execProcMutex_);
        execProc_ = std::move(proc);
    }

    std::string line;
    while (raw->readLine(line)) appendLine(line);
    const int code = raw->wait();
    {
        std::lock_guard<std::mutex> lock(execProcMutex_);
        execProc_.reset();
    }
    return code;
}

// Where a PCSX2 install lives is the one thing that is genuinely differently
// SHAPED per OS rather than just differently spelled, so it stays here instead
// of moving into platform.cpp: Windows has two Program Files roots, Linux has
// PATH plus flatpak plus a downloaded AppImage in the usual places.
static std::string findPCSX2() {
    std::error_code ec;
#ifdef _WIN32
    std::vector<fs::path> dirs;
    if (const char* pf = getenv("ProgramFiles")) dirs.push_back(fs::path(pf) / "PCSX2");
    if (const char* pf86 = getenv("ProgramFiles(x86)")) dirs.push_back(fs::path(pf86) / "PCSX2");
    for (const auto& dir : dirs)
        for (const char* exe : {"pcsx2-qt.exe", "pcsx2.exe"}) {
            fs::path p = dir / exe;
            if (fs::exists(p, ec)) return p.string();
        }
#else
    // A packaged install (deb/rpm/AUR) puts one of these on PATH.
    for (const char* exe : {"pcsx2-qt", "pcsx2"})
        if (platform::commandExists(exe)) return exe;
    // Debian/Ubuntu ship emulators in /usr/games, which is on the default PATH
    // but not on every trimmed one - probe it explicitly rather than telling a
    // user with a perfectly normal apt install that PCSX2 was not found.
    std::vector<fs::path> candidates{"/usr/games/pcsx2-qt", "/usr/games/pcsx2",
                                     "/var/lib/flatpak/exports/bin/net.pcsx2.PCSX2"};
    if (const fs::path home = platform::homeDir(); !home.empty()) {
        candidates.push_back(home / ".local" / "share" / "flatpak" / "exports" / "bin" /
                             "net.pcsx2.PCSX2");
        // The AppImage is the upstream-recommended download, and it lands
        // wherever the browser put it - probe the two usual spots by prefix.
        for (const char* dir : {"Applications", "Downloads"}) {
            const fs::path d = home / dir;
            if (!fs::exists(d, ec)) continue;
            for (const auto& e : fs::directory_iterator(d, ec)) {
                const std::string name = e.path().filename().string();
                if (name.rfind("pcsx2", 0) == 0 || name.rfind("PCSX2", 0) == 0)
                    if (e.path().extension() == ".AppImage") candidates.push_back(e.path());
            }
        }
    }
    for (const fs::path& c : candidates)
        if (fs::exists(c, ec)) return c.string();
#endif
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
            appendLine("[editor] PCSX2 not found. Install it or set the emulator "
                       "path in Edit > Preferences.");
        return false;
    }
    lastEmulator_ = exe;
    std::error_code ec;
    if (!fs::exists(p.elfPath(), ec)) {
        appendLine("[editor] ELF not found: " + p.elfPath() + " - build the project first.");
        return false;
    }

    // PCSX2's host: ELF loader silently gives up on a long path: the emulator
    // logs "ELF Loading: ..." and then the EE never reaches "is executing" -
    // a black window with nothing to go on. Measured on PCSX2 with this
    // engine: 145 characters boot, 147 do not (the "host:" prefix puts that
    // right at a 150-byte buffer). Say so instead of letting the user stare
    // at it. Worth checking on every platform, but it bites on Linux first -
    // a home directory plus a deep project tree passes 145 far sooner than
    // C:\Users\<name>\TyraProjects\<project> does.
    constexpr size_t kMaxElfPathChars = 145;
    if (p.elfPath().size() > kMaxElfPathChars) {
        appendLine("[editor] WARNING: the ELF path is " +
                   std::to_string(p.elfPath().size()) + " characters (" +
                   p.elfPath() +
                   ") - PCSX2 will load it and then fail to start the game. Move "
                   "the project somewhere shorter (at most " +
                   std::to_string(kMaxElfPathChars) + " characters up to and "
                   "including bin/<name>.elf).");
    }

    // Kill a previous emulator instance, if any (ignore errors).
    exec(platform::killByName(emulatorProcessNames(exe)), "");

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
    // The time machine (docs/time-machine.md): a leftover livetime.rst would
    // teleport the fresh boot into the last session's world on its first poll,
    // and a stale livetime.bin would read as history that never happened.
    fs::remove(fs::path(p.dir) / "bin" / "livetime.bin", logEc);
    fs::remove(fs::path(p.dir) / "bin" / "livetime.rst", logEc);
    // Remote Pad (docs/remote-pad.md): a leftover state still says "attached"
    // and still holds whatever was held when the last session ended, so the
    // fresh boot would start walking before anyone touched anything.
    fs::remove(fs::path(p.dir) / "bin" / "livepad.bin", logEc);

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
    // (The ELF path is native-separator already: Project::filePath() applies
    // make_preferred, which is what PCSX2 needs - it refuses a boot ELF whose
    // path mixes separators.)
    if (!platform::Process::startDetached(platform::shellArg(exe) + " -elf " +
                                          platform::shellArg(p.elfPath()))) {
        appendLine("[editor] Failed to launch PCSX2.");
        return false;
    }
    return true;
}

// The PS2 deploy tools ship in the repo (tools/...); the editor binary lives
// in build/, so probe upward from it for the tools folder. Returns "" when the
// tool is nowhere to be found.
static std::string findTool(const std::string& relUnderTools) {
    const std::string exe = platform::exePath();
    if (exe.empty()) return "";
    fs::path dir = fs::path(exe).parent_path();
    for (int up = 0; up < 3; up++) {
        fs::path candidate = dir / "tools" / relUnderTools;
        std::error_code ec;
        if (fs::exists(candidate, ec)) return candidate.string();
        dir = dir.parent_path();
    }
    return "";
}

// Falls back to PATH so a system-wide ps2client install also works.
static std::string findPs2Client() {
    const std::string name = std::string("ps2client") + platform::exeSuffix();
    const std::string tool = findTool((fs::path("ps2client") / "bin" / name).string());
    return tool.empty() ? name : tool;
}

void Runner::killPs2Client() {
    std::shared_ptr<platform::Process> proc;
    {
        std::lock_guard<std::mutex> lock(ps2ClientMutex_);
        proc.swap(ps2Client_);  // clear first - ps2ClientAlive() polls it
    }
    if (proc) proc->kill();
    // The pump exits once the process (and thus its pipe) is gone. It holds
    // its own shared_ptr, so the Process outlives this scope until it joins.
    if (ps2Pump_.joinable()) ps2Pump_.join();
}

// Resets ps2link, and reports whatever the console says while it happens -
// WITHOUT treating silence as a verdict.
//
// `ps2client reset` is fire-and-forget UDP, so this used to run a ps2client in
// `listen` mode as a witness and retry the command until something came back.
// That was right when a reset still printed "unmounting" from the IOP side;
// ps2link r4 stopped unmounting anything (there is no reboot there to unmount
// for any more) and narrates the restart on the console's own screen instead,
// so a perfectly good reset now says nothing at all over the network. Reading
// that as "the console is hung" made the editor refuse to deploy to a healthy
// console - a false negative that is much worse than the missing signal it was
// meant to replace, because it blocks the thing it was checking.
//
// So: send it once, keep the listener because its output is genuinely useful
// when there is any, and let the caller's own timeout be the judge of a dead
// console - the execee that follows a deploy already reports one within 15 s.
//
// The caller is expected to have killed the deploy's file server first: a game
// polling host: every frame (Remote Pad, Live Link, the time machine) both
// keeps the IOP busy and holds the tty port this listener needs.
Runner::ResetResult Runner::resetPs2Link(const Project& p) {
    const std::string client = findPs2Client();

    platform::Process::Options opts;
    opts.capture = true;
    std::shared_ptr<platform::Process> listener = platform::Process::start(
        platform::shellArg(client) + " -h " + p.ps2LinkIp + " listen", opts);
    std::atomic<int> heard{0};
    std::thread pump;
    if (listener) {
        pump = std::thread([this, listener, &heard] {
            std::string line;
            while (listener->readLine(line)) {
                appendLine("[ps2] " + line);
                heard++;
            }
        });
    }

    ResetResult out = ResetResult::Sent;
    if (exec(platform::shellArg(client) + " -h " + p.ps2LinkIp + " -t 10 reset",
             "") != 0) {
        out = ResetResult::Failed;
    } else {
        // Long enough to catch anything the console does say, short enough not
        // to be a tax on every deploy. The settle wait belongs to the caller.
        for (int i = 0; i < 8 && heard.load() == 0 && !cancelRequested_; i++)
            platform::sleepMs(250);
        if (heard.load() > 0) out = ResetResult::Answered;
    }

    if (listener) listener->kill();
    if (pump.joinable()) pump.join();
    return out;
}

void Runner::stopPs2(const Project& p) {
    if (busy()) return;
    join();
    cancelRequested_ = false;
    state_ = State::Running;
    thread_ = std::thread([this, p] {
        appendLine("[editor] Stopping the game on the PS2...");
        // The file server goes first: the game loses host: (and stops flooding
        // the IOP with per-frame polls, which is what the reset has to cut
        // through), and the tty port is freed for the listener resetPs2Link
        // runs to hear the console with.
        killPs2Client();
        exec(platform::killByName({"ps2client"}), "");
        if (!p.ps2LinkIp.empty()) {
            if (resetPs2Link(p) == ResetResult::Failed)
                appendLine("[editor] Could not reach ps2link at " + p.ps2LinkIp + ".");
            else
                appendLine("[editor] Reset sent - the console should be back in "
                           "ps2link. (r4 restarts without saying anything over the "
                           "network; it narrates it on the TV.)");
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
        exec(platform::killByName(emulatorProcessNames(lastEmulator_)), "");
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
    // the local tty port and starve this one). It also has to be gone before
    // the reset: see resetPs2Link.
    killPs2Client();
    exec(platform::killByName({"ps2client"}), "");

    // Reset exactly like Stop does, and do not launch into a console that
    // never answered - the execee would just sit there until the 15 s timeout
    // below and report a firewall problem that isn't one.
    appendLine("[editor] Resetting ps2link at " + p.ps2LinkIp + "...");
    if (resetPs2Link(p) == ResetResult::Failed) {
        appendLine("[editor] Could not reach ps2link at " + p.ps2LinkIp +
                   " - is the PS2 on and running PS2LINK.ELF? (Check the IP in "
                   "Edit > Preferences.)");
        return false;
    }

    // Same as the PCSX2 path: the game appends TYRA_LOG to bin/log.txt over
    // host: (here: over the network); drop the stale file for the Debug window.
    std::error_code logEc;
    fs::remove(fs::path(binDir) / "log.txt", logEc);
    fs::remove(fs::path(binDir) / "livedbg.bin", logEc);
    fs::remove(fs::path(binDir) / "livedbg.cmd", logEc);
    fs::remove(fs::path(binDir) / "livelogic.bin", logEc);
    fs::remove(fs::path(binDir) / "livetime.bin", logEc);
    fs::remove(fs::path(binDir) / "livetime.rst", logEc);
    fs::remove(fs::path(binDir) / "livepad.bin", logEc);

    // ps2link passes execee arguments in a non-standard way that the game's
    // toolchain crt0 does not deliver, so "-ps2link" alone cannot be relied
    // on. This marker is the deploy signal instead: the game probes it over
    // host: before booting the engine (launchPCSX2 deletes it, so emulator
    // runs keep the stock IOP-reset path).
    if (std::ofstream marker(fs::path(binDir) / "ps2link.run"); marker)
        marker << "deployed by TyraX\n";
    else
        appendLine("[editor] Warning: could not write bin/ps2link.run marker.");

    // ps2link reboots the IOP, restarts its own image and reloads every IRX
    // before it listens again - the better part of ten seconds on the console,
    // and the tty only comes back with udptty at the end of it. Deploying into
    // the middle of that gets a game that loads and then waits forever on a
    // pad the freshly loaded padman has not finished handshaking (seen on the
    // console: "Curent pad(0,0) status: DISCONNECT" and a frozen Tyra logo).
    for (int i = 0; i < 40 && !cancelRequested_; i++) platform::sleepMs(250);
    if (cancelRequested_) return false;

    // The execee process is the host: file server for the whole game session -
    // it must outlive this build. -ps2link tells the game to skip the IOP
    // reset that would unload ps2link (see the generated main.cpp). cwd is
    // bin/, so the game's "host:" cwd maps to bin/ exactly like a PCSX2 run.
    appendLine("[editor] Launching on PS2: host:" + p.elfName() + " (assets served from " +
               binDir + ")");

    const std::string cmd = platform::shellArg(client) + " -h " + p.ps2LinkIp +
                            " execee host:" + p.elfName() + " -ps2link";
    platform::Process::Options opts;
    opts.cwd = binDir;
    opts.capture = true;
    std::shared_ptr<platform::Process> proc = platform::Process::start(cmd, opts);
    if (!proc) {
        appendLine("[editor] Failed to start: " + cmd);
        return false;
    }
    {
        std::lock_guard<std::mutex> lock(ps2ClientMutex_);
        ps2Client_ = proc;
    }
    ps2Lines_ = 0;

    // Pump ps2client's output - including the console's printf/TYRA log
    // arriving over UDP 18194 - into the Output panel for the session's
    // lifetime. Reads block until the process dies and the pipe breaks. The
    // thread holds its own shared_ptr so the Process cannot be destroyed out
    // from under a blocked read by killPs2Client().
    ps2Pump_ = std::thread([this, proc] {
        std::string line;
        while (proc->readLine(line)) {
            appendLine("[ps2] " + line);
            ps2Lines_++;
        }
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
        if (!proc->running()) {
            appendLine("[editor] ps2client exited during startup - see its output above.");
            killPs2Client();
            return false;
        }
        if (waited >= 15000) {
            appendLine("[editor] No response from " + p.ps2LinkIp + " within 15s - is "
                       "the PS2 on and running PS2LINK.ELF? (Check the IP in Edit > "
                       "Preferences and the firewall/port rules for ps2client.)");
            killPs2Client();
            return false;
        }
        platform::sleepMs(250);
    }
    appendLine("[editor] Game running on PS2. Keep the editor open - it is "
               "serving the game's files over the network.");
    return true;
}

void Runner::worker(Project p, bool build, bool run, bool ps2, bool rebuild) {
    bool ok = true;

    if (build) {
        appendLine(rebuild ? "[editor] === Rebuild started: " + p.name + " ==="
                           : "[editor] === Build started: " + p.name + " ===");

        // A user script in a stale namespace (the usual cause: the project was
        // renamed, or copied from an example and renamed) cannot compile, and
        // the toolchain's own diagnostic for it is forty lines of template noise
        // that never mentions the rename. Stop here instead - unlike the
        // refresh below, whose failures are I/O problems worth warning about and
        // continuing past, this one has no chance of producing a binary.
        if (auto err = project::checkScriptNamespaces(p); !err.empty()) {
            appendLine("[editor] " + err);
            appendLine("[editor] === Build FAILED ===");
            state_ = State::Failed;
            return;
        }

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

        const std::string dc = "docker compose exec -T compiler sh -c ";

        // Old template used a fixed container name shared by all projects;
        // remove such a leftover so `compose up` cannot hit a name conflict.
        exec(platform::quiet("docker rm -f tyra-game-compiler"), p.dir);

        appendLine("[editor] Starting docker container (first run may download the Tyra image)...");
        // Plain `up -d`, not `up -d --build`: measured 0.32 s against 4.05 s,
        // and there is nothing to build any more (the compose file names the
        // stock image - see TPL_COMPOSE). What must NOT be optimised away is
        // the `up` itself, however cheap the container's state makes it look:
        // it is `up` that reconciles a RUNNING container with the compose file,
        // and the compose project name is the project's own name. Two
        // directories holding a project of the same name - a copy, a second
        // worktree - share it, so skipping `up` when a container answers means
        // building into whichever directory the live container happens to have
        // bind-mounted at /host. That produces a green build log and no ELF.
        const std::string up = rebuild ? "docker compose up -d --force-recreate"
                                       : "docker compose up -d";
        if (exec(up, p.dir) != 0) {
            appendLine("[editor] Failed to start docker container. Is Docker Desktop running?");
            ok = false;
        }

        // Rebuild: throw away every incremental shortcut this pipeline takes -
        // the game objects, the compiled engine and its VU1 microprograms - so
        // the next steps rebuild all of it from source. The way out when a
        // build goes wrong in a way an incremental build cannot see.
        if (ok && rebuild) {
            appendLine("[editor] Rebuild: dropping the game objects and the compiled engine...");
            exec(dc + platform::shellArg(
                     "rm -rf /src/obj /src/bin /tyra/engine/obj /tyra/engine/bin"),
                 p.dir);
        }

        // The Tyra engine is maintained inside the editor repo (vendor/tyra,
        // with the editor's fixes applied directly) and bind-mounted read-only
        // at /engine-src. Sync it into the shared build volume; when anything
        // changed (checksum compare - mounts have unreliable mtimes), rebuild
        // libtyra, force the VU1 microprograms (outside make's dependency
        // tracking) and drop the game ELF so it relinks against the new lib.
        if (ok && exec(dc + platform::shellArg("test -d /engine-src/engine"), p.dir) != 0) {
            appendLine("[editor] Engine sources not mounted at /engine-src - the editor "
                       "must run from its repo (vendor/tyra). Recreate the container "
                       "if docker-compose.yml just changed.");
            ok = false;
        }
        if (ok) {
            ok = exec(dc + platform::shellArg(
                          "mkdir -p /tyra/engine && "
                           "cmp -s /engine-src/Makefile.base /tyra/Makefile.base || "
                           "cp /engine-src/Makefile.base /tyra/Makefile.base; "
                           // Overlay the custom audsrv (per-channel L/R panning for sound
                           // emitters) over the image's PS2SDK copies, so the engine embeds
                           // this IRX, links this EE lib and compiles against this header
                           // (see vendor/tyra/audsrv-pan/README.md).
                           //
                           // Both copies are STAMPED, and that is load-bearing rather than
                           // tidy: `cp` sets the destination's mtime to now, PS2SDK headers
                           // reach the compiler through -I (so they are ordinary user
                           // headers that land in the .d files - 16 of this game's 18
                           // translation units list audsrv.h), and re-applying the overlay
                           // unconditionally therefore invalidated almost the whole game on
                           // EVERY build. That, together with the editor rewriting its
                           // generated sources, is why no build here was ever incremental.
                           //
                           // Two stamps for the overlay, because they answer two
                           // different questions (a third, unrelated one for the VU
                           // assembler follows them).
                           // The container-side one guards the files that live in the
                           // IMAGE, so a recreated container has no stamp and re-applies
                           // the overlay. The /tyra one guards the compiled ENGINE in the
                           // shared volume: when the vendored IRX changes, libtyra has to
                           // be relinked so the new one gets re-embedded.
                           "md5sum /engine-src/audsrv-pan/audsrv.irx "
                           "/engine-src/audsrv-pan/libaudsrv.a "
                           "/engine-src/audsrv-pan/audsrv.h > /tmp/audsrv.stamp; "
                           "if ! cmp -s /tmp/audsrv.stamp /usr/local/ps2dev/.audsrv-stamp 2>/dev/null; then "
                           "echo '[editor] Applying the vendored audsrv overlay...' && "
                           "cp /engine-src/audsrv-pan/audsrv.irx /usr/local/ps2dev/ps2sdk/iop/irx/audsrv.irx && "
                           "cp /engine-src/audsrv-pan/libaudsrv.a /usr/local/ps2dev/ps2sdk/ee/lib/libaudsrv.a && "
                           "cp /engine-src/audsrv-pan/audsrv.h /usr/local/ps2dev/ps2sdk/ee/include/audsrv.h && "
                           "cp /tmp/audsrv.stamp /usr/local/ps2dev/.audsrv-stamp; fi; "
                           "if ! cmp -s /tmp/audsrv.stamp /tyra/.audsrv-stamp 2>/dev/null; then "
                           // obj/irx/audsrv.o must go too: the .irx-em make rule depends only
                           // on the .irx-em file, not the IRX binary it embeds, so without
                           // this bin2s never re-runs and the OLD irx stays inside libtyra.
                           "rm -f /tyra/engine/bin/libtyra.a /tyra/engine/obj/irx/audsrv.o; "
                           "cp /tmp/audsrv.stamp /tyra/.audsrv-stamp; fi; "
                           // Third stamp, same idea, for the VU chain: WHICH ASSEMBLER built
                           // the microcode is a build input, and nothing else here can see it.
                           // Two implementations of `vcl` exist now (Sony's prebuilt VCL and
                           // the from-source openvcl, plus the flags the image's wrapper
                           // passes it - docs/toolchain-image.md), and swapping the toolchain
                           // image touches no engine source, so every check below says
                           // "nothing changed" and the PREVIOUS image's microcode is relinked.
                           // That is not a slow build, it is a wrong one: three consecutive
                           // A/B probes booted the same VU objects and produced three
                           // identical screenshots. md5 of the resolved binaries covers both
                           // forms - the legacy symlink resolves to the 32-bit vcl, the
                           // openvcl form is a wrapper script whose text carries its flags.
                           // Unquoted on purpose: no double quotes may appear in these
                           // commands (platform::shellArg - cmd.exe cannot pass them), and
                           // none of the three paths has a space in it.
                           "md5sum $(readlink -f $(command -v vcl)) "
                           "$(readlink -f $(command -v vclpp)) > /tmp/vcl.stamp 2>/dev/null; "
                           "if ! cmp -s /tmp/vcl.stamp /tyra/.vcl-stamp 2>/dev/null; then "
                           "echo '[editor] VU assembler changed - rebuilding the "
                           "microprograms (takes a minute or two)...'; " +
                           std::string(kPurgeVuObjects) +
                           "rm -f /tyra/engine/bin/libtyra.a; "
                           "cp /tmp/vcl.stamp /tyra/.vcl-stamp; fi; "
                           "rsync -rlci --delete --exclude=obj --exclude=bin "
                           "/engine-src/engine/ /tyra/engine/ "
                           "| grep -v '^.d' > /tmp/engine-sync.txt; "
                           "if [ -s /tmp/engine-sync.txt ] || "
                           "[ ! -f /tyra/engine/bin/libtyra.a ]; then "
                           "echo '[editor] Engine sources changed - rebuilding "
                           "libtyra...' && "
                           // The VU1 microprograms sit outside make's dependency
                           // tracking (a .vclpp #includes .i/.h files nothing
                           // declares), so they are force-rebuilt - but ONLY when
                           // one of those actually changed. Nuking them on every
                           // engine edit meant a one-line change to a .cpp paid
                           // 109 s of vcl (measured, -j6), which is most of what
                           // an engine iteration cost. The extension list is the
                           // complete set the VU sources are built from and can
                           // include (grep the .vclpp/.i files: only .i and .h).
                           "if grep -qE '[.](vclpp|vcl|vsm|i|h)$' /tmp/engine-sync.txt; then "
                           "echo '[editor] VU1 sources changed - rebuilding the "
                           "microprograms (takes a minute or two)...'; " +
                           std::string(kPurgeVuObjects) +
                           "fi; "
                           "cd /tyra/engine && make -j$(nproc) && rm -f /src/bin/*.elf; "
                          "fi"),
                      p.dir) == 0;
            if (!ok) appendLine("[editor] Engine sync/build failed.");
        }

        // Export PS2SDK headers for VS Code IntelliSense (one-time per machine).
        if (ok) {
            if (const fs::path cfg = platform::configDir(); !cfg.empty()) {
                fs::path sdkCache = cfg / "ps2sdk";
                std::error_code ec;
                if (!fs::exists(sdkCache / "ee" / "include", ec)) {
                    appendLine("[editor] Exporting PS2SDK headers for IntelliSense...");
                    fs::create_directories(sdkCache / "ee", ec);
                    fs::create_directories(sdkCache / "common", ec);
                    // Failure is non-fatal - IntelliSense just has fewer headers.
                    exec("docker compose cp compiler:/usr/local/ps2dev/ps2sdk/ee/include " +
                             platform::shellArg((sdkCache / "ee" / "include").string()),
                         p.dir);
                    exec("docker compose cp "
                         "compiler:/usr/local/ps2dev/ps2sdk/common/include " +
                             platform::shellArg((sdkCache / "common" / "include").string()),
                         p.dir);
                }
            }
        }

        if (ok) {
            appendLine("[editor] Syncing sources into container...");
            // No -c (checksum): it used to be needed because the editor rewrote
            // every generated file on every build, so mtimes lied - but hashing
            // res/ and .res-baked/ end to end on every build is O(assets), and
            // project::writeFile no longer touches a file whose content is
            // unchanged. Size+mtime is the honest comparison again.
            ok = exec(dc + platform::shellArg(
                          "rsync -a --delete --exclude=.git --exclude=.vscode "
                          "--exclude=obj --exclude=bin /host/ /src/"),
                      p.dir) == 0;
        }

        if (ok) {
            appendLine("[editor] Compiling (PS2DEV toolchain)...");
            // -j like the engine build above: the game is a dozen-odd
            // translation units and every one of them parses the whole engine
            // header set (96s -> 54s on examples/showcase at -j6).
            ok = exec(dc + platform::shellArg("cd /src && make -j$(nproc)"), p.dir) == 0;
        }

        // Sound effects: res/sfx/*.wav -> bin/sfx/*.adpcm (PS2SDK adpenc).
        // Skipped per file when the .adpcm is already newer than its .wav.
        // IFS= disables word splitting so filenames with spaces survive the
        // per-file loop. Inner double-quotes around $f/$o would be cleaner but
        // cannot survive the cmd.exe /S + docker.exe argv unquoting (see exec);
        // single quotes would block the expansion we need, so empty IFS it is.
        // (platform::shellArg is what keeps the $-expansions below for the
        // CONTAINER's shell - without it /bin/sh empties them on the host.)
        // Globs cover two levels of sfx subfolders (res/sfx/steps/wood.wav);
        // an unmatched glob stays a literal word, which the -e test skips.
        if (ok) {
            ok = exec(dc + platform::shellArg(
                          "cd /src && IFS= && for f in res/sfx/*.wav "
                           "res/sfx/*/*.wav res/sfx/*/*/*.wav; do "
                           "[ -e $f ] || continue; "
                           "o=${f%.wav}.adpcm && o=bin/${o#res/} && "
                           "mkdir -p $(dirname $o); "
                           "if [ ! $o -nt $f ]; then "
                           "echo [editor] adpenc $f && adpenc $f $o || exit 1; "
                          "fi; done"),
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
            exec(dc + platform::shellArg(
                     "cd /src && rm -f bin/sfx/*.wav bin/sfx/*/*.wav "
                      "bin/sfx/*/*/*.wav; IFS= && for o in bin/sfx/*.adpcm "
                      "bin/sfx/*/*.adpcm bin/sfx/*/*/*.adpcm; do "
                      "[ -e $o ] || continue; s=res/${o#bin/} && "
                      "s=${s%.adpcm}.wav; [ -e $s ] || rm -f $o; done"),
                 p.dir);

        if (ok) {
            appendLine("[editor] Copying binaries back to host...");
            // --chown: the container is root, and on a plain Linux Docker that
            // means every file it writes into the bind-mounted project comes
            // out root-owned - the user cannot then delete their own bin/
            // without sudo. Docker Desktop maps ownership itself, so the flag
            // is empty (and omitted) there.
            // No -z: both ends of this copy are the same machine (a docker
            // volume and a bind mount), so compressing the stream only spends
            // CPU. -c stays - this one lands on the host bind mount, whose
            // timestamp granularity is not ours to trust (Docker Desktop), and
            // bin/ is what actually ships.
            const std::string owner = platform::containerFileOwner();
            ok = exec(dc + platform::shellArg(
                          "rsync -ac --include=*/ --include=bin/** --exclude=* " +
                          (owner.empty() ? std::string() : "--chown=" + owner + " ") +
                          "/src/ /host/"),
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

        // Every release build audits itself: the devkit (Live Link / Live
        // Debugger / Live Logic) claims to cost a shipped game nothing, and a
        // claim that is never checked rots. This reads the ELF that was just
        // produced and says so in the build log, with the real numbers - see
        // docs/devkit.md and `--audit-release`.
        if (ok && p.settings.buildProfile == "release") {
            const elfsym::Audit audit = elfsym::auditRelease(p.elfPath());
            appendLine("[editor] " + audit.summary());
            for (const elfsym::AuditFinding& f : audit.findings)
                appendLine("[editor]   leaked: " + f.what + " (" + f.where + ")");
        }

        appendLine(ok ? "[editor] === Build OK ==="
                   : cancelRequested_ ? "[editor] === Build CANCELLED ==="
                                      : "[editor] === Build FAILED ===");
    }

    if (ok && run && !cancelRequested_) ok = ps2 ? deployToPs2(p) : launchPCSX2(p);

    state_ = (ok && !cancelRequested_) ? State::Success : State::Failed;
}

bool Runner::ps2ClientAlive() const {
    std::shared_ptr<platform::Process> proc;
    {
        std::lock_guard<std::mutex> lock(ps2ClientMutex_);
        proc = ps2Client_;
    }
    return proc && proc->running();
}
