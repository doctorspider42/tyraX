#include "project.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

#include "history.hpp"
#include "json.hpp"
#include "menubake.hpp"
#include "objparser.hpp"
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
        case PrimitiveType::Empty: return "empty";
        case PrimitiveType::Plane: return "plane";
        case PrimitiveType::Decal: return "decal";
        case PrimitiveType::Camera: return "camera";
        case PrimitiveType::Mirror: return "mirror";
        case PrimitiveType::Portal: return "portal";
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
    if (s == "empty") return PrimitiveType::Empty;
    if (s == "plane") return PrimitiveType::Plane;
    if (s == "decal") return PrimitiveType::Decal;
    if (s == "camera") return PrimitiveType::Camera;
    if (s == "mirror") return PrimitiveType::Mirror;
    if (s == "portal") return PrimitiveType::Portal;
    return PrimitiveType::Box;
}

std::vector<int> Project::atlasFontIndices() const {
    std::vector<int> out;
    auto want = [&](const std::string& ref) {
        const GameFont* gf = findFont(ref);  // "" / stale -> the default entry
        if (!gf) return;
        const int idx = (int)(gf - fonts.data());
        for (int e : out)
            if (e == idx) return;
        out.push_back(idx);
    };
    for (const SceneData& sc : scenes)
        for (const SceneObject& o : sc.objects)
            for (const FlowNode& n : o.flowGraph.nodes)
                if (n.type == "DisplayText") want(n.str);
    std::sort(out.begin(), out.end());
    return out;
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

// One file per scene object (merge-friendly layout): the manifest lists only
// ordered ids, each object's body lives in objects/<id>.json. Flat (not per
// scene) because object ids are project-global - so a scene rename never moves
// a file and an object can change scenes without touching its body.
static fs::path objectsDir(const Project& p) { return fs::path(p.dir) / "objects"; }
static fs::path objectPath(const Project& p, const std::string& id) {
    return objectsDir(p) / (id + ".json");
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
                (l.kind == FlowLinkText ? ", \"text\": true" : "") +
                (l.toPin ? ", \"pin\": " + std::to_string(l.toPin) : "") + " }";
    }
    return json + "] }";
}

// Public wrapper (project.hpp) - the whole file already sits in namespace
// project, flowGraphJson above is the private serializer.
std::string flowGraphToJson(const FlowGraph& fg) { return flowGraphJson(fg); }

// Pre-Font-Manager projects stored a raw TTF path on every text and menu
// ("res/fonts/x.ttf", "impact.ttf"). Those fields now name a Project::fonts
// entry, so fold each distinct legacy path into one entry and rewrite the
// references to its name. A value that is already a name is left alone.
static void migrateFontRefs(Project& out) {
    auto looksLikePath = [](const std::string& s) {
        if (s.find('/') != std::string::npos || s.find('\\') != std::string::npos)
            return true;
        if (s.size() < 4) return false;
        std::string ext = s.substr(s.size() - 4);
        for (char& c : ext) c = (char)tolower((unsigned char)c);
        return ext == ".ttf" || ext == ".otf";
    };
    auto entryFor = [&](const std::string& path) -> std::string {
        for (const GameFont& f : out.fonts)
            if (f.fontPath == path) return f.name;
        std::string base = fs::path(path).stem().string();
        if (base.empty()) base = "font";
        std::string name = base;
        for (int n = 2;; ++n) {
            bool taken = false;
            for (const GameFont& f : out.fonts) taken |= (f.name == name);
            if (!taken) break;
            name = base + "-" + std::to_string(n);
        }
        GameFont f;
        f.name = name;
        f.fontPath = path;
        out.fonts.push_back(f);
        return name;
    };
    auto fix = [&](std::string& ref) {
        if (!ref.empty() && looksLikePath(ref)) ref = entryFor(ref);
    };
    for (HudText& t : out.hudTexts) fix(t.font);
    for (LoadingScreenDef& ls : out.loadingScreens)
        for (HudText& t : ls.texts) fix(t.font);
    for (GameMenu& m : out.menus) fix(m.font);
}

static void readFlowGraph(const json::Value& jg, FlowGraph& fg) {
    // Pre-merge graphs name a node per branch (ShowObject / HideObject / ...);
    // those types are gone, so each one becomes its merged type and every exec
    // link landing on it is retargeted to the branch's pin (flowLegacyNodes).
    struct Retarget {
        int nodeId;
        int pin;
    };
    std::vector<Retarget> retargets;

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
            if (const FlowLegacyNode* m = flowLegacyNode(n.type)) {
                n.type = m->to;
                if (m->pin) retargets.push_back({n.id, m->pin});
            }
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
            if (const auto* v = jl.find("pin")) l.toPin = (int)v->numberOr(0);
            if (l.kind == FlowLinkExec && !l.toPin)
                for (const Retarget& r : retargets)
                    if (r.nodeId == l.toNode) l.toPin = r.pin;
            if (l.id > 0) fg.links.push_back(l);
        }
    }
}

std::string objectJson(const SceneObject& o) {
    std::string json =
        "{ \"id\": \"" + jsonEscape(o.id) + "\", \"name\": \"" + jsonEscape(o.name) +
        "\", \"type\": \"" + primitiveTypeName(o.type) +
        "\", \"position\": " + fmtVec3(o.position) +
        ", \"rotation\": " + fmtVec3(o.rotation) + ", \"scale\": " + fmtVec3(o.scale) +
        ", \"color\": " + fmtVec3(o.color) +
        ", \"physics\": " + (o.physics ? "true" : "false") +
        // the physics material block only where it means something
        (o.physics ? ", \"physMass\": " + fmtFloat(o.physMass) +
                         ", \"physBounce\": " + fmtFloat(o.physBounce) +
                         ", \"physFriction\": " + fmtFloat(o.physFriction) +
                         ", \"physTumble\": " + (o.physTumble ? "true" : "false") +
                         ", \"physSleep\": " + fmtFloat(o.physSleep)
                   : "") +
        (o.usable ? ", \"usable\": true" : "") +
        (o.pickable ? ", \"pickable\": true" : "") +
        (o.pickThrow ? ", \"pickThrow\": true" : "") +
        (o.saveState ? ", \"saveState\": true" : "") +
        // collision: box is the default and stays implicit
        (o.collisionMode == 1 ? ", \"collision\": \"mesh\""
                              : o.collisionMode == 2 ? ", \"collision\": \"none\"" : "") +
        (o.layer.empty() ? "" : ", \"layer\": \"" + jsonEscape(o.layer) + "\"") +
        // geometry primitives only; the type's default detail stays implicit
        (((o.type == PrimitiveType::Box || o.type == PrimitiveType::Sphere ||
           o.type == PrimitiveType::Cylinder || o.type == PrimitiveType::Cone ||
           o.type == PrimitiveType::SavePoint) &&
          o.primDetail != defaultPrimDetail(o.type))
             ? ", \"detail\": " + std::to_string(o.primDetail)
             : "") +
        // 0 = unlimited (default) stays implicit
        (o.drawDistance > 0.0f
             ? ", \"drawDistance\": " + fmtFloat(o.drawDistance)
             : "") +
        // rendered into the dynamic env map; default (false) stays implicit
        (o.reflected ? std::string(", \"reflected\": true") : "") +
        (!o.castShadow ? std::string(", \"castShadow\": false") : "") +
        (o.modelPath.empty() ? "" : ", \"model\": \"" + jsonEscape(o.modelPath) + "\"") +
        (o.materialPath.empty() ? ""
                                : ", \"material\": \"" + jsonEscape(o.materialPath) + "\"") +
        // decal projection: off (flat quad) stays implicit
        (o.decalProject ? ", \"decalProject\": true" : "");
    if (o.type == PrimitiveType::Player) {
        const char* modeName = o.playerMode == 1   ? "noclip"
                               : o.playerMode == 2 ? "thirdperson"
                                                   : "walk";
        json += ", \"player\": { \"mode\": \"" + std::string(modeName) +
                "\", \"walkSpeed\": " + fmtFloat(o.playerWalkSpeed) +
                ", \"lookSpeed\": " + fmtFloat(o.playerLookSpeed) +
                ", \"eyeHeight\": " + fmtFloat(o.playerEyeHeight) +
                ", \"jumpSpeed\": " + fmtFloat(o.playerJumpSpeed) +
                ", \"canJump\": " + (o.playerCanJump ? "true" : "false") +
                // Third-person avatar: locomotion clip mapping + camera boom.
                ", \"thirdPerson\": { \"idleClip\": \"" + jsonEscape(o.playerIdleClip) +
                "\", \"walkClip\": \"" + jsonEscape(o.playerWalkClip) +
                "\", \"runClip\": \"" + jsonEscape(o.playerRunClip) +
                "\", \"jumpClip\": \"" + jsonEscape(o.playerJumpClip) +
                "\", \"runThreshold\": " + fmtFloat(o.playerRunThreshold) +
                ", \"camDist\": " + fmtFloat(o.playerCamDist) +
                ", \"camHeight\": " + fmtFloat(o.playerCamHeight) +
                ", \"camShoulder\": " + fmtFloat(o.playerCamShoulder) +
                ", \"turnRate\": " + fmtFloat(o.playerTurnRate) +
                // Camera style: orbit is the classic rig; the fixed styles
                // (topdown/isometric/fixed) pin the pitch (+ yaw unless
                // camRotate) for top-down / isometric games.
                ", \"camStyle\": \"" +
                std::string(o.playerCamStyle == 1   ? "topdown"
                            : o.playerCamStyle == 2 ? "isometric"
                            : o.playerCamStyle == 3 ? "fixed"
                                                    : "orbit") +
                "\", \"camPitch\": " + fmtFloat(o.playerCamPitch) +
                ", \"camYaw\": " + fmtFloat(o.playerCamYaw) +
                ", \"camRotate\": " + (o.playerCamYawRotate ? "true" : "false") +
                " }" +
                ", \"flashlight\": { \"enabled\": " +
                (o.flashlightEnabled ? "true" : "false") + ", \"color\": " +
                fmtVec3(o.flashlightColor) + ", \"range\": " +
                fmtFloat(o.flashlightRange) + ", \"angle\": " +
                fmtFloat(o.flashlightAngle) + ", \"toggle\": \"" +
                jsonEscape(o.flashlightToggleButton) + "\" }" + " }";
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
        json += ", \"sound\": { \"path\": \"" + jsonEscape(o.soundPath) +
                "\", \"autoplay\": " + (o.soundAuto ? "true" : "false") +
                ", \"range\": " + fmtFloat(o.soundRange) +
                ", \"interval\": " + fmtFloat(o.soundInterval) +
                ", \"onPlayer\": " + (o.soundOnPlayer ? "true" : "false") + " }";
    }
    if (o.type == PrimitiveType::PointLight) {
        json += ", \"light\": { \"brightness\": " + fmtFloat(o.lightBright) +
                ", \"radius\": " + fmtFloat(o.lightRadius) + " }";
    }
    if (o.type == PrimitiveType::Camera) {
        json += ", \"camera\": { \"fov\": " + fmtFloat(o.cameraFov) +
                ", \"feed\": " + (o.camFeed ? "true" : "false") +
                ", \"feedTerrain\": " + (o.camFeedTerrain ? "true" : "false") +
                ", \"feedObjects\": [";
        for (size_t i = 0; i < o.camFeedObjects.size(); ++i)
            json += (i ? ", \"" : "\"") + jsonEscape(o.camFeedObjects[i]) + "\"";
        json += "] }";
    }
    if (!o.textureFeed.empty())
        json += ", \"textureFeed\": \"" + jsonEscape(o.textureFeed) + "\"";
    if (o.type == PrimitiveType::Mirror) {
        json += ", \"mirror\": { \"opacity\": " + fmtFloat(o.mirrorOpacity) +
                ", \"reflectPlayer\": " +
                (o.mirrorReflectPlayer ? "true" : "false") +
                ", \"raytraced\": " + (o.mirrorRaytraced ? "true" : "false") +
                ", \"rtSize\": " + std::to_string(o.mirrorRtSize) +
                ", \"objects\": [";
        for (size_t i = 0; i < o.mirrorObjects.size(); ++i)
            json += (i ? ", \"" : "\"") + jsonEscape(o.mirrorObjects[i]) + "\"";
        json += "] }";
    }
    if (o.type == PrimitiveType::Portal) {
        json += ", \"portal\": { \"target\": \"" + o.portalTarget +
                "\", \"showTerrain\": " + (o.portalShowTerrain ? "true" : "false") +
                ", \"teleportObjects\": " +
                (o.portalTeleportObjects ? "true" : "false") +
                ", \"viewAll\": " + (o.portalViewAll ? "true" : "false") +
                ", \"objects\": [";
        for (size_t i = 0; i < o.portalObjects.size(); ++i)
            json += (i ? ", \"" : "\"") + o.portalObjects[i] + "\"";
        json += "] }";
    }
    if (o.type == PrimitiveType::Model && isAnimatedModelPath(o.modelPath)) {
        json += ", \"anim\": { \"clip\": \"" + jsonEscape(o.animClip) +
                "\", \"autoplay\": " + (o.animAutoplay ? "true" : "false") +
                ", \"loop\": " + (o.animLoop ? "true" : "false") +
                ", \"speed\": " + fmtFloat(o.animSpeed) + " }";
    }
    // Per-object LOD overrides (animated models + player avatars); omitted at
    // the -1 default = "use the project preference".
    if (o.animLodOverride >= 0.0f)
        json += ", \"animLod\": " + fmtFloat(o.animLodOverride);
    if (o.meshLodOverride >= 0.0f)
        json += ", \"meshLod\": " + fmtFloat(o.meshLodOverride);
    if (o.modelYawOffset != 0.0f)
        json += ", \"modelYaw\": " + fmtFloat(o.modelYawOffset);
    if (!o.scripts.empty()) {
        json += ", \"scripts\": [";
        for (size_t i = 0; i < o.scripts.size(); ++i)
            json += (i ? ", \"" : "\"") + jsonEscape(o.scripts[i]) + "\"";
        json += "]";
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

// "layers": [ ... ] - a scene's streaming layers (shared by the project file
// and the history file). Omitted entirely when the scene has no layers.
static void writeLayersArray(std::ostream& json, const std::vector<SceneLayer>& layers) {
    json << "[";
    for (size_t i = 0; i < layers.size(); ++i) {
        json << (i ? ", " : "") << "{ \"name\": \"" << layers[i].name << "\""
             << (layers[i].startLoaded ? "" : ", \"startLoaded\": false")
             << (layers[i].editorVisible ? "" : ", \"editorVisible\": false");
        if (layers[i].autoStream)  // off = keys omitted (older files stay valid)
            json << ", \"autoStream\": true, \"streamX\": " << fmtFloat(layers[i].streamX)
                 << ", \"streamZ\": " << fmtFloat(layers[i].streamZ)
                 << ", \"streamRadius\": " << fmtFloat(layers[i].streamRadius);
        json << " }";
    }
    json << "]";
}

static void readLayersArray(const json::Value& arr, std::vector<SceneLayer>& layers) {
    layers.clear();
    if (arr.type != json::Value::Type::Array) return;
    for (const auto& jl : arr.arr) {
        SceneLayer l;
        if (const auto* v = jl.find("name")) l.name = v->stringOr("");
        if (const auto* v = jl.find("startLoaded")) l.startLoaded = v->boolOr(true);
        if (const auto* v = jl.find("editorVisible")) l.editorVisible = v->boolOr(true);
        if (const auto* v = jl.find("autoStream")) l.autoStream = v->boolOr(false);
        if (const auto* v = jl.find("streamX")) l.streamX = (float)v->numberOr(0.0);
        if (const auto* v = jl.find("streamZ")) l.streamZ = (float)v->numberOr(0.0);
        if (const auto* v = jl.find("streamRadius")) {
            l.streamRadius = (float)v->numberOr(60.0);
            if (l.streamRadius < 1.0f) l.streamRadius = 1.0f;
        }
        if (!l.name.empty()) layers.push_back(l);
    }
}

// Paintable terrain layers (docs/terrain-painting.md). The per-texel splat
// weights live in the terrain-<scene>.splat sidecar; only the layer list (a
// name + .mtl material each) travels in the project / history JSON.
static void writeTerrainLayersArray(std::ostream& json,
                                    const std::vector<TerrainLayer>& layers) {
    json << "[";
    for (size_t i = 0; i < layers.size(); ++i) {
        json << (i ? ", " : "") << "{ \"name\": \"" << layers[i].name
             << "\", \"material\": \"" << layers[i].material << "\", \"scale\": "
             << fmtFloat(layers[i].scale);
        if (layers[i].stochastic) json << ", \"stochastic\": true";
        json << " }";
    }
    json << "]";
}

static void readTerrainLayersArray(const json::Value& arr,
                                   std::vector<TerrainLayer>& layers) {
    layers.clear();
    if (arr.type != json::Value::Type::Array) return;
    for (const auto& jl : arr.arr) {
        TerrainLayer l;
        if (const auto* v = jl.find("name")) l.name = v->stringOr("Layer");
        if (const auto* v = jl.find("material")) l.material = v->stringOr("");
        if (const auto* v = jl.find("scale")) l.scale = (float)v->numberOr(1.0);
        if (l.scale <= 0.0f) l.scale = 1.0f;
        if (const auto* v = jl.find("stochastic")) l.stochastic = v->boolOr(false);
        layers.push_back(l);
    }
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
      << (s.skyDome ? "true" : "false") << ", \"zenithSize\": " << fmtFloat(s.zenithSize)
      << " }, \"clipping\": \"" << s.clipping
      << "\", \"terrainMaterial\": \"" << s.terrainMaterial
      << "\", \"postfx\": { \"bloom\": " << fmtFloat(s.bloom)
      << ", \"grain\": " << fmtFloat(s.grain)
      << ", \"dofAmount\": " << fmtFloat(s.dofAmount)
      << ", \"dofFocus\": " << fmtFloat(s.dofFocus)
      << ", \"dofRange\": " << fmtFloat(s.dofRange) << " }, \"fog\": { \"enabled\": "
      << (s.fogEnabled ? "true" : "false") << ", \"color\": " << fmtVec3(s.fogColor)
      << ", \"start\": " << fmtFloat(s.fogStart) << ", \"end\": " << fmtFloat(s.fogEnd)
      << " }, \"highlight\": { \"usable\": "
      << (s.highlightUsable ? "true" : "false") << ", \"distance\": "
      << fmtFloat(s.highlightDistance) << ", \"color\": " << fmtVec3(s.highlightColor)
      << ", \"width\": " << fmtFloat(s.highlightWidth) << ", \"steps\": " << s.highlightSteps
      << ", \"opacity\": " << fmtFloat(s.highlightOpacity)
      << ", \"overlay\": " << (s.highlightOverlay ? "true" : "false")
      << " } }";
    const SceneOverrides& o = sc.overrides;
    j << ", \"overrides\": { \"lighting\": " << (o.lighting ? "true" : "false")
      << ", \"sky\": " << (o.sky ? "true" : "false") << ", \"clipping\": "
      << (o.clipping ? "true" : "false") << ", \"terrainMat\": "
      << (o.terrainMat ? "true" : "false") << ", \"postFx\": " << (o.postFx ? "true" : "false")
      << ", \"fog\": " << (o.fog ? "true" : "false")
      << ", \"highlight\": " << (o.highlight ? "true" : "false") << " }";
    if (!sc.ambiencePreset.empty())
        j << ", \"ambiencePreset\": \"" << jsonEscape(sc.ambiencePreset) << "\"";
    if (!sc.loadingScreen.empty())
        j << ", \"loadingScreen\": \"" << jsonEscape(sc.loadingScreen) << "\"";
}

// Reads a scene's scene-visual settings + override flags. New files carry an
// "overrides" object; older files carried a top-level "lighting"/"terrainTexScale"
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
                if (const auto* v = sk->find("zenithSize"))
                    s.zenithSize = (float)v->numberOr(0.5);
            }
            if (const auto* v = st->find("clipping")) {
                // "vu1" (default) = precise per-package classification +
                // clipping on VU1; "precise" = the legacy EE clipper.
                const std::string c = v->stringOr("vu1");
                s.clipping = (c == "fast" || c == "precise") ? c : "vu1";
            } else {
                // pre-clipping-key projects were authored against the EE
                // clipper - keep their behavior
                s.clipping = "precise";
            }
            if (const auto* v = st->find("terrainMaterial")) s.terrainMaterial = v->stringOr("");
            if (const auto* pf = st->find("postfx")) {
                if (const auto* v = pf->find("bloom")) s.bloom = clamp01((float)v->numberOr(0.0));
                if (const auto* v = pf->find("grain")) s.grain = clamp01((float)v->numberOr(0.0));
                if (const auto* v = pf->find("dofAmount"))
                    s.dofAmount = clamp01((float)v->numberOr(0.0));
                if (const auto* v = pf->find("dofFocus"))
                    s.dofFocus = (float)v->numberOr(s.dofFocus);
                if (const auto* v = pf->find("dofRange"))
                    s.dofRange = (float)v->numberOr(s.dofRange);
            }
            if (const auto* fg = st->find("fog")) {
                if (const auto* v = fg->find("enabled")) s.fogEnabled = v->boolOr(false);
                readVec3(fg->find("color"), s.fogColor);
                if (const auto* v = fg->find("start")) s.fogStart = (float)v->numberOr(15.0);
                if (const auto* v = fg->find("end")) s.fogEnd = (float)v->numberOr(120.0);
            }
            if (const auto* hl = st->find("highlight")) {
                if (const auto* v = hl->find("usable")) s.highlightUsable = v->boolOr(false);
                if (const auto* v = hl->find("distance"))
                    s.highlightDistance = (float)v->numberOr(6.0);
                readVec3(hl->find("color"), s.highlightColor);
                if (const auto* v = hl->find("width"))
                    s.highlightWidth = (float)v->numberOr(0.35);
                if (const auto* v = hl->find("steps")) s.highlightSteps = (int)v->numberOr(4);
                if (const auto* v = hl->find("opacity"))
                    s.highlightOpacity = (float)v->numberOr(0.56);
                if (const auto* v = hl->find("overlay"))
                    s.highlightOverlay = v->boolOr(false);
            }
        }
        sc.overrides.lighting = ov->find("lighting") ? ov->find("lighting")->boolOr(false) : false;
        sc.overrides.sky = ov->find("sky") ? ov->find("sky")->boolOr(false) : false;
        sc.overrides.clipping = ov->find("clipping") ? ov->find("clipping")->boolOr(false) : false;
        // Legacy files carried this flag as "terrainTex" (it gated the terrain
        // texture, now the terrain material). Accept both keys.
        sc.overrides.terrainMat =
            ov->find("terrainMat")   ? ov->find("terrainMat")->boolOr(false)
            : ov->find("terrainTex") ? ov->find("terrainTex")->boolOr(false)
                                     : false;
        sc.overrides.postFx = ov->find("postFx") ? ov->find("postFx")->boolOr(false) : false;
        sc.overrides.fog = ov->find("fog") ? ov->find("fog")->boolOr(false) : false;
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
        // Terrain was picked by raw texture + tiling scale in these legacy
        // files; both are gone now - terrain takes a material whose map "-s"
        // option carries the tiling. Nothing to migrate here.
    }
    if (const auto* v = js.find("ambiencePreset")) sc.ambiencePreset = v->stringOr("");
    if (const auto* v = js.find("loadingScreen")) sc.loadingScreen = v->stringOr("");
    if (s.brightness < 0.0f) s.brightness = 0.0f;
    if (s.brightness > 2.0f) s.brightness = 2.0f;
    if (s.highlightSteps < 1) s.highlightSteps = 1;
    if (s.highlightSteps > 8) s.highlightSteps = 8;
    if (s.highlightOpacity < 0.0f) s.highlightOpacity = 0.0f;
    if (s.highlightOpacity > 1.0f) s.highlightOpacity = 1.0f;
    if (s.fogStart < 0.0f) s.fogStart = 0.0f;
    if (s.fogEnd <= s.fogStart + 1.0f) s.fogEnd = s.fogStart + 1.0f;
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
        r.zenithSize = o.zenithSize;
    }
    if (s.overrides.clipping) r.clipping = o.clipping;
    if (s.overrides.terrainMat) r.terrainMaterial = o.terrainMaterial;
    if (s.overrides.postFx) {
        r.bloom = o.bloom;
        r.grain = o.grain;
        r.dofAmount = o.dofAmount;
        r.dofFocus = o.dofFocus;
        r.dofRange = o.dofRange;
    }
    if (s.overrides.fog) {
        r.fogEnabled = o.fogEnabled;
        for (int i = 0; i < 3; ++i) r.fogColor[i] = o.fogColor[i];
        r.fogStart = o.fogStart;
        r.fogEnd = o.fogEnd;
    }
    if (s.overrides.highlight) {
        r.highlightUsable = o.highlightUsable;
        r.highlightDistance = o.highlightDistance;
        for (int i = 0; i < 3; ++i) r.highlightColor[i] = o.highlightColor[i];
        r.highlightWidth = o.highlightWidth;
        r.highlightSteps = o.highlightSteps;
        r.highlightOpacity = o.highlightOpacity;
        r.highlightOverlay = o.highlightOverlay;
    }
    // Ambience preset overlay: a resolved preset owns sky + lighting + fog and
    // wins over the raw project/scene values above (those remain the fallback
    // when no presets exist). Keeps all downstream codegen/viewport reading the
    // same ProjectSettings fields.
    const int ai = ambienceIndexFor(p, s);
    if (ai >= 0) {
        const AmbiencePreset& a = p.ambiencePresets[ai];
        for (int i = 0; i < 3; ++i) {
            r.skyColor[i] = a.skyColor[i];
            r.skyTopColor[i] = a.skyTopColor[i];
            r.lightDir[i] = a.lightDir[i];
            r.lightColor[i] = a.lightColor[i];
            r.fogColor[i] = a.fogColor[i];
        }
        r.skyDome = a.skyDome;
        r.zenithSize = a.zenithSize;
        r.ambient = a.ambient;
        r.diffuse = a.diffuse;
        r.brightness = a.brightness;
        r.aoEnabled = a.aoEnabled;
        r.aoStrength = a.aoStrength;
        r.aoRadius = a.aoRadius;
        r.fogEnabled = a.fogEnabled;
        r.fogStart = a.fogStart;
        r.fogEnd = a.fogEnd;
    }
    return r;
}

