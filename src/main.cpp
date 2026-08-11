#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <algorithm>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "aichat.hpp"
#include "aigen.hpp"
#include "aisupport.hpp"
#include "blss.hpp"  // the neural upscaler's headless trainer / eval / emitter
#include "blss_ui.hpp"     // the measured speed model (fill::) + speedFrom()
#include "blsscorpus.hpp"  // blss::measureCoverage - the overdraw counter
#include "devsession.hpp"
#include "editorcfg.hpp"
#include "elfsym.hpp"
#include "gibake.hpp"
#include "livedbg.hpp"
#include "livepad.hpp"
#include "uiscript.hpp"
#include "vucap.hpp"
#include "vuasm.hpp"
#include "vugen.hpp"
#include "vushader.hpp"
#include "vusim.hpp"
#include "app.hpp"
#include "migrations.hpp"
#include "platform.hpp"
#include "procbake.hpp"
#include "project.hpp"
#include "runner.hpp"

// tyrax-editor.exe --debug-state
//
// "What is this machine debugging right now?" - the question anyone (a person
// coming back to a session, or an assistant with a shell) has to answer before
// they can look at anything. Without it, finding the live project means
// guessing at paths: the editor keeps its projects wherever the user put them.
//
// Sources, in order of how current they are: editor.ini's recent list (entry 0
// is the last project OPENED - the list is rewritten at open time), then the
// debug artifacts each project's bin/ holds, dated. A running game is even more
// current, but that is a process query, not a file one - see docs/devkit.md.
// Age, not a wall-clock stamp: no timezone, no format, and it answers the
// actual question ("is this from this session or from last week?").
static std::string ageText(long long secs) {
    char buf[64];
    if (secs < 90)
        std::snprintf(buf, sizeof(buf), "%llds ago", (long long)secs);
    else if (secs < 5400)
        std::snprintf(buf, sizeof(buf), "%lldm ago", (long long)(secs / 60));
    else if (secs < 172800)
        std::snprintf(buf, sizeof(buf), "%lldh ago", (long long)(secs / 3600));
    else
        std::snprintf(buf, sizeof(buf), "%lldd ago", (long long)(secs / 86400));
    return buf;
}

static long long fileAgeSecs(const std::filesystem::path& p) {
    std::error_code ec;
    const auto t = std::filesystem::last_write_time(p, ec);
    if (ec) return -1;
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::filesystem::file_time_type::clock::now() - t)
        .count();
}

// Reports one bin/ artifact and returns its age in seconds (-1 = absent), so
// the caller can name the freshest thing on the machine.
static long long reportDebugArtifact(const std::filesystem::path& dir,
                                     const char* name,
                                     const std::string& summary) {
    const auto p = dir / "bin" / name;
    std::error_code ec;
    const auto size = std::filesystem::file_size(p, ec);
    if (ec) {
        std::printf("    %-12s -\n", name);
        return -1;
    }
    const long long age = fileAgeSecs(p);
    std::printf("    %-12s %8llu B  %-9s  %s\n", name, (unsigned long long)size,
                age < 0 ? "?" : ageText(age).c_str(), summary.c_str());
    return age;
}

// One project's debug artifacts. Returns the age of its freshest one.
static long long reportProjectState(const std::filesystem::path& dir,
                                    bool verbose) {
    namespace fs = std::filesystem;
    // vucap.bin is the one worth decoding inline: its header alone says which
    // draw of which frame the user is staring at.
    std::string vuLine;
    if (fs::exists(dir / "bin" / "vucap.bin")) {
        vucap::Capture c;
        if (vucap::load((dir / "bin" / "vucap.bin").string(), c)) {
            char b[192];
            std::snprintf(b, sizeof(b),
                          "frame %u, flush %d/%d, %d mesh(es), %d tris in, "
                          "%dx%d",
                          c.frame, c.flushIndex, c.flushCount,
                          (int)c.meshes.size(), c.inputTris(), c.renderWidth,
                          c.renderHeight);
            vuLine = b;
        } else {
            vuLine = c.error;
        }
    }
    std::string dbgLine;
    if (fs::exists(dir / "bin" / "livedbg.bin")) {
        livedbg::Snapshot s;
        if (livedbg::readSnapshot((dir / "bin" / "livedbg.bin").string(), s)) {
            char b[128];
            std::snprintf(b, sizeof(b), "frame %u, scene %d%s", s.frame, s.scene,
                          s.halted ? ", HALTED" : "");
            dbgLine = b;
        }
    }
    long long best = -1;
    auto keep = [&best](long long age) {
        if (age >= 0 && (best < 0 || age < best)) best = age;
    };
    keep(reportDebugArtifact(dir, "livedbg.bin", dbgLine));
    keep(reportDebugArtifact(dir, "vucap.bin", vuLine));
    keep(reportDebugArtifact(dir, "log.txt", "the game's own TYRA_LOG output"));
    keep(reportDebugArtifact(dir, "crash.txt", "a crash report is waiting"));
    if (verbose && !vuLine.empty())
        std::printf("    -> tyrax-editor --dump-vucap \"%s\"\n",
                    dir.string().c_str());
    return best;
}

// A game running RIGHT NOW is the most current source of all, and it is a
// process query rather than a file one - so ask it here instead of printing a
// PowerShell incantation for the reader to run (which is what this used to do,
// and which said nothing at all about the ps2link half).
//
// The two processes worth naming are the ones the editor itself starts and the
// ones it must not kill blindly: the emulator carries `-elf <projectDir>/bin/
// <name>.elf`, and the ps2link file server carries `-h <console>` and
// `execee host:<name>.elf`. That command line IS the identity the Runner now
// decides ownership by (see runner.cpp), so printing it makes the decision
// checkable from a shell - "whose ps2client is that" was previously answerable
// only by hand.
static void reportRunningGames() {
    std::printf("\nrunning games:\n");
    bool any = false;
    for (const char* name : {"pcsx2-qt", "pcsx2"})
        for (const platform::RunningProcess& p : platform::processesNamed(name)) {
            any = true;
            std::printf("  %-10s pid %-6llu %s\n", name, p.pid,
                        p.commandLine.empty() ? "(command line unreadable)"
                                              : p.commandLine.c_str());
        }
    for (const platform::RunningProcess& p : platform::processesNamed("ps2client")) {
        any = true;
        std::printf("  %-10s pid %-6llu %s\n", "ps2client", p.pid,
                    p.commandLine.empty() ? "(command line unreadable)"
                                          : p.commandLine.c_str());
    }
    if (!any)
        std::printf("  none (no emulator and no ps2link file server on this "
                    "machine)\n");
    else
        std::printf(
            "  The -elf path is <projectDir>/bin/<name>.elf; a ps2client's "
            "-h names the console and\n  its host:<name>.elf names the game it "
            "is serving. Only one ps2client can serve a\n  console at a time, "
            "which is why a deploy refuses rather than killing one it does not "
            "own.\n");
}

static int debugStateFromCli(int argc, char** argv) {
    namespace fs = std::filesystem;
    bool verbose = false;
    std::string only;
    for (int i = 2; i < argc; ++i) {
        if (std::strcmp(argv[i], "--verbose") == 0)
            verbose = true;
        else
            only = argv[i];
    }
    if (!only.empty()) {  // one named project, no searching
        std::printf("%s\n", fs::path(only).string().c_str());
        reportProjectState(only, true);
        return 0;
    }
    // Source 1: running editors. Each publishes a pointer with a heartbeat
    // (devsession.hpp), so this needs no searching and no guessing - and it is
    // the only source that knows about a project which was never opened here
    // before, or a game reached over ps2link (no local emulator process to
    // find). Several editors at once are normal, so all of them are listed.
    const std::vector<devsession::Info> sessions = devsession::list();
    std::vector<std::string> fromSessions;
    if (!sessions.empty()) {
        std::printf("editor sessions (%s):\n", devsession::dir().c_str());
        for (const devsession::Info& s : sessions) {
            const long long age = s.ageSeconds();
            std::printf("  pid %-6d %-9s %s\n", s.pid,
                        s.live() ? "LIVE" : "stale",
                        s.project.empty() ? "(no project open)"
                                          : s.project.c_str());
            std::printf("           heartbeat %s%s%s%s\n",
                        age < 0 ? "never" : ageText(age).c_str(),
                        s.profile.empty() ? "" : ("  profile " + s.profile).c_str(),
                        s.transport.empty() ? ""
                                            : ("  over " + s.transport).c_str(),
                        s.gameLive ? (s.gameHalted ? "  game HALTED" : "  game running")
                                   : "");
            if (!s.project.empty()) fromSessions.push_back(s.project);
        }
    } else {
        std::printf("editor sessions: none published (no editor running)\n");
    }

    const std::string cfg = editorcfg::configPath();
    std::printf("\neditor config: %s%s\n", cfg.c_str(),
                fs::exists(cfg) ? "" : "  (absent - no project opened yet)");

    // Two sources, in this order: what was opened (editor.ini's list, entry 0
    // is the last project opened) and what merely exists where new projects
    // are made - a project created by the CLI, or by an older install, never
    // reaches the recent list, and its bin/ is just as live.
    std::vector<std::string> dirs = fromSessions;  // live editors first
    for (const std::string& d : editorcfg::recentProjects()) {
        bool known = false;
        for (const std::string& k : dirs)
            known = known || fs::path(k).lexically_normal() ==
                                 fs::path(d).lexically_normal();
        if (!known) dirs.push_back(d);
    }
    const size_t opened = dirs.size();
    const std::string defDir = editorcfg::defaultProjectsDir();
    std::error_code ec;
    if (!defDir.empty() && fs::is_directory(defDir, ec))
        for (const auto& e : fs::directory_iterator(defDir, ec)) {
            if (!e.is_directory()) continue;
            bool known = false;
            for (const std::string& d : dirs)
                known = known || fs::path(d).lexically_normal() ==
                                     e.path().lexically_normal();
            if (!known) dirs.push_back(e.path().string());
        }
    if (dirs.empty()) {
        std::printf(
            "\nNothing found: no project has been opened on this machine and "
            "%s holds none.\n",
            defDir.empty() ? "the default projects folder" : defDir.c_str());
        return 0;
    }
    std::string freshest;
    long long freshestAge = -1;
    for (size_t i = 0; i < dirs.size(); ++i) {
        const fs::path dir(dirs[i]);
        const char* tag =
            i < fromSessions.size()
                ? "   <- open in a running editor"
                : (i >= opened ? "   (never opened here)" : "   (recent list)");
        const bool exists = fs::exists(dir);
        std::printf("\n[%zu] %s%s\n", i, dir.string().c_str(),
                    exists ? tag : "   (folder is gone)");
        if (!exists) continue;
        const long long age = reportProjectState(dir, verbose);
        if (age >= 0 && (freshestAge < 0 || age < freshestAge)) {
            freshestAge = age;
            freshest = dir.string();
        }
    }
    if (!freshest.empty())
        std::printf("\nfreshest debug artifact: %s (%s)\n", freshest.c_str(),
                    ageText(freshestAge).c_str());
    reportRunningGames();
    return 0;
}

// Headless helper:
//   tyrax-editor.exe --new <name> <parentDir> [width] [depth]
//                    [empty|fpp|thirdperson] [unitsPerMeter] [--no-terrain]
static int createFromCli(int argc, char** argv) {
    if (argc < 4) {
        std::fprintf(stderr,
                     "usage: tyrax-editor --new <name> <parentDir> [width] [depth] "
                     "[empty|fpp|thirdperson] [unitsPerMeter] [--no-terrain]\n");
        return 2;
    }
    // --no-terrain is a FLAG among positional arguments (the dialog's "Create
    // terrain" checkbox, docs/terrain.md), so it is accepted anywhere and
    // pulled out before the rest is read by position.
    std::vector<const char*> pos;  // pos[0] = argv[4], the first optional
    bool noTerrain = false;
    for (int i = 4; i < argc; ++i) {
        if (std::strcmp(argv[i], "--no-terrain") == 0)
            noTerrain = true;
        else
            pos.push_back(argv[i]);
    }
    TerrainConfig t;
    t.enabled = !noTerrain;
    if (pos.size() > 0) t.width = std::atoi(pos[0]);
    if (pos.size() > 1) t.depth = std::atoi(pos[1]);
    const char* preset = pos.size() > 2 ? pos[2] : "empty";
    // World scale (docs/world-scale.md); 1 unit = 1 m unless asked otherwise.
    const float ups = pos.size() > 3 ? (float)std::atof(pos[3]) : 1.0f;

    Project p;
    std::string err = project::create(p, argv[2], argv[3], t, preset, ups);
    if (!err.empty()) {
        std::fprintf(stderr, "error: %s\n", err.c_str());
        return 1;
    }
    if (p.scenes[0].terrain.enabled)
        std::printf("created: %s (terrain %dx%d, %.3f units/m)\n", p.dir.c_str(),
                    p.scenes[0].terrain.width, p.scenes[0].terrain.depth,
                    p.settings.unitsPerMeter);
    else
        std::printf("created: %s (no terrain, %dx%d world, %.3f units/m)\n",
                    p.dir.c_str(), p.scenes[0].terrain.width,
                    p.scenes[0].terrain.depth, p.settings.unitsPerMeter);
    return 0;
}

// Headless helper: tyrax-editor --build <projectDir> [--run | --run-ps2 [ip]] [--rebuild]
// Bakes every stale Scatter volume into its chunk meshes and saves the result
// (docs/procedural-generation.md). The GUI does this in App::projectForBuild;
// the headless paths need their own call, or an agent-driven build would ship
// whatever the last GUI session happened to bake - or nothing at all.
static void bakeProcedural(Project& p) {
    if (!procbake::anyStale(p)) return;
    const procbake::Report rep = procbake::bakeAll(p, false);
    std::printf("procedural: baked %d volume(s) -> %d chunks, %d instances, "
                "%d triangles\n",
                rep.volumes, rep.chunks, rep.instances, rep.triangles);
    for (const std::string& w : rep.warnings)
        std::printf("procedural: %s\n", w.c_str());
    if (std::string err = project::save(p); !err.empty())
        std::fprintf(stderr, "warning: could not save the baked scene: %s\n",
                     err.c_str());
}

// Shared gate for the headless commands: a project with pending format
// migrations is refused instead of silently and irreversibly rewritten by a
// script/CI - migrating is an explicit act (--migrate, or opening in the GUI).
// Purely additive format gaps pass (nothing to transform); files from a newer
// editor never get here (project::load refuses them).
static bool refuseUnmigrated(const Project& p) {
    if (migrations::stepsFor(p.formatVersionOnDisk).empty()) return false;
    std::fprintf(stderr,
                 "error: project format v%d needs migration to v%d.\n"
                 "Run: tyrax-editor --migrate <projectDir> (a backup is created "
                 "automatically), or open the project in the editor.\n",
                 p.formatVersionOnDisk, version::kFormatVersion);
    return true;
}

static int buildFromCli(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr,
                     "usage: tyrax-editor --build <projectDir> "
                     "[--run | --run-ps2 [ip]] [--rebuild]\n");
        return 2;
    }
    // --rebuild may sit anywhere among the optional arguments, so the flags are
    // scanned rather than read positionally; the first bare word after
    // --run-ps2 is still the console's IP.
    bool run = false, runPs2 = false, rebuild = false;
    std::string ps2Ip;
    for (int i = 3; i < argc; i++) {
        if (std::strcmp(argv[i], "--run") == 0)
            run = true;
        else if (std::strcmp(argv[i], "--run-ps2") == 0)
            runPs2 = true;
        else if (std::strcmp(argv[i], "--rebuild") == 0)
            rebuild = true;
        else if (runPs2 && ps2Ip.empty())
            ps2Ip = argv[i];
    }

    Project p;
    std::string err = project::load(p, argv[2]);
    if (!err.empty()) {
        std::fprintf(stderr, "error: %s\n", err.c_str());
        return 1;
    }
    if (refuseUnmigrated(p)) return 1;
    if (!ps2Ip.empty()) p.ps2LinkIp = ps2Ip;
    bakeProcedural(p);

    Runner runner;
    if (runPs2)
        runner.buildAndRunPs2(p, true, rebuild);
    else
        runner.buildAndRun(p, run, rebuild);
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
// an up-to-date project, but the tolerant loader lifts every legacy shape (e.g.
// stamping stable object ids on pre-id projects), so this refreshes a project
// to the current on-disk format without opening the GUI. Projects with pending
// REGISTERED migration steps (data transforms) are refused - that irreversible
// path is --migrate's job.
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
    if (refuseUnmigrated(p)) return 1;
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

