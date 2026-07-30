#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <algorithm>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "aigen.hpp"
#include "aisupport.hpp"
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
#include "vusim.hpp"
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
          << "]";
        // Only when the scene has no ground at all - an assistant reading this
        // has to know nothing rests on the terrain here (docs/terrain.md).
        if (!sc.terrain.enabled) o << ", \"terrainRemoved\": true";
        o << ", \"layers\": [";
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
    names("credits", p.credits, [](const CreditsRoll& r) { return r.name; });
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
    auto push = [&](const livepad::State& s, bool attached) {
        const std::string e = livepad::write(path, s, ++seq, attached);
        if (e.empty()) return true;
        std::fprintf(stderr, "error: %s\n", e.c_str());
        failed = true;
        return false;
    };

    const auto t0 = std::chrono::steady_clock::now();
    for (const livepad::Step& st : steps) {
        std::printf("[pad] %s\n", st.source.c_str());
        std::fflush(stdout);
        if (!push(st.state, true)) break;
        if (st.seconds <= 0.0) continue;
        // Keep refreshing while we hold: the seq is what tells the game we are
        // still here (see livepad::kStaleFrames), and a state written once
        // would expire mid-hold on a long wait.
        const auto until = std::chrono::steady_clock::now() +
                           std::chrono::milliseconds((int)(st.seconds * 1000.0));
        while (std::chrono::steady_clock::now() < until) {
            platform::sleepMs(40);
            if (!push(st.state, true)) break;
        }
        if (failed) break;
    }
    // Detach: neutral AND flagged gone, so the game drops the overlay on its
    // next poll instead of holding the last state for the watchdog's two
    // seconds.
    push(livepad::State(), false);
    const double secs =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - t0)
            .count();
    if (!failed)
        std::printf("[pad] done - %zu step(s) in %.1fs, pad released\n",
                    steps.size(), secs);
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
    built.reserve(vugen::allAsIsDescs().size());
    std::printf("-- generated vs handwritten, in the simulator --\n");
    for (const vugen::Desc& d : vugen::allAsIsDescs()) {
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
             "core" / "programs" / "as_is" / (d.fileStem + ".vclpp"))
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

    // 3. The micro-memory budget for the generated set.
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

    const bool ok = parseFailed == 0 && mismatches == 0;
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
    for (const vugen::Desc& d : vugen::allAsIsDescs()) {
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
    std::printf("; %s - %d instructions, %d VF names, %d VI names\n", p.name.c_str(),
                (int)p.code.size(), (int)p.vfNames.size(), (int)p.viNames.size());
    for (const std::string& n : p.notes) std::printf("; note: %s\n", n.c_str());
    std::printf("%s", vusim::listing(p).c_str());
    return 0;
}

int main(int argc, char** argv) {
    if (argc > 1 && std::strcmp(argv[1], "--vu-check") == 0)
        return vuCheckFromCli(argc, argv);
    if (argc > 1 && std::strcmp(argv[1], "--vu-emit") == 0)
        return vuEmitFromCli(argc, argv);
    if (argc > 1 && std::strcmp(argv[1], "--vu-list") == 0)
        return vuListFromCli(argc, argv);
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
            "  --new <name> <parentDir> [w] [d] [empty|fpp|thirdperson] "
            "[unitsPerMeter] [--no-terrain]\n"
            "  --build <projectDir> [--run | --run-ps2 [ip]]\n"
            "  --audit-release <projectDir>            prove a release ELF "
            "carries no devkit code\n"
            "  --debug-state [--verbose]               what is being debugged "
            "on this machine right now\n"
            "  --dump-vucap <projectDir>               decode the last VU1 "
            "capture\n"
            "  --vu-check [engineDir]                  run every microprogram "
            "in the host VU1 simulator\n"
            "  --vu-emit <outDir> [engineDir]          generate .vclpp + the EE "
            "program classes\n"
            "  --vu-list <file.vclpp> [engineDir]      expand and disassemble "
            "one microprogram\n"
            "  --pad <projectDir> \"<script>\"           drive the running "
            "game's controller (docs/remote-pad.md)\n"
            "  --ui-script [projectDir] \"<script>\"     drive the EDITOR's own "
            "UI (docs/ui-scripting.md)\n"
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
