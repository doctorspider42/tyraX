#include "project.hpp"

#include <cmath>
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
        case PrimitiveType::Player: return "player";
    }
    return "box";
}

static PrimitiveType primitiveTypeFromName(const std::string& s) {
    if (s == "sphere") return PrimitiveType::Sphere;
    if (s == "cylinder") return PrimitiveType::Cylinder;
    if (s == "cone") return PrimitiveType::Cone;
    if (s == "spawn-point") return PrimitiveType::SpawnPoint;
    if (s == "model") return PrimitiveType::Model;
    if (s == "player") return PrimitiveType::Player;
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

static void readVec3(const json::Value* v, float* out);

// "flowGraph": { nodes, links, nextId } - shared by objects (per-object
// graphs) and the legacy project-level graph reader.
static std::string flowGraphJson(const FlowGraph& fg) {
    std::string json = "{ \"nextId\": " + std::to_string(fg.nextId) + ", \"nodes\": [";
    for (size_t i = 0; i < fg.nodes.size(); ++i) {
        const FlowNode& n = fg.nodes[i];
        json += std::string(i ? ", " : "") + "{ \"id\": " + std::to_string(n.id) +
                ", \"type\": \"" + n.type + "\", \"pos\": [" + fmtFloat(n.pos[0]) + ", " +
                fmtFloat(n.pos[1]) + "], \"str\": \"" + n.str +
                "\", \"num\": " + fmtVec3(n.num) + " }";
    }
    json += "], \"links\": [";
    for (size_t i = 0; i < fg.links.size(); ++i) {
        const FlowLink& l = fg.links[i];
        json += std::string(i ? ", " : "") + "{ \"id\": " + std::to_string(l.id) +
                ", \"from\": " + std::to_string(l.fromNode) +
                ", \"to\": " + std::to_string(l.toNode) +
                (l.kind == FlowLinkObject ? ", \"data\": true" : "") +
                (l.kind == FlowLinkPos ? ", \"pos\": true" : "") + " }";
    }
    return json + "] }";
}

static void readFlowGraph(const json::Value& jg, FlowGraph& fg) {
    if (const auto* v = jg.find("nextId")) fg.nextId = (int)v->numberOr(1);
    if (const auto* nodes = jg.find("nodes");
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
            if (n.id > 0 && flowNodeType(n.type)) fg.nodes.push_back(n);
        }
    }
    if (const auto* links = jg.find("links");
        links && links->type == json::Value::Type::Array) {
        for (const auto& jl : links->arr) {
            FlowLink l;
            if (const auto* v = jl.find("id")) l.id = (int)v->numberOr(0);
            if (const auto* v = jl.find("from")) l.fromNode = (int)v->numberOr(0);
            if (const auto* v = jl.find("to")) l.toNode = (int)v->numberOr(0);
            if (const auto* v = jl.find("data");
                v && v->type == json::Value::Type::Bool && v->boolean)
                l.kind = FlowLinkObject;
            if (const auto* v = jl.find("pos");
                v && v->type == json::Value::Type::Bool && v->boolean)
                l.kind = FlowLinkPos;
            if (l.id > 0) fg.links.push_back(l);
        }
    }
}