int ambienceIndexFor(const Project& p, const SceneData& s) {
    if (!s.ambiencePreset.empty()) {
        for (int i = 0; i < (int)p.ambiencePresets.size(); ++i)
            if (p.ambiencePresets[i].name == s.ambiencePreset) return i;
    }
    if (p.defaultAmbience >= 0 && p.defaultAmbience < (int)p.ambiencePresets.size())
        return p.defaultAmbience;
    return -1;
}

int loadingScreenIndexFor(const Project& p, const SceneData& s) {
    if (!s.loadingScreen.empty()) {
        for (int i = 0; i < (int)p.loadingScreens.size(); ++i)
            if (p.loadingScreens[i].name == s.loadingScreen) return i;
    }
    if (p.defaultLoadingScreen >= 0 &&
        p.defaultLoadingScreen < (int)p.loadingScreens.size())
        return p.defaultLoadingScreen;
    return -1;
}

TerrainMaterial resolveTerrainMaterial(const Project& p, const std::string& matRel) {
    TerrainMaterial out;
    if (matRel.empty()) return out;
    std::vector<objparser::MtlMaterial> mats;
    const std::string full = (fs::path(p.dir) / matRel).string();
    if (!objparser::loadMtl(full, mats) || mats.empty()) return out;
    const objparser::MtlMaterial& m = mats.front();
    out.present = true;
    for (int i = 0; i < 3; ++i) out.kd[i] = m.kd[i];
    out.tile[0] = m.scale[0];
    out.tile[1] = m.scale[1];
    // map_Kd is relative to the .mtl's own directory.
    if (!m.texture.empty())
        out.texture = (fs::path(matRel).parent_path() / m.texture).generic_string();
    return out;
}

// --- Manifest section writers -------------------------------------------------
// Each writes its group of top-level .tyra keys WITHOUT the leading ",\n  "
// separator (the composers below add it), preserving the exact historical byte
// layout of the manifest. sectionJson() wraps the same bytes in braces to form
// a standalone JSON object - the collaboration wire format for project-wide
// data. New manifest keys must join one of these writers (or become a new
// Section) so they reach both the file and the wire.

static void writeSettingsSection(std::ostream& json, const Project& p) {
    json << "\"settings\": {\n"
         << "    \"videoSystem\": \"" << p.settings.videoSystem << "\",\n"
         << "    \"displayMode\": \"" << p.settings.displayMode << "\",\n"
         << (p.settings.palFullHeight ? "    \"palFullHeight\": true,\n" : "")
         << "    \"widescreen\": " << (p.settings.widescreen ? "true" : "false")
         << ",\n"
         << "    \"buildProfile\": \"" << p.settings.buildProfile << "\",\n"
         << "    \"textureQuant\": \"" << p.settings.textureQuant << "\",\n"
         << "    \"textureAtlas\": " << (p.settings.textureAtlas ? "true" : "false")
         << ",\n"
         << "    \"showFps\": " << (p.settings.showFps ? "true" : "false") << ",\n"
         << "    \"showMemory\": " << (p.settings.showMemory ? "true" : "false")
         << ",\n"
         << "    \"showProfiler\": "
         << (p.settings.showProfiler ? "true" : "false") << ",\n"
         << "    \"liveLink\": " << (p.settings.liveLink ? "true" : "false")
         << ",\n"
         << "    \"disableVsync\": "
         << (p.settings.disableVsync ? "true" : "false") << ",\n"
         << "    \"clipping\": \"" << p.settings.clipping << "\",\n"
         << "    \"animLodDistance\": " << fmtFloat(p.settings.animLodDistance)
         << ",\n"
         << "    \"meshLodDistance\": " << fmtFloat(p.settings.meshLodDistance)
         << ",\n"
         << "    \"staticBatching\": "
         << (p.settings.staticBatching ? "true" : "false") << ",\n"
         << "    \"envProbeReflected\": "
         << (p.settings.envProbeReflected ? "true" : "false") << ",\n"
         << "    \"navCellSize\": " << fmtFloat(p.settings.navCellSize) << ",\n"
         << "    \"navMaxSlope\": " << fmtFloat(p.settings.navMaxSlope) << ",\n"
         << "    \"navAgentRadius\": " << fmtFloat(p.settings.navAgentRadius)
         << ",\n"
         << "    \"terrainDetail\": " << p.settings.terrainDetail << ",\n"
         << "    \"terrainViewDistance\": " << fmtFloat(p.settings.terrainViewDistance)
         << ",\n"
         << "    \"skyColor\": " << fmtVec3(p.settings.skyColor) << ",\n"
         << "    \"skyTopColor\": " << fmtVec3(p.settings.skyTopColor) << ",\n"
         << "    \"skyDome\": " << (p.settings.skyDome ? "true" : "false") << ",\n"
         << "    \"zenithSize\": " << fmtFloat(p.settings.zenithSize) << ",\n"
         << "    \"eyeHeight\": " << fmtFloat(p.settings.eyeHeight) << ",\n"
         << "    \"walkSpeed\": " << fmtFloat(p.settings.walkSpeed) << ",\n"
         << "    \"lookSpeed\": " << fmtFloat(p.settings.lookSpeed) << ",\n"
         << "    \"stickDeadzoneL\": " << fmtFloat(p.settings.stickDeadzoneL) << ",\n"
         << "    \"stickDeadzoneR\": " << fmtFloat(p.settings.stickDeadzoneR) << ",\n"
         << "    \"stickCurveL\": " << p.settings.stickCurveL << ",\n"
         << "    \"stickCurveR\": " << p.settings.stickCurveR << ",\n"
         << "    \"stickExpL\": " << fmtFloat(p.settings.stickExpL) << ",\n"
         << "    \"stickExpR\": " << fmtFloat(p.settings.stickExpR) << ",\n"
         << "    \"multiplayer\": \"" << p.settings.multiplayer << "\",\n"
         << "    \"p2JoinOnStart\": " << (p.settings.p2JoinOnStart ? "true" : "false") << ",\n"
         << "    \"orbitSpeed\": " << fmtFloat(p.settings.orbitSpeed) << ",\n"
         << "    \"gravity\": " << fmtFloat(p.settings.gravity) << ",\n"
         << "    \"jumpSpeed\": " << fmtFloat(p.settings.jumpSpeed) << ",\n"
         << "    \"lightDir\": " << fmtVec3(p.settings.lightDir) << ",\n"
         << "    \"ambient\": " << fmtFloat(p.settings.ambient) << ",\n"
         << "    \"diffuse\": " << fmtFloat(p.settings.diffuse) << ",\n"
         << "    \"lightColor\": " << fmtVec3(p.settings.lightColor) << ",\n"
         << "    \"brightness\": " << fmtFloat(p.settings.brightness) << ",\n"
         << "    \"aoEnabled\": " << (p.settings.aoEnabled ? "true" : "false")
         << ",\n"
         << "    \"aoStrength\": " << fmtFloat(p.settings.aoStrength) << ",\n"
         << "    \"aoRadius\": " << fmtFloat(p.settings.aoRadius) << ",\n"
         << "    \"terrainMaterial\": \"" << p.settings.terrainMaterial << "\",\n"
         << "    \"bloom\": " << fmtFloat(p.settings.bloom) << ",\n"
         << "    \"grain\": " << fmtFloat(p.settings.grain) << ",\n"
         << "    \"dofAmount\": " << fmtFloat(p.settings.dofAmount) << ",\n"
         << "    \"dofFocus\": " << fmtFloat(p.settings.dofFocus) << ",\n"
         << "    \"dofRange\": " << fmtFloat(p.settings.dofRange) << ",\n"
         << "    \"fogEnabled\": " << (p.settings.fogEnabled ? "true" : "false")
         << ",\n"
         << "    \"fogColor\": " << fmtVec3(p.settings.fogColor) << ",\n"
         << "    \"fogStart\": " << fmtFloat(p.settings.fogStart) << ",\n"
         << "    \"fogEnd\": " << fmtFloat(p.settings.fogEnd) << ",\n"
         << "    \"highlightUsable\": "
         << (p.settings.highlightUsable ? "true" : "false") << ",\n"
         << "    \"highlightDistance\": " << fmtFloat(p.settings.highlightDistance)
         << ",\n"
         << "    \"highlightColor\": " << fmtVec3(p.settings.highlightColor) << ",\n"
         << "    \"highlightWidth\": " << fmtFloat(p.settings.highlightWidth) << ",\n"
         << "    \"highlightSteps\": " << p.settings.highlightSteps << ",\n"
         << "    \"highlightOpacity\": " << fmtFloat(p.settings.highlightOpacity)
         << ",\n"
         << "    \"highlightOverlay\": "
         << (p.settings.highlightOverlay ? "true" : "false") << ",\n"
         << "    \"loadingScreen\": " << (p.settings.loadingScreen ? "true" : "false")
         << "\n"
         << "  }";
}

// The scene table: names + per-scene meta + ordered object-id lists. Not a
// Section - the collaboration layer ships it as its own message (per-object
// bodies live in objects/<id>.json).
static void writeScenesTable(std::ostream& json, const Project& p) {
    json << "\"scenes\": [";
    for (size_t i = 0; i < p.scenes.size(); ++i) {
        const SceneData& sc = p.scenes[i];
        json << (i ? ",\n    " : "\n    ") << "{ \"name\": \"" << sc.name
             << "\",\n      \"terrain\": { \"width\": " << sc.terrain.width
             << ", \"depth\": " << sc.terrain.depth << " },\n      ";
        writeSceneVisuals(json, sc);
        if (!sc.layers.empty()) {
            json << ",\n      \"layers\": ";
            writeLayersArray(json, sc.layers);
        }
        if (!sc.terrainLayers.empty()) {
            json << ",\n      \"terrainLayers\": ";
            writeTerrainLayersArray(json, sc.terrainLayers);
        }
        if (sc.terrainBaseStochastic)
            json << ",\n      \"terrainBaseStochastic\": true";
        if (sc.terrainTintVariation > 0.0f)
            json << ",\n      \"terrainTintVariation\": "
                 << fmtFloat(sc.terrainTintVariation)
                 << ",\n      \"terrainTintScale\": "
                 << fmtFloat(sc.terrainTintScale);
        // Ordered ids only - each object's body is a separate objects/<id>.json
        // file (written below). Order is significant (first Player / SpawnPoint
        // wins, draw order), so it is preserved by the list.
        json << ",\n      \"objects\": [";
        for (size_t k = 0; k < sc.objects.size(); ++k)
            json << (k ? ", " : "") << "\"" << sc.objects[k].id << "\"";
        json << "]";
        json << " }";
    }
    json << "\n  ]";
}

