#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>

#include "aigen.hpp"
#include "aisupport.hpp"
#include "devsession.hpp"
#include "editorcfg.hpp"
#include "elfsym.hpp"
#include "gibake.hpp"
#include "livedbg.hpp"
#include "vucap.hpp"
#include "app.hpp"
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
    std::printf(
        "\nA game running right now is the most current source of all, and it "
        "is a process query, not a file one:\n  Get-CimInstance Win32_Process "
        "-Filter \"name='pcsx2-qt.exe'\" | Select-Object CommandLine\nThe -elf "
        "path in it is <projectDir>\\bin\\<name>.elf.\n");
    return 0;
}

// Headless helper:
//   tyrax-editor.exe --new <name> <parentDir> [width] [depth]
//                    [empty|fpp|thirdperson] [unitsPerMeter]
static int createFromCli(int argc, char** argv) {
    if (argc < 4) {
        std::fprintf(stderr,
                     "usage: tyrax-editor --new <name> <parentDir> [width] [depth] "
                     "[empty|fpp|thirdperson] [unitsPerMeter]\n");
        return 2;
    }
    TerrainConfig t;
    if (argc > 4) t.width = std::atoi(argv[4]);
    if (argc > 5) t.depth = std::atoi(argv[5]);
    const char* preset = argc > 6 ? argv[6] : "empty";
    // World scale (docs/world-scale.md); 1 unit = 1 m unless asked otherwise.
    const float ups = argc > 7 ? (float)std::atof(argv[7]) : 1.0f;

    Project p;
    std::string err = project::create(p, argv[2], argv[3], t, preset, ups);
    if (!err.empty()) {
        std::fprintf(stderr, "error: %s\n", err.c_str());
        return 1;
    }
    std::printf("created: %s (terrain %dx%d, %.3f units/m)\n", p.dir.c_str(),
                p.scenes[0].terrain.width, p.scenes[0].terrain.depth,
                p.settings.unitsPerMeter);
    return 0;
}

// Headless helper: tyrax-editor.exe --build <projectDir> [--run | --run-ps2 [ip]]
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
    bakeProcedural(p);

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

// ---------------------------------------------------------------------------
// AI-agent CLI (docs/ai-tools.md): machine-readable project inspection and
// flow-graph manipulation, so an AI assistant working inside a generated
// project can read and steer it without driving the GUI.
// ---------------------------------------------------------------------------

static std::string cliJsonEsc(const std::string& s) {
    std::string out;
    for (char c : s) {
        if (c == '"' || c == '\\') out += '\\';
        if (c == '\n') { out += "\\n"; continue; }
        out += c;
    }
    return out;
}

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
    std::ostringstream o;
    o << "{ \"name\": \"" << cliJsonEsc(p.name) << "\", \"template\": \""
      << p.gameTemplate << "\", \"scenes\": [";
    for (size_t si = 0; si < p.scenes.size(); ++si) {
        const SceneData& sc = p.scenes[si];
        o << (si ? ", " : "") << "{ \"name\": \"" << cliJsonEsc(sc.name)
          << "\", \"terrain\": [" << sc.terrain.width << ", " << sc.terrain.depth
          << "], \"layers\": [";
        for (size_t i = 0; i < sc.layers.size(); ++i)
            o << (i ? ", " : "") << "\"" << cliJsonEsc(sc.layers[i].name) << "\"";
        o << "], \"objects\": [";
        for (size_t i = 0; i < sc.objects.size(); ++i) {
            const SceneObject& ob = sc.objects[i];
            o << (i ? ", " : "") << "{ \"name\": \"" << cliJsonEsc(ob.name)
              << "\", \"type\": \"" << primitiveTypeName(ob.type)
              << "\", \"position\": [" << ob.position[0] << ", " << ob.position[1]
              << ", " << ob.position[2] << "]";
            if (ob.usable) o << ", \"usable\": true";
            if (!ob.modelPath.empty())
                o << ", \"model\": \"" << cliJsonEsc(ob.modelPath) << "\"";
            if (!ob.layer.empty())
                o << ", \"layer\": \"" << cliJsonEsc(ob.layer) << "\"";
            if (!ob.flowGraph.empty())
                o << ", \"flowGraphNodes\": " << ob.flowGraph.nodes.size();
            if (!ob.scripts.empty()) {
                o << ", \"scripts\": [";
                for (size_t k = 0; k < ob.scripts.size(); ++k)
                    o << (k ? ", " : "") << "\"" << cliJsonEsc(ob.scripts[k]) << "\"";
                o << "]";
            }
            o << " }";
        }
        o << "] }";
    }
    auto strList = [&o](const char* key, const std::vector<std::string>& v) {
        o << ", \"" << key << "\": [";
        for (size_t i = 0; i < v.size(); ++i)
            o << (i ? ", " : "") << "\"" << cliJsonEsc(v[i]) << "\"";
        o << "]";
    };
    o << "]";
    strList("music", p.music);
    strList("sounds", p.sounds);
    auto names = [&strList](const char* key, const auto& v, auto name) {
        std::vector<std::string> out;
        for (const auto& e : v) out.push_back(name(e));
        strList(key, out);
    };
    names("saveValues", p.saveValues, [](const SaveValue& v) { return v.name; });
    names("saveTexts", p.saveTexts, [](const SaveTextValue& v) { return v.name; });
    names("menus", p.menus, [](const GameMenu& m) { return m.name; });
    names("hudTexts", p.hudTexts, [](const HudText& t) { return t.name; });
    names("gradings", p.gradings, [](const ColorGradingPreset& g) { return g.name; });
    names("ambiencePresets", p.ambiencePresets,
          [](const AmbiencePreset& a) { return a.name; });
    names("sequences", p.sequences, [](const Sequence& s) { return s.name; });
    // Input actions / binding presets: what On Action and Set Input Preset
    // reference (docs/input-bindings.md).
    names("inputActions", p.input.actions,
          [](const InputAction& a) { return a.name; });
    names("inputPresets", p.input.presets,
          [](const InputPreset& v) { return v.name; });
    o << " }\n";
    std::printf("%s", o.str().c_str());
    return 0;
}

