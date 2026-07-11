#include "project.hpp"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "history.hpp"
#include "json.hpp"
#include "menubake.hpp"
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
        case PrimitiveType::Emitter: return "emitter";
        case PrimitiveType::SoundEmitter: return "sound";
        case PrimitiveType::PointLight: return "point-light";
        case PrimitiveType::SavePoint: return "save-point";
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
    if (s == "emitter") return PrimitiveType::Emitter;
    if (s == "sound") return PrimitiveType::SoundEmitter;
    if (s == "point-light") return PrimitiveType::PointLight;
    if (s == "save-point") return PrimitiveType::SavePoint;
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

// The single project file (game data + editor state + layout) and the
// sidecar undo-history file, both next to each other in the project dir.
static fs::path projectPath(const Project& p) {
    return fs::path(p.dir) / (p.name + ".tyra");
}
static fs::path historyPath(const Project& p) {
    return fs::path(p.dir) / (p.name + ".history");
}

static std::string fmtFloat(float v) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.6g", (double)v);
    return buf;
}

static std::string fmtVec3(const float* v) {
    return "[" + fmtFloat(v[0]) + ", " + fmtFloat(v[1]) + ", " + fmtFloat(v[2]) + "]";
}

// JSON-escape an arbitrary string. Needed for the ImGui window layout, which
// carries newlines and brackets (our JSON parser decodes the same escapes).
static std::string jsonEscape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 16);
    for (char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            default:
                if ((unsigned char)c < 0x20)
                    out += ' ';  // other control chars never appear in our data
                else
                    out += c;
        }
    }
    return out;
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
                fmtFloat(n.pos[1]) + "], \"str\": \"" + jsonEscape(n.str) + "\"" +
                (n.str2.empty() ? "" : ", \"str2\": \"" + jsonEscape(n.str2) + "\"") +
                ", \"num\": [" + fmtFloat(n.num[0]) + ", " + fmtFloat(n.num[1]) +
                ", " + fmtFloat(n.num[2]) + ", " + fmtFloat(n.num[3]) + "] }";
    }
    json += "], \"links\": [";
    for (size_t i = 0; i < fg.links.size(); ++i) {
        const FlowLink& l = fg.links[i];
        json += std::string(i ? ", " : "") + "{ \"id\": " + std::to_string(l.id) +
                ", \"from\": " + std::to_string(l.fromNode) +
                ", \"to\": " + std::to_string(l.toNode) +
                (l.kind == FlowLinkObject ? ", \"data\": true" : "") +
                (l.kind == FlowLinkPos ? ", \"pos\": true" : "") +
                (l.kind == FlowLinkBool ? ", \"bool\": true" : "") +
                (l.kind == FlowLinkText ? ", \"text\": true" : "") + " }";
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
            if (const auto* v = jn.find("str2")) n.str2 = v->stringOr("");
            if (const auto* v = jn.find("num");
                v && v->type == json::Value::Type::Array)
                for (size_t i = 0; i < 4 && i < v->arr.size(); ++i)
                    n.num[i] = (float)v->arr[i].numberOr(0);
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
            if (const auto* v = jl.find("bool");
                v && v->type == json::Value::Type::Bool && v->boolean)
                l.kind = FlowLinkBool;
            if (const auto* v = jl.find("text");
                v && v->type == json::Value::Type::Bool && v->boolean)
                l.kind = FlowLinkText;
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
        (o.usable ? ", \"usable\": true" : "") +
        (o.saveState ? ", \"saveState\": true" : "") +
        // collision: box is the default and stays implicit
        (o.collisionMode == 1 ? ", \"collision\": \"mesh\""
                              : o.collisionMode == 2 ? ", \"collision\": \"none\"" : "") +
        (o.modelPath.empty() ? "" : ", \"model\": \"" + o.modelPath + "\"") +
        (o.materialPath.empty() ? "" : ", \"material\": \"" + o.materialPath + "\"");
    if (o.type == PrimitiveType::Player) {
        json += ", \"player\": { \"mode\": \"" +
                std::string(o.playerMode == 1 ? "noclip" : "walk") +
                "\", \"walkSpeed\": " + fmtFloat(o.playerWalkSpeed) +
                ", \"lookSpeed\": " + fmtFloat(o.playerLookSpeed) +
                ", \"eyeHeight\": " + fmtFloat(o.playerEyeHeight) +
                ", \"jumpSpeed\": " + fmtFloat(o.playerJumpSpeed) +
                ", \"canJump\": " + (o.playerCanJump ? "true" : "false") + " }";
    }
    if (o.type == PrimitiveType::Emitter) {
        static const char* kinds[] = {"fire", "smoke", "fog", "sparks", "rain",
                                      "custom"};
        const int k = (o.emitterKind >= 0 && o.emitterKind < 6) ? o.emitterKind : 0;
        json += ", \"emitter\": { \"kind\": \"" + std::string(kinds[k]) +
                "\", \"count\": " + std::to_string(o.emitterCount) +
                ", \"size\": " + fmtFloat(o.emitterSize) +
                ", \"enabled\": " + (o.emitterEnabled ? "true" : "false") +
                ", \"followPlayer\": " + (o.emitterFollowPlayer ? "true" : "false");
        if (k == 5) {  // custom physics block only where it means something
            json += ", \"speed\": " + fmtFloat(o.emitterSpeed) +
                    ", \"spread\": " + fmtFloat(o.emitterSpread) +
                    ", \"gravity\": " + fmtFloat(o.emitterGravity) +
                    ", \"weight\": " + fmtFloat(o.emitterWeight) +
                    ", \"life\": " + fmtFloat(o.emitterLife) +
                    ", \"grow\": " + fmtFloat(o.emitterGrow) +
                    ", \"opacity\": " + fmtFloat(o.emitterOpacity) +
                    ", \"dieOnGround\": " + (o.emitterDieOnGround ? "true" : "false");
        }
        json += " }";
    }
    if (o.type == PrimitiveType::SoundEmitter) {
        json += ", \"sound\": { \"path\": \"" + o.soundPath +
                "\", \"autoplay\": " + (o.soundAuto ? "true" : "false") +
                ", \"range\": " + fmtFloat(o.soundRange) +
                ", \"interval\": " + fmtFloat(o.soundInterval) +
                ", \"onPlayer\": " + (o.soundOnPlayer ? "true" : "false") + " }";
    }
    if (o.type == PrimitiveType::PointLight) {
        json += ", \"light\": { \"brightness\": " + fmtFloat(o.lightBright) +
                ", \"radius\": " + fmtFloat(o.lightRadius) + " }";
    }
    if (o.type == PrimitiveType::Model && isAnimatedModelPath(o.modelPath)) {
        json += ", \"anim\": { \"clip\": \"" + o.animClip +
                "\", \"autoplay\": " + (o.animAutoplay ? "true" : "false") +
                ", \"loop\": " + (o.animLoop ? "true" : "false") +
                ", \"speed\": " + fmtFloat(o.animSpeed) + " }";
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

// A scene's "settings" + "overrides" blocks (shared by the project file and
// the history file). `settings` carries the scene's own scene-visual values;
// `overrides` says which categories are active. Emitted without surrounding
// braces or leading comma - the caller places them inside the scene object.
static void writeSceneVisuals(std::ostream& j, const SceneData& sc) {
    const ProjectSettings& s = sc.settings;
    j << "\"settings\": { \"lighting\": { \"dir\": " << fmtVec3(s.lightDir)
      << ", \"ambient\": " << fmtFloat(s.ambient) << ", \"diffuse\": " << fmtFloat(s.diffuse)
      << ", \"color\": " << fmtVec3(s.lightColor) << ", \"brightness\": "
      << fmtFloat(s.brightness) << " }, \"sky\": { \"color\": " << fmtVec3(s.skyColor)
      << ", \"topColor\": " << fmtVec3(s.skyTopColor) << ", \"dome\": "
      << (s.skyDome ? "true" : "false") << " }, \"clipping\": \"" << s.clipping
      << "\", \"terrainTexture\": \"" << s.terrainTexture << "\", \"terrainTexScale\": "
      << fmtFloat(s.terrainTexScale) << ", \"postfx\": { \"bloom\": " << fmtFloat(s.bloom)
      << ", \"grain\": " << fmtFloat(s.grain) << " }, \"highlight\": { \"usable\": "
      << (s.highlightUsable ? "true" : "false") << ", \"distance\": "
      << fmtFloat(s.highlightDistance) << ", \"color\": " << fmtVec3(s.highlightColor)
      << ", \"width\": " << fmtFloat(s.highlightWidth) << ", \"steps\": " << s.highlightSteps
      << " } }";
    const SceneOverrides& o = sc.overrides;
    j << ", \"overrides\": { \"lighting\": " << (o.lighting ? "true" : "false")
      << ", \"sky\": " << (o.sky ? "true" : "false") << ", \"clipping\": "
      << (o.clipping ? "true" : "false") << ", \"terrainTex\": "
      << (o.terrainTex ? "true" : "false") << ", \"postFx\": " << (o.postFx ? "true" : "false")
      << ", \"highlight\": " << (o.highlight ? "true" : "false") << " }";
}

// Reads a scene's scene-visual settings + override flags. New files carry an
// "overrides" object; older files carried a top-level "lighting"/"terrainTexture"
// per scene (always-active) - migrate those with the two flags on.
static void readSceneVisuals(const json::Value& js, SceneData& sc) {
    ProjectSettings& s = sc.settings;
    auto clamp01 = [](float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); };
    if (const auto* ov = js.find("overrides")) {
        if (const auto* st = js.find("settings")) {
            if (const auto* li = st->find("lighting")) {
                readVec3(li->find("dir"), s.lightDir);
                if (const auto* v = li->find("ambient")) s.ambient = (float)v->numberOr(0.55);
                if (const auto* v = li->find("diffuse")) s.diffuse = (float)v->numberOr(0.45);
                readVec3(li->find("color"), s.lightColor);
                if (const auto* v = li->find("brightness"))
                    s.brightness = (float)v->numberOr(1.0);
            }
            if (const auto* sk = st->find("sky")) {
                readVec3(sk->find("color"), s.skyColor);
                readVec3(sk->find("topColor"), s.skyTopColor);
                if (const auto* v = sk->find("dome")) s.skyDome = v->boolOr(true);
            }
            if (const auto* v = st->find("clipping"))
                s.clipping = v->stringOr("precise") == "fast" ? "fast" : "precise";
            if (const auto* v = st->find("terrainTexture")) s.terrainTexture = v->stringOr("");
            if (const auto* v = st->find("terrainTexScale"))
                s.terrainTexScale = (float)v->numberOr(4.0);
            if (const auto* pf = st->find("postfx")) {
                if (const auto* v = pf->find("bloom")) s.bloom = clamp01((float)v->numberOr(0.0));
                if (const auto* v = pf->find("grain")) s.grain = clamp01((float)v->numberOr(0.0));
            }
            if (const auto* hl = st->find("highlight")) {
                if (const auto* v = hl->find("usable")) s.highlightUsable = v->boolOr(false);
                if (const auto* v = hl->find("distance"))
                    s.highlightDistance = (float)v->numberOr(6.0);
                readVec3(hl->find("color"), s.highlightColor);
                if (const auto* v = hl->find("width"))
                    s.highlightWidth = (float)v->numberOr(0.35);
                if (const auto* v = hl->find("steps")) s.highlightSteps = (int)v->numberOr(4);
            }
        }
        sc.overrides.lighting = ov->find("lighting") ? ov->find("lighting")->boolOr(false) : false;
        sc.overrides.sky = ov->find("sky") ? ov->find("sky")->boolOr(false) : false;
        sc.overrides.clipping = ov->find("clipping") ? ov->find("clipping")->boolOr(false) : false;
        sc.overrides.terrainTex =
            ov->find("terrainTex") ? ov->find("terrainTex")->boolOr(false) : false;
        sc.overrides.postFx = ov->find("postFx") ? ov->find("postFx")->boolOr(false) : false;
        sc.overrides.highlight =
            ov->find("highlight") ? ov->find("highlight")->boolOr(false) : false;
    } else {
        // Legacy per-scene lighting + terrain texture were always active.
        if (const auto* li = js.find("lighting")) {
            readVec3(li->find("dir"), s.lightDir);
            if (const auto* v = li->find("ambient")) s.ambient = (float)v->numberOr(0.55);
            if (const auto* v = li->find("diffuse")) s.diffuse = (float)v->numberOr(0.45);
            readVec3(li->find("color"), s.lightColor);
            if (const auto* v = li->find("brightness")) s.brightness = (float)v->numberOr(1.0);
            sc.overrides.lighting = true;
        }
        if (const auto* v = js.find("terrainTexture")) {
            s.terrainTexture = v->stringOr("");
            sc.overrides.terrainTex = true;
        }
        if (const auto* v = js.find("terrainTexScale"))
            s.terrainTexScale = (float)v->numberOr(4.0);
    }
    if (s.terrainTexScale < 0.25f) s.terrainTexScale = 0.25f;
    if (s.brightness < 0.0f) s.brightness = 0.0f;
    if (s.brightness > 2.0f) s.brightness = 2.0f;
    if (s.highlightSteps < 1) s.highlightSteps = 1;
    if (s.highlightSteps > 8) s.highlightSteps = 8;
}

ProjectSettings resolvedSettings(const Project& p, const SceneData& s) {
    ProjectSettings r = p.settings;
    const ProjectSettings& o = s.settings;
    if (s.overrides.lighting) {
        for (int i = 0; i < 3; ++i) r.lightDir[i] = o.lightDir[i], r.lightColor[i] = o.lightColor[i];
        r.ambient = o.ambient, r.diffuse = o.diffuse, r.brightness = o.brightness;
    }
    if (s.overrides.sky) {
        for (int i = 0; i < 3; ++i) r.skyColor[i] = o.skyColor[i], r.skyTopColor[i] = o.skyTopColor[i];
        r.skyDome = o.skyDome;
    }
    if (s.overrides.clipping) r.clipping = o.clipping;
    if (s.overrides.terrainTex) {
        r.terrainTexture = o.terrainTexture;
        r.terrainTexScale = o.terrainTexScale;
    }
    if (s.overrides.postFx) {
        r.bloom = o.bloom;
        r.grain = o.grain;
    }
    if (s.overrides.highlight) {
        r.highlightUsable = o.highlightUsable;
        r.highlightDistance = o.highlightDistance;
        for (int i = 0; i < 3; ++i) r.highlightColor[i] = o.highlightColor[i];
        r.highlightWidth = o.highlightWidth;
        r.highlightSteps = o.highlightSteps;
    }
    return r;
}

std::string save(const Project& p) {
    std::ostringstream json;
    json << "{\n"
         << "  \"name\": \"" << p.name << "\",\n"
         << "  \"template\": \"" << p.gameTemplate << "\",\n"
         << "  \"settings\": {\n"
         << "    \"videoSystem\": \"" << p.settings.videoSystem << "\",\n"
         << "    \"buildProfile\": \"" << p.settings.buildProfile << "\",\n"
         << "    \"textureQuant\": \"" << p.settings.textureQuant << "\",\n"
         << "    \"showFps\": " << (p.settings.showFps ? "true" : "false") << ",\n"
         << "    \"showMemory\": " << (p.settings.showMemory ? "true" : "false")
         << ",\n"
         << "    \"clipping\": \"" << p.settings.clipping << "\",\n"
         << "    \"terrainDetail\": " << p.settings.terrainDetail << ",\n"
         << "    \"skyColor\": " << fmtVec3(p.settings.skyColor) << ",\n"
         << "    \"skyTopColor\": " << fmtVec3(p.settings.skyTopColor) << ",\n"
         << "    \"skyDome\": " << (p.settings.skyDome ? "true" : "false") << ",\n"
         << "    \"eyeHeight\": " << fmtFloat(p.settings.eyeHeight) << ",\n"
         << "    \"walkSpeed\": " << fmtFloat(p.settings.walkSpeed) << ",\n"
         << "    \"lookSpeed\": " << fmtFloat(p.settings.lookSpeed) << ",\n"
         << "    \"stickDeadzoneL\": " << fmtFloat(p.settings.stickDeadzoneL) << ",\n"
         << "    \"stickDeadzoneR\": " << fmtFloat(p.settings.stickDeadzoneR) << ",\n"
         << "    \"orbitSpeed\": " << fmtFloat(p.settings.orbitSpeed) << ",\n"
         << "    \"gravity\": " << fmtFloat(p.settings.gravity) << ",\n"
         << "    \"jumpSpeed\": " << fmtFloat(p.settings.jumpSpeed) << ",\n"
         << "    \"lightDir\": " << fmtVec3(p.settings.lightDir) << ",\n"
         << "    \"ambient\": " << fmtFloat(p.settings.ambient) << ",\n"
         << "    \"diffuse\": " << fmtFloat(p.settings.diffuse) << ",\n"
         << "    \"lightColor\": " << fmtVec3(p.settings.lightColor) << ",\n"
         << "    \"brightness\": " << fmtFloat(p.settings.brightness) << ",\n"
         << "    \"terrainTexture\": \"" << p.settings.terrainTexture << "\",\n"
         << "    \"terrainTexScale\": " << fmtFloat(p.settings.terrainTexScale) << ",\n"
         << "    \"bloom\": " << fmtFloat(p.settings.bloom) << ",\n"
         << "    \"grain\": " << fmtFloat(p.settings.grain) << ",\n"
         << "    \"highlightUsable\": "
         << (p.settings.highlightUsable ? "true" : "false") << ",\n"
         << "    \"highlightDistance\": " << fmtFloat(p.settings.highlightDistance)
         << ",\n"
         << "    \"highlightColor\": " << fmtVec3(p.settings.highlightColor) << ",\n"
         << "    \"highlightWidth\": " << fmtFloat(p.settings.highlightWidth) << ",\n"
         << "    \"highlightSteps\": " << p.settings.highlightSteps << ",\n"
         << "    \"loadingScreen\": " << (p.settings.loadingScreen ? "true" : "false")
         << "\n"
         << "  },\n"
         << "  \"scenes\": [";
    for (size_t i = 0; i < p.scenes.size(); ++i) {
        const SceneData& sc = p.scenes[i];
        json << (i ? ",\n    " : "\n    ") << "{ \"name\": \"" << sc.name
             << "\",\n      \"terrain\": { \"width\": " << sc.terrain.width
             << ", \"depth\": " << sc.terrain.depth << " },\n      ";
        writeSceneVisuals(json, sc);
        json << ",\n      \"objects\": ";
        writeObjectsArray(json, sc.objects, "      ");
        json << " }";
    }
    json << "\n  ]";
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
    {
        bool first = true;
        for (const auto& [path, opt] : p.musicBuild) {
            if (opt.rate == 0 && !opt.mono) continue;  // default = as-is
            json << (first ? ",\n  \"musicBuild\": [\n    " : ",\n    ")
                 << "{ \"path\": \"" << jsonEscape(path) << "\", \"rate\": " << opt.rate
                 << ", \"mono\": " << (opt.mono ? "true" : "false") << " }";
            first = false;
        }
        if (!first) json << "\n  ]";
    }
    json << ",\n  \"sounds\": [";
    for (size_t i = 0; i < p.sounds.size(); ++i)
        json << (i ? ", " : "") << "\"" << p.sounds[i] << "\"";
    json << "]";
    if (!p.textureQuality.empty()) {
        json << ",\n  \"textureQuality\": {";
        bool first = true;
        for (const auto& [asset, q] : p.textureQuality) {
            json << (first ? " " : ", ") << "\"" << asset << "\": \"" << q << "\"";
            first = false;
        }
        json << " }";
    }
    json << ",\n  \"saveValues\": [";
    for (size_t i = 0; i < p.saveValues.size(); ++i)
        json << (i ? ",\n    " : "\n    ") << "{ \"name\": \"" << p.saveValues[i].name
             << "\", \"default\": " << fmtFloat(p.saveValues[i].value) << " }";
    json << (p.saveValues.empty() ? "]" : "\n  ]");
    json << ",\n  \"saveTexts\": [";
    for (size_t i = 0; i < p.saveTexts.size(); ++i)
        json << (i ? ",\n    " : "\n    ") << "{ \"name\": \""
             << jsonEscape(p.saveTexts[i].name) << "\", \"default\": \""
             << jsonEscape(p.saveTexts[i].value) << "\" }";
    json << (p.saveTexts.empty() ? "]" : "\n  ]");
    json << ",\n  \"gradings\": [";
    for (size_t i = 0; i < p.gradings.size(); ++i) {
        const ColorGradingPreset& g = p.gradings[i];
        json << (i ? ",\n    " : "\n    ") << "{ \"name\": \""
             << jsonEscape(g.name) << "\", \"brightness\": " << fmtFloat(g.brightness)
             << ", \"contrast\": " << fmtFloat(g.contrast)
             << ", \"saturation\": " << fmtFloat(g.saturation)
             << ", \"temperature\": " << fmtFloat(g.temperature)
             << ", \"tint\": " << fmtVec3(g.tint)
             << ", \"tintAmount\": " << fmtFloat(g.tintAmount)
             << ", \"lift\": " << fmtVec3(g.lift)
             << ", \"gain\": " << fmtVec3(g.gain) << " }";
    }
    json << (p.gradings.empty() ? "]" : "\n  ]");
    json << ",\n  \"defaultGrading\": " << p.defaultGrading;
    json << ",\n  \"menus\": [";
    static const char* kMenuActions[] = {"close",     "scene",     "save-menu",
                                         "menu",      "set-value", "add-value",
                                         "event"};
    for (size_t i = 0; i < p.menus.size(); ++i) {
        const GameMenu& m = p.menus[i];
        json << (i ? ",\n    " : "\n    ") << "{ \"name\": \"" << m.name
             << "\", \"title\": \"" << m.title << "\""
             << (m.titleScreen ? ", \"titleScreen\": true" : "")
             << (m.pauseGame ? "" : ", \"pause\": false")
             << (m.pauseMenu ? ", \"pauseMenu\": true" : "")
             << (m.panelW != 256 ? ", \"panelW\": " + std::to_string(m.panelW) : "")
             << (m.showTitle ? "" : ", \"showTitle\": false")
             << (m.fontPath.empty() ? "" : ", \"font\": \"" + m.fontPath + "\"")
             << (m.titleSize != 18 ? ", \"titleSize\": " + std::to_string(m.titleSize)
                                   : "")
             << (m.entrySize != 15 ? ", \"entrySize\": " + std::to_string(m.entrySize)
                                   : "")
             << ", \"screenPos\": [" << fmtFloat(m.screenPos[0]) << ", "
             << fmtFloat(m.screenPos[1]) << "]"
             << ", \"accent\": " << fmtVec3(m.accent);
        static const char* kImageSlots[] = {"above-title", "above-entries",
                                            "below-entries", "background",
                                            "overlay"};
        if (!m.images.empty()) {
            json << ",\n      \"images\": [";
            for (size_t im = 0; im < m.images.size(); ++im) {
                const MenuImage& img = m.images[im];
                const int s = (img.slot >= 0 && img.slot <= 4) ? img.slot : 0;
                json << (im ? ",\n        " : "\n        ") << "{ \"path\": \""
                     << img.path << "\", \"slot\": \"" << kImageSlots[s] << "\""
                     << (img.scale != 1.0f ? ", \"scale\": " + fmtFloat(img.scale)
                                           : "")
                     << ((img.offset[0] != 0.0f || img.offset[1] != 0.0f)
                             ? ", \"offset\": [" + fmtFloat(img.offset[0]) + ", " +
                                   fmtFloat(img.offset[1]) + "]"
                             : "")
                     << " }";
            }
            json << "\n      ]";
        }
        json << ",\n      \"entries\": [";
        for (size_t e = 0; e < m.entries.size(); ++e) {
            const MenuEntry& en = m.entries[e];
            const int a = (en.action >= 0 && en.action <= 6) ? en.action : 0;
            json << (e ? ",\n        " : "\n        ") << "{ \"label\": \""
                 << en.label << "\", \"action\": \"" << kMenuActions[a] << "\""
                 << (en.param.empty() ? "" : ", \"param\": \"" + en.param + "\"")
                 << (en.amount != 0.0f ? ", \"amount\": " + fmtFloat(en.amount) : "")
                 << " }";
        }
        json << (m.entries.empty() ? "]" : "\n      ]") << " }";
    }
    json << (p.menus.empty() ? "]" : "\n  ]");
    // Editor-side state + window layout: the .tyra file is the whole project.
    json << ",\n  \"editor\": { \"selectedObject\": " << p.selectedObject
         << ", \"gizmo\": " << p.gizmoOp << ", \"viewMode\": " << p.viewMode
         << ", \"emulatorPath\": \"" << jsonEscape(p.emulatorPath) << "\""
         << ", \"ps2LinkIp\": \"" << jsonEscape(p.ps2LinkIp) << "\" }";
    json << ",\n  \"layout\": \"" << jsonEscape(p.windowLayout) << "\"";
    json << "\n}\n";
    return writeFile(projectPath(p), json.str());
}

std::string create(Project& out, const std::string& name, const std::string& parentDir,
                   const TerrainConfig& terrain, const std::string& preset) {
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
    out.scenes[0].terrain = terrain;

    // Two presets: "fpp" (FPP game template with a single player entity) and
    // "empty" (orbit camera, no objects). Anything else is treated as empty.
    const bool fpp = preset == "fpp";
    out.gameTemplate = fpp ? "fpp" : "orbit";

    if (fpp) {
        // The player entity: the camera becomes this player at game start.
        SceneObject player;
        player.name = "player-1";
        player.type = PrimitiveType::Player;
        player.position[0] = 0.0f, player.position[1] = 0.0f, player.position[2] = 0.0f;
        player.color[0] = 0.15f, player.color[1] = 0.9f, player.color[2] = 0.9f;
        out.scenes[0].objects.push_back(player);
    }

    ensureHeightmap(out);

    for (const auto& f : templates::generate(out)) {
        if (auto err = writeFile(root / f.relativePath, f.content); !err.empty()) return err;
    }
    if (auto err = save(out); !err.empty()) return err;

    // Every project is born with its history file (a single-entry undo stack)
    // next to the .tyra project file.
    History h;
    h.reset({out.scenes});
    return saveHistory(out, h);
}

// --- Terrain heightmap -------------------------------------------------------

static void sceneGridDims(const Project& p, const SceneData& s, int& vertsW,
                          int& vertsD) {
    const int maxCells = p.settings.terrainDetail;
    const int cellsX = s.terrain.width > maxCells ? maxCells : s.terrain.width;
    const int cellsZ = s.terrain.depth > maxCells ? maxCells : s.terrain.depth;
    vertsW = cellsX + 1;
    vertsD = cellsZ + 1;
}

void terrainGridDims(const Project& p, int& vertsW, int& vertsD) {
    sceneGridDims(p, p.active(), vertsW, vertsD);
}

static void ensureSceneHeightmap(const Project& p, SceneData& s) {
    int vw = 0, vd = 0;
    sceneGridDims(p, s, vw, vd);
    if (s.hmW == vw && s.hmD == vd && (int)s.heights.size() == vw * vd) return;

    std::vector<float> next((size_t)vw * vd, 0.0f);
    if (s.hmW > 1 && s.hmD > 1 && (int)s.heights.size() == s.hmW * s.hmD) {
        // nearest-neighbor resample from the previous grid
        for (int z = 0; z < vd; ++z)
            for (int x = 0; x < vw; ++x) {
                const int sx = (int)((float)x / (vw - 1) * (s.hmW - 1) + 0.5f);
                const int sz = (int)((float)z / (vd - 1) * (s.hmD - 1) + 0.5f);
                next[(size_t)z * vw + x] = s.heights[(size_t)sz * s.hmW + sx];
            }
    }
    s.heights = std::move(next);
    s.hmW = vw;
    s.hmD = vd;
}

void ensureHeightmap(Project& p) {
    for (SceneData& s : p.scenes) ensureSceneHeightmap(p, s);
}

float heightAtWorld(const Project& p, float x, float z) {
    const SceneData& s = p.active();
    if (s.hmW < 2 || s.hmD < 2) return 0.0f;
    const float w = (float)s.terrain.width, d = (float)s.terrain.depth;
    float gx = (x + w * 0.5f) / w * (s.hmW - 1);
    float gz = (z + d * 0.5f) / d * (s.hmD - 1);
    if (gx < 0) gx = 0;
    if (gz < 0) gz = 0;
    if (gx > s.hmW - 1.001f) gx = s.hmW - 1.001f;
    if (gz > s.hmD - 1.001f) gz = s.hmD - 1.001f;
    const int ix = (int)gx, iz = (int)gz;
    const float fx = gx - ix, fz = gz - iz;
    auto h = [&](int a, int b) { return s.heights[(size_t)b * s.hmW + a]; };
    const float top = h(ix, iz) * (1 - fx) + h(ix + 1, iz) * fx;
    const float bottom = h(ix, iz + 1) * (1 - fx) + h(ix + 1, iz + 1) * fx;
    return top * (1 - fz) + bottom * fz;
}

void sculptHeightmap(Project& p, float worldX, float worldZ, float radius, float delta) {
    SceneData& s = p.active();
    if (s.hmW < 2 || s.hmD < 2 || radius <= 0.0f) return;
    const float w = (float)s.terrain.width, d = (float)s.terrain.depth;
    const float stepX = w / (s.hmW - 1), stepZ = d / (s.hmD - 1);

    for (int z = 0; z < s.hmD; ++z) {
        for (int x = 0; x < s.hmW; ++x) {
            const float vx = -w * 0.5f + x * stepX;
            const float vz = -d * 0.5f + z * stepZ;
            const float dx = vx - worldX, dz = vz - worldZ;
            const float dist = std::sqrt(dx * dx + dz * dz);
            if (dist >= radius) continue;
            // smooth cosine falloff: 1 at the center, 0 at the edge
            const float t = dist / radius;
            const float falloff = 0.5f + 0.5f * std::cos(t * 3.14159265f);
            s.heights[(size_t)z * s.hmW + x] += delta * falloff;
        }
    }
}

void flattenHeightmap(Project& p, float worldX, float worldZ, float radius, float targetH,
                      float strength) {
    SceneData& s = p.active();
    if (s.hmW < 2 || s.hmD < 2 || radius <= 0.0f) return;
    const float w = (float)s.terrain.width, d = (float)s.terrain.depth;
    const float stepX = w / (s.hmW - 1), stepZ = d / (s.hmD - 1);
    if (strength > 1.0f) strength = 1.0f;

    for (int z = 0; z < s.hmD; ++z) {
        for (int x = 0; x < s.hmW; ++x) {
            const float vx = -w * 0.5f + x * stepX;
            const float vz = -d * 0.5f + z * stepZ;
            const float dx = vx - worldX, dz = vz - worldZ;
            const float dist = std::sqrt(dx * dx + dz * dz);
            if (dist >= radius) continue;
            const float t = dist / radius;
            const float falloff = 0.5f + 0.5f * std::cos(t * 3.14159265f);
            float& h = s.heights[(size_t)z * s.hmW + x];
            h += (targetH - h) * strength * falloff;
        }
    }
}

// One heights file per scene: terrain-<scene>.heights; the first scene also
// reads the legacy single-scene terrain.heights.
static fs::path heightsPath(const Project& p, const SceneData& s) {
    return fs::path(p.dir) / ("terrain-" + s.name + ".heights");
}

std::string saveHeights(const Project& p) {
    for (const SceneData& s : p.scenes) {
        std::ostringstream out;
        out << s.hmW << " " << s.hmD << "\n";
        for (int z = 0; z < s.hmD; ++z) {
            for (int x = 0; x < s.hmW; ++x) {
                if (x) out << " ";
                out << fmtFloat(s.heights[(size_t)z * s.hmW + x]);
            }
            out << "\n";
        }
        if (auto err = writeFile(heightsPath(p, s), out.str()); !err.empty()) return err;
    }
    return "";
}

void loadHeights(Project& p) {
    for (size_t i = 0; i < p.scenes.size(); ++i) {
        SceneData& s = p.scenes[i];
        std::ifstream f(heightsPath(p, s));
        if (!f && i == 0) f.open(fs::path(p.dir) / "terrain.heights");  // legacy
        if (!f) continue;
        int vw = 0, vd = 0;
        f >> vw >> vd;
        if (vw < 2 || vd < 2 || vw > 1025 || vd > 1025) continue;
        std::vector<float> data((size_t)vw * vd, 0.0f);
        bool ok = true;
        for (auto& h : data)
            if (!(f >> h)) { ok = false; break; }
        if (!ok) continue;
        s.heights = std::move(data);
        s.hmW = vw;
        s.hmD = vd;
    }
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
        if (const auto* v = jo.find("usable"))
            o.usable = v->type == json::Value::Type::Bool && v->boolean;
        if (const auto* v = jo.find("saveState"))
            o.saveState = v->type == json::Value::Type::Bool && v->boolean;
        if (const auto* v = jo.find("collision")) {
            const std::string mode = v->stringOr("box");
            o.collisionMode = mode == "mesh" ? 1 : mode == "none" ? 2 : 0;
        }
        if (const auto* v = jo.find("model")) o.modelPath = v->stringOr("");
        if (const auto* v = jo.find("material")) o.materialPath = v->stringOr("");
        // pre-materials projects had a per-object "texture" PNG - dropped
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
        if (const auto* em = jo.find("emitter")) {
            if (const auto* v = em->find("kind")) {
                const std::string k = v->stringOr("fire");
                o.emitterKind = k == "smoke" ? 1
                                : k == "fog" ? 2
                                : k == "sparks" ? 3
                                : k == "rain" ? 4
                                : k == "custom" ? 5 : 0;
            }
            if (const auto* v = em->find("count")) o.emitterCount = (int)v->numberOr(24);
            if (o.emitterCount < 1) o.emitterCount = 1;
            if (o.emitterCount > 256) o.emitterCount = 256;
            if (const auto* v = em->find("size")) o.emitterSize = (float)v->numberOr(0.5);
            if (const auto* v = em->find("enabled"))
                o.emitterEnabled = !(v->type == json::Value::Type::Bool && !v->boolean);
            if (const auto* v = em->find("followPlayer"))
                o.emitterFollowPlayer = v->type == json::Value::Type::Bool && v->boolean;
            if (const auto* v = em->find("speed")) o.emitterSpeed = (float)v->numberOr(3);
            if (const auto* v = em->find("spread"))
                o.emitterSpread = (float)v->numberOr(20);
            if (const auto* v = em->find("gravity"))
                o.emitterGravity = (float)v->numberOr(9.8);
            if (const auto* v = em->find("weight"))
                o.emitterWeight = (float)v->numberOr(1);
            if (o.emitterWeight < 0.05f) o.emitterWeight = 0.05f;
            if (const auto* v = em->find("life")) o.emitterLife = (float)v->numberOr(1.5);
            if (o.emitterLife < 0.1f) o.emitterLife = 0.1f;
            if (const auto* v = em->find("grow")) o.emitterGrow = (float)v->numberOr(1);
            if (const auto* v = em->find("opacity"))
                o.emitterOpacity = (float)v->numberOr(0.6);
            if (const auto* v = em->find("dieOnGround"))
                o.emitterDieOnGround = v->type == json::Value::Type::Bool && v->boolean;
        }
        if (const auto* sn = jo.find("sound")) {
            if (const auto* v = sn->find("path")) o.soundPath = v->stringOr("");
            if (const auto* v = sn->find("autoplay"))
                o.soundAuto = !(v->type == json::Value::Type::Bool && !v->boolean);
            if (const auto* v = sn->find("range")) o.soundRange = (float)v->numberOr(15.0);
            if (o.soundRange < 0.5f) o.soundRange = 0.5f;
            if (const auto* v = sn->find("interval"))
                o.soundInterval = (float)v->numberOr(0.0);
            if (o.soundInterval < 0.0f) o.soundInterval = 0.0f;
            if (const auto* v = sn->find("onPlayer"))
                o.soundOnPlayer = v->type == json::Value::Type::Bool && v->boolean;
        }
        if (const auto* lt = jo.find("light")) {
            if (const auto* v = lt->find("brightness"))
                o.lightBright = (float)v->numberOr(1.0);
            if (const auto* v = lt->find("radius"))
                o.lightRadius = (float)v->numberOr(8.0);
            if (o.lightRadius < 0.1f) o.lightRadius = 0.1f;
        }
        if (const auto* an = jo.find("anim")) {
            if (const auto* v = an->find("clip")) o.animClip = v->stringOr("");
            if (const auto* v = an->find("autoplay"))
                o.animAutoplay = !(v->type == json::Value::Type::Bool && !v->boolean);
            if (const auto* v = an->find("loop"))
                o.animLoop = !(v->type == json::Value::Type::Bool && !v->boolean);
            if (const auto* v = an->find("speed")) o.animSpeed = (float)v->numberOr(1.0);
            if (o.animSpeed < 0.05f) o.animSpeed = 0.05f;
            if (o.animSpeed > 10.0f) o.animSpeed = 10.0f;
        }
        if (const auto* fg = jo.find("flowGraph")) readFlowGraph(*fg, o.flowGraph);
        out.push_back(std::move(o));
    }
}

