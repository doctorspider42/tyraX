#include "project.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "history.hpp"
#include "json.hpp"
#include "templates.hpp"

namespace fs = std::filesystem;

const char* primitiveTypeName(PrimitiveType t) {
    switch (t) {
        case PrimitiveType::Box: return "box";
        case PrimitiveType::Sphere: return "sphere";
        case PrimitiveType::Cylinder: return "cylinder";
        case PrimitiveType::Cone: return "cone";
        case PrimitiveType::SpawnPoint: return "spawn-point";
        case PrimitiveType::Model: return "model";
    }
    return "box";
}

static PrimitiveType primitiveTypeFromName(const std::string& s) {
    if (s == "sphere") return PrimitiveType::Sphere;
    if (s == "cylinder") return PrimitiveType::Cylinder;
    if (s == "cone") return PrimitiveType::Cone;
    if (s == "spawn-point") return PrimitiveType::SpawnPoint;
    if (s == "model") return PrimitiveType::Model;
    return PrimitiveType::Box;
}

namespace project {

static std::string writeFile(const fs::path& path, const std::string& content) {
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    std::ofstream f(path, std::ios::binary);
    if (!f) return "Cannot write file: " + path.string();
    f << content;
    return "";
}

static std::string fmtFloat(float v) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.6g", (double)v);
    return buf;
}

static std::string fmtVec3(const float* v) {
    return "[" + fmtFloat(v[0]) + ", " + fmtFloat(v[1]) + ", " + fmtFloat(v[2]) + "]";
}

static std::string objectJson(const SceneObject& o) {
    return "{ \"name\": \"" + o.name + "\", \"type\": \"" + primitiveTypeName(o.type) +
           "\", \"position\": " + fmtVec3(o.position) +
           ", \"rotation\": " + fmtVec3(o.rotation) + ", \"scale\": " + fmtVec3(o.scale) +
           ", \"color\": " + fmtVec3(o.color) +
           ", \"physics\": " + (o.physics ? "true" : "false") +
           (o.modelPath.empty() ? "" : ", \"model\": \"" + o.modelPath + "\"") + " }";
}

// "objects": [ ... ] with the given indentation for entries
static void writeObjectsArray(std::ostringstream& json, const std::vector<SceneObject>& objects,
                              const std::string& indent) {
    json << "[";
    for (size_t i = 0; i < objects.size(); ++i)
        json << (i ? ",\n" + indent : "\n" + indent) << objectJson(objects[i]);
    if (!objects.empty()) json << "\n" << indent.substr(0, indent.size() > 2 ? indent.size() - 2 : 0);
    json << "]";
}

