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
//   launch PCSX2 with the produced ELF
class Runner {
public:
    enum class State { Idle, Running, Success, Failed };

    ~Runner();

    void buildAndRun(const Project& p, bool runEmulator);
    void runEmulatorOnly(const Project& p);

    State state() const { return state_.load(); }
    bool busy() const { return state_.load() == State::Running; }

    // Copies the whole log (called every frame by the UI; log is small).
    std::string log() const;
    void clearLog();

private:
    void appendLine(const std::string& line);
    // Runs a command through cmd.exe in `cwd`, streams output to log.
    // Returns process exit code, or -1 on spawn failure.
    int exec(const std::string& cmdline, const std::string& cwd);
    bool launchPCSX2(const Project& p);
    void worker(Project p, bool build, bool run);
    void join();

    std::thread thread_;
    std::atomic<State> state_{State::Idle};
    mutable std::mutex logMutex_;
    std::string log_;
};