std::string load(Project& out, const std::string& projectDir) {
    // The project is defined by a single <name>.tyra file in the directory.
    fs::path tyraPath;
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(projectDir, ec)) {
        if (entry.is_regular_file(ec) && entry.path().extension() == ".tyra") {
            tyraPath = entry.path();
            break;
        }
    }
    if (tyraPath.empty())
        return "Not a tyra-editor project (no .tyra file): " + projectDir;
    std::ifstream f(tyraPath, std::ios::binary);
    if (!f) return "Cannot open project file: " + tyraPath.string();
    std::stringstream ss;
    ss << f.rdbuf();

    json::Value root;
    if (!json::parse(ss.str(), root) || root.type != json::Value::Type::Object)
        return tyraPath.filename().string() + " is malformed";

    out = Project{};
    out.dir = fs::path(projectDir).string();
    if (const auto* v = root.find("name")) out.name = v->stringOr("");
    if (out.name.empty())
        return tyraPath.filename().string() + " is malformed (no name)";

    if (const auto* v = root.find("template"))
        out.gameTemplate = v->stringOr("orbit") == "fpp" ? "fpp" : "orbit";

    if (const auto* s = root.find("settings")) {
        ProjectSettings& st = out.settings;
        if (const auto* v = s->find("videoSystem")) {
            const std::string sys = v->stringOr("auto");
            st.videoSystem = (sys == "pal" || sys == "ntsc") ? sys : "auto";
        }
        if (const auto* v = s->find("buildProfile"))
            st.buildProfile = v->stringOr("release") == "debug" ? "debug" : "release";
        // pre-quantization projects keep their full-color output
        st.textureQuant = "none";
        if (const auto* v = s->find("textureQuant")) {
            const std::string q = v->stringOr("none");
            st.textureQuant = (q == "8bit" || q == "4bit") ? q : "none";
        }
        if (const auto* v = s->find("showFps")) st.showFps = v->boolOr(false);
        if (const auto* v = s->find("showMemory")) st.showMemory = v->boolOr(false);
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
        // Legacy single-value key seeds both sticks; per-stick keys override.
        if (const auto* v = s->find("stickDeadzone")) {
            st.stickDeadzoneL = (float)v->numberOr(0.2);
            st.stickDeadzoneR = st.stickDeadzoneL;
        }
        if (const auto* v = s->find("stickDeadzoneL"))
            st.stickDeadzoneL = (float)v->numberOr(0.2);
        if (const auto* v = s->find("stickDeadzoneR"))
            st.stickDeadzoneR = (float)v->numberOr(0.2);
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
        auto clamp01 = [](float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); };
        if (const auto* v = s->find("bloom")) st.bloom = clamp01((float)v->numberOr(0.0));
        if (const auto* v = s->find("grain")) st.grain = clamp01((float)v->numberOr(0.0));
        if (const auto* v = s->find("highlightUsable"))
            st.highlightUsable = v->type == json::Value::Type::Bool && v->boolean;
        if (const auto* v = s->find("highlightDistance"))
            st.highlightDistance = (float)v->numberOr(6.0);
        if (st.highlightDistance < 0.5f) st.highlightDistance = 0.5f;
        if (st.highlightDistance > 1000.0f) st.highlightDistance = 1000.0f;
        readVec3(s->find("highlightColor"), st.highlightColor);
        if (const auto* v = s->find("highlightWidth"))
            st.highlightWidth = (float)v->numberOr(0.35);
        if (st.highlightWidth < 0.05f) st.highlightWidth = 0.05f;
        if (st.highlightWidth > 2.0f) st.highlightWidth = 2.0f;
        if (const auto* v = s->find("highlightSteps"))
            st.highlightSteps = (int)v->numberOr(4);
        if (st.highlightSteps < 1) st.highlightSteps = 1;
        if (st.highlightSteps > 8) st.highlightSteps = 8;
        if (const auto* v = s->find("loadingScreen"))
            st.loadingScreen = !(v->type == json::Value::Type::Bool && !v->boolean);
    }

    // Scenes. New format: [{ "name", "objects" }]; legacy: an array of scene
    // name strings plus a project-level "objects" array (single scene).
    if (const auto* scenes = root.find("scenes");
        scenes && scenes->type == json::Value::Type::Array && !scenes->arr.empty()) {
        if (scenes->arr[0].type == json::Value::Type::Object) {
            out.scenes.clear();
            for (const auto& js : scenes->arr) {
                SceneData sc;
                if (const auto* v = js.find("name")) sc.name = v->stringOr("scene");
                if (const auto* objs = js.find("objects"))
                    readObjectsArray(*objs, sc.objects);
                if (const auto* t = js.find("terrain")) {
                    if (const auto* v = t->find("width"))
                        sc.terrain.width = (int)v->numberOr(64);
                    if (const auto* v = t->find("depth"))
                        sc.terrain.depth = (int)v->numberOr(64);
                }
                readSceneVisuals(js, sc);
                out.scenes.push_back(std::move(sc));
            }
        } else {
            out.scenes.clear();
            for (const auto& s : scenes->arr)
                out.scenes.push_back(SceneData{s.stringOr("main"), {}});
        }
    }
    if (out.scenes.empty()) out.scenes.push_back(SceneData{});

    // Legacy project-level terrain size: copy into every scene. Legacy
    // project-level lighting / terrain texture live in out.settings (read
    // above) and reach scenes through inheritance (project::resolvedSettings),
    // so no per-scene copy is needed here.
    if (const auto* terrain = root.find("terrain")) {
        TerrainConfig t;
        if (const auto* v = terrain->find("width")) t.width = (int)v->numberOr(64);
        if (const auto* v = terrain->find("depth")) t.depth = (int)v->numberOr(64);
        for (SceneData& sc : out.scenes) sc.terrain = t;
    }

    if (const auto* objects = root.find("objects");
        objects && objects->type == json::Value::Type::Array) {
        readObjectsArray(*objects, out.scenes[0].objects);  // legacy single scene
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

    if (const auto* mb = root.find("musicBuild");
        mb && mb->type == json::Value::Type::Array) {
        for (const auto& jo : mb->arr) {
            std::string path;
            Project::MusicBuildOpt opt;
            if (const auto* v = jo.find("path")) path = v->stringOr("");
            if (const auto* v = jo.find("rate")) opt.rate = (int)v->numberOr(0);
            if (const auto* v = jo.find("mono"))
                opt.mono = v->type == json::Value::Type::Bool && v->boolean;
            if (!path.empty() && (opt.rate != 0 || opt.mono)) out.musicBuild[path] = opt;
        }
    }

    if (const auto* sounds = root.find("sounds");
        sounds && sounds->type == json::Value::Type::Array) {
        for (const auto& s : sounds->arr) {
            const std::string path = s.stringOr("");
            if (!path.empty()) out.sounds.push_back(path);
        }
    }

    if (const auto* tq = root.find("textureQuality");
        tq && tq->type == json::Value::Type::Object) {
        for (const auto& [asset, v] : tq->obj) {
            const std::string q = v.stringOr("");
            if (q == "none" || q == "8bit" || q == "4bit")
                out.textureQuality[asset] = q;
        }
    }

    if (const auto* values = root.find("saveValues");
        values && values->type == json::Value::Type::Array) {
        for (const auto& jv : values->arr) {
            SaveValue v;
            if (const auto* n = jv.find("name")) v.name = n->stringOr("");
            if (const auto* d = jv.find("default")) v.value = (float)d->numberOr(0.0);
            if (!v.name.empty()) out.saveValues.push_back(std::move(v));
        }
    }

    if (const auto* texts = root.find("saveTexts");
        texts && texts->type == json::Value::Type::Array) {
        for (const auto& jv : texts->arr) {
            SaveTextValue v;
            if (const auto* n = jv.find("name")) v.name = n->stringOr("");
            if (const auto* d = jv.find("default")) v.value = d->stringOr("");
            if (!v.name.empty()) out.saveTexts.push_back(std::move(v));
        }
    }

    if (const auto* gradings = root.find("gradings");
        gradings && gradings->type == json::Value::Type::Array) {
        for (const auto& jg : gradings->arr) {
            ColorGradingPreset g;
            if (const auto* v = jg.find("name")) g.name = v->stringOr("");
            if (const auto* v = jg.find("brightness"))
                g.brightness = (float)v->numberOr(1.0);
            if (const auto* v = jg.find("contrast"))
                g.contrast = (float)v->numberOr(1.0);
            if (const auto* v = jg.find("saturation"))
                g.saturation = (float)v->numberOr(1.0);
            if (const auto* v = jg.find("temperature"))
                g.temperature = (float)v->numberOr(0.0);
            readVec3(jg.find("tint"), g.tint);
            if (const auto* v = jg.find("tintAmount"))
                g.tintAmount = (float)v->numberOr(0.0);
            readVec3(jg.find("lift"), g.lift);
            readVec3(jg.find("gain"), g.gain);
            if (!g.name.empty()) out.gradings.push_back(std::move(g));
        }
    }
    if (const auto* v = root.find("defaultGrading"))
        out.defaultGrading = (int)v->numberOr(-1.0);
    if (out.defaultGrading < -1 ||
        out.defaultGrading >= (int)out.gradings.size())
        out.defaultGrading = -1;

    if (const auto* menus = root.find("menus");
        menus && menus->type == json::Value::Type::Array) {
        for (const auto& jm : menus->arr) {
            GameMenu m;
            if (const auto* v = jm.find("name")) m.name = v->stringOr("menu");
            if (const auto* v = jm.find("title")) m.title = v->stringOr("MENU");
            if (const auto* v = jm.find("titleScreen"))
                m.titleScreen = v->type == json::Value::Type::Bool && v->boolean;
            if (const auto* v = jm.find("pause"))
                m.pauseGame = !(v->type == json::Value::Type::Bool && !v->boolean);
            if (const auto* v = jm.find("pauseMenu"))
                m.pauseMenu = v->type == json::Value::Type::Bool && v->boolean;
            if (const auto* v = jm.find("panelW")) {
                const int w = (int)v->numberOr(256);
                m.panelW = (w == 128 || w == 512) ? w : 256;
            }
            if (const auto* v = jm.find("showTitle"))
                m.showTitle = !(v->type == json::Value::Type::Bool && !v->boolean);
            if (const auto* v = jm.find("font")) m.fontPath = v->stringOr("");
            if (const auto* v = jm.find("titleSize"))
                m.titleSize = (int)v->numberOr(18);
            if (m.titleSize < 10) m.titleSize = 10;
            if (m.titleSize > 48) m.titleSize = 48;
            if (const auto* v = jm.find("entrySize"))
                m.entrySize = (int)v->numberOr(15);
            if (m.entrySize < 8) m.entrySize = 8;
            if (m.entrySize > 32) m.entrySize = 32;
            if (const auto* v = jm.find("screenPos");
                v && v->type == json::Value::Type::Array && v->arr.size() >= 2) {
                m.screenPos[0] = (float)v->arr[0].numberOr(0.5);
                m.screenPos[1] = (float)v->arr[1].numberOr(0.45);
            }
            if (const auto* imgs = jm.find("images");
                imgs && imgs->type == json::Value::Type::Array) {
                for (const auto& ji : imgs->arr) {
                    MenuImage img;
                    if (const auto* v = ji.find("path")) img.path = v->stringOr("");
                    if (const auto* v = ji.find("slot")) {
                        const std::string s = v->stringOr("above-title");
                        img.slot = s == "above-entries" ? MenuImage::AboveEntries
                                   : s == "below-entries" ? MenuImage::BelowEntries
                                   : s == "background"    ? MenuImage::Background
                                   : s == "overlay"       ? MenuImage::Overlay
                                                          : MenuImage::AboveTitle;
                    }
                    if (const auto* v = ji.find("scale"))
                        img.scale = (float)v->numberOr(1.0);
                    if (img.scale < 0.05f) img.scale = 0.05f;
                    if (img.scale > 4.0f) img.scale = 4.0f;
                    if (const auto* v = ji.find("offset");
                        v && v->type == json::Value::Type::Array && v->arr.size() >= 2) {
                        img.offset[0] = (float)v->arr[0].numberOr(0.0);
                        img.offset[1] = (float)v->arr[1].numberOr(0.0);
                    }
                    if (!img.path.empty()) m.images.push_back(std::move(img));
                }
            }
            // Legacy single-image fields (pre image list)
            if (const auto* v = jm.find("image")) {
                MenuImage img;
                img.path = v->stringOr("");
                if (const auto* mode = jm.find("imageMode"))
                    img.slot = mode->stringOr("top") == "background"
                                   ? MenuImage::Background
                                   : MenuImage::AboveTitle;
                if (!img.path.empty()) m.images.push_back(std::move(img));
            }
            readVec3(jm.find("accent"), m.accent);
            if (const auto* entries = jm.find("entries");
                entries && entries->type == json::Value::Type::Array) {
                for (const auto& je : entries->arr) {
                    MenuEntry en;
                    if (const auto* v = je.find("label")) en.label = v->stringOr("Entry");
                    if (const auto* v = je.find("action")) {
                        const std::string a = v->stringOr("close");
                        en.action = a == "scene"       ? MenuEntry::SwitchScene
                                    : a == "save-menu" ? MenuEntry::OpenSaveMenu
                                    : a == "menu"      ? MenuEntry::OpenMenu
                                    : a == "set-value" ? MenuEntry::SetValue
                                    : a == "add-value" ? MenuEntry::AddValue
                                    : a == "event"     ? MenuEntry::FlowEvent
                                                       : MenuEntry::Close;
                    }
                    if (const auto* v = je.find("param")) en.param = v->stringOr("");
                    if (const auto* v = je.find("amount"))
                        en.amount = (float)v->numberOr(0.0);
                    m.entries.push_back(std::move(en));
                }
            }
            if (!m.name.empty()) out.menus.push_back(std::move(m));
        }
    }

    loadHeights(out);
    ensureHeightmap(out);

    // Legacy project-level flow graph (pre per-object graphs): adopt it into
    // the first object so old projects keep working. It is written back in
    // the new per-object format on the next save.
    if (const auto* fg = root.find("flowGraph"); fg && !out.scenes[0].objects.empty()) {
        FlowGraph legacy;
        readFlowGraph(*fg, legacy);
        if (!legacy.empty() && out.scenes[0].objects[0].flowGraph.empty())
            out.scenes[0].objects[0].flowGraph = std::move(legacy);
    }

    // Editor-side state + window layout (the .tyra file holds the whole
    // project). All are clamped/validated where they are applied.
    if (const auto* ed = root.find("editor")) {
        if (const auto* v = ed->find("selectedObject"))
            out.selectedObject = (int)v->numberOr(-1);
        if (const auto* v = ed->find("gizmo")) out.gizmoOp = (int)v->numberOr(0);
        if (const auto* v = ed->find("viewMode")) out.viewMode = (int)v->numberOr(0);
        if (const auto* v = ed->find("emulatorPath")) out.emulatorPath = v->stringOr("");
        if (const auto* v = ed->find("ps2LinkIp")) out.ps2LinkIp = v->stringOr("");
    }
    if (const auto* v = root.find("layout")) out.windowLayout = v->stringOr("");

    return "";
}