// Fonts ride in the Hud section (HUD text + menus reference them), so they
// travel over the collaboration wire as part of that section's blob.
static void writeHudSection(std::ostream& json, const Project& p) {
    json << "\"fonts\": [";
    for (size_t i = 0; i < p.fonts.size(); ++i) {
        const GameFont& f = p.fonts[i];
        json << (i ? ",\n    " : "\n    ") << "{ \"name\": \"" << jsonEscape(f.name)
             << "\", \"path\": \"" << jsonEscape(f.fontPath)
             << "\", \"atlasSize\": " << f.atlasSize << ", \"color\": "
             << fmtVec3(f.color) << ", \"shadow\": " << (f.shadow ? "true" : "false")
             << ", \"quant\": \"" << f.quant << "\" }";
    }
    json << (p.fonts.empty() ? "]" : "\n  ]");
    json << ",\n  \"hud\": [";
    for (size_t i = 0; i < p.hud.size(); ++i) {
        const HudImage& h = p.hud[i];
        json << (i ? ",\n    " : "\n    ") << "{ \"name\": \"" << h.name << "\", \"image\": \""
             << h.imagePath << "\", \"pos\": [" << fmtFloat(h.pos[0]) << ", "
             << fmtFloat(h.pos[1]) << "], \"size\": [" << fmtFloat(h.size[0]) << ", "
             << fmtFloat(h.size[1]) << "], \"texW\": " << h.texW << ", \"texH\": "
             << h.texH << ", \"texQuant\": \"" << h.texQuant << "\" }";
    }
    json << (p.hud.empty() ? "]" : "\n  ]");
    // The USE prompt HUD element (non-deletable; imagePath "" = built-in).
    json << ",\n  \"usePrompt\": { \"image\": \"" << p.usePrompt.imagePath
         << "\", \"pos\": [" << fmtFloat(p.usePrompt.pos[0]) << ", "
         << fmtFloat(p.usePrompt.pos[1]) << "], \"size\": ["
         << fmtFloat(p.usePrompt.size[0]) << ", " << fmtFloat(p.usePrompt.size[1])
         << "], \"texW\": " << p.usePrompt.texW << ", \"texH\": " << p.usePrompt.texH
         << ", \"texQuant\": \"" << p.usePrompt.texQuant << "\" }";
    json << ",\n  \"hudTexts\": [";
    for (size_t i = 0; i < p.hudTexts.size(); ++i) {
        const HudText& t = p.hudTexts[i];
        json << (i ? ",\n    " : "\n    ") << "{ \"name\": \"" << jsonEscape(t.name)
             << "\", \"text\": \"" << jsonEscape(t.text) << "\", \"pos\": ["
             << fmtFloat(t.pos[0]) << ", " << fmtFloat(t.pos[1]) << "], \"size\": "
             << t.size << ", \"color\": " << fmtVec3(t.color)
             << (t.font.empty() ? "" : ", \"font\": \"" + jsonEscape(t.font) + "\"")
             << ", \"shadow\": " << (t.shadow ? "true" : "false")
             << ", \"visibleAtStart\": " << (t.visibleAtStart ? "true" : "false")
             << " }";
    }
    json << (p.hudTexts.empty() ? "]" : "\n  ]");
    json << ",\n  \"hudBloomLayer\": " << p.hudBloomLayer;
    json << ",\n  \"hudGrainLayer\": " << p.hudGrainLayer;
    if (!p.screenFx.empty()) {
        json << ",\n  \"screenFx\": [";
        for (size_t i = 0; i < p.screenFx.size(); ++i) {
            const ScreenFxPlacement& f = p.screenFx[i];
            json << (i ? ",\n    " : "\n    ") << "{ \"key\": \""
                 << jsonEscape(f.key) << "\", \"layer\": " << f.layer
                 << ", \"enabled\": " << (f.enabled ? "true" : "false")
                 << ", \"params\": [" << fmtFloat(f.params[0]) << ", "
                 << fmtFloat(f.params[1]) << ", " << fmtFloat(f.params[2])
                 << ", " << fmtFloat(f.params[3]) << "] }";
        }
        json << "\n  ]";
    }
}

static void writeAudioSection(std::ostream& json, const Project& p) {
    json << "\"music\": [";
    for (size_t i = 0; i < p.music.size(); ++i)
        json << (i ? ", " : "") << "\"" << jsonEscape(p.music[i]) << "\"";
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
        json << (i ? ", " : "") << "\"" << jsonEscape(p.sounds[i]) << "\"";
    json << "]";
}

// Empty (no keys) when there are no overrides - the composers skip it then,
// matching the manifest's historical conditional emission.
static void writeTexQualitySection(std::ostream& json, const Project& p) {
    if (p.textureQuality.empty()) return;
    json << "\"textureQuality\": {";
    bool first = true;
    for (const auto& [asset, q] : p.textureQuality) {
        json << (first ? " " : ", ") << "\"" << jsonEscape(asset) << "\": \"" << q
             << "\"";
        first = false;
    }
    json << " }";
}

// Also conditional: no custom LOD meshes = no key at all.
static void writeModelLodsSection(std::ostream& json, const Project& p) {
    if (p.modelLods.empty()) return;
    json << "\"modelLods\": {";
    bool first = true;
    for (const auto& [asset, tiers] : p.modelLods) {
        json << (first ? " " : ", ") << "\"" << jsonEscape(asset) << "\": [";
        for (size_t i = 0; i < tiers.size(); ++i)
            json << (i ? ", " : "") << "\"" << jsonEscape(tiers[i]) << "\"";
        json << "]";
        first = false;
    }
    json << " }";
}

static void writeSaveDataSection(std::ostream& json, const Project& p) {
    json << "\"saveValues\": [";
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
}

static void writeGradingsSection(std::ostream& json, const Project& p) {
    json << "\"gradings\": [";
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
}

static void writeAmbienceSection(std::ostream& json, const Project& p) {
    json << "\"ambience\": [";
    for (size_t i = 0; i < p.ambiencePresets.size(); ++i) {
        const AmbiencePreset& a = p.ambiencePresets[i];
        json << (i ? ",\n    " : "\n    ") << "{ \"name\": \"" << jsonEscape(a.name)
             << "\", \"skyColor\": " << fmtVec3(a.skyColor)
             << ", \"skyTopColor\": " << fmtVec3(a.skyTopColor)
             << ", \"skyDome\": " << (a.skyDome ? "true" : "false")
             << ", \"zenithSize\": " << fmtFloat(a.zenithSize)
             << ", \"lightDir\": " << fmtVec3(a.lightDir)
             << ", \"ambient\": " << fmtFloat(a.ambient)
             << ", \"diffuse\": " << fmtFloat(a.diffuse)
             << ", \"lightColor\": " << fmtVec3(a.lightColor)
             << ", \"brightness\": " << fmtFloat(a.brightness)
             << ", \"aoEnabled\": " << (a.aoEnabled ? "true" : "false")
             << ", \"aoStrength\": " << fmtFloat(a.aoStrength)
             << ", \"aoRadius\": " << fmtFloat(a.aoRadius)
             << ", \"fogEnabled\": " << (a.fogEnabled ? "true" : "false")
             << ", \"fogColor\": " << fmtVec3(a.fogColor)
             << ", \"fogStart\": " << fmtFloat(a.fogStart)
             << ", \"fogEnd\": " << fmtFloat(a.fogEnd) << " }";
    }
    json << (p.ambiencePresets.empty() ? "]" : "\n  ]");
    json << ",\n  \"defaultAmbience\": " << p.defaultAmbience;
}

static void writeLoadingScreensSection(std::ostream& json, const Project& p) {
    json << "\"loadingScreens\": [";
    for (size_t i = 0; i < p.loadingScreens.size(); ++i) {
        const LoadingScreenDef& ls = p.loadingScreens[i];
        json << (i ? ",\n    " : "\n    ") << "{ \"name\": \"" << jsonEscape(ls.name)
             << "\", \"bgColor\": " << fmtVec3(ls.bgColor) << ",\n      \"images\": [";
        for (size_t k = 0; k < ls.images.size(); ++k) {
            const HudImage& h = ls.images[k];
            json << (k ? ",\n        " : "\n        ") << "{ \"name\": \""
                 << jsonEscape(h.name) << "\", \"image\": \"" << h.imagePath
                 << "\", \"pos\": [" << fmtFloat(h.pos[0]) << ", " << fmtFloat(h.pos[1])
                 << "], \"size\": [" << fmtFloat(h.size[0]) << ", " << fmtFloat(h.size[1])
                 << "], \"texW\": " << h.texW << ", \"texH\": " << h.texH
                 << ", \"texQuant\": \"" << h.texQuant << "\" }";
        }
        json << (ls.images.empty() ? "]" : "\n      ]") << ",\n      \"texts\": [";
        for (size_t k = 0; k < ls.texts.size(); ++k) {
            const HudText& t = ls.texts[k];
            json << (k ? ",\n        " : "\n        ") << "{ \"name\": \""
                 << jsonEscape(t.name) << "\", \"text\": \"" << jsonEscape(t.text)
                 << "\", \"pos\": [" << fmtFloat(t.pos[0]) << ", " << fmtFloat(t.pos[1])
                 << "], \"size\": " << t.size << ", \"color\": " << fmtVec3(t.color)
                 << (t.font.empty() ? "" : ", \"font\": \"" + jsonEscape(t.font) + "\"")
                 << ", \"shadow\": " << (t.shadow ? "true" : "false") << " }";
        }
        json << (ls.texts.empty() ? "]" : "\n      ]") << ",\n      \"bars\": [";
        for (size_t k = 0; k < ls.bars.size(); ++k) {
            const LoadingBar& b = ls.bars[k];
            json << (k ? ",\n        " : "\n        ") << "{ \"name\": \""
                 << jsonEscape(b.name) << "\", \"kind\": " << b.kind
                 << ", \"pos\": [" << fmtFloat(b.pos[0]) << ", " << fmtFloat(b.pos[1])
                 << "], \"size\": [" << fmtFloat(b.size[0]) << ", " << fmtFloat(b.size[1])
                 << "], \"bgColor\": " << fmtVec3(b.bgColor)
                 << ", \"fillColor\": " << fmtVec3(b.fillColor)
                 << ", \"segments\": " << b.segments
                 << ", \"spacing\": " << fmtFloat(b.spacing);
            if (!b.segImage.imagePath.empty())
                json << ", \"segImage\": { \"image\": \"" << b.segImage.imagePath
                     << "\", \"texW\": " << b.segImage.texW << ", \"texH\": "
                     << b.segImage.texH << ", \"texQuant\": \"" << b.segImage.texQuant
                     << "\" }";
            json << " }";
        }
        json << (ls.bars.empty() ? "]" : "\n      ]") << " }";
    }
    json << (p.loadingScreens.empty() ? "]" : "\n  ]");
    json << ",\n  \"defaultLoadingScreen\": " << p.defaultLoadingScreen;
}

static void writeSplashSection(std::ostream& json, const Project& p) {
    json << "\"splashScreens\": [";
    for (size_t i = 0; i < p.splashScreens.size(); ++i) {
        const SplashScreen& s = p.splashScreens[i];
        const HudImage& h = s.image;
        json << (i ? ",\n    " : "\n    ") << "{ \"name\": \"" << jsonEscape(s.name)
             << "\", \"duration\": " << fmtFloat(s.duration)
             << ", \"bgColor\": " << fmtVec3(s.bgColor)
             << ", \"image\": { \"image\": \"" << h.imagePath << "\", \"pos\": ["
             << fmtFloat(h.pos[0]) << ", " << fmtFloat(h.pos[1]) << "], \"size\": ["
             << fmtFloat(h.size[0]) << ", " << fmtFloat(h.size[1]) << "], \"texW\": "
             << h.texW << ", \"texH\": " << h.texH << ", \"texQuant\": \""
             << h.texQuant << "\" } }";
    }
    json << (p.splashScreens.empty() ? "]" : "\n  ]");
}

static void writeSequencesSection(std::ostream& json, const Project& p) {
    json << "\"sequences\": [";
    for (size_t i = 0; i < p.sequences.size(); ++i) {
        const Sequence& s = p.sequences[i];
        json << (i ? ",\n    " : "\n    ") << "{ \"name\": \"" << jsonEscape(s.name)
             << "\", \"duration\": " << fmtFloat(s.duration)
             << ", \"loop\": " << (s.loop ? "true" : "false")
             << ", \"cameraEnabled\": " << (s.cameraEnabled ? "true" : "false")
             << ", \"hidePlayer\": " << (s.hidePlayer ? "true" : "false")
             << ", \"bars\": " << s.bars
             << ", \"skippable\": " << (s.skippable ? "true" : "false")
             << ", \"fadeIn\": " << fmtFloat(s.fadeIn)
             << ", \"fadeOut\": " << fmtFloat(s.fadeOut)
             << ", \"barsSlideIn\": " << fmtFloat(s.barsSlideIn)
             << ", \"barsSlideOut\": " << fmtFloat(s.barsSlideOut)
             << ", \"tracks\": [";
        for (size_t ti = 0; ti < s.tracks.size(); ++ti) {
            const SeqTrack& t = s.tracks[ti];
            json << (ti ? ",\n      " : "\n      ") << "{ \"target\": \""
                 << jsonEscape(t.target) << "\", \"animPos\": " << (t.animPos ? "true" : "false")
                 << ", \"animRot\": " << (t.animRot ? "true" : "false")
                 << ", \"animScale\": " << (t.animScale ? "true" : "false")
                 << ", \"animColor\": " << (t.animColor ? "true" : "false")
                 << ", \"animVis\": " << (t.animVis ? "true" : "false") << ", \"keys\": [";
            for (size_t ki = 0; ki < t.keys.size(); ++ki) {
                const SeqObjectKey& k = t.keys[ki];
                json << (ki ? ", " : "") << "{ \"t\": " << fmtFloat(k.time)
                     << ", \"pos\": " << fmtVec3(k.position)
                     << ", \"rot\": " << fmtVec3(k.rotation)
                     << ", \"scale\": " << fmtVec3(k.scale)
                     << ", \"color\": " << fmtVec3(k.color)
                     << ", \"vis\": " << (k.visible ? "true" : "false")
                     << ", \"ease\": " << k.easing << " }";
            }
            json << "] }";
        }
        json << (s.tracks.empty() ? "]" : "\n    ]") << ", \"cameraKeys\": [";
        for (size_t ci = 0; ci < s.cameraKeys.size(); ++ci) {
            const SeqCameraKey& k = s.cameraKeys[ci];
            json << (ci ? ", " : "") << "{ \"t\": " << fmtFloat(k.time)
                 << ", \"eye\": " << fmtVec3(k.eye) << ", \"target\": " << fmtVec3(k.target)
                 << ", \"fov\": " << fmtFloat(k.fov)
                 << (k.shake > 0.0f ? ", \"shake\": " + fmtFloat(k.shake) : "")
                 << (k.camera.empty() ? "" : ", \"camera\": \"" + jsonEscape(k.camera) + "\"")
                 << ", \"ease\": " << k.easing << " }";
        }
        json << "] }";
    }
    json << (p.sequences.empty() ? "]" : "\n  ]");
}

static void writeMenusSection(std::ostream& json, const Project& p) {
    json << "\"menus\": [";
    static const char* kMenuActions[] = {"close",     "scene",     "save-menu",
                                         "menu",      "set-value", "add-value",
                                         "event",     "toggle",    "choice",
                                         "apply-video"};
    for (size_t i = 0; i < p.menus.size(); ++i) {
        const GameMenu& m = p.menus[i];
        json << (i ? ",\n    " : "\n    ") << "{ \"name\": \"" << m.name
             << "\", \"title\": \"" << m.title << "\""
             << (m.titleScreen ? ", \"titleScreen\": true" : "")
             << (m.pauseGame ? "" : ", \"pause\": false")
             << (m.pauseMenu ? ", \"pauseMenu\": true" : "")
             << (m.panelW != 256 ? ", \"panelW\": " + std::to_string(m.panelW) : "")
             << (m.showTitle ? "" : ", \"showTitle\": false")
             << (m.font.empty() ? "" : ", \"font\": \"" + jsonEscape(m.font) + "\"")
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
            const int a = (en.action >= 0 && en.action <= 9) ? en.action : 0;
            json << (e ? ",\n        " : "\n        ") << "{ \"label\": \""
                 << en.label << "\", \"action\": \"" << kMenuActions[a] << "\""
                 << (en.param.empty() ? "" : ", \"param\": \"" + en.param + "\"")
                 << (en.amount != 0.0f ? ", \"amount\": " + fmtFloat(en.amount) : "");
            if (!en.options.empty()) {
                json << ", \"options\": [";
                for (size_t o = 0; o < en.options.size(); ++o)
                    json << (o ? ", " : "") << "\"" << jsonEscape(en.options[o])
                         << "\"";
                json << "]";
            }
            if (!en.optionModes.empty()) {
                json << ", \"optionModes\": [";
                for (size_t o = 0; o < en.optionModes.size(); ++o)
                    json << (o ? ", " : "") << en.optionModes[o];
                json << "]";
            }
            static const char* kMenuBinds[] = {
                "",           "music-volume", "sfx-volume",  "deadzone",
                "stick-curve", "display-mode", "widescreen", "player-count"};
            if (en.settingBind >= 1 && en.settingBind <= 7)
                json << ", \"bind\": \"" << kMenuBinds[en.settingBind] << "\"";
            json << " }";
        }
        json << (m.entries.empty() ? "]" : "\n      ]") << " }";
    }
    json << (p.menus.empty() ? "]" : "\n  ]");
}

// The wire form of one section: its manifest keys, no wrapping braces. Empty
// for a conditional section with nothing to emit (TexQuality with no entries).
static std::string sectionBody(const Project& p, Section s) {
    std::ostringstream ss;
    switch (s) {
        case Section::Settings: writeSettingsSection(ss, p); break;
        case Section::Hud: writeHudSection(ss, p); break;
        case Section::Audio: writeAudioSection(ss, p); break;
        case Section::TexQuality: writeTexQualitySection(ss, p); break;
        case Section::ModelLods: writeModelLodsSection(ss, p); break;
        case Section::SaveData: writeSaveDataSection(ss, p); break;
        case Section::Gradings: writeGradingsSection(ss, p); break;
        case Section::Ambience: writeAmbienceSection(ss, p); break;
        case Section::LoadingScreens: writeLoadingScreensSection(ss, p); break;
        case Section::Splash: writeSplashSection(ss, p); break;
        case Section::Sequences: writeSequencesSection(ss, p); break;
        case Section::Menus: writeMenusSection(ss, p); break;
    }
    return ss.str();
}

const char* sectionName(Section s) {
    switch (s) {
        case Section::Settings: return "settings";
        case Section::Hud: return "hud";
        case Section::Audio: return "audio";
        case Section::TexQuality: return "texQuality";
        case Section::ModelLods: return "modelLods";
        case Section::SaveData: return "saveData";
        case Section::Gradings: return "gradings";
        case Section::Ambience: return "ambience";
        case Section::LoadingScreens: return "loadingScreens";
        case Section::Splash: return "splash";
        case Section::Sequences: return "sequences";
        case Section::Menus: return "menus";
    }
    return "unknown";
}

std::string sectionJson(const Project& p, Section s) {
    const std::string body = sectionBody(p, s);
    return body.empty() ? "{ }" : "{ " + body + " }";
}

