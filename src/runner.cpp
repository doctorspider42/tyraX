#include "runner.hpp"

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

bool Runner::launchPCSX2(const Project& p) {
    const std::string exe = findPCSX2();
    if (exe.empty()) {
        appendLine("[editor] PCSX2 not found in Program Files. Install it or add a custom path.");
        return false;
    }
    std::error_code ec;
    if (!fs::exists(p.elfPath(), ec)) {
        appendLine("[editor] ELF not found: " + p.elfPath() + " - build the project first.");
        return false;
    }

    // Kill a previous emulator instance, if any (ignore errors).
    exec("taskkill /F /IM pcsx2-qt.exe 2>nul & taskkill /F /IM pcsx2.exe 2>nul & exit 0", "");

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

        // Old template used a fixed container name shared by all projects;
        // remove such a leftover so `compose up` cannot hit a name conflict.
        exec("docker rm -f tyra-game-compiler 2>nul & exit 0", p.dir);

        appendLine("[editor] Starting docker container (first run may download the Tyra image)...");
        if (exec("docker compose up -d --build", p.dir) != 0) {
            appendLine("[editor] Failed to start docker container. Is Docker Desktop running?");
            ok = false;
        }

        const std::string dc = "docker compose exec -T compiler sh -c ";
        if (ok && exec(dc + "\"test -f /tyra/Makefile.base\"", p.dir) != 0) {
            appendLine("[editor] Tyra engine not found - installing into the shared "
                       "volume (one-time step, takes a few minutes)...");
            ok = exec(dc + "\"rm -rf /tyra/* /tyra/.git && git clone --depth 1 "
                           "https://github.com/h4570/tyra.git /tyra && cd /tyra/engine && "
                           "make -j$(nproc)\"",
                      p.dir) == 0;
            if (!ok) appendLine("[editor] Engine installation failed.");
        }

        // Engine patch (idempotent, marker file in the shared volume):
        // zero the hardcoded "crappy guard band" in RenderBBox::clipFrustumCheck.
        // Those fixed world-unit margins reclassify partially-visible geometry
        // as "fully visible" and send it to the fast cull path, where the VU1
        // program drops whole triangles -> holes in the ground near the camera.
        if (ok && exec(dc + "\"test -f /tyra/.tyra-editor-patch-1\"", p.dir) != 0) {
            appendLine("[editor] Patching Tyra engine (clipper guard band) and "
                       "rebuilding it - one-time step, takes a few minutes...");
            ok = exec(dc + "\"sed -i -E 's/(guardBand\\[[0-9]\\]) = -[0-9.]+F;/\\1 = 0.0F;/' "
                           "/tyra/engine/src/renderer/core/3d/bbox/render_bbox.cpp && "
                           "cd /tyra/engine && make -j$(nproc) && "
                           "touch /tyra/.tyra-editor-patch-1\"",
                      p.dir) == 0;
            if (!ok) appendLine("[editor] Engine patch failed.");
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

        // Engine patch v2 (idempotent): fast EE clipper - outcode-based
        // trivial accept/reject + no heap allocations per clip call.
        // Patched sources are generated into <project>/.tyra-engine-patch/
        // and land in /src via the rsync above.
        if (ok && exec(dc + "\"test -f /tyra/.tyra-editor-patch-5\"", p.dir) != 0) {
            appendLine("[editor] Patching Tyra engine (VU1 guard band + fast EE clipper) "
                       "and rebuilding it - one-time step, takes a minute...");
            ok = exec(dc + "\"cp /src/.tyra-engine-patch/planes_clip_algorithm.cpp "
                           "/tyra/engine/src/renderer/core/3d/clipper/"
                           "planes_clip_algorithm.cpp && "
                           "cp /src/.tyra-engine-patch/stapip_clipper.cpp "
                           "/tyra/engine/src/renderer/3d/pipeline/static/core/"
                           "stapip_clipper.cpp && "
                           "cp /src/.tyra-engine-patch/stapip_qbuffer.cpp "
                           "/tyra/engine/src/renderer/3d/pipeline/static/core/"
                           "stapip_qbuffer.cpp && "
                           "cp /src/.tyra-engine-patch/render_bbox.cpp "
                           "/tyra/engine/src/renderer/core/3d/bbox/render_bbox.cpp && "
                           "sh /src/.tyra-engine-patch/apply_vu1_guardband.sh && "
                           "cd /tyra/engine && make -j$(nproc) && "
                           "touch /tyra/.tyra-editor-patch-5\"",
                      p.dir) == 0;
            if (!ok) appendLine("[editor] Engine clipper patch failed.");
        }

        if (ok) {
            appendLine("[editor] Compiling (PS2DEV toolchain)...");
            ok = exec(dc + "\"cd /src && make\"", p.dir) == 0;
        }

        // Sound effects: res/sfx/*.wav -> bin/sfx/*.adpcm (PS2SDK adpenc).
        // Skipped per file when the .adpcm is already newer than its .wav.
        if (ok) {
            ok = exec(dc + "\"cd /src && if ls res/sfx/*.wav >/dev/null 2>&1; then "
                           "mkdir -p bin/sfx && for f in res/sfx/*.wav; do "
                           "o=bin/sfx/$(basename $f .wav).adpcm; "
                           "if [ ! $o -nt $f ]; then "
                           "echo [editor] adpenc $f && adpenc $f $o || exit 1; "
                           "fi; done; fi\"",
                      p.dir) == 0;
            if (!ok) appendLine("[editor] Sound conversion (adpenc) failed.");
        }

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