// --- history file (<name>.history) ------------------------------------------

std::string saveHistory(const Project& p, const History& h) {
    std::ostringstream json;
    json << "{\n"
         << "  \"version\": 3,\n"
         << "  \"history\": {\n"
         << "    \"index\": " << h.index() << ",\n"
         << "    \"entries\": [";
    const auto& entries = h.entries();
    for (size_t i = 0; i < entries.size(); ++i) {
        const SceneSnapshot& s = entries[i];
        json << (i ? ",\n      " : "\n      ") << "{ \"scenes\": [";
        for (size_t k = 0; k < s.scenes.size(); ++k) {
            const SceneData& sc = s.scenes[k];
            json << (k ? ", " : "") << "{ \"name\": \"" << sc.name
                 << "\", \"terrain\": { \"width\": " << sc.terrain.width
                 << ", \"depth\": " << sc.terrain.depth << " }, ";
            writeSceneVisuals(json, sc);
            json << ", \"objects\": ";
            writeObjectsArray(json, sc.objects, "        ");
            json << " }";
        }
        json << "] }";
    }
    json << (entries.empty() ? "]\n" : "\n    ]\n") << "  }\n}\n";
    return writeFile(historyPath(p), json.str());
}

std::string loadHistory(const Project& p, History& h) {
    std::ifstream f(historyPath(p), std::ios::binary);
    if (!f) return "no history file";
    std::stringstream ss;
    ss << f.rdbuf();

    json::Value root;
    if (!json::parse(ss.str(), root) || root.type != json::Value::Type::Object)
        return "history file is malformed";

    const auto* hist = root.find("history");
    if (!hist) return "history file has no history";
    const auto* entriesVal = hist->find("entries");
    if (!entriesVal || entriesVal->type != json::Value::Type::Array || entriesVal->arr.empty())
        return "history is empty";

    std::vector<SceneSnapshot> entries;
    for (const auto& je : entriesVal->arr) {
        SceneSnapshot s;
        if (const auto* scenes = je.find("scenes");
            scenes && scenes->type == json::Value::Type::Array) {
            for (const auto& js : scenes->arr) {
                SceneData sc;
                if (const auto* v = js.find("name")) sc.name = v->stringOr("scene");
                if (const auto* objs = js.find("objects"))
                    readObjectsArray(*objs, sc.objects);
                if (const auto* t = js.find("terrain")) {
                    if (const auto* v = t->find("width"))
                        sc.terrain.width = (int)v->numberOr(64);
                    if (const auto* v = t->find("depth"))
                        sc.terrain.depth = (int)v->numberOr(64);
                }
                readSceneVisuals(js, sc);
                s.scenes.push_back(std::move(sc));
            }
        }
        entries.push_back(std::move(s));
    }

    // Heightmaps are not persisted in the history (they would balloon it);
    // adopt the current per-scene heights into every entry, so the stale
    // check below passes and in-session undo of sculpting works from here.
    for (SceneSnapshot& e : entries)
        for (SceneData& sc : e.scenes)
            for (const SceneData& cur : p.scenes)
                if (cur.name == sc.name) {
                    sc.heights = cur.heights;
                    sc.hmW = cur.hmW;
                    sc.hmD = cur.hmD;
                    break;
                }

    int index = 0;
    if (const auto* v = hist->find("index")) index = (int)v->numberOr(0);
    if (index < 0 || index >= (int)entries.size()) return "history index is invalid";

    // Stale check: the .tyra project file is the source of truth for the
    // current state. If it was edited outside the editor, the persisted
    // history no longer applies and we start fresh.
    if (!(entries[index] == SceneSnapshot{p.scenes}))
        return "history is stale (project file changed outside the editor)";

    h.restore(std::move(entries), index);
    return "";
}