std::string save(const Project& p) {
    std::ostringstream json;
    json << "{\n"
         << "  \"name\": \"" << p.name << "\",\n"
         << "  \"terrain\": {\n"
         << "    \"width\": " << p.terrain.width << ",\n"
         << "    \"depth\": " << p.terrain.depth << "\n"
         << "  },\n"
         << "  \"template\": \"" << p.gameTemplate << "\",\n"
         << "  \"settings\": {\n"
         << "    \"clipping\": \"" << p.settings.clipping << "\",\n"
         << "    \"terrainDetail\": " << p.settings.terrainDetail << ",\n"
         << "    \"skyColor\": " << fmtVec3(p.settings.skyColor) << ",\n"
         << "    \"skyTopColor\": " << fmtVec3(p.settings.skyTopColor) << ",\n"
         << "    \"skyDome\": " << (p.settings.skyDome ? "true" : "false") << ",\n"
         << "    \"eyeHeight\": " << fmtFloat(p.settings.eyeHeight) << ",\n"
         << "    \"walkSpeed\": " << fmtFloat(p.settings.walkSpeed) << ",\n"
         << "    \"lookSpeed\": " << fmtFloat(p.settings.lookSpeed) << ",\n"
         << "    \"orbitSpeed\": " << fmtFloat(p.settings.orbitSpeed) << ",\n"
         << "    \"gravity\": " << fmtFloat(p.settings.gravity) << ",\n"
         << "    \"jumpSpeed\": " << fmtFloat(p.settings.jumpSpeed) << ",\n"
         << "    \"lightDir\": " << fmtVec3(p.settings.lightDir) << ",\n"
         << "    \"ambient\": " << fmtFloat(p.settings.ambient) << ",\n"
         << "    \"diffuse\": " << fmtFloat(p.settings.diffuse) << "\n"
         << "  },\n"
         << "  \"scenes\": [";
    for (size_t i = 0; i < p.scenes.size(); ++i) {
        if (i) json << ", ";
        json << "\"" << p.scenes[i] << "\"";
    }
    json << "],\n"
         << "  \"objects\": ";
    writeObjectsArray(json, p.objects, "    ");
    json << ",\n  \"hud\": [";
    for (size_t i = 0; i < p.hud.size(); ++i) {
        const HudImage& h = p.hud[i];
        json << (i ? ",\n    " : "\n    ") << "{ \"name\": \"" << h.name << "\", \"image\": \""
             << h.imagePath << "\", \"pos\": [" << fmtFloat(h.pos[0]) << ", "
             << fmtFloat(h.pos[1]) << "], \"size\": [" << fmtFloat(h.size[0]) << ", "
             << fmtFloat(h.size[1]) << "] }";
    }
    json << (p.hud.empty() ? "]" : "\n  ]");
    json << ",\n  \"flowGraph\": {\n"
         << "    \"nextId\": " << p.flowGraph.nextId << ",\n"
         << "    \"nodes\": [";
    for (size_t i = 0; i < p.flowGraph.nodes.size(); ++i) {
        const FlowNode& n = p.flowGraph.nodes[i];
        json << (i ? ",\n      " : "\n      ") << "{ \"id\": " << n.id << ", \"type\": \""
             << n.type << "\", \"pos\": [" << fmtFloat(n.pos[0]) << ", " << fmtFloat(n.pos[1])
             << "], \"str\": \"" << n.str << "\", \"num\": " << fmtVec3(n.num) << " }";
    }
    json << (p.flowGraph.nodes.empty() ? "],\n" : "\n    ],\n") << "    \"links\": [";
    for (size_t i = 0; i < p.flowGraph.links.size(); ++i) {
        const FlowLink& l = p.flowGraph.links[i];
        json << (i ? ",\n      " : "\n      ") << "{ \"id\": " << l.id
             << ", \"from\": " << l.fromNode << ", \"to\": " << l.toNode << " }";
    }
    json << (p.flowGraph.links.empty() ? "]\n" : "\n    ]\n") << "  }\n}\n";
    return writeFile(fs::path(p.dir) / "project.json", json.str());
}

std::string create(Project& out, const std::string& name, const std::string& parentDir,
                   const TerrainConfig& terrain, const std::string& gameTemplate) {
    if (name.empty()) return "Project name is empty";
    for (char c : name) {
        if (!isalnum((unsigned char)c) && c != '-' && c != '_')
            return "Project name may contain only letters, digits, '-' and '_'";
    }
    if (parentDir.empty()) return "Location is empty";

    fs::path root = fs::path(parentDir) / name;
    std::error_code ec;
    if (fs::exists(root, ec) && !fs::is_empty(root, ec))
        return "Directory already exists and is not empty: " + root.string();
    fs::create_directories(root, ec);
    if (ec) return "Cannot create directory: " + root.string() + " (" + ec.message() + ")";

    out = Project{};
    out.name = name;
    out.dir = root.string();
    out.terrain = terrain;
    out.gameTemplate = gameTemplate == "fpp" ? "fpp" : "orbit";

    if (out.gameTemplate == "fpp") {
        // FPP scenes need a player start - seed one in the terrain center.
        SceneObject spawn;
        spawn.name = "spawn-1";
        spawn.type = PrimitiveType::SpawnPoint;
        spawn.position[1] = 0.0f;
        spawn.color[0] = 0.15f, spawn.color[1] = 0.9f, spawn.color[2] = 0.9f;
        out.objects.push_back(spawn);

        // ...and a box in front of the player, so the example script
        // (walk close + press X) has something to interact with.
        SceneObject box;
        box.name = "box-1";
        box.position[0] = 0.0f, box.position[1] = 1.0f, box.position[2] = 6.0f;
        box.scale[0] = box.scale[1] = box.scale[2] = 2.0f;
        out.objects.push_back(box);

        // ...and a physics demo: a sphere that drops from the sky at start.
        SceneObject ball;
        ball.name = "ball-1";
        ball.type = PrimitiveType::Sphere;
        ball.position[0] = 3.0f, ball.position[1] = 10.0f, ball.position[2] = 6.0f;
        ball.color[0] = 0.25f, ball.color[1] = 0.45f, ball.color[2] = 0.9f;
        ball.physics = true;
        out.objects.push_back(ball);
    }

    for (const auto& f : templates::generate(out)) {
        if (auto err = writeFile(root / f.relativePath, f.content); !err.empty()) return err;
    }
    if (auto err = save(out); !err.empty()) return err;

    // Every project is born with its solution file (projects are opened
    // through it) and a single-entry history.
    History h;
    h.reset({out.terrain, out.objects});
    return saveSolution(out, h, -1, 0, 0);
}