// The whole .tyra manifest as one string - save() writes it to disk,
// manifestFiles() ships the same bytes over the collaboration wire.
static std::string manifestJson(const Project& p) {
    std::ostringstream json;
    json << "{\n"
         << "  \"name\": \"" << jsonEscape(p.name) << "\",\n"
         << "  \"template\": \"" << p.gameTemplate << "\"";
    // Omitted while empty so a project never born through ensureProjectId
    // round-trips unchanged (and the golden byte layout predates the key).
    if (!p.projectId.empty())
        json << ",\n  \"projectId\": \"" << jsonEscape(p.projectId) << "\"";
    json << ",\n  ";
    writeSettingsSection(json, p);
    json << ",\n  ";
    writeScenesTable(json, p);
    for (int s = 0; s < kSectionCount; ++s) {
        if ((Section)s == Section::Settings) continue;  // already emitted above
        const std::string body = sectionBody(p, (Section)s);
        if (!body.empty()) json << ",\n  " << body;
    }
    // Editor-side state + window layout: the .tyra file is the whole project.
    json << ",\n  \"editor\": { \"selectedObject\": " << p.selectedObject
         << ", \"gizmo\": " << p.gizmoOp << ", \"gizmoSpace\": " << p.gizmoSpace
         << ", \"viewMode\": " << p.viewMode << " }";
    // emulatorPath / ps2LinkIp used to live here but are now machine-global
    // editor settings (editor.ini), no longer written per-project. The reader
    // still accepts them to migrate older projects into the global config.
    // Named window layouts (docking arrangements) + the active one. Replaces the
    // former single "layout" dump; the reader still migrates that legacy key.
    json << ",\n  \"activeLayout\": " << p.activeLayout;
    json << ",\n  \"layouts\": [";
    for (size_t i = 0; i < p.windowLayouts.size(); ++i) {
        const WindowLayout& L = p.windowLayouts[i];
        json << (i ? ",\n" : "\n") << "    { \"name\": \"" << jsonEscape(L.name)
             << "\", \"recipe\": " << L.recipe << ", \"open\": [";
        for (size_t j = 0; j < L.openWindows.size(); ++j)
            json << (j ? ", " : "") << "\"" << jsonEscape(L.openWindows[j]) << "\"";
        json << "], \"ini\": \"" << jsonEscape(L.ini) << "\" }";
    }
    json << (p.windowLayouts.empty() ? "]" : "\n  ]");
    json << "\n}\n";
    return json.str();
}

std::string save(const Project& p) {
    // One file per object. Write every live object, then prune objects/*.json
    // whose object no longer exists (deleted, or moved out on a previous save).
    // Ids are guaranteed present here: every path that reaches save() runs
    // ensureObjectIds first (create / load / commitChange).
    std::error_code ec;
    fs::create_directories(objectsDir(p), ec);
    std::unordered_set<std::string> live;
    for (const SceneData& sc : p.scenes)
        for (const SceneObject& o : sc.objects) {
            live.insert(o.id);
            if (auto err = writeFile(objectPath(p, o.id), objectJson(o) + "\n");
                !err.empty())
                return err;
        }
    for (const auto& entry : fs::directory_iterator(objectsDir(p), ec)) {
        if (!entry.is_regular_file(ec) || entry.path().extension() != ".json") continue;
        if (!live.count(entry.path().stem().string())) fs::remove(entry.path(), ec);
    }

    return writeFile(projectPath(p), manifestJson(p));
}

std::string newObjectId() {
    // 64 bits of randomness rendered as 16 hex chars. Seeded once from the
    // platform entropy source; the sequence is process-global, which is all we
    // need (ids only have to be unique within a project, checked below).
    static std::mt19937_64 rng(std::random_device{}());
    char buf[17];
    std::snprintf(buf, sizeof(buf), "%016llx", (unsigned long long)rng());
    return buf;
}

void ensureProjectId(Project& p) {
    // Same id shape as objects (16 hex chars of 64-bit randomness); projects
    // and objects never share a namespace, so reusing the generator is fine.
    if (p.projectId.empty()) p.projectId = newObjectId();
}

void ensureObjectIds(Project& p) {
    // Single pass: an object gets a fresh id when it has none (legacy / just
    // pasted) or when its id already appeared on an earlier object (accidental
    // duplicate - the first occurrence keeps the id, later ones are reissued).
    std::unordered_set<std::string> seen;
    for (SceneData& s : p.scenes)
        for (SceneObject& o : s.objects) {
            if (o.id.empty() || seen.count(o.id)) {
                std::string id;
                do { id = newObjectId(); } while (seen.count(id));
                o.id = std::move(id);
            }
            seen.insert(o.id);
        }
}