// Headless helper: tyrax-editor.exe --migrate <projectDir>
// The CLI twin of the editor's migration prompt: backs up the format-bearing
// files into _backup/, applies the pending migration steps and rewrites the
// project in the current format. Degrades to a plain resave when the project
// only needs a version stamp. Disk is not touched when a step fails.
// Writes the same set of files as --resave (manifest + heights + splat): a
// migration that persisted less than a resave would DROP the data it skipped.
static int migrateFromCli(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: tyrax-editor --migrate <projectDir>\n");
        return 2;
    }
    Project p;
    if (std::string err = project::load(p, argv[2]); !err.empty()) {
        std::fprintf(stderr, "error: %s\n", err.c_str());
        return 1;
    }
    const auto steps = migrations::stepsFor(p.formatVersionOnDisk);
    if (!steps.empty()) {
        std::string backupDir;
        if (std::string err = migrations::backup(p, p.formatVersionOnDisk, backupDir);
            !err.empty()) {
            std::fprintf(stderr,
                         "error: backup failed, migration aborted (project not "
                         "modified): %s\n", err.c_str());
            return 1;
        }
        std::printf("backup: %s\n", backupDir.c_str());
        for (const auto* m : steps)
            std::printf("migrating: v%d -> v%d: %s\n", m->from, m->from + 1,
                        m->summary);
        if (std::string err = migrations::run(p, p.formatVersionOnDisk);
            !err.empty()) {
            std::fprintf(stderr,
                         "error: cannot migrate (project not modified): %s\n",
                         err.c_str());
            return 1;
        }
        p.formatVersionOnDisk = version::kFormatVersion;
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
    std::printf("migrated: %s (format v%d)\n", p.dir.c_str(),
                version::kFormatVersion);
    return 0;
}

// ---------------------------------------------------------------------------
// AI-agent CLI (docs/ai-tools.md): machine-readable project inspection and
// flow-graph manipulation, so an AI assistant working inside a generated
// project can read and steer it without driving the GUI.
// ---------------------------------------------------------------------------

// Positional args after the fixed ones may name a scene; resolve it (default:
// scene 0) and point `p.activeScene` at it so p.objects() works.
static bool selectScene(Project& p, const char* sceneName) {
    if (!sceneName) return true;
    for (int i = 0; i < (int)p.scenes.size(); ++i)
        if (p.scenes[i].name == sceneName) {
            p.activeScene = i;
            return true;
        }
    std::fprintf(stderr, "error: no scene named '%s'\n", sceneName);
    return false;
}

static int findObject(const Project& p, const char* name) {
    for (int i = 0; i < (int)p.objects().size(); ++i)
        if (p.objects()[i].name == name) return i;
    std::fprintf(stderr, "error: no object named '%s' in scene '%s'\n", name,
                 p.active().name.c_str());
    return -1;
}

// tyrax-editor.exe --list-nodes <projectDir>
// The flow-node catalog (built-ins + the project's custom .flownode nodes) in
// the same prose format the AI system prompt uses.
static int listNodesFromCli(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: tyrax-editor --list-nodes <projectDir>\n");
        return 2;
    }
    Project p;
    if (std::string err = project::load(p, argv[2]); !err.empty()) {
        std::fprintf(stderr, "error: %s\n", err.c_str());
        return 1;
    }
    // The catalog section of the system prompt is exactly the reference an
    // agent needs - print the whole prompt minus nothing: it also documents
    // the JSON schema and link rules --apply-graph validates against.
    std::printf("%s", aigen::systemPrompt(p, -1).c_str());
    return 0;
}

// tyrax-editor.exe --dump <projectDir>
// One-screen JSON summary of the project: scenes, objects, assets, names every
// flow-graph parameter can reference.
static int dumpFromCli(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: tyrax-editor --dump <projectDir>\n");
        return 2;
    }
    Project p;
    if (std::string err = project::load(p, argv[2]); !err.empty()) {
        std::fprintf(stderr, "error: %s\n", err.c_str());
        return 1;
    }
    // The same summary the editor's AI Assistant gets from its project_summary
    // tool (src/aichat.cpp) - one answer to "describe this project to a model",
    // so the CLI and the in-editor assistant cannot describe it differently.
    std::printf("%s\n", aichat::projectSummaryJson(p).c_str());
    return 0;
}

// tyrax-editor.exe --dump-graph <projectDir> <objectName> [sceneName]
// Full-text search over the documentation the editor carries (docs/ai-chat.md) -
// the assistant's search_docs tool from a shell. Useful on its own ("which page
// talks about VRAM residency?") and the way to check the tool without a backend.
static int searchDocsFromCli(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr,
                     "usage: tyrax-editor --search-docs \"<query>\" [page]\n");
        return 2;
    }
    const std::string hits =
        aichat::searchDocs(argv[2], argc > 3 ? argv[3] : std::string());
    if (hits.empty()) {
        std::fprintf(stderr, "no documentation line matches \"%s\"\n", argv[2]);
        return 1;
    }
    std::printf("%s", hits.c_str());
    return 0;
}

// The in-editor assistant's system prompt (docs/ai-chat.md), for the same
// reason --list-nodes prints the generator's: it is the only way to READ what
// the assistant is told - the tool catalog, the documentation index derived from
// docs/*.md, and the live project context - without a backend and without
// clicking. A project argument is optional: with none it prints the
// no-project-open variant, which is what the welcome screen's assistant sees.
static int chatPromptFromCli(int argc, char** argv) {
    Project p;
    aichat::Context ctx;
    if (argc > 2) {
        if (std::string err = project::load(p, argv[2]); !err.empty()) {
            std::fprintf(stderr, "error: %s\n", err.c_str());
            return 1;
        }
        ctx.project = &p;
    }
    // The editor fills these from its own state; the CLI has none, so it prints
    // the prompt for "a project open, nothing selected".
    ctx.windows = App::chatWindowKeys();
    std::printf("%s", aichat::systemPrompt(ctx).c_str());
    return 0;
}

static int dumpGraphFromCli(int argc, char** argv) {
    if (argc < 4) {
        std::fprintf(stderr,
                     "usage: tyrax-editor --dump-graph <projectDir> <objectName> "
                     "[sceneName]\n");
        return 2;
    }
    Project p;
    if (std::string err = project::load(p, argv[2]); !err.empty()) {
        std::fprintf(stderr, "error: %s\n", err.c_str());
        return 1;
    }
    if (!selectScene(p, argc > 4 ? argv[4] : nullptr)) return 1;
    const int idx = findObject(p, argv[3]);
    if (idx < 0) return 1;
    std::printf("%s\n", project::flowGraphToJson(p.objects()[idx].flowGraph).c_str());
    return 0;
}

// tyrax-editor.exe --apply-graph <projectDir> <objectName> <graph.json>
//                  [sceneName] [--append]
// Validates the graph (node types, link pin rules) exactly like the AI path,
// then writes it into the object and saves the project.
static int applyGraphFromCli(int argc, char** argv) {
    if (argc < 5) {
        std::fprintf(stderr,
                     "usage: tyrax-editor --apply-graph <projectDir> <objectName> "
                     "<graph.json> [sceneName] [--append]\n");
        return 2;
    }
    bool append = false;
    const char* sceneName = nullptr;
    for (int i = 5; i < argc; ++i) {
        if (std::strcmp(argv[i], "--append") == 0)
            append = true;
        else
            sceneName = argv[i];
    }
    Project p;
    if (std::string err = project::load(p, argv[2]); !err.empty()) {
        std::fprintf(stderr, "error: %s\n", err.c_str());
        return 1;
    }
    if (refuseUnmigrated(p)) return 1;  // --apply-graph / --ai-graph rewrite the project
    if (!selectScene(p, sceneName)) return 1;
    const int idx = findObject(p, argv[3]);
    if (idx < 0) return 1;

    std::ifstream f(argv[4], std::ios::binary);
    if (!f) {
        std::fprintf(stderr, "error: cannot read %s\n", argv[4]);
        return 1;
    }
    std::ostringstream ss;
    ss << f.rdbuf();

    FlowGraph fg;
    std::string warnings;
    if (std::string err = aigen::parseGraph(ss.str(), fg, &warnings); !err.empty()) {
        std::fprintf(stderr, "error: %s\n", err.c_str());
        return 1;
    }
    if (!warnings.empty()) std::fprintf(stderr, "warning: %s\n", warnings.c_str());
    if (append)
        aigen::appendGraph(p.objects()[idx].flowGraph, fg);
    else
        p.objects()[idx].flowGraph = fg;
    if (std::string err = project::save(p); !err.empty()) {
        std::fprintf(stderr, "error: %s\n", err.c_str());
        return 1;
    }
    std::printf("applied: %zu nodes, %zu links -> \"%s\"\n", fg.nodes.size(),
                fg.links.size(), argv[3]);
    return 0;
}

// tyrax-editor.exe --refresh-gen <projectDir>
// Regenerates the editor-owned game sources from the current templates without
// building - lets an agent see codegen results (and IntelliSense configs)
// without Docker.
static int refreshGenFromCli(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: tyrax-editor --refresh-gen <projectDir>\n");
        return 2;
    }
    Project p;
    if (std::string err = project::load(p, argv[2]); !err.empty()) {
        std::fprintf(stderr, "error: %s\n", err.c_str());
        return 1;
    }
    // Gated like --build: bakeProcedural saves the project when a Scatter
    // volume is stale, so this command can rewrite the manifest too.
    if (refuseUnmigrated(p)) return 1;
    bakeProcedural(p);
    if (std::string err = project::refreshGenerated(p); !err.empty()) {
        std::fprintf(stderr, "error: %s\n", err.c_str());
        return 1;
    }
    std::printf("refreshed generated files: %s\n", p.dir.c_str());
    return 0;
}

// Bakes global illumination for every scene (docs/global-illumination.md) into
// .res-baked/gi/, then refreshes the generated files so the probe table and
// the lightmap flags follow immediately. The GUI's Tools > Bake Global
// Illumination runs the same gibake::bakeScene on a worker thread; this is the
// headless twin - it is what a build server or a test harness uses, and it is
// how the bake gets verified without clicking anything.
static int bakeGiFromCli(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: tyrax-editor --bake-gi <projectDir>\n");
        return 2;
    }
    Project p;
    if (std::string err = project::load(p, argv[2]); !err.empty()) {
        std::fprintf(stderr, "error: %s\n", err.c_str());
        return 1;
    }
    if (!p.settings.giEnabled) {
        std::fprintf(stderr,
                     "error: global illumination is off for this project "
                     "(Preferences > Lighting)\n");
        return 1;
    }
    const std::atomic<bool> never{false};
    for (int si = 0; si < (int)p.scenes.size(); ++si) {
        const auto t0 = std::chrono::steady_clock::now();
        const gibake::Bake b = gibake::bakeScene(p, si, &never, nullptr);
        if (!b.valid) {
            std::fprintf(stderr, "error: bake failed for scene %d\n", si);
            return 1;
        }
        if (!gibake::write(gibake::cachePath(p, si), b)) {
            std::fprintf(stderr, "error: cannot write the bake for scene %d\n", si);
            return 1;
        }
        const double secs =
            std::chrono::duration<double>(std::chrono::steady_clock::now() - t0)
                .count();
        std::printf("baked GI: %s (atlas %d, terrain %d, probes %dx%dx%d) %.1fs\n",
                    p.scenes[si].name.c_str(), b.atlas.size, b.terrain.size,
                    b.probes.dim[0], b.probes.dim[1], b.probes.dim[2], secs);
    }
    if (std::string err = project::refreshGenerated(p); !err.empty()) {
        std::fprintf(stderr, "error: %s\n", err.c_str());
        return 1;
    }
    return 0;
}

