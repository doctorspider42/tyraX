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
    if (std::string err = project::refreshGenerated(p); !err.empty()) {
        std::fprintf(stderr, "error: %s\n", err.c_str());
        return 1;
    }
    std::printf("refreshed generated files: %s\n", p.dir.c_str());
    return 0;
}

// The GUI's AI settings (editor.ini) as CLI defaults, so --ai-graph without
// flags behaves like the editor. Only the ai* keys are read here; the full
// config lives in app.cpp.
static aigen::Config aiConfigFromEditorIni() {
    aigen::Config cfg;
    const char* base = getenv("LOCALAPPDATA");
    if (!base || !*base) base = getenv("USERPROFILE");
    if (!base || !*base) return cfg;
    std::ifstream f(std::filesystem::path(base) / "tyra-editor" / "editor.ini");
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

int main(int argc, char** argv) {
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
    if (argc > 1 && std::strcmp(argv[1], "--ai-graph") == 0)
        return aiGraphFromCli(argc, argv);
    if (argc > 1 && std::strcmp(argv[1], "--add-ai-support") == 0)
        return aiSupportFromCli(argc, argv);
    if (argc > 1 && (std::strcmp(argv[1], "--help") == 0 || std::strcmp(argv[1], "-h") == 0)) {
        std::printf(
            "tyrax-editor [projectDir]                 open the GUI\n"
            "  --new <name> <parentDir> [w] [d] [empty|fpp]\n"
            "  --build <projectDir> [--run | --run-ps2 [ip]]\n"
            "  --resave <projectDir>\n"
            "  --refresh-gen <projectDir>\n"
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