void seedBuiltinLayouts(Project& p) {
    p.windowLayouts.clear();
    // recipe-backed, empty ini: App::buildLayoutRecipe arranges them the first
    // time each is shown (see WindowLayout / LayoutRecipe).
    p.windowLayouts.push_back({"Default", "", (int)LayoutRecipe::Default, {}});
    p.windowLayouts.push_back(
        {"Director", "", (int)LayoutRecipe::Director, {"cutscene"}});
    p.windowLayouts.push_back(
        {"Material Designer", "", (int)LayoutRecipe::Material, {"material"}});
    p.activeLayout = 0;
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

    // Start with one ambience preset (its defaults match the project's default
    // sky/lighting/fog) so the sky renders and the Ambience Editor isn't empty.
    // New projects get baked ambient occlusion out of the box; loaded pre-AO
    // projects keep their look (the field defaults to off on read).
    AmbiencePreset amb;
    amb.name = "Default";
    amb.aoEnabled = true;
    out.ambiencePresets.push_back(amb);
    out.defaultAmbience = 0;

    // Seed the built-in window layouts (Default/Director/Material Designer).
    seedBuiltinLayouts(out);

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

    ensureProjectId(out);
    ensureObjectIds(out);
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

// The terrain-<scene>.heights file body (shared by saveHeights and the
// collaboration manifestFiles).
static std::string heightsText(const SceneData& s) {
    std::ostringstream out;
    out << s.hmW << " " << s.hmD << "\n";
    for (int z = 0; z < s.hmD; ++z) {
        for (int x = 0; x < s.hmW; ++x) {
            if (x) out << " ";
            out << fmtFloat(s.heights[(size_t)z * s.hmW + x]);
        }
        out << "\n";
    }
    return out.str();
}

std::string saveHeights(const Project& p) {
    for (const SceneData& s : p.scenes) {
        if (auto err = writeFile(heightsPath(p, s), heightsText(s)); !err.empty())
            return err;
    }
    return "";
}

std::string scenesLayoutJson(const Project& p) {
    std::ostringstream j;
    j << "{ ";
    writeScenesTable(j, p);
    j << " }";
    return j.str();
}

bool applyScenesLayout(Project& p, const std::string& body) {
    json::Value root;
    if (!json::parse(body, root) || root.type != json::Value::Type::Object)
        return false;
    const auto* scenes = root.find("scenes");
    if (!scenes || scenes->type != json::Value::Type::Array) return false;

    // Pool every current object by id, so a reorder / cross-scene move keeps
    // the object's live body (only membership + order come from the layout).
    std::unordered_map<std::string, SceneObject> pool;
    for (SceneData& s : p.scenes)
        for (SceneObject& o : s.objects)
            if (!o.id.empty()) pool.emplace(o.id, std::move(o));

    // Old per-index bulk terrain data (heightmap + paint splat), preserved
    // across a layout that only renames / reorders: heights arrive as their
    // own message, and the splat weights are not carried in a scene-layout at
    // all, so a structural sync must not wipe the receiver's painted terrain.
    struct Grid {
        std::vector<float> h;
        int w = 0, d = 0;
        std::vector<uint8_t> splat;
        int splatW = 0, splatD = 0;
    };
    std::vector<Grid> oldGrids;
    for (const SceneData& s : p.scenes)
        oldGrids.push_back({s.heights, s.hmW, s.hmD, s.splat, s.splatW, s.splatD});

    std::vector<SceneData> next;
    for (const auto& js : scenes->arr) {
        SceneData sc;
        if (const auto* v = js.find("name")) sc.name = v->stringOr("scene");
        if (const auto* ls = js.find("layers")) readLayersArray(*ls, sc.layers);
        if (const auto* tl = js.find("terrainLayers"))
            readTerrainLayersArray(*tl, sc.terrainLayers);
        if (const auto* v = js.find("terrainBaseStochastic"))
            sc.terrainBaseStochastic = v->boolOr(false);
        if (const auto* v = js.find("terrainTintVariation"))
            sc.terrainTintVariation = (float)v->numberOr(0.0);
        if (const auto* v = js.find("terrainTintScale")) {
            sc.terrainTintScale = (float)v->numberOr(24.0);
            if (sc.terrainTintScale < 1.0f) sc.terrainTintScale = 1.0f;
        }
        if (const auto* t = js.find("terrain")) {
            if (const auto* v = t->find("width")) sc.terrain.width = (int)v->numberOr(64);
            if (const auto* v = t->find("depth")) sc.terrain.depth = (int)v->numberOr(64);
        }
        readSceneVisuals(js, sc);
        if (const auto* objs = js.find("objects");
            objs && objs->type == json::Value::Type::Array) {
            for (const auto& jid : objs->arr) {
                const std::string id = jid.stringOr("");
                if (id.empty()) continue;
                auto it = pool.find(id);
                if (it != pool.end()) {
                    sc.objects.push_back(std::move(it->second));
                    pool.erase(it);
                }
                // An id with no current body is SKIPPED, never turned into an
                // empty placeholder: fabricating one would diverge from a peer
                // that still holds the real object (a delete-vs-keep race). In
                // normal flow the body's upsert always precedes this layout in
                // the same batch, so the id is in the pool.
            }
        }
        const size_t idx = next.size();
        if (idx < oldGrids.size()) {
            sc.heights = std::move(oldGrids[idx].h);
            sc.hmW = oldGrids[idx].w;
            sc.hmD = oldGrids[idx].d;
            sc.splat = std::move(oldGrids[idx].splat);
            sc.splatW = oldGrids[idx].splatW;
            sc.splatD = oldGrids[idx].splatD;
        }
        next.push_back(std::move(sc));
    }
    if (next.empty()) next.push_back(SceneData{});
    p.scenes = std::move(next);
    if (p.activeScene < 0 || p.activeScene >= (int)p.scenes.size()) p.activeScene = 0;
    ensureHeightmap(p);
    return true;
}

std::vector<VirtualFile> manifestFiles(const Project& p) {
    // Exactly the bytes save()/saveHeights() would write, without touching
    // disk - the live in-memory model, which on a dirty host is NEWER than the
    // saved files. Forward-slash relative paths (wire paths).
    std::vector<VirtualFile> out;
    out.push_back({p.name + ".tyra", manifestJson(p)});
    for (const SceneData& sc : p.scenes)
        for (const SceneObject& o : sc.objects)
            out.push_back({"objects/" + o.id + ".json", objectJson(o) + "\n"});
    for (const SceneData& s : p.scenes)
        out.push_back({"terrain-" + s.name + ".heights", heightsText(s)});
    return out;
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

// --- Terrain splatmap --------------------------------------------------------

static void ensureSceneSplatmap(const Project& p, SceneData& s) {
    const int n = (int)s.terrainLayers.size();
    if (n == 0) {  // no layers -> the single-material terrain, no splat
        s.splat.clear();
        s.splatW = s.splatD = 0;
        return;
    }
    // Per-VERTEX weights on the terrain render grid: the blend ships as
    // Gouraud vertex alpha, so vertex resolution IS the blend resolution.
    int vw = 0, vd = 0;
    sceneGridDims(p, s, vw, vd);
    if (s.splatW == vw && s.splatD == vd && (int)s.splat.size() == vw * vd * n)
        return;  // already matches

    // Resample the existing map (nearest) into the new grid/stride. Layers keep
    // their index, so adding a layer at the end leaves a fresh empty column and
    // a terrain-detail change preserves the painted shape (same policy as
    // ensureSceneHeightmap). Mid-list add/remove is handled by the caller
    // shifting the byte columns before this runs.
    const int oldN =
        (s.splatW > 0 && s.splatD > 0)
            ? (int)(s.splat.size() / ((size_t)s.splatW * s.splatD))
            : 0;
    std::vector<uint8_t> next((size_t)vw * vd * n, 0);
    if (oldN > 0 && (int)s.splat.size() == s.splatW * s.splatD * oldN) {
        const int copyN = n < oldN ? n : oldN;
        for (int z = 0; z < vd; ++z)
            for (int x = 0; x < vw; ++x) {
                const int sx = (int)((float)x / (vw - 1) * (s.splatW - 1) + 0.5f);
                const int sz = (int)((float)z / (vd - 1) * (s.splatD - 1) + 0.5f);
                const uint8_t* src =
                    &s.splat[((size_t)sz * s.splatW + sx) * oldN];
                uint8_t* dst = &next[((size_t)z * vw + x) * n];
                for (int l = 0; l < copyN; ++l) dst[l] = src[l];
            }
    }
    s.splat = std::move(next);
    s.splatW = vw;
    s.splatD = vd;
}

void ensureSplatmap(Project& p) {
    for (SceneData& s : p.scenes) ensureSceneSplatmap(p, s);
}

void paintSplat(Project& p, int layer, float worldX, float worldZ, float radius,
                float delta) {
    SceneData& s = p.active();
    const int n = (int)s.terrainLayers.size();
    if (n == 0 || layer < 0 || layer >= n) return;
    if (s.splatW < 1 || s.splatD < 1 ||
        (int)s.splat.size() != s.splatW * s.splatD * n)
        ensureSceneSplatmap(p, s);
    if (s.splatW < 1 || radius <= 0.0f) return;

    const float w = (float)s.terrain.width, d = (float)s.terrain.depth;
    // Vertex positions on the render grid - the same lattice sculptHeightmap
    // brushes, since the weights live on the terrain vertices.
    const float stepX = w / (s.splatW - 1), stepZ = d / (s.splatD - 1);
    const bool erase = delta < 0.0f;
    const float mag = std::fabs(delta);

    for (int z = 0; z < s.splatD; ++z) {
        for (int x = 0; x < s.splatW; ++x) {
            const float vx = -w * 0.5f + x * stepX;
            const float vz = -d * 0.5f + z * stepZ;
            const float dx = vx - worldX, dz = vz - worldZ;
            const float dist = std::sqrt(dx * dx + dz * dz);
            if (dist >= radius) continue;
            const float t = dist / radius;
            const float falloff = 0.5f + 0.5f * std::cos(t * 3.14159265f);
            const float amt = mag * falloff * 255.0f;
            uint8_t* cell = &s.splat[((size_t)z * s.splatW + x) * n];
            auto clamp8 = [](float v) {
                return (uint8_t)(v < 0.0f ? 0.0f : (v > 255.0f ? 255.0f : v));
            };
            if (erase) {
                cell[layer] = clamp8(cell[layer] - amt);  // reveal what's below
            } else {
                cell[layer] = clamp8(cell[layer] + amt);
                // Painting a layer pushes the OTHER extra layers back (and the
                // base fills the remainder), so a stroke reads as "replace".
                for (int l = 0; l < n; ++l)
                    if (l != layer) cell[l] = clamp8(cell[l] - amt);
            }
        }
    }
}

void addTerrainLayer(Project& p, const std::string& name,
                     const std::string& material) {
    SceneData& s = p.active();
    TerrainLayer l;
    l.name = name;
    l.material = material;
    s.terrainLayers.push_back(l);
    ensureSceneSplatmap(p, s);  // grows the stride, new column zero-filled
}

void removeTerrainLayer(Project& p, int idx) {
    SceneData& s = p.active();
    const int n = (int)s.terrainLayers.size();
    if (idx < 0 || idx >= n) return;
    // Drop the layer's byte column from the interleaved splat before erasing it.
    if (n > 1 && s.splatW > 0 && s.splatD > 0 &&
        (int)s.splat.size() == s.splatW * s.splatD * n) {
        std::vector<uint8_t> next((size_t)s.splatW * s.splatD * (n - 1));
        const size_t texels = (size_t)s.splatW * s.splatD;
        for (size_t t = 0; t < texels; ++t) {
            const uint8_t* src = &s.splat[t * n];
            uint8_t* dst = &next[t * (n - 1)];
            int w = 0;
            for (int l = 0; l < n; ++l)
                if (l != idx) dst[w++] = src[l];
        }
        s.splat = std::move(next);
    } else if (n == 1) {
        s.splat.clear();
        s.splatW = s.splatD = 0;
    }
    s.terrainLayers.erase(s.terrainLayers.begin() + idx);
    ensureSceneSplatmap(p, s);
}

void moveTerrainLayer(Project& p, int idx, int dir) {
    SceneData& s = p.active();
    const int n = (int)s.terrainLayers.size();
    const int j = idx + dir;
    if (idx < 0 || idx >= n || j < 0 || j >= n) return;
    std::swap(s.terrainLayers[idx], s.terrainLayers[j]);
    if (s.splatW > 0 && s.splatD > 0 &&
        (int)s.splat.size() == s.splatW * s.splatD * n) {
        const size_t texels = (size_t)s.splatW * s.splatD;
        for (size_t t = 0; t < texels; ++t) {
            uint8_t* cell = &s.splat[t * n];
            std::swap(cell[idx], cell[j]);
        }
    }
}

// One splat sidecar per scene: terrain-<scene>.splat. Binary (a 256x256 map is
// far too big for the text format heights use): "TXSP", int32 w/h/layers,
// then w*h*layers weight bytes (row-major, layer-interleaved).
static fs::path splatPath(const Project& p, const SceneData& s) {
    return fs::path(p.dir) / ("terrain-" + s.name + ".splat");
}

std::string saveSplat(const Project& p) {
    for (const SceneData& s : p.scenes) {
        const int n = (int)s.terrainLayers.size();
        if (n == 0 || s.splat.empty()) {  // no layers -> drop any stale sidecar
            std::error_code ec;
            fs::remove(splatPath(p, s), ec);
            continue;
        }
        std::string buf;
        buf.reserve(16 + s.splat.size());
        buf.append("TXSP", 4);
        auto putI = [&](int32_t v) {
            buf.append(reinterpret_cast<const char*>(&v), sizeof(v));
        };
        putI(s.splatW);
        putI(s.splatD);
        putI(n);
        buf.append(reinterpret_cast<const char*>(s.splat.data()), s.splat.size());
        if (auto err = writeFile(splatPath(p, s), buf); !err.empty()) return err;
    }
    return "";
}

void loadSplat(Project& p) {
    for (SceneData& s : p.scenes) {
        std::ifstream f(splatPath(p, s), std::ios::binary);
        if (!f) continue;
        char magic[4] = {0};
        f.read(magic, 4);
        if (std::string(magic, 4) != "TXSP") continue;
        int32_t w = 0, h = 0, n = 0;
        f.read(reinterpret_cast<char*>(&w), sizeof(w));
        f.read(reinterpret_cast<char*>(&h), sizeof(h));
        f.read(reinterpret_cast<char*>(&n), sizeof(n));
        if (!f || w < 2 || h < 2 || w > 1024 || h > 1024 || n < 1 || n > 64)
            continue;
        std::vector<uint8_t> data((size_t)w * h * n);
        f.read(reinterpret_cast<char*>(data.data()), (std::streamsize)data.size());
        if (!f) continue;
        s.splat = std::move(data);
        s.splatW = w;
        s.splatD = h;
    }
    ensureSplatmap(p);  // reconcile with the current layer count / resolution
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
        // Empty on pre-id projects; project::ensureObjectIds fills it after the
        // load and it is written back on the next save.
        if (const auto* v = jo.find("id")) o.id = v->stringOr("");
        if (const auto* v = jo.find("name")) o.name = v->stringOr("object");
        if (const auto* v = jo.find("type")) o.type = primitiveTypeFromName(v->stringOr("box"));
        readVec3(jo.find("position"), o.position);
        readVec3(jo.find("rotation"), o.rotation);
        readVec3(jo.find("scale"), o.scale);
        readVec3(jo.find("color"), o.color);
        if (const auto* v = jo.find("physics"))
            o.physics = v->type == json::Value::Type::Bool && v->boolean;
        if (const auto* v = jo.find("physMass")) o.physMass = (float)v->numberOr(1.0);
        if (o.physMass < 0.05f) o.physMass = 0.05f;
        if (const auto* v = jo.find("physBounce"))
            o.physBounce = (float)v->numberOr(0.35);
        if (o.physBounce < 0.0f) o.physBounce = 0.0f;
        if (o.physBounce > 1.0f) o.physBounce = 1.0f;
        if (const auto* v = jo.find("physFriction"))
            o.physFriction = (float)v->numberOr(0.5);
        if (o.physFriction < 0.0f) o.physFriction = 0.0f;
        if (o.physFriction > 1.0f) o.physFriction = 1.0f;
        if (const auto* v = jo.find("physTumble"))
            o.physTumble = !(v->type == json::Value::Type::Bool && !v->boolean);
        if (const auto* v = jo.find("physSleep"))
            o.physSleep = (float)v->numberOr(3.0);
        if (o.physSleep < 0.1f) o.physSleep = 0.1f;
        if (o.physSleep > 60.0f) o.physSleep = 60.0f;
        if (const auto* v = jo.find("usable"))
            o.usable = v->type == json::Value::Type::Bool && v->boolean;
        if (const auto* v = jo.find("pickable")) o.pickable = v->boolOr(false);
        if (const auto* v = jo.find("pickThrow")) o.pickThrow = v->boolOr(false);
        if (const auto* v = jo.find("saveState"))
            o.saveState = v->type == json::Value::Type::Bool && v->boolean;
        if (const auto* v = jo.find("collision")) {
            const std::string mode = v->stringOr("box");
            o.collisionMode = mode == "mesh" ? 1 : mode == "none" ? 2 : 0;
        }
        if (const auto* v = jo.find("layer")) o.layer = v->stringOr("");
        // Default depends on the shape (box baseline is 1, curved is 16); old
        // projects with no "detail" key keep each shape's plain look.
        o.primDetail = defaultPrimDetail(o.type);
        if (const auto* v = jo.find("detail"))
            o.primDetail = clampPrimDetail(o.type, (int)v->numberOr(o.primDetail));
        if (const auto* v = jo.find("drawDistance")) {
            o.drawDistance = (float)v->numberOr(0.0);
            if (o.drawDistance < 0.0f) o.drawDistance = 0.0f;
        }
        if (const auto* v = jo.find("reflected")) o.reflected = v->boolOr(false);
        if (const auto* v = jo.find("castShadow")) o.castShadow = v->boolOr(true);
        if (const auto* v = jo.find("model")) o.modelPath = v->stringOr("");
        if (const auto* v = jo.find("material")) o.materialPath = v->stringOr("");
        if (const auto* v = jo.find("decalProject")) o.decalProject = v->boolOr(false);
        // pre-materials projects had a per-object "texture" PNG - dropped
        if (const auto* pl = jo.find("player")) {
            if (const auto* v = pl->find("mode")) {
                const std::string m = v->stringOr("walk");
                o.playerMode = m == "noclip" ? 1 : (m == "thirdperson" ? 2 : 0);
            }
            if (const auto* tp = pl->find("thirdPerson")) {
                if (const auto* v = tp->find("idleClip")) o.playerIdleClip = v->stringOr("");
                if (const auto* v = tp->find("walkClip")) o.playerWalkClip = v->stringOr("");
                if (const auto* v = tp->find("runClip")) o.playerRunClip = v->stringOr("");
                if (const auto* v = tp->find("jumpClip")) o.playerJumpClip = v->stringOr("");
                if (const auto* v = tp->find("runThreshold"))
                    o.playerRunThreshold = (float)v->numberOr(0.55);
                if (const auto* v = tp->find("camDist"))
                    o.playerCamDist = (float)v->numberOr(6.0);
                if (const auto* v = tp->find("camHeight"))
                    o.playerCamHeight = (float)v->numberOr(1.6);
                if (const auto* v = tp->find("camShoulder"))
                    o.playerCamShoulder = (float)v->numberOr(0.0);
                if (const auto* v = tp->find("turnRate"))
                    o.playerTurnRate = (float)v->numberOr(0.25);
                if (const auto* v = tp->find("camStyle")) {
                    const std::string s = v->stringOr("orbit");
                    o.playerCamStyle = s == "topdown"     ? 1
                                       : s == "isometric" ? 2
                                       : s == "fixed"     ? 3
                                                          : 0;
                }
                if (const auto* v = tp->find("camPitch"))
                    o.playerCamPitch = (float)v->numberOr(55.0);
                if (const auto* v = tp->find("camYaw"))
                    o.playerCamYaw = (float)v->numberOr(45.0);
                if (const auto* v = tp->find("camRotate"))
                    o.playerCamYawRotate = v->boolOr(false);
            }
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
            if (const auto* fl = pl->find("flashlight")) {
                if (const auto* v = fl->find("enabled"))
                    o.flashlightEnabled = v->boolOr(false);
                readVec3(fl->find("color"), o.flashlightColor);
                if (const auto* v = fl->find("range"))
                    o.flashlightRange = (float)v->numberOr(30.0);
                if (const auto* v = fl->find("angle"))
                    o.flashlightAngle = (float)v->numberOr(20.0);
                if (const auto* v = fl->find("toggle"))
                    o.flashlightToggleButton = v->stringOr("");
                if (o.flashlightRange < 1.0f) o.flashlightRange = 1.0f;
                if (o.flashlightAngle < 2.0f) o.flashlightAngle = 2.0f;
                if (o.flashlightAngle > 80.0f) o.flashlightAngle = 80.0f;
            }
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
        if (const auto* cm = jo.find("camera")) {
            if (const auto* v = cm->find("fov")) o.cameraFov = (float)v->numberOr(60.0);
            if (o.cameraFov < 20.0f) o.cameraFov = 20.0f;
            if (o.cameraFov > 110.0f) o.cameraFov = 110.0f;
            if (const auto* v = cm->find("feed")) o.camFeed = v->boolOr(false);
            if (const auto* v = cm->find("feedTerrain"))
                o.camFeedTerrain = !(v->type == json::Value::Type::Bool && !v->boolean);
            if (const auto* v = cm->find("feedObjects");
                v && v->type == json::Value::Type::Array) {
                for (const auto& s : v->arr)
                    if (s.type == json::Value::Type::String && !s.str.empty())
                        o.camFeedObjects.push_back(s.str);
            }
        }
        if (const auto* v = jo.find("textureFeed")) o.textureFeed = v->stringOr("");
        if (const auto* mr = jo.find("mirror")) {
            if (const auto* v = mr->find("opacity")) {
                o.mirrorOpacity = (float)v->numberOr(0.35);
                if (o.mirrorOpacity < 0.0f) o.mirrorOpacity = 0.0f;
                if (o.mirrorOpacity > 1.0f) o.mirrorOpacity = 1.0f;
            }
            if (const auto* v = mr->find("reflectPlayer"))
                o.mirrorReflectPlayer = v->boolOr(false);
            if (const auto* v = mr->find("raytraced"))
                o.mirrorRaytraced = v->boolOr(false);
            if (const auto* v = mr->find("rtSize")) {
                const int s = (int)v->numberOr(64);
                o.mirrorRtSize =
                    (s == 32 || s == 128 || s == 256 || s == 512) ? s : 64;
            }
            if (const auto* v = mr->find("objects");
                v && v->type == json::Value::Type::Array) {
                for (const auto& s : v->arr)
                    if (s.type == json::Value::Type::String && !s.str.empty())
                        o.mirrorObjects.push_back(s.str);
            }
        }
        if (const auto* pt = jo.find("portal")) {
            if (const auto* v = pt->find("target")) o.portalTarget = v->stringOr("");
            if (const auto* v = pt->find("showTerrain"))
                o.portalShowTerrain = !(v->type == json::Value::Type::Bool && !v->boolean);
            if (const auto* v = pt->find("teleportObjects"))
                o.portalTeleportObjects = v->boolOr(false);
            if (const auto* v = pt->find("viewAll"))
                o.portalViewAll = v->boolOr(false);
            if (const auto* v = pt->find("objects");
                v && v->type == json::Value::Type::Array) {
                for (const auto& s : v->arr)
                    if (s.type == json::Value::Type::String && !s.str.empty())
                        o.portalObjects.push_back(s.str);
            }
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
        if (const auto* v = jo.find("animLod")) {
            o.animLodOverride = (float)v->numberOr(-1.0);
            if (o.animLodOverride < 0.0f) o.animLodOverride = -1.0f;
        }
        if (const auto* v = jo.find("meshLod")) {
            o.meshLodOverride = (float)v->numberOr(-1.0);
            if (o.meshLodOverride < 0.0f) o.meshLodOverride = -1.0f;
        }
        if (const auto* v = jo.find("modelYaw")) {
            o.modelYawOffset = (float)v->numberOr(0.0);
        }
        if (const auto* sc = jo.find("scripts");
            sc && sc->type == json::Value::Type::Array) {
            for (const auto& s : sc->arr)
                if (s.type == json::Value::Type::String && !s.str.empty())
                    o.scripts.push_back(s.str);
        }
        if (const auto* fg = jo.find("flowGraph")) readFlowGraph(*fg, o.flowGraph);
        out.push_back(std::move(o));
    }
}

bool parseObject(const std::string& body, SceneObject& out) {
    json::Value ov;
    if (!json::parse(body, ov) || ov.type != json::Value::Type::Object) return false;
    // Reuse readObjectsArray by wrapping the single body in a one-element
    // array (json::Value is a plain struct - cheap to build).
    json::Value arr;
    arr.type = json::Value::Type::Array;
    arr.arr.push_back(std::move(ov));
    std::vector<SceneObject> objs;
    readObjectsArray(arr, objs);
    if (objs.empty()) return false;
    out = std::move(objs.front());
    return true;
}

// A scene's "objects" manifest field. New (split) layout: an array of id
// strings, each loaded from objects/<id>.json in list order. Legacy layout: an
// array of inline object bodies. The two are told apart by the first element's
// type; an empty array is either. A referenced object file that is missing or
// malformed is skipped (the object is dropped) rather than aborting the load.
static void readSceneObjects(const Project& p, const json::Value& objs,
                             std::vector<SceneObject>& out) {
    if (objs.type != json::Value::Type::Array) return;
    const bool split = !objs.arr.empty() && objs.arr[0].type == json::Value::Type::String;
    if (!split) {  // legacy: bodies inline in the manifest
        readObjectsArray(objs, out);
        return;
    }
    for (const auto& jid : objs.arr) {
        const std::string id = jid.stringOr("");
        if (id.empty()) continue;
        std::ifstream f(objectPath(p, id), std::ios::binary);
        if (!f) continue;
        std::stringstream ss;
        ss << f.rdbuf();
        json::Value ov;
        if (!json::parse(ss.str(), ov) || ov.type != json::Value::Type::Object) continue;
        // Reuse readObjectsArray by wrapping the single body in a one-element
        // array (json::Value is a plain struct - cheap to build).
        json::Value arr;
        arr.type = json::Value::Type::Array;
        arr.arr.push_back(std::move(ov));
        readObjectsArray(arr, out);
    }
}

// --- Manifest section readers -------------------------------------------------
// The mirror of the section writers above: each replaces its group of fields
// from a parsed manifest (or a sectionJson() wire blob). A section blob is
// total, not a patch - every reader resets its fields to defaults first, so
// keys absent from the blob land on their defaults exactly like a fresh load.

static void readSettingsSection(const json::Value& root, Project& out) {
    out.settings = ProjectSettings{};
    if (const auto* s = root.find("settings")) {
        ProjectSettings& st = out.settings;
        if (const auto* v = s->find("videoSystem")) {
            const std::string sys = v->stringOr("auto");
            st.videoSystem = (sys == "pal" || sys == "ntsc") ? sys : "auto";
        }
        if (const auto* v = s->find("displayMode")) {
            const std::string dm = v->stringOr("interlaced");
            st.displayMode = (dm == "progressive" || dm == "1080i" ||
                              dm == "interlaced-field" || dm == "pal576")
                                 ? dm
                                 : "interlaced";
        }
        if (const auto* v = s->find("palFullHeight"))
            st.palFullHeight = v->boolOr(false);
        if (const auto* v = s->find("widescreen"))
            st.widescreen = v->boolOr(false);
        if (const auto* v = s->find("buildProfile"))
            st.buildProfile = v->stringOr("release") == "debug" ? "debug" : "release";
        // pre-quantization projects keep their full-color output
        st.textureQuant = "none";
        if (const auto* v = s->find("textureQuant")) {
            const std::string q = v->stringOr("none");
            st.textureQuant = (q == "8bit" || q == "4bit") ? q : "none";
        }
        if (const auto* v = s->find("textureAtlas"))
            st.textureAtlas = v->boolOr(false);
        if (const auto* v = s->find("showFps")) st.showFps = v->boolOr(false);
        if (const auto* v = s->find("showMemory")) st.showMemory = v->boolOr(false);
        if (const auto* v = s->find("showProfiler"))
            st.showProfiler = v->boolOr(false);
        if (const auto* v = s->find("liveLink")) st.liveLink = v->boolOr(true);
        if (const auto* v = s->find("disableVsync"))
            st.disableVsync = v->boolOr(false);
        if (const auto* v = s->find("clipping")) {
            // "vu1" (default) = precise per-package classification +
            // clipping on VU1; "precise" = the legacy EE clipper.
            const std::string c = v->stringOr("vu1");
            st.clipping = (c == "fast" || c == "precise") ? c : "vu1";
        } else {
            // pre-clipping-key projects were authored against the EE
            // clipper - keep their behavior
            st.clipping = "precise";
        }
        if (const auto* v = s->find("animLodDistance")) {
            st.animLodDistance = (float)v->numberOr(0.0);
            if (st.animLodDistance < 0.0f) st.animLodDistance = 0.0f;
        }
        if (const auto* v = s->find("meshLodDistance")) {
            st.meshLodDistance = (float)v->numberOr(0.0);
            if (st.meshLodDistance < 0.0f) st.meshLodDistance = 0.0f;
        }
        if (const auto* v = s->find("staticBatching"))
            st.staticBatching = v->boolOr(true);
        if (const auto* v = s->find("envProbeReflected"))
            st.envProbeReflected = v->boolOr(false);
        if (const auto* v = s->find("navCellSize")) {
            st.navCellSize = (float)v->numberOr(1.0);
            if (st.navCellSize < 0.25f) st.navCellSize = 0.25f;
        }
        if (const auto* v = s->find("navMaxSlope")) {
            st.navMaxSlope = (float)v->numberOr(40.0);
            if (st.navMaxSlope < 1.0f) st.navMaxSlope = 1.0f;
            if (st.navMaxSlope > 89.0f) st.navMaxSlope = 89.0f;
        }
        if (const auto* v = s->find("navAgentRadius")) {
            st.navAgentRadius = (float)v->numberOr(0.4);
            if (st.navAgentRadius < 0.0f) st.navAgentRadius = 0.0f;
        }
        if (const auto* v = s->find("terrainDetail"))
            st.terrainDetail = (int)v->numberOr(32);
        if (st.terrainDetail < 4) st.terrainDetail = 4;
        if (st.terrainDetail > 512) st.terrainDetail = 512;
        if (const auto* v = s->find("terrainViewDistance")) {
            st.terrainViewDistance = (float)v->numberOr(0.0);
            if (st.terrainViewDistance < 0.0f) st.terrainViewDistance = 0.0f;
        }
        readVec3(s->find("skyColor"), st.skyColor);
        readVec3(s->find("skyTopColor"), st.skyTopColor);
        if (const auto* v = s->find("skyDome"))
            st.skyDome = v->type == json::Value::Type::Bool && v->boolean;
        if (const auto* v = s->find("zenithSize")) st.zenithSize = (float)v->numberOr(0.5);
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
        // Response curves default to Linear (0) / exponent 2 on older projects.
        if (const auto* v = s->find("stickCurveL")) st.stickCurveL = (int)v->numberOr(0);
        if (const auto* v = s->find("stickCurveR")) st.stickCurveR = (int)v->numberOr(0);
        if (const auto* v = s->find("stickExpL")) st.stickExpL = (float)v->numberOr(2.0);
        if (const auto* v = s->find("stickExpR")) st.stickExpR = (float)v->numberOr(2.0);
        if (st.stickCurveL < 0 || st.stickCurveL > 2) st.stickCurveL = 0;
        if (st.stickCurveR < 0 || st.stickCurveR > 2) st.stickCurveR = 0;
        if (st.stickExpL < 1.0f) st.stickExpL = 1.0f;
        if (st.stickExpR < 1.0f) st.stickExpR = 1.0f;
        if (const auto* v = s->find("multiplayer")) st.multiplayer = v->stringOr("off");
        if (st.multiplayer != "off" && st.multiplayer != "shared" &&
            st.multiplayer != "split")
            st.multiplayer = "off";
        if (const auto* v = s->find("p2JoinOnStart")) st.p2JoinOnStart = v->boolOr(true);
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
        if (const auto* v = s->find("terrainMaterial")) st.terrainMaterial = v->stringOr("");
        auto clamp01 = [](float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); };
        if (const auto* v = s->find("aoEnabled")) st.aoEnabled = v->boolOr(false);
        if (const auto* v = s->find("aoStrength"))
            st.aoStrength = clamp01((float)v->numberOr(0.55));
        if (const auto* v = s->find("aoRadius"))
            st.aoRadius = (float)v->numberOr(2.5);
        if (st.aoRadius < 0.1f) st.aoRadius = 0.1f;
        if (st.aoRadius > 50.0f) st.aoRadius = 50.0f;
        if (const auto* v = s->find("bloom")) st.bloom = clamp01((float)v->numberOr(0.0));
        if (const auto* v = s->find("grain")) st.grain = clamp01((float)v->numberOr(0.0));
        if (const auto* v = s->find("dofAmount"))
            st.dofAmount = clamp01((float)v->numberOr(0.0));
        if (const auto* v = s->find("dofFocus"))
            st.dofFocus = (float)v->numberOr(st.dofFocus);
        if (const auto* v = s->find("dofRange"))
            st.dofRange = (float)v->numberOr(st.dofRange);
        if (const auto* v = s->find("fogEnabled"))
            st.fogEnabled = v->type == json::Value::Type::Bool && v->boolean;
        readVec3(s->find("fogColor"), st.fogColor);
        if (const auto* v = s->find("fogStart")) st.fogStart = (float)v->numberOr(15.0);
        if (const auto* v = s->find("fogEnd")) st.fogEnd = (float)v->numberOr(120.0);
        if (st.fogStart < 0.0f) st.fogStart = 0.0f;
        if (st.fogEnd <= st.fogStart + 1.0f) st.fogEnd = st.fogStart + 1.0f;
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
        if (const auto* v = s->find("highlightOpacity"))
            st.highlightOpacity = (float)v->numberOr(0.56);
        if (st.highlightOpacity < 0.0f) st.highlightOpacity = 0.0f;
        if (st.highlightOpacity > 1.0f) st.highlightOpacity = 1.0f;
        if (const auto* v = s->find("highlightOverlay"))
            st.highlightOverlay = v->boolOr(false);
        if (const auto* v = s->find("loadingScreen"))
            st.loadingScreen = !(v->type == json::Value::Type::Bool && !v->boolean);
    }
}

static void readHudSection(const json::Value& root, Project& out) {
    // Fonts ride in the Hud section (see writeHudSection). Total-replace like
    // every section reader.
    out.fonts.clear();
    if (const auto* fonts = root.find("fonts");
        fonts && fonts->type == json::Value::Type::Array) {
        for (const auto& jf : fonts->arr) {
            GameFont f;
            if (const auto* v = jf.find("name")) f.name = v->stringOr("Default");
            if (const auto* v = jf.find("path")) f.fontPath = v->stringOr("");
            if (const auto* v = jf.find("atlasSize"))
                f.atlasSize = (int)v->numberOr(16);
            f.atlasSize = f.atlasSize < 8 ? 8 : f.atlasSize > 48 ? 48 : f.atlasSize;
            if (const auto* v = jf.find("color");
                v && v->type == json::Value::Type::Array && v->arr.size() >= 3)
                for (int i = 0; i < 3; ++i) f.color[i] = (float)v->arr[i].numberOr(1);
            if (const auto* v = jf.find("shadow")) f.shadow = v->boolOr(true);
            if (const auto* v = jf.find("quant")) f.quant = v->stringOr("4bit");
            out.fonts.push_back(f);
        }
    }
    out.hud.clear();
    out.usePrompt = defaultUsePrompt();
    out.hudTexts.clear();
    out.screenFx.clear();
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
            // Bake settings; absent (older projects) = auto size, full color.
            if (const auto* v = jh.find("texW")) h.texW = (int)v->numberOr(0);
            if (const auto* v = jh.find("texH")) h.texH = (int)v->numberOr(0);
            if (const auto* v = jh.find("texQuant")) {
                const std::string q = v->stringOr("");
                h.texQuant =
                    (q == "none" || q == "8bit" || q == "4bit") ? q : "";
            }
            if (!h.imagePath.empty()) out.hud.push_back(std::move(h));
        }
    }
    // The USE prompt element; absent (older projects) = the classic built-in
    // sprite at its hardcoded placement (defaultUsePrompt in project.hpp).
    if (const auto* up = root.find("usePrompt")) {
        HudImage h = defaultUsePrompt();
        if (const auto* v = up->find("image")) h.imagePath = v->stringOr("");
        if (const auto* v = up->find("pos");
            v && v->type == json::Value::Type::Array && v->arr.size() >= 2) {
            h.pos[0] = (float)v->arr[0].numberOr(h.pos[0]);
            h.pos[1] = (float)v->arr[1].numberOr(h.pos[1]);
        }
        if (const auto* v = up->find("size");
            v && v->type == json::Value::Type::Array && v->arr.size() >= 2) {
            h.size[0] = (float)v->arr[0].numberOr(h.size[0]);
            h.size[1] = (float)v->arr[1].numberOr(h.size[1]);
        }
        if (const auto* v = up->find("texW")) h.texW = (int)v->numberOr(0);
        if (const auto* v = up->find("texH")) h.texH = (int)v->numberOr(0);
        if (const auto* v = up->find("texQuant")) {
            const std::string q = v->stringOr("");
            h.texQuant = (q == "none" || q == "8bit" || q == "4bit") ? q : "";
        }
        out.usePrompt = std::move(h);
    }
    if (const auto* texts = root.find("hudTexts");
        texts && texts->type == json::Value::Type::Array) {
        for (const auto& jt : texts->arr) {
            HudText t;
            if (const auto* v = jt.find("name")) t.name = v->stringOr("text");
            if (const auto* v = jt.find("text")) t.text = v->stringOr("");
            if (const auto* v = jt.find("pos");
                v && v->type == json::Value::Type::Array && v->arr.size() >= 2) {
                t.pos[0] = (float)v->arr[0].numberOr(0.5);
                t.pos[1] = (float)v->arr[1].numberOr(0.8);
            }
            if (const auto* v = jt.find("size")) t.size = (int)v->numberOr(16);
            if (t.size < 8) t.size = 8;
            if (t.size > 48) t.size = 48;
            readVec3(jt.find("color"), t.color);
            if (const auto* v = jt.find("font")) t.font = v->stringOr("");
            if (const auto* v = jt.find("shadow"))
                t.shadow = !(v->type == json::Value::Type::Bool && !v->boolean);
            if (const auto* v = jt.find("visibleAtStart"))
                t.visibleAtStart = v->type == json::Value::Type::Bool && v->boolean;
            if (!t.name.empty()) out.hudTexts.push_back(std::move(t));
        }
    }
    // Effect layer positions; absent (older projects) or out of range = -1,
    // i.e. the effect applies over everything at end of frame - the old
    // behavior. "hudPostFxLayer" is the pre-split key (bloom+grain shared one
    // layer); migrate it to both.
    int legacyLayer = -1;
    if (const auto* v = root.find("hudPostFxLayer"))
        legacyLayer = (int)v->numberOr(-1.0);
    out.hudBloomLayer = legacyLayer;
    out.hudGrainLayer = legacyLayer;
    if (const auto* v = root.find("hudBloomLayer"))
        out.hudBloomLayer = (int)v->numberOr(-1.0);
    if (const auto* v = root.find("hudGrainLayer"))
        out.hudGrainLayer = (int)v->numberOr(-1.0);
    auto clampLayer = [&](int& L) {
        if (L < -1 || L >= (int)out.hud.size()) L = -1;
    };
    clampLayer(out.hudBloomLayer);
    clampLayer(out.hudGrainLayer);

    // Custom screen effect placements. A placement whose .screenfx file was not
    // loaded above (missing / moved project) is dropped - the same rule that
    // cleans up unknown flow-graph nodes.
    if (const auto* sfx = root.find("screenFx");
        sfx && sfx->type == json::Value::Type::Array) {
        for (const auto& jo : sfx->arr) {
            ScreenFxPlacement f;
            if (const auto* v = jo.find("key")) f.key = v->stringOr("");
            if (f.key.empty() || customScreenFx(f.key) == nullptr) continue;
            if (const auto* v = jo.find("layer")) f.layer = (int)v->numberOr(-1.0);
            if (const auto* v = jo.find("enabled"))
                f.enabled = !(v->type == json::Value::Type::Bool && !v->boolean);
            if (const auto* pa = jo.find("params");
                pa && pa->type == json::Value::Type::Array) {
                for (size_t i = 0; i < pa->arr.size() && i < 4; ++i)
                    f.params[i] = (float)pa->arr[i].numberOr(0.0);
            }
            clampLayer(f.layer);
            out.screenFx.push_back(std::move(f));
        }
    }
}