// tyrax-editor --blss-coverage <projectDir> [--frames N] [--raster N]
//                                           [--threads N] [--out WxH] [--verbose]
//
// HOW MUCH FILL A PROJECT ASKS THE GS FOR, headless - the speed half of "should
// I turn BLSS on", which until now existed ONLY as a button in the Neural
// Upscaler window.
//
// That is why this verb exists, and the reason is not convenience. The hardware
// calibration on 2026-08-09 (docs/profiling.md) worked back from five measured
// load points to `examples/upscaler-lab`'s true fill and wanted to compare it
// against what the estimator says - and could not, because the estimator was
// reachable only by clicking a button in a GUI. So the round that MEASURED the
// model had to quote `kAnchorCoverages` as recorded rather than re-derive it,
// and wrote down that whoever owns the estimator should print its own figure
// before rescaling anything. A number nobody can re-run is a number nobody can
// check; this makes the estimate as falsifiable as `--blss-eval`'s tables.
//
// IN-PROCESS like the window's button, and for the same reason the window gives:
// `blss::measureCoverage` IS the public API here, there is nothing in an
// anonymous namespace to re-implement, and it takes about a second - so unlike
// --blss-train/--blss-eval there is no second answer to keep honest.
//
// The output is script-parseable: every line a tool should read starts `[blss]`
// and is `key=value` pairs, the same shape `--blss-eval`'s verdict line uses.
// The human table underneath is what makes those lines falsifiable.
static int blssCoverageFromCli(int argc, char** argv) {
    if (argc < 3 || argv[2][0] == '-') {
        std::fprintf(stderr,
                     "usage: tyrax-editor --blss-coverage <projectDir> [--frames N] "
                     "[--raster N] [--threads N] [--out WxH] [--verbose]\n");
        return 2;
    }
    blss::CoverageConfig cfg;
    cfg.projectDir = argv[2];
    // The PROJECT'S own raster by default, so the coverages are per the frame
    // the console will present rather than per a nominal 512x448 - the same
    // resolution App::blssStartCoverage passes, or the verb and the window
    // would answer slightly different questions.
    // ...and the project's own RECONSTRUCTION, for the same reason: plain mode
    // (ProjectSettings::blssNetwork false) pays a seventh of the neural EE bill,
    // so the verdict below is against a break-even four times lower. A CLI that
    // priced every project as neural would tell a plain project at 5 coverages
    // to leave the feature off when it is a clear win.
    bool network = true;
    {
        Project p;
        if (project::load(p, argv[2]).empty()) {
            const DisplayModeInfo& dm =
                project::displayModeInfo(project::bootDisplayMode(p.settings));
            cfg.outW = dm.bufW;
            cfg.outH = dm.halfHeight ? dm.logicalH / 2 : dm.logicalH;
            network = p.settings.blssNetwork;
        }
    }
    for (int i = 3; i < argc; ++i) {
        const std::string a = argv[i];
        const auto next = [&](int def) {
            return i + 1 < argc ? std::atoi(argv[++i]) : def;
        };
        if (a == "--frames") cfg.framesPerShot = std::max(1, next(cfg.framesPerShot));
        else if (a == "--raster") cfg.raster = std::max(32, next(cfg.raster));
        else if (a == "--threads") cfg.threads = std::max(0, next(0));
        else if (a == "--verbose") cfg.verbose = true;
        else if (a == "--out" && i + 1 < argc) {
            int w = 0, h = 0;
            if (std::sscanf(argv[++i], "%dx%d", &w, &h) == 2 && w > 0 && h > 0)
                cfg.outW = w, cfg.outH = h;
        } else {
            std::fprintf(stderr, "blss: unknown argument '%s'\n", a.c_str());
            return 2;
        }
    }

    const auto t0 = std::chrono::steady_clock::now();
    const blss::CoverageReport rep = blss::measureCoverage(cfg, nullptr);
    const double secs =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    if (!rep.ok) {
        std::fprintf(stderr, "blss: %s\n",
                     rep.err.empty() ? "the coverage estimate produced nothing" : rep.err.c_str());
        return 1;
    }

    std::printf("blss: %d scene(s), %zu shot(s), %d frame(s), %zu triangle(s), %d emitter(s) "
                "/ %d billboard(s) at %dx%d output, %d raster, %.1fs\n",
                rep.scenes, rep.shots.size(), rep.frames, rep.triangles, rep.emitters,
                rep.billboards, cfg.outW, cfg.outH, cfg.raster, secs);
    std::printf("\n  %-14s %-16s %-8s %8s %8s %8s %7s\n", "scene", "camera move", "kind",
                "geometry", "emitters", "total", "worst");
    for (const blss::CoverageShot& s : rep.shots)
        std::printf("  %-14.14s %-16.16s %-8.8s %8.2f %8.2f %8.2f %7.2f\n", s.scene.c_str(),
                    s.name.c_str(), s.move.c_str(), s.geom, s.emit, s.geom + s.emit, s.peak);
    std::printf("  %-14s %-16s %-8s %8.2f %8.2f %8.2f %7.2f\n", "", "-- all shots --", "",
                rep.geomMean, rep.emitMean, rep.mean, rep.p95);

    // The derived verdict, from the SAME pure function the window draws
    // (blssui::speedFrom) rather than arithmetic restated here - a CLI with its
    // own copy of the model is exactly the second answer this feature has spent
    // its whole life avoiding.
    // Priced at the raster the coverages were counted per, which the report
    // echoes back: one full-screen pass is per PIXEL, so a 512x512 project pays
    // 14.3 % more per coverage than a 512x448 one and its break-even is 11.4
    // rather than 13.1. The single scalar this replaces was a 576i measurement
    // quoted at every resolution.
    const blssui::SpeedEstimate sp =
        blssui::speedFrom(rep.mean, (double)rep.outW * rep.outH, network);
    const char* band = sp.band == blssui::SpeedEstimate::Band::Win      ? "win"
                       : sp.band == blssui::SpeedEstimate::Band::Marginal ? "marginal"
                                                                          : "loss";
    std::printf("\nblss: %.2f coverages against a %.1f break-even at %dx%d, %s mode -> %s%.2f ms "
                "a frame (%s)\n",
                rep.mean, sp.breakEven, rep.outW, rep.outH, network ? "neural" : "plain",
                sp.savedMs >= 0 ? "+" : "", sp.savedMs, band);
    // The OTHER mode's line, always, because it is one project setting away and
    // the two answers can disagree about the verdict entirely.
    {
        const blssui::SpeedEstimate other =
            blssui::speedFrom(rep.mean, (double)rep.outW * rep.outH, !network);
        std::printf("blss: in %s mode the same scene is a %.1f break-even -> %s%.2f ms (%s)\n",
                    network ? "plain" : "neural", other.breakEven,
                    other.savedMs >= 0 ? "+" : "", other.savedMs,
                    other.band == blssui::SpeedEstimate::Band::Win        ? "win"
                    : other.band == blssui::SpeedEstimate::Band::Marginal ? "marginal"
                                                                          : "loss");
    }
    if (sp.band == blssui::SpeedEstimate::Band::Win)
        std::printf("blss: roughly %.2f-%.2fx, the ends being 'the frame is 60%% fill' and "
                    "'the frame is nothing but fill'\n", sp.lo, sp.hi);
    else if (sp.band == blssui::SpeedEstimate::Band::Marginal)
        std::printf("blss: NO MULTIPLIER IS QUOTED - %.2f ms is smaller than what this count "
                    "admits it cannot see\n", sp.savedMs);

    // What the count could not see. Every one of these can only make the real
    // figure BIGGER, which is what makes the estimate a floor rather than a
    // guess - and a caller reading the machine lines below is entitled to the
    // same caveats the window prints under its answer.
    std::printf("blss: not counted - the sky dome (~1 more coverage); particle lifetimes and "
                "drift%s%s%s.\n"
                "blss: the HUD, the menus and the post effects are neither counted nor reduced. "
                "A FLOOR, not a measurement.\n",
                rep.sawCutout ? "; alpha-tested cutouts, counted here as solid" : "",
                rep.sawDisabledEmitter ? "; emitters that start disabled" : "",
                rep.sawAnimated ? "" : "; nothing animated was found");

    // --- the machine-readable half -----------------------------------------
    for (const blss::CoverageShot& s : rep.shots)
        std::printf("[blss] coverage shot scene=%s name=%s move=%s geom=%.4f emit=%.4f "
                    "total=%.4f peak=%.4f frames=%d\n",
                    s.scene.c_str(), s.name.c_str(), s.move.c_str(), s.geom, s.emit,
                    s.geom + s.emit, s.peak, s.frames);
    std::printf("[blss] coverage mean=%.4f p95=%.4f geom=%.4f emit=%.4f scenes=%d shots=%zu "
                "frames=%d triangles=%zu emitters=%d billboards=%d cutout=%d animated=%d "
                "disabledEmitter=%d\n",
                rep.mean, rep.p95, rep.geomMean, rep.emitMean, rep.scenes, rep.shots.size(),
                rep.frames, rep.triangles, rep.emitters, rep.billboards, rep.sawCutout ? 1 : 0,
                rep.sawAnimated ? 1 : 0, rep.sawDisabledEmitter ? 1 : 0);
    std::printf("[blss] coverage verdict coverages=%.4f fillMs=%.4f savedMs=%.4f breakEven=%.4f "
                "lo=%.4f hi=%.4f band=%s anchorCoverages=%.4f passMs=%.4f raster=%dx%d "
                "perMpx=%.4f network=%d eeMs=%.4f\n",
                sp.coverages, sp.fillMs, sp.savedMs, sp.breakEven, sp.lo, sp.hi, band,
                blssui::fill::kAnchorCoverages, sp.passMs, rep.outW, rep.outH,
                blssui::fill::kPassMsPerMpx, network ? 1 : 0,
                blssui::fill::eeCostMs(network));
    return 0;
}

// The GUI's AI settings (editor.ini) as CLI defaults, so --ai-graph without
// flags behaves like the editor. Only the ai* keys are read here; the full
// config lives in app.cpp.
static aigen::Config aiConfigFromEditorIni() {
    aigen::Config cfg;
    const std::filesystem::path base = platform::configDir();
    if (base.empty()) return cfg;
    std::ifstream f(base / "editor.ini");
    std::string line;
    while (std::getline(f, line)) {
        if (line.rfind("aiBackend=", 0) == 0) cfg.backend = line.substr(10);
        else if (line.rfind("aiModel=", 0) == 0) cfg.model = line.substr(8);
        else if (line.rfind("aiThinking=", 0) == 0) cfg.thinking = line.substr(11) == "1";
    }
    if (cfg.backend.empty()) cfg.backend = "claude";
    return cfg;
}

// tyrax-editor.exe --ai-graph <projectDir> <objectName> <prompt|prompt-file>
//                  [sceneName] [--backend claude|copilot|openai] [--model m]
//                  [--thinking]
// The whole AI generation pipeline, headless: build the system prompt, run
// the backend, parse/validate the reply, write the graph, save. An existing
// graph goes into the prompt and the model decides from the request whether
// to change, extend or rebuild it - the reply is always the complete
// resulting graph (same behavior as the GUI modal).
static int aiGraphFromCli(int argc, char** argv) {
    if (argc < 5) {
        std::fprintf(stderr,
                     "usage: tyrax-editor --ai-graph <projectDir> <objectName> "
                     "<prompt|prompt-file> [sceneName] [--backend claude|copilot|"
                     "openai] [--model <m>] [--thinking]\n");
        return 2;
    }
    aigen::Config cfg = aiConfigFromEditorIni();
    const char* sceneName = nullptr;
    for (int i = 5; i < argc; ++i) {
        if (std::strcmp(argv[i], "--backend") == 0 && i + 1 < argc)
            cfg.backend = argv[++i];
        else if (std::strcmp(argv[i], "--model") == 0 && i + 1 < argc)
            cfg.model = argv[++i];
        else if (std::strcmp(argv[i], "--thinking") == 0)
            cfg.thinking = true;
        else
            sceneName = argv[i];
    }

    Project p;
    if (std::string err = project::load(p, argv[2]); !err.empty()) {
        std::fprintf(stderr, "error: %s\n", err.c_str());
        return 1;
    }
    if (refuseUnmigrated(p)) return 1;  // --apply-graph / --ai-graph rewrite the project
    if (!selectScene(p, sceneName)) return 1;
    const int idx = findObject(p, argv[3]);
    if (idx < 0) return 1;

    // The prompt argument is a file path when one exists, literal text otherwise.
    std::string prompt = argv[4];
    if (std::error_code ec; std::filesystem::exists(argv[4], ec)) {
        std::ifstream f(argv[4], std::ios::binary);
        std::ostringstream ss;
        ss << f.rdbuf();
        prompt = ss.str();
    }

    const bool hasGraph = !p.objects()[idx].flowGraph.empty();
    std::fprintf(stderr, "[ai] backend=%s model=%s thinking=%d%s\n",
                 cfg.backend.c_str(),
                 cfg.model.empty() ? "(default)" : cfg.model.c_str(),
                 cfg.thinking ? 1 : 0,
                 hasGraph ? " (existing graph in prompt)" : "");
    aigen::Generator gen;
    gen.start(cfg,
              aigen::systemPrompt(p, idx,
                                  hasGraph ? &p.objects()[idx].flowGraph
                                           : nullptr),
              prompt);
    while (gen.busy()) std::this_thread::sleep_for(std::chrono::milliseconds(200));
    if (gen.state() != aigen::Generator::State::Success) {
        std::fprintf(stderr, "error: %s\n", gen.error().c_str());
        return 1;
    }

    FlowGraph fg;
    std::string warnings;
    if (std::string err = aigen::parseGraph(gen.reply(), fg, &warnings);
        !err.empty()) {
        std::fprintf(stderr, "error: %s\nreply:\n%s\n", err.c_str(),
                     gen.reply().c_str());
        return 1;
    }
    if (!warnings.empty()) std::fprintf(stderr, "warning: %s\n", warnings.c_str());
    p.objects()[idx].flowGraph = fg;
    if (std::string err = project::save(p); !err.empty()) {
        std::fprintf(stderr, "error: %s\n", err.c_str());
        return 1;
    }
    std::printf("generated: %zu nodes, %zu links -> \"%s\"\n", fg.nodes.size(),
                fg.links.size(), argv[3]);
    std::printf("%s\n", project::flowGraphToJson(p.objects()[idx].flowGraph).c_str());
    return 0;
}

// tyrax-editor.exe --add-ai-support <projectDir> [claude] [codex] [copilot]
// Installs the AI assistant skills into an existing project (same files the
// "Add AI support" option writes at project creation).
static int aiSupportFromCli(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr,
                     "usage: tyrax-editor --add-ai-support <projectDir> "
                     "[claude] [codex] [copilot]\n");
        return 2;
    }
    bool claude = false, copilot = false, codex = false;
    for (int i = 3; i < argc; ++i) {
        if (std::strcmp(argv[i], "claude") == 0) claude = true;
        if (std::strcmp(argv[i], "codex") == 0) codex = true;
        if (std::strcmp(argv[i], "copilot") == 0) copilot = true;
    }
    if (!claude && !copilot && !codex) claude = true;  // default: Claude
    const std::string status = aisupport::install(argv[2], claude, copilot, codex);
    std::printf("%s\n", status.c_str());
    return status.rfind("error:", 0) == 0 ? 1 : 0;
}

// Headless helper:
//   tyrax-editor.exe --audit-release <projectDir>
//
// The devkit's promise is that a shipped (release) game pays NOTHING for the
// debugging layers - no code, no static arrays, not even the file names they
// use. This checks the claim against the built ELF instead of trusting it: it
// lists any devkit symbol or string that survived, and prints what the binary
// actually costs. Exit code 0 = clean, 1 = something leaked (so it can gate a
// release in a script). See docs/devkit.md.
static int auditReleaseFromCli(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: tyrax-editor --audit-release <projectDir>\n");
        return 2;
    }
    Project p;
    const std::string err = project::load(p, argv[2]);
    if (!err.empty()) {
        std::fprintf(stderr, "error: %s\n", err.c_str());
        return 1;
    }
    const elfsym::Audit a = elfsym::auditRelease(p.elfPath());
    std::printf("%s\n", a.summary().c_str());
    if (!a.error.empty()) return 1;
    if (p.settings.buildProfile != "release")
        std::printf(
            "note: this project's build profile is \"%s\" - a debug build is "
            "SUPPOSED to carry the devkit.\n",
            p.settings.buildProfile.c_str());
    for (const elfsym::AuditFinding& f : a.findings)
        std::printf("  %-52s %s%s\n", f.what.c_str(), f.where.c_str(),
                    f.bytes ? (" (" + std::to_string(f.bytes) + " B)").c_str() : "");
    if (a.clean)
        std::printf(
            "No trace of Live Link / Live Debugger / Live Logic in this "
            "binary.\n");
    return a.clean ? 0 : 1;
}

// Headless helper:
//   tyrax-editor.exe --symbolize <projectDir> <addr> [addr ...]
//
// Turns crash-report addresses into function names and source lines, using the
// PS2 toolchain in the project's build container against the unstripped copy a
// debug build keeps (bin/<name>.elf.sym). See docs/devkit.md.
static int symbolizeFromCli(int argc, char** argv) {
    if (argc < 4) {
        std::fprintf(stderr,
                     "usage: tyrax-editor --symbolize <projectDir> <addr> "
                     "[addr ...]\n"
                     "");
        return 2;
    }
    Project p;
    const std::string err = project::load(p, argv[2]);
    if (!err.empty()) {
        std::fprintf(stderr, "error: %s\n"
        "", err.c_str());
        return 1;
    }
    std::vector<uint32_t> addrs;
    for (int i = 3; i < argc; ++i)
        addrs.push_back((uint32_t)std::strtoul(argv[i], nullptr, 0));
    std::string symErr;
    const auto locs = elfsym::symbolize(p.dir, "bin/" + p.elfName() + ".sym",
                                        addrs, &symErr);
    if (!symErr.empty()) std::fprintf(stderr, "warning: %s\n"
    "", symErr.c_str());
    for (const elfsym::Location& l : locs)
        std::printf("0x%08x  %-40s %s\n"
        "", l.addr,
                    l.func.empty() ? "(unknown)" : l.func.c_str(),
                    l.source.c_str());
    return symErr.empty() ? 0 : 1;
}

