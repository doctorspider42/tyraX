#include "runner.hpp"

#include "isoexport.hpp"
#include "pcsx2_config.hpp"
#include "texbake.hpp"

#include <cstdlib>
#include <filesystem>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace fs = std::filesystem;

Runner::~Runner() { join(); }

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
    std::lock_guard<std::mutex> lock(logMutex_);
    log_ += line;
    log_ += '\n';
}

void Runner::buildAndRun(const Project& p, bool runEmulator) {
    if (busy()) return;
    join();
    state_ = State::Running;
    thread_ = std::thread(&Runner::worker, this, p, true, runEmulator);
}

void Runner::runEmulatorOnly(const Project& p) {
    if (busy()) return;
    join();
    state_ = State::Running;
    thread_ = std::thread(&Runner::worker, this, p, false, true);
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

int Runner::exec(const std::string& cmdline, const std::string& cwd) {
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
                       " - check the path in Project > Preferences.");
        else
            appendLine("[editor] PCSX2 not found in Program Files. Install it or set the "
                       "emulator path in Project > Preferences.");
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
    // stale file so the Debug window shows only this run's log.
    std::error_code logEc;
    fs::remove(fs::path(p.dir) / "bin" / "log.txt", logEc);

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

void Runner::worker(Project p, bool build, bool run) {
    bool ok = true;

    if (build) {
        appendLine("[editor] === Build started: " + p.name + " ===");

        // Keep docker files and generated sources in sync with the project
        // data (also migrates projects created with older editor versions).
        if (auto err = project::refreshGenerated(p); !err.empty())
            appendLine("[editor] Warning: could not refresh generated files: " + err);

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
        // exported ISO - drop them.
        if (ok)
            exec(dc + "\"cd /src && rm -f bin/sfx/*.wav bin/sfx/*/*.wav "
                      "bin/sfx/*/*/*.wav\"",
                 p.dir);

        if (ok) {
            appendLine("[editor] Copying binaries back to host...");
            ok = exec(dc + "\"rsync -zac --include=*/ --include=bin/** --exclude=* "
                           "/src/ /host/\"",
                      p.dir) == 0;
        }

        appendLine(ok ? "[editor] === Build OK ===" : "[editor] === Build FAILED ===");
    }

    if (ok && run) ok = launchPCSX2(p);

    state_ = ok ? State::Success : State::Failed;
}
