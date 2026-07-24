#pragma once

#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "project.hpp"

// Executes the Tyra build & run pipeline in a worker thread:
//   docker compose up -d --build   (once, keeps container alive)
//   rsync host -> /src, make, rsync bin -> host   (inside container)
//   launch PCSX2 with the produced ELF, or deploy to a real PS2 over the
//   network (ps2client execee to a console running ps2link)
class Runner {
public:
    enum class State { Idle, Running, Success, Failed };

    ~Runner();

    void buildAndRun(const Project& p, bool runEmulator);
    void runEmulatorOnly(const Project& p);
    // Network deploy to a PS2 running ps2link at p.ps2LinkIp: reset ps2link,
    // then `ps2client execee host:<name>.elf -ps2link` with cwd = bin/, so the
    // game boots on the console with assets served from this PC. The ps2client
    // process stays alive as the host: file server (killed on the next deploy
    // or editor exit - the game on the console dies with it) and its output,
    // including the console's printf log, streams into the Output panel.
    void buildAndRunPs2(const Project& p, bool build);
    // Stops the game running on the console: kills the ps2client file server
    // and resets ps2link, so the PS2 reboots back into its listening state.
    void stopPs2(const Project& p);
    // Closes a running PCSX2 instance (by process name - the editor does not
    // keep the emulator's handle after launch). Best-effort and instant; a
    // no-op when nothing is running.
    void stopEmulator();
    // Builds <project>/<name>.iso from bin/ (see isoexport.hpp for layout).
    void exportIso(const Project& p);
    // Builds <project>/<name>-esr.iso: the same disc made ESR-compatible for a
    // modded PS2 (UDF bridge + ESR patch, see esrudf.hpp / isoexport::buildEsr).
    void exportEsrIso(const Project& p);
    // VS-style Clean: wipes the build products - obj/ and bin/ in the
    // container's game volume plus the host bin/ mirror. The next build
    // recompiles the game from scratch (the shared engine volume stays).
    void clean(const Project& p);
    // VS-style Cancel: kills the currently running build step and stops the
    // worker at the next step boundary. A compile already dispatched into
    // the container may finish there in the background - harmless, the next
    // build just finds warm objects.
    void cancel();

    State state() const { return state_.load(); }
    bool busy() const { return state_.load() == State::Running; }
    // True while the ps2client file server from the last PS2 deploy is alive
    // (i.e. the game on the console still has its host: filesystem).
    bool ps2ClientAlive() const;

    // Copies the whole log (called every frame by the UI; log is small).
    std::string log() const;
    void clearLog();

    // Absolute path to the PCSX2 console log (emulog.txt) for the emulator this
    // project would launch (configured path first, then auto-detect). Empty when
    // the emulator or its PCSX2.ini can't be located. Used by the Debug window.
    std::string emulatorLogPath(const Project& p) const;

private:
    void appendLine(const std::string& line);
    // Runs a command through cmd.exe in `cwd`, streams output to log.
    // Returns process exit code, or -1 on spawn failure.
    int exec(const std::string& cmdline, const std::string& cwd);
    bool launchPCSX2(const Project& p);
    bool deployToPs2(const Project& p);
    void killPs2Client();
    void worker(Project p, bool build, bool run, bool ps2);
    void join();

    std::thread thread_;
    std::atomic<State> state_{State::Idle};
    mutable std::mutex logMutex_;
    std::string log_;

    // Cancel support: exec() parks the running child's HANDLE here so
    // cancel() can terminate it; the mutex orders terminate vs CloseHandle.
    std::atomic<bool> cancelRequested_{false};
    std::mutex execProcMutex_;
    void* execProc_ = nullptr;  // HANDLE of the exec() child (guarded above)

    // The long-lived `ps2client execee` file server of the last PS2 deploy
    // (see buildAndRunPs2) and the thread pumping its output into the log.
    void* ps2ClientProc_ = nullptr;  // HANDLE; void* keeps windows.h out of here
    std::thread ps2Pump_;
    // Output lines received from ps2client since the last deploy. ps2link
    // commands are UDP fire-and-forget (reset/execee "succeed" against a dead
    // IP), so the console's first log line is the only real liveness signal.
    std::atomic<int> ps2Lines_{0};
};