// Headless helper:
//   tyrax-editor.exe --pad <projectDir> "<script>" [more...]
//   tyrax-editor.exe --pad <projectDir> --file <script.pad>
//   tyrax-editor.exe --pad <projectDir> --stdin
//
// Holds the controller for a running game (docs/remote-pad.md). This is the
// unattended half of the Remote Pad: it writes bin/livepad.bin, which the game
// polls over the same host: filesystem it loads assets from - so no window
// needs the keyboard focus, which on Windows is the difference between an input
// test that can be scripted and one that needs a human in front of PCSX2.
//
// The script language is livepad::parseScript. Note that a `hold` with no
// `wait` after it does nothing visible: the driver detaches when it exits and
// the game lets go, on purpose - a killed driver must not leave the player
// walking into a wall.
static int padFromCli(int argc, char** argv) {
    auto usage = [] {
        std::fprintf(stderr,
                     "usage: tyrax-editor --pad <projectDir> \"<script>\" "
                     "[more...]\n"
                     "       tyrax-editor --pad <projectDir> --file "
                     "<script.pad>\n"
                     "       tyrax-editor --pad <projectDir> --stdin\n"
                     "\n"
                     "script: press cross [s] | hold up | release up|all |\n"
                     "        stick l|r <x> <y> | wait <s> | neutral | pad 1|2\n"
                     "        (separated by ';' or newlines, '#' comments)\n"
                     "example: --pad myproj \"stick l 0 -127; wait 2; neutral\"\n");
        return 2;
    };
    if (argc < 3) return usage();
    Project p;
    const std::string err = project::load(p, argv[2]);
    if (!err.empty()) {
        std::fprintf(stderr, "error: %s\n", err.c_str());
        return 1;
    }

    std::string text;
    auto append = [&text](const std::string& s) {
        if (!text.empty()) text += "\n";
        text += s;
    };
    for (int i = 3; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--stdin") {
            std::ostringstream ss;
            ss << std::cin.rdbuf();
            append(ss.str());
            continue;
        }
        if (a == "--file") {
            if (i + 1 >= argc) return usage();
            std::ifstream f(argv[++i]);
            if (!f) {
                std::fprintf(stderr, "error: cannot read %s\n", argv[i]);
                return 1;
            }
            std::ostringstream ss;
            ss << f.rdbuf();
            append(ss.str());
            continue;
        }
        append(a);
    }
    if (text.empty()) return usage();

    std::vector<livepad::Step> steps;
    std::string perr;
    if (!livepad::parseScript(text, steps, perr)) {
        std::fprintf(stderr, "error: %s\n", perr.c_str());
        return 2;
    }
    if (steps.empty()) {
        std::fprintf(stderr, "error: nothing to do\n");
        return 2;
    }
    // The commonest reason "nothing happened": the game was never built with
    // the channel in it. Say so up front rather than letting the run look fine.
    if (p.settings.buildProfile != "debug")
        std::fprintf(stderr,
                     "warning: this project's build profile is \"%s\" - a "
                     "release build carries no Remote Pad, so the running game "
                     "will ignore this.\n",
                     p.settings.buildProfile.c_str());
    else if (!p.settings.remotePad)
        std::fprintf(stderr,
                     "warning: the \"Remote Pad\" preference is off for this "
                     "project - the game was built without the channel and "
                     "will ignore this.\n");

    const std::string path =
        (std::filesystem::path(p.dir) / "bin" / "livepad.bin").string();
    // Seed the sequence from the clock: a second driver run must never reuse a
    // number the still-running game already saw, or its first state reads as
    // "nothing changed" and the staleness watchdog starts counting.
    uint32_t seq = (uint32_t)std::chrono::duration_cast<std::chrono::seconds>(
                       std::chrono::system_clock::now().time_since_epoch())
                       .count();
    bool failed = false;
    int lostRefreshes = 0;
    // A write carries either a NEW state (a step: losing it means the game never
    // sees that button) or the same state again (a refresh, one of ~25 per second
    // - losing one costs nothing, because the next is 40 ms away and the game's
    // staleness watchdog is 120 frames). Only the first kind is worth aborting a
    // run for; treating the second as fatal is what let a lost race on Windows
    // (see livepad::write) kill about one 9 s hold in five.
    auto push = [&](const livepad::State& s, bool attached, bool isStep) {
        const std::string e = livepad::write(path, s, ++seq, attached);
        if (e.empty()) return true;
        if (!isStep) {
            if (++lostRefreshes <= 3)
                std::fprintf(stderr, "warning: lost one pad refresh: %s\n",
                             e.c_str());
            return true;
        }
        std::fprintf(stderr, "error: %s\n", e.c_str());
        failed = true;
        return false;
    };

    const auto t0 = std::chrono::steady_clock::now();
    for (const livepad::Step& st : steps) {
        std::printf("[pad] %s\n", st.source.c_str());
        std::fflush(stdout);
        if (!push(st.state, true, true)) break;
        if (st.seconds <= 0.0) continue;
        // Keep refreshing while we hold: the seq is what tells the game we are
        // still here (see livepad::kStaleFrames), and a state written once
        // would expire mid-hold on a long wait.
        const auto until = std::chrono::steady_clock::now() +
                           std::chrono::milliseconds((int)(st.seconds * 1000.0));
        // No break on failure here: a refresh push cannot fail the run (above),
        // and a step that did already broke out of the outer loop.
        while (std::chrono::steady_clock::now() < until) {
            platform::sleepMs(40);
            push(st.state, true, false);
        }
    }
    // Detach: neutral AND flagged gone, so the game drops the overlay on its
    // next poll instead of holding the last state for the watchdog's two
    // seconds. Not worth failing the run over either - the watchdog is the
    // backstop that exists for exactly this.
    push(livepad::State(), false, false);
    const double secs =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - t0)
            .count();
    if (!failed) {
        std::printf("[pad] done - %zu step(s) in %.1fs, pad released", steps.size(),
                    secs);
        // Report them rather than hiding them: a run that had to skip refreshes
        // is still a run whose timing was disturbed.
        if (lostRefreshes > 0)
            std::printf(" (%d refresh write(s) lost to the reader)",
                        lostRefreshes);
        std::printf("\n");
    }
    return failed ? 1 : 0;
}

// Scripted GUI run:
//   tyrax-editor.exe --ui-script [projectDir] "<script>" [more...]
//   tyrax-editor.exe --ui-script [projectDir] --file <script.ui>
//
// Drives the EDITOR without a human (docs/ui-scripting.md). Unlike every other
// entry point here this one RUNS THE GUI - it just holds the mouse and keyboard
// itself, by injecting into ImGui's event queue and resolving targets by widget
// name. Exit code 0 = every step succeeded.
static int uiScriptFromCli(int argc, char** argv) {
    auto usage = [] {
        std::fprintf(
            stderr,
            "usage: tyrax-editor --ui-script [projectDir] \"<script>\" [more...]\n"
            "       tyrax-editor --ui-script [projectDir] --file <script.ui>\n"
            "\n"
            "script: click|rightclick|hover|doubleclick|expect|expect-not <target>\n"
            "        hold <target> [seconds] | drag <target> <dx> <dy>\n"
            "        key <chord> | text <string> | wait <s> | frames <n>\n"
            "        shot <file.png> | dump | log <text> | quit\n"
            "target: \"Window/Label\" or \"Label\" (case-insensitive)\n"
            "example: --ui-script myproj \"click Tools; click \\\"Remote Pad\\\";"
            " shot pad.png\"\n"
            "hint:    a script of just \"dump\" lists every widget on screen\n");
        return 2;
    };
    if (argc < 3) return usage();

    // The project directory is optional (a script may only need the welcome
    // screen), so it is "the first argument, if it looks like a directory".
    int i = 2;
    std::string projectDir;
    if (std::filesystem::is_directory(argv[i]) ||
        std::filesystem::path(argv[i]).extension() == ".tyra") {
        projectDir = argv[i];
        ++i;
    }
    std::string text;
    auto append = [&text](const std::string& s) {
        if (!text.empty()) text += "\n";
        text += s;
    };
    for (; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--file") {
            if (i + 1 >= argc) return usage();
            std::ifstream f(argv[++i]);
            if (!f) {
                std::fprintf(stderr, "error: cannot read %s\n", argv[i]);
                return 1;
            }
            std::ostringstream ss;
            ss << f.rdbuf();
            append(ss.str());
            continue;
        }
        if (a == "--stdin") {
            std::ostringstream ss;
            ss << std::cin.rdbuf();
            append(ss.str());
            continue;
        }
        append(a);
    }
    if (text.empty()) return usage();

    std::vector<uiscript::Step> steps;
    std::string err;
    if (!uiscript::parseScript(text, steps, err)) {
        std::fprintf(stderr, "error: %s\n", err.c_str());
        return 2;
    }
    App app;
    app.setUiScript(steps);
    return app.run(projectDir);
}

// Headless helper:
//   tyrax-editor.exe --dump-vucap <projectDir>
//
// Decodes bin/vucap.bin - the VU1 DMA chain the game handed over - so the
// packet inspector can be checked without the GUI. See docs/devkit.md.
static int dumpVuCapFromCli(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: tyrax-editor --dump-vucap <projectDir>\n");
        return 2;
    }
    Project p;
    const std::string err = project::load(p, argv[2]);
    if (!err.empty()) {
        std::fprintf(stderr, "error: %s\n", err.c_str());
        return 1;
    }
    vucap::Capture cap;
    const std::string path =
        (std::filesystem::path(p.dir) / "bin" / "vucap.bin").string();
    if (!vucap::load(path, cap)) {
        std::fprintf(stderr, "error: %s (%s)\n", cap.error.c_str(), path.c_str());
        return 1;
    }
    std::printf("frame %u, %d quadwords, %d unpacks, %d triangles\n", cap.frame,
                cap.qw, (int)cap.unpacks.size(), cap.triangleCount());
    if (cap.flushIndex >= 0)
        std::printf("bag flush %d of %d this frame; rendering at %dx%d\n",
                    cap.flushIndex, cap.flushCount, cap.renderWidth,
                    cap.renderHeight);
    for (int a : cap.mscal) std::printf("microprogram start: %d\n", a);
    for (const vucap::Step& st : cap.steps)
        std::printf("[%4u] %s\n", st.offsetQw, st.text.c_str());
    // The classification is printed per block: a mesh list that silently drops
    // a stream looks exactly like "the pipeline never sent my model".
    for (size_t i = 0; i < cap.unpacks.size(); ++i) {
        const vucap::Unpack& u = cap.unpacks[i];
        std::printf("unpack %zu: %s x%d -> VU1 %u, %zu words   [%s]\n", i,
                    u.format.c_str(), u.count, u.vuAddr, u.words.size(),
                    u.posNote.empty() ? "POSITIONS" : u.posNote.c_str());
    }
    // One flush carries a whole bag, so the chain holds one position stream per
    // mesh - list them all, not just the biggest (that is what the GUI draws).
    std::printf("meshes in this flush: %zu\n", cap.meshes.size());
    for (size_t i = 0; i < cap.meshes.size(); ++i) {
        const vucap::Mesh& m = cap.meshes[i];
        char prog[48];
        if (m.program >= 0)
            std::snprintf(prog, sizeof(prog), "program @%d", m.program);
        else
            std::snprintf(prog, sizeof(prog), "program carried over (MSCNT)");
        std::printf("  mesh %zu: %d verts (%d tris), %.1f units across, %s, "
                    "unpack %d -> VU1 %u%s\n",
                    i, m.verts, m.tris, m.extent(), prog, m.unpack,
                    cap.unpacks[m.unpack].vuAddr,
                    m.degenerate ? " [DEGENERATE TRIANGLES]" : "");
    }
    if (const std::vector<float>* v = cap.vertices()) {
        std::printf("largest vertex stream: %zu vertices (%d triangles)\n",
                    v->size() / 4, cap.triangleCount());
        for (size_t i = 0; i < v->size() / 4 && i < 4; ++i)
            std::printf("  in v%zu  %.3f %.3f %.3f\n", i, (*v)[i * 4],
                        (*v)[i * 4 + 1], (*v)[i * 4 + 2]);
    }
    if (cap.hasVuMem) {
        std::printf("\nVU1 data memory: captured (1024 qw)\n");
        if (cap.hasMvp) {
            std::printf("MVP (as uploaded, column per quadword):\n");
            for (int r = 0; r < 4; ++r)
                std::printf("  %9.3f %9.3f %9.3f %9.3f\n", cap.mvp[r],
                            cap.mvp[4 + r], cap.mvp[8 + r], cap.mvp[12 + r]);
        }
        std::printf("scales: %.1f %.1f %.1f\n", cap.scale[0], cap.scale[1],
                    cap.scale[2]);
        std::printf("GIF packets staged by the program: %zu (%d GS vertices)\n",
                    cap.gifs.size(), cap.outputVerts());
        for (size_t i = 0; i < cap.gifs.size() && i < 4; ++i) {
            const vucap::GifPacket& g = cap.gifs[i];
            std::printf("  gif %zu @VU1 %d: %s nloop=%d nreg=%d [%s]%s\n", i,
                        g.vuAddr, g.primName().c_str(), g.nloop, g.nreg,
                        g.regs.c_str(), g.eop ? " EOP" : "");
            for (size_t v = 0; v < g.verts.size() && v < 4; ++v) {
                const vucap::GsVertex& gv = g.verts[v];
                std::printf("     out v%zu  x=%.1f y=%.1f z=%u  rgba %u,%u,%u,%u\n",
                            v, gv.px(), gv.py(), gv.z, gv.r, gv.g, gv.b, gv.a);
            }
        }
        // Print the first few output/reference pairs: when the two disagree the
        // pattern (constant offset? sign? scale?) is the diagnosis.
        if (!cap.reference.empty())
            {
                const vucap::GifPacket* g2 = nullptr;
                for (const vucap::GifPacket& c : cap.gifs)
                    if (c.hasGeometry &&
                        (!g2 || c.verts.size() > g2->verts.size()))
                        g2 = &c;
                if (g2) {
                const vucap::GifPacket& g = *g2;
                for (size_t v = 0; v < g.verts.size() && v < 6; ++v) {
                    if (v >= cap.reference.size()) break;
                    const vucap::RefVertex& r = cap.reference[v];
                    std::printf(
                        "   cmp v%zu  out(%.1f, %.1f)  ref(%.1f, %.1f)  refFlip %.1f  clipw %.3f%s\n",
                        v, g.verts[v].px(), g.verts[v].py(), r.x / 16.0f,
                        r.y / 16.0f, r.yFlipped / 16.0f, r.clip[3],
                        r.behind ? "  BEHIND" : "");
                }
                }
            }
        std::printf("input: %d mesh(es), %d triangles; staged in VU1: %d "
                    "triangles (the LAST run(s) only - the output area is "
                    "double buffered), last mesh %d tris, delta %+d\n",
                    (int)cap.meshes.size(), cap.inputTris(), cap.outputTris(),
                    cap.meshes.empty() ? 0 : cap.meshes.back().tris,
                    cap.clipDelta());
        // The findings the GUI paints amber, in the same words.
        if (cap.hasWindow) {
            std::printf("drawing window: x %.0f..%.0f, y %.0f..%.0f (GS plane "
                        "units, %dx%d centred on 2048)\n",
                        cap.winX0, cap.winX1, cap.winY0, cap.winY1,
                        cap.renderWidth, cap.renderHeight);
            std::printf("biggest staged packet spans x %.0f..%.0f, y "
                        "%.0f..%.0f (%d of %d vertices outside the window - "
                        "ordinary, the GS scissors them)\n",
                        cap.gsX0, cap.gsX1, cap.gsY0, cap.gsY1, cap.gsOffWindow,
                        cap.gsVerts);
            if (cap.packetOffscreen)
                std::printf("! that packet MISSES the drawing window entirely "
                            "- none of it can appear on screen\n");
        }
        if (cap.hugeTris)
            std::printf("! %d staged triangle(s) span nearly the whole GS "
                        "plane (vertex at or behind w = 0?)\n",
                        cap.hugeTris);
        if (cap.behindVerts)
            std::printf("! %d input vertices have clip w <= 0 (behind the "
                        "camera)\n",
                        cap.behindVerts);
        if (cap.degenerateTris)
            std::printf("! %d input triangle(s) have no area\n",
                        cap.degenerateTris);
        if (cap.gsZeroAlpha)
            std::printf("! %d staged vertices are fully transparent in a "
                        "packet with no +ABE\n",
                        cap.gsZeroAlpha);
        std::printf(
            "note: one flush can carry SEVERAL meshes, and the MVP in VU1 "
            "memory is the LAST one uploaded - so the host reference is exact "
            "only for a single-mesh flush. Reported, not trusted "
            "(docs/devkit.md).\n");
        if (cap.diffCompared)
            std::printf("host reference diff over %d vertices: max %.1f/%.1f, "
                        "mean %.2f/%.2f (12.4 units; 16 = one pixel), screen Y %s\n",
                        cap.diffCompared, cap.diffMaxX, cap.diffMaxY,
                        cap.diffMeanX, cap.diffMeanY,
                        cap.yFlipped ? "flipped (GS down)" : "up");
        else
            std::printf("host reference diff: nothing lined up 1:1 to compare\n");
    }
    return 0;
}

// The in-tree Tyra engine, whose .vclpp programs the VU framework reads as its
// reference implementation. Same resolution templates.cpp uses for the build
// container's bind mount; an explicit argument always wins.
static std::string vuEngineDir(const char* override) {
    namespace fs = std::filesystem;
    if (override && *override) return override;
    const std::string exe = platform::exePath();
    if (!exe.empty()) {
        std::error_code ec;
        const fs::path candidate =
            fs::path(exe).parent_path() / ".." / "vendor" / "tyra" / "engine";
        if (fs::exists(candidate / "Makefile", ec))
            return fs::weakly_canonical(candidate, ec).string();
    }
    return "vendor/tyra/engine";
}