static std::string objectJson(const SceneObject& o) {
    std::string json =
        "{ \"name\": \"" + o.name + "\", \"type\": \"" + primitiveTypeName(o.type) +
        "\", \"position\": " + fmtVec3(o.position) +
        ", \"rotation\": " + fmtVec3(o.rotation) + ", \"scale\": " + fmtVec3(o.scale) +
        ", \"color\": " + fmtVec3(o.color) +
        ", \"physics\": " + (o.physics ? "true" : "false") +
        (o.modelPath.empty() ? "" : ", \"model\": \"" + o.modelPath + "\"") +
        (o.texturePath.empty() ? "" : ", \"texture\": \"" + o.texturePath + "\"");
    if (o.type == PrimitiveType::Player) {
        json += ", \"player\": { \"mode\": \"" +
                std::string(o.playerMode == 1 ? "noclip" : "walk") +
                "\", \"walkSpeed\": " + fmtFloat(o.playerWalkSpeed) +
                ", \"lookSpeed\": " + fmtFloat(o.playerLookSpeed) +
                ", \"eyeHeight\": " + fmtFloat(o.playerEyeHeight) +
                ", \"jumpSpeed\": " + fmtFloat(o.playerJumpSpeed) +
                ", \"canJump\": " + (o.playerCanJump ? "true" : "false") + " }";
    }
    if (!o.flowGraph.empty()) json += ", \"flowGraph\": " + flowGraphJson(o.flowGraph);
    return json + " }";
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
         << "    \"diffuse\": " << fmtFloat(p.settings.diffuse) << ",\n"
         << "    \"lightColor\": " << fmtVec3(p.settings.lightColor) << ",\n"
         << "    \"brightness\": " << fmtFloat(p.settings.brightness) << ",\n"
         << "    \"terrainTexture\": \"" << p.settings.terrainTexture << "\",\n"
         << "    \"terrainTexScale\": " << fmtFloat(p.settings.terrainTexScale) << "\n"
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
    json << ",\n  \"music\": [";
    for (size_t i = 0; i < p.music.size(); ++i)
        json << (i ? ", " : "") << "\"" << p.music[i] << "\"";
    json << "]";
    json << ",\n  \"sounds\": [";
    for (size_t i = 0; i < p.sounds.size(); ++i)
        json << (i ? ", " : "") << "\"" << p.sounds[i] << "\"";
    json << "]";
    json << "\n}\n";
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
    // "showcase" is a content preset on top of the FPP game template
    const bool showcase = gameTemplate == "showcase";
    out.gameTemplate = (gameTemplate == "fpp" || showcase) ? "fpp" : "orbit";

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

    ensureHeightmap(out);

    if (showcase) {
        // Built-in assets (written before generate(): model codegen reads them)
        if (auto err = writeFile(root / "res" / "models" / "house.obj",
                                 templates::houseObjText());
            !err.empty())
            return err;
        {
            size_t pngSize = 0;
            const unsigned char* png = templates::crosshairPng(pngSize);
            std::error_code ec;
            fs::create_directories(root / "res" / "hud", ec);
            std::ofstream f(root / "res" / "hud" / "crosshair.png", std::ios::binary);
            if (!f) return "Cannot write crosshair.png";
            f.write((const char*)png, (std::streamsize)pngSize);
        }

        // A house model...
        SceneObject house;
        house.name = "house-1";
        house.type = PrimitiveType::Model;
        house.modelPath = "res/models/house.obj";
        house.position[0] = -7.0f, house.position[1] = 0.0f, house.position[2] = 9.0f;
        house.rotation[1] = 30.0f;
        house.scale[0] = house.scale[1] = house.scale[2] = 2.5f;
        house.color[0] = 0.9f, house.color[1] = 0.85f, house.color[2] = 0.7f;
        out.objects.push_back(house);

        // ...a pillar to jump on...
        SceneObject pillar;
        pillar.name = "pillar-1";
        pillar.type = PrimitiveType::Cylinder;
        pillar.position[0] = 5.0f, pillar.position[1] = 0.5f, pillar.position[2] = 10.0f;
        pillar.scale[0] = 2.0f, pillar.scale[1] = 1.0f, pillar.scale[2] = 2.0f;
        pillar.color[0] = 0.7f, pillar.color[1] = 0.7f, pillar.color[2] = 0.75f;
        out.objects.push_back(pillar);

        // ...a HUD crosshair...
        HudImage crosshair;
        crosshair.name = "crosshair";
        crosshair.imagePath = "res/hud/crosshair.png";
        crosshair.size[0] = crosshair.size[1] = 40.0f;
        out.hud.push_back(crosshair);

        // ...and two small per-object flow graphs. Object params are left
        // empty = "self", so both graphs survive copy-paste unchanged.
        // box-1: Circle toggles the box.
        for (SceneObject& obj : out.objects) {
            if (obj.name == "box-1") {
                FlowGraph& fg = obj.flowGraph;
                FlowNode onButton;
                onButton.id = fg.nextId++;
                onButton.type = "OnButton";
                onButton.str = "Circle";
                onButton.pos[0] = 40, onButton.pos[1] = 40;
                FlowNode toggle;
                toggle.id = fg.nextId++;
                toggle.type = "ToggleObject";  // str empty = self
                toggle.pos[0] = 300, toggle.pos[1] = 40;
                fg.nodes = {onButton, toggle};
                FlowLink l1;
                l1.id = fg.nextId++;
                l1.fromNode = onButton.id;
                l1.toNode = toggle.id;
                fg.links = {l1};
            } else if (obj.name == "house-1") {
                // house-1: walking up to it greets you in the log.
                FlowGraph& fg = obj.flowGraph;
                FlowNode near;
                near.id = fg.nextId++;
                near.type = "NearObject";  // str empty = self
                near.num[0] = 6.0f;
                near.pos[0] = 40, near.pos[1] = 40;
                FlowNode log;
                log.id = fg.nextId++;
                log.type = "Log";
                log.str = "Welcome home!";
                log.pos[0] = 300, log.pos[1] = 40;
                fg.nodes = {near, log};
                FlowLink l2;
                l2.id = fg.nextId++;
                l2.fromNode = near.id;
                l2.toNode = log.id;
                fg.links = {l2};
            }
        }
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

// --- Terrain heightmap -------------------------------------------------------

void terrainGridDims(const Project& p, int& vertsW, int& vertsD) {
    const int maxCells = p.settings.terrainDetail;
    const int cellsX = p.terrain.width > maxCells ? maxCells : p.terrain.width;
    const int cellsZ = p.terrain.depth > maxCells ? maxCells : p.terrain.depth;
    vertsW = cellsX + 1;
    vertsD = cellsZ + 1;
}

void ensureHeightmap(Project& p) {
    int vw = 0, vd = 0;
    terrainGridDims(p, vw, vd);
    if (p.hmW == vw && p.hmD == vd && (int)p.heights.size() == vw * vd) return;

    std::vector<float> next((size_t)vw * vd, 0.0f);
    if (p.hmW > 1 && p.hmD > 1 && (int)p.heights.size() == p.hmW * p.hmD) {
        // nearest-neighbor resample from the previous grid
        for (int z = 0; z < vd; ++z)
            for (int x = 0; x < vw; ++x) {
                const int sx = (int)((float)x / (vw - 1) * (p.hmW - 1) + 0.5f);
                const int sz = (int)((float)z / (vd - 1) * (p.hmD - 1) + 0.5f);
                next[(size_t)z * vw + x] = p.heights[(size_t)sz * p.hmW + sx];
            }
    }
    p.heights = std::move(next);
    p.hmW = vw;
    p.hmD = vd;
}

float heightAtWorld(const Project& p, float x, float z) {
    if (p.hmW < 2 || p.hmD < 2) return 0.0f;
    const float w = (float)p.terrain.width, d = (float)p.terrain.depth;
    float gx = (x + w * 0.5f) / w * (p.hmW - 1);
    float gz = (z + d * 0.5f) / d * (p.hmD - 1);
    if (gx < 0) gx = 0;
    if (gz < 0) gz = 0;
    if (gx > p.hmW - 1.001f) gx = p.hmW - 1.001f;
    if (gz > p.hmD - 1.001f) gz = p.hmD - 1.001f;
    const int ix = (int)gx, iz = (int)gz;
    const float fx = gx - ix, fz = gz - iz;
    auto h = [&](int a, int b) { return p.heights[(size_t)b * p.hmW + a]; };
    const float top = h(ix, iz) * (1 - fx) + h(ix + 1, iz) * fx;
    const float bottom = h(ix, iz + 1) * (1 - fx) + h(ix + 1, iz + 1) * fx;
    return top * (1 - fz) + bottom * fz;
}

void sculptHeightmap(Project& p, float worldX, float worldZ, float radius, float delta) {
    if (p.hmW < 2 || p.hmD < 2 || radius <= 0.0f) return;
    const float w = (float)p.terrain.width, d = (float)p.terrain.depth;
    const float stepX = w / (p.hmW - 1), stepZ = d / (p.hmD - 1);

    for (int z = 0; z < p.hmD; ++z) {
        for (int x = 0; x < p.hmW; ++x) {
            const float vx = -w * 0.5f + x * stepX;
            const float vz = -d * 0.5f + z * stepZ;
            const float dx = vx - worldX, dz = vz - worldZ;
            const float dist = std::sqrt(dx * dx + dz * dz);
            if (dist >= radius) continue;
            // smooth cosine falloff: 1 at the center, 0 at the edge
            const float t = dist / radius;
            const float falloff = 0.5f + 0.5f * std::cos(t * 3.14159265f);
            p.heights[(size_t)z * p.hmW + x] += delta * falloff;
        }
    }
}

std::string saveHeights(const Project& p) {
    std::ostringstream out;
    out << p.hmW << " " << p.hmD << "\n";
    for (int z = 0; z < p.hmD; ++z) {
        for (int x = 0; x < p.hmW; ++x) {
            if (x) out << " ";
            out << fmtFloat(p.heights[(size_t)z * p.hmW + x]);
        }
        out << "\n";
    }
    return writeFile(fs::path(p.dir) / "terrain.heights", out.str());
}

void loadHeights(Project& p) {
    std::ifstream f(fs::path(p.dir) / "terrain.heights");
    if (!f) return;
    int vw = 0, vd = 0;
    f >> vw >> vd;
    if (vw < 2 || vd < 2 || vw > 1025 || vd > 1025) return;
    std::vector<float> data((size_t)vw * vd, 0.0f);
    for (auto& h : data)
        if (!(f >> h)) return;
    p.heights = std::move(data);
    p.hmW = vw;
    p.hmD = vd;
    ensureHeightmap(p);  // resample if the grid config changed meanwhile
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
        if (const auto* v = jo.find("texture")) o.texturePath = v->stringOr("");
        if (const auto* pl = jo.find("player")) {
            if (const auto* v = pl->find("mode"))
                o.playerMode = v->stringOr("walk") == "noclip" ? 1 : 0;
            if (const auto* v = pl->find("walkSpeed"))
                o.playerWalkSpeed = (float)v->numberOr(0.4);
            if (const auto* v = pl->find("lookSpeed"))
                o.playerLookSpeed = (float)v->numberOr(1.0);
            if (const auto* v = pl->find("eyeHeight"))
                o.playerEyeHeight = (float)v->numberOr(1.8);
            if (const auto* v = pl->find("jumpSpeed"))
                o.playerJumpSpeed = (float)v->numberOr(4.5);
            if (const auto* v = pl->find("canJump"))
                o.playerCanJump = !(v->type == json::Value::Type::Bool && !v->boolean);
        }
        if (const auto* fg = jo.find("flowGraph")) readFlowGraph(*fg, o.flowGraph);
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
        readVec3(s->find("lightColor"), st.lightColor);
        if (const auto* v = s->find("brightness")) st.brightness = (float)v->numberOr(1.0);
        if (st.brightness < 0.0f) st.brightness = 0.0f;
        if (st.brightness > 2.0f) st.brightness = 2.0f;
        if (const auto* v = s->find("terrainTexture")) st.terrainTexture = v->stringOr("");
        if (const auto* v = s->find("terrainTexScale"))
            st.terrainTexScale = (float)v->numberOr(4.0);
        if (st.terrainTexScale < 0.25f) st.terrainTexScale = 0.25f;
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

    if (const auto* music = root.find("music");
        music && music->type == json::Value::Type::Array) {
        for (const auto& m : music->arr) {
            const std::string path = m.stringOr("");
            if (!path.empty()) out.music.push_back(path);
        }
    }

    if (const auto* sounds = root.find("sounds");
        sounds && sounds->type == json::Value::Type::Array) {
        for (const auto& s : sounds->arr) {
            const std::string path = s.stringOr("");
            if (!path.empty()) out.sounds.push_back(path);
        }
    }

    loadHeights(out);
    ensureHeightmap(out);

    // Legacy project-level flow graph (pre per-object graphs): adopt it into
    // the first object so old projects keep working. It is written back in
    // the new per-object format on the next save.
    if (const auto* fg = root.find("flowGraph"); fg && !out.objects.empty()) {
        FlowGraph legacy;
        readFlowGraph(*fg, legacy);
        if (!legacy.empty() && out.objects[0].flowGraph.empty())
            out.objects[0].flowGraph = std::move(legacy);
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
            f.relativePath == "inc\\hud_data.gen.hpp" ||
            f.relativePath == "inc\\terrain_heights.gen.hpp" ||
            f.relativePath == "inc\\texture_data.gen.hpp") {
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