static void readVec3(const json::Value* v, float* out) {
    if (!v || v->type != json::Value::Type::Array || v->arr.size() < 3) return;
    for (int i = 0; i < 3; ++i) out[i] = (float)v->arr[i].numberOr(out[i]);
}

static void readObjectsArray(const json::Value& arr, std::vector<SceneObject>& out) {
    if (arr.type != json::Value::Type::Array) return;
    for (const auto& jo : arr.arr) {
        if (jo.type != json::Value::Type::Object) continue;
        SceneObject o;
        if (const auto* v = jo.find("name")) o.name = v->stringOr("object");
        if (const auto* v = jo.find("type")) o.type = primitiveTypeFromName(v->stringOr("box"));
        readVec3(jo.find("position"), o.position);
        readVec3(jo.find("rotation"), o.rotation);
        readVec3(jo.find("scale"), o.scale);
        readVec3(jo.find("color"), o.color);
        if (const auto* v = jo.find("physics"))
            o.physics = v->type == json::Value::Type::Bool && v->boolean;
        if (const auto* v = jo.find("model")) o.modelPath = v->stringOr("");
        out.push_back(std::move(o));
    }
}

std::string load(Project& out, const std::string& projectDir) {
    fs::path jsonPath = fs::path(projectDir) / "project.json";
    std::ifstream f(jsonPath, std::ios::binary);
    if (!f) return "Not a tyra-editor project (missing project.json): " + projectDir;
    std::stringstream ss;
    ss << f.rdbuf();

    json::Value root;
    if (!json::parse(ss.str(), root) || root.type != json::Value::Type::Object)
        return "project.json is malformed";

    out = Project{};
    out.dir = fs::path(projectDir).string();
    if (const auto* v = root.find("name")) out.name = v->stringOr("");
    if (out.name.empty()) return "project.json is malformed (no name)";

    if (const auto* terrain = root.find("terrain")) {
        if (const auto* v = terrain->find("width")) out.terrain.width = (int)v->numberOr(64);
        if (const auto* v = terrain->find("depth")) out.terrain.depth = (int)v->numberOr(64);
    }

    if (const auto* v = root.find("template"))
        out.gameTemplate = v->stringOr("orbit") == "fpp" ? "fpp" : "orbit";

    if (const auto* s = root.find("settings")) {
        ProjectSettings& st = out.settings;
        if (const auto* v = s->find("clipping"))
            st.clipping = v->stringOr("precise") == "fast" ? "fast" : "precise";
        if (const auto* v = s->find("terrainDetail"))
            st.terrainDetail = (int)v->numberOr(32);
        if (st.terrainDetail < 4) st.terrainDetail = 4;
        if (st.terrainDetail > 128) st.terrainDetail = 128;
        readVec3(s->find("skyColor"), st.skyColor);
        readVec3(s->find("skyTopColor"), st.skyTopColor);
        if (const auto* v = s->find("skyDome"))
            st.skyDome = v->type == json::Value::Type::Bool && v->boolean;
        if (const auto* v = s->find("eyeHeight")) st.eyeHeight = (float)v->numberOr(1.8);
        if (const auto* v = s->find("walkSpeed")) st.walkSpeed = (float)v->numberOr(0.4);
        if (const auto* v = s->find("lookSpeed")) st.lookSpeed = (float)v->numberOr(1.0);
        if (const auto* v = s->find("orbitSpeed")) st.orbitSpeed = (float)v->numberOr(1.0);
        if (const auto* v = s->find("gravity")) st.gravity = (float)v->numberOr(9.8);
        if (const auto* v = s->find("jumpSpeed")) st.jumpSpeed = (float)v->numberOr(4.5);
        readVec3(s->find("lightDir"), st.lightDir);
        if (const auto* v = s->find("ambient")) st.ambient = (float)v->numberOr(0.55);
        if (const auto* v = s->find("diffuse")) st.diffuse = (float)v->numberOr(0.45);
    }

    if (const auto* scenes = root.find("scenes");
        scenes && scenes->type == json::Value::Type::Array && !scenes->arr.empty()) {
        out.scenes.clear();
        for (const auto& s : scenes->arr) out.scenes.push_back(s.stringOr("main"));
    }

    if (const auto* objects = root.find("objects");
        objects && objects->type == json::Value::Type::Array) {
        readObjectsArray(*objects, out.objects);
    }

    if (const auto* hud = root.find("hud"); hud && hud->type == json::Value::Type::Array) {
        for (const auto& jh : hud->arr) {
            HudImage h;
            if (const auto* v = jh.find("name")) h.name = v->stringOr("image");
            if (const auto* v = jh.find("image")) h.imagePath = v->stringOr("");
            if (const auto* v = jh.find("pos");
                v && v->type == json::Value::Type::Array && v->arr.size() >= 2) {
                h.pos[0] = (float)v->arr[0].numberOr(0.5);
                h.pos[1] = (float)v->arr[1].numberOr(0.5);
            }
            if (const auto* v = jh.find("size");
                v && v->type == json::Value::Type::Array && v->arr.size() >= 2) {
                h.size[0] = (float)v->arr[0].numberOr(64);
                h.size[1] = (float)v->arr[1].numberOr(64);
            }
            if (!h.imagePath.empty()) out.hud.push_back(std::move(h));
        }
    }

    if (const auto* fg = root.find("flowGraph")) {
        if (const auto* v = fg->find("nextId")) out.flowGraph.nextId = (int)v->numberOr(1);
        if (const auto* nodes = fg->find("nodes");
            nodes && nodes->type == json::Value::Type::Array) {
            for (const auto& jn : nodes->arr) {
                FlowNode n;
                if (const auto* v = jn.find("id")) n.id = (int)v->numberOr(0);
                if (const auto* v = jn.find("type")) n.type = v->stringOr("");
                if (const auto* v = jn.find("pos");
                    v && v->type == json::Value::Type::Array && v->arr.size() >= 2) {
                    n.pos[0] = (float)v->arr[0].numberOr(0);
                    n.pos[1] = (float)v->arr[1].numberOr(0);
                }
                if (const auto* v = jn.find("str")) n.str = v->stringOr("");
                readVec3(jn.find("num"), n.num);
                if (n.id > 0 && flowNodeType(n.type)) out.flowGraph.nodes.push_back(n);
            }
        }
        if (const auto* links = fg->find("links");
            links && links->type == json::Value::Type::Array) {
            for (const auto& jl : links->arr) {
                FlowLink l;
                if (const auto* v = jl.find("id")) l.id = (int)v->numberOr(0);
                if (const auto* v = jl.find("from")) l.fromNode = (int)v->numberOr(0);
                if (const auto* v = jl.find("to")) l.toNode = (int)v->numberOr(0);
                if (l.id > 0) out.flowGraph.links.push_back(l);
            }
        }
    }
    return "";
}