// The stage-library half of --vu-check.
//
// Two claims, and each stage has to satisfy BOTH or it is not shippable.
//
// 1. IDENTITY AT ZERO. Every stage's strength parameters are bound to per-mesh
//    slots and the mesh asks for nothing; the program must then produce output
//    bit-identical to the engine's own handwritten cull program. That is the
//    contract the whole design rests on - a project's program REPLACES a
//    material class, so it runs on every mesh of that class, and the only thing
//    that makes that acceptable is that a mesh which wants no effect gets
//    exactly the pixels it would have got. "Approximately identical" would mean
//    installing a program silently re-shades the whole scene.
//
// 2. IT DOES SOMETHING. The same program with a non-zero mesh parameter must
//    produce DIFFERENT output. Without this, a stage that quietly folded to
//    nothing would sail through claim 1 with full marks.
//
// The identity direction is the one that catches real bugs: it is an exact
// bit-for-bit comparison against a program nobody generated, over randomized
// vertices, so an off-by-one field mask or a broadcast on the wrong operand
// shows up immediately.
static int vuCheckStages(const std::string& engine) {
    namespace fs = std::filesystem;
    std::printf("-- stage library: identity at zero, and an effect above it --\n");
    int fails = 0;

    // The two handwritten programs a custom one is measured against.
    auto readHand = [&](const vugen::Desc& d, vuir::Program& out) {
        vuasm::Options opt;
        opt.includeRoot = engine;
        std::string err;
        const std::string path =
            (fs::path(engine) / "src" / "renderer" / "3d" / "pipeline" /
             "static" / "core" / "programs" / "cull" / (d.fileStem + ".vclpp"))
                .string();
        return vuasm::parseFile(path, opt, out, err);
    };

    for (const vugen::StageDef& sd : vugen::stageDefs()) {
        const unsigned cls = sd.needsTexture ? (1u << 3) : (1u << 0);
        vugen::Desc d = vugen::descForClass(cls);
        vugen::Stage st = vugen::makeStage(sd.key);
        // Bind every strength to its own mesh slot; leave the rest at the
        // catalogue defaults, which is how someone would actually author it.
        int slot = 0;
        for (int i = 0; i < sd.paramCount; ++i)
            if (sd.params[i].strength && slot < 4) st.params[i].meshSlot = slot++;
        d.stages.push_back(st);
        const vugen::Built b = vugen::build(d);
        if (!b.errors.empty()) {
            std::printf("  %-14s BUILD REFUSED: %s\n", sd.key, b.errors[0].c_str());
            ++fails;
            continue;
        }

        // The reference: the engine's own program for the same base, which the
        // custom one is a stage-weave of.
        vugen::Desc refDesc = sd.needsTexture ? vugen::descCullTextureColor()
                                              : vugen::descCullColor();
        vuir::Program hand;
        if (!readHand(refDesc, hand)) {
            std::printf("  %-14s could not read the reference program\n", sd.key);
            ++fails;
            continue;
        }

        const float zeros[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        const vugen::Equivalence idle =
            vugen::equivalence(hand, b.program, d, 24, 0x51A6E00Du, zeros, 0.0f);
        // Something every strength can be set to that is not zero. The values
        // differ per stage only in magnitude; the point is "not the default".
        const float live[4] = {0.7f, 0.4f, 0.9f, 0.55f};
        const vugen::Equivalence busy =
            vugen::equivalence(hand, b.program, d, 24, 0x51A6E00Du, live, 1.3f);

        const bool ok = idle.identical && !busy.identical;
        std::printf("  %-14s %-9s +%3d instructions   %s\n", sd.key,
                    ok ? "OK" : "FAILED", b.stageInstrs,
                    !idle.identical ? "NOT the identity at zero strength"
                    : busy.identical ? "changes nothing at full strength"
                                     : "");
        if (!idle.identical && !idle.detail.empty())
            std::printf("      %s\n", idle.detail.c_str());
        if (!idle.identical && !idle.error.empty())
            std::printf("      %s\n", idle.error.c_str());
        if (!ok) ++fails;

        // And the emitted text has to behave like the IR it came from - the
        // same round trip the engine programs get, for the same reason.
        vuasm::Options opt;
        vuir::Program back;
        std::string err;
        if (!vuasm::parseText(b.vclpp, std::string(sd.key) + ".vclpp", opt, back,
                              err)) {
            std::printf("      emitted source does not parse: %s\n", err.c_str());
            ++fails;
            continue;
        }
        const vugen::Equivalence rt =
            vugen::equivalence(b.program, back, d, 12, 0x0FF1CE00u, live, 1.3f);
        if (!rt.identical) {
            std::printf("      emitted source does not match the IR: %s%s\n",
                        rt.detail.c_str(), rt.error.c_str());
            ++fails;
        }
    }
    // The same two claims, but across ALL FIVE material classes with a stage
    // made of plain VALUES. This is the path that was wrongly closed: the four
    // per-mesh numbers live in the directional-lights colour block, so a look
    // that BINDS one cannot go on a lit class - but a look of plain values
    // never reads those addresses and may go anywhere. That distinction is the
    // difference between "cell shading works on unlit props" and "cell shading
    // works on the scene", so it gets a check rather than a comment.
    std::printf("  -- a values-only stage, on every class --\n");
    static const struct {
        unsigned bit;
        const char* stem;
    } kClassRef[] = {
        {1u << 0, "stapip_cull_c_vu1"},   {1u << 1, "stapip_cull_d_vu1"},
        {1u << 2, "stapip_cull_td_vu1"},  {1u << 3, "stapip_cull_tc_vu1"},
        {1u << 4, "stapip_cull_tce_vu1"},
    };
    for (const auto& cr : kClassRef) {
        vugen::Desc d = vugen::descForClass(cr.bit);
        vugen::Stage st = vugen::makeStage("posterize");
        st.params[0].value = 4.0f;   // levels
        st.params[1].value = 0.0f;   // strength: the identity, for now
        d.stages.push_back(st);
        vuasm::Options opt;
        opt.includeRoot = engine;
        vuir::Program hand;
        std::string err;
        const std::string path =
            (fs::path(engine) / "src" / "renderer" / "3d" / "pipeline" /
             "static" / "core" / "programs" / "cull" /
             (std::string(cr.stem) + ".vclpp")).string();
        if (!vuasm::parseFile(path, opt, hand, err)) {
            std::printf("    %-22s could not read %s\n",
                        vugen::classTitle(cr.bit), cr.stem);
            ++fails;
            continue;
        }
        const vugen::Built idleB = vugen::build(d);
        const vugen::Equivalence idle =
            vugen::equivalence(hand, idleB.program, d, 16, 0x5AFE0001u);
        d.stages[0].params[1].value = 0.9f;  // and now it must change things
        const vugen::Built liveB = vugen::build(d);
        const vugen::Equivalence busy =
            vugen::equivalence(hand, liveB.program, d, 16, 0x5AFE0001u);
        const bool ok = idle.identical && !busy.identical &&
                        idleB.errors.empty() && liveB.errors.empty();
        std::printf("    %-22s %s\n", vugen::classTitle(cr.bit),
                    ok ? "OK"
                       : !idle.identical ? "NOT the identity at zero"
                                         : "changes nothing at full strength");
        if (!ok) ++fails;
    }

    std::printf("  %s\n\n", fails == 0
                                ? "every stage is a no-op at zero and an effect "
                                  "above it, in the emitted text too"
                                : "FAILED");
    return fails == 0 ? 0 : 1;
}

// The SCRIPT half of --vu-check: a project's own C++ program (src/vushader.hpp),
// built for every class it claims and simulated against the engine's own. The
// point is not that cell shading looks right - the host cannot know that - but
// that the path works end to end without Docker: the body a project writes
// reaches the emitter, the emitter produces a program vcl-shaped source, and it
// draws something DIFFERENT from the stock one while still drawing.
static vu::Program* vuCheckScriptCurrent = nullptr;

static int vuCheckScripts(const std::string& engine) {
    namespace fs = std::filesystem;
    std::printf("-- project scripts: build and simulate --\n");
    int fails = 0;
    (void)engine;
    // EVERY PROGRAM THE GENERATOR WOULD EMIT, not just the cull one. A class is
    // three programs and src/vumain.cpp emits all of them a script can carry -
    // so checking only the cull half left the two that draw whatever the
    // frustum cut untested, which is precisely where a script goes wrong.
    static const struct {
        vugen::Half half;
        const char* label;
    } kHalves[3] = {{vugen::Half::Cull, ""},
                    {vugen::Half::Clip, " [clip]"},
                    {vugen::Half::AsIs, " [as_is]"}};
    for (vu::Program* sp : vu::registeredPrograms()) {
        for (unsigned cls : vugen::customClasses()) {
            if ((sp->classes() & cls) == 0) continue;
            for (const auto& h : kHalves) {
                // The as_is twin is fed NDC, so a geometry script has nothing
                // to displace there and the generator does not emit it.
                if (h.half == vugen::Half::AsIs &&
                    sp->slot() == vugen::Slot::ObjectSpace)
                    continue;
                vugen::Desc d = vugen::descForClass(cls, 0, h.half);
                d.scriptPrepare = [](vugen::ScriptCtx& sc) {
                    vu::Ctx c(sc);
                    vuCheckScriptCurrent->prepare(c);
                };
                d.script = [](vugen::ScriptCtx& sc) {
                    // One program at a time - the loop below sets it.
                    vu::Ctx c(sc);
                    vuCheckScriptCurrent->vertex(c);
                };
                d.scriptSlot = sp->slot();
                vuCheckScriptCurrent = sp;
                const vugen::Built b = vugen::build(d);
                const std::string what =
                    std::string(vugen::classTitle(cls)) + h.label;
                if (!b.errors.empty()) {
                    std::printf("  %-16s %-22s BUILD REFUSED: %s\n", sp->name(),
                                what.c_str(), b.errors[0].c_str());
                    ++fails;
                    continue;
                }
                const float zeros[4] = {0.0f, 0.0f, 0.0f, 0.0f};
                const vugen::Equivalence run = vugen::equivalence(
                    b.program, b.program, d, 12, 0x2B71C0DEu, zeros, 0.4f);
                bool ok = run.identical && b.program.code.size() > 0;
                // AND IT HAS TO CHANGE SOMETHING. Running is the weaker half of
                // the claim: a script woven into the wrong place, or into a
                // register the program overwrites a line later, still runs and
                // still draws - it just draws the stock picture. Comparing
                // against the SAME description with the script removed is what
                // says the body reached the output, and it is the check that
                // makes "a script reaches every draw path" a fact rather than a
                // structural argument about which files got emitted.
                vugen::Desc plain = vugen::descForClass(cls, 0, h.half);
                const vugen::Built pb = vugen::build(plain);
                const float live[4] = {0.7f, 0.35f, 1.4f, 0.5f};
                const vugen::Equivalence effect = vugen::equivalence(
                    pb.program, b.program, d, 12, 0x2B71C0DEu, live, 0.4f);
                const bool changes = effect.ran && !effect.identical;
                if (!changes) ok = false;
                std::printf("  %-16s %-22s %-7s %4d instructions%s\n",
                            sp->name(), what.c_str(), ok ? "OK" : "FAILED",
                            (int)b.program.code.size(),
                            changes ? "" : "   (the script changes NOTHING here)");
                if (!ok) ++fails;
            }
        }
    }
    if (vu::registeredPrograms().empty()) {
        std::printf("  (none registered)\n");
    }
    return fails;
}

// The PROJECT-KERNEL half of --vu-check: a project's own C++ kernel
// (src/vu0/*.cpp, `vu::Kernel`), built and run under the VU0 machine model.
//
// The claim is the same one vuCheckScripts makes about a VU1 script, and it is
// worth as much here: not that the arithmetic is what the author meant - the
// host cannot know that - but that the body REACHED the microprogram. A kernel
// woven into the wrong place, or into a register the store overwrites a line
// later, still builds and still runs; it just copies its input to its output.
// So every kernel is run twice, once with the body and once without, and a
// kernel whose output matches the bodyless one is reported as doing nothing.
static vu::Kernel* vuCheckKernelCurrent = nullptr;

static int vuCheckProjectKernels() {
    std::printf("-- project kernels: build and run on VU0 --\n");
    const std::vector<vu::Kernel*>& kerns = vu::registeredKernels();
    if (kerns.empty()) {
        std::printf("  (none registered)\n\n");
        return 0;
    }
    int fails = 0;
    // Eight elements spread over a couple of units in every lane, which is
    // enough for a body that reads x, one that reads all four, and one that
    // only writes - and small enough that a failure prints in one screen.
    const int n = 8;
    std::vector<float> in((size_t)n * 4);
    for (int i = 0; i < n; ++i) {
        in[(size_t)i * 4 + 0] = -1.5f + 0.5f * (float)i;
        in[(size_t)i * 4 + 1] = 0.25f * (float)i;
        in[(size_t)i * 4 + 2] = 2.0f - 0.3f * (float)i;
        in[(size_t)i * 4 + 3] = 1.0f;
    }
    const float params[4] = {0.75f, 1.25f, -0.5f, 2.0f};
    for (vu::Kernel* kp : kerns) {
        vugen::KernelDesc kd;
        kd.title = kp->name();
        int want = kp->maxElements();
        if (want < 1) want = 1;
        kd.maxElements = want;
        kd.outputAddr = kd.inputAddr + want;
        kd.scriptPrepare = [](vugen::ScriptCtx& sc) {
            vu::Ctx c(sc);
            vuCheckKernelCurrent->prepare(c);
        };
        kd.script = [](vugen::ScriptCtx& sc) {
            vu::Ctx c(sc);
            vuCheckKernelCurrent->element(c);
        };
        vuCheckKernelCurrent = kp;
        const vugen::BuiltKernel b = vugen::buildKernel(kd);
        if (!b.errors.empty()) {
            std::printf("  %-20s BUILD REFUSED: %s\n", kp->name(),
                        b.errors[0].c_str());
            ++fails;
            continue;
        }
        for (const std::string& note : b.notes)
            std::printf("  %-20s note: %s\n", kp->name(), note.c_str());
        if (want < n) {
            std::printf("  %-20s batch is %d - too small to check\n",
                        kp->name(), want);
            continue;
        }
        std::string err;
        const std::vector<float> out =
            vugen::simulateKernel(b, kd, in, params, 0.4f, &err);
        if (out.size() != in.size()) {
            std::printf("  %-20s did not run: %s\n", kp->name(), err.c_str());
            ++fails;
            continue;
        }
        // The same kernel with the body taken out. Its output is the input,
        // element for element, so "changed nothing" is exactly "the body never
        // reached the store".
        vugen::KernelDesc bare = kd;
        bare.script = nullptr;
        bare.scriptPrepare = nullptr;
        const vugen::BuiltKernel bb = vugen::buildKernel(bare);
        const std::vector<float> plain =
            vugen::simulateKernel(bb, bare, in, params, 0.4f, &err);
        bool changes = plain.size() != out.size();
        for (size_t i = 0; i < out.size() && !changes; ++i)
            changes = out[i] != plain[i];
        // AND IT HAS TO STAY A NUMBER. A NaN out of a kernel is a body dividing
        // by a lane that happens to be zero, and it survives every structural
        // check there is - the program built, it ran, it changed something.
        bool finite = true;
        for (float v : out)
            if (!(v == v) || v > 1e30f || v < -1e30f) finite = false;
        const int instrs = (int)b.program.code.size();
        const bool fits = (instrs + 1) / 2 <= vugen::kVu0MicroCeiling;
        const bool ok = changes && finite && fits;
        std::printf("  %-20s %-7s %4d instructions (%d..%d of %d), %d per "
                    "element, batch %d%s%s%s\n",
                    kp->name(), ok ? "OK" : "FAILED", instrs, (instrs + 1) / 2,
                    instrs, vugen::kVu0MicroCeiling, b.perElement, want,
                    changes ? "" : "   (the body changes NOTHING)",
                    finite ? "" : "   (the output is not a finite number)",
                    fits ? "" : "   (it cannot fit VU0)");
        if (!ok) ++fails;
    }
    std::printf("  %s\n\n", fails == 0
                                ? "OK - every kernel builds, runs on VU0 and "
                                  "changes what it was handed"
                                : "FAILED");
    return fails;
}

// The kernel half of --vu-check: build a VU0 kernel from the same stage
// library, run it on the host, and check the numbers against arithmetic done
// here in C++. `squash` is the one to pin exactly - it is three multiplies with
// no approximation anywhere, so an exact comparison is fair and any drift is a
// real bug rather than a tolerance argument.
static int vuCheckKernel() {
    std::printf("-- VU0 kernel: built from the stage library, run on the host --\n");
    vugen::KernelDesc k;
    k.title = "vu-check scratch kernel";
    vugen::Stage sq = vugen::makeStage("squash");
    sq.params[0].value = 0.5f;   // x * 1.5
    sq.params[1].value = -0.25f; // y * 0.75
    sq.params[2].value = 1.0f;   // z * 2.0
    k.stages.push_back(sq);

    vugen::BuiltKernel b = vugen::buildKernel(k);
    if (!b.errors.empty()) {
        std::printf("  build refused: %s\n\n", b.errors[0].c_str());
        return 1;
    }
    int fails = 0;
    const int n = 6;
    std::vector<float> in((size_t)n * 4);
    for (int i = 0; i < n; ++i) {
        in[(size_t)i * 4 + 0] = 1.0f + (float)i;
        in[(size_t)i * 4 + 1] = -2.0f * (float)i;
        in[(size_t)i * 4 + 2] = 0.5f * (float)i;
        in[(size_t)i * 4 + 3] = 7.0f;
    }
    const float params[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    std::string err;
    const std::vector<float> out =
        vugen::simulateKernel(b, k, in, params, 0.0f, &err);
    if (out.size() != in.size()) {
        std::printf("  the kernel did not run: %s\n\n", err.c_str());
        return 1;
    }
    for (int i = 0; i < n; ++i) {
        const float want[3] = {in[(size_t)i * 4 + 0] * 1.5f,
                               in[(size_t)i * 4 + 1] * 0.75f,
                               in[(size_t)i * 4 + 2] * 2.0f};
        for (int c = 0; c < 3; ++c)
            if (out[(size_t)i * 4 + c] != want[c]) {
                std::printf("  element %d field %c: %g, expected %g\n", i,
                            "xyz"[c], (double)out[(size_t)i * 4 + c],
                            (double)want[c]);
                ++fails;
            }
    }

    // A wobble kernel exercises the SINE, and this is the check that matters,
    // because a peak-only one does not work. The generated series is
    //
    //   y = 4x(1-|x|);  sin ~= y + k*(y|y| - y),  k = 0.225
    //
    // and `y|y| - y` is ZERO at the peaks - so a wrong k reproduces the extremes
    // exactly and only shows in between. A wrong k is precisely what shipped:
    // `Vu::constants` was writing k + 1 into the w field (vf00.w is 1.0), the
    // simulator models vf00 correctly so both sides agreed, and a peak check
    // gave it full marks. The comparison below is against the same formula
    // evaluated HERE, at every element, which pins k as well as the shape.
    {
        vugen::KernelDesc ks;
        ks.title = "vu-check sine kernel";
        vugen::Stage sw = vugen::makeStage("wobble");
        sw.params[0].value = 1.0f;   // amplitude 1: the output IS the sine
        sw.params[1].value = 1.0f;   // frequency 1: the angle IS x + z
        sw.params[2].value = 0.0f;   // frozen
        ks.stages.push_back(sw);
        const vugen::BuiltKernel bs = vugen::buildKernel(ks);
        std::vector<float> sin_in;
        for (int i = 0; i < 24; ++i) {  // -3..+3 radians, across a whole period
            const float a = -3.0f + (float)i * 0.25f;
            sin_in.push_back(a);
            sin_in.push_back(0.0f);
            sin_in.push_back(0.0f);
            sin_in.push_back(1.0f);
        }
        std::string serr;
        const std::vector<float> so =
            vugen::simulateKernel(bs, ks, sin_in, params, 0.0f, &serr);
        double worst = 0.0;
        double worstAt = 0.0;
        if (so.size() != sin_in.size()) {
            std::printf("  the sine kernel did not run: %s\n", serr.c_str());
            ++fails;
        } else {
            for (size_t i = 0; i * 4 < so.size(); ++i) {
                const double a = sin_in[i * 4];
                // The same series, in double, straight off the documentation.
                double t = a * 0.15915494309189535 + 0.5;
                const double u = t - std::floor(t);
                const double x = 2.0 * u - 1.0;
                const double y = 4.0 * x * (1.0 - std::fabs(x));
                const double want = y + 0.225 * (y * std::fabs(y) - y);
                const double got = so[i * 4 + 1];
                if (std::fabs(got - want) > worst) {
                    worst = std::fabs(got - want);
                    worstAt = a;
                }
            }
            // Float arithmetic with VU rounding against a double reference:
            // a few 1e-6 apart is the format, 1e-2 apart is a wrong constant.
            if (worst > 1e-4) {
                std::printf("  the generated sine is %.5f off the formula at "
                            "%.2f rad - a coefficient is wrong\n", worst,
                            worstAt);
                ++fails;
            }
            std::printf("  sine: worst %.7f off its own series over -3..+3 rad, "
                        "%d samples\n", worst, 24);
        }
    }

    // And the amplitude, which is a separate claim: the approximation is worth
    // about 0.2%, so a displacement must stay inside the amplitude it was
    // given plus that margin.
    vugen::KernelDesc kw;
    kw.title = "vu-check wobble kernel";
    vugen::Stage wb = vugen::makeStage("wobble");
    wb.params[0].value = 3.0f;  // amplitude
    wb.params[1].value = 0.4f;  // frequency
    wb.params[2].value = 0.0f;  // frozen, so the check does not need a clock
    kw.stages.push_back(wb);
    const vugen::BuiltKernel bw = vugen::buildKernel(kw);
    if (!bw.errors.empty()) {
        std::printf("  wobble kernel refused: %s\n", bw.errors[0].c_str());
        ++fails;
    } else {
        const std::vector<float> ow =
            vugen::simulateKernel(bw, kw, in, params, 0.0f, &err);
        if (ow.size() != in.size()) {
            std::printf("  the wobble kernel did not run: %s\n", err.c_str());
            ++fails;
        } else {
            float peak = 0.0f;
            bool moved = false;
            for (int i = 0; i < n; ++i) {
                const float dy = ow[(size_t)i * 4 + 1] - in[(size_t)i * 4 + 1];
                if (dy != 0.0f) moved = true;
                if (dy > peak) peak = dy;
                if (-dy > peak) peak = -dy;
                if (ow[(size_t)i * 4 + 0] != in[(size_t)i * 4 + 0]) {
                    std::printf("  wobble moved X, which it must not\n");
                    ++fails;
                    break;
                }
            }
            if (!moved) {
                std::printf("  wobble displaced nothing\n");
                ++fails;
            }
            if (peak > 3.0f * 1.005f) {
                std::printf("  wobble overshot its amplitude: %g > 3\n",
                            (double)peak);
                ++fails;
            }
            std::printf("  wobble: peak displacement %.4f of an amplitude of 3, "
                        "%d instructions per element\n", (double)peak,
                        bw.perElement);
        }
    }

    std::printf("  squash: %d elements exact, %d instructions per element\n", n,
                b.perElement);
    std::printf("  %s\n\n", fails == 0
                                ? "OK - a kernel built from stages computes what "
                                  "the same arithmetic computes here"
                                : "FAILED");
    return fails == 0 ? 0 : 1;
}

// The VU0 half of --vu-check.
//
// The engine ships exactly one VU0 program - the raytracer kernel behind the
// raytraced mirrors - and until now the framework could only PARSE it. It is now
// RUN, under the VU0 machine model: 256 quadwords of data memory rather than
// VU1's 1024, 512 micro slots rather than 2048, and the vcallms entry contract
// (restart at instruction 0, data memory persists between calls). That target
// choice is the whole point - the kernel addresses quadwords up to 247, which is
// inside 256 by six quadwords, and against the VU1 model an overrun would wrap
// somewhere harmless and the out-of-range warning would never fire.
//
// The scene is staged the way vu0_raytracer.cpp stages it (its data-memory
// contract is written out at the top of the .vclpp): a single sphere dead ahead
// of the eye and a four-texel row that walks off it, so the first texel is a
// sphere hit and the last is a sky miss. The assertions are on the SHAPE of the
// answer - a direct-colour texel, channels in range, the sphere's own hue, a sky
// colour between the two gradient stops - because the kernel's shading constants
// are its business and pinning them here would make this a change detector
// rather than a check.
static int vuCheckVu0(const std::string& engine) {
    namespace fs = std::filesystem;
    std::printf("-- VU0: the raytracer kernel, run under the VU0 model --\n");
    const std::string path = (fs::path(engine) / "src" / "renderer" / "rt" /
                              "vu0_rt_kernel.vclpp").string();
    vuasm::Options opt;
    opt.includeRoot = engine;
    vuir::Program k;
    std::string err;
    if (!vuasm::parseFile(path, opt, k, err)) {
        std::printf("  could not read the kernel: %s\n\n", err.c_str());
        return 1;
    }

    auto bits = [](float f) {
        uint32_t b;
        std::memcpy(&b, &f, 4);
        return b;
    };
    std::vector<uint32_t> mem((size_t)vusim::memWords(vusim::Target::VU0), 0u);
    auto putf = [&](int qw, int f, float v) { mem[(size_t)qw * 4 + f] = bits(v); };
    auto puti = [&](int qw, int f, uint32_t v) { mem[(size_t)qw * 4 + f] = v; };

    // A ray STARTS at its texel on the mirror plane and points away from the
    // reflected eye - the eye is only there to give the direction. So the eye
    // goes behind the row, not on top of the sphere.
    const int kTexels = 4;
    putf(0, 0, 0.0f), putf(0, 1, 0.0f), putf(0, 2, -10.0f);    // eye
    putf(1, 0, 0.0f), putf(1, 1, 0.0f), putf(1, 2, 0.0f);      // rowBase
    putf(2, 0, 1.0f), putf(2, 1, 0.5f), putf(2, 2, 0.0f);      // du: +X +Y per texel
    putf(3, 0, 0.0f), putf(3, 1, 1.0f), putf(3, 2, 0.0f);      // light: straight up
    putf(4, 0, 40.0f), putf(4, 1, 90.0f), putf(4, 2, 170.0f);  // sky zenith
    putf(5, 0, 180.0f), putf(5, 1, 210.0f), putf(5, 2, 235.0f);  // sky horizon
    puti(6, 0, 1), puti(6, 1, (uint32_t)kTexels), puti(6, 2, 0), puti(6, 3, 0);
    putf(7, 0, 0.30f), putf(7, 1, 0.70f), putf(7, 2, 0.5f), putf(7, 3, 1e38f);
    putf(8, 0, 0.01f), putf(8, 1, 1e-8f), putf(8, 2, 255.0f), putf(8, 3, 4096.0f);
    putf(9, 3, 0.0f), putf(10, 3, 0.0f);  // no triangle groups: r^2 = 0 misses
    puti(11, 0, 104), puti(11, 1, 0), puti(11, 2, 104), puti(11, 3, 0);
    // One sphere on the row's forward axis, ten units out. w is the RADIUS
    // SQUARED, which is what the kernel's discriminant wants.
    putf(12, 0, 0.0f), putf(12, 1, 0.0f), putf(12, 2, 10.0f), putf(12, 3, 9.0f);
    putf(20, 0, 200.0f), putf(20, 1, 100.0f), putf(20, 2, 50.0f);

    vusim::Config cfg;
    cfg.target = vusim::Target::VU0;
    // One vcallms, through the kernel-call harness so the persistence contract
    // is the one being exercised rather than a plain run().
    const std::vector<vusim::Result> calls =
        vusim::runKernel(k, mem, {{}}, cfg);
    if (calls.empty() || !calls.back().ok) {
        std::printf("  the kernel did not run: %s\n\n",
                    calls.empty() ? "no result" : calls.back().error.c_str());
        return 1;
    }
    const vusim::Result& r = calls.back();

    int fails = 0;
    for (const vusim::Warning& w : r.warnings) {
        std::printf("  warning at pc %d: %s\n", w.pc, w.text.c_str());
        ++fails;  // every warning here is a real finding: the kernel is VU0-honest
    }

    // The row comes back as INTEGERS - the kernel's last act is an ftoi0, so
    // the EE can pack the texel without touching a float.
    const int kOutBase = 40;
    int32_t rgb[4][3];
    for (int i = 0; i < kTexels; ++i) {
        const size_t q = (size_t)(kOutBase + i) * 4;
        for (int c = 0; c < 3; ++c) rgb[i][c] = (int32_t)r.mem[q + c];
        const int32_t w = (int32_t)r.mem[q + 3];
        if (w >= 0) {
            std::printf("  texel %d came back as a TRIANGLE hit (w %d) - this "
                        "scene has no triangle groups\n", i, (int)w);
            ++fails;
        }
        for (int c = 0; c < 3; ++c)
            if (rgb[i][c] < 0 || rgb[i][c] > 255) {
                std::printf("  texel %d channel %d is %d, outside 0..255\n", i, c,
                            (int)rgb[i][c]);
                ++fails;
            }
    }
    // Texel 0's ray is (0,0,1) and runs straight into the sphere; the shade
    // scales the sphere colour, so the RATIO is what survives it.
    const bool hitHue = rgb[0][0] > rgb[0][1] * 3 / 2 &&
                        rgb[0][1] > rgb[0][2] * 3 / 2 && rgb[0][0] > 1;
    if (!hitHue) {
        std::printf("  texel 0 is (%d, %d, %d) - not the sphere's 200:100:50\n",
                    (int)rgb[0][0], (int)rgb[0][1], (int)rgb[0][2]);
        ++fails;
    }
    // Texel 3 is 3 units off the axis and misses by a wide margin, so it must be
    // a blend of the two sky stops.
    const bool sky = rgb[3][0] >= 40 && rgb[3][0] <= 180 && rgb[3][2] >= 170 &&
                     rgb[3][2] <= 235;
    if (!sky) {
        std::printf("  texel 3 is (%d, %d, %d) - not between the sky stops\n",
                    (int)rgb[3][0], (int)rgb[3][1], (int)rgb[3][2]);
        ++fails;
    }

    std::printf("  %d instructions, %lld steps, %d quadwords of data memory\n",
                (int)k.code.size(), (long long)r.steps,
                vusim::memQuads(vusim::Target::VU0));
    std::printf("  sphere texel (%d, %d, %d)   sky texel (%d, %d, %d)\n",
                (int)rgb[0][0], (int)rgb[0][1], (int)rgb[0][2], (int)rgb[3][0],
                (int)rgb[3][1], (int)rgb[3][2]);
    const std::vector<std::pair<std::string, const vuir::Program*>> kset = {
        {k.name.empty() ? "Vu0RtKernel" : k.name, &k}};
    const vugen::Budget kb = vugen::budget(kset, vugen::kVu0MicroCeiling);
    std::printf("  micro memory: %d instructions -> %d..%d of %d VU0 slots  %s\n",
                kb.entries.empty() ? 0 : kb.entries[0].emitted, kb.totalMin,
                kb.totalMax, kb.ceiling,
                kb.certainlyFits()         ? "fits"
                : kb.certainlyOverflows()  ? "OVERFLOWS"
                                           : "depends on how VCL pairs them");
    std::printf("  %s\n\n", fails == 0 ? "OK - runs, stays inside VU0's 256 "
                                         "quadwords, and shades what it should"
                                       : "FAILED");
    return fails == 0 ? 0 : 1;
}

// Usage:
//   tyrax-editor --vu-check [engineDir]
//
// The VU framework's self-test, and the reason it can claim anything (see
// docs/vu-framework.md): parse EVERY handwritten .vclpp the engine ships, build
// the described programs from their C++ descriptions, run both in the host VU1
// simulator on identical randomized input, and diff what they staged for the GS
// quadword by quadword. No Docker, no PCSX2, no console.
static int vuCheckFromCli(int argc, char** argv) {
    namespace fs = std::filesystem;
    const std::string engine = vuEngineDir(argc > 2 ? argv[2] : nullptr);
    std::printf("engine: %s\n\n", engine.c_str());

    std::error_code ec;
    if (!fs::exists(fs::path(engine) / "src", ec)) {
        std::fprintf(stderr,
                     "error: no engine sources at %s\n"
                     "       pass the path: --vu-check <engineDir>\n",
                     engine.c_str());
        return 1;
    }

    // 1. Every handwritten program must parse.
    std::vector<std::string> files;
    for (const auto& e : fs::recursive_directory_iterator(fs::path(engine) / "src", ec))
        if (e.is_regular_file() && e.path().extension() == ".vclpp")
            files.push_back(e.path().string());
    std::sort(files.begin(), files.end());

    int parsed = 0, parseFailed = 0;
    std::printf("-- parsing the handwritten programs --\n");
    for (const std::string& f : files) {
        vuasm::Options opt;
        opt.includeRoot = engine;
        vuir::Program p;
        std::string err;
        if (vuasm::parseFile(f, opt, p, err)) {
            ++parsed;
            std::printf("  %-42s %4d instructions\n",
                        fs::path(f).filename().string().c_str(), (int)p.code.size());
            for (const std::string& n : p.notes)
                std::printf("      note: %s\n", n.c_str());
        } else {
            ++parseFailed;
            std::printf("  %-42s FAILED: %s\n",
                        fs::path(f).filename().string().c_str(), err.c_str());
        }
    }
    std::printf("  %d parsed, %d failed\n\n", parsed, parseFailed);

    // 2. Every described program must be bit-identical to its handwritten twin.
    int mismatches = 0;
    std::vector<std::pair<std::string, const vuir::Program*>> set;
    std::vector<vugen::Built> built;
    built.reserve(vugen::allDescs().size());
    std::printf("-- generated vs handwritten, in the simulator --\n");
    for (const vugen::Desc& d : vugen::allDescs()) {
        built.push_back(vugen::build(d));
        const vugen::Built& b = built.back();
        for (const std::string& n : b.notes) std::printf("  note: %s\n", n.c_str());
        if (b.program.code.empty()) continue;

        vuasm::Options opt;
        opt.includeRoot = engine;
        vuir::Program hand;
        std::string err;
        const std::string path =
            (fs::path(engine) / "src" / "renderer" / "3d" / "pipeline" / "static" /
             "core" / "programs" / d.dir / (d.fileStem + ".vclpp"))
                .string();
        if (!vuasm::parseFile(path, opt, hand, err)) {
            std::printf("  %-16s could not read the reference: %s\n",
                        d.vclName.c_str(), err.c_str());
            ++mismatches;
            continue;
        }
        const vugen::Equivalence eq =
            vugen::equivalence(hand, b.program, d, 60, 0x5eed1234u);
        std::printf("  %-16s %-9s %d trials, up to %d vertices\n", d.vclName.c_str(),
                    eq.identical ? "IDENTICAL" : "DIFFERENT", eq.trials, eq.vertices);
        if (!eq.identical) {
            ++mismatches;
            if (!eq.error.empty()) std::printf("      %s\n", eq.error.c_str());
            if (!eq.detail.empty()) std::printf("      %s\n", eq.detail.c_str());
        }
    }
    std::printf("\n");

    // 3. The emitted SOURCE must behave like the IR it came from.
    //
    // This is not paranoia, it is the check whose absence shipped a broken
    // engine build: everything above compares the in-memory IR, and the file
    // that actually reaches vclpp is produced by a separate text emitter. Parse
    // the emitted text back and run the same equivalence over it, so a program
    // is only "generated" once the bytes on disk are proven too.
    int roundTripFails = 0;
    std::printf("-- emitted source, parsed back and re-run --\n");
    for (size_t i = 0; i < built.size(); ++i) {
        const vugen::Built& b = built[i];
        if (b.program.code.empty() || b.vclpp.empty()) continue;
        const vugen::Desc d = vugen::allDescs()[i];
        vuasm::Options opt;  // the emitted source has no #includes by design
        vuir::Program back;
        std::string err;
        if (!vuasm::parseText(b.vclpp, d.fileStem + ".vclpp", opt, back, err)) {
            std::printf("  %-16s EMITTED SOURCE DOES NOT PARSE: %s\n",
                        d.vclName.c_str(), err.c_str());
            ++roundTripFails;
            continue;
        }
        const vugen::Equivalence eq =
            vugen::equivalence(b.program, back, d, 30, 0x51DE0FFEu);
        std::printf("  %-16s %-9s %d instructions in, %d parsed back\n",
                    d.vclName.c_str(), eq.identical ? "MATCHES" : "DIFFERENT",
                    (int)b.program.code.size(), (int)back.code.size());
        if (!eq.identical) {
            ++roundTripFails;
            if (!eq.error.empty()) std::printf("      %s\n", eq.error.c_str());
            if (!eq.detail.empty()) std::printf("      %s\n", eq.detail.c_str());
        }
        for (const std::string& n : back.notes)
            std::printf("      note: %s\n", n.c_str());
    }
    std::printf("\n");

    // 4. The micro-memory budget for the generated set.
    for (const vugen::Built& b : built)
        if (!b.program.code.empty()) set.push_back({b.program.name, &b.program});
    const vugen::Budget bud = vugen::budget(set);
    std::printf("-- VU1 micro memory (%d slots, %d usable below the draw-finish "
                "helper) --\n", vugen::kMicroMemSlots, bud.ceiling);
    for (const vugen::BudgetEntry& e : bud.entries)
        std::printf("  %-16s %4d instructions -> %4d..%4d slots\n", e.name.c_str(),
                    e.emitted, e.slotsMin, e.slotsMax);
    std::printf("  %-16s %4s %17d..%4d slots  %s\n", "TOTAL", "",
                bud.totalMin, bud.totalMax,
                bud.certainlyFits()      ? "fits"
                : bud.certainlyOverflows() ? "OVERFLOWS"
                                           : "depends on how VCL pairs them");
    std::printf(
        "  (a range, not a number: VCL packs an upper and a lower op into one\n"
        "   64-bit slot when it can, so the exact size is only known after it "
        "runs)\n\n");

    // 4b. Register pressure. The IR has unlimited virtual VF registers and VCL
    //     allocates the real 31, so running out is invisible to everything
    //     above and surfaces as `no opt table` from vcl, inside Docker, with no
    //     line number. Estimating it here is what turns that into a number.
    std::printf("-- VF register pressure (31 allocatable; past that vcl may "
                "refuse with \"no opt table\") --\n");
    for (const vugen::Built& b : built) {
        if (b.program.code.empty()) continue;
        const vugen::Pressure pr = vugen::vfPressure(b.program);
        std::printf("  %-16s peak %2d of %d live   (%d names)%s\n",
                    b.program.name.c_str(), pr.peak, vugen::kVfRegisters,
                    pr.names, pr.fits() ? "" : "   TIGHT");
    }
    std::printf("  (an ESTIMATE, and it over-states: it ignores control flow "
                "and cannot split a\n   live range the way vcl does. These ten "
                "all compile, which is the calibration -\n   known-good at <= "
                "27, measured to still compile at 32, measured to FAIL at "
                "36.)\n\n");

    // 5. The authoring layer: every stage, both directions.
    const int stageFails = vuCheckStages(engine);

    // 6. VU0 - the engine's own kernel under the VU0 machine model, and a
    //    generated one built from the same stage library.
    const int vu0Fails = vuCheckVu0(engine) + vuCheckKernel();

    // 7. A project's own C++ program, through the same emitter - and its own
    //    C++ KERNEL, through the VU0 one.
    const int scriptFails = vuCheckScripts(engine) + vuCheckProjectKernels();

    const bool ok = parseFailed == 0 && mismatches == 0 && roundTripFails == 0 &&
                    stageFails == 0 && vu0Fails == 0 &&
                    scriptFails == 0;
    std::printf("%s\n", ok ? "PASS - every described program matches its "
                             "handwritten twin bit for bit"
                           : "FAIL");
    return ok ? 0 : 1;
}

// Usage:
//   tyrax-editor --vu-emit <outDir> [engineDir]
//
// Writes the generated .vclpp and the matching EE-side program class for every
// described program. Deliberately NOT written straight into vendor/tyra: adopting
// generated microcode is a change that has to be built in Docker and looked at on
// hardware, so this stages it for a human to diff first.
static int vuEmitFromCli(int argc, char** argv) {
    namespace fs = std::filesystem;
    if (argc < 3) {
        std::fprintf(stderr, "usage: tyrax-editor --vu-emit <outDir> [engineDir]\n");
        return 2;
    }
    const fs::path out = argv[2];
    std::error_code ec;
    fs::create_directories(out, ec);
    int written = 0;
    for (const vugen::Desc& d : vugen::allDescs()) {
        const vugen::Built b = vugen::build(d);
        for (const std::string& n : b.notes) std::printf("note: %s\n", n.c_str());
        if (b.vclpp.empty()) continue;
        const std::pair<std::string, const std::string*> files[] = {
            {d.fileStem + ".vclpp", &b.vclpp},
            {d.fileStem + "_program.cpp", &b.eeSource},
            {d.fileStem + "_program.hpp", &b.eeHeader},
        };
        for (const auto& f : files) {
            std::ofstream o((out / f.first).string(), std::ios::binary);
            if (!o) {
                std::fprintf(stderr, "error: cannot write %s\n", f.first.c_str());
                return 1;
            }
            o << *f.second;
            ++written;
        }
        std::printf("%-32s %4d instructions, %d tag quadwords, %d GS regs/vertex\n",
                    d.fileStem.c_str(), (int)b.program.code.size(), b.tagQuads,
                    b.regsPerVertex);
    }
    std::printf("\n%d files written to %s\n", written, out.string().c_str());
    return 0;
}

// Usage:
//   tyrax-editor --vu-list <file.vclpp> [engineDir]
//
// Expands and disassembles one microprogram - what the framework actually sees
// after the vclpp layer, which is the first thing to look at when a program does
// something you did not write.
static int vuListFromCli(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr,
                     "usage: tyrax-editor --vu-list <file.vclpp> [engineDir]\n");
        return 2;
    }
    vuasm::Options opt;
    opt.includeRoot = vuEngineDir(argc > 3 ? argv[3] : nullptr);
    vuir::Program p;
    std::string err;
    if (!vuasm::parseFile(argv[2], opt, p, err)) {
        std::fprintf(stderr, "error: %s\n", err.c_str());
        return 1;
    }
    const vugen::Pressure pr = vugen::vfPressure(p);
    std::printf("; %s - %d instructions, %d VF names, %d VI names\n", p.name.c_str(),
                (int)p.code.size(), (int)p.vfNames.size(), (int)p.viNames.size());
    // The one property of a microprogram that is invisible everywhere else and
    // fatal at assembly time (see vugen::vfPressure).
    std::printf("; peak VF pressure %d of %d%s\n", pr.peak, vugen::kVfRegisters,
                pr.fits() ? ""
                          : "  - TIGHT, vcl may refuse with \"no opt table\"");
    if (!pr.fits()) {
        std::printf("; live at instruction %d:", pr.at);
        for (const std::string& n : pr.live) std::printf(" %s", n.c_str());
        std::printf("\n");
    }
    for (const std::string& n : p.notes) std::printf("; note: %s\n", n.c_str());
    std::printf("%s", vusim::listing(p).c_str());
    return 0;
}

