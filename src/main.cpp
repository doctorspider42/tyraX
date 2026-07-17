#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>

#include "app.hpp"
#include "project.hpp"
#include "runner.hpp"

// Headless helper:
//   tyrax-editor.exe --new <name> <parentDir> [width] [depth] [empty|fpp]
static int createFromCli(int argc, char** argv) {
    if (argc < 4) {
        std::fprintf(stderr,
                     "usage: tyrax-editor --new <name> <parentDir> [width] [depth] "
                     "[empty|fpp]\n");
        return 2;
    }
    TerrainConfig t;
    if (argc > 4) t.width = std::atoi(argv[4]);
    if (argc > 5) t.depth = std::atoi(argv[5]);
    const char* preset = argc > 6 ? argv[6] : "empty";

    Project p;
    std::string err = project::create(p, argv[2], argv[3], t, preset);
    if (!err.empty()) {
        std::fprintf(stderr, "error: %s\n", err.c_str());
        return 1;
    }
    std::printf("created: %s (terrain %dx%d)\n", p.dir.c_str(), p.scenes[0].terrain.width,
                p.scenes[0].terrain.depth);
    return 0;
}

// Headless helper: tyrax-editor.exe --build <projectDir> [--run | --run-ps2 [ip]]
static int buildFromCli(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr,
                     "usage: tyrax-editor --build <projectDir> [--run | --run-ps2 [ip]]\n");
        return 2;
    }
    const bool run = argc > 3 && std::strcmp(argv[3], "--run") == 0;
    const bool runPs2 = argc > 3 && std::strcmp(argv[3], "--run-ps2") == 0;

    Project p;
    std::string err = project::load(p, argv[2]);
    if (!err.empty()) {
        std::fprintf(stderr, "error: %s\n", err.c_str());
        return 1;
    }
    if (runPs2 && argc > 4) p.ps2LinkIp = argv[4];

    Runner runner;
    if (runPs2)
        runner.buildAndRunPs2(p, true);
    else
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
    if (runner.state() != Runner::State::Success) return 1;

    // A PS2 deploy leaves ps2client running as the game's host: file server -
    // returning would destroy the Runner and cut the game off. Stay alive,
    // relaying the console's log, until the server dies or the user Ctrl+Cs.
    while (runPs2 && runner.ps2ClientAlive()) {
        flushLog();
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
    flushLog();
    return 0;
}

// Headless helper: tyrax-editor.exe --resave <projectDir>
// Loads a project and writes it straight back out. On its own it is a no-op for
// an up-to-date project, but loading runs every format migration (e.g. stamping
// stable object ids on pre-id projects), so this is the one-shot way to migrate
// an existing project to the current on-disk format without opening the GUI -
// handy for batch-migrating a team's projects before they switch to the
// merge-friendly workflow.
static int resaveFromCli(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: tyrax-editor --resave <projectDir>\n");
        return 2;
    }
    Project p;
    if (std::string err = project::load(p, argv[2]); !err.empty()) {
        std::fprintf(stderr, "error: %s\n", err.c_str());
        return 1;
    }
    if (std::string err = project::save(p); !err.empty()) {
        std::fprintf(stderr, "error: %s\n", err.c_str());
        return 1;
    }
    if (std::string err = project::saveHeights(p); !err.empty()) {
        std::fprintf(stderr, "error: %s\n", err.c_str());
        return 1;
    }
    if (std::string err = project::saveSplat(p); !err.empty()) {
        std::fprintf(stderr, "error: %s\n", err.c_str());
        return 1;
    }
    std::printf("resaved: %s\n", p.dir.c_str());
    return 0;
}

int main(int argc, char** argv) {
    if (argc > 1 && std::strcmp(argv[1], "--new") == 0) return createFromCli(argc, argv);
    if (argc > 1 && std::strcmp(argv[1], "--build") == 0) return buildFromCli(argc, argv);
    if (argc > 1 && std::strcmp(argv[1], "--resave") == 0) return resaveFromCli(argc, argv);

    App app;
    return app.run(argc > 1 ? argv[1] : "");
}