// --- solution file (<name>.tyra) --------------------------------------------

static fs::path solutionPath(const Project& p) {
    return fs::path(p.dir) / (p.name + ".tyra");
}

std::string saveSolution(const Project& p, const History& h, int selectedObject, int gizmoOp,
                         int viewMode) {
    std::ostringstream json;
    json << "{\n"
         << "  \"version\": 1,\n"
         << "  \"project\": \"project.json\",\n"
         << "  \"editor\": { \"selectedObject\": " << selectedObject
         << ", \"gizmo\": " << gizmoOp << ", \"viewMode\": " << viewMode << " },\n"
         << "  \"history\": {\n"
         << "    \"index\": " << h.index() << ",\n"
         << "    \"entries\": [";
    const auto& entries = h.entries();
    for (size_t i = 0; i < entries.size(); ++i) {
        const SceneSnapshot& s = entries[i];
        json << (i ? ",\n      " : "\n      ")
             << "{ \"terrain\": { \"width\": " << s.terrain.width
             << ", \"depth\": " << s.terrain.depth << " }, \"objects\": ";
        writeObjectsArray(json, s.objects, "        ");
        json << " }";
    }
    json << (entries.empty() ? "]\n" : "\n    ]\n") << "  }\n}\n";
    return writeFile(solutionPath(p), json.str());
}