// tyrax-editor.exe --dump-graph <projectDir> <objectName> [sceneName]
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

// tyrax-editor.exe --add-ai-support <projectDir> [claude] [copilot]
// Installs the AI assistant skills into an existing project (same files the
// "Add AI support" option writes at project creation).
static int aiSupportFromCli(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr,
                     "usage: tyrax-editor --add-ai-support <projectDir> "
                     "[claude] [copilot]\n");
        return 2;
    }
    bool claude = false, copilot = false;
    for (int i = 3; i < argc; ++i) {
        if (std::strcmp(argv[i], "claude") == 0) claude = true;
        if (std::strcmp(argv[i], "copilot") == 0) copilot = true;
    }
    if (!claude && !copilot) claude = true;  // default: Claude
    const std::string status = aisupport::install(argv[2], claude, copilot);
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

int main(int argc, char** argv) {
    if (argc > 1 && std::strcmp(argv[1], "--debug-state") == 0)
        return debugStateFromCli(argc, argv);
    if (argc > 1 && std::strcmp(argv[1], "--dump-vucap") == 0)
        return dumpVuCapFromCli(argc, argv);
    if (argc > 1 && std::strcmp(argv[1], "--symbolize") == 0)
        return symbolizeFromCli(argc, argv);
    if (argc > 1 && std::strcmp(argv[1], "--audit-release") == 0)
        return auditReleaseFromCli(argc, argv);
    if (argc > 1 && std::strcmp(argv[1], "--new") == 0) return createFromCli(argc, argv);
    if (argc > 1 && std::strcmp(argv[1], "--build") == 0) return buildFromCli(argc, argv);
    if (argc > 1 && std::strcmp(argv[1], "--resave") == 0) return resaveFromCli(argc, argv);
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
    if (argc > 1 && (std::strcmp(argv[1], "--help") == 0 || std::strcmp(argv[1], "-h") == 0)) {
        std::printf(
            "tyrax-editor [projectDir]                 open the GUI\n"
            "  --new <name> <parentDir> [w] [d] [empty|fpp|thirdperson] [unitsPerMeter]\n"
            "  --build <projectDir> [--run | --run-ps2 [ip]]\n"
            "  --audit-release <projectDir>            prove a release ELF "
            "carries no devkit code\n"
            "  --debug-state [--verbose]               what is being debugged "
            "on this machine right now\n"
            "  --dump-vucap <projectDir>               decode the last VU1 "
            "capture\n"
            "  --resave <projectDir>\n"
            "  --refresh-gen <projectDir>\n"
            "  --bake-gi <projectDir>                  bake global "
            "illumination + light probes\n"
            "AI-agent tools (docs/ai-tools.md):\n"
            "  --dump <projectDir>\n"
            "  --list-nodes <projectDir>\n"
            "  --dump-graph <projectDir> <object> [scene]\n"
            "  --apply-graph <projectDir> <object> <graph.json> [scene] [--append]\n"
            "  --ai-graph <projectDir> <object> <prompt|file> [scene]\n"
            "             [--backend claude|copilot|openai] [--model <m>]\n"
            "             [--thinking]\n"
            "  --add-ai-support <projectDir> [claude] [copilot]\n");
        return 0;
    }

    App app;
    return app.run(argc > 1 ? argv[1] : "");
}