static void readAudioSection(const json::Value& root, Project& out) {
    out.music.clear();
    out.musicBuild.clear();
    out.sounds.clear();
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
}

static void readTexQualitySection(const json::Value& root, Project& out) {
    out.textureQuality.clear();
    if (const auto* tq = root.find("textureQuality");
        tq && tq->type == json::Value::Type::Object) {
        for (const auto& [asset, v] : tq->obj) {
            const std::string q = v.stringOr("");
            if (q == "none" || q == "8bit" || q == "4bit")
                out.textureQuality[asset] = q;
        }
    }
}

static void readModelLodsSection(const json::Value& root, Project& out) {
    out.modelLods.clear();
    if (const auto* ml = root.find("modelLods");
        ml && ml->type == json::Value::Type::Object) {
        for (const auto& [asset, v] : ml->obj) {
            if (v.type != json::Value::Type::Array) continue;
            std::vector<std::string> tiers;
            for (const auto& t : v.arr) {
                const std::string path = t.stringOr("");
                if (!path.empty()) tiers.push_back(path);
            }
            if (!tiers.empty()) out.modelLods[asset] = tiers;
        }
    }
}

static void readSaveDataSection(const json::Value& root, Project& out) {
    out.saveValues.clear();
    out.saveTexts.clear();
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
}

static void readGradingsSection(const json::Value& root, Project& out) {
    out.gradings.clear();
    out.defaultGrading = -1;
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
}

static void readAmbienceSection(const json::Value& root, Project& out) {
    out.ambiencePresets.clear();
    out.defaultAmbience = -1;
    if (const auto* amb = root.find("ambience");
        amb && amb->type == json::Value::Type::Array) {
        for (const auto& ja : amb->arr) {
            AmbiencePreset a;
            if (const auto* v = ja.find("name")) a.name = v->stringOr("");
            readVec3(ja.find("skyColor"), a.skyColor);
            readVec3(ja.find("skyTopColor"), a.skyTopColor);
            if (const auto* v = ja.find("skyDome")) a.skyDome = v->boolOr(true);
            if (const auto* v = ja.find("zenithSize")) a.zenithSize = (float)v->numberOr(0.5);
            readVec3(ja.find("lightDir"), a.lightDir);
            if (const auto* v = ja.find("ambient")) a.ambient = (float)v->numberOr(0.55);
            if (const auto* v = ja.find("diffuse")) a.diffuse = (float)v->numberOr(0.45);
            readVec3(ja.find("lightColor"), a.lightColor);
            if (const auto* v = ja.find("brightness"))
                a.brightness = (float)v->numberOr(1.0);
            if (const auto* v = ja.find("aoEnabled")) a.aoEnabled = v->boolOr(false);
            if (const auto* v = ja.find("aoStrength"))
                a.aoStrength = (float)v->numberOr(0.55);
            if (a.aoStrength < 0.0f) a.aoStrength = 0.0f;
            if (a.aoStrength > 1.0f) a.aoStrength = 1.0f;
            if (const auto* v = ja.find("aoRadius"))
                a.aoRadius = (float)v->numberOr(2.5);
            if (a.aoRadius < 0.1f) a.aoRadius = 0.1f;
            if (a.aoRadius > 50.0f) a.aoRadius = 50.0f;
            if (const auto* v = ja.find("fogEnabled")) a.fogEnabled = v->boolOr(false);
            readVec3(ja.find("fogColor"), a.fogColor);
            if (const auto* v = ja.find("fogStart")) a.fogStart = (float)v->numberOr(15.0);
            if (const auto* v = ja.find("fogEnd")) a.fogEnd = (float)v->numberOr(120.0);
            if (!a.name.empty()) out.ambiencePresets.push_back(std::move(a));
        }
    }
    if (const auto* v = root.find("defaultAmbience"))
        out.defaultAmbience = (int)v->numberOr(-1.0);
    if (out.defaultAmbience < -1 ||
        out.defaultAmbience >= (int)out.ambiencePresets.size())
        out.defaultAmbience = -1;
}

static void readLoadingScreensSection(const json::Value& root, Project& out) {
    out.loadingScreens.clear();
    out.defaultLoadingScreen = -1;
    if (const auto* lss = root.find("loadingScreens");
        lss && lss->type == json::Value::Type::Array) {
        for (const auto& jl : lss->arr) {
            LoadingScreenDef ls;
            if (const auto* v = jl.find("name")) ls.name = v->stringOr("");
            readVec3(jl.find("bgColor"), ls.bgColor);
            if (const auto* imgs = jl.find("images");
                imgs && imgs->type == json::Value::Type::Array) {
                for (const auto& jh : imgs->arr) {
                    HudImage h;
                    if (const auto* v = jh.find("name")) h.name = v->stringOr("");
                    if (const auto* v = jh.find("image")) h.imagePath = v->stringOr("");
                    if (const auto* v = jh.find("pos");
                        v && v->type == json::Value::Type::Array && v->arr.size() >= 2) {
                        h.pos[0] = (float)v->arr[0].numberOr(0.5);
                        h.pos[1] = (float)v->arr[1].numberOr(0.5);
                    }
                    if (const auto* v = jh.find("size");
                        v && v->type == json::Value::Type::Array && v->arr.size() >= 2) {
                        h.size[0] = (float)v->arr[0].numberOr(64.0);
                        h.size[1] = (float)v->arr[1].numberOr(64.0);
                    }
                    if (const auto* v = jh.find("texW")) h.texW = (int)v->numberOr(0);
                    if (const auto* v = jh.find("texH")) h.texH = (int)v->numberOr(0);
                    if (const auto* v = jh.find("texQuant")) {
                        const std::string q = v->stringOr("");
                        h.texQuant = (q == "none" || q == "8bit" || q == "4bit") ? q : "";
                    }
                    if (!h.imagePath.empty()) ls.images.push_back(std::move(h));
                }
            }
            if (const auto* texts = jl.find("texts");
                texts && texts->type == json::Value::Type::Array) {
                for (const auto& jt : texts->arr) {
                    HudText t;
                    if (const auto* v = jt.find("name")) t.name = v->stringOr("text");
                    if (const auto* v = jt.find("text")) t.text = v->stringOr("");
                    if (const auto* v = jt.find("pos");
                        v && v->type == json::Value::Type::Array && v->arr.size() >= 2) {
                        t.pos[0] = (float)v->arr[0].numberOr(0.5);
                        t.pos[1] = (float)v->arr[1].numberOr(0.8);
                    }
                    if (const auto* v = jt.find("size")) t.size = (int)v->numberOr(16);
                    if (t.size < 8) t.size = 8;
                    if (t.size > 48) t.size = 48;
                    readVec3(jt.find("color"), t.color);
                    if (const auto* v = jt.find("font")) t.font = v->stringOr("");
                    if (const auto* v = jt.find("shadow"))
                        t.shadow = !(v->type == json::Value::Type::Bool && !v->boolean);
                    if (!t.name.empty()) ls.texts.push_back(std::move(t));
                }
            }
            if (const auto* bars = jl.find("bars");
                bars && bars->type == json::Value::Type::Array) {
                for (const auto& jb : bars->arr) {
                    LoadingBar b;
                    if (const auto* v = jb.find("name")) b.name = v->stringOr("bar");
                    if (const auto* v = jb.find("kind")) b.kind = (int)v->numberOr(0);
                    if (b.kind != 0 && b.kind != 1) b.kind = 0;
                    if (const auto* v = jb.find("pos");
                        v && v->type == json::Value::Type::Array && v->arr.size() >= 2) {
                        b.pos[0] = (float)v->arr[0].numberOr(0.5);
                        b.pos[1] = (float)v->arr[1].numberOr(0.75);
                    }
                    if (const auto* v = jb.find("size");
                        v && v->type == json::Value::Type::Array && v->arr.size() >= 2) {
                        b.size[0] = (float)v->arr[0].numberOr(256.0);
                        b.size[1] = (float)v->arr[1].numberOr(16.0);
                    }
                    readVec3(jb.find("bgColor"), b.bgColor);
                    readVec3(jb.find("fillColor"), b.fillColor);
                    if (const auto* v = jb.find("segments")) b.segments = (int)v->numberOr(5);
                    if (b.segments < 2) b.segments = 2;
                    if (b.segments > 16) b.segments = 16;
                    if (const auto* v = jb.find("spacing")) b.spacing = (float)v->numberOr(6.0);
                    if (b.spacing < 0.0f) b.spacing = 0.0f;
                    if (const auto* si = jb.find("segImage")) {
                        if (const auto* v = si->find("image"))
                            b.segImage.imagePath = v->stringOr("");
                        if (const auto* v = si->find("texW")) b.segImage.texW = (int)v->numberOr(0);
                        if (const auto* v = si->find("texH")) b.segImage.texH = (int)v->numberOr(0);
                        if (const auto* v = si->find("texQuant")) {
                            const std::string q = v->stringOr("");
                            b.segImage.texQuant =
                                (q == "none" || q == "8bit" || q == "4bit") ? q : "";
                        }
                    }
                    ls.bars.push_back(std::move(b));
                }
            }
            if (!ls.name.empty()) out.loadingScreens.push_back(std::move(ls));
        }
    }
    if (const auto* v = root.find("defaultLoadingScreen"))
        out.defaultLoadingScreen = (int)v->numberOr(-1.0);
    if (out.defaultLoadingScreen < -1 ||
        out.defaultLoadingScreen >= (int)out.loadingScreens.size())
        out.defaultLoadingScreen = -1;
}

static void readSplashSection(const json::Value& root, Project& out) {
    out.splashScreens.clear();
    if (const auto* sps = root.find("splashScreens");
        sps && sps->type == json::Value::Type::Array) {
        for (const auto& jsp : sps->arr) {
            SplashScreen s;
            if (const auto* v = jsp.find("name")) s.name = v->stringOr("splash");
            if (const auto* v = jsp.find("duration"))
                s.duration = (float)v->numberOr(2.0);
            if (s.duration < 0.1f) s.duration = 0.1f;
            if (s.duration > 10.0f) s.duration = 10.0f;
            readVec3(jsp.find("bgColor"), s.bgColor);
            if (const auto* im = jsp.find("image")) {
                if (const auto* v = im->find("image")) s.image.imagePath = v->stringOr("");
                if (const auto* v = im->find("pos");
                    v && v->type == json::Value::Type::Array && v->arr.size() >= 2) {
                    s.image.pos[0] = (float)v->arr[0].numberOr(0.5);
                    s.image.pos[1] = (float)v->arr[1].numberOr(0.5);
                }
                if (const auto* v = im->find("size");
                    v && v->type == json::Value::Type::Array && v->arr.size() >= 2) {
                    s.image.size[0] = (float)v->arr[0].numberOr(512.0);
                    s.image.size[1] = (float)v->arr[1].numberOr(448.0);
                }
                if (const auto* v = im->find("texW")) s.image.texW = (int)v->numberOr(0);
                if (const auto* v = im->find("texH")) s.image.texH = (int)v->numberOr(0);
                if (const auto* v = im->find("texQuant")) {
                    const std::string q = v->stringOr("");
                    s.image.texQuant = (q == "none" || q == "8bit" || q == "4bit") ? q : "";
                }
            }
            if (!s.image.imagePath.empty()) out.splashScreens.push_back(std::move(s));
        }
    }
}