// Usage:
//   tyrax-editor --vu-replay <projectDir> [engineDir]
//
// Takes a VU1 capture off a real console (bin/vucap.bin - Debugger > VU, or
// docs/devkit.md) and RE-RUNS it on the host, then diffs the result against what
// the hardware actually produced. See docs/vu-framework.md.
//
// The capture happens to contain both halves of the experiment: the DMA chain
// the EE built (the input) and a snapshot of all 1024 quadwords of VU1 data
// memory taken once the microprogram went idle (the output). So the input can be
// reconstructed, fed to the simulator, and the answer compared against the
// console's own.
//
// Two things the capture does NOT record are found by SEARCH rather than
// assumed, which turns out to be the more useful design: which microprogram ran
// (the chain carries an MSCAL entry address, and an address is only meaningful
// against a program layout the host does not have), and which half of the double
// buffer it ran from. Every candidate is tried and the one that reproduces the
// hardware output is reported - so the tool answers "which program drew this?"
// with evidence instead of a label, and a run where NOTHING matches is itself
// the finding.
static int vuReplayFromCli(int argc, char** argv) {
    namespace fs = std::filesystem;
    if (argc < 3) {
        std::fprintf(stderr,
                     "usage: tyrax-editor --vu-replay <projectDir> [engineDir]\n");
        return 2;
    }
    Project proj;
    const std::string perr = project::load(proj, argv[2]);
    if (!perr.empty()) {
        std::fprintf(stderr, "error: %s\n", perr.c_str());
        return 1;
    }
    const std::string capPath = (fs::path(proj.dir) / "bin" / "vucap.bin").string();
    vucap::Capture cap;
    if (!vucap::load(capPath, cap)) {
        std::fprintf(stderr, "error: %s (%s)\n", cap.error.c_str(), capPath.c_str());
        return 1;
    }
    std::printf("capture: frame %u, %d quadwords, %d mesh(es), flush %d of %d\n",
                cap.frame, cap.qw, (int)cap.meshes.size(), cap.flushIndex,
                cap.flushCount);
    if (!cap.hasVuMem) {
        std::fprintf(stderr,
                     "error: this capture has no VU1 memory snapshot, so there is "
                     "nothing to compare against.\n"
                     "       Re-arm it from Debugger > VU (a v3 or newer capture).\n");
        return 1;
    }

    // Every microprogram the engine ships is a candidate - AND every one the
    // project generated. Leaving the project's own out is not a gap in
    // coverage, it is a wrong answer: a packet drawn by a project's script
    // cannot match any shipped program, so the tool reports "not reproduced"
    // for the one case the author most wants explained.
    const std::string engine = vuEngineDir(argc > 3 ? argv[3] : nullptr);
    std::error_code ec;
    std::vector<std::string> files;
    for (const auto& e : fs::recursive_directory_iterator(fs::path(engine) / "src", ec))
        if (e.is_regular_file() && e.path().extension() == ".vclpp")
            files.push_back(e.path().string());
    for (const auto& e :
         fs::directory_iterator(fs::path(proj.dir) / "src" / "gen", ec))
        if (!ec && e.is_regular_file() && e.path().extension() == ".vclpp")
            files.push_back(e.path().string());
    std::sort(files.begin(), files.end());
    if (files.empty()) {
        std::fprintf(stderr, "error: no .vclpp programs under %s\n", engine.c_str());
        return 1;
    }

    // The two halves of the StaPip double buffer (stapip_qbuffer_renderer.cpp:
    // starting address VU1_STAPIP_LAST_ITEM_ADDR + 1, size split evenly below
    // VU1_STAPIP_DBUFFER_END). The capture does not say which one this run used.
    const int kStart = 22, kEnd = 944;
    const int half = (kEnd - kStart) / 2;
    const int topsCandidates[2] = {kStart, kStart + half};

    // Reconstruct the input. The constants the object-data chain uploaded
    // (matrices, tags, fog) are not in THIS chain - but they survive in the
    // captured memory, because nothing in the run overwrites quadwords 0..21.
    // So those are copied from the snapshot and the chain's unpacks are replayed
    // on top of an OTHERWISE ZEROED memory.
    //
    // Zeroing the rest is the load-bearing part, and seeding from the whole
    // snapshot instead was the first version's bug: the console's own output
    // packet is still sitting in that memory, so the packet scan found it no
    // matter what the simulated program did, and every one of the 25 candidates
    // "reproduced" the capture. A comparison that cannot fail proves nothing.
    // With the output area zeroed, a packet found afterwards was written by
    // THIS run.
    const int kConstQuads = 22;  // VU1_STAPIP_LAST_ITEM_ADDR + 1

    // Only the LAST mesh of the chain can be replayed: its output is the one
    // still in the snapshot, everything before it has been overwritten. Each
    // mesh's upload starts with the 2-quadword buffer header at +TOPS offset 0,
    // so the final group is everything from the last such unpack onward.
    // (Grouping by `Unpack::program` is not enough - consecutive meshes often
    // run the SAME microprogram, and their two buffer halves would be merged
    // into one incoherent image.)
    size_t groupStart = 0;
    for (size_t i = 0; i < cap.unpacks.size(); ++i)
        if (cap.unpacks[i].useTops && cap.unpacks[i].vuAddr == 0) groupStart = i;

    // The clip programs read a six-plane table the EE uploads per mesh into the
    // scratch above the double buffer (VU1_CLIP_PLANES_ADDR, stapip_vu1_shared
    // _defines.h). It is not in this chain either, and zeroing it makes every
    // clip program cut everything away - so it is carried over from the snapshot
    // exactly like the low constants are.
    const int kScratchFrom = 944;  // VU1_STAPIP_DBUFFER_END
    auto stage = [&](int tops) {
        std::vector<uint32_t> mem(vusim::kMemWords, 0u);
        for (int i = 0; i < kConstQuads * 4 && i < (int)cap.vuMem.size(); ++i)
            mem[i] = cap.vuMem[i];
        for (int i = kScratchFrom * 4;
             i < vusim::kMemWords && i < (int)cap.vuMem.size(); ++i)
            mem[i] = cap.vuMem[i];
        for (size_t k = groupStart; k < cap.unpacks.size(); ++k) {
            const vucap::Unpack& u = cap.unpacks[k];
            const int base = (u.useTops ? tops : 0) + (int)u.vuAddr;
            for (size_t i = 0; i < u.words.size(); ++i) {
                const size_t at = (size_t)base * 4 + i;
                if (at < mem.size()) mem[at] = u.words[i];
            }
        }
        return mem;
    };

    // Which quadwords the EE itself wrote. A "match" inside this range is the
    // input echoing itself, not a program reproducing an output - the first
    // version compared the biggest geometry packet in memory and that packet
    // turned out to be the EE's own PRIM tag at buffer+1 followed by the vertex
    // array, read as GS vertices. Every candidate matched it. Anything the
    // comparison lands on has to be OUTSIDE what the chain uploaded.
    auto unpackTouches = [&](int tops, int qw0, int qw1) {
        for (const vucap::Unpack& u : cap.unpacks) {
            const int base = (u.useTops ? tops : 0) + (int)u.vuAddr;
            const int end = base + (int)(u.words.size() / 4);
            if (qw0 < end && base < qw1) return true;
        }
        return false;
    };
    // The first geometry packet at or after `start` in a memory image.
    auto packetAt = [](const std::vector<uint32_t>& mem, int start,
                       vucap::GifPacket& out) {
        std::vector<vucap::GifPacket> all;
        vucap::scanGifPackets(mem, all);
        for (const vucap::GifPacket& g : all)
            if (g.vuAddr >= start && g.hasGeometry) {
                out = g;
                return true;
            }
        return false;
    };

    struct Hit {
        std::string file, name;
        int tops = 0, kick = 0, verts = 0, mismatched = 0;
        std::string regs, prim;
        std::vector<std::string> worst;  // the biggest per-vertex deltas
    };
    std::vector<Hit> exact, near;
    int ran = 0, kicked = 0, echo = 0;

    for (const std::string& f : files) {
        vuasm::Options opt;
        opt.includeRoot = engine;
        vuir::Program prog;
        std::string err;
        if (!vuasm::parseFile(f, opt, prog, err)) continue;

        for (int tops : topsCandidates) {
            vusim::Config cfg;
            cfg.top = tops;
            const vusim::Result r = vusim::run(prog, stage(tops), cfg);
            if (!r.ok || r.kicks.empty()) continue;
            ++ran;

            // Compare at the address THIS program says it kicked - that is where
            // the GS would have read from, so it is where the console's memory
            // must agree if the same program produced it.
            const int kick = r.kicks.front();
            vucap::GifPacket mine, theirs;
            if (!packetAt(r.mem, kick, mine)) continue;
            if (!packetAt(cap.vuMem, kick, theirs)) continue;
            ++kicked;
            if (mine.verts.size() != theirs.verts.size()) continue;
            if (mine.verts.empty()) continue;
            const int qw0 = mine.vuAddr;
            const int qw1 = qw0 + 1 + mine.nloop * mine.nreg;
            if (unpackTouches(tops, qw0, qw1)) {
                ++echo;
                continue;  // inside the EE's own upload: proves nothing
            }

            int bad = 0;
            std::vector<std::pair<long long, std::string>> deltas;
            for (size_t i = 0; i < mine.verts.size(); ++i) {
                const vucap::GsVertex& a = theirs.verts[i];
                const vucap::GsVertex& b = mine.verts[i];
                // ST is compared as BITS, and it is what separates otherwise
                // identical candidates: for a mesh entirely inside the frustum
                // the cull and clip families stage the same positions, and only
                // the texture coordinates say whether the matcap variant ran.
                if (a.x != b.x || a.y != b.y || a.z != b.z || a.r != b.r ||
                    a.g != b.g || a.b != b.b || a.a != b.a ||
                    std::memcmp(&a.s, &b.s, 4) != 0 ||
                    std::memcmp(&a.t, &b.t, 4) != 0) {
                    ++bad;
                    const long long dx = std::llabs((long long)a.x - b.x);
                    const long long dy = std::llabs((long long)a.y - b.y);
                    const long long dz =
                        std::llabs((long long)a.z - (long long)b.z);
                    char line[192];
                    std::snprintf(line, sizeof line,
                                  "v%-3d dx=%lld dy=%lld dz=%lld   hw "
                                  "(%d,%d,%u) sim (%d,%d,%u)",
                                  (int)i, dx, dy, dz, a.x, a.y, a.z, b.x, b.y,
                                  b.z);
                    deltas.push_back({dx + dy + dz, line});
                }
            }
            std::sort(deltas.begin(), deltas.end(),
                      [](const std::pair<long long, std::string>& p,
                         const std::pair<long long, std::string>& q) {
                          return p.first > q.first;
                      });
            Hit h{fs::path(f).filename().string(),
                  prog.name,
                  tops,
                  kick,
                  (int)mine.verts.size(),
                  bad,
                  mine.regs,
                  mine.primName(),
                  {}};
            for (size_t k = 0; k < deltas.size() && k < 4; ++k)
                h.worst.push_back(deltas[k].second);
            (bad == 0 ? exact : near).push_back(h);
        }
    }
    std::printf("%d candidate runs kicked a packet, %d had one to compare, "
                "%d landed inside the EE's own upload and were discarded\n\n",
                ran, kicked, echo);

    if (!exact.empty()) {
        std::printf("REPRODUCED - the host simulator produced the console's "
                    "packet exactly:\n");
        for (const Hit& h : exact)
            std::printf("  %-32s (%s)\n      buffer half %d, kicked quadword %d, "
                        "%s [%s], %d/%d vertices identical\n",
                        h.file.c_str(), h.name.c_str(), h.tops, h.kick,
                        h.prim.c_str(), h.regs.c_str(), h.verts, h.verts);
        std::printf(
            "\nEvery GS vertex matches bit for bit - screen X/Y in 12.4, the "
            "24-bit Z\nand the clamped colours. The program above is what drew "
            "this packet, and\nthe simulator agrees with the hardware on it.\n");
        return 0;
    }

    std::printf("NOT REPRODUCED - no shipped microprogram replayed into the "
                "console's packet.\n\n");
    if (near.empty()) {
        std::printf(
            "No candidate produced a comparable packet outside the EE's own\n"
            "upload. That usually means the reconstruction is wrong rather than\n"
            "any program: this chain carries several meshes and only the LAST\n"
            "one's output survives in the snapshot, and the per-mesh constants\n"
            "come from an object-data chain this capture does not contain.\n");
    } else {
        std::printf("Closest candidates (same vertex count, differing values):\n");
        std::sort(near.begin(), near.end(),
                  [](const Hit& a, const Hit& b) { return a.mismatched < b.mismatched; });
        for (size_t i = 0; i < near.size() && i < 6; ++i)
            std::printf("  %-32s half %d, kick %d: %d of %d vertices differ\n",
                        near[i].file.c_str(), near[i].tops, near[i].kick,
                        near[i].mismatched, near[i].verts);
        // HOW they differ decides what the near miss means, and printing the
        // count alone is not enough to tell those apart: a handful of vertices
        // off by one unit in the last place is arithmetic, every vertex off by
        // hundreds is the wrong program or the wrong input.
        if (!near[0].worst.empty()) {
            std::printf("\nWorst vertices of the closest candidate (%s):\n",
                        near[0].file.c_str());
            for (const std::string& w : near[0].worst)
                std::printf("  %s\n", w.c_str());
        }
        std::printf(
            "\nA near miss is worth reading, not dismissing: identical vertex\n"
            "counts with differing values means the right program ran and one\n"
            "input differs - the per-mesh constants the object-data chain\n"
            "uploaded are NOT in this capture, so a matrix or a fog parameter\n"
            "read from the snapshot may belong to a later mesh.\n");
    }
    return 1;
}