std::string loadSolution(const Project& p, History& h, int& selectedObject, int& gizmoOp,
                         int& viewMode) {
    std::ifstream f(solutionPath(p), std::ios::binary);
    if (!f) return "no solution file";
    std::stringstream ss;
    ss << f.rdbuf();

    json::Value root;
    if (!json::parse(ss.str(), root) || root.type != json::Value::Type::Object)
        return "solution file is malformed";

    const auto* hist = root.find("history");
    if (!hist) return "solution file has no history";
    const auto* entriesVal = hist->find("entries");
    if (!entriesVal || entriesVal->type != json::Value::Type::Array || entriesVal->arr.empty())
        return "solution history is empty";

    std::vector<SceneSnapshot> entries;
    for (const auto& je : entriesVal->arr) {
        SceneSnapshot s;
        if (const auto* terrain = je.find("terrain")) {
            if (const auto* v = terrain->find("width")) s.terrain.width = (int)v->numberOr(64);
            if (const auto* v = terrain->find("depth")) s.terrain.depth = (int)v->numberOr(64);
        }
        if (const auto* objects = je.find("objects")) readObjectsArray(*objects, s.objects);
        entries.push_back(std::move(s));
    }

    int index = 0;
    if (const auto* v = hist->find("index")) index = (int)v->numberOr(0);
    if (index < 0 || index >= (int)entries.size()) return "solution history index is invalid";

    // Stale check: project.json is the source of truth for the current state.
    // If it was edited outside the editor, the persisted history no longer applies.
    if (!(entries[index] == SceneSnapshot{p.terrain, p.objects}))
        return "solution history is stale (project.json changed outside the editor)";

    h.restore(std::move(entries), index);

    if (const auto* editor = root.find("editor")) {
        if (const auto* v = editor->find("selectedObject"))
            selectedObject = (int)v->numberOr(-1);
        if (const auto* v = editor->find("gizmo")) gizmoOp = (int)v->numberOr(0);
        if (const auto* v = editor->find("viewMode")) viewMode = (int)v->numberOr(0);
    }
    if (selectedObject >= (int)p.objects.size()) selectedObject = -1;
    if (gizmoOp < 0 || gizmoOp > 2) gizmoOp = 0;
    if (viewMode < 0 || viewMode > 2) viewMode = 0;
    return "";
}

std::string refreshGenerated(const Project& p) {
    for (const auto& f : templates::generate(p)) {
        const fs::path path = fs::path(p.dir) / f.relativePath;

        bool write = false;
        if (f.relativePath == "Dockerfile" || f.relativePath == "docker-compose.yml" ||
            f.relativePath == "inc\\terrain_config.hpp" ||
            f.relativePath == "inc\\scene_data.hpp" ||
            f.relativePath == ".vscode\\c_cpp_properties.json" ||
            f.relativePath == "src\\scripts\\flow_graph.gen.cpp" ||
            f.relativePath == "inc\\model_data.gen.hpp" ||
            f.relativePath == "inc\\hud_data.gen.hpp") {
            write = true;  // editor-owned, always in sync with project data
        } else if (f.relativePath == "src\\terrain_game.cpp" ||
                   f.relativePath == "inc\\terrain_game.hpp" ||
                   f.relativePath == "inc\\scripts\\script.hpp") {
            // Regenerate while the ownership marker is present, or when the
            // file is byte-identical to an old template (never user-edited).
            std::ifstream existing(path, std::ios::binary);
            if (!existing) {
                write = true;
            } else {
                std::stringstream content;
                content << existing.rdbuf();
                std::string firstLine = content.str().substr(0, content.str().find('\n'));
                write = firstLine.find("Generated by tyra-editor") != std::string::npos ||
                        templates::matchesLegacy(p, f.relativePath, content.str());
            }
        }

        if (write) {
            if (auto err = writeFile(path, f.content); !err.empty()) return err;
        }
    }
    return "";
}

}  // namespace project