static void readSequencesSection(const json::Value& root, Project& out) {
    out.sequences.clear();
    if (const auto* seqs = root.find("sequences");
        seqs && seqs->type == json::Value::Type::Array) {
        for (const auto& js : seqs->arr) {
            Sequence s;
            if (const auto* v = js.find("name")) s.name = v->stringOr("Cutscene");
            if (const auto* v = js.find("duration")) s.duration = (float)v->numberOr(5.0);
            if (const auto* v = js.find("loop")) s.loop = v->boolOr(false);
            if (const auto* v = js.find("cameraEnabled")) s.cameraEnabled = v->boolOr(false);
            if (const auto* v = js.find("hidePlayer")) s.hidePlayer = v->boolOr(false);
            if (const auto* v = js.find("bars")) s.bars = (int)v->numberOr(0.0);
            if (s.bars < 0 || s.bars >= kSeqBarsStyleCount) s.bars = kSeqBarsNone;
            if (const auto* v = js.find("skippable")) s.skippable = v->boolOr(false);
            if (const auto* v = js.find("fadeIn")) s.fadeIn = (float)v->numberOr(0.0);
            if (const auto* v = js.find("fadeOut")) s.fadeOut = (float)v->numberOr(0.0);
            if (s.fadeIn < 0.0f) s.fadeIn = 0.0f;
            if (s.fadeOut < 0.0f) s.fadeOut = 0.0f;
            // Absent on projects authored before the bars-slide controls: keep
            // the historical fixed 0.4 s slide so they look unchanged.
            if (const auto* v = js.find("barsSlideIn"))
                s.barsSlideIn = (float)v->numberOr(kSeqBarsSlideDefault);
            if (const auto* v = js.find("barsSlideOut"))
                s.barsSlideOut = (float)v->numberOr(kSeqBarsSlideDefault);
            if (s.barsSlideIn < 0.0f) s.barsSlideIn = 0.0f;
            if (s.barsSlideOut < 0.0f) s.barsSlideOut = 0.0f;
            if (const auto* jt = js.find("tracks"); jt && jt->type == json::Value::Type::Array) {
                for (const auto& jtr : jt->arr) {
                    SeqTrack t;
                    if (const auto* v = jtr.find("target")) t.target = v->stringOr("");
                    if (const auto* v = jtr.find("animPos")) t.animPos = v->boolOr(true);
                    if (const auto* v = jtr.find("animRot")) t.animRot = v->boolOr(false);
                    if (const auto* v = jtr.find("animScale")) t.animScale = v->boolOr(false);
                    if (const auto* v = jtr.find("animColor")) t.animColor = v->boolOr(false);
                    if (const auto* v = jtr.find("animVis")) t.animVis = v->boolOr(false);
                    if (const auto* jk = jtr.find("keys"); jk && jk->type == json::Value::Type::Array) {
                        for (const auto& jkk : jk->arr) {
                            SeqObjectKey k;
                            if (const auto* v = jkk.find("t")) k.time = (float)v->numberOr(0.0);
                            readVec3(jkk.find("pos"), k.position);
                            readVec3(jkk.find("rot"), k.rotation);
                            readVec3(jkk.find("scale"), k.scale);
                            readVec3(jkk.find("color"), k.color);
                            if (const auto* v = jkk.find("vis")) k.visible = v->boolOr(true);
                            if (const auto* v = jkk.find("ease")) k.easing = (int)v->numberOr(1.0);
                            t.keys.push_back(k);
                        }
                    }
                    s.tracks.push_back(std::move(t));
                }
            }
            if (const auto* jc = js.find("cameraKeys");
                jc && jc->type == json::Value::Type::Array) {
                for (const auto& jck : jc->arr) {
                    SeqCameraKey k;
                    if (const auto* v = jck.find("t")) k.time = (float)v->numberOr(0.0);
                    readVec3(jck.find("eye"), k.eye);
                    readVec3(jck.find("target"), k.target);
                    if (const auto* v = jck.find("fov")) k.fov = (float)v->numberOr(60.0);
                    if (const auto* v = jck.find("shake")) k.shake = (float)v->numberOr(0.0);
                    if (const auto* v = jck.find("camera")) k.camera = v->stringOr("");
                    if (const auto* v = jck.find("ease")) k.easing = (int)v->numberOr(1.0);
                    s.cameraKeys.push_back(k);
                }
            }
            if (!s.name.empty()) out.sequences.push_back(std::move(s));
        }
    }
}

static void readMenusSection(const json::Value& root, Project& out) {
    out.menus.clear();
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
            if (const auto* v = jm.find("font")) m.font = v->stringOr("");
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
                                    : a == "toggle"    ? MenuEntry::Toggle
                                    : a == "choice"    ? MenuEntry::Choice
                                    : a == "apply-video" ? MenuEntry::ApplyVideo
                                                       : MenuEntry::Close;
                    }
                    if (const auto* v = je.find("param")) en.param = v->stringOr("");
                    if (const auto* v = je.find("amount"))
                        en.amount = (float)v->numberOr(0.0);
                    if (const auto* v = je.find("options");
                        v && v->type == json::Value::Type::Array) {
                        for (const auto& jo : v->arr) {
                            const std::string s = jo.stringOr("");
                            if (!s.empty()) en.options.push_back(s);
                        }
                    }
                    if (const auto* v = je.find("optionModes");
                        v && v->type == json::Value::Type::Array) {
                        for (const auto& jo : v->arr) {
                            int m = (int)jo.numberOr(0.0);
                            // Tyra::DisplayMode range; -1 = project default
                            if (m < -1) m = -1;
                            if (m > 4) m = 4;
                            en.optionModes.push_back(m);
                        }
                    }
                    if (const auto* v = je.find("bind")) {
                        const std::string b = v->stringOr("");
                        en.settingBind =
                            b == "music-volume"  ? MenuEntry::BindMusicVolume
                            : b == "sfx-volume"  ? MenuEntry::BindSfxVolume
                            : b == "deadzone"    ? MenuEntry::BindDeadzone
                            : b == "stick-curve" ? MenuEntry::BindStickCurve
                            : b == "display-mode" ? MenuEntry::BindDisplayMode
                            : b == "widescreen"  ? MenuEntry::BindWidescreen
                            : b == "player-count" ? MenuEntry::BindPlayerCount
                                                 : MenuEntry::BindNone;
                    }
                    m.entries.push_back(std::move(en));
                }
            }
            if (!m.name.empty()) out.menus.push_back(std::move(m));
        }
    }
}