std::string refreshGenerated(const Project& p) {
    for (const auto& f : templates::generate(p)) {
        const fs::path path = fs::path(p.dir) / f.relativePath;

        bool write = false;
        if (f.relativePath == "Dockerfile" || f.relativePath == "docker-compose.yml" ||
            f.relativePath == "src\\main.cpp" ||
            f.relativePath == "inc\\terrain_config.hpp" ||
            f.relativePath == "inc\\scene_data.hpp" ||
            f.relativePath == ".vscode\\c_cpp_properties.json" ||
            f.relativePath == "src\\scripts\\flow_graph.gen.cpp" ||
            f.relativePath == "inc\\model_data.gen.hpp" ||
            f.relativePath == "inc\\hud_data.gen.hpp" ||
            f.relativePath == "inc\\terrain_heights.gen.hpp" ||
            f.relativePath == "inc\\texture_data.gen.hpp" ||
            f.relativePath == "inc\\save_system.gen.hpp" ||
            f.relativePath == "src\\save_system.gen.cpp" ||
            f.relativePath == "inc\\menu_data.gen.hpp") {
            write = true;  // editor-owned, always in sync with project data
        } else if (f.relativePath == "src\\terrain_game.cpp" ||
                   f.relativePath == "inc\\terrain_game.hpp" ||
                   f.relativePath == "inc\\controls.hpp" ||
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

    // Animated models: re-bake every referenced .glb into its .tanm (+
    // extracted PNG textures) so the game assets always match the sources.
    // Bake problems fail soft - the build proceeds, the game skips the model.
    {
        std::vector<std::string> warnings;
        for (const auto& f : templates::bakeAnimAssets(p, &warnings)) {
            if (auto err = writeFile(fs::path(p.dir) / f.relativePath, f.content);
                !err.empty())
                return err;
        }
        for (const auto& w : warnings)
            printf("[anim bake] %s\n", w.c_str());
    }

    // Built-in HUD assets shipped into every project, written only when
    // missing - replace the file to customize the prompt.
    {
        const fs::path usePng = fs::path(p.dir) / "res" / "hud" / "use.png";
        std::error_code ec;
        if (!fs::exists(usePng, ec)) {
            size_t n = 0;
            const unsigned char* png = templates::usePromptPng(n);
            fs::create_directories(usePng.parent_path(), ec);
            std::ofstream f(usePng, std::ios::binary);
            if (f) f.write(reinterpret_cast<const char*>(png), (std::streamsize)n);
        }
    }
    {
        const fs::path loadPng = fs::path(p.dir) / "res" / "hud" / "loading.png";
        std::error_code ec;
        if (!fs::exists(loadPng, ec)) {
            size_t n = 0;
            const unsigned char* png = templates::loadingPng(n);
            fs::create_directories(loadPng.parent_path(), ec);
            std::ofstream f(loadPng, std::ios::binary);
            if (f) f.write(reinterpret_cast<const char*>(png), (std::streamsize)n);
        }
    }
    // Debug-HUD glyph strip, needed only when a debug-profile overlay is on.
    if (p.settings.buildProfile == "debug" &&
        (p.settings.showFps || p.settings.showMemory)) {
        const fs::path fontPng = fs::path(p.dir) / "res" / "hud" / "debugfont.png";
        std::error_code ec;
        if (!fs::exists(fontPng, ec)) {
            const auto& png = templates::debugFontPng();
            fs::create_directories(fontPng.parent_path(), ec);
            std::ofstream f(fontPng, std::ios::binary);
            if (f)
                f.write(reinterpret_cast<const char*>(png.data()),
                        (std::streamsize)png.size());
        }
    }
    for (const templates::BuiltinAsset& a : templates::saveMenuAssets()) {
        const fs::path png = fs::path(p.dir) / "res" / "hud" / a.fileName;
        std::error_code ec;
        if (fs::exists(png, ec)) continue;
        fs::create_directories(png.parent_path(), ec);
        std::ofstream f(png, std::ios::binary);
        if (f) f.write(reinterpret_cast<const char*>(a.data), (std::streamsize)a.size);
    }

    // Game menu panels: derived from project data (labels, colors), so
    // ALWAYS rebaked - unlike the replaceable save-menu sprites above.
    for (const GameMenu& m : p.menus) {
        std::vector<unsigned char> png;
        if (!menubake::bakePanelPNG(m, p.dir, png))
            return "Menu bake failed (no usable TTF font found in Windows\\Fonts)";
        const fs::path path =
            fs::path(p.dir) / "res" / "menus" / menubake::panelFileName(m.name);
        std::error_code ec;
        fs::create_directories(path.parent_path(), ec);
        std::ofstream f(path, std::ios::binary);
        if (!f) return "Cannot write menu panel: " + path.string();
        f.write(reinterpret_cast<const char*>(png.data()), (std::streamsize)png.size());
    }
    return "";
}

}  // namespace project