int main(int argc, char** argv) {
    if (argc > 1 && std::strcmp(argv[1], "--vu-check") == 0)
        return vuCheckFromCli(argc, argv);
    if (argc > 1 && std::strcmp(argv[1], "--vu-emit") == 0)
        return vuEmitFromCli(argc, argv);
    if (argc > 1 && std::strcmp(argv[1], "--vu-list") == 0)
        return vuListFromCli(argc, argv);
    if (argc > 1 && std::strcmp(argv[1], "--vu-replay") == 0)
        return vuReplayFromCli(argc, argv);
    if (argc > 1 && std::strcmp(argv[1], "--debug-state") == 0)
        return debugStateFromCli(argc, argv);
    if (argc > 1 && std::strcmp(argv[1], "--dump-vucap") == 0)
        return dumpVuCapFromCli(argc, argv);
    if (argc > 1 && std::strcmp(argv[1], "--symbolize") == 0)
        return symbolizeFromCli(argc, argv);
    if (argc > 1 && std::strcmp(argv[1], "--audit-release") == 0)
        return auditReleaseFromCli(argc, argv);
    if (argc > 1 && std::strcmp(argv[1], "--pad") == 0)
        return padFromCli(argc, argv);
    if (argc > 1 && std::strcmp(argv[1], "--ui-script") == 0)
        return uiScriptFromCli(argc, argv);
    if (argc > 1 && std::strcmp(argv[1], "--new") == 0) return createFromCli(argc, argv);
    if (argc > 1 && std::strcmp(argv[1], "--build") == 0) return buildFromCli(argc, argv);
    if (argc > 1 && std::strcmp(argv[1], "--resave") == 0) return resaveFromCli(argc, argv);
    if (argc > 1 && std::strcmp(argv[1], "--migrate") == 0) return migrateFromCli(argc, argv);
    if (argc > 1 && std::strcmp(argv[1], "--list-nodes") == 0)
        return listNodesFromCli(argc, argv);
    if (argc > 1 && std::strcmp(argv[1], "--dump") == 0) return dumpFromCli(argc, argv);
    if (argc > 1 && std::strcmp(argv[1], "--dump-graph") == 0)
        return dumpGraphFromCli(argc, argv);
    if (argc > 1 && std::strcmp(argv[1], "--apply-graph") == 0)
        return applyGraphFromCli(argc, argv);
    if (argc > 1 && std::strcmp(argv[1], "--refresh-gen") == 0)
        return refreshGenFromCli(argc, argv);
    if (argc > 1 && std::strcmp(argv[1], "--bake-gi") == 0)
        return bakeGiFromCli(argc, argv);
    if (argc > 1 && std::strcmp(argv[1], "--ai-graph") == 0)
        return aiGraphFromCli(argc, argv);
    if (argc > 1 && std::strcmp(argv[1], "--add-ai-support") == 0)
        return aiSupportFromCli(argc, argv);
    if (argc > 1 && std::strcmp(argv[1], "--chat-prompt") == 0)
        return chatPromptFromCli(argc, argv);
    if (argc > 1 && std::strcmp(argv[1], "--search-docs") == 0)
        return searchDocsFromCli(argc, argv);
    // The neural upscaler's host side (docs/neural-upscaler.md). Headless like
    // --bake-gi: no display, no Project - the network is trained on a
    // procedural corpus and baked into the game as a header.
    //
    // UNBUFFERED, because these three are the only commands here that report
    // PROGRESS over minutes and the only ones something else watches. A C
    // stdout writing to a PIPE is block-buffered, so `--blss-train | tee`, a
    // CI log and the editor's own Neural Upscaler window all saw nothing at all
    // until the process exited and then the whole run at once - a corpus
    // render, an oracle pass and 400 epochs arriving as one lump. Total output
    // is a couple of kilobytes, so there is nothing to pay for it with.
    if (argc > 1 && std::strncmp(argv[1], "--blss-", 7) == 0)
        std::setvbuf(stdout, nullptr, _IONBF, 0);
    if (argc > 1 && std::strcmp(argv[1], "--blss-train") == 0)
        return blss::trainMain(argc, argv);
    if (argc > 1 && std::strcmp(argv[1], "--blss-eval") == 0)
        return blss::evalMain(argc, argv);
    if (argc > 1 && std::strcmp(argv[1], "--blss-emit") == 0)
        return blss::emitMain(argc, argv);
    if (argc > 1 && std::strcmp(argv[1], "--blss-coverage") == 0)
        return blssCoverageFromCli(argc, argv);
    if (argc > 1 && (std::strcmp(argv[1], "--help") == 0 || std::strcmp(argv[1], "-h") == 0)) {
        std::printf(
            "tyrax-editor [projectDir]                 open the GUI\n"
            "  --new <name> <parentDir> [w] [d] [empty|fpp|thirdperson] "
            "[unitsPerMeter] [--no-terrain]\n"
            "  --build <projectDir> [--run | --run-ps2 [ip]] [--rebuild]\n"
            "  --audit-release <projectDir>            prove a release ELF "
            "carries no devkit code\n"
            "  --debug-state [--verbose]               what is being debugged "
            "on this machine right now\n"
            "  --dump-vucap <projectDir>               decode the last VU1 "
            "capture\n"
            "  --pad <projectDir> \"<script>\"           drive the running "
            "game's controller (docs/remote-pad.md)\n"
            "  --ui-script [projectDir] \"<script>\"     drive the EDITOR's own "
            "UI (docs/ui-scripting.md)\n"
            "  --vu-check [engineDir]                  run every microprogram "
            "in the host VU1 simulator\n"
            "  --vu-emit <outDir> [engineDir]          generate .vclpp + the EE "
            "program classes\n"
            "  --vu-list <file.vclpp> [engineDir]      expand and disassemble "
            "one microprogram\n"
            "  --vu-replay <projectDir> [engineDir]    re-run a console VU1 "
            "capture on the host and diff it\n"
            "  --resave <projectDir>\n"
            "  --migrate <projectDir>                  backup + apply pending "
            "format migrations (docs/format-versioning.md)\n"
            "  --refresh-gen <projectDir>\n"
            "  --bake-gi <projectDir>                  bake global "
            "illumination + light probes\n"
            "Neural upscaler (docs/neural-upscaler.md):\n"
            "  --blss-train [<projectDir>] [-o blss.net] [--frames N] "
            "[--epochs N] [--dump <dir>]\n"
            "                                          train the BLSS net. With "
            "a project directory the corpus\n"
            "                                          is THAT PROJECT'S own "
            "scenes; without one, the built-in\n"
            "                                          procedural bestiary\n"
            "        --all-shots                       fit ALL shots, not just "
            "the split's training side:\n"
            "                                          the net to SHIP (and "
            "--blss-eval's held-out columns\n"
            "                                          then mean nothing - use "
            "--blss-eval --cv)\n"
            "  --blss-eval [<projectDir>] [-i blss.net] [--frames N] [--dump "
            "<dir>]\n"
            "                                          PSNR + flicker + "
            "occupancy: the net vs every fixed kernel\n"
            "        --cv [--cv-seeds N] [--cv-folds N] leave-one-shot-out "
            "cross-validation: trains its\n"
            "                                          own net per fold, so it "
            "ignores -i. THE honest\n"
            "                                          out-of-distribution "
            "number - one split is one draw\n"
            "        --features [--probe \"<line>\"]      what the six input "
            "channels look like over the\n"
            "                                          corpus; --probe places a "
            "console BLSSFEAT line in it\n"
            "        --drop-feature <name>[,...]       hold channels at zero - "
            "\"does this one earn its keep\"\n"
            "        --no-package-split                one bag proxy per object "
            "instead of one per VU1\n"
            "                                          package (reproduces the "
            "pre-split fold tables)\n"
            "      both take --assets <dir> --seed N --sharpen K --scale-1x2 "
            "--weight-decay W\n"
            "      --standardise, and the two\n"
            "      oracle-objective weights (sweep them as a PAIR, they trade "
            "against each other):\n"
            "        --flicker-weight W                penalty vs the "
            "reprojected history (default 0)\n"
            "        --fill-weight W                   cost per full-screen "
            "composite pass (default 16)\n"
            "  --blss-emit [-i blss.net] [-o inc/blss_net.gen.hpp]\n"
            "                                          bake a net into the C++ "
            "the game compiles (no -o: stdout)\n"
            "  --blss-coverage <projectDir> [--frames N] [--raster N] [--threads "
            "N] [--out WxH]\n"
            "                                          how much FILL the scenes "
            "ask the GS for, against the\n"
            "                                          measured break-even: the "
            "speed half of 'turn it on?'\n"
            "AI-agent tools (docs/ai-tools.md):\n"
            "  --dump <projectDir>\n"
            "  --list-nodes <projectDir>\n"
            "  --dump-graph <projectDir> <object> [scene]\n"
            "  --apply-graph <projectDir> <object> <graph.json> [scene] [--append]\n"
            "  --ai-graph <projectDir> <object> <prompt|file> [scene]\n"
            "             [--backend claude|copilot|openai] [--model <m>]\n"
            "             [--thinking]\n"
            "  --add-ai-support <projectDir> [claude] [copilot]\n"
            "  --chat-prompt [projectDir]              print the AI "
            "Assistant's system prompt (docs/ai-chat.md)\n"
            "  --search-docs \"<query>\" [page]           full-text search over "
            "the built-in documentation\n");
        return 0;
    }

    App app;
    return app.run(argc > 1 ? argv[1] : "");
}