bool applySectionJson(Project& p, Section s, const std::string& body) {
    json::Value root;
    if (!json::parse(body, root) || root.type != json::Value::Type::Object)
        return false;
    switch (s) {
        case Section::Settings: readSettingsSection(root, p); break;
        case Section::Hud: readHudSection(root, p); break;
        case Section::Audio: readAudioSection(root, p); break;
        case Section::TexQuality: readTexQualitySection(root, p); break;
        case Section::ModelLods: readModelLodsSection(root, p); break;
        case Section::SaveData: readSaveDataSection(root, p); break;
        case Section::Gradings: readGradingsSection(root, p); break;
        case Section::Ambience: readAmbienceSection(root, p); break;
        case Section::LoadingScreens: readLoadingScreensSection(root, p); break;
        case Section::Splash: readSplashSection(root, p); break;
        case Section::Sequences: readSequencesSection(root, p); break;
        case Section::Menus: readMenusSection(root, p); break;
    }
    return true;
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
        return "Not a TyraX project (no .tyra file): " + projectDir;
    std::ifstream f(tyraPath, std::ios::binary);
    if (!f) return "Cannot open project file: " + tyraPath.string();
    std::stringstream ss;
    ss << f.rdbuf();

    json::Value root;
    if (!json::parse(ss.str(), root) || root.type != json::Value::Type::Object)
        return tyraPath.filename().string() + " is malformed";

    out = Project{};
    out.dir = fs::path(projectDir).string();
    // Register the project's custom flow nodes BEFORE the graphs are parsed:
    // readFlowGraph drops any node whose type is unknown (line ~156), so a
    // "custom:*" node only survives the load if its .flownode file is present.
    flownode::loadForProject(out.dir);
    // Same for custom screen effects: their placements below reference a
    // screen-effects/*.screenfx file by key, and a placement whose file is
    // missing is dropped (so a moved .tyra cannot silently keep a dead effect).
    screenfx::loadForProject(out.dir);
    if (const auto* v = root.find("name")) out.name = v->stringOr("");
    if (out.name.empty())
        return tyraPath.filename().string() + " is malformed (no name)";

    if (const auto* v = root.find("template"))
        out.gameTemplate = v->stringOr("orbit") == "fpp" ? "fpp" : "orbit";

    if (const auto* v = root.find("projectId")) out.projectId = v->stringOr("");
    ensureProjectId(out);  // backfill projects born before project ids

    readSettingsSection(root, out);

    // Scenes. New format: [{ "name", "objects" }]; legacy: an array of scene
    // name strings plus a project-level "objects" array (single scene).
    if (const auto* scenes = root.find("scenes");
        scenes && scenes->type == json::Value::Type::Array && !scenes->arr.empty()) {
        if (scenes->arr[0].type == json::Value::Type::Object) {
            out.scenes.clear();
            for (const auto& js : scenes->arr) {
                SceneData sc;
                if (const auto* v = js.find("name")) sc.name = v->stringOr("scene");
                if (const auto* ls = js.find("layers")) readLayersArray(*ls, sc.layers);
                if (const auto* tl = js.find("terrainLayers"))
                    readTerrainLayersArray(*tl, sc.terrainLayers);
                if (const auto* v = js.find("terrainBaseStochastic"))
                    sc.terrainBaseStochastic = v->boolOr(false);
                if (const auto* v = js.find("terrainTintVariation"))
                    sc.terrainTintVariation = (float)v->numberOr(0.0);
                if (const auto* v = js.find("terrainTintScale")) {
                    sc.terrainTintScale = (float)v->numberOr(24.0);
                    if (sc.terrainTintScale < 1.0f) sc.terrainTintScale = 1.0f;
                }
                if (const auto* objs = js.find("objects"))
                    readSceneObjects(out, *objs, sc.objects);
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

    // Every scene object must carry a stable id before the caller snapshots the
    // project (loadHistory compares against out.scenes). Pre-id projects get
    // theirs here; they are written back on the next save.
    ensureObjectIds(out);

    readHudSection(root, out);

    readAudioSection(root, out);

    readTexQualitySection(root, out);
    readModelLodsSection(root, out);

    readSaveDataSection(root, out);

    readGradingsSection(root, out);

    readAmbienceSection(root, out);

    readLoadingScreensSection(root, out);

    readSplashSection(root, out);

    readSequencesSection(root, out);

    // Migrate projects authored before the Ambience Editor: sky/lighting/fog
    // used to live in Preferences (global + per-scene overrides). Fold them
    // into presets so the same values keep driving the scene now that those
    // controls have moved. Only runs when the project has no presets yet.
    if (out.ambiencePresets.empty()) {
        auto uniqueName = [&](std::string base) {
            if (base.empty()) base = "Ambience";
            std::string n = base;
            for (int k = 2;; ++k) {
                bool taken = false;
                for (const auto& a : out.ambiencePresets) taken |= (a.name == n);
                if (!taken) return n;
                n = base + "-" + std::to_string(k);
            }
        };
        auto fromSettings = [](const ProjectSettings& s, const std::string& name) {
            AmbiencePreset a;
            a.name = name;
            for (int i = 0; i < 3; ++i) {
                a.skyColor[i] = s.skyColor[i];
                a.skyTopColor[i] = s.skyTopColor[i];
                a.lightDir[i] = s.lightDir[i];
                a.lightColor[i] = s.lightColor[i];
                a.fogColor[i] = s.fogColor[i];
            }
            a.skyDome = s.skyDome;
            a.zenithSize = s.zenithSize;
            a.ambient = s.ambient, a.diffuse = s.diffuse, a.brightness = s.brightness;
            a.aoEnabled = s.aoEnabled, a.aoStrength = s.aoStrength;
            a.aoRadius = s.aoRadius;
            a.fogEnabled = s.fogEnabled, a.fogStart = s.fogStart, a.fogEnd = s.fogEnd;
            return a;
        };
        // Default at index 0. Keep defaultAmbience = -1 during the per-scene
        // loop so resolvedSettings() below sees NO preset overlay and captures
        // each scene's own overridden sky/lighting/fog, not the default's.
        out.ambiencePresets.push_back(fromSettings(out.settings, uniqueName("Default")));
        for (SceneData& sc : out.scenes) {
            if (!(sc.overrides.sky || sc.overrides.lighting || sc.overrides.fog))
                continue;
            AmbiencePreset a = fromSettings(resolvedSettings(out, sc), uniqueName(sc.name));
            sc.ambiencePreset = a.name;
            sc.overrides.sky = sc.overrides.lighting = sc.overrides.fog = false;
            out.ambiencePresets.push_back(std::move(a));
        }
        out.defaultAmbience = 0;
    }
    if (out.defaultAmbience < -1 ||
        out.defaultAmbience >= (int)out.ambiencePresets.size())
        out.defaultAmbience = -1;

    readMenusSection(root, out);

    loadHeights(out);
    ensureHeightmap(out);
    loadSplat(out);  // reads <scene>.splat sidecars + reconciles with the layers

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
        if (const auto* v = ed->find("gizmoSpace"))
            out.gizmoSpace = (int)v->numberOr(0);
        if (const auto* v = ed->find("viewMode")) out.viewMode = (int)v->numberOr(0);
        // Legacy fields: emulatorPath / ps2LinkIp are now machine-global
        // (editor.ini). Still read so the editor can migrate an older project's
        // values into the global config on first open (see App::attachProject);
        // no longer written back out.
        if (const auto* v = ed->find("emulatorPath")) out.emulatorPath = v->stringOr("");
        if (const auto* v = ed->find("ps2LinkIp")) out.ps2LinkIp = v->stringOr("");
    }
    // Window layouts. New format: a "layouts" array + "activeLayout" index.
    // Legacy format: a single "layout" dump - migrate it into the built-in set
    // so older projects gain Director/Material while keeping their arrangement.
    if (const auto* layouts = root.find("layouts");
        layouts && layouts->type == json::Value::Type::Array) {
        for (const auto& jl : layouts->arr) {
            WindowLayout L;
            if (const auto* v = jl.find("name")) L.name = v->stringOr("");
            if (const auto* v = jl.find("recipe")) L.recipe = (int)v->numberOr(-1);
            if (const auto* v = jl.find("ini")) L.ini = v->stringOr("");
            if (const auto* v = jl.find("open");
                v && v->type == json::Value::Type::Array)
                for (const auto& jo : v->arr)
                    if (jo.type == json::Value::Type::String) L.openWindows.push_back(jo.str);
            if (!L.name.empty()) out.windowLayouts.push_back(std::move(L));
        }
        if (const auto* v = root.find("activeLayout"))
            out.activeLayout = (int)v->numberOr(0);
    } else {
        seedBuiltinLayouts(out);
        if (const auto* v = root.find("layout")) {
            const std::string legacy = v->stringOr("");
            if (!legacy.empty()) out.windowLayouts[0].ini = legacy;  // keep old arrangement
        }
    }
    // A project must always have at least one layout, and activeLayout must be
    // in range (a hand-edited or corrupt file could break either).
    if (out.windowLayouts.empty()) seedBuiltinLayouts(out);
    if (out.activeLayout < 0 || out.activeLayout >= (int)out.windowLayouts.size())
        out.activeLayout = 0;

    // Same contract as the layouts: fonts[0] is the fallback every empty font
    // reference resolves to, so the list must never be empty.
    if (out.fonts.empty()) out.fonts.push_back(GameFont{});
    migrateFontRefs(out);

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
            if (!sc.layers.empty()) {
                json << ", \"layers\": ";
                writeLayersArray(json, sc.layers);
            }
            if (!sc.terrainLayers.empty()) {
                json << ", \"terrainLayers\": ";
                writeTerrainLayersArray(json, sc.terrainLayers);
            }
            if (sc.terrainBaseStochastic)
                json << ", \"terrainBaseStochastic\": true";
            if (sc.terrainTintVariation > 0.0f)
                json << ", \"terrainTintVariation\": "
                     << fmtFloat(sc.terrainTintVariation)
                     << ", \"terrainTintScale\": " << fmtFloat(sc.terrainTintScale);
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
                if (const auto* ls = js.find("layers")) readLayersArray(*ls, sc.layers);
                if (const auto* tl = js.find("terrainLayers"))
                    readTerrainLayersArray(*tl, sc.terrainLayers);
                if (const auto* v = js.find("terrainBaseStochastic"))
                    sc.terrainBaseStochastic = v->boolOr(false);
                if (const auto* v = js.find("terrainTintVariation"))
                    sc.terrainTintVariation = (float)v->numberOr(0.0);
                if (const auto* v = js.find("terrainTintScale")) {
                    sc.terrainTintScale = (float)v->numberOr(24.0);
                    if (sc.terrainTintScale < 1.0f) sc.terrainTintScale = 1.0f;
                }
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

// --- Live Link hashing (see docs/live-link.md) -------------------------------
// FNV-1a 64 primitives (same recipe as App::updateProjectedDecals' signature).
namespace {
constexpr uint64_t kFnvSeed = 1469598103934665603ull;
constexpr uint64_t kFnvPrime = 1099511628211ull;
inline void fnvMix(uint64_t& h, uint64_t v) { h = (h ^ v) * kFnvPrime; }
inline void fnvMixS(uint64_t& h, const std::string& s) {
    for (unsigned char c : s) fnvMix(h, c);
    fnvMix(h, 0xFF);  // terminator so {"ab",""} != {"a","b"}
}
inline void fnvMixF(uint64_t& h, float f) {
    uint32_t b;
    std::memcpy(&b, &f, sizeof(b));
    fnvMix(h, b);
}
inline void fnvMix3(uint64_t& h, const float* v) {
    fnvMixF(h, v[0]), fnvMixF(h, v[1]), fnvMixF(h, v[2]);
}
}  // namespace

uint64_t liveLinkIdHash(const SceneObject& o) {
    uint64_t h = kFnvSeed;
    fnvMixS(h, o.id.empty() ? o.name : o.id);
    return h;
}

uint64_t liveLinkRecipeHash(const SceneObject& o) {
    // Everything a live patch CANNOT change and a spawned clone copies from
    // its template - i.e. all of SceneObject except identity (id/name), the
    // live-patched transform + color, and the per-object logic (flow graph /
    // scripts are compiled per authored object; liveLinkCanSpawnLive refuses
    // objects that carry any). Two objects with equal recipes are
    // interchangeable as spawn templates.
    uint64_t h = kFnvSeed;
    fnvMix(h, (uint64_t)o.type);
    fnvMix(h, (o.physics ? 1 : 0) | (o.usable ? 2 : 0) | (o.saveState ? 4 : 0) |
                  (o.pickable ? 32 : 0) | (o.pickThrow ? 64 : 0) |
                  (o.decalProject ? 8 : 0));
    fnvMix(h, (uint64_t)o.collisionMode);
    fnvMixS(h, o.layer);
    fnvMix(h, (uint64_t)o.primDetail);
    fnvMixF(h, o.drawDistance);
    // Cast shadow feeds the build-time AO bake (occluder tables + textures);
    // a live edit of it cannot show without a rebuild.
    fnvMix(h, o.castShadow ? 1 : 0);
    // Physics material: baked into SCENE_OBJECTS, never live-patched (the
    // snapshot record carries only transform + color), and copied wholesale
    // by a spawned clone. Only meaningful while `physics` is on - the runtime
    // reads none of it otherwise - so stale values on a non-physics object
    // must not force a rebuild.
    if (o.physics) {
        fnvMixF(h, o.physMass), fnvMixF(h, o.physBounce);
        fnvMixF(h, o.physFriction);
        fnvMix(h, o.physTumble ? 1 : 0);
        fnvMixF(h, o.physSleep);
    }
    fnvMixS(h, o.modelPath);
    fnvMixS(h, o.materialPath);
    // Player entity tunables (markers in the world, but baked per scene).
    fnvMix(h, (uint64_t)o.playerMode);
    fnvMixF(h, o.playerWalkSpeed), fnvMixF(h, o.playerLookSpeed);
    fnvMixF(h, o.playerEyeHeight), fnvMixF(h, o.playerJumpSpeed);
    fnvMix(h, o.playerCanJump ? 1 : 0);
    fnvMixS(h, o.playerIdleClip), fnvMixS(h, o.playerWalkClip);
    fnvMixS(h, o.playerRunClip), fnvMixS(h, o.playerJumpClip);
    fnvMixF(h, o.playerRunThreshold);
    fnvMixF(h, o.playerCamDist), fnvMixF(h, o.playerCamHeight);
    fnvMixF(h, o.playerCamShoulder), fnvMixF(h, o.playerTurnRate);
    fnvMix(h, (uint64_t)o.playerCamStyle);
    fnvMixF(h, o.playerCamPitch), fnvMixF(h, o.playerCamYaw);
    fnvMix(h, o.playerCamYawRotate ? 1 : 0);
    fnvMix(h, o.flashlightEnabled ? 1 : 0);
    fnvMix3(h, o.flashlightColor);
    fnvMixF(h, o.flashlightRange), fnvMixF(h, o.flashlightAngle);
    fnvMixS(h, o.flashlightToggleButton);
    fnvMix(h, (uint64_t)o.emitterKind);
    fnvMix(h, (uint64_t)o.emitterCount);
    fnvMixF(h, o.emitterSize);
    fnvMix(h, (o.emitterEnabled ? 1 : 0) | (o.emitterFollowPlayer ? 2 : 0) |
                  (o.emitterDieOnGround ? 4 : 0));
    fnvMixF(h, o.emitterSpeed), fnvMixF(h, o.emitterSpread);
    fnvMixF(h, o.emitterGravity), fnvMixF(h, o.emitterWeight);
    fnvMixF(h, o.emitterLife), fnvMixF(h, o.emitterGrow);
    fnvMixF(h, o.emitterOpacity);
    fnvMixS(h, o.soundPath);
    fnvMix(h, (o.soundAuto ? 1 : 0) | (o.soundOnPlayer ? 2 : 0));
    fnvMixF(h, o.soundRange), fnvMixF(h, o.soundInterval);
    fnvMixF(h, o.cameraFov);
    // Texture feeds bake into side tables (CAM_FEEDS / OBJECT_FEEDS).
    fnvMix(h, (o.camFeed ? 1 : 0) | (o.camFeedTerrain ? 2 : 0));
    for (const auto& n : o.camFeedObjects) fnvMixS(h, n);
    fnvMixS(h, o.textureFeed);
    fnvMixS(h, o.animClip);
    fnvMix(h, (o.animAutoplay ? 1 : 0) | (o.animLoop ? 2 : 0));
    fnvMixF(h, o.animSpeed);
    fnvMixF(h, o.animLodOverride), fnvMixF(h, o.meshLodOverride);
    fnvMixF(h, o.modelYawOffset);
    // Mirror parameters live in a baked side table (MIRRORS/MIRROR_TARGETS).
    for (const auto& n : o.mirrorObjects) fnvMixS(h, n);
    fnvMix(h, (o.mirrorReflectPlayer ? 1 : 0) | (o.mirrorRaytraced ? 2 : 0));
    fnvMix(h, (uint64_t)o.mirrorRtSize);
    fnvMixF(h, o.mirrorOpacity);
    // Portal parameters live in a baked side table (PORTALS/PORTAL_VIEW_OBJECTS).
    fnvMixS(h, o.portalTarget);
    for (const auto& n : o.portalObjects) fnvMixS(h, n);
    fnvMix(h, (o.portalShowTerrain ? 1 : 0) | (o.portalTeleportObjects ? 2 : 0) |
                  (o.portalViewAll ? 4 : 0));
    // Build-time-baked transforms: a projected decal's transform IS the
    // projector, a point light's pose/color/falloff is baked into nearby
    // vertex colors. Folding them into the recipe makes any live edit of
    // these objects read as "rebuild needed" instead of silently no-op.
    if (o.type == PrimitiveType::Decal && o.decalProject) {
        fnvMix3(h, o.position), fnvMix3(h, o.rotation), fnvMix3(h, o.scale);
    }
    if (o.type == PrimitiveType::PointLight) {
        fnvMix3(h, o.position), fnvMix3(h, o.color);
        fnvMixF(h, o.lightBright), fnvMixF(h, o.lightRadius);
    }
    return h;
}

bool liveLinkCanSpawnLive(const SceneObject& o) {
    // Objects a live session may instantiate through the runtime spawn pool
    // (given a matching template). The exceptions rely on build-time baking
    // or per-authored-object codegen, so a live-spawned instance would be a
    // silent dud: point lights (vertex-color bake), projecting decals (host
    // projection bake), mirrors (baked MIRRORS side table), and anything
    // carrying per-object logic (flow graphs / attached scripts are compiled
    // for authored objects only).
    if (o.type == PrimitiveType::PointLight) return false;
    if (o.type == PrimitiveType::Decal && o.decalProject) return false;
    if (o.type == PrimitiveType::Mirror) return false;
    if (o.type == PrimitiveType::Portal) return false;  // baked PORTALS side table
    if (!o.flowGraph.nodes.empty() || !o.scripts.empty()) return false;
    return true;
}

uint64_t liveLinkContextHash(const Project& p) {
    // Cross-object structure a live session cannot absorb: scene list shape
    // and the baked streaming-layer tables (indices + zones).
    uint64_t h = kFnvSeed;
    fnvMix(h, p.scenes.size());
    for (const SceneData& sc : p.scenes) {
        fnvMix(h, 0x5C);  // scene separator
        fnvMix(h, sc.layers.size());
        for (const auto& l : sc.layers) {
            fnvMixS(h, l.name);
            fnvMix(h, (l.startLoaded ? 1 : 0) | (l.autoStream ? 2 : 0));
            fnvMixF(h, l.streamX), fnvMixF(h, l.streamZ);
            fnvMixF(h, l.streamRadius);
        }
    }
    return h;
}

std::string liveLinkSigFile(const Project& p) {
    // The as-built structure record the editor checks live edits against
    // (bin/livelink.sig, stamped by the Runner at build start). Text, one
    // token pair per authored object IN BUILT ORDER - the editor derives
    // spawn-template indices from the line positions.
    std::ostringstream out;
    out << "2\n";  // format version (matches LL_VERSION in live_link.gen.cpp)
    char buf[17];
    std::snprintf(buf, sizeof(buf), "%016llx",
                  (unsigned long long)liveLinkContextHash(p));
    out << "ctx " << buf << "\n";
    for (size_t si = 0; si < p.scenes.size(); ++si) {
        out << "scene " << si << "\n";
        for (const SceneObject& o : p.scenes[si].objects) {
            std::snprintf(buf, sizeof(buf), "%016llx",
                          (unsigned long long)liveLinkIdHash(o));
            out << buf;
            std::snprintf(buf, sizeof(buf), "%016llx",
                          (unsigned long long)liveLinkRecipeHash(o));
            out << " " << buf << "\n";
        }
    }
    return out.str();
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
            f.relativePath == "src\\gen\\flow_graph.gen.cpp" ||
            f.relativePath == "src\\gen\\live_link.gen.cpp" ||
            f.relativePath == "src\\gen\\live_tex.gen.cpp" ||
            f.relativePath == "src\\gen\\object_scripts.gen.cpp" ||
            f.relativePath == "src\\gen\\screen_fx.gen.cpp" ||
            f.relativePath == "inc\\scripts\\screen_fx.gen.hpp" ||
            f.relativePath == "inc\\scripts\\sequences.gen.hpp" ||
            f.relativePath == "src\\gen\\sequences.gen.cpp" ||
            f.relativePath == "inc\\model_data.gen.hpp" ||
            f.relativePath == "inc\\hud_data.gen.hpp" ||
            f.relativePath == "inc\\font_data.gen.hpp" ||
            f.relativePath == "inc\\loading_data.gen.hpp" ||
            f.relativePath == "inc\\terrain_heights.gen.hpp" ||
            f.relativePath == "inc\\nav_data.gen.hpp" ||
            f.relativePath == "inc\\scripts\\navigation.gen.hpp" ||
            f.relativePath == "src\\gen\\navigation.gen.cpp" ||
            f.relativePath == "inc\\texture_data.gen.hpp" ||
            f.relativePath == "inc\\decal_data.gen.hpp" ||
            f.relativePath == "inc\\ao_data.gen.hpp" ||
            f.relativePath == "inc\\save_system.gen.hpp" ||
            f.relativePath == "src\\save_system.gen.cpp" ||
            f.relativePath == "inc\\menu_data.gen.hpp") {
            write = true;  // editor-owned, always in sync with project data
        } else if (f.relativePath == "src\\terrain_game.cpp" ||
                   f.relativePath == "inc\\terrain_game.hpp" ||
                   f.relativePath == "inc\\controls.hpp" ||
                   f.relativePath == "inc\\scripts\\script.hpp" ||
                   f.relativePath == "inc\\scripts\\flow_nodes.hpp") {
            // Regenerate while the ownership marker is present, or when the
            // file is byte-identical to an old template (never user-edited).
            std::ifstream existing(path, std::ios::binary);
            if (!existing) {
                write = true;
            } else {
                std::stringstream content;
                content << existing.rdbuf();
                std::string firstLine = content.str().substr(0, content.str().find('\n'));
                // Accept the current "Generated by TyraX" marker and the legacy
                // "Generated by tyra-editor" one (projects created before the
                // TyraX rebrand) so their ownable files keep syncing.
                write = firstLine.find("Generated by TyraX") != std::string::npos ||
                        firstLine.find("Generated by tyra-editor") != std::string::npos ||
                        templates::matchesLegacy(p, f.relativePath, content.str());
            }
        } else if (f.relativePath == ".vscode\\extensions.json") {
            // Static, machine-independent recommendation list: write it once so
            // existing projects pick it up on the next build, but never clobber
            // recommendations the user may have added (JSON has no room for the
            // ownership-marker line the ownable sources above use).
            std::error_code ec;
            write = !fs::exists(path, ec);
        }

        if (write) {
            if (auto err = writeFile(path, f.content); !err.empty()) return err;
        }
    }

    // Migration: generated script sources used to live in src/scripts/ next
    // to the user's own scripts - confusing in the Scripts panel, and now
    // that they are written to src/gen/ a leftover copy would be compiled
    // twice (duplicate symbols). Always-regenerated files, safe to delete.
    for (const char* stale :
         {"flow_graph.gen.cpp", "live_link.gen.cpp", "live_tex.gen.cpp",
          "object_scripts.gen.cpp", "screen_fx.gen.cpp", "sequences.gen.cpp",
          "navigation.gen.cpp"}) {
        std::error_code ec;
        fs::remove(fs::path(p.dir) / "src" / "scripts" / stale, ec);
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

    // Static models: re-bake every referenced .obj into its .tmdl (the binary
    // format the PS2 loads - docs/model-pipeline.md). Same soft-fail rule as
    // the animated bake above.
    {
        std::vector<std::string> warnings;
        for (const auto& f : templates::bakeStaticModels(p, &warnings)) {
            if (auto err = writeFile(fs::path(p.dir) / f.relativePath, f.content);
                !err.empty())
                return err;
        }
        for (const auto& w : warnings)
            printf("[model bake] %s\n", w.c_str());
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
        const fs::path pickPng = fs::path(p.dir) / "res" / "hud" / "pickup.png";
        std::error_code ec;
        if (!fs::exists(pickPng, ec)) {
            size_t n = 0;
            const unsigned char* png = templates::pickPromptPng(n);
            fs::create_directories(pickPng.parent_path(), ec);
            std::ofstream f(pickPng, std::ios::binary);
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
    // HUD glyph strip: the debug-profile overlays AND the release-build
    // video-mode confirm prompt (Set Display Mode flow node) draw from it,
    // so it ships with every build. Always rewritten: the glyph set evolves
    // (digits-only -> letters) and a stale strip renders as blank glyphs.
    {
        const fs::path fontPng = fs::path(p.dir) / "res" / "hud" / "debugfont.png";
        std::error_code ec;
        const auto& png = templates::debugFontPng();
        fs::create_directories(fontPng.parent_path(), ec);
        std::ofstream f(fontPng, std::ios::binary);
        if (f)
            f.write(reinterpret_cast<const char*>(png.data()),
                    (std::streamsize)png.size());
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
        if (!menubake::bakePanelPNG(m, p, png))
            return "Menu bake failed (no usable TTF font found in Windows\\Fonts)";
        const fs::path path =
            fs::path(p.dir) / "res" / "menus" / menubake::panelFileName(m.name);
        std::error_code ec;
        fs::create_directories(path.parent_path(), ec);
        std::ofstream f(path, std::ios::binary);
        if (!f) return "Cannot write menu panel: " + path.string();
        f.write(reinterpret_cast<const char*>(png.data()), (std::streamsize)png.size());

        // Toggle/Choice value labels ride in a second per-menu strip texture.
        if (menubake::menuHasValueEntries(m)) {
            std::vector<unsigned char> strip;
            if (!menubake::bakeValueStripPNG(m, p, strip))
                return "Menu value bake failed (no usable TTF font found)";
            const fs::path vpath = fs::path(p.dir) / "res" / "menus" /
                                   menubake::valueStripFileName(m.name);
            std::ofstream vf(vpath, std::ios::binary);
            if (!vf) return "Cannot write menu value strip: " + vpath.string();
            vf.write(reinterpret_cast<const char*>(strip.data()),
                     (std::streamsize)strip.size());
        }
    }

    // Glyph atlases for the fonts a Display Text node draws with. Only those:
    // a font used solely by static text never ships (its strings are already
    // pixels). Always rebaked - the metrics in font_data.gen.hpp are computed
    // from the same atlasLayout, and a stale sheet would misplace every glyph.
    std::vector<std::string> wantedAtlases;
    for (int fi : p.atlasFontIndices()) {
        const GameFont& gf = p.fonts[fi];
        std::vector<unsigned char> png;
        if (!menubake::bakeAtlasPNG(gf, p, png))
            return "Font atlas bake failed for \"" + gf.name +
                   "\" (no usable TTF font found)";
        const std::string fileName = menubake::atlasFileName(gf.name);
        wantedAtlases.push_back(fileName);
        const fs::path path = fs::path(p.dir) / "res" / "fonts" / fileName;
        std::error_code ec;
        fs::create_directories(path.parent_path(), ec);
        std::ofstream f(path, std::ios::binary);
        if (!f) return "Cannot write font atlas: " + path.string();
        f.write(reinterpret_cast<const char*>(png.data()), (std::streamsize)png.size());
    }
    // Prune atlases we no longer generate (font renamed/deleted, or its last
    // Display Text node removed). res/ ships, so a leftover sheet would be
    // copied into the game and onto the ISO forever - the same reason save()
    // prunes orphaned objects/<id>.json. Only ever touches our own atlas-*.png.
    {
        const fs::path fontsDir = fs::path(p.dir) / "res" / "fonts";
        std::error_code ec;
        if (fs::exists(fontsDir, ec)) {
            for (const auto& e : fs::directory_iterator(fontsDir, ec)) {
                if (!e.is_regular_file()) continue;
                const std::string fn = e.path().filename().string();
                if (fn.rfind("atlas-", 0) != 0) continue;
                bool wanted = false;
                for (const std::string& w : wantedAtlases) wanted |= (w == fn);
                if (!wanted) fs::remove(e.path(), ec);
            }
        }
    }

    // HUD texts: baked text sprites, always rebaked (derived from project
    // data like the menu panels).
    for (const HudText& t : p.hudTexts) {
        std::vector<unsigned char> png;
        if (!menubake::bakeTextPNG(t, p, png))
            return "HUD text bake failed (no usable TTF font found)";
        const fs::path path =
            fs::path(p.dir) / "res" / "hud" / menubake::textFileName(t.name);
        std::error_code ec;
        fs::create_directories(path.parent_path(), ec);
        std::ofstream f(path, std::ios::binary);
        if (!f) return "Cannot write HUD text sprite: " + path.string();
        f.write(reinterpret_cast<const char*>(png.data()), (std::streamsize)png.size());
    }

    // Loading-screen texts: baked like HUD texts, under screen-index-mangled
    // names ("ls-<i>-<name>") so they never collide with HUD text sprites.
    // Codegen (loadingDataHeader) applies the identical mangle.
    for (size_t si = 0; si < p.loadingScreens.size(); ++si) {
        for (const HudText& t : p.loadingScreens[si].texts) {
            HudText copy = t;
            copy.name = "ls-" + std::to_string(si) + "-" + t.name;
            std::vector<unsigned char> png;
            if (!menubake::bakeTextPNG(copy, p, png))
                return "Loading text bake failed (no usable TTF font found)";
            const fs::path path =
                fs::path(p.dir) / "res" / "hud" / menubake::textFileName(copy.name);
            std::error_code ec;
            fs::create_directories(path.parent_path(), ec);
            std::ofstream f(path, std::ios::binary);
            if (!f) return "Cannot write loading text sprite: " + path.string();
            f.write(reinterpret_cast<const char*>(png.data()),
                    (std::streamsize)png.size());
        }
    }
    return "";
}

}  // namespace project
