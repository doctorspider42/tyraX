#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>

#include "app.hpp"
#include "project.hpp"
#include "runner.hpp"

// Headless helper: tyra-editor.exe --new <name> <parentDir> [width] [depth] [orbit|fpp]
static int createFromCli(int argc, char** argv) {
    if (argc < 4) {
        std::fprintf(stderr,
                     "usage: tyra-editor --new <name> <parentDir> [width] [depth] [orbit|fpp]\n");
        return 2;
    }
    TerrainConfig t;
    if (argc > 4) t.width = std::atoi(argv[4]);
    if (argc > 5) t.depth = std::atoi(argv[5]);
    const char* gameTemplate = argc > 6 ? argv[6] : "orbit";

    Project p;
    std::string err = project::create(p, argv[2], argv[3], t, gameTemplate);
    if (!err.empty()) {
        std::fprintf(stderr, "error: %s\n", err.c_str());
        return 1;
    }
    std::printf("created: %s (terrain %dx%d)\n", p.dir.c_str(), p.terrain.width,
                p.terrain.depth);
    return 0;
}

// Headless helper: tyra-editor.exe --build <projectDir> [--run]
static int buildFromCli(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: tyra-editor --build <projectDir> [--run]\n");
        return 2;
    }
    const bool run = argc > 3 && std::strcmp(argv[3], "--run") == 0;

    Project p;
    std::string err = project::load(p, argv[2]);
    if (!err.empty()) {
        std::fprintf(stderr, "error: %s\n", err.c_str());
        return 1;
    }

    Runner runner;
    runner.buildAndRun(p, run);
    size_t printed = 0;
    auto flushLog = [&] {
        std::string log = runner.log();
        if (log.size() > printed) {
            std::fwrite(log.data() + printed, 1, log.size() - printed, stdout);
            std::fflush(stdout);
            printed = log.size();
        }
    };
    while (runner.busy()) {
        flushLog();
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
    flushLog();
    return runner.state() == Runner::State::Success ? 0 : 1;
}

int main(int argc, char** argv) {
    if (argc > 1 && std::strcmp(argv[1], "--new") == 0) return createFromCli(argc, argv);
    if (argc > 1 && std::strcmp(argv[1], "--build") == 0) return buildFromCli(argc, argv);

    App app;
    return app.run(argc > 1 ? argv[1] : "");
}
