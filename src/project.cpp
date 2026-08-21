#include "project.hpp"
#include "vugen.hpp"  // vugen::classTitle - VU material-class labels

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <random>
#include <set>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

#include "history.hpp"
#include "json.hpp"
#include "menubake.hpp"
#include "menulayout.hpp"
#include "menustyle.hpp"
#include "objparser.hpp"
#include "platform.hpp"
#include "savebake.hpp"
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
        case PrimitiveType::Area: return "area";
        case PrimitiveType::Scatter: return "scatter";
        case PrimitiveType::Scroller: return "scroller";
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
    if (s == "area") return PrimitiveType::Area;
    if (s == "scatter") return PrimitiveType::Scatter;
    if (s == "scroller") return PrimitiveType::Scroller;
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
    // A menu with a "Rebind key" row draws the current binding name as runtime
    // text (the string is only known while the game runs - see
    // docs/input-bindings.md), so that menu's font needs an atlas too.
    for (const GameMenu& m : menus)
        for (const MenuEntry& e : m.entries)
            if (e.action == MenuEntry::RebindKey) {
                want(m.font);
                break;
            }
    // The save menu ALWAYS draws runtime text - its rows are "SLOT n" and the
    // page counter, neither of which can be baked when the slot count is free.
    // Without this its atlas would be missing and the rows would be blank.
    for (const GameMenu& m : menus)
        if (m.saveMenu) want(m.font);
    std::sort(out.begin(), out.end());
    return out;
}

namespace project {

// --- display modes -----------------------------------------------------------
// The host twin of Tyra::RendererSettings::updateGeometry (engine
// inc/renderer/renderer_settings.hpp). Keep the two in step.
const std::vector<DisplayModeInfo>& displayModes() {
    static const std::vector<DisplayModeInfo> v = {
        {"interlaced", "480i / 576i (interlaced)", 512, 448, false},
        {"interlaced-field", "480i / 576i, field rendering", 512, 448, true},
        {"progressive", "480p (progressive)", 448, 448, false},
        {"1080i", "1080i (hi-def)", 448, 540, false},
        {"pal576", "576i full PAL", 512, 512, false},
    };
    return v;
}

const DisplayModeInfo& displayModeInfo(const std::string& key) {
    for (const DisplayModeInfo& d : displayModes())
        if (key == d.key) return d;
    return displayModes()[0];
}

TripleBufferFit tripleBufferingFit(const Project& p, const ProjectSettings& s,
                                   const std::string& modeKey) {
    // GS VRAM in words (1 word = 4 bytes); buffers are page aligned (2048).
    const int kVramWords = 1048576;
    const int kPage = 2048;
    // The reserve the engine checks against: everything RendererCore::init
    // still allocates after gs.init (post fx, env map + z, camera feed + z,
    // the projected-shadow slots) plus a texture floor. Keep these two equal to
    // kThirdBufferReserveWords / kThirdBufferMinTextureWords in the engine.
    // (Deliberately NOT discounted when a project reserves neither the env
    // map nor the camera feed - 128 KB of that reserve is then unused, so
    // this refuses a third buffer that would in fact fit. Erring toward two
    // buffers is the safe direction, and the engine's own constants would
    // have to move with it.)
    const int kNeed = 98304 + 65536;
    auto pageUp = [&](int w) { return ((w + kPage - 1) / kPage) * kPage; };

    // THE MODE IS A PARAMETER, because the boot mode is not the only mode the
    // game runs in. `supportedModes` declares the scan modes a player can
    // switch into, RendererCore::setDisplayOutput re-runs the whole VRAM layout
    // on each switch, and allocateVramBuffers therefore asks this question
    // again with a different framebuffer size - so a third buffer that fits at
    // 512x224 does not fit at 512x512 and the engine silently drops back to
    // two. The boot mode is just the default answer (the one-argument overload).
    const DisplayModeInfo& d = displayModeInfo(modeKey);
    const int w = d.bufW;
    const int h = d.halfHeight ? d.logicalH / 2 : d.logicalH;
    // A display buffer is one word per pixel at PSMCT32 and half that at
    // PSMCT16 (docs/gs-vram.md) - which is what most often decides this
    // question, since 16-bit colour makes a third buffer cost what two used
    // to. Keep in step with RendererCoreGS::allocateVramBuffers.
    const bool halfDepth = s.colorDepth == "16bit";
    const int bufferWords = pageUp(halfDepth ? (w * h + 1) / 2 : w * h);

    // THE UPSCALER IS A PER-SCENE SETTING, so `s.blssEnabled` is the project
    // DEFAULT and almost never the question (docs/neural-upscaler.md, "Per
    // scene") - blssUse is. `s` is the staged settings the Preferences modal
    // has not committed yet, which is exactly what the two-argument overload
    // exists for.
    const BlssUse u = blssUse(p, s);
    const int sx = s.blssScale == 0 ? 2 : 1;
    const int sy = 2;

    // The z buffer follows the RASTER, which the neural upscaler shrinks -
    // which is exactly what can make room for the third buffer. But a MIXED
    // project pins z at the FULL display raster (RendererCoreGS::setZRasterScale
    // via configure()'s eighth argument), so it gets none of that back; this
    // used to read the project default alone and promised a third buffer such
    // a project cannot have.
    const bool zShrinks = u.any && !u.mixed;
    const int zWords = pageUp(zShrinks ? (w / sx) * (h / sy) : w * h);

    // The low-res colour target, which exists whenever the upscaler is on
    // ANYWHERE (configure() is given the widest configuration the run takes).
    // It is allocated after the engine's own headroom check, so both sides have
    // to subtract it by hand - keep this equal to the blssWords block in
    // RendererCoreGS::allocateVramBuffers.
    int lowWords = 0;
    if (u.any) {
        const int lowBufW = -64 & ((w / sx) + 63);  // 64-aligned, as BLSS sizes it
        const int lowPixels = lowBufW * (h / sy);
        lowWords = pageUp(halfDepth ? (lowPixels + 1) / 2 : lowPixels);
    }

    TripleBufferFit f;
    f.bufferWords = bufferWords;
    f.needWords = kNeed;
    f.leftWords =
        kVramWords - (2 * bufferWords + zWords + lowWords) - bufferWords;
    f.fits = f.leftWords >= kNeed;
    f.mode = d.key;
    return f;
}

TripleBufferFit tripleBufferingFit(const Project& p, const ProjectSettings& s) {
    return tripleBufferingFit(p, s, bootDisplayMode(s));
}

TripleBufferModes tripleBufferingModes(const Project& p,
                                       const ProjectSettings& s) {
    TripleBufferModes m;
    m.boot = bootDisplayMode(s);
    // The boot mode first, then everything `supportedModes` declares. The two
    // are separate questions and a project can declare a set that does not
    // contain what it boots in, so both go in - deduped, in table order after
    // the boot entry.
    std::vector<std::string> keys{m.boot};
    for (const std::string& k : supportedDisplayModes(s)) {
        bool seen = false;
        for (const std::string& h : keys) seen |= (h == k);
        if (!seen) keys.push_back(k);
    }
    for (const std::string& k : keys) {
        const TripleBufferFit f = tripleBufferingFit(p, s, k);
        if (k == m.boot) m.bootFits = f.fits;
        (f.fits ? m.fitting : m.notFitting).push_back(f);
    }
    for (const DisplayModeInfo& d : displayModes()) {
        bool asked = false;
        for (const std::string& h : keys) asked |= (h == d.key);
        if (asked) continue;
        const TripleBufferFit f = tripleBufferingFit(p, s, d.key);
        if (f.fits) m.roomElsewhere.push_back(f);
    }
    return m;
}

std::string bootDisplayMode(const ProjectSettings& s) {
    if (s.displayMode == "interlaced" && s.palFullHeight && s.videoSystem != "ntsc")
        return "pal576";
    return s.displayMode;
}

std::vector<std::string> previewDisplayModes(const ProjectSettings& s) {
    const std::string boot = bootDisplayMode(s);
    std::vector<std::string> out{boot};
    const std::vector<std::string> declared = supportedDisplayModes(s);
    const bool any = !s.supportedModes.empty();
    for (const DisplayModeInfo& d : displayModes()) {
        if (d.key == boot) continue;
        if (any) {
            for (const std::string& k : declared)
                if (k == d.key) out.push_back(d.key);
        } else {
            out.push_back(d.key);
        }
    }
    return out;
}

std::vector<std::string> supportedDisplayModes(const ProjectSettings& s) {
    std::vector<std::string> out;
    for (const DisplayModeInfo& d : displayModes())
        for (const std::string& k : s.supportedModes)
            if (k == d.key) out.push_back(d.key);
    if (out.empty()) out.push_back(bootDisplayMode(s));
    return out;
}

static std::string writeFile(const fs::path& path, const std::string& content) {
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    // A byte-identical rewrite is skipped, and that is what makes an
    // incremental build incremental. refreshGenerated() runs at the start of
    // EVERY build and rewrites every editor-owned file unconditionally, so
    // their mtimes moved every time; the Runner's rsync propagates mtimes into
    // the container even when it has no bytes to send, and `make` then found
    // every source newer than its object and recompiled the whole game. In
    // other words: before this, no build was ever incremental (measured on
    // examples/showcase - a second build with nothing changed still spent 70 s
    // recompiling all 15 translation units).
    bool identical = false;
    if (std::ifstream existing(path, std::ios::binary); existing) {
        std::stringstream current;
        current << existing.rdbuf();
        identical = current.str() == content;
    }
    if (!identical) {
        std::ofstream f(path, std::ios::binary);
        if (!f) return "Cannot write file: " + path.string();
        f << content;
        f.close();
    }
    // A generated shell script has to be runnable, and the file mode is not
    // something the templates can express. Harmless on Windows, where the
    // execute bits are not part of the permission model.
    if (path.extension() == ".sh")
        fs::permissions(path,
                        fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec,
                        fs::perm_options::add, ec);
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

// "flowGraph": { nodes, links, nextId } - one per scene object, stored inside
// that object's objects/<id>.json body.
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
                (l.kind == FlowLinkNum ? ", \"number\": true" : "") +
                (l.toPin ? ", \"pin\": " + std::to_string(l.toPin) : "") +
                (l.fromPin ? ", \"fpin\": " + std::to_string(l.fromPin) : "") +
                " }";
    }
    return json + "] }";
}

// Public wrapper (project.hpp) - the whole file already sits in namespace
// project, flowGraphJson above is the private serializer.
std::string flowGraphToJson(const FlowGraph& fg) { return flowGraphJson(fg); }

// 64-bit values that must survive JSON round-tripping exactly (point-override
// keys, bake hashes) travel as 16 hex digits: a JSON number would silently
// lose the low bits past 2^53.
static std::string hex64(uint64_t v) {
    char buf[17];
    std::snprintf(buf, sizeof(buf), "%016llx", (unsigned long long)v);
    return buf;
}

static uint64_t parseHex64(const std::string& s) {
    uint64_t v = 0;
    for (char c : s) {
        int d;
        if (c >= '0' && c <= '9')
            d = c - '0';
        else if (c >= 'a' && c <= 'f')
            d = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F')
            d = c - 'A' + 10;
        else
            continue;
        v = (v << 4) | (uint64_t)d;
    }
    return v;
}

// "procGraph": { seed, nextId, baked, nodes, links, overrides } - the
// procedural graph of a Scatter object. Parameters are written as the maps
// they are, so a node only carries what was actually set and a new parameter
// on an existing node type reads as its registry default in old projects.
std::string procGraphJson(const ProcGraph& g) {
    std::string json = "{ \"seed\": " + std::to_string((long long)g.seed) +
                       ", \"nextId\": " + std::to_string(g.nextId);
    if (g.bakedHash) json += ", \"baked\": \"" + hex64(g.bakedHash) + "\"";
    // Runtime mode (docs/procedural-runtime.md). Omitted while off, so every
    // project authored before it round-trips byte-identically.
    if (g.runtime) {
        json += ", \"runtime\": true";
        if (!g.runAtStart) json += ", \"runAtStart\": false";
        if (g.seedMode) json += ", \"seedMode\": " + std::to_string(g.seedMode);
    }
    json += ", \"nodes\": [";
    for (size_t i = 0; i < g.nodes.size(); ++i) {
        const ProcNode& n = g.nodes[i];
        json += std::string(i ? ", " : "") + "{ \"id\": " + std::to_string(n.id) +
                ", \"type\": \"" + n.type + "\", \"pos\": [" + fmtFloat(n.pos[0]) +
                ", " + fmtFloat(n.pos[1]) + "]";
        if (n.bypass) json += ", \"bypass\": true";
        if (!n.nums.empty()) {
            json += ", \"nums\": {";
            bool first = true;
            for (const auto& kv : n.nums) {
                json += std::string(first ? "" : ", ") + "\"" + jsonEscape(kv.first) +
                        "\": " + fmtFloat(kv.second);
                first = false;
            }
            json += "}";
        }
        if (!n.strs.empty()) {
            json += ", \"strs\": {";
            bool first = true;
            for (const auto& kv : n.strs) {
                json += std::string(first ? "" : ", ") + "\"" + jsonEscape(kv.first) +
                        "\": \"" + jsonEscape(kv.second) + "\"";
                first = false;
            }
            json += "}";
        }
        if (!n.rows.empty()) {
            json += ", \"rows\": [";
            for (size_t r = 0; r < n.rows.size(); ++r) {
                const ProcRow& row = n.rows[r];
                json += std::string(r ? ", " : "") + "{ \"s\": \"" +
                        jsonEscape(row.s) + "\", \"v\": [" + fmtFloat(row.v[0]) +
                        ", " + fmtFloat(row.v[1]) + ", " + fmtFloat(row.v[2]) +
                        ", " + fmtFloat(row.v[3]) + "] }";
            }
            json += "]";
        }
        json += " }";
    }
    json += "], \"links\": [";
    for (size_t i = 0; i < g.links.size(); ++i) {
        const ProcLink& l = g.links[i];
        json += std::string(i ? ", " : "") + "{ \"id\": " + std::to_string(l.id) +
                ", \"from\": " + std::to_string(l.fromNode) +
                ", \"fromPin\": " + std::to_string(l.fromPin) +
                ", \"to\": " + std::to_string(l.toNode) +
                ", \"toPin\": " + std::to_string(l.toPin) + " }";
    }
    json += "]";
    if (!g.overrides.empty()) {
        json += ", \"overrides\": [";
        for (size_t i = 0; i < g.overrides.size(); ++i) {
            const ProcOverride& o = g.overrides[i];
            // The key is a 64-bit hash: written as hex text because JSON
            // numbers lose the low bits past 2^53.
            json += std::string(i ? ", " : "") + "{ \"key\": \"" + hex64(o.key) + "\"";
            if (o.removed) json += ", \"removed\": true";
            if (o.offset[0] || o.offset[1] || o.offset[2])
                json += ", \"offset\": " + fmtVec3(o.offset);
            if (o.rotate[0] || o.rotate[1] || o.rotate[2])
                json += ", \"rotate\": " + fmtVec3(o.rotate);
            if (o.scale != 1.0f) json += ", \"scale\": " + fmtFloat(o.scale);
            if (o.asset >= 0) json += ", \"asset\": " + std::to_string(o.asset);
            json += " }";
        }
        json += "]";
    }
    return json + " }";
}

static void readProcGraph(const json::Value& jg, ProcGraph& g) {
    if (const auto* v = jg.find("seed")) g.seed = (uint32_t)v->numberOr(1);
    if (const auto* v = jg.find("nextId")) g.nextId = (int)v->numberOr(1);
    if (const auto* v = jg.find("baked")) g.bakedHash = parseHex64(v->stringOr(""));
    if (const auto* v = jg.find("runtime")) g.runtime = v->boolOr(false);
    if (const auto* v = jg.find("runAtStart")) g.runAtStart = v->boolOr(true);
    if (const auto* v = jg.find("seedMode")) g.seedMode = (int)v->numberOr(0);
    if (const auto* nodes = jg.find("nodes");
        nodes && nodes->type == json::Value::Type::Array) {
        for (const auto& jn : nodes->arr) {
            ProcNode n;
            if (const auto* v = jn.find("id")) n.id = (int)v->numberOr(0);
            if (const auto* v = jn.find("type")) n.type = v->stringOr("");
            if (const auto* v = jn.find("pos");
                v && v->type == json::Value::Type::Array && v->arr.size() >= 2) {
                n.pos[0] = (float)v->arr[0].numberOr(0);
                n.pos[1] = (float)v->arr[1].numberOr(0);
            }
            if (const auto* v = jn.find("bypass"))
                n.bypass = v->type == json::Value::Type::Bool && v->boolean;
            if (const auto* v = jn.find("nums");
                v && v->type == json::Value::Type::Object)
                for (const auto& kv : v->obj) n.nums[kv.first] = (float)kv.second.numberOr(0);
            if (const auto* v = jn.find("strs");
                v && v->type == json::Value::Type::Object)
                for (const auto& kv : v->obj) n.strs[kv.first] = kv.second.stringOr("");
            if (const auto* v = jn.find("rows");
                v && v->type == json::Value::Type::Array)
                for (const auto& jr : v->arr) {
                    ProcRow row;
                    if (const auto* s = jr.find("s")) row.s = s->stringOr("");
                    if (const auto* a = jr.find("v");
                        a && a->type == json::Value::Type::Array)
                        for (size_t i = 0; i < 4 && i < a->arr.size(); ++i)
                            row.v[i] = (float)a->arr[i].numberOr(0);
                    n.rows.push_back(std::move(row));
                }
            // Unknown node types are dropped (a graph from a newer editor, or
            // a retired type) - the links to them go with them below.
            if (n.id > 0 && procNodeType(n.type)) g.nodes.push_back(std::move(n));
        }
    }
    if (const auto* links = jg.find("links");
        links && links->type == json::Value::Type::Array) {
        for (const auto& jl : links->arr) {
            ProcLink l;
            if (const auto* v = jl.find("id")) l.id = (int)v->numberOr(0);
            if (const auto* v = jl.find("from")) l.fromNode = (int)v->numberOr(0);
            if (const auto* v = jl.find("fromPin")) l.fromPin = (int)v->numberOr(0);
            if (const auto* v = jl.find("to")) l.toNode = (int)v->numberOr(0);
            if (const auto* v = jl.find("toPin")) l.toPin = (int)v->numberOr(0);
            const bool ends = procgraph::node(g, l.fromNode) && procgraph::node(g, l.toNode);
            if (l.id > 0 && ends) g.links.push_back(l);
        }
    }
    if (const auto* ovr = jg.find("overrides");
        ovr && ovr->type == json::Value::Type::Array) {
        for (const auto& jo : ovr->arr) {
            ProcOverride o;
            if (const auto* v = jo.find("key")) o.key = parseHex64(v->stringOr(""));
            if (const auto* v = jo.find("removed"))
                o.removed = v->type == json::Value::Type::Bool && v->boolean;
            if (const auto* v = jo.find("offset")) readVec3(v, o.offset);
            if (const auto* v = jo.find("rotate")) readVec3(v, o.rotate);
            if (const auto* v = jo.find("scale")) o.scale = (float)v->numberOr(1.0);
            if (const auto* v = jo.find("asset")) o.asset = (int)v->numberOr(-1);
            if (o.key) g.overrides.push_back(o);
        }
    }
    // Keep nextId ahead of everything actually in the graph: a hand-edited or
    // truncated file must not hand out an id that is already taken.
    for (const ProcNode& n : g.nodes) g.nextId = std::max(g.nextId, n.id + 1);
    for (const ProcLink& l : g.links) g.nextId = std::max(g.nextId, l.id + 1);
}

bool parseProcGraph(const std::string& body, ProcGraph& out) {
    json::Value root;
    if (!json::parse(body, root) || root.type != json::Value::Type::Object)
        return false;
    out = ProcGraph{};
    readProcGraph(root, out);
    return true;
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
            // "number", not "num": a node's numeric PARAMS already serialize as
            // a "num" array, and one key meaning two things in one file invites
            // exactly the bug it looks like.
            if (const auto* v = jl.find("number");
                v && v->type == json::Value::Type::Bool && v->boolean)
                l.kind = FlowLinkNum;
            if (const auto* v = jl.find("pin")) l.toPin = (int)v->numberOr(0);
            // "fpin" = which exec OUTPUT of the source the link leaves (Branch's
            // true/false, Sequence's 1..4). Omitted at 0, the first output.
            if (const auto* v = jl.find("fpin")) l.fromPin = (int)v->numberOr(0);
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
        // cylinders only, and only when ON - a project that never asked for
        // axial rings round-trips byte-identically to how it always did
        ((o.type == PrimitiveType::Cylinder && o.primRings)
             ? ", \"rings\": true"
             : "") +
        // 0 = unlimited (default) stays implicit
        (o.drawDistance > 0.0f
             ? ", \"drawDistance\": " + fmtFloat(o.drawDistance)
             : "") +
        // rendered into the dynamic env map; default (false) stays implicit
        (o.reflected ? std::string(", \"reflected\": true") : "") +
        (!o.castShadow ? std::string(", \"castShadow\": false") : "") +
        (!o.bakedLighting ? std::string(", \"bakedLighting\": false") : "") +
        (o.dynamicLighting ? std::string(", \"dynamicLighting\": true") : "") +
        (o.prelit ? std::string(", \"prelit\": true") : "") +
        // Pre-lit bookkeeping, each written only when it says something (an
        // object that never met the baker resaves byte for byte). prelitSig is
        // a hex STRING - 64 bits do not survive a JSON number. prelitSource is
        // omitted at "" because "" is also what a missing key reads as, and
        // that IS the value it would have carried: the model's own mtllib.
        (o.prelitWanted ? std::string(", \"prelitWanted\": true") : "") +
        (o.prelitSig ? ", \"prelitSig\": \"" + hex64(o.prelitSig) + "\"" : "") +
        (o.prelitSource.empty()
             ? ""
             : ", \"prelitSource\": \"" + jsonEscape(o.prelitSource) + "\"") +
        // projected (live) silhouette shadow; default (false) stays implicit
        (o.projShadow ? std::string(", \"projShadow\": true") : "") +
        (o.modelPath.empty() ? "" : ", \"model\": \"" + jsonEscape(o.modelPath) + "\"") +
        (o.materialPath.empty() ? ""
                                : ", \"material\": \"" + jsonEscape(o.materialPath) + "\"") +
        // decal projection: off (flat quad) stays implicit
        (o.decalProject ? ", \"decalProject\": true" : "");
    if (o.type == PrimitiveType::Player) {
        const char* modeName = o.playerMode == 1   ? "noclip"
                               : o.playerMode == 2 ? "thirdperson"
                                                   : "walk";
        // The two extra speed tiers are written only when SET (0 = inherit),
        // so a project that never touched them resaves byte for byte.
        const std::string speedTiers =
            (o.playerRunSpeed > 0.0f
                 ? ", \"runSpeed\": " + fmtFloat(o.playerRunSpeed)
                 : std::string()) +
            (o.playerSprintSpeed > 0.0f
                 ? ", \"sprintSpeed\": " + fmtFloat(o.playerSprintSpeed)
                 : std::string());
        json += ", \"player\": { \"mode\": \"" + std::string(modeName) +
                "\", \"walkSpeed\": " + fmtFloat(o.playerWalkSpeed) +
                speedTiers +
                ", \"lookSpeed\": " + fmtFloat(o.playerLookSpeed) +
                ", \"eyeHeight\": " + fmtFloat(o.playerEyeHeight) +
                ", \"jumpSpeed\": " + fmtFloat(o.playerJumpSpeed) +
                ", \"canJump\": " + (o.playerCanJump ? "true" : "false") +
                // Third-person avatar: locomotion clip mapping + camera boom.
                ", \"thirdPerson\": { \"idleClip\": \"" + jsonEscape(o.playerIdleClip) +
                "\", \"walkClip\": \"" + jsonEscape(o.playerWalkClip) +
                "\", \"runClip\": \"" + jsonEscape(o.playerRunClip) +
                // Written only when set, so a project that never picked a
                // sprint clip resaves byte for byte.
                (o.playerSprintClip.empty()
                     ? std::string()
                     : "\", \"sprintClip\": \"" +
                           jsonEscape(o.playerSprintClip)) +
                "\", \"jumpClip\": \"" + jsonEscape(o.playerJumpClip) +
                "\", \"backClip\": \"" + jsonEscape(o.playerBackClip) +
                "\", \"strafeLeftClip\": \"" + jsonEscape(o.playerStrafeLeftClip) +
                "\", \"strafeRightClip\": \"" + jsonEscape(o.playerStrafeRightClip) +
                "\", \"faceCamera\": " + (o.playerFaceCamera ? "true" : "false") +
                ", \"runThreshold\": " + fmtFloat(o.playerRunThreshold) +
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
                jsonEscape(o.flashlightToggleButton) + "\"" +
                (o.flashlightTexture.empty()
                     ? ""
                     : ", \"texture\": \"" + jsonEscape(o.flashlightTexture) +
                           "\"") +
                " }" + " }";
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
        // Opacity is written for the kinds whose codegen READS it: fog
        // (alpha = emitOpacity * 60) and custom (* 128). The other four have
        // hardcoded peak alphas (fire 90, smoke 40, sparks 110, rain 70), so
        // the field means nothing there and writing it would add a key to
        // every emitter in every project for no effect. Fog used to have no
        // line of its own here and fell past the custom-only block below, so
        // the slider the inspector offers it was lost on every save.
        if (k == 2) json += ", \"opacity\": " + fmtFloat(o.emitterOpacity);
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
                ", \"onPlayer\": " + (o.soundOnPlayer ? "true" : "false") +
                ", \"reverb\": " + (o.soundReverb ? "true" : "false") +
                (o.soundPriority != 0
                     ? ", \"priority\": " + std::to_string(o.soundPriority)
                     : std::string()) +
                " }";
    }
    if (o.type == PrimitiveType::PointLight) {
        json += ", \"light\": { \"brightness\": " + fmtFloat(o.lightBright) +
                ", \"radius\": " + fmtFloat(o.lightRadius) +
                ", \"dynamic\": " + (o.lightDynamic ? "true" : "false") +
                ", \"flicker\": " + fmtFloat(o.lightFlicker) +
                (o.lightSpot ? ", \"spot\": true, \"spotAngle\": " +
                                   fmtFloat(o.lightSpotAngle)
                             : std::string()) +
                ", \"beam\": " + std::to_string(o.lightBeam) + " }";
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
    // Catch area (Mirror / Portal / feed Camera); omitted when unset.
    if (!o.catchArea.empty()) {
        json += ", \"catchArea\": \"" + jsonEscape(o.catchArea) + "\"";
        if (o.catchAreaLive) json += ", \"catchAreaLive\": true";
    }
    // Reverb zone (Area only); omitted entirely unless the box is one, so an
    // ordinary area's file does not change shape.
    if (o.type == PrimitiveType::Area && o.reverbZone) {
        json += ", \"reverb\": { \"preset\": " + std::to_string(o.reverbPreset) +
                ", \"amount\": " + fmtFloat(o.reverbAmount) +
                ", \"delay\": " + std::to_string(o.reverbDelay) +
                ", \"feedback\": " + std::to_string(o.reverbFeedback) +
                ", \"priority\": " + std::to_string(o.reverbPriority) + " }";
    }
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
    if (o.type == PrimitiveType::Scroller) {
        json += ", \"scroller\": { \"speed\": " + fmtFloat(o.scrollSpeed) +
                ", \"ahead\": " + fmtFloat(o.scrollAhead) +
                ", \"behind\": " + fmtFloat(o.scrollBehind) +
                ", \"autostart\": " + (o.scrollAutostart ? "true" : "false") +
                ", \"maxClones\": " + std::to_string(o.scrollMaxClones) +
                ", \"overlap\": " + fmtFloat(o.scrollOverlap) +
                ", \"varySeed\": " + std::to_string(o.scrollVarySeed) +
                ", \"segments\": [";
        for (size_t i = 0; i < o.scrollSegments.size(); ++i) {
            const ScrollSegment& s = o.scrollSegments[i];
            json += (i ? ", " : "") + std::string("{ \"name\": \"") + s.name +
                    "\", \"length\": " + fmtFloat(s.length) + ", \"objects\": [";
            for (size_t k = 0; k < s.objects.size(); ++k) {
                const ScrollMember& m = s.objects[k];
                json += k ? ", " : "";
                // A member that varies nothing stays a bare name, so belts
                // authored before per-cell variation round-trip unchanged.
                if (scrollMemberIsPlain(m)) {
                    json += "\"" + m.name + "\"";
                    continue;
                }
                json += "{ \"n\": \"" + m.name + "\"";
                if (m.chance < 1.0f) json += ", \"chance\": " + fmtFloat(m.chance);
                if (m.variant != 0) json += ", \"variant\": " + std::to_string(m.variant);
                if (m.yawVary != 0.0f) json += ", \"yaw\": " + fmtFloat(m.yawVary);
                if (m.offsetVary != 0.0f)
                    json += ", \"offset\": " + fmtFloat(m.offsetVary);
                if (m.scaleVary != 0.0f) json += ", \"scale\": " + fmtFloat(m.scaleVary);
                json += " }";
            }
            json += "] }";
        }
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
    if (!o.procGraph.empty()) json += ", \"procGraph\": " + procGraphJson(o.procGraph);
    if (!o.procSource.empty())
        json += ", \"procSource\": \"" + jsonEscape(o.procSource) + "\"";
    if (!o.prefabSource.empty())
        json += ", \"prefabSource\": \"" + jsonEscape(o.prefabSource) + "\"";
    // The mesh's numbers for the project's own VU1 program. Omitted at the
    // all-zero default, which is every object in a project that has no such
    // program - and "wants nothing" for every object in one that does.
    if (o.vuParams[0] != 0.0f || o.vuParams[1] != 0.0f ||
        o.vuParams[2] != 0.0f || o.vuParams[3] != 0.0f) {
        json += ", \"vuParams\": [";
        for (int i = 0; i < 4; ++i)
            json += (i ? ", " : "") + fmtFloat(o.vuParams[i]);
        json += "]";
    }
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
        if (layers[i].autoStream) {  // off = keys omitted (older files stay valid)
            json << ", \"autoStream\": true, \"streamX\": " << fmtFloat(layers[i].streamX)
                 << ", \"streamZ\": " << fmtFloat(layers[i].streamZ)
                 << ", \"streamRadius\": " << fmtFloat(layers[i].streamRadius);
            // Area zone (docs/areas.md); absent = the circle above.
            if (!layers[i].streamArea.empty())
                json << ", \"streamArea\": \"" << jsonEscape(layers[i].streamArea) << "\"";
        }
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
        if (const auto* v = jl.find("streamArea")) l.streamArea = v->stringOr("");
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
      << ", \"bloomThreshold\": " << fmtFloat(s.bloomThreshold)
      << ", \"bloomSpread\": " << fmtFloat(s.bloomSpread)
      << ", \"grain\": " << fmtFloat(s.grain)
      << ", \"dofAmount\": " << fmtFloat(s.dofAmount)
      << ", \"dofFocus\": " << fmtFloat(s.dofFocus)
      << ", \"dofRange\": " << fmtFloat(s.dofRange)
      << ", \"flare\": " << fmtFloat(s.flare)
      << ", \"godRays\": " << fmtFloat(s.godRays) << " }, \"fog\": { \"enabled\": "
      << (s.fogEnabled ? "true" : "false") << ", \"color\": " << fmtVec3(s.fogColor)
      << ", \"start\": " << fmtFloat(s.fogStart) << ", \"end\": " << fmtFloat(s.fogEnd)
      << " }, \"highlight\": { \"usable\": "
      << (s.highlightUsable ? "true" : "false") << ", \"distance\": "
      << fmtFloat(s.highlightDistance) << ", \"color\": " << fmtVec3(s.highlightColor)
      << ", \"width\": " << fmtFloat(s.highlightWidth) << ", \"steps\": " << s.highlightSteps
      << ", \"opacity\": " << fmtFloat(s.highlightOpacity)
      << ", \"overlay\": " << (s.highlightOverlay ? "true" : "false")
      << " }";
    // The neural upscaler's two per-scene values, and the override flag below,
    // are written ONLY when the scene actually overrides - unlike every
    // category above, which is emitted whether it is active or not. That is
    // deliberate and it is the property the feature is built on: a project
    // whose scenes all inherit produces the bytes it produced before per-scene
    // BLSS existed, so `--resave` on any existing project is a no-op and there
    // is nothing for a migration step to do (the shot plan set the precedent).
    if (sc.overrides.upscaler)
        j << ", \"blss\": { \"enabled\": " << (s.blssEnabled ? "true" : "false")
          << ", \"network\": " << (s.blssNetwork ? "true" : "false") << " }";
    j << " }";
    const SceneOverrides& o = sc.overrides;
    j << ", \"overrides\": { \"lighting\": " << (o.lighting ? "true" : "false")
      << ", \"sky\": " << (o.sky ? "true" : "false") << ", \"clipping\": "
      << (o.clipping ? "true" : "false") << ", \"terrainMat\": "
      << (o.terrainMat ? "true" : "false") << ", \"postFx\": " << (o.postFx ? "true" : "false")
      << ", \"fog\": " << (o.fog ? "true" : "false")
      << ", \"highlight\": " << (o.highlight ? "true" : "false");
    if (o.upscaler) j << ", \"upscaler\": true";
    j << " }";
    if (!sc.ambiencePreset.empty())
        j << ", \"ambiencePreset\": \"" << jsonEscape(sc.ambiencePreset) << "\"";
    if (!sc.loadingScreen.empty())
        j << ", \"loadingScreen\": \"" << jsonEscape(sc.loadingScreen) << "\"";
}

// Reads a scene's scene-visual settings + override flags: a "settings" object
// carrying every category's values, and an "overrides" object saying which of
// them this scene actually overrides (the rest inherit the project's).
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
                // clipping on VU1; "precise" = the EE clipper.
                const std::string c = v->stringOr("vu1");
                s.clipping = (c == "fast" || c == "precise") ? c : "vu1";
            }
            if (const auto* v = st->find("terrainMaterial")) s.terrainMaterial = v->stringOr("");
            if (const auto* pf = st->find("postfx")) {
                // bloom rides a whole-byte GS blend FIX, so it goes to 2x
                if (const auto* v = pf->find("bloom")) {
                    const float b = (float)v->numberOr(0.0);
                    s.bloom = b < 0.0f ? 0.0f : (b > 2.0f ? 2.0f : b);
                }
                if (const auto* v = pf->find("bloomThreshold"))
                    s.bloomThreshold = clamp01((float)v->numberOr(0.0));
                if (const auto* v = pf->find("bloomSpread"))
                    s.bloomSpread = clamp01((float)v->numberOr(0.0));
                if (const auto* v = pf->find("grain")) s.grain = clamp01((float)v->numberOr(0.0));
                if (const auto* v = pf->find("dofAmount"))
                    s.dofAmount = clamp01((float)v->numberOr(0.0));
                if (const auto* v = pf->find("dofFocus"))
                    s.dofFocus = (float)v->numberOr(s.dofFocus);
                if (const auto* v = pf->find("dofRange"))
                    s.dofRange = (float)v->numberOr(s.dofRange);
                if (const auto* v = pf->find("flare"))
                    s.flare = clamp01((float)v->numberOr(0.0));
                if (const auto* v = pf->find("godRays"))
                    s.godRays = clamp01((float)v->numberOr(0.0));
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
            // Absent on every file written before per-scene BLSS and on every
            // scene that inherits, so the defaults here are the project's own
            // - they are only read at all when "upscaler" is set below.
            if (const auto* bl = st->find("blss")) {
                if (const auto* v = bl->find("enabled")) s.blssEnabled = v->boolOr(false);
                if (const auto* v = bl->find("network")) s.blssNetwork = v->boolOr(true);
            }
        }
        sc.overrides.lighting = ov->find("lighting") ? ov->find("lighting")->boolOr(false) : false;
        sc.overrides.sky = ov->find("sky") ? ov->find("sky")->boolOr(false) : false;
        sc.overrides.clipping = ov->find("clipping") ? ov->find("clipping")->boolOr(false) : false;
        sc.overrides.terrainMat =
            ov->find("terrainMat") ? ov->find("terrainMat")->boolOr(false) : false;
        sc.overrides.postFx = ov->find("postFx") ? ov->find("postFx")->boolOr(false) : false;
        sc.overrides.fog = ov->find("fog") ? ov->find("fog")->boolOr(false) : false;
        sc.overrides.highlight =
            ov->find("highlight") ? ov->find("highlight")->boolOr(false) : false;
        sc.overrides.upscaler =
            ov->find("upscaler") ? ov->find("upscaler")->boolOr(false) : false;
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

void clampDayKey(DayKey& k) {
    k.hour = ambience::wrap24(k.hour);
    auto c01 = [](float& v) { v = v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); };
    for (int i = 0; i < 3; ++i) {
        c01(k.skyColor[i]);
        c01(k.skyTopColor[i]);
        c01(k.lightColor[i]);
        c01(k.fogColor[i]);
    }
    c01(k.ambient);
    c01(k.diffuse);
    c01(k.stars);
    if (k.brightness < 0.0f) k.brightness = 0.0f;
    if (k.brightness > 2.0f) k.brightness = 2.0f;
}

void clampDayCycle(DayCycle& c) {
    c.time = ambience::wrap24(c.time);
    c.sunAzimuth = ambience::wrap24(c.sunAzimuth / 15.0f) * 15.0f;  // 0..360
    c.moonAzimuth = ambience::wrap24(c.moonAzimuth / 15.0f) * 15.0f;
    auto clampf = [](float& v, float lo, float hi) {
        v = v < lo ? lo : (v > hi ? hi : v);
    };
    clampf(c.sunTilt, -89.0f, 89.0f);
    clampf(c.moonTilt, -89.0f, 89.0f);
    c.sunrise = ambience::wrap24(c.sunrise);
    c.sunset = ambience::wrap24(c.sunset);
    c.moonOffset = ambience::wrap24(c.moonOffset);
    clampf(c.sunSize, 0.25f, 30.0f);
    clampf(c.moonSize, 0.25f, 30.0f);
    clampf(c.moonPhase, 0.0f, 1.0f);
    clampf(c.moonOpacity, 0.0f, 1.0f);
    clampf(c.dayLength, 8.0f, 7200.0f);
    c.bakeHour = ambience::wrap24(c.bakeHour);
    clampf(c.starTwinkle, 0.0f, 1.0f);
    clampf(c.starField.magnitudeSpread, 0.0f, 1.0f);
    clampf(c.starField.milkyWay, 0.0f, 1.0f);
    clampf(c.starField.milkyWayTilt, -89.0f, 89.0f);
    clampf(c.starField.sizeScale, 0.25f, 4.0f);
    if (c.starField.count < 0) c.starField.count = 0;
    if (c.starField.count > starfield::kMaxStars)
        c.starField.count = starfield::kMaxStars;
    for (DayKey& k : c.keys) clampDayKey(k);
    sortDayKeys(c);
}

void sortDayKeys(DayCycle& c) {
    std::stable_sort(c.keys.begin(), c.keys.end(),
                     [](const DayKey& a, const DayKey& b) { return a.hour < b.hour; });
}

// --- Player speed tiers (docs/player-speeds.md) ----------------------------
// The whole "0 = inherit" chain lives here and nowhere else: the Properties
// readout, codegen's PLAYER_RUN_SPEEDS / PLAYER_SPRINT_SPEEDS tables and the
// fallback walker's terrain_config constants all resolve through these four,
// so the number the panel prints is by construction the number that ships.
float playerRunSpeed(const SceneObject& o) {
    return o.playerRunSpeed > 0.0f ? o.playerRunSpeed : o.playerWalkSpeed;
}

float playerSprintSpeed(const SceneObject& o, const ProjectSettings& st) {
    if (o.playerSprintSpeed > 0.0f) return o.playerSprintSpeed;
    // The multiplier applies to the RUN speed, which IS the walk speed unless
    // one was set - so a project that never set a run speed sprints at exactly
    // the walk x multiplier its old build did.
    const float mult = st.sprintMultiplier > 1.0f ? st.sprintMultiplier : 1.0f;
    return playerRunSpeed(o) * mult;
}

float settingsRunSpeed(const ProjectSettings& st) {
    return st.runSpeed > 0.0f ? st.runSpeed : st.walkSpeed;
}

float settingsSprintSpeed(const ProjectSettings& st) {
    const float mult = st.sprintMultiplier > 1.0f ? st.sprintMultiplier : 1.0f;
    return settingsRunSpeed(st) * mult;
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
        r.bloomThreshold = o.bloomThreshold;
        r.bloomSpread = o.bloomSpread;
        r.grain = o.grain;
        r.dofAmount = o.dofAmount;
        r.dofFocus = o.dofFocus;
        r.dofRange = o.dofRange;
        r.flare = o.flare;
        r.godRays = o.godRays;
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
    // The neural upscaler, per scene: a scene with a portal can refuse it while
    // the scene next door keeps it (docs/neural-upscaler.md, "Per scene"). Only
    // these two - the scale, the jitter, the sharpen/temporal tuning and the
    // debug view stay project-wide; SceneOverrides::upscaler says why.
    if (s.overrides.upscaler) {
        r.blssEnabled = o.blssEnabled;
        r.blssNetwork = o.blssNetwork;
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

        // Day/night cycle (docs/day-night-cycle.md). THE hook: everything
        // downstream keeps reading these same ProjectSettings fields, so the
        // time-of-day slider reaches the vertex bake, aobake, gibake and its
        // probe grid, SCENE_LIGHT_* in codegen and from there the runtime
        // projected shadows, blob shadows, lens flare and god rays - without a
        // single one of them knowing a cycle exists.
        if (a.cycle.enabled) {
            const ambience::Resolved d =
                ambience::evaluate(a.cycle, ambience::bakedHour(a.cycle));
            for (int i = 0; i < 3; ++i) {
                r.skyColor[i] = d.skyColor[i];
                r.skyTopColor[i] = d.skyTopColor[i];
                r.lightDir[i] = d.lightDir[i];
                r.lightColor[i] = d.lightColor[i];
                r.fogColor[i] = d.fogColor[i];
            }
            r.ambient = d.ambient;
            r.diffuse = d.diffuse;
            r.brightness = d.brightness;
        }
    }
    return r;
}

BlssUse blssUse(const Project& p) { return blssUse(p, p.settings); }

BlssUse blssUse(const Project& p, const ProjectSettings& defaults) {
    BlssUse u;
    bool first = true;
    bool e0 = false, n0 = false;
    for (const SceneData& sc : p.scenes) {
        // resolvedSettings() reaches these two fields through exactly this
        // branch and nothing else touches them on the way out (the ambience
        // overlay owns sky/light/fog and no more), so resolving them here
        // against the supplied defaults IS what that function would do - and it
        // is the only way to answer for settings a modal has not committed yet.
        const bool en =
            sc.overrides.upscaler ? sc.settings.blssEnabled : defaults.blssEnabled;
        const bool nw =
            sc.overrides.upscaler ? sc.settings.blssNetwork : defaults.blssNetwork;
        u.any |= en;
        u.anyNative |= !en;
        u.anyNetwork |= (en && nw);
        if (first) {
            e0 = en, n0 = nw, first = false;
        } else if (en != e0 || (en && nw != n0)) {
            u.mixed = true;
        }
    }
    // A project with no scenes at all (never on disk, but the model allows it)
    // resolves to "nothing uses it", which is what every consumer wants.
    if (!u.any) u.mixed = false;
    return u;
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
    // map_Kd is relative to the .mtl's own directory - and the result must be
    // NORMALIZED, because a material one folder over yields "materials/../
    // textures/x.png" and **the PS2 cannot walk ".."**. PCSX2 hides this
    // completely (its host: fs resolves the path through the OS) while a disc
    // has no such entry at all, so the texture silently fails to load on real
    // hardware only. texbake already copies the file to its normalized
    // location, so this is also what makes the two agree.
    if (!m.texture.empty())
        out.texture = (fs::path(matRel).parent_path() / m.texture)
                          .lexically_normal()
                          .generic_string();
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
         << (p.settings.supportedModes.empty()
                 ? ""
                 : "    \"supportedModes\": [" +
                       [&] {
                           std::string list;
                           for (size_t i = 0; i < p.settings.supportedModes.size(); ++i)
                               list += (i ? ", \"" : "\"") +
                                       p.settings.supportedModes[i] + "\"";
                           return list;
                       }() +
                       "],\n")
         << (p.settings.palFullHeight ? "    \"palFullHeight\": true,\n" : "")
         // Written only when set away from the default, so an existing
         // project's manifest does not gain two keys just by being resaved.
         << (p.settings.colorDepth == "16bit"
                 ? "    \"colorDepth\": \"16bit\",\n"
                 : "")
         << (p.settings.dither ? "" : "    \"dither\": false,\n")
         << (p.settings.tripleBuffering ? "    \"tripleBuffering\": true,\n" : "")
         << (p.settings.frameExtrapolation ? "    \"frameExtrapolation\": true,\n" : "")
         << (p.settings.frameExtrapolationPlane != 0.0f
                 ? "    \"frameExtrapolationPlane\": " +
                       fmtFloat(p.settings.frameExtrapolationPlane) + ",\n"
                 : "")
         << (p.settings.frameExtrapolationForce
                 ? "    \"frameExtrapolationForce\": true,\n"
                 : "")
         << (!p.settings.frameExtrapolationGround
                 ? "    \"frameExtrapolationGround\": false,\n"
                 : "")
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
         << "    \"showAreas\": "
         << (p.settings.showAreas ? "true" : "false") << ",\n"
         << "    \"showCollision\": "
         << (p.settings.showCollision ? "true" : "false") << ",\n"
         << "    \"liveLink\": " << (p.settings.liveLink ? "true" : "false")
         << ",\n"
         << "    \"liveDebug\": " << (p.settings.liveDebug ? "true" : "false")
         << ",\n"
         << "    \"eeCrashHandler\": "
         << (p.settings.eeCrashHandler ? "true" : "false") << ",\n"
         << "    \"liveLogic\": " << (p.settings.liveLogic ? "true" : "false")
         << ",\n"
         << "    \"timeMachine\": "
         << (p.settings.timeMachine ? "true" : "false") << ",\n"
         << "    \"remotePad\": " << (p.settings.remotePad ? "true" : "false")
         << ",\n"
         << "    \"inputRecorder\": "
         << (p.settings.inputRecorder ? "true" : "false") << ",\n"
         << "    \"keyboardMouse\": "
         << (p.settings.keyboardMouse ? "true" : "false") << ",\n"
         << "    \"keyboardMousePs2Link\": "
         << (p.settings.keyboardMousePs2Link ? "true" : "false") << ",\n"
         << "    \"disableVsync\": "
         << (p.settings.disableVsync ? "true" : "false") << ",\n"
         << "    \"clipping\": \"" << p.settings.clipping << "\",\n"
         << "    \"animLodDistance\": " << fmtFloat(p.settings.animLodDistance)
         << ",\n"
         << "    \"meshLodDistance\": " << fmtFloat(p.settings.meshLodDistance)
         << ",\n"
         << "    \"animSourceFps\": " << fmtFloat(p.settings.animSourceFps)
         << ",\n"
         << "    \"animPlayFps\": " << fmtFloat(p.settings.animPlayFps) << ",\n"
         << "    \"staticBatching\": "
         << (p.settings.staticBatching ? "true" : "false") << ",\n"
         << "    \"envProbeReflected\": "
         << (p.settings.envProbeReflected ? "true" : "false") << ",\n"
         << "    \"navCellSize\": " << fmtFloat(p.settings.navCellSize) << ",\n"
         << "    \"navMaxSlope\": " << fmtFloat(p.settings.navMaxSlope) << ",\n"
         << "    \"navAgentRadius\": " << fmtFloat(p.settings.navAgentRadius)
         << ",\n"
         << "    \"unitsPerMeter\": " << fmtFloat(p.settings.unitsPerMeter)
         << ",\n"
         << "    \"terrainDetail\": " << p.settings.terrainDetail << ",\n"
         << "    \"terrainViewDistance\": " << fmtFloat(p.settings.terrainViewDistance)
         << ",\n"
         << "    \"terrainLodDistance\": "
         << fmtFloat(p.settings.terrainLodDistance) << ",\n"
         << (p.settings.flashShadowVolumes
                 ? "    \"flashShadowVolumes\": true,\n"
                 : "")
         << "    \"skyColor\": " << fmtVec3(p.settings.skyColor) << ",\n"
         << "    \"skyTopColor\": " << fmtVec3(p.settings.skyTopColor) << ",\n"
         << "    \"skyDome\": " << (p.settings.skyDome ? "true" : "false") << ",\n"
         << "    \"zenithSize\": " << fmtFloat(p.settings.zenithSize) << ",\n"
         << "    \"eyeHeight\": " << fmtFloat(p.settings.eyeHeight) << ",\n"
         << "    \"walkSpeed\": " << fmtFloat(p.settings.walkSpeed) << ",\n"
         // Written only when set (0 = the walk speed is the top speed), so a
         // project that never used the ramp resaves unchanged.
         << (p.settings.runSpeed > 0.0f
                 ? "    \"runSpeed\": " + fmtFloat(p.settings.runSpeed) + ",\n"
                 : std::string())
         << "    \"lookSpeed\": " << fmtFloat(p.settings.lookSpeed) << ",\n"
         << "    \"sprintMultiplier\": " << fmtFloat(p.settings.sprintMultiplier)
         << ",\n"
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
         << "    \"giEnabled\": " << (p.settings.giEnabled ? "true" : "false")
         << ",\n"
         << "    \"giRays\": " << p.settings.giRays << ",\n"
         << "    \"giBounces\": " << p.settings.giBounces << ",\n"
         << "    \"giSkyLight\": " << fmtFloat(p.settings.giSkyLight) << ",\n"
         << "    \"giSunLight\": " << fmtFloat(p.settings.giSunLight) << ",\n"
         << "    \"giAmbientFloor\": " << fmtFloat(p.settings.giAmbientFloor)
         << ",\n"
         << "    \"giProbes\": " << (p.settings.giProbes ? "true" : "false")
         << ",\n"
         << "    \"giProbeSpacing\": " << fmtFloat(p.settings.giProbeSpacing)
         << ",\n"
         << "    \"giProbeHeight\": " << fmtFloat(p.settings.giProbeHeight)
         << ",\n"
         << "    \"giProbeLevels\": " << p.settings.giProbeLevels << ",\n"
         // Automatic model AO (docs/ambient-occlusion.md). Emitted only when it
         // is not the struct default, so every project saved before this
         // existed resaves byte for byte.
         << (p.settings.modelAo ? "    \"modelAo\": true,\n" : "")
         << (p.settings.modelAoStrength != 0.7f
                 ? "    \"modelAoStrength\": " +
                       fmtFloat(p.settings.modelAoStrength) + ",\n"
                 : "")
         << (p.settings.modelAoRays != 64
                 ? "    \"modelAoRays\": " +
                       std::to_string(p.settings.modelAoRays) + ",\n"
                 : "")
         << (p.settings.modelAoDist != 0.0f
                 ? "    \"modelAoDist\": " + fmtFloat(p.settings.modelAoDist) +
                       ",\n"
                 : "")
         << (p.settings.prelitAutoBake ? "    \"prelitAutoBake\": true,\n" : "")
         << (p.settings.giAutoBake ? "    \"giAutoBake\": true,\n" : "")
         << "    \"terrainMaterial\": \"" << p.settings.terrainMaterial << "\",\n"
         << "    \"bloom\": " << fmtFloat(p.settings.bloom) << ",\n"
         << "    \"bloomThreshold\": " << fmtFloat(p.settings.bloomThreshold)
         << ",\n"
         << "    \"bloomSpread\": " << fmtFloat(p.settings.bloomSpread) << ",\n"
         << "    \"grain\": " << fmtFloat(p.settings.grain) << ",\n"
         << "    \"dofAmount\": " << fmtFloat(p.settings.dofAmount) << ",\n"
         << "    \"dofFocus\": " << fmtFloat(p.settings.dofFocus) << ",\n"
         << "    \"dofRange\": " << fmtFloat(p.settings.dofRange) << ",\n"
         << "    \"flare\": " << fmtFloat(p.settings.flare) << ",\n"
         << "    \"godRays\": " << fmtFloat(p.settings.godRays) << ",\n"
         << "    \"blobShadows\": " << (p.settings.blobShadows ? "true" : "false")
         << ",\n"
         // The neural upscaler (docs/neural-upscaler.md). Project-wide, always
         // emitted like the fog/highlight groups next to it.
         << "    \"blssEnabled\": " << (p.settings.blssEnabled ? "true" : "false")
         << ",\n"
         << "    \"blssScale\": " << p.settings.blssScale << ",\n"
         << "    \"blssNetwork\": "
         << (p.settings.blssNetwork ? "true" : "false") << ",\n"
         << "    \"blssSharpen\": " << fmtFloat(p.settings.blssSharpen) << ",\n"
         << "    \"blssTemporal\": "
         << (p.settings.blssTemporal ? "true" : "false") << ",\n"
         << "    \"blssJitter\": "
         << (p.settings.blssJitter ? "true" : "false") << ",\n"
         << "    \"blssDebugView\": " << p.settings.blssDebugView << ",\n"
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
             << ", \"depth\": " << sc.terrain.depth
             // Omitted at its default, like every other flag here: a project
             // with no "enabled" key HAS a terrain (docs/terrain.md).
             << (sc.terrain.enabled ? "" : ", \"enabled\": false") << " },\n      ";
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
    // Which of them the game boots into. Omitted at 0, so no existing .tyra
    // changes shape until someone actually moves it.
    if (p.startScene > 0) json << ",\n  \"startScene\": " << p.startScene;
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
    // The two prompts: an explicit text/image mode plus the text itself, so
    // flipping to the image keeps whatever text was typed.
    auto writePromptText = [&](const char* key, const HudText& t) {
        json << ",\n  \"" << key << "\": { \"text\": \""
             << jsonEscape(t.text) << "\", \"size\": " << t.size
             << ", \"color\": " << fmtVec3(t.color)
             << (t.font.empty() ? "" : ", \"font\": \"" + jsonEscape(t.font) + "\"")
             << ", \"shadow\": " << (t.shadow ? "true" : "false") << " }";
    };
    json << ",\n  \"usePromptIsText\": "
         << (p.usePromptIsText ? "true" : "false");
    if (!p.usePromptText.text.empty())
        writePromptText("usePromptText", p.usePromptText);
    json << ",\n  \"pickPromptIsText\": "
         << (p.pickPromptIsText ? "true" : "false");
    if (!p.pickPromptText.text.empty())
        writePromptText("pickPromptText", p.pickPromptText);
    if (!p.pickPromptImage.empty())
        json << ",\n  \"pickPromptImage\": \""
             << jsonEscape(p.pickPromptImage) << "\"";
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
    // Inline text icons ({{name}} in any text). Emitted even at their seeded
    // defaults: the set is what a project's texts reference by name, and a
    // dropped key would silently change what {{cross}} resolves to.
    json << ",\n  \"textIcons\": [";
    for (size_t i = 0; i < p.textIcons.size(); ++i) {
        const TextIcon& ic = p.textIcons[i];
        json << (i ? ",\n    " : "\n    ") << "{ \"name\": \""
             << jsonEscape(ic.name) << "\", \"path\": \"" << jsonEscape(ic.path)
             << "\"";
        if (ic.scale != 1.0f) json << ", \"scale\": " << fmtFloat(ic.scale);
        json << " }";
    }
    json << (p.textIcons.empty() ? "]" : "\n  ]");
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

// Conditional too: no per-asset automatic-AO override = no key at all, so an
// untouched project resaves byte for byte (docs/ambient-occlusion.md).
static void writeModelAoSection(std::ostream& json, const Project& p) {
    bool any = false;
    for (const auto& [asset, mode] : p.modelAoMode) any |= mode != 0;
    if (!any) return;
    json << "\"modelAoMode\": {";
    bool first = true;
    for (const auto& [asset, mode] : p.modelAoMode) {
        if (mode == 0) continue;  // "follow the project default" IS no entry
        json << (first ? " " : ", ") << "\"" << jsonEscape(asset)
             << "\": " << mode;
        first = false;
    }
    json << " }";
}

// Conditional as well: nothing imported with a known real-world size = no key.
static void writeModelUnitsSection(std::ostream& json, const Project& p) {
    if (p.modelUnitMeters.empty()) return;
    json << "\"modelUnits\": {";
    bool first = true;
    for (const auto& [asset, meters] : p.modelUnitMeters) {
        json << (first ? " " : ", ") << "\"" << jsonEscape(asset)
             << "\": " << fmtFloat(meters);
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
    // Memory-card appearance (Tools > Save Editor): browser title and the
    // icon geometry/animation the icon bake reads.
    json << ",\n  \"saveTitle\": \"" << jsonEscape(p.saveTitle) << "\"";
    json << ",\n  \"saveIcon\": \"" << jsonEscape(p.saveIcon) << "\"";
    json << ",\n  \"saveIconModel\": \"" << jsonEscape(p.saveIconModel) << "\"";
    json << ",\n  \"saveIconClip\": \"" << jsonEscape(p.saveIconClip) << "\"";
    json << ",\n  \"saveIconFrames\": " << p.saveIconFrames;
    json << ",\n  \"saveIconMotion\": \"" << jsonEscape(p.saveIconMotion) << "\"";
    json << ",\n  \"saveIconMotionAmount\": " << fmtFloat(p.saveIconMotionAmount);
    json << ",\n  \"saveMenuWritesCheckpoint\": "
         << (p.saveMenuWritesCheckpoint ? "true" : "false");
    json << ",\n  \"saveAutosaveSlot\": " << p.saveAutosaveSlot;
    json << ",\n  \"saveSlotCount\": " << p.saveSlotCount;
    json << ",\n  \"saveSlotsPerPage\": " << p.saveSlotsPerPage;
    json << ",\n  \"saveAsync\": " << (p.saveAsync ? "true" : "false");
    json << ",\n  \"saveSpinner\": " << (p.saveSpinner ? "true" : "false");
    json << ",\n  \"saveSpinnerImage\": \"" << jsonEscape(p.saveSpinnerImage)
         << "\"";
    json << ",\n  \"saveSpinnerFrames\": " << p.saveSpinnerFrames;
    json << ",\n  \"saveSpinnerCorner\": " << p.saveSpinnerCorner;
    json << ",\n  \"saveSpinnerMargin\": " << fmtFloat(p.saveSpinnerMargin);
    json << ",\n  \"saveSpinnerScale\": " << fmtFloat(p.saveSpinnerScale);
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
             << ", \"fogEnd\": " << fmtFloat(a.fogEnd);
        // Day/night cycle (docs/day-night-cycle.md). Omitted entirely when the
        // preset has none, so a project that never opened the tab keeps the
        // .tyra it had.
        const DayCycle& c = a.cycle;
        if (c.enabled || !c.keys.empty()) {
            json << ",\n      \"cycle\": { \"enabled\": "
                 << (c.enabled ? "true" : "false")
                 << ", \"time\": " << fmtFloat(c.time)
                 << ", \"sunAzimuth\": " << fmtFloat(c.sunAzimuth)
                 << ", \"sunTilt\": " << fmtFloat(c.sunTilt)
                 << ", \"sunrise\": " << fmtFloat(c.sunrise)
                 << ", \"sunset\": " << fmtFloat(c.sunset)
                 << ", \"sunSize\": " << fmtFloat(c.sunSize)
                 << ", \"moonAzimuth\": " << fmtFloat(c.moonAzimuth)
                 << ", \"moonTilt\": " << fmtFloat(c.moonTilt)
                 << ", \"moonOffset\": " << fmtFloat(c.moonOffset)
                 << ", \"moonSize\": " << fmtFloat(c.moonSize)
                 << ", \"moonPhase\": " << fmtFloat(c.moonPhase)
                 << ", \"moonOpacity\": " << fmtFloat(c.moonOpacity)
                 << ", \"moonTexture\": \"" << jsonEscape(c.moonTexture)
                 << "\", \"runtime\": " << (c.runtime ? "true" : "false")
                 << ", \"dayLength\": " << fmtFloat(c.dayLength)
                 << ", \"bakeHour\": " << fmtFloat(c.bakeHour)
                 << ", \"runtimeGrade\": " << (c.runtimeGrade ? "true" : "false")
                 << ", \"starsEnabled\": " << (c.starsEnabled ? "true" : "false")
                 << ", \"starTwinkle\": " << fmtFloat(c.starTwinkle)
                 << ", \"starSeed\": " << c.starField.seed
                 << ", \"starCount\": " << c.starField.count
                 << ", \"starSpread\": " << fmtFloat(c.starField.magnitudeSpread)
                 << ", \"milkyWay\": " << fmtFloat(c.starField.milkyWay)
                 << ", \"milkyWayTilt\": " << fmtFloat(c.starField.milkyWayTilt)
                 << ", \"starSize\": " << fmtFloat(c.starField.sizeScale)
                 << ", \"keys\": [";
            for (size_t k = 0; k < c.keys.size(); ++k) {
                const DayKey& dk = c.keys[k];
                json << (k ? ",\n        " : "\n        ")
                     << "{ \"hour\": " << fmtFloat(dk.hour)
                     << ", \"skyColor\": " << fmtVec3(dk.skyColor)
                     << ", \"skyTopColor\": " << fmtVec3(dk.skyTopColor)
                     << ", \"lightColor\": " << fmtVec3(dk.lightColor)
                     << ", \"ambient\": " << fmtFloat(dk.ambient)
                     << ", \"diffuse\": " << fmtFloat(dk.diffuse)
                     << ", \"brightness\": " << fmtFloat(dk.brightness)
                     << ", \"fogColor\": " << fmtVec3(dk.fogColor)
                     << ", \"stars\": " << fmtFloat(dk.stars) << " }";
            }
            json << (c.keys.empty() ? "] }" : "\n      ] }");
        }
        json << " }";
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

// Credits rolls (Tools > Credits Editor). Blocks are written as a flat array in
// flow order; every presentation field that means "inherit the roll's" (size 0,
// empty font, no own color) is omitted so a roll restyled at the top stays one
// edit here too.
static void writeCreditsSection(std::ostream& json, const Project& p) {
    json << "\"credits\": [";
    for (size_t i = 0; i < p.credits.size(); ++i) {
        const CreditsRoll& r = p.credits[i];
        const HudImage& bg = r.bgImage;
        json << (i ? ",\n    " : "\n    ") << "{ \"name\": \"" << jsonEscape(r.name)
             << "\", \"bgColor\": " << fmtVec3(r.bgColor)
             << ", \"color\": " << fmtVec3(r.color)
             << ", \"headingColor\": " << fmtVec3(r.headingColor)
             << (r.font.empty() ? "" : ", \"font\": \"" + jsonEscape(r.font) + "\"")
             << ", \"headingSize\": " << r.headingSize
             << ", \"lineSize\": " << r.lineSize
             << ", \"shadow\": " << (r.shadow ? "true" : "false")
             << ", \"pageW\": " << r.pageW << ", \"margin\": " << fmtFloat(r.margin)
             << ", \"columnGap\": " << fmtFloat(r.columnGap)
             << ", \"lineSpacing\": " << fmtFloat(r.lineSpacing)
             << ",\n      \"mode\": " << r.mode << ", \"speed\": " << fmtFloat(r.speed)
             << ", \"cardSeconds\": " << fmtFloat(r.cardSeconds)
             << ", \"startDelay\": " << fmtFloat(r.startDelay)
             << ", \"endHold\": " << fmtFloat(r.endHold)
             << ", \"fadeIn\": " << fmtFloat(r.fadeIn)
             << ", \"fadeOut\": " << fmtFloat(r.fadeOut)
             << ", \"quant\": \"" << r.quant << "\"";
        if (!r.music.empty())
            json << ",\n      \"music\": \"" << jsonEscape(r.music)
                 << "\", \"musicLoop\": " << (r.musicLoop ? "true" : "false")
                 << ", \"musicStopAtEnd\": " << (r.musicStopAtEnd ? "true" : "false")
                 << ", \"musicVolume\": " << r.musicVolume;
        json << ",\n      \"skippable\": " << (r.skippable ? "true" : "false")
             << ", \"skipAfter\": " << fmtFloat(r.skipAfter)
             << (r.skipAction.empty()
                     ? ""
                     : ", \"skipAction\": \"" + jsonEscape(r.skipAction) + "\"")
             << ", \"showSkipHint\": " << (r.showSkipHint ? "true" : "false")
             << ", \"skipHint\": \"" << jsonEscape(r.skipHint)
             << "\", \"hintPos\": [" << fmtFloat(r.hintPos[0]) << ", "
             << fmtFloat(r.hintPos[1]) << "], \"hintSize\": " << r.hintSize
             << ",\n      \"finish\": " << r.finish << ", \"finishParam\": \""
             << jsonEscape(r.finishParam) << "\"";
        if (!r.source.empty())
            json << ", \"source\": \"" << jsonEscape(r.source) << "\"";
        if (!bg.imagePath.empty())
            json << ",\n      \"bgImage\": { \"image\": \"" << bg.imagePath
                 << "\", \"pos\": [" << fmtFloat(bg.pos[0]) << ", "
                 << fmtFloat(bg.pos[1]) << "], \"size\": [" << fmtFloat(bg.size[0])
                 << ", " << fmtFloat(bg.size[1]) << "], \"texW\": " << bg.texW
                 << ", \"texH\": " << bg.texH << ", \"texQuant\": \""
                 << bg.texQuant << "\" }";
        json << ",\n      \"blocks\": [";
        for (size_t k = 0; k < r.blocks.size(); ++k) {
            const CreditsBlock& b = r.blocks[k];
            json << (k ? ",\n        " : "\n        ") << "{ \"kind\": " << b.kind;
            if (!b.text.empty()) json << ", \"text\": \"" << jsonEscape(b.text) << "\"";
            if (!b.text2.empty())
                json << ", \"text2\": \"" << jsonEscape(b.text2) << "\"";
            if (!b.imagePath.empty())
                json << ", \"image\": \"" << b.imagePath << "\"";
            if (b.size) json << ", \"size\": " << b.size;
            if (!b.font.empty()) json << ", \"font\": \"" << jsonEscape(b.font) << "\"";
            if (b.ownColor) json << ", \"color\": " << fmtVec3(b.color);
            if (b.align != 1) json << ", \"align\": " << b.align;
            if (b.space != 0.0f) json << ", \"space\": " << fmtFloat(b.space);
            if (b.kind == CreditsBlock::Image) json << ", \"scale\": " << fmtFloat(b.scale);
            json << " }";
        }
        json << (r.blocks.empty() ? "]" : "\n      ]") << " }";
    }
    json << (p.credits.empty() ? "]" : "\n  ]");
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
                 << (k.roll != 0.0f ? ", \"roll\": " + fmtFloat(k.roll) : "")
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
                                         "apply-video", "rebind", "credits",
                                         "label"};
    for (size_t i = 0; i < p.menus.size(); ++i) {
        const GameMenu& m = p.menus[i];
        json << (i ? ",\n    " : "\n    ") << "{ \"name\": \"" << m.name
             << "\", \"title\": \"" << m.title << "\""
             << (m.titleScreen ? ", \"titleScreen\": true" : "")
             << (m.pauseGame ? "" : ", \"pause\": false")
             << (m.pauseMenu ? ", \"pauseMenu\": true" : "")
             << (m.saveMenu ? ", \"saveMenu\": true" : "")
             << (m.panelW != 256 ? ", \"panelW\": " + std::to_string(m.panelW) : "")
             << (m.showTitle ? "" : ", \"showTitle\": false")
             << (m.font.empty() ? "" : ", \"font\": \"" + jsonEscape(m.font) + "\"")
             << (m.style.empty() ? "" : ", \"style\": \"" + jsonEscape(m.style) + "\"")
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
            const int a =
                (en.action >= 0 && en.action <= MenuEntry::Label) ? en.action : 0;
            json << (e ? ",\n        " : "\n        ") << "{ \"label\": \""
                 << en.label << "\", \"action\": \"" << kMenuActions[a] << "\""
                 << (en.param.empty() ? "" : ", \"param\": \"" + en.param + "\"")
                 << (en.bindAction.empty()
                         ? ""
                         : ", \"bindAction\": \"" + jsonEscape(en.bindAction) +
                               "\"")
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
                "",            "music-volume", "sfx-volume",  "deadzone",
                "stick-curve", "display-mode", "widescreen",  "player-count",
                "input-preset"};
            if (en.settingBind >= 1 && en.settingBind <= 8)
                json << ", \"bind\": \"" << kMenuBinds[en.settingBind] << "\"";
            if (!en.styleClass.empty())
                json << ", \"class\": \"" << jsonEscape(en.styleClass) << "\"";
            if (!en.description.empty())
                json << ", \"desc\": \"" << jsonEscape(en.description) << "\"";
            if (!en.icon.empty())
                json << ", \"icon\": \"" << jsonEscape(en.icon) << "\"";
            if (!en.enabledWhen.empty())
                json << ", \"enabledWhen\": \"" << jsonEscape(en.enabledWhen) << "\"";
            json << " }";
        }
        json << (m.entries.empty() ? "]" : "\n      ]") << " }";
    }
    json << (p.menus.empty() ? "]" : "\n  ]");
}

// Input actions + binding presets (Tools > Input Map). Always emitted: after
// ensureInputActions every project has the built-in actions, and a project
// whose .tyra lost the key would silently fall back to the seeded defaults
// instead of the user's bindings.
static void writeInputSection(std::ostream& json, const Project& p) {
    // Role -> stable json name. Index = InputAction::Role.
    static const char* kRoles[] = {
        "",        "jump",      "use",       "throw",     "sprint",
        "fly-up",  "fly-down",  "confirm",   "back",      "menu",
        "alt",     "menu-up",   "menu-down", "menu-left", "menu-right",
        "move-forward", "move-back", "move-left", "move-right"};
    json << "\"input\": {\n    \"activePreset\": " << p.input.activePreset
         << ",\n    \"allowRebind\": "
         << (p.input.allowRebind ? "true" : "false") << ",\n    \"actions\": [";
    for (size_t i = 0; i < p.input.actions.size(); ++i) {
        const InputAction& a = p.input.actions[i];
        const int r = (a.role > 0 && a.role < InputAction::RoleCount) ? a.role : 0;
        json << (i ? ",\n      " : "\n      ") << "{ \"name\": \""
             << jsonEscape(a.name) << "\", \"label\": \"" << jsonEscape(a.label)
             << "\"";
        if (r != 0) json << ", \"role\": \"" << kRoles[r] << "\"";
        if (!a.rebindable) json << ", \"rebindable\": false";
        json << " }";
    }
    json << (p.input.actions.empty() ? "]" : "\n    ]") << ",\n    \"presets\": [";
    for (size_t i = 0; i < p.input.presets.size(); ++i) {
        const InputPreset& pr = p.input.presets[i];
        json << (i ? ",\n      " : "\n      ") << "{ \"name\": \""
             << jsonEscape(pr.name) << "\", \"bindings\": [";
        for (size_t b = 0; b < pr.bindings.size(); ++b) {
            const InputBinding& bd = pr.bindings[b];
            json << (b ? ",\n          " : "\n          ") << "{ \"action\": \""
                 << jsonEscape(bd.action) << "\"";
            if (!bd.pad.empty()) json << ", \"pad\": \"" << bd.pad << "\"";
            if (bd.key != 0) json << ", \"key\": " << bd.key;
            if (bd.mouse != 0) json << ", \"mouse\": " << bd.mouse;
            json << " }";
        }
        json << (pr.bindings.empty() ? "]" : "\n        ]") << " }";
    }
    json << (p.input.presets.empty() ? "]" : "\n    ]") << "\n  }";
}

static void readInputSection(const json::Value& root, Project& out) {
    out.input = InputMap{};
    const auto* in = root.find("input");
    if (!in || in->type != json::Value::Type::Object) return;
    if (const auto* v = in->find("activePreset"))
        out.input.activePreset = (int)v->numberOr(0.0);
    if (const auto* v = in->find("allowRebind"))
        out.input.allowRebind = v->boolOr(true);
    if (const auto* arr = in->find("actions");
        arr && arr->type == json::Value::Type::Array) {
        for (const auto& ja : arr->arr) {
            InputAction a;
            if (const auto* v = ja.find("name")) a.name = v->stringOr("");
            if (const auto* v = ja.find("label")) a.label = v->stringOr("");
            if (const auto* v = ja.find("role")) {
                const std::string r = v->stringOr("");
                for (int i = 1; i < InputAction::RoleCount; ++i)
                    if (r == inputRoleName(i)) a.role = i;
            }
            if (const auto* v = ja.find("rebindable"))
                a.rebindable = v->boolOr(true);
            if (a.name.empty()) continue;  // an unnamed action addresses nothing
            if (a.label.empty()) a.label = a.name;
            out.input.actions.push_back(std::move(a));
        }
    }
    if (const auto* arr = in->find("presets");
        arr && arr->type == json::Value::Type::Array) {
        for (const auto& jp : arr->arr) {
            InputPreset pr;
            if (const auto* v = jp.find("name")) pr.name = v->stringOr("Preset");
            if (const auto* bs = jp.find("bindings");
                bs && bs->type == json::Value::Type::Array) {
                for (const auto& jb : bs->arr) {
                    InputBinding b;
                    if (const auto* v = jb.find("action"))
                        b.action = v->stringOr("");
                    if (const auto* v = jb.find("pad")) b.pad = v->stringOr("");
                    if (const auto* v = jb.find("key")) b.key = (int)v->numberOr(0.0);
                    if (const auto* v = jb.find("mouse"))
                        b.mouse = (int)v->numberOr(0.0);
                    // Drop what the game could not use: an unknown pad name or
                    // an out-of-range key/mouse would reach codegen as junk.
                    if (b.action.empty()) continue;
                    if (!b.pad.empty() && padButtonIndex(b.pad) < 0) b.pad.clear();
                    if (b.key < 0 || b.key > 255) b.key = 0;
                    if (b.mouse < 0 || b.mouse > 3) b.mouse = 0;
                    pr.bindings.push_back(std::move(b));
                }
            }
            if (!pr.name.empty()) out.input.presets.push_back(std::move(pr));
        }
    }
    if (out.input.activePreset < 0 ||
        out.input.activePreset >= (int)out.input.presets.size())
        out.input.activePreset = 0;
}

static void readObjectsArray(const json::Value& arr, std::vector<SceneObject>& out);

// Prefabs (Tools > Prefabs, docs/prefabs.md). Members are ordinary scene
// objects written with the SAME objectJson the scenes use - a prefab is a piece
// of scene, and giving it its own lighter member format would mean two writers
// to keep in step every time an object grows a field. Conditional: a project
// with no prefabs emits nothing.
static void writePrefabsSection(std::ostream& json, const Project& p) {
    if (p.prefabs.empty()) return;
    json << "\"prefabs\": [";
    for (size_t i = 0; i < p.prefabs.size(); ++i) {
        const Prefab& pf = p.prefabs[i];
        json << (i ? ",\n    " : "\n    ") << "{ \"id\": \"" << jsonEscape(pf.id)
             << "\", \"name\": \"" << jsonEscape(pf.name) << "\"";
        if (!pf.notes.empty())
            json << ", \"notes\": \"" << jsonEscape(pf.notes) << "\"";
        json << ", \"objects\": [";
        for (size_t k = 0; k < pf.objects.size(); ++k)
            json << (k ? ",\n      " : "\n      ") << objectJson(pf.objects[k]);
        if (!pf.objects.empty()) json << "\n    ";
        json << "] }";
    }
    json << "\n  ]";
}

static void readPrefabsSection(const json::Value& root, Project& out) {
    out.prefabs.clear();
    const json::Value* arr = root.find("prefabs");
    if (!arr || arr->type != json::Value::Type::Array) return;
    for (const json::Value& e : arr->arr) {
        Prefab pf;
        if (const json::Value* v = e.find("id")) pf.id = v->stringOr("");
        if (const json::Value* v = e.find("name")) pf.name = v->stringOr("");
        if (const json::Value* v = e.find("notes")) pf.notes = v->stringOr("");
        if (pf.name.empty()) continue;
        if (pf.id.empty()) pf.id = project::newObjectId();
        if (const json::Value* objs = e.find("objects"))
            if (objs->type == json::Value::Type::Array)
                readObjectsArray(*objs, pf.objects);
        // A member must never carry an object id: two instances of one prefab
        // would then share an identity, and the .tyra's per-object files are
        // keyed on exactly that.
        for (SceneObject& o : pf.objects) o.id.clear();
        out.prefabs.push_back(std::move(pf));
    }
}

// Non-destructive clip edits (Tools > Animation Editor). Conditional: an
// untouched project emits nothing, so the key only appears once the user has
// actually changed a clip.
static void writeAnimEditsSection(std::ostream& json, const Project& p) {
    if (p.animClipEdits.empty()) return;
    json << "\"animClipEdits\": [";
    for (size_t i = 0; i < p.animClipEdits.size(); ++i) {
        const AnimClipEdit& e = p.animClipEdits[i];
        json << (i ? ",\n    " : "\n    ") << "{ \"model\": \""
             << jsonEscape(e.model) << "\", \"clip\": \"" << jsonEscape(e.clip)
             << "\"";
        if (!e.rename.empty())
            json << ", \"rename\": \"" << jsonEscape(e.rename) << "\"";
        if (e.timeScale != 1.0f)
            json << ", \"timeScale\": " << fmtFloat(e.timeScale);
        if (e.trimStart != 0.0f)
            json << ", \"trimStart\": " << fmtFloat(e.trimStart);
        if (e.trimEnd != 0.0f) json << ", \"trimEnd\": " << fmtFloat(e.trimEnd);
        if (e.inPlace) json << ", \"inPlace\": true";
        if (!e.loop) json << ", \"loop\": false";
        json << " }";
    }
    json << "\n  ]";
}

// Clips borrowed from other model files (docs/animation-import.md). Same
// conditional shape as the section above: a project that never imported
// anything emits nothing at all. Every retarget flag is written only when it
// differs from its default, so a row created with the defaults - which is
// almost every row - stays two keys long.
static void writeAnimImportsSection(std::ostream& json, const Project& p) {
    if (p.animImports.empty()) return;
    json << "\"animImports\": [";
    for (size_t i = 0; i < p.animImports.size(); ++i) {
        const AnimImport& a = p.animImports[i];
        json << (i ? ",\n    " : "\n    ") << "{ \"model\": \""
             << jsonEscape(a.model) << "\", \"source\": \""
             << jsonEscape(a.source) << "\"";
        // Absent = every clip in the donor, which is what a one-clip Mixamo
        // file wants and what the panel offers by default.
        if (!a.clips.empty()) {
            json << ", \"clips\": [";
            for (size_t k = 0; k < a.clips.size(); ++k)
                json << (k ? ", " : "") << "\"" << jsonEscape(a.clips[k]) << "\"";
            json << "]";
        }
        if (!a.prefix.empty())
            json << ", \"prefix\": \"" << jsonEscape(a.prefix) << "\"";
        if (!a.boneMap.empty()) {
            json << ", \"boneMap\": [";
            for (size_t k = 0; k < a.boneMap.size(); ++k)
                json << (k ? ", " : "") << "{ \"s\": \""
                     << jsonEscape(a.boneMap[k].first) << "\", \"t\": \""
                     << jsonEscape(a.boneMap[k].second) << "\" }";
            json << "]";
        }
        if (a.facing >= 0) json << ", \"facing\": " << a.facing;
        if (a.mirror) json << ", \"mirror\": true";
        if (a.lean != 0.0f) json << ", \"lean\": " << fmtFloat(a.lean);
        if (a.translation != 0) json << ", \"translation\": " << a.translation;
        if (!a.ignoreScale) json << ", \"ignoreScale\": false";
        if (!a.retargetRoot) json << ", \"retargetRoot\": false";
        if (!a.stripNamespace) json << ", \"stripNamespace\": false";
        if (!a.caseInsensitive) json << ", \"caseInsensitive\": false";
        if (!a.skeletonTracksOnly) json << ", \"skeletonTracksOnly\": false";
        json << " }";
    }
    json << "\n  ]";
}

static void readAnimImportsSection(const json::Value& root, Project& out) {
    out.animImports.clear();
    const auto* arr = root.find("animImports");
    if (!arr || arr->type != json::Value::Type::Array) return;
    for (const json::Value& e : arr->arr) {
        AnimImport a;
        if (const auto* v = e.find("model")) a.model = v->stringOr("");
        if (const auto* v = e.find("source")) a.source = v->stringOr("");
        // A row naming neither end can do nothing but produce a warning per
        // build, so it is dropped on load like an unknown screen-effect key.
        if (a.model.empty() || a.source.empty()) continue;
        if (const auto* v = e.find("clips"); v && v->type == json::Value::Type::Array)
            for (const json::Value& c : v->arr) a.clips.push_back(c.stringOr(""));
        if (const auto* v = e.find("prefix")) a.prefix = v->stringOr("");
        if (const auto* v = e.find("boneMap");
            v && v->type == json::Value::Type::Array)
            for (const json::Value& m : v->arr) {
                const auto* from = m.find("s");
                const auto* to = m.find("t");
                if (from && to)
                    a.boneMap.emplace_back(from->stringOr(""), to->stringOr(""));
            }
        if (const auto* v = e.find("facing")) a.facing = (int)v->numberOr(-1);
        if (const auto* v = e.find("mirror")) a.mirror = v->boolOr(false);
        if (const auto* v = e.find("lean")) a.lean = (float)v->numberOr(0.0);
        if (const auto* v = e.find("translation"))
            a.translation = (int)v->numberOr(0);
        if (a.translation < 0 || a.translation > 2) a.translation = 0;
        if (const auto* v = e.find("ignoreScale")) a.ignoreScale = v->boolOr(true);
        if (const auto* v = e.find("retargetRoot")) a.retargetRoot = v->boolOr(true);
        if (const auto* v = e.find("stripNamespace"))
            a.stripNamespace = v->boolOr(true);
        if (const auto* v = e.find("caseInsensitive"))
            a.caseInsensitive = v->boolOr(true);
        if (const auto* v = e.find("skeletonTracksOnly"))
            a.skeletonTracksOnly = v->boolOr(true);
        out.animImports.push_back(std::move(a));
    }
}

// The project's own VU programs (docs/vu-authoring.md). Conditional: a project
// that never opened Tools > VU Programs emits nothing.
static void writeVuStages(std::ostream& json, const std::vector<VuStage>& st) {
    json << "[";
    for (size_t i = 0; i < st.size(); ++i) {
        const VuStage& s = st[i];
        json << (i ? ", " : "") << "{ \"kind\": \"" << jsonEscape(s.kind) << "\"";
        if (!s.enabled) json << ", \"off\": true";
        json << ", \"p\": [";
        for (int k = 0; k < 4; ++k)
            json << (k ? ", " : "") << fmtFloat(s.params[k]);
        json << "]";
        // Only written when something is actually bound - the common case is
        // four literals and four -1s would be noise in every diff.
        bool any = false;
        for (int k = 0; k < 4; ++k) any = any || s.bind[k] >= 0;
        if (any) {
            json << ", \"bind\": [";
            for (int k = 0; k < 4; ++k) json << (k ? ", " : "") << s.bind[k];
            json << "]";
        }
        json << " }";
    }
    json << "]";
}

static void readVuStages(const json::Value& arr, std::vector<VuStage>& out) {
    out.clear();
    if (arr.type != json::Value::Type::Array) return;
    for (const json::Value& e : arr.arr) {
        VuStage s;
        if (const json::Value* v = e.find("kind")) s.kind = v->stringOr("");
        if (s.kind.empty()) continue;
        if (const json::Value* v = e.find("off")) s.enabled = !v->boolOr(false);
        if (const json::Value* v = e.find("p"))
            if (v->type == json::Value::Type::Array)
                for (size_t k = 0; k < v->arr.size() && k < 4; ++k)
                    s.params[k] = (float)v->arr[k].numberOr(0.0);
        if (const json::Value* v = e.find("bind"))
            if (v->type == json::Value::Type::Array)
                for (size_t k = 0; k < v->arr.size() && k < 4; ++k) {
                    const int b = (int)v->arr[k].numberOr(-1.0);
                    s.bind[k] = (b >= 0 && b < 4) ? b : -1;
                }
        out.push_back(std::move(s));
    }
}

static void writeVuSection(std::ostream& json, const Project& p) {
    const VuSettings& v = p.vu;
    const bool touched = !v.programs.empty() || !v.kernel.stages.empty() ||
                         v.kernel.enabled || !v.residentAuto ||
                         v.residentClasses != 0x1Fu || !v.scriptBoot.empty();
    if (!touched) return;
    json << "\"vu\": { \"activeLook\": " << v.activeLook
         << ", \"residentClasses\": " << v.residentClasses
         << ", \"residentAuto\": " << (v.residentAuto ? "true" : "false");
    if (!v.scriptBoot.empty()) {
        json << ", \"scriptBoot\": [";
        for (size_t i = 0; i < v.scriptBoot.size(); ++i)
            json << (i ? ", " : "") << "{ \"name\": \""
                 << jsonEscape(v.scriptBoot[i].first) << "\", \"on\": "
                 << (v.scriptBoot[i].second ? "true" : "false") << " }";
        json << "]";
    }
    if (!v.programs.empty()) {
        json << ", \"looks\": [";
        for (size_t i = 0; i < v.programs.size(); ++i) {
            const VuProgram& pr = v.programs[i];
            json << (i ? ",\n      " : "\n      ") << "{ \"name\": \""
                 << jsonEscape(pr.name) << "\", \"classes\": " << pr.classes;
            if (!pr.enabled) json << ", \"off\": true";
            json << ", \"stages\": ";
            writeVuStages(json, pr.stages);
            json << " }";
        }
        json << "\n    ]";
    }
    if (v.kernel.enabled || !v.kernel.stages.empty()) {
        json << ", \"kernel\": { \"name\": \"" << jsonEscape(v.kernel.name)
             << "\", \"enabled\": " << (v.kernel.enabled ? "true" : "false")
             << ", \"maxElements\": " << v.kernel.maxElements << ", \"stages\": ";
        writeVuStages(json, v.kernel.stages);
        json << " }";
    }
    json << " }";
}

// The neural upscaler's training-shot plan (docs/neural-upscaler.md). Written
// ONLY when something has been authored: a default plan is what the trainer has
// always done, so an untouched project's .tyra keeps its exact previous shape
// and every fold table published against it stays reproducible.
//
// The automatic moves are written as a NAME list rather than a bool array, so
// the order in the enum is free to grow without a file written today meaning
// something else tomorrow - the same reason `inputCodes()` is append-only and
// the menu display modes are stored explicitly.
static void writeBlssShotsSection(std::ostream& json, const Project& p) {
    const BlssShotPlan& pl = p.blssShots;
    if (pl.isDefault()) return;
    json << "\"blssShots\": { \"takes\": " << (pl.authoredTakes ? "true" : "false")
         << ", \"auto\": [";
    bool first = true;
    for (int i = 0; i < kBlssAutoMoveCount; ++i) {
        if (!pl.autoMove[i]) continue;
        json << (first ? "" : ", ") << "\"" << blssAutoMoveName(i) << "\"";
        first = false;
    }
    json << "]";
    // Per-move frame counts, only for the moves that ask for one.
    bool anyFrames = false;
    for (int i = 0; i < kBlssAutoMoveCount; ++i) anyFrames = anyFrames || pl.autoFrames[i] > 0;
    if (anyFrames) {
        json << ", \"autoFrames\": {";
        first = true;
        for (int i = 0; i < kBlssAutoMoveCount; ++i) {
            if (pl.autoFrames[i] <= 0) continue;
            json << (first ? "" : ", ") << "\"" << blssAutoMoveName(i) << "\": " << pl.autoFrames[i];
            first = false;
        }
        json << "}";
    }
    if (!pl.shots.empty()) {
        json << ", \"shots\": [";
        for (size_t i = 0; i < pl.shots.size(); ++i) {
            const BlssShot& s = pl.shots[i];
            json << (i ? ",\n      " : "\n      ") << "{ \"name\": \"" << jsonEscape(s.name)
                 << "\", \"scene\": \"" << jsonEscape(s.scene) << "\"";
            if (!s.camera.empty()) json << ", \"camera\": \"" << jsonEscape(s.camera) << "\"";
            if (!s.cameraTo.empty())
                json << ", \"cameraTo\": \"" << jsonEscape(s.cameraTo) << "\"";
            json << ", \"eye\": [" << fmtFloat(s.eye[0]) << ", " << fmtFloat(s.eye[1]) << ", "
                 << fmtFloat(s.eye[2]) << "], \"look\": [" << fmtFloat(s.look[0]) << ", "
                 << fmtFloat(s.look[1]) << ", " << fmtFloat(s.look[2]) << "]";
            if (s.move)
                json << ", \"move\": true, \"eye2\": [" << fmtFloat(s.eye2[0]) << ", "
                     << fmtFloat(s.eye2[1]) << ", " << fmtFloat(s.eye2[2]) << "], \"look2\": ["
                     << fmtFloat(s.look2[0]) << ", " << fmtFloat(s.look2[1]) << ", "
                     << fmtFloat(s.look2[2]) << "]";
            if (s.fovDeg != 60.0f) json << ", \"fov\": " << fmtFloat(s.fovDeg);
            if (s.frames > 0) json << ", \"frames\": " << s.frames;
            if (!s.enabled) json << ", \"off\": true";
            json << " }";
        }
        json << "\n    ]";
    }
    json << " }";
}

// --- World Facts (docs/world-facts.md) ---------------------------------------
// One section carrying all four collections, because they only mean anything
// together: a query over facts that are not there, or a rule over a query that
// is not, is not a state a collaboration peer should ever be handed.

static void writeFactCondition(std::ostream& json, const facts::Condition& c) {
    json << "{ \"kind\": \"" << facts::conditionKindKey(c.kind) << "\"";
    if (c.kind == facts::Condition::Kind::Compare) {
        json << ", \"fact\": \"" << jsonEscape(c.fact) << "\", \"cmp\": \""
             << facts::cmpKey(c.cmp) << "\"";
        if (!c.rhsFact.empty())
            json << ", \"rhsFact\": \"" << jsonEscape(c.rhsFact) << "\"";
        else
            json << ", \"value\": " << fmtFloat(c.value);
    } else if (c.kind == facts::Condition::Kind::Query) {
        json << ", \"query\": \"" << jsonEscape(c.query) << "\"";
    } else if (!c.children.empty()) {
        json << ", \"children\": [";
        for (size_t i = 0; i < c.children.size(); ++i) {
            json << (i ? ", " : "");
            writeFactCondition(json, c.children[i]);
        }
        json << "]";
    }
    json << " }";
}

static void readBlssShotsSection(const json::Value& root, Project& out) {
    out.blssShots = BlssShotPlan();
    const json::Value* v = root.find("blssShots");
    if (!v || v->type != json::Value::Type::Object) return;
    if (const json::Value* t = v->find("takes")) out.blssShots.authoredTakes = t->boolOr(true);
    // An absent "auto" key would mean "every move", but the key is always
    // written when the section is - and an EMPTY list is a legitimate choice
    // (shoot only what I authored), so the two must not be confused.
    if (const json::Value* a = v->find("auto"); a && a->type == json::Value::Type::Array) {
        for (int i = 0; i < kBlssAutoMoveCount; ++i) out.blssShots.autoMove[i] = false;
        for (const json::Value& e : a->arr) {
            const std::string name = e.stringOr("");
            for (int i = 0; i < kBlssAutoMoveCount; ++i)
                if (name == blssAutoMoveName(i)) out.blssShots.autoMove[i] = true;
        }
    }
    if (const json::Value* f = v->find("autoFrames"); f && f->type == json::Value::Type::Object)
        for (int i = 0; i < kBlssAutoMoveCount; ++i)
            if (const json::Value* n = f->find(blssAutoMoveName(i)))
                out.blssShots.autoFrames[i] = std::max(0, (int)n->numberOr(0.0));
    const json::Value* arr = v->find("shots");
    if (!arr || arr->type != json::Value::Type::Array) return;
    for (const json::Value& e : arr->arr) {
        BlssShot s;
        if (const json::Value* x = e.find("name")) s.name = x->stringOr("");
        if (const json::Value* x = e.find("scene")) s.scene = x->stringOr("");
        if (const json::Value* x = e.find("camera")) s.camera = x->stringOr("");
        if (const json::Value* x = e.find("cameraTo")) s.cameraTo = x->stringOr("");
        const auto vec3 = [&](const char* key, float dst[3]) {
            const json::Value* x = e.find(key);
            if (!x || x->type != json::Value::Type::Array) return;
            for (size_t k = 0; k < x->arr.size() && k < 3; ++k)
                dst[k] = (float)x->arr[k].numberOr(0.0);
        };
        vec3("eye", s.eye);
        vec3("look", s.look);
        if (const json::Value* x = e.find("move")) s.move = x->boolOr(false);
        vec3("eye2", s.eye2);
        vec3("look2", s.look2);
        if (const json::Value* x = e.find("fov")) s.fovDeg = (float)x->numberOr(60.0);
        if (s.fovDeg < 5.0f || s.fovDeg > 175.0f) s.fovDeg = 60.0f;
        if (const json::Value* x = e.find("frames")) s.frames = std::max(0, (int)x->numberOr(0.0));
        if (const json::Value* x = e.find("off")) s.enabled = !x->boolOr(false);
        out.blssShots.shots.push_back(std::move(s));
    }
}

static void readFactCondition(const json::Value& v, facts::Condition& out) {
    out = facts::Condition();
    if (v.type != json::Value::Type::Object) return;
    if (const json::Value* x = v.find("kind"))
        out.kind = facts::conditionKindFromKey(x->stringOr("all"));
    if (const json::Value* x = v.find("fact")) out.fact = x->stringOr("");
    if (const json::Value* x = v.find("cmp"))
        out.cmp = facts::cmpFromKey(x->stringOr("ge"));
    if (const json::Value* x = v.find("value"))
        out.value = (float)x->numberOr(0.0);
    if (const json::Value* x = v.find("rhsFact")) out.rhsFact = x->stringOr("");
    if (const json::Value* x = v.find("query")) out.query = x->stringOr("");
    if (const json::Value* kids = v.find("children");
        kids && kids->type == json::Value::Type::Array) {
        for (const json::Value& k : kids->arr) {
            facts::Condition c;
            readFactCondition(k, c);
            out.children.push_back(std::move(c));
        }
    }
}

static void writeFactsSection(std::ostream& json, const Project& p) {
    if (p.facts.empty() && p.factQueries.empty() && p.factRules.empty() &&
        p.factScenarios.empty())
        return;
    json << "\"facts\": [";
    for (size_t i = 0; i < p.facts.size(); ++i) {
        const facts::Fact& f = p.facts[i];
        json << (i ? ",\n    " : "\n    ") << "{ \"id\": \"" << jsonEscape(f.id)
             << "\", \"name\": \"" << jsonEscape(f.name) << "\", \"type\": \""
             << facts::typeKey(f.type) << "\", \"persist\": \""
             << facts::persistKey(f.persist) << "\"";
        if (f.scope != facts::Scope::World)
            json << ", \"scope\": \"" << facts::scopeKey(f.scope) << "\"";
        if (f.type == facts::Type::Position)
            json << ", \"pos\": [" << fmtFloat(f.pos[0]) << ", "
                 << fmtFloat(f.pos[1]) << ", " << fmtFloat(f.pos[2]) << "]";
        else if (f.value != 0.0f)
            json << ", \"default\": " << fmtFloat(f.value);
        if (!f.options.empty()) {
            json << ", \"options\": [";
            for (size_t k = 0; k < f.options.size(); ++k)
                json << (k ? ", " : "") << "\"" << jsonEscape(f.options[k])
                     << "\"";
            json << "]";
        }
        if (!f.computed.empty())
            json << ", \"computed\": \"" << jsonEscape(f.computed) << "\"";
        if (!f.desc.empty())
            json << ", \"desc\": \"" << jsonEscape(f.desc) << "\"";
        json << " }";
    }
    json << (p.facts.empty() ? "]" : "\n  ]");

    json << ",\n  \"factQueries\": [";
    for (size_t i = 0; i < p.factQueries.size(); ++i) {
        const facts::Query& q = p.factQueries[i];
        json << (i ? ",\n    " : "\n    ") << "{ \"name\": \""
             << jsonEscape(q.name) << "\"";
        if (!q.desc.empty())
            json << ", \"desc\": \"" << jsonEscape(q.desc) << "\"";
        json << ", \"when\": ";
        writeFactCondition(json, q.root);
        json << " }";
    }
    json << (p.factQueries.empty() ? "]" : "\n  ]");

    json << ",\n  \"factRules\": [";
    for (size_t i = 0; i < p.factRules.size(); ++i) {
        const facts::Rule& r = p.factRules[i];
        json << (i ? ",\n    " : "\n    ") << "{ \"name\": \""
             << jsonEscape(r.name) << "\", \"policy\": \""
             << facts::policyKey(r.policy) << "\"";
        if (!r.enabled) json << ", \"off\": true";
        if (!r.desc.empty())
            json << ", \"desc\": \"" << jsonEscape(r.desc) << "\"";
        json << ", \"when\": ";
        writeFactCondition(json, r.when);
        json << ", \"then\": [";
        for (size_t k = 0; k < r.then.size(); ++k) {
            const facts::RuleAction& a = r.then[k];
            json << (k ? ", " : "") << "{ \"do\": \""
                 << facts::actionKindKey(a.kind) << "\", \"target\": \""
                 << jsonEscape(a.target) << "\"";
            if (a.kind != facts::RuleAction::Kind::ToggleFact)
                json << ", \"value\": " << fmtFloat(a.value);
            json << " }";
        }
        json << "] }";
    }
    json << (p.factRules.empty() ? "]" : "\n  ]");

    json << ",\n  \"factScenarios\": [";
    for (size_t i = 0; i < p.factScenarios.size(); ++i) {
        const facts::Scenario& s = p.factScenarios[i];
        json << (i ? ",\n    " : "\n    ") << "{ \"name\": \""
             << jsonEscape(s.name) << "\"";
        if (!s.desc.empty())
            json << ", \"desc\": \"" << jsonEscape(s.desc) << "\"";
        json << ", \"values\": [";
        for (size_t k = 0; k < s.values.size(); ++k) {
            const facts::ScenarioValue& v = s.values[k];
            json << (k ? ", " : "") << "{ \"fact\": \"" << jsonEscape(v.fact)
                 << "\", \"value\": " << fmtFloat(v.value);
            if (v.pos[0] != 0.0f || v.pos[1] != 0.0f || v.pos[2] != 0.0f)
                json << ", \"pos\": [" << fmtFloat(v.pos[0]) << ", "
                     << fmtFloat(v.pos[1]) << ", " << fmtFloat(v.pos[2]) << "]";
            json << " }";
        }
        json << "] }";
    }
    json << (p.factScenarios.empty() ? "]" : "\n  ]");
}

static void readFactsSection(const json::Value& root, Project& out) {
    out.facts.clear();
    out.factQueries.clear();
    out.factRules.clear();
    out.factScenarios.clear();

    if (const json::Value* arr = root.find("facts");
        arr && arr->type == json::Value::Type::Array) {
        for (const json::Value& e : arr->arr) {
            facts::Fact f;
            if (const json::Value* x = e.find("id")) f.id = x->stringOr("");
            if (const json::Value* x = e.find("name")) f.name = x->stringOr("");
            if (f.name.empty()) continue;
            if (const json::Value* x = e.find("type"))
                f.type = facts::typeFromKey(x->stringOr("bool"));
            if (const json::Value* x = e.find("persist"))
                f.persist = facts::persistFromKey(x->stringOr("session"));
            if (const json::Value* x = e.find("scope"))
                f.scope = facts::scopeFromKey(x->stringOr("world"));
            if (const json::Value* x = e.find("default"))
                f.value = (float)x->numberOr(0.0);
            if (const json::Value* x = e.find("pos");
                x && x->type == json::Value::Type::Array)
                for (size_t a = 0; a < 3 && a < x->arr.size(); ++a)
                    f.pos[a] = (float)x->arr[a].numberOr(0.0);
            if (const json::Value* x = e.find("options");
                x && x->type == json::Value::Type::Array)
                for (const json::Value& o : x->arr)
                    f.options.push_back(o.stringOr(""));
            if (const json::Value* x = e.find("computed"))
                f.computed = x->stringOr("");
            if (const json::Value* x = e.find("desc")) f.desc = x->stringOr("");
            out.facts.push_back(std::move(f));
        }
    }

    if (const json::Value* arr = root.find("factQueries");
        arr && arr->type == json::Value::Type::Array) {
        for (const json::Value& e : arr->arr) {
            facts::Query q;
            if (const json::Value* x = e.find("name")) q.name = x->stringOr("");
            if (q.name.empty()) continue;
            if (const json::Value* x = e.find("desc")) q.desc = x->stringOr("");
            if (const json::Value* x = e.find("when")) readFactCondition(*x, q.root);
            out.factQueries.push_back(std::move(q));
        }
    }

    if (const json::Value* arr = root.find("factRules");
        arr && arr->type == json::Value::Type::Array) {
        for (const json::Value& e : arr->arr) {
            facts::Rule r;
            if (const json::Value* x = e.find("name")) r.name = x->stringOr("");
            if (r.name.empty()) continue;
            if (const json::Value* x = e.find("desc")) r.desc = x->stringOr("");
            if (const json::Value* x = e.find("policy"))
                r.policy = facts::policyFromKey(x->stringOr("rising"));
            if (const json::Value* x = e.find("off")) r.enabled = !x->boolOr(false);
            if (const json::Value* x = e.find("when")) readFactCondition(*x, r.when);
            if (const json::Value* acts = e.find("then");
                acts && acts->type == json::Value::Type::Array) {
                for (const json::Value& a : acts->arr) {
                    facts::RuleAction ra;
                    if (const json::Value* x = a.find("do"))
                        ra.kind = facts::actionKindFromKey(x->stringOr("set"));
                    if (const json::Value* x = a.find("target"))
                        ra.target = x->stringOr("");
                    if (const json::Value* x = a.find("value"))
                        ra.value = (float)x->numberOr(0.0);
                    r.then.push_back(std::move(ra));
                }
            }
            out.factRules.push_back(std::move(r));
        }
    }

    if (const json::Value* arr = root.find("factScenarios");
        arr && arr->type == json::Value::Type::Array) {
        for (const json::Value& e : arr->arr) {
            facts::Scenario s;
            if (const json::Value* x = e.find("name")) s.name = x->stringOr("");
            if (s.name.empty()) continue;
            if (const json::Value* x = e.find("desc")) s.desc = x->stringOr("");
            if (const json::Value* vals = e.find("values");
                vals && vals->type == json::Value::Type::Array) {
                for (const json::Value& v : vals->arr) {
                    facts::ScenarioValue sv;
                    if (const json::Value* x = v.find("fact"))
                        sv.fact = x->stringOr("");
                    if (const json::Value* x = v.find("value"))
                        sv.value = (float)x->numberOr(0.0);
                    if (const json::Value* x = v.find("pos");
                        x && x->type == json::Value::Type::Array)
                        for (size_t a = 0; a < 3 && a < x->arr.size(); ++a)
                            sv.pos[a] = (float)x->arr[a].numberOr(0.0);
                    if (!sv.fact.empty()) s.values.push_back(std::move(sv));
                }
            }
            out.factScenarios.push_back(std::move(s));
        }
    }
}

static void readVuSection(const json::Value& root, Project& out) {
    out.vu = VuSettings();
    const json::Value* v = root.find("vu");
    if (!v || v->type != json::Value::Type::Object) return;
    if (const json::Value* x = v->find("residentClasses"))
        out.vu.residentClasses = (unsigned)x->numberOr(0x1F) & 0x1Fu;
    if (const json::Value* x = v->find("residentAuto"))
        out.vu.residentAuto = x->boolOr(true);
    if (const json::Value* sb = v->find("scriptBoot");
        sb && sb->type == json::Value::Type::Array)
        for (const json::Value& e : sb->arr) {
            const json::Value* n = e.find("name");
            if (!n) continue;
            const json::Value* on = e.find("on");
            out.vu.scriptBoot.push_back(
                {n->stringOr(""), on ? on->boolOr(true) : true});
        }
    if (const json::Value* x = v->find("activeLook"))
        out.vu.activeLook = (int)x->numberOr(0);
    const json::Value* arr = v->find("looks");
    if (arr && arr->type == json::Value::Type::Array)
        for (const json::Value& e : arr->arr) {
            VuProgram pr;
            if (const json::Value* x = e.find("name")) pr.name = x->stringOr("look");
            if (const json::Value* x = e.find("classes"))
                pr.classes = (unsigned)x->numberOr(1.0) & 0x1Fu;
            if (const json::Value* x = e.find("off")) pr.enabled = !x->boolOr(false);
            if (const json::Value* x = e.find("stages")) readVuStages(*x, pr.stages);
            if (pr.classes == 0) continue;
            out.vu.programs.push_back(std::move(pr));
        }
    if (const json::Value* k = v->find("kernel")) {
        if (const json::Value* x = k->find("name")) out.vu.kernel.name = x->stringOr("kernel");
        if (const json::Value* x = k->find("enabled"))
            out.vu.kernel.enabled = x->boolOr(false);
        if (const json::Value* x = k->find("maxElements"))
            out.vu.kernel.maxElements = (int)x->numberOr(112.0);
        if (const json::Value* x = k->find("stages"))
            readVuStages(*x, out.vu.kernel.stages);
    }
    // A look with no class is dropped above, so the boot index can outlive the
    // look it named. Codegen would fall back to look 0 anyway; clamping here
    // means the panel and the .tyra agree about which one that is.
    if (out.vu.activeLook < 0 ||
        out.vu.activeLook >= (int)out.vu.programs.size())
        out.vu.activeLook = 0;
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
        case Section::ModelAo: writeModelAoSection(ss, p); break;
        case Section::SaveData: writeSaveDataSection(ss, p); break;
        case Section::Gradings: writeGradingsSection(ss, p); break;
        case Section::Ambience: writeAmbienceSection(ss, p); break;
        case Section::LoadingScreens: writeLoadingScreensSection(ss, p); break;
        case Section::Splash: writeSplashSection(ss, p); break;
        case Section::Credits: writeCreditsSection(ss, p); break;
        case Section::Sequences: writeSequencesSection(ss, p); break;
        case Section::Menus: writeMenusSection(ss, p); break;
        case Section::AnimEdits: writeAnimEditsSection(ss, p); break;
        case Section::AnimImports: writeAnimImportsSection(ss, p); break;
        case Section::ModelUnits: writeModelUnitsSection(ss, p); break;
        case Section::Input: writeInputSection(ss, p); break;
        case Section::Prefabs: writePrefabsSection(ss, p); break;
        case Section::VuPrograms: writeVuSection(ss, p); break;
        case Section::Facts: writeFactsSection(ss, p); break;
        case Section::BlssShots: writeBlssShotsSection(ss, p); break;
        case Section::Count: break;  // not a section
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
        case Section::ModelAo: return "modelAo";
        case Section::SaveData: return "saveData";
        case Section::Gradings: return "gradings";
        case Section::Ambience: return "ambience";
        case Section::LoadingScreens: return "loadingScreens";
        case Section::Splash: return "splash";
        case Section::Credits: return "credits";
        case Section::Sequences: return "sequences";
        case Section::Menus: return "menus";
        case Section::AnimEdits: return "animEdits";
        case Section::AnimImports: return "animImports";
        case Section::ModelUnits: return "modelUnits";
        case Section::Input: return "input";
        case Section::Prefabs: return "prefabs";
        case Section::VuPrograms: return "vu";
        case Section::Facts: return "facts";
        case Section::BlssShots: return "blssShots";
        case Section::Count: break;  // not a section
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
         // The on-disk format contract (gates opening + migrations) and the
         // editor that wrote the file (informational only). See version.hpp.
         // These ride the collaboration wire too - manifestFiles() ships these
         // same bytes - so a peer sees the same stamp the file carries.
         << "  \"formatVersion\": " << version::kFormatVersion << ",\n"
         << "  \"editorVersion\": \"" << version::kEditorVersion << "\",\n"
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
         << ", \"viewMode\": " << p.viewMode
         << ", \"viewProjection\": " << p.viewProjection
         << ", \"showFog\": " << (p.viewShowFog ? "true" : "false")
         << ", \"cam\": [" << fmtFloat(p.viewCamYaw) << ", "
         << fmtFloat(p.viewCamPitch) << ", " << fmtFloat(p.viewCamDist) << ", "
         << fmtFloat(p.viewCamTarget[0]) << ", " << fmtFloat(p.viewCamTarget[1])
         << ", " << fmtFloat(p.viewCamTarget[2]) << "]"
         << ", \"breakpoints\": [";
    for (size_t i = 0; i < p.debugBreakpoints.size(); ++i)
        json << (i ? ", " : "") << "\"" << jsonEscape(p.debugBreakpoints[i])
             << "\"";
    json << "] }";
    // emulatorPath / ps2LinkIp are NOT written here: they are machine-global
    // editor settings (editor.ini), not project data.
    // Named window layouts (docking arrangements) + the active one.
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

void ensureFactIds(Project& p) {
    std::set<std::string> seen;
    for (facts::Fact& f : p.facts) {
        if (f.id.empty() || seen.count(f.id)) f.id = newObjectId();
        seen.insert(f.id);
    }
}

namespace {

// Every flow node whose STRING param is a fact name, found through the
// registry rather than a hardcoded list - so a fact node added tomorrow is
// covered by the rename and the usage scan today.
bool nodeNamesFact(const FlowNode& n) {
    const FlowNodeType* t = flowNodeType(n.type);
    return t && t->strKind == FlowParamKind::FactName;
}
bool nodeNamesQuery(const FlowNode& n) {
    const FlowNodeType* t = flowNodeType(n.type);
    return t && t->strKind == FlowParamKind::FactQueryName;
}

void pushUnique(std::vector<std::string>& v, const std::string& s) {
    for (const std::string& e : v)
        if (e == s) return;
    v.push_back(s);
}

// Walks every graph in the project (scene objects and prefab members alike -
// a prefab carries its members' graphs, so a fact used only inside one is
// still used).
template <typename Fn>
void forEachGraphNode(const Project& p, Fn&& fn) {
    for (size_t si = 0; si < p.scenes.size(); ++si)
        for (const SceneObject& o : p.scenes[si].objects)
            for (const FlowNode& n : o.flowGraph.nodes)
                fn(p.scenes[si].name, o.name, n);
    for (const Prefab& pf : p.prefabs)
        for (const SceneObject& o : pf.objects)
            for (const FlowNode& n : o.flowGraph.nodes)
                fn("prefab " + pf.name, o.name, n);
}

template <typename Fn>
void forEachGraphNodeMut(Project& p, Fn&& fn) {
    for (SceneData& sc : p.scenes)
        for (SceneObject& o : sc.objects)
            for (FlowNode& n : o.flowGraph.nodes) fn(n);
    for (Prefab& pf : p.prefabs)
        for (SceneObject& o : pf.objects)
            for (FlowNode& n : o.flowGraph.nodes) fn(n);
}

// Does this condition tree mention the fact anywhere in it (as either side of
// a comparison)? Query leaves are NOT followed - a query that uses the fact is
// reported as a user in its own right, which is the more useful answer.
bool conditionMentionsFact(const facts::Condition& c, const std::string& name) {
    if (c.kind == facts::Condition::Kind::Compare)
        return c.fact == name || c.rhsFact == name;
    for (const facts::Condition& ch : c.children)
        if (conditionMentionsFact(ch, name)) return true;
    return false;
}

bool conditionMentionsQuery(const facts::Condition& c, const std::string& name) {
    if (c.kind == facts::Condition::Kind::Query) return c.query == name;
    for (const facts::Condition& ch : c.children)
        if (conditionMentionsQuery(ch, name)) return true;
    return false;
}

void renameInCondition(facts::Condition& c, const std::string& from,
                       const std::string& to, bool query) {
    if (query) {
        if (c.kind == facts::Condition::Kind::Query && c.query == from)
            c.query = to;
    } else if (c.kind == facts::Condition::Kind::Compare) {
        if (c.fact == from) c.fact = to;
        if (c.rhsFact == from) c.rhsFact = to;
    }
    for (facts::Condition& ch : c.children) renameInCondition(ch, from, to, query);
}

}  // namespace

FactUsage factUsage(const Project& p, const std::string& factName) {
    FactUsage u;
    if (factName.empty()) return u;

    forEachGraphNode(p, [&](const std::string& scene, const std::string& obj,
                            const FlowNode& n) {
        if (nodeNamesFact(n) && n.str == factName)
            pushUnique(u.graphs, scene + " / " + obj);
    });
    for (const facts::Query& q : p.factQueries)
        if (conditionMentionsFact(q.root, factName)) pushUnique(u.queries, q.name);
    for (const facts::Rule& r : p.factRules) {
        bool used = conditionMentionsFact(r.when, factName);
        for (const facts::RuleAction& a : r.then)
            if (a.kind != facts::RuleAction::Kind::SendEvent &&
                a.target == factName)
                used = true;
        if (used) pushUnique(u.rules, r.name);
    }
    for (const facts::Scenario& s : p.factScenarios)
        for (const facts::ScenarioValue& v : s.values)
            if (v.fact == factName) pushUnique(u.scenarios, s.name);
    // A computed fact whose query reads this one depends on it too - the
    // indirection is exactly what makes it easy to miss by hand.
    for (const facts::Fact& f : p.facts) {
        if (!f.isComputed()) continue;
        const int qi = facts::queryIndexOf(p.factQueries, f.computed);
        if (qi < 0) continue;
        std::vector<std::string> reads;
        facts::conditionFacts(p.factQueries[(size_t)qi].root, p.factQueries,
                              reads);
        for (const std::string& r : reads)
            if (r == factName) pushUnique(u.computed, f.name);
    }
    return u;
}

FactUsage queryUsage(const Project& p, const std::string& queryName) {
    FactUsage u;
    if (queryName.empty()) return u;

    forEachGraphNode(p, [&](const std::string& scene, const std::string& obj,
                            const FlowNode& n) {
        if (nodeNamesQuery(n) && n.str == queryName)
            pushUnique(u.graphs, scene + " / " + obj);
    });
    for (const facts::Query& q : p.factQueries)
        if (q.name != queryName && conditionMentionsQuery(q.root, queryName))
            pushUnique(u.queries, q.name);
    for (const facts::Rule& r : p.factRules)
        if (conditionMentionsQuery(r.when, queryName)) pushUnique(u.rules, r.name);
    for (const facts::Fact& f : p.facts)
        if (f.computed == queryName) pushUnique(u.computed, f.name);
    return u;
}

void renameFactRefs(Project& p, const std::string& from, const std::string& to) {
    if (from.empty() || to.empty() || from == to) return;
    forEachGraphNodeMut(p, [&](FlowNode& n) {
        if (nodeNamesFact(n) && n.str == from) n.str = to;
    });
    for (facts::Query& q : p.factQueries)
        renameInCondition(q.root, from, to, false);
    for (facts::Rule& r : p.factRules) {
        renameInCondition(r.when, from, to, false);
        for (facts::RuleAction& a : r.then)
            if (a.kind != facts::RuleAction::Kind::SendEvent && a.target == from)
                a.target = to;
    }
    for (facts::Scenario& s : p.factScenarios)
        for (facts::ScenarioValue& v : s.values)
            if (v.fact == from) v.fact = to;
}

void renameFactQueryRefs(Project& p, const std::string& from,
                         const std::string& to) {
    if (from.empty() || to.empty() || from == to) return;
    forEachGraphNodeMut(p, [&](FlowNode& n) {
        if (nodeNamesQuery(n) && n.str == from) n.str = to;
    });
    for (facts::Query& q : p.factQueries)
        renameInCondition(q.root, from, to, true);
    for (facts::Rule& r : p.factRules) renameInCondition(r.when, from, to, true);
    for (facts::Fact& f : p.facts)
        if (f.computed == from) f.computed = to;
}

const char* inputRoleName(int role) {
    switch (role) {
        case InputAction::RoleJump: return "jump";
        case InputAction::RoleUse: return "use";
        case InputAction::RoleThrow: return "throw";
        case InputAction::RoleSprint: return "sprint";
        case InputAction::RoleFlyUp: return "fly-up";
        case InputAction::RoleFlyDown: return "fly-down";
        case InputAction::RoleConfirm: return "confirm";
        case InputAction::RoleBack: return "back";
        case InputAction::RoleMenu: return "menu";
        case InputAction::RoleAlt: return "alt";
        case InputAction::RoleMenuUp: return "menu-up";
        case InputAction::RoleMenuDown: return "menu-down";
        case InputAction::RoleMenuLeft: return "menu-left";
        case InputAction::RoleMenuRight: return "menu-right";
        case InputAction::RoleMoveForward: return "move-forward";
        case InputAction::RoleMoveBack: return "move-back";
        case InputAction::RoleMoveLeft: return "move-left";
        case InputAction::RoleMoveRight: return "move-right";
        default: return "";
    }
}

// --- the neural upscaler's training-shot plan --------------------------------

const char* blssAutoMoveName(int move) {
    switch ((BlssAutoMove)move) {
        case BlssAutoMove::Walk: return "walk";
        case BlssAutoMove::Pan: return "pan";
        case BlssAutoMove::Orbit: return "orbit";
        case BlssAutoMove::Whip: return "whip";
        case BlssAutoMove::Pitch: return "pitch";
        case BlssAutoMove::Strafe: return "strafe";
        default: return "";
    }
}

// The twin of the `s.move = "..."` literals in blssscene.cpp's autoShots(). It
// is here rather than there because the window has to LABEL a move the same way
// the tool's own per-shot table does, and a second spelling would make the two
// tables un-joinable by eye.
const char* blssAutoMoveKind(int move) {
    switch ((BlssAutoMove)move) {
        case BlssAutoMove::Walk: return "dolly-forward";
        case BlssAutoMove::Pan: return "pan";
        case BlssAutoMove::Orbit: return "orbit";
        case BlssAutoMove::Whip: return "whip";
        case BlssAutoMove::Pitch: return "pitch-up";
        case BlssAutoMove::Strafe: return "dolly-lateral";
        default: return "";
    }
}

const char* blssAutoMoveWhy(int move) {
    switch ((BlssAutoMove)move) {
        case BlssAutoMove::Walk:
            return "What the player sees for most of the running time - forward from the "
                   "player start.";
        case BlssAutoMove::Pan:
            return "A yaw sweep from one standpoint: the same content at every reprojection "
                   "offset a stick can produce.";
        case BlssAutoMove::Orbit:
            return "The only move that sweeps silhouettes across the whole tile grid.";
        case BlssAutoMove::Whip:
            return "Eased, so the angular velocity peaks mid-shot: history that is fine, "
                   "history that is useless, and both transitions.";
        case BlssAutoMove::Pitch:
            return "Sweeps coverage from 1 to nearly 0, which is what makes the empty-tile "
                   "case a moving target rather than a corner.";
        case BlssAutoMove::Strafe:
            return "Real parallax - near geometry sliding across far, which a tile's single "
                   "representative depth cannot reproject.";
        default: return "";
    }
}

int blssShotScene(const Project& p, const BlssShot& s) {
    if (p.scenes.empty()) return -1;
    if (s.scene.empty()) return 0;
    for (size_t i = 0; i < p.scenes.size(); ++i)
        if (p.scenes[i].name == s.scene) return (int)i;
    return -1;
}

std::string blssShotLabel(const Project& p, const BlssShot& s, int index) {
    if (!s.name.empty()) return s.name;
    const int si = blssShotScene(p, s);
    const std::string scene = si >= 0 ? p.scenes[(size_t)si].name : std::string("?");
    return scene + " shot " + std::to_string(index + 1);
}

bool blssResolveShot(const Project& p, const BlssShot& s, float eyeA[3], float lookA[3],
                     float eyeB[3], float lookB[3], float* fovDeg) {
    if (!s.enabled) return false;
    const int si = blssShotScene(p, s);
    if (si < 0) return false;
    const SceneData& sc = p.scenes[(size_t)si];
    float fov = s.fovDeg;
    // A Camera object gives both the standpoint and the aim, which is the whole
    // reason the field exists: the author has already placed the camera where
    // the player stands, and re-typing its numbers into this shot would be a
    // second copy that goes stale the moment the camera is nudged.
    const auto fromCamera = [&](const std::string& name, float eye[3], float look[3]) {
        for (const SceneObject& o : sc.objects) {
            if (o.type != PrimitiveType::Camera || o.name != name) continue;
            float fwd[3];
            seqCameraForward(o.rotation, fwd);
            for (int k = 0; k < 3; ++k) {
                eye[k] = o.position[k];
                look[k] = eye[k] + fwd[k];
            }
            fov = o.cameraFov;
            return true;
        }
        return false;
    };
    if (!s.camera.empty()) {
        if (!fromCamera(s.camera, eyeA, lookA)) return false;
    } else {
        for (int k = 0; k < 3; ++k) eyeA[k] = s.eye[k], lookA[k] = s.look[k];
    }
    // The second key. A `cameraTo` that names nothing is the same class of
    // mistake as a `camera` that does - drop the shot rather than shoot half
    // of it, because a move that silently became a still is a corpus that
    // silently stopped covering the motion the author asked for.
    if (s.move) {
        if (!s.cameraTo.empty()) {
            if (!fromCamera(s.cameraTo, eyeB, lookB)) return false;
        } else {
            for (int k = 0; k < 3; ++k) eyeB[k] = s.eye2[k], lookB[k] = s.look2[k];
        }
    } else {
        for (int k = 0; k < 3; ++k) eyeB[k] = eyeA[k], lookB[k] = lookA[k];
    }
    // A key whose eye and look-at coincide has no direction at all, and the
    // corpus would normalise a zero vector.
    const auto degenerate = [](const float e[3], const float l[3]) {
        const float d[3] = {l[0] - e[0], l[1] - e[1], l[2] - e[2]};
        return d[0] * d[0] + d[1] * d[1] + d[2] * d[2] < 1e-8f;
    };
    if (degenerate(eyeA, lookA) || degenerate(eyeB, lookB)) return false;
    if (fovDeg) *fovDeg = fov;
    return true;
}

void ensureTextIcons(Project& p) {
    // One entry per pad button, named after it (lowercased) - that convention
    // is what makes {{cross}} and {{action:jump}} resolve. Their PNGs are
    // generated into res/hud/ by saveAssets when missing, so an override is
    // just replacing the file.
    for (const std::string& name : menubake::builtinIconNames()) {
        bool have = false;
        for (const TextIcon& ic : p.textIcons) have |= (ic.name == name);
        if (have) continue;
        TextIcon ic;
        ic.name = name;
        ic.path = "res/hud/" + menubake::iconFileName(name);
        p.textIcons.push_back(std::move(ic));
    }
}

int saveMenuIndex(const Project& p) {
    for (size_t i = 0; i < p.menus.size(); ++i)
        if (p.menus[i].saveMenu) return (int)i;
    return -1;
}

void ensureSaveMenu(Project& p) {
    // More than one is a data error, not a choice: keep the first.
    bool seen = false;
    for (GameMenu& m : p.menus) {
        if (!m.saveMenu) continue;
        if (seen) m.saveMenu = false;
        seen = true;
    }
    if (seen) return;
    // Seeded to match the panel that used to ship as res/hud/save-menu.png:
    // same title, same blue, same 256-wide panel, so a project that predates
    // this looks exactly as it did.
    GameMenu m;
    m.name = "save";
    m.title = "SAVE GAME";
    m.saveMenu = true;
    m.pauseGame = true;
    m.panelW = 256;
    m.screenPos[0] = 0.5f;
    m.screenPos[1] = 0.45f;
    m.accent[0] = 0.47f;
    m.accent[1] = 0.82f;
    m.accent[2] = 1.0f;
    m.titleSize = 18;
    m.entrySize = 15;
    // No entries: the rows ARE the save slots and are laid out from
    // Project::saveSlotsPerPage at bake time.
    p.menus.push_back(m);
}

void ensureInputActions(Project& p) {
    // The bindings TyraX hardcoded before the Input Map existed (the old
    // controls.hpp defaults - see docs/keyboard-mouse.md), plus the new sprint
    // action. Order is the order the Input Map window and a scaffolded controls
    // menu list them in.
    struct Seed {
        int role;
        const char* label;
        const char* pad;
        int key;
        int mouse;
        bool rebindable;
    };
    static const Seed kSeeds[] = {
        {InputAction::RoleMoveForward, "Move forward", "", 0x1A, 0, true},
        {InputAction::RoleMoveBack, "Move back", "", 0x16, 0, true},
        {InputAction::RoleMoveLeft, "Move left", "", 0x04, 0, true},
        {InputAction::RoleMoveRight, "Move right", "", 0x07, 0, true},
        {InputAction::RoleJump, "Jump", "Cross", 0x2C, 2, true},
        {InputAction::RoleSprint, "Sprint", "R2", 0xE1, 0, true},
        {InputAction::RoleUse, "Use", "Square", 0x08, 1, true},
        {InputAction::RoleThrow, "Throw", "Circle", 0, 3, true},
        {InputAction::RoleFlyUp, "Fly up", "Cross", 0x2C, 0, true},
        {InputAction::RoleFlyDown, "Fly down", "Square", 0x08, 0, true},
        // The menu set is deliberately NOT rebindable by default: a player who
        // rebinds "Confirm" to a button they cannot reach is locked out of the
        // menu they would need to fix it.
        {InputAction::RoleConfirm, "Confirm", "Cross", 0x28, 0, false},
        {InputAction::RoleBack, "Back", "Triangle", 0x2A, 0, false},
        {InputAction::RoleMenu, "Pause menu", "Start", 0x29, 0, false},
        {InputAction::RoleAlt, "Alternate", "Circle", 0x15, 3, false},
        {InputAction::RoleMenuUp, "Menu up", "DpadUp", 0x52, 0, false},
        {InputAction::RoleMenuDown, "Menu down", "DpadDown", 0x51, 0, false},
        {InputAction::RoleMenuLeft, "Menu left", "DpadLeft", 0x50, 0, false},
        {InputAction::RoleMenuRight, "Menu right", "DpadRight", 0x4F, 0, false},
    };

    if (p.input.presets.empty()) p.input.presets.push_back(InputPreset{});
    if (p.input.activePreset < 0 ||
        p.input.activePreset >= (int)p.input.presets.size())
        p.input.activePreset = 0;

    for (const Seed& s : kSeeds) {
        const char* name = inputRoleName(s.role);
        // A role already covered (even under a user-chosen name) is left alone.
        int idx = p.input.roleIndex(s.role);
        if (idx < 0 && p.input.findAction(name)) continue;
        if (idx < 0) {
            InputAction a;
            a.name = name;
            a.label = s.label;
            a.role = s.role;
            a.rebindable = s.rebindable;
            p.input.actions.push_back(std::move(a));
            idx = (int)p.input.actions.size() - 1;
            // A newly seeded action needs its default binding in EVERY preset,
            // not just the active one - otherwise switching preset unbinds it.
            for (InputPreset& pr : p.input.presets) {
                InputBinding& b = pr.at(p.input.actions[idx].name);
                b.pad = s.pad;
                b.key = s.key;
                b.mouse = s.mouse;
            }
        }
    }
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
    // "debugger" in the DEFAULT layout: the panel is open from the first run of
    // a new project, docked as a tab behind Properties (buildLayoutRecipe), so
    // the first Build & Run has somewhere to report instead of the debugger
    // being a thing you have to know to go and open. Properties is the selected
    // tab, so an open Debugger costs one tab header until it has something to
    // say. Existing projects keep the layouts saved in their .tyra.
    p.windowLayouts.push_back(
        {"Default", "", (int)LayoutRecipe::Default, {"debugger"}});
    p.windowLayouts.push_back(
        {"Director", "", (int)LayoutRecipe::Director, {"cutscene"}});
    p.windowLayouts.push_back(
        {"Material Designer", "", (int)LayoutRecipe::Material, {"material"}});
    p.windowLayouts.push_back(
        {"Debugger", "", (int)LayoutRecipe::Debugger, {"debugger"}});
    p.windowLayouts.push_back(
        {"Procedural", "", (int)LayoutRecipe::Procedural, {"proc", "prefabs"}});
    p.windowLayouts.push_back({"Menu Designer", "", (int)LayoutRecipe::MenuDesigner,
                               {"menus", "menupreview", "fonts"}});
    p.activeLayout = 0;
}

std::string create(Project& out, const std::string& name, const std::string& parentDir,
                   const TerrainConfig& terrain, const std::string& preset,
                   float unitsPerMeter) {
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

    // New-project defaults that deliberately differ from the struct defaults -
    // those are what a project predating each key loads as, so they cannot
    // carry the new answer without changing existing projects' behavior.
    //
    // A fresh project is born for authoring: the debug profile so Live Link and
    // the on-screen overlays work from the first build (switch to release for
    // the disc), and USB keyboard & mouse OFF - a pad game pays nothing for
    // drivers it never uses, and the choice is one to make deliberately.
    out.settings.buildProfile = "debug";
    out.settings.liveLink = true;
    out.settings.keyboardMouse = false;

    // PAL picture ON: on a PAL console the region-following interlaced mode
    // boots the full-height 576i frame (512 rendered lines) instead of the
    // letterboxed NTSC-sized picture - the "full PAL" of European releases,
    // and the whole point of a 50 Hz signal. It costs ~380 KB of GS VRAM and
    // nothing at all on an NTSC console, which still gets its own 448 lines.
    // Existing projects keep the letterboxed picture (the field reads as false)
    // because turning it on retroactively would change what they output.
    out.settings.palFullHeight = true;

    // World scale, chosen when the project is created because the alternative
    // is discovering it after the world is built. The metric-by-definition
    // numbers scale with it, so the FPP preset is a 1.8 m player at any scale
    // (docs/world-scale.md); everything else is in units by nature and stays.
    if (!(unitsPerMeter > 0.0001f)) unitsPerMeter = 1.0f;
    out.settings.unitsPerMeter = unitsPerMeter;
    out.settings.eyeHeight *= unitsPerMeter;
    out.settings.walkSpeed *= unitsPerMeter;
    out.settings.gravity *= unitsPerMeter;
    out.settings.jumpSpeed *= unitsPerMeter;

    // Start with one ambience preset (its defaults match the project's default
    // sky/lighting/fog) so the sky renders and the Ambience Editor isn't empty.
    // New projects get baked ambient occlusion out of the box; loaded pre-AO
    // projects keep their look (the field defaults to off on read).
    AmbiencePreset amb;
    amb.name = "Default";
    amb.aoEnabled = true;
    out.ambiencePresets.push_back(amb);
    // ...and automatic model AO, for the same reason and by the same rule: the
    // struct default is what an older file loads as, so the new answer belongs
    // here (docs/ambient-occlusion.md, "Model AO").
    out.settings.modelAo = true;
    out.defaultAmbience = 0;

    // Seed the built-in window layouts (Default/Director/Material Designer).
    seedBuiltinLayouts(out);

    // Three presets: "fpp" and "thirdperson" (the player-entity game template,
    // differing only in the seeded Player's mode) and "empty" (no objects).
    // Anything else is treated as empty. The choice is permanent - see
    // Project::gameTemplate.
    const bool thirdPerson = preset == "thirdperson";
    const bool player1st = preset == "fpp";
    out.gameTemplate = thirdPerson ? "thirdperson" : player1st ? "fpp" : "orbit";

    if (!thirdPerson && !player1st) {
        // An empty project starts EMPTY: nothing in the scene and a camera that
        // does not move on its own. The template's automatic orbit is what a
        // scene with no Player object falls back to, and `orbitSpeed = 0` parks
        // it at a fixed vantage point looking at the origin - so the first
        // thing the project does is whatever the user adds (a Player, a script,
        // a Camera object, a cutscene), not a demo turntable. Set here rather
        // than in the struct initializer, which is what projects saved before
        // this load as (they keep their turntable).
        out.settings.orbitSpeed = 0.0f;
    }

    if (thirdPerson || player1st) {
        // The player entity: the camera becomes this player at game start.
        SceneObject player;
        player.name = "player-1";
        player.type = PrimitiveType::Player;
        // Walk (FPP) vs third person - the only difference between the two
        // player presets. A third-person avatar is the object's OWN animated
        // model; it starts without one (the rig and the movement work anyway,
        // the model is dropped in from Properties later).
        player.playerMode = thirdPerson ? 2 : 0;
        player.position[0] = 0.0f, player.position[1] = 0.0f, player.position[2] = 0.0f;
        player.color[0] = 0.15f, player.color[1] = 0.9f, player.color[2] = 0.9f;
        // Same reasoning as the settings above: these are metres by definition
        // (a 1.8 m person running 5 m/s), so they follow the world scale.
        player.playerEyeHeight *= unitsPerMeter;
        player.playerWalkSpeed *= unitsPerMeter;
        player.playerJumpSpeed *= unitsPerMeter;
        player.playerCamDist *= unitsPerMeter;
        player.playerCamHeight *= unitsPerMeter;
        out.scenes[0].objects.push_back(player);
    }

    ensureProjectId(out);
    ensureObjectIds(out);
    ensureInputActions(out);
    ensureFactIds(out);
    ensureSaveMenu(out);
    ensureTextIcons(out);
    // A fresh project's USE prompt is TEXT carrying the button glyph, so it says
    // what to press rather than a generic "USE" - and follows a rebind. Only on
    // create: flipping an existing project from its image to text would restyle
    // it behind the user's back (the UI Editor offers the switch instead).
    // Both prompts start as TEXT (the defaults carry the button glyph); the
    // strings themselves come from the members' initializers.
    out.usePromptIsText = true;
    out.pickPromptIsText = true;
    ensureHeightmap(out);

    for (const auto& f : templates::generate(out)) {
        if (auto err = writeFile(root / templates::nativePath(f.relativePath), f.content);
            !err.empty())
            return err;
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

// One heights file per scene: terrain-<scene>.heights.
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
    // The start scene indexes the list this message carries, so it travels with
    // it. Absent means 0 - a peer on an older build simply boots the first.
    p.startScene = 0;
    if (const auto* v = root.find("startScene")) p.startScene = (int)v->numberOr(0.0);

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
            if (const auto* v = t->find("enabled")) sc.terrain.enabled = v->boolOr(true);
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
    clampStartScene(p);
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

// --- Areas (docs/areas.md) --------------------------------------------------

namespace {

struct AV3 {
    float x, y, z;
};

// Rotation order X, then Y, then Z - the same convention as the viewport's
// model matrix, aobake::rotated and the generated game's rotated(). Keep in
// sync.
AV3 areaRotated(const AV3& v, const float* rotDeg) {
    AV3 r = v;
    const float d2r = 3.14159265358979323846f / 180.0f;
    const float rx = rotDeg[0] * d2r, ry = rotDeg[1] * d2r, rz = rotDeg[2] * d2r;
    {
        const float c = std::cos(rx), s = std::sin(rx);
        const float y = r.y * c - r.z * s, z = r.y * s + r.z * c;
        r.y = y, r.z = z;
    }
    {
        const float c = std::cos(ry), s = std::sin(ry);
        const float x = r.x * c + r.z * s, z = -r.x * s + r.z * c;
        r.x = x, r.z = z;
    }
    {
        const float c = std::cos(rz), s = std::sin(rz);
        const float x = r.x * c - r.y * s, y = r.x * s + r.y * c;
        r.x = x, r.y = y;
    }
    return r;
}

// Squared distance from a world point to the area's oriented box, 0 inside.
// The rotated unit axes are orthonormal, so projecting the offset onto each and
// clamping to the half extent gives the closest point without a matrix inverse.
float areaDistSq(const SceneObject& area, float x, float y, float z) {
    const AV3 d{x - area.position[0], y - area.position[1], z - area.position[2]};
    const AV3 ax[3] = {areaRotated({1, 0, 0}, area.rotation),
                       areaRotated({0, 1, 0}, area.rotation),
                       areaRotated({0, 0, 1}, area.rotation)};
    float out = 0.0f;
    for (int k = 0; k < 3; ++k) {
        const float half = 0.5f * std::fabs(area.scale[k]);
        const float t = d.x * ax[k].x + d.y * ax[k].y + d.z * ax[k].z;
        const float over = std::fabs(t) - half;
        if (over > 0.0f) out += over * over;
    }
    return out;
}

}  // namespace

const SceneObject* findArea(const std::vector<SceneObject>& objs,
                            const std::string& name) {
    if (name.empty()) return nullptr;
    for (const SceneObject& o : objs)
        if (o.type == PrimitiveType::Area && o.name == name) return &o;
    return nullptr;
}

unsigned vuClassOfObject(const Project& p, const SceneObject& o) {
    // Markers, areas, cameras and the rest draw nothing, so they belong to no
    // class - and must not be offered VU parameters.
    switch (o.type) {
        case PrimitiveType::Box:
        case PrimitiveType::Sphere:
        case PrimitiveType::Cylinder:
        case PrimitiveType::Cone:
        case PrimitiveType::Plane:
        case PrimitiveType::Model:
            break;
        default:
            return 0;
    }
    const bool lit = o.dynamicLighting;
    const bool textured = !o.materialPath.empty() || !o.modelPath.empty();
    if (!o.materialPath.empty()) {
        // A `refl` statement is what makes a material a matcap - the same line
        // objparser and the engine's lean_obj_loader read. It wins over
        // everything: the engine checks coordinatesAreNormals first.
        std::ifstream in(p.filePath(o.materialPath), std::ios::binary);
        if (in) {
            std::string line;
            while (std::getline(in, line)) {
                size_t a = line.find_first_not_of(" 	");
                if (a == std::string::npos) continue;
                if (line.compare(a, 5, "refl ") == 0) return 1u << 4;
            }
        }
    }
    if (lit && textured) return 1u << 2;
    if (lit) return 1u << 1;
    if (textured) return 1u << 3;
    return 1u << 0;
}

bool vuClassCanBind(unsigned classBit) {
    // Colour, Textured and Reflective carry no lighting, so the
    // directional-lights colour block - where the per-mesh quadword and the
    // clock live - is free in them and ONLY in them. A look made of plain
    // values never reads those addresses and may go anywhere, which is what
    // lets a scene-wide treatment reach lit geometry at all.
    return classBit == (1u << 0) || classBit == (1u << 3) ||
           classBit == (1u << 4);
}

bool vuLookBindsPerMesh(const VuProgram& look) {
    for (const VuStage& st : look.stages) {
        if (!st.enabled) continue;
        for (int i = 0; i < 4; ++i)
            if (st.bind[i] >= 0) return true;
    }
    return false;
}

// True when any stage of the look displaces the vertex. Such a stage cannot run
// on the as_is twin (those vertices are already transformed), so the look stops
// at a package the frustum cut - the panel and the inspector both say so.
bool vuLookMovesGeometry(const VuProgram& look) {
    for (const VuStage& st : look.stages) {
        if (!st.enabled) continue;
        const vugen::StageDef* d = vugen::stageDef(st.kind);
        if (d && (d->slot == vugen::Slot::ObjectSpace ||
                  d->slot == vugen::Slot::ClipSpace ||
                  d->slot == vugen::Slot::Ndc))
            return true;
    }
    return false;
}

const char* vuClassName(unsigned classBit) {
    // One list, in vugen - the generated file headers name the class too.
    return vugen::classTitle(classBit);
}

unsigned vuNeededClasses(const Project& p) {
    unsigned mask = 1u << 0;  // colour: the fallback floor, always resident
    auto scan = [&](const SceneObject& o) { mask |= vuClassOfObject(p, o); };
    for (const SceneData& sc : p.scenes)
        for (const SceneObject& o : sc.objects) scan(o);
    // A prefab member can reach the world without being in any scene (stamped
    // by a procedural volume, spawned by a flow node), and a class that is not
    // resident when it appears draws in the wrong style.
    for (const Prefab& pf : p.prefabs)
        for (const SceneObject& o : pf.objects) scan(o);
    return mask;
}

unsigned vuResidentClasses(const Project& p) {
    return p.vu.residentAuto ? vuNeededClasses(p)
                             : (p.vu.residentClasses | 1u);
}

bool areaContainsPoint(const SceneObject& area, float x, float y, float z) {
    return areaDistSq(area, x, y, z) <= 0.0f;
}

bool areaCatchable(PrimitiveType t) {
    switch (t) {
        case PrimitiveType::Box:
        case PrimitiveType::Sphere:
        case PrimitiveType::Cylinder:
        case PrimitiveType::Cone:
        case PrimitiveType::Plane:
        case PrimitiveType::SavePoint:
        case PrimitiveType::Model:
        case PrimitiveType::Decal:
            return true;
        default:
            return false;  // markers, lights, cameras, mirrors, portals, areas
    }
}

std::vector<int> areaCaughtObjects(const std::vector<SceneObject>& objs,
                                   const std::string& areaName, int exclude) {
    std::vector<int> out;
    const SceneObject* area = findArea(objs, areaName);
    if (!area) return out;
    for (size_t i = 0; i < objs.size(); ++i) {
        if ((int)i == exclude) continue;
        const SceneObject& o = objs[i];
        if (!areaCatchable(o.type)) continue;
        // Bounding sphere = half the largest scale axis, so a prop only
        // partly inside still counts (a strict center test would drop a wide
        // crate the author clearly meant to include).
        float r = std::fabs(o.scale[0]);
        r = std::max(r, std::fabs(o.scale[1]));
        r = std::max(r, std::fabs(o.scale[2]));
        r *= 0.5f;
        if (areaDistSq(*area, o.position[0], o.position[1], o.position[2]) <= r * r)
            out.push_back((int)i);
    }
    return out;
}

std::set<std::string> runtimeRefNames(const Project& p,
                                      const std::vector<SceneObject>& objs) {
    std::set<std::string> refs;
    for (const SceneObject& o : objs) {
        for (const FlowNode& n : o.flowGraph.nodes) {
            const FlowNodeType* t = flowNodeType(n.type);
            if (t && t->strKind == FlowParamKind::ObjectName && !n.str.empty())
                refs.insert(n.str);
        }
        if (o.type == PrimitiveType::Mirror)
            for (const std::string& m : o.mirrorObjects) refs.insert(m);
        if (o.type == PrimitiveType::Portal)
            for (const std::string& m : o.portalObjects) refs.insert(m);
    }
    for (const Sequence& s : p.sequences) {
        for (const SeqTrack& tr : s.tracks) refs.insert(tr.target);
        for (const SeqCameraKey& k : s.cameraKeys) refs.insert(k.camera);
    }
    return refs;
}

bool objectRuntimeMovable(const SceneObject& o,
                          const std::set<std::string>& refs) {
    if (o.physics) return true;    // gravity, bounces, gets pushed
    if (o.pickable) return true;   // carried in front of the camera, thrown
    if (o.usable) return true;     // the highlight defers and re-submits it
    if (o.saveState) return true;  // a loaded save repositions it
    if (!o.layer.empty()) return true;  // streams in and out of the world
    // Per-object logic: the graph can move self, attached scripts get a
    // per-frame hook on this object.
    if (!o.flowGraph.nodes.empty() || !o.scripts.empty()) return true;
    return refs.find(o.name) != refs.end();
}

std::vector<int> areaLiveCandidates(const std::vector<SceneObject>& objs,
                                    int exclude,
                                    const std::set<std::string>& refs) {
    std::vector<int> out;
    for (size_t i = 0; i < objs.size(); ++i) {
        if ((int)i == exclude) continue;
        if (!areaCatchable(objs[i].type)) continue;
        if (objectRuntimeMovable(objs[i], refs)) out.push_back((int)i);
    }
    return out;
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
        // No key = off, which is what every project written before axial rings
        // existed means: the classic single-quad cylinder side.
        if (const auto* v = jo.find("rings")) o.primRings = v->boolOr(false);
        if (const auto* v = jo.find("drawDistance")) {
            o.drawDistance = (float)v->numberOr(0.0);
            if (o.drawDistance < 0.0f) o.drawDistance = 0.0f;
        }
        if (const auto* v = jo.find("reflected")) o.reflected = v->boolOr(false);
        if (const auto* v = jo.find("castShadow")) o.castShadow = v->boolOr(true);
        if (const auto* v = jo.find("bakedLighting"))
            o.bakedLighting = v->boolOr(true);
        if (const auto* v = jo.find("dynamicLighting"))
            o.dynamicLighting = v->boolOr(false);
        if (const auto* v = jo.find("prelit")) o.prelit = v->boolOr(false);
        if (const auto* v = jo.find("prelitWanted"))
            o.prelitWanted = v->boolOr(false);
        if (const auto* v = jo.find("prelitSig"))
            o.prelitSig = parseHex64(v->stringOr(""));
        if (const auto* v = jo.find("prelitSource"))
            o.prelitSource = v->stringOr("");
        if (const auto* v = jo.find("projShadow")) o.projShadow = v->boolOr(false);
        if (const auto* v = jo.find("model")) o.modelPath = v->stringOr("");
        if (const auto* v = jo.find("material")) o.materialPath = v->stringOr("");
        if (const auto* v = jo.find("decalProject")) o.decalProject = v->boolOr(false);
        if (const auto* pl = jo.find("player")) {
            if (const auto* v = pl->find("mode")) {
                const std::string m = v->stringOr("walk");
                o.playerMode = m == "noclip" ? 1 : (m == "thirdperson" ? 2 : 0);
            }
            if (const auto* tp = pl->find("thirdPerson")) {
                if (const auto* v = tp->find("idleClip")) o.playerIdleClip = v->stringOr("");
                if (const auto* v = tp->find("walkClip")) o.playerWalkClip = v->stringOr("");
                if (const auto* v = tp->find("runClip")) o.playerRunClip = v->stringOr("");
                if (const auto* v = tp->find("sprintClip"))
                    o.playerSprintClip = v->stringOr("");
                if (const auto* v = tp->find("jumpClip")) o.playerJumpClip = v->stringOr("");
                if (const auto* v = tp->find("backClip")) o.playerBackClip = v->stringOr("");
                if (const auto* v = tp->find("strafeLeftClip"))
                    o.playerStrafeLeftClip = v->stringOr("");
                if (const auto* v = tp->find("strafeRightClip"))
                    o.playerStrafeRightClip = v->stringOr("");
                if (const auto* v = tp->find("faceCamera"))
                    o.playerFaceCamera = v->boolOr(false);
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
                o.playerWalkSpeed = (float)v->numberOr(0.1);
            // Absent = 0 = inherit, which is what a project written before
            // these existed means and what it must keep meaning.
            if (const auto* v = pl->find("runSpeed"))
                o.playerRunSpeed = (float)v->numberOr(0.0);
            if (const auto* v = pl->find("sprintSpeed"))
                o.playerSprintSpeed = (float)v->numberOr(0.0);
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
                if (const auto* v = fl->find("texture"))
                    o.flashlightTexture = v->stringOr("");
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
            if (o.emitterOpacity < 0.0f) o.emitterOpacity = 0.0f;
            if (o.emitterOpacity > 1.0f) o.emitterOpacity = 1.0f;
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
            if (const auto* v = sn->find("reverb"))
                o.soundReverb = !(v->type == json::Value::Type::Bool && !v->boolean);
            if (const auto* v = sn->find("priority"))
                o.soundPriority = (int)v->numberOr(0.0);
        }
        // Reverb zone (Area). The key only exists on a zone, so its presence
        // IS the flag - an area saved before this feature simply isn't one.
        if (const auto* rv = jo.find("reverb");
            rv && rv->type == json::Value::Type::Object) {
            o.reverbZone = true;
            if (const auto* v = rv->find("preset")) o.reverbPreset = (int)v->numberOr(1.0);
            if (o.reverbPreset < 0 || o.reverbPreset > 9) o.reverbPreset = 1;
            if (const auto* v = rv->find("amount"))
                o.reverbAmount = (float)v->numberOr(0.5);
            if (o.reverbAmount < 0.0f) o.reverbAmount = 0.0f;
            if (o.reverbAmount > 1.0f) o.reverbAmount = 1.0f;
            if (const auto* v = rv->find("delay")) o.reverbDelay = (int)v->numberOr(64.0);
            if (o.reverbDelay < 0) o.reverbDelay = 0;
            if (o.reverbDelay > 127) o.reverbDelay = 127;
            if (const auto* v = rv->find("feedback"))
                o.reverbFeedback = (int)v->numberOr(64.0);
            if (o.reverbFeedback < 0) o.reverbFeedback = 0;
            if (o.reverbFeedback > 127) o.reverbFeedback = 127;
            if (const auto* v = rv->find("priority"))
                o.reverbPriority = (int)v->numberOr(0.0);
        }
        if (const auto* lt = jo.find("light")) {
            if (const auto* v = lt->find("brightness"))
                o.lightBright = (float)v->numberOr(1.0);
            if (const auto* v = lt->find("radius"))
                o.lightRadius = (float)v->numberOr(8.0);
            if (o.lightRadius < 0.1f) o.lightRadius = 0.1f;
            if (const auto* v = lt->find("dynamic"))
                o.lightDynamic = v->type == json::Value::Type::Bool && v->boolean;
            if (const auto* v = lt->find("flicker"))
                o.lightFlicker = (float)v->numberOr(0.0);
            if (o.lightFlicker < 0.0f) o.lightFlicker = 0.0f;
            if (o.lightFlicker > 1.0f) o.lightFlicker = 1.0f;
            if (const auto* v = lt->find("spot"))
                o.lightSpot = v->type == json::Value::Type::Bool && v->boolean;
            if (const auto* v = lt->find("spotAngle"))
                o.lightSpotAngle = (float)v->numberOr(25.0);
            if (o.lightSpotAngle < 5.0f) o.lightSpotAngle = 5.0f;
            if (o.lightSpotAngle > 60.0f) o.lightSpotAngle = 60.0f;
            if (const auto* v = lt->find("beam"))
                o.lightBeam = (int)v->numberOr(0.0);
            if (o.lightBeam < 0 || o.lightBeam > 2) o.lightBeam = 0;
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
        if (const auto* v = jo.find("catchArea")) o.catchArea = v->stringOr("");
        if (const auto* v = jo.find("catchAreaLive"))
            o.catchAreaLive = v->type == json::Value::Type::Bool && v->boolean;
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
        if (const auto* sr = jo.find("scroller")) {
            if (const auto* v = sr->find("speed")) o.scrollSpeed = (float)v->numberOr(6.0);
            if (const auto* v = sr->find("ahead")) o.scrollAhead = (float)v->numberOr(40.0);
            if (o.scrollAhead < 0.0f) o.scrollAhead = 0.0f;
            if (const auto* v = sr->find("behind"))
                o.scrollBehind = (float)v->numberOr(10.0);
            if (o.scrollBehind < 0.0f) o.scrollBehind = 0.0f;
            if (const auto* v = sr->find("autostart"))
                o.scrollAutostart = !(v->type == json::Value::Type::Bool && !v->boolean);
            if (const auto* v = sr->find("maxClones"))
                o.scrollMaxClones = (int)v->numberOr(120);
            if (o.scrollMaxClones < 1) o.scrollMaxClones = 1;
            if (const auto* v = sr->find("overlap"))
                o.scrollOverlap = (float)v->numberOr(0.02);
            if (o.scrollOverlap < 0.0f) o.scrollOverlap = 0.0f;
            if (const auto* v = sr->find("varySeed"))
                o.scrollVarySeed = (int)v->numberOr(1);
            if (const auto* segs = sr->find("segments");
                segs && segs->type == json::Value::Type::Array) {
                for (const auto& js : segs->arr) {
                    if (js.type != json::Value::Type::Object) continue;
                    ScrollSegment seg;
                    if (const auto* v = js.find("name")) seg.name = v->stringOr("segment");
                    if (const auto* v = js.find("length"))
                        seg.length = (float)v->numberOr(0.0);
                    if (const auto* v = js.find("objects");
                        v && v->type == json::Value::Type::Array) {
                        // Two spellings, mixable in one list: a bare name (a
                        // member that varies nothing) or an object carrying the
                        // per-cell variation fields.
                        for (const auto& s : v->arr) {
                            ScrollMember m;
                            if (s.type == json::Value::Type::String) {
                                m.name = s.str;
                            } else if (s.type == json::Value::Type::Object) {
                                if (const auto* f = s.find("n")) m.name = f->stringOr("");
                                if (const auto* f = s.find("chance"))
                                    m.chance = (float)f->numberOr(1.0);
                                if (const auto* f = s.find("variant"))
                                    m.variant = (int)f->numberOr(0);
                                if (const auto* f = s.find("yaw"))
                                    m.yawVary = (float)f->numberOr(0.0);
                                if (const auto* f = s.find("offset"))
                                    m.offsetVary = (float)f->numberOr(0.0);
                                if (const auto* f = s.find("scale"))
                                    m.scaleVary = (float)f->numberOr(0.0);
                            }
                            if (m.name.empty()) continue;
                            if (m.chance < 0.0f) m.chance = 0.0f;
                            if (m.chance > 1.0f) m.chance = 1.0f;
                            if (m.variant < 0) m.variant = 0;
                            seg.objects.push_back(std::move(m));
                        }
                    }
                    o.scrollSegments.push_back(std::move(seg));
                }
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
        if (const auto* pg = jo.find("procGraph")) readProcGraph(*pg, o.procGraph);
        if (const auto* v = jo.find("procSource")) o.procSource = v->stringOr("");
        if (const auto* v = jo.find("prefabSource"))
            o.prefabSource = v->stringOr("");
        if (const auto* v = jo.find("vuParams"))
            if (v->type == json::Value::Type::Array)
                for (size_t k = 0; k < v->arr.size() && k < 4; ++k)
                    o.vuParams[k] = (float)v->arr[k].numberOr(0.0);
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

// A scene's "objects" manifest field: an array of object-id strings, each
// loaded from objects/<id>.json in list order (the merge-friendly split - one
// file per object). A referenced object file that is missing or malformed is
// skipped (the object is dropped) rather than aborting the load.
static void readSceneObjects(const Project& p, const json::Value& objs,
                             std::vector<SceneObject>& out) {
    if (objs.type != json::Value::Type::Array) return;
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
        if (const auto* v = s->find("supportedModes");
            v && v->type == json::Value::Type::Array) {
            st.supportedModes.clear();
            for (const auto& jm : v->arr) {
                const std::string k = jm.stringOr("");
                for (const DisplayModeInfo& d : displayModes())
                    if (k == d.key) st.supportedModes.push_back(k);
            }
        }
        if (const auto* v = s->find("palFullHeight"))
            st.palFullHeight = v->boolOr(false);
        // Framebuffer colour depth + GS dithering (docs/gs-vram.md). Absent
        // in every project written before they existed, and the defaults are
        // exactly what those projects already did.
        if (const auto* v = s->find("colorDepth"))
            st.colorDepth = v->stringOr("32bit") == "16bit" ? "16bit" : "32bit";
        if (const auto* v = s->find("dither")) st.dither = v->boolOr(true);
        if (const auto* v = s->find("tripleBuffering"))
            st.tripleBuffering = v->boolOr(false);
        if (const auto* v = s->find("frameExtrapolation"))
            st.frameExtrapolation = v->boolOr(false);
        if (const auto* v = s->find("frameExtrapolationPlane"))
            st.frameExtrapolationPlane = (float)v->numberOr(0.0);
        if (const auto* v = s->find("frameExtrapolationForce"))
            st.frameExtrapolationForce = v->boolOr(false);
        if (const auto* v = s->find("frameExtrapolationGround"))
            st.frameExtrapolationGround = v->boolOr(true);
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
        if (const auto* v = s->find("showAreas")) st.showAreas = v->boolOr(false);
        if (const auto* v = s->find("showCollision"))
            st.showCollision = v->boolOr(false);
        if (const auto* v = s->find("liveLink")) st.liveLink = v->boolOr(true);
        if (const auto* v = s->find("liveDebug")) st.liveDebug = v->boolOr(true);
        if (const auto* v = s->find("liveLogic")) st.liveLogic = v->boolOr(true);
        if (const auto* v = s->find("timeMachine"))
            st.timeMachine = v->boolOr(true);
        if (const auto* v = s->find("remotePad")) st.remotePad = v->boolOr(true);
        // Off for a project that predates the key: the recorder writes a
        // growing file, so it is opt-in rather than something a rebuild
        // silently switches on for everybody.
        if (const auto* v = s->find("inputRecorder"))
            st.inputRecorder = v->boolOr(false);
        if (const auto* v = s->find("eeCrashHandler"))
            st.eeCrashHandler = v->boolOr(false);
        if (const auto* v = s->find("keyboardMouse"))
            st.keyboardMouse = v->boolOr(true);
        // (a retired "keyboardMousePs2LinkResident" key is ignored - the
        // ps2link option now always means the TyraX ps2link, the only one the
        // editor deploys to; a project that predates the key gets it ON, which
        // is what that ps2link supports)
        if (const auto* v = s->find("keyboardMousePs2Link"))
            st.keyboardMousePs2Link = v->boolOr(true);
        if (const auto* v = s->find("disableVsync"))
            st.disableVsync = v->boolOr(false);
        if (const auto* v = s->find("clipping")) {
            // "vu1" (default) = precise per-package classification +
            // clipping on VU1; "precise" = the older EE clipper.
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
        // Absent on projects authored before the animation-fps setting: both
        // default to 24, i.e. a ratio of 1, so they keep playing unchanged.
        if (const auto* v = s->find("animSourceFps"))
            st.animSourceFps = (float)v->numberOr(24.0);
        if (const auto* v = s->find("animPlayFps"))
            st.animPlayFps = (float)v->numberOr(24.0);
        if (st.animSourceFps < 1.0f) st.animSourceFps = 1.0f;
        if (st.animSourceFps > 240.0f) st.animSourceFps = 240.0f;
        if (st.animPlayFps < 1.0f) st.animPlayFps = 1.0f;
        if (st.animPlayFps > 240.0f) st.animPlayFps = 240.0f;
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
        // World scale. Absent (every project saved before it existed) = 1
        // unit per meter, which is what the importers assumed all along.
        if (const auto* v = s->find("unitsPerMeter")) {
            st.unitsPerMeter = (float)v->numberOr(1.0);
            if (!(st.unitsPerMeter > 0.0001f)) st.unitsPerMeter = 1.0f;
        }
        if (const auto* v = s->find("terrainDetail"))
            st.terrainDetail = (int)v->numberOr(32);
        if (st.terrainDetail < 4) st.terrainDetail = 4;
        if (st.terrainDetail > 512) st.terrainDetail = 512;
        if (const auto* v = s->find("terrainViewDistance")) {
            st.terrainViewDistance = (float)v->numberOr(0.0);
            if (st.terrainViewDistance < 0.0f) st.terrainViewDistance = 0.0f;
        }
        if (const auto* v = s->find("terrainLodDistance")) {
            st.terrainLodDistance = (float)v->numberOr(0.0);
            if (st.terrainLodDistance < 0.0f) st.terrainLodDistance = 0.0f;
        }
        if (const auto* v = s->find("flashShadowVolumes"))
            st.flashShadowVolumes = v->boolOr(false);
        readVec3(s->find("skyColor"), st.skyColor);
        readVec3(s->find("skyTopColor"), st.skyTopColor);
        if (const auto* v = s->find("skyDome"))
            st.skyDome = v->type == json::Value::Type::Bool && v->boolean;
        if (const auto* v = s->find("zenithSize")) st.zenithSize = (float)v->numberOr(0.5);
        if (const auto* v = s->find("eyeHeight")) st.eyeHeight = (float)v->numberOr(1.8);
        if (const auto* v = s->find("walkSpeed")) st.walkSpeed = (float)v->numberOr(0.1);
        if (const auto* v = s->find("runSpeed")) st.runSpeed = (float)v->numberOr(0.0);
        if (const auto* v = s->find("lookSpeed")) st.lookSpeed = (float)v->numberOr(1.0);
        // Sprint: projects that predate it read 1.8 like a fresh one (the
        // sprint action ensureInputActions seeds is what actually enables it).
        if (const auto* v = s->find("sprintMultiplier")) {
            st.sprintMultiplier = (float)v->numberOr(1.8);
            if (st.sprintMultiplier < 1.0f) st.sprintMultiplier = 1.0f;
            if (st.sprintMultiplier > 4.0f) st.sprintMultiplier = 4.0f;
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
        // Baked global illumination (docs/global-illumination.md). Every read
        // defaults to the struct initializer, which is what a project saved
        // before GI existed loads as - i.e. off, and identical to before.
        if (const auto* v = s->find("giEnabled")) st.giEnabled = v->boolOr(false);
        if (const auto* v = s->find("giRays")) st.giRays = (int)v->numberOr(128);
        if (st.giRays < 8) st.giRays = 8;
        if (st.giRays > 1024) st.giRays = 1024;
        if (const auto* v = s->find("giBounces"))
            st.giBounces = (int)v->numberOr(2);
        if (st.giBounces < 0) st.giBounces = 0;
        if (st.giBounces > 8) st.giBounces = 8;
        if (const auto* v = s->find("giSkyLight"))
            st.giSkyLight = (float)v->numberOr(1.0);
        if (const auto* v = s->find("giSunLight"))
            st.giSunLight = (float)v->numberOr(1.0);
        if (const auto* v = s->find("giAmbientFloor"))
            st.giAmbientFloor = clamp01((float)v->numberOr(0.03));
        if (const auto* v = s->find("giProbes")) st.giProbes = v->boolOr(true);
        if (const auto* v = s->find("giProbeSpacing"))
            st.giProbeSpacing = (float)v->numberOr(3.0);
        if (st.giProbeSpacing < 0.5f) st.giProbeSpacing = 0.5f;
        if (const auto* v = s->find("giProbeHeight"))
            st.giProbeHeight = (float)v->numberOr(2.0);
        if (st.giProbeHeight < 0.25f) st.giProbeHeight = 0.25f;
        if (const auto* v = s->find("giProbeLevels"))
            st.giProbeLevels = (int)v->numberOr(4);
        if (st.giProbeLevels < 1) st.giProbeLevels = 1;
        if (st.giProbeLevels > 16) st.giProbeLevels = 16;
        // Automatic model AO. The struct defaults are what a file written
        // before this key existed loads as, which is the whole compatibility
        // story - see ProjectSettings::modelAo.
        if (const auto* v = s->find("modelAo")) st.modelAo = v->boolOr(false);
        if (const auto* v = s->find("modelAoStrength"))
            st.modelAoStrength = clamp01((float)v->numberOr(0.7));
        if (const auto* v = s->find("modelAoRays"))
            st.modelAoRays = (int)v->numberOr(64);
        if (st.modelAoRays < 8) st.modelAoRays = 8;
        if (st.modelAoRays > 512) st.modelAoRays = 512;
        if (const auto* v = s->find("modelAoDist"))
            st.modelAoDist = (float)v->numberOr(0.0);
        if (st.modelAoDist < 0.0f) st.modelAoDist = 0.0f;
        if (const auto* v = s->find("prelitAutoBake"))
            st.prelitAutoBake = v->boolOr(false);
        if (const auto* v = s->find("giAutoBake"))
            st.giAutoBake = v->boolOr(false);
        if (const auto* v = s->find("bloom")) {  // 0..2 (see the scene reader)
            const float b = (float)v->numberOr(0.0);
            st.bloom = b < 0.0f ? 0.0f : (b > 2.0f ? 2.0f : b);
        }
        if (const auto* v = s->find("bloomThreshold"))
            st.bloomThreshold = clamp01((float)v->numberOr(0.0));
        if (const auto* v = s->find("bloomSpread"))
            st.bloomSpread = clamp01((float)v->numberOr(0.0));
        if (const auto* v = s->find("grain")) st.grain = clamp01((float)v->numberOr(0.0));
        if (const auto* v = s->find("dofAmount"))
            st.dofAmount = clamp01((float)v->numberOr(0.0));
        if (const auto* v = s->find("dofFocus"))
            st.dofFocus = (float)v->numberOr(st.dofFocus);
        if (const auto* v = s->find("dofRange"))
            st.dofRange = (float)v->numberOr(st.dofRange);
        if (const auto* v = s->find("flare"))
            st.flare = clamp01((float)v->numberOr(0.0));
        if (const auto* v = s->find("godRays"))
            st.godRays = clamp01((float)v->numberOr(0.0));
        if (const auto* v = s->find("blobShadows"))
            st.blobShadows = v->type == json::Value::Type::Bool && v->boolean;
        // The neural upscaler (docs/neural-upscaler.md). Absent = off, which is
        // every project saved before it existed; blssTemporal defaults ON, so
        // it reads like loadingScreen (absent means the default, not false).
        if (const auto* v = s->find("blssEnabled"))
            st.blssEnabled = v->type == json::Value::Type::Bool && v->boolean;
        if (const auto* v = s->find("blssScale"))
            st.blssScale = (int)v->numberOr(0.0);
        if (st.blssScale < 0) st.blssScale = 0;
        if (st.blssScale > 1) st.blssScale = 1;
        // PLAIN MODE, and it reads like blssTemporal rather than like
        // blssJitter: absent means the NETWORK, which is the only thing a
        // project saved before this key existed can have meant. There is no
        // "the old behaviour was harmful" argument here - the reconstruction a
        // legacy BLSS project shipped with is the one it was trained for.
        if (const auto* v = s->find("blssNetwork"))
            st.blssNetwork = !(v->type == json::Value::Type::Bool && !v->boolean);
        if (const auto* v = s->find("blssSharpen"))
            st.blssSharpen = clamp01((float)v->numberOr(0.5));
        if (const auto* v = s->find("blssTemporal"))
            st.blssTemporal = !(v->type == json::Value::Type::Bool && !v->boolean);
        // NOT the "absent means what it always did" rule the keys around it
        // use, and the difference is deliberate. The jitter is the confirmed
        // cause of the screen shake (project.hpp), so a project saved before
        // this key existed - which is every project that ever shook - opens
        // with it OFF rather than keeping the behaviour it was saved with.
        // The one direction this can move a legacy project is "stops
        // flickering"; there is no configuration it makes worse, and a project
        // that wants the samples back says so explicitly and gets a rewritten
        // key on the next save. A malformed value lands on the default too,
        // for the same reason.
        if (const auto* v = s->find("blssJitter"))
            st.blssJitter = (v->type == json::Value::Type::Bool && v->boolean);
        if (const auto* v = s->find("blssDebugView"))
            st.blssDebugView = (int)v->numberOr(0.0);
        if (st.blssDebugView < 0) st.blssDebugView = 0;
        // 2 is the FEATURE-SPREAD INSTRUMENT (RendererCoreBlss::
        // logFeatureSpread): one BLSSGRID/BLSSFEAT/BLSSOUT/BLSSFILL group per
        // second into the game's bin/log.txt, picture untouched. It is what
        // `tyrax-editor --blss-eval --probe "<BLSSFEAT line>"` reads, i.e. the
        // only way to find out whether the network is being run on the
        // distribution it was trained on. The panel's combo offers 0 and 1;
        // this value is reachable by editing the .tyra, which is deliberate -
        // it is a developer instrument, not a project setting.
        if (st.blssDebugView > 2) st.blssDebugView = 2;
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
    // The two interaction prompts. Their mode is explicit; a project written
    // before the flag existed is migrated by the rule that was in force then
    // ("non-empty text wins"), so it keeps the look it had.
    // Returns true when the project actually carried this text - the pre-flag
    // migration rule ("non-empty text wins") may only fire then, or every older
    // project would flip to text mode showing the default.
    auto readPromptText = [&](const char* key, HudText& t) {
        const auto* jt = root.find(key);
        if (!jt || jt->type != json::Value::Type::Object) return false;
        if (const auto* v = jt->find("text")) t.text = v->stringOr("");
        if (const auto* v = jt->find("size")) t.size = (int)v->numberOr(16);
        if (t.size < 8) t.size = 8;
        if (t.size > 48) t.size = 48;
        readVec3(jt->find("color"), t.color);
        if (const auto* v = jt->find("font")) t.font = v->stringOr("");
        if (const auto* v = jt->find("shadow"))
            t.shadow = !(v->type == json::Value::Type::Bool && !v->boolean);
        return true;
    };
    out.usePromptText = defaultPromptText("use-prompt", "{{use}} USE");
    out.pickPromptText = defaultPromptText("pick-prompt", "{{use}} PICK UP");
    bool hadUseText = readPromptText("usePromptText", out.usePromptText);
    bool hadPickText = readPromptText("pickPromptText", out.pickPromptText);
    // Mode: the stored flag when there is one, otherwise the pre-flag rule
    // ("non-empty text wins") so an older project keeps the look it had.
    out.usePromptIsText = hadUseText && !out.usePromptText.text.empty();
    out.pickPromptIsText = hadPickText && !out.pickPromptText.text.empty();
    if (const auto* v = root.find("usePromptIsText"))
        out.usePromptIsText = v->boolOr(false);
    if (const auto* v = root.find("pickPromptIsText"))
        out.pickPromptIsText = v->boolOr(false);
    // "New text" is HudText's own default and can only have got in here from an
    // interim build that default-constructed a prompt text and then wrote it out
    // (flag included). Nobody types that into a prompt, so restore the real
    // default and fall back to the image - AFTER the flags, or the bogus flag
    // would keep the placeholder on screen.
    auto dropPlaceholder = [](HudText& t, bool& isText, const char* def) {
        if (t.text != "New text") return;
        t.text = def;
        isText = false;
    };
    dropPlaceholder(out.usePromptText, out.usePromptIsText, "{{use}} USE");
    dropPlaceholder(out.pickPromptText, out.pickPromptIsText,
                    "{{use}} PICK UP");
    out.pickPromptImage.clear();
    if (const auto* v = root.find("pickPromptImage"))
        out.pickPromptImage = v->stringOr("");

    out.textIcons.clear();
    if (const auto* icons = root.find("textIcons");
        icons && icons->type == json::Value::Type::Array) {
        for (const auto& ji : icons->arr) {
            TextIcon ic;
            if (const auto* v = ji.find("name")) ic.name = v->stringOr("");
            if (const auto* v = ji.find("path")) ic.path = v->stringOr("");
            if (const auto* v = ji.find("scale")) ic.scale = (float)v->numberOr(1.0);
            if (ic.scale < 0.2f) ic.scale = 0.2f;
            if (ic.scale > 4.0f) ic.scale = 4.0f;
            // A nameless icon has no placeholder that could reach it.
            if (ic.name.empty()) continue;
            bool dup = false;
            for (const TextIcon& e : out.textIcons) dup |= (e.name == ic.name);
            if (!dup) out.textIcons.push_back(std::move(ic));
        }
    }
    // Seed/backfill the pad-button set: a project from before text icons (or one
    // whose key was trimmed) still resolves {{cross}}.
    ensureTextIcons(out);
    // Effect layer positions; absent or out of range = -1, i.e. the effect
    // applies over everything at end of frame.
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

static void readModelAoSection(const json::Value& root, Project& out) {
    out.modelAoMode.clear();
    if (const auto* ma = root.find("modelAoMode");
        ma && ma->type == json::Value::Type::Object) {
        for (const auto& [asset, v] : ma->obj) {
            const int mode = (int)v.numberOr(0);
            if (mode == 1 || mode == 2) out.modelAoMode[asset] = mode;
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

    // Absent in projects saved before the Save Editor - all default to ""
    // (project-name title, built-in flat icon).
    if (const auto* t = root.find("saveTitle")) out.saveTitle = t->stringOr("");
    if (const auto* ic = root.find("saveIcon")) out.saveIcon = ic->stringOr("");
    if (const auto* m = root.find("saveIconModel"))
        out.saveIconModel = m->stringOr("");
    if (const auto* c = root.find("saveIconClip"))
        out.saveIconClip = c->stringOr("");
    if (const auto* f = root.find("saveIconFrames")) {
        out.saveIconFrames = (int)f->numberOr(6.0);
        if (out.saveIconFrames < 1) out.saveIconFrames = 1;
        if (out.saveIconFrames > 8) out.saveIconFrames = 8;
    }
    // A project written before the motion setting has neither key; "" is the
    // sway every icon used to do, so those keep looking exactly as they did.
    if (const auto* m = root.find("saveIconMotion"))
        out.saveIconMotion = m->stringOr("");
    if (const auto* a = root.find("saveIconMotionAmount")) {
        out.saveIconMotionAmount = (float)a->numberOr(1.0);
        if (out.saveIconMotionAmount < 0.25f) out.saveIconMotionAmount = 0.25f;
        if (out.saveIconMotionAmount > 2.0f) out.saveIconMotionAmount = 2.0f;
    }
    if (const auto* v = root.find("saveMenuWritesCheckpoint"))
        out.saveMenuWritesCheckpoint = v->boolOr(false);
    // Read the counts BEFORE the autosave slot - its valid range depends on
    // them, and JSON key order is not something a reader may rely on.
    if (const auto* v = root.find("saveSlotCount")) {
        out.saveSlotCount = (int)v->numberOr(3.0);
        if (out.saveSlotCount < 1) out.saveSlotCount = 1;
        if (out.saveSlotCount > kMaxSaveSlots) out.saveSlotCount = kMaxSaveSlots;
    }
    if (const auto* v = root.find("saveSlotsPerPage")) {
        out.saveSlotsPerPage = (int)v->numberOr(3.0);
        if (out.saveSlotsPerPage < 1) out.saveSlotsPerPage = 1;
        if (out.saveSlotsPerPage > kMaxSaveSlotsPerPage)
            out.saveSlotsPerPage = kMaxSaveSlotsPerPage;
    }
    if (const auto* v = root.find("saveAutosaveSlot")) {
        out.saveAutosaveSlot = (int)v->numberOr(-1.0);
        if (out.saveAutosaveSlot < -1 ||
            out.saveAutosaveSlot >= out.saveSlotCount)
            out.saveAutosaveSlot = -1;
    }
    if (const auto* v = root.find("saveAsync")) out.saveAsync = v->boolOr(false);
    if (const auto* v = root.find("saveSpinner"))
        out.saveSpinner = v->boolOr(true);
    if (const auto* v = root.find("saveSpinnerImage"))
        out.saveSpinnerImage = v->stringOr("");
    if (const auto* v = root.find("saveSpinnerFrames")) {
        out.saveSpinnerFrames = (int)v->numberOr(8.0);
        if (out.saveSpinnerFrames < 1) out.saveSpinnerFrames = 1;
        if (out.saveSpinnerFrames > 64) out.saveSpinnerFrames = 64;
    }
    if (const auto* v = root.find("saveSpinnerCorner")) {
        out.saveSpinnerCorner = (int)v->numberOr(3.0);
        if (out.saveSpinnerCorner < 0 || out.saveSpinnerCorner > 3)
            out.saveSpinnerCorner = 3;
    }
    if (const auto* v = root.find("saveSpinnerMargin")) {
        out.saveSpinnerMargin = (float)v->numberOr(20.0);
        if (out.saveSpinnerMargin < 0.0f) out.saveSpinnerMargin = 0.0f;
        if (out.saveSpinnerMargin > 200.0f) out.saveSpinnerMargin = 200.0f;
    }
    if (const auto* v = root.find("saveSpinnerScale")) {
        out.saveSpinnerScale = (float)v->numberOr(1.0);
        if (out.saveSpinnerScale < 0.4f) out.saveSpinnerScale = 0.4f;
        if (out.saveSpinnerScale > 3.0f) out.saveSpinnerScale = 3.0f;
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
            if (const auto* jc = ja.find("cycle");
                jc && jc->type == json::Value::Type::Object) {
                DayCycle& c = a.cycle;
                if (const auto* v = jc->find("enabled")) c.enabled = v->boolOr(false);
                if (const auto* v = jc->find("time")) c.time = (float)v->numberOr(12.0);
                if (const auto* v = jc->find("sunAzimuth"))
                    c.sunAzimuth = (float)v->numberOr(90.0);
                if (const auto* v = jc->find("sunTilt"))
                    c.sunTilt = (float)v->numberOr(25.0);
                if (const auto* v = jc->find("sunrise"))
                    c.sunrise = (float)v->numberOr(6.0);
                if (const auto* v = jc->find("sunset"))
                    c.sunset = (float)v->numberOr(18.0);
                if (const auto* v = jc->find("sunSize"))
                    c.sunSize = (float)v->numberOr(3.0);
                if (const auto* v = jc->find("moonAzimuth"))
                    c.moonAzimuth = (float)v->numberOr(90.0);
                if (const auto* v = jc->find("moonTilt"))
                    c.moonTilt = (float)v->numberOr(35.0);
                if (const auto* v = jc->find("moonOffset"))
                    c.moonOffset = (float)v->numberOr(12.0);
                if (const auto* v = jc->find("moonSize"))
                    c.moonSize = (float)v->numberOr(4.0);
                if (const auto* v = jc->find("moonPhase"))
                    c.moonPhase = (float)v->numberOr(0.5);
                if (const auto* v = jc->find("moonOpacity"))
                    c.moonOpacity = (float)v->numberOr(1.0);
                if (const auto* v = jc->find("moonTexture"))
                    c.moonTexture = v->stringOr("");
                if (const auto* v = jc->find("runtime")) c.runtime = v->boolOr(false);
                if (const auto* v = jc->find("dayLength"))
                    c.dayLength = (float)v->numberOr(240.0);
                if (const auto* v = jc->find("runtimeGrade"))
                    c.runtimeGrade = v->boolOr(true);
                if (const auto* v = jc->find("bakeHour"))
                    c.bakeHour = (float)v->numberOr(12.0);
                if (const auto* v = jc->find("starsEnabled"))
                    c.starsEnabled = v->boolOr(false);
                if (const auto* v = jc->find("starTwinkle"))
                    c.starTwinkle = (float)v->numberOr(0.35);
                if (const auto* v = jc->find("starSeed"))
                    c.starField.seed = (int)v->numberOr(1.0);
                if (const auto* v = jc->find("starCount"))
                    c.starField.count = (int)v->numberOr(400.0);
                if (const auto* v = jc->find("starSpread"))
                    c.starField.magnitudeSpread = (float)v->numberOr(0.7);
                if (const auto* v = jc->find("milkyWay"))
                    c.starField.milkyWay = (float)v->numberOr(0.6);
                if (const auto* v = jc->find("milkyWayTilt"))
                    c.starField.milkyWayTilt = (float)v->numberOr(30.0);
                if (const auto* v = jc->find("starSize"))
                    c.starField.sizeScale = (float)v->numberOr(1.0);
                if (const auto* jk = jc->find("keys");
                    jk && jk->type == json::Value::Type::Array) {
                    for (const auto& jd : jk->arr) {
                        DayKey dk;
                        if (const auto* v = jd.find("hour"))
                            dk.hour = (float)v->numberOr(12.0);
                        readVec3(jd.find("skyColor"), dk.skyColor);
                        readVec3(jd.find("skyTopColor"), dk.skyTopColor);
                        readVec3(jd.find("lightColor"), dk.lightColor);
                        if (const auto* v = jd.find("ambient"))
                            dk.ambient = (float)v->numberOr(0.55);
                        if (const auto* v = jd.find("diffuse"))
                            dk.diffuse = (float)v->numberOr(0.45);
                        if (const auto* v = jd.find("brightness"))
                            dk.brightness = (float)v->numberOr(1.0);
                        readVec3(jd.find("fogColor"), dk.fogColor);
                        if (const auto* v = jd.find("stars"))
                            dk.stars = (float)v->numberOr(0.0);
                        project::clampDayKey(dk);
                        c.keys.push_back(dk);
                    }
                }
                project::clampDayCycle(c);
            }
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

static void readCreditsSection(const json::Value& root, Project& out) {
    out.credits.clear();
    const auto* rolls = root.find("credits");
    if (!rolls || rolls->type != json::Value::Type::Array) return;
    for (const auto& jr : rolls->arr) {
        CreditsRoll r;
        if (const auto* v = jr.find("name")) r.name = v->stringOr("credits");
        readVec3(jr.find("bgColor"), r.bgColor);
        readVec3(jr.find("color"), r.color);
        readVec3(jr.find("headingColor"), r.headingColor);
        if (const auto* v = jr.find("font")) r.font = v->stringOr("");
        if (const auto* v = jr.find("headingSize")) r.headingSize = (int)v->numberOr(22);
        if (const auto* v = jr.find("lineSize")) r.lineSize = (int)v->numberOr(16);
        if (const auto* v = jr.find("shadow")) r.shadow = v->boolOr(true);
        if (const auto* v = jr.find("pageW")) r.pageW = (int)v->numberOr(512);
        if (r.pageW != 256 && r.pageW != 512) r.pageW = 512;
        if (const auto* v = jr.find("margin")) r.margin = (float)v->numberOr(40.0);
        if (const auto* v = jr.find("columnGap")) r.columnGap = (float)v->numberOr(24.0);
        if (const auto* v = jr.find("lineSpacing"))
            r.lineSpacing = (float)v->numberOr(1.25);
        if (r.lineSpacing < 0.8f) r.lineSpacing = 0.8f;
        if (const auto* v = jr.find("mode")) r.mode = (int)v->numberOr(0);
        if (r.mode < 0 || r.mode > 1) r.mode = 0;
        if (const auto* v = jr.find("speed")) r.speed = (float)v->numberOr(34.0);
        if (r.speed < 1.0f) r.speed = 1.0f;
        if (const auto* v = jr.find("cardSeconds"))
            r.cardSeconds = (float)v->numberOr(4.0);
        if (r.cardSeconds < 0.5f) r.cardSeconds = 0.5f;
        if (const auto* v = jr.find("startDelay")) r.startDelay = (float)v->numberOr(0.8);
        if (const auto* v = jr.find("endHold")) r.endHold = (float)v->numberOr(2.0);
        if (const auto* v = jr.find("fadeIn")) r.fadeIn = (float)v->numberOr(0.6);
        if (const auto* v = jr.find("fadeOut")) r.fadeOut = (float)v->numberOr(1.2);
        if (r.startDelay < 0.0f) r.startDelay = 0.0f;
        if (r.endHold < 0.0f) r.endHold = 0.0f;
        if (r.fadeIn < 0.0f) r.fadeIn = 0.0f;
        if (r.fadeOut < 0.0f) r.fadeOut = 0.0f;
        if (const auto* v = jr.find("quant")) {
            const std::string q = v->stringOr("");
            r.quant = (q == "none" || q == "8bit" || q == "4bit") ? q : "4bit";
        }
        if (const auto* v = jr.find("music")) r.music = v->stringOr("");
        if (const auto* v = jr.find("musicLoop")) r.musicLoop = v->boolOr(true);
        if (const auto* v = jr.find("musicStopAtEnd"))
            r.musicStopAtEnd = v->boolOr(true);
        if (const auto* v = jr.find("musicVolume"))
            r.musicVolume = (int)v->numberOr(100);
        if (r.musicVolume < 0) r.musicVolume = 0;
        if (r.musicVolume > 100) r.musicVolume = 100;
        if (const auto* v = jr.find("skippable")) r.skippable = v->boolOr(true);
        if (const auto* v = jr.find("skipAction")) r.skipAction = v->stringOr("");
        if (const auto* v = jr.find("skipAfter")) r.skipAfter = (float)v->numberOr(1.0);
        if (r.skipAfter < 0.0f) r.skipAfter = 0.0f;
        if (const auto* v = jr.find("showSkipHint")) r.showSkipHint = v->boolOr(true);
        if (const auto* v = jr.find("skipHint")) r.skipHint = v->stringOr("");
        if (const auto* v = jr.find("hintPos");
            v && v->type == json::Value::Type::Array && v->arr.size() >= 2) {
            r.hintPos[0] = (float)v->arr[0].numberOr(0.5);
            r.hintPos[1] = (float)v->arr[1].numberOr(0.93);
        }
        if (const auto* v = jr.find("hintSize")) r.hintSize = (int)v->numberOr(14);
        if (const auto* v = jr.find("finish")) r.finish = (int)v->numberOr(0);
        if (r.finish < 0 || r.finish > CreditsRoll::Hold) r.finish = CreditsRoll::Resume;
        if (const auto* v = jr.find("finishParam")) r.finishParam = v->stringOr("");
        if (const auto* v = jr.find("source")) r.source = v->stringOr("");
        if (const auto* im = jr.find("bgImage")) {
            HudImage& bg = r.bgImage;
            if (const auto* v = im->find("image")) bg.imagePath = v->stringOr("");
            if (const auto* v = im->find("pos");
                v && v->type == json::Value::Type::Array && v->arr.size() >= 2) {
                bg.pos[0] = (float)v->arr[0].numberOr(0.5);
                bg.pos[1] = (float)v->arr[1].numberOr(0.5);
            }
            if (const auto* v = im->find("size");
                v && v->type == json::Value::Type::Array && v->arr.size() >= 2) {
                bg.size[0] = (float)v->arr[0].numberOr(512.0);
                bg.size[1] = (float)v->arr[1].numberOr(448.0);
            }
            if (const auto* v = im->find("texW")) bg.texW = (int)v->numberOr(0);
            if (const auto* v = im->find("texH")) bg.texH = (int)v->numberOr(0);
            if (const auto* v = im->find("texQuant")) {
                const std::string q = v->stringOr("");
                bg.texQuant = (q == "none" || q == "8bit" || q == "4bit") ? q : "";
            }
        }
        if (const auto* jb = jr.find("blocks");
            jb && jb->type == json::Value::Type::Array) {
            for (const auto& jbl : jb->arr) {
                CreditsBlock b;
                if (const auto* v = jbl.find("kind")) b.kind = (int)v->numberOr(1);
                if (b.kind < 0 || b.kind > CreditsBlock::Break) b.kind = CreditsBlock::Line;
                if (const auto* v = jbl.find("text")) b.text = v->stringOr("");
                if (const auto* v = jbl.find("text2")) b.text2 = v->stringOr("");
                if (const auto* v = jbl.find("image")) b.imagePath = v->stringOr("");
                if (const auto* v = jbl.find("size")) b.size = (int)v->numberOr(0);
                if (const auto* v = jbl.find("font")) b.font = v->stringOr("");
                if (const auto* v = jbl.find("color")) {
                    readVec3(v, b.color);
                    b.ownColor = true;
                }
                if (const auto* v = jbl.find("align")) b.align = (int)v->numberOr(1);
                if (b.align < 0 || b.align > 2) b.align = 1;
                if (const auto* v = jbl.find("space")) b.space = (float)v->numberOr(0.0);
                if (const auto* v = jbl.find("scale")) b.scale = (float)v->numberOr(1.0);
                if (b.scale < 0.05f) b.scale = 0.05f;
                if (b.scale > 1.0f) b.scale = 1.0f;
                r.blocks.push_back(std::move(b));
            }
        }
        out.credits.push_back(std::move(r));
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
                    if (const auto* v = jck.find("roll")) k.roll = (float)v->numberOr(0.0);
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
            if (const auto* v = jm.find("saveMenu"))
                m.saveMenu = v->type == json::Value::Type::Bool && v->boolean;
            if (const auto* v = jm.find("panelW")) {
                const int w = (int)v->numberOr(256);
                m.panelW = (w == 128 || w == 512) ? w : 256;
            }
            if (const auto* v = jm.find("showTitle"))
                m.showTitle = !(v->type == json::Value::Type::Bool && !v->boolean);
            if (const auto* v = jm.find("font")) m.font = v->stringOr("");
            // The stylesheet key. An unknown one resolves to Classic through the
            // registry rather than failing the load, so a project that lost its
            // menu-styles/ folder still opens and still bakes.
            if (const auto* v = jm.find("style")) m.style = v->stringOr("");
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
                                    : a == "rebind"    ? MenuEntry::RebindKey
                                    : a == "credits"   ? MenuEntry::PlayCredits
                                    : a == "label"     ? MenuEntry::Label
                                                       : MenuEntry::Close;
                    }
                    if (const auto* v = je.find("param")) en.param = v->stringOr("");
                    if (const auto* v = je.find("bindAction"))
                        en.bindAction = v->stringOr("");
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
                            : b == "input-preset" ? MenuEntry::BindInputPreset
                                                 : MenuEntry::BindNone;
                    }
                    if (const auto* v = je.find("class"))
                        en.styleClass = v->stringOr("");
                    if (const auto* v = je.find("desc"))
                        en.description = v->stringOr("");
                    if (const auto* v = je.find("icon")) en.icon = v->stringOr("");
                    if (const auto* v = je.find("enabledWhen"))
                        en.enabledWhen = v->stringOr("");
                    m.entries.push_back(std::move(en));
                }
            }
            if (!m.name.empty()) out.menus.push_back(std::move(m));
        }
    }
}

static void readAnimEditsSection(const json::Value& root, Project& out) {
    out.animClipEdits.clear();
    const auto* arr = root.find("animClipEdits");
    if (!arr || arr->type != json::Value::Type::Array) return;
    for (const auto& je : arr->arr) {
        AnimClipEdit e;
        if (const auto* v = je.find("model")) e.model = v->stringOr("");
        if (const auto* v = je.find("clip")) e.clip = v->stringOr("");
        if (const auto* v = je.find("rename")) e.rename = v->stringOr("");
        if (const auto* v = je.find("timeScale"))
            e.timeScale = (float)v->numberOr(1.0);
        if (const auto* v = je.find("trimStart"))
            e.trimStart = (float)v->numberOr(0.0);
        if (const auto* v = je.find("trimEnd"))
            e.trimEnd = (float)v->numberOr(0.0);
        if (const auto* v = je.find("inPlace")) e.inPlace = v->boolOr(false);
        if (const auto* v = je.find("loop")) e.loop = v->boolOr(true);
        // Same clamp the Animation Editor enforces; a hand-edited file can
        // otherwise stall a clip (0x) or make it unplayably fast.
        if (e.timeScale < 0.05f) e.timeScale = 0.05f;
        if (e.timeScale > 10.0f) e.timeScale = 10.0f;
        if (e.trimStart < 0.0f) e.trimStart = 0.0f;
        if (e.trimEnd < 0.0f) e.trimEnd = 0.0f;
        // An entry with no model or clip addresses nothing - drop it rather
        // than keep a row the Animation Editor could never show.
        if (e.model.empty() || e.clip.empty()) continue;
        out.animClipEdits.push_back(std::move(e));
    }
}

static void readModelUnitsSection(const json::Value& root, Project& out) {
    out.modelUnitMeters.clear();
    const auto* obj = root.find("modelUnits");
    if (!obj || obj->type != json::Value::Type::Object) return;
    for (const auto& [asset, v] : obj->obj) {
        const float meters = (float)v.numberOr(1.0);
        // A non-positive size says nothing about the model and would collapse
        // every object made from it - drop the entry instead.
        if (meters > 0.0001f) out.modelUnitMeters[asset] = meters;
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
        case Section::ModelAo: readModelAoSection(root, p); break;
        case Section::SaveData: readSaveDataSection(root, p); break;
        case Section::Gradings: readGradingsSection(root, p); break;
        case Section::Ambience: readAmbienceSection(root, p); break;
        case Section::LoadingScreens: readLoadingScreensSection(root, p); break;
        case Section::Splash: readSplashSection(root, p); break;
        case Section::Credits: readCreditsSection(root, p); break;
        case Section::Sequences: readSequencesSection(root, p); break;
        case Section::Menus: readMenusSection(root, p); break;
        case Section::AnimEdits: readAnimEditsSection(root, p); break;
        case Section::AnimImports: readAnimImportsSection(root, p); break;
        case Section::ModelUnits: readModelUnitsSection(root, p); break;
        // A section blob is total, so a peer that never had the Input Map
        // would wipe it - re-seed the built-ins after applying (idempotent).
        case Section::Input:
            readInputSection(root, p);
            ensureInputActions(p);
            break;
        case Section::Prefabs: readPrefabsSection(root, p); break;
        case Section::VuPrograms: readVuSection(root, p); break;
        // A peer's catalog arrives whole; a fact that reached them without an
        // id (hand-edited .tyra, an older editor) must get one here or the
        // next save writes a save-key that is not there.
        case Section::Facts:
            readFactsSection(root, p);
            ensureFactIds(p);
            break;
        case Section::BlssShots: readBlssShotsSection(root, p); break;
        case Section::Count: return false;  // not a section
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

    // Format gate, before anything else is read. Too NEW is refused outright -
    // this editor would silently drop the fields it does not know and destroy
    // them on the next save. Too OLD is refused as well, and by name: the
    // reader carries no translations below kMinFormatVersion, so it would find
    // nothing it recognises and open an empty project without saying why.
    // Everything in between loads normally; the caller checks
    // migrations::stepsFor(formatVersionOnDisk) to decide whether an
    // (irreversible) migration prompt is needed.
    out.formatVersionOnDisk = 0;  // no field = saved before versioning existed
    if (const auto* v = root.find("formatVersion"))
        out.formatVersionOnDisk = (int)v->numberOr(0);
    if (out.formatVersionOnDisk > version::kFormatVersion) {
        std::string wrote;
        if (const auto* v = root.find("editorVersion")) wrote = v->stringOr("");
        return tyraPath.filename().string() + " was saved by a newer editor" +
               (wrote.empty() ? "" : " (TyraX " + wrote + ")") +
               ": project format v" + std::to_string(out.formatVersionOnDisk) +
               ", this editor (TyraX " + version::kEditorVersion +
               ") reads up to v" + std::to_string(version::kFormatVersion) +
               ". Update TyraX to open this project.";
    }
    if (out.formatVersionOnDisk < version::kMinFormatVersion) {
        return tyraPath.filename().string() +
               " is in project format v" +
               std::to_string(out.formatVersionOnDisk) +
               ", which this editor (TyraX " + version::kEditorVersion +
               ") no longer reads - it reads v" +
               std::to_string(version::kMinFormatVersion) + " to v" +
               std::to_string(version::kFormatVersion) + ".";
    }

    // Register the project's custom flow nodes BEFORE the graphs are parsed:
    // readFlowGraph drops any node whose type is unknown (line ~156), so a
    // "custom:*" node only survives the load if its .flownode file is present.
    flownode::loadForProject(out.dir);
    // Same for custom screen effects: their placements below reference a
    // screen-effects/*.screenfx file by key, and a placement whose file is
    // missing is dropped (so a moved .tyra cannot silently keep a dead effect).
    screenfx::loadForProject(out.dir);
    // Menu stylesheets, for the same reason: a menu names one by key and the
    // bake resolves it through the registry, so the sheets have to be in place
    // before the menus section is read.
    menustyle::loadForProject(out.dir);
    if (const auto* v = root.find("name")) out.name = v->stringOr("");
    if (out.name.empty())
        return tyraPath.filename().string() + " is malformed (no name)";

    if (const auto* v = root.find("template")) {
        const std::string t = v->stringOr("orbit");
        out.gameTemplate = t == "fpp" ? "fpp" : t == "thirdperson" ? "thirdperson" : "orbit";
    }

    if (const auto* v = root.find("projectId")) out.projectId = v->stringOr("");
    ensureProjectId(out);  // backfill projects born before project ids

    readSettingsSection(root, out);

    // Scenes: [{ "name", "terrain", "settings", "overrides", "objects" }], the
    // "objects" list being the ids of the objects/<id>.json bodies.
    if (const auto* v = root.find("startScene")) out.startScene = (int)v->numberOr(0.0);
    if (const auto* scenes = root.find("scenes");
        scenes && scenes->type == json::Value::Type::Array && !scenes->arr.empty()) {
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
                if (const auto* v = t->find("enabled"))
                    sc.terrain.enabled = v->boolOr(true);
            }
            readSceneVisuals(js, sc);
            out.scenes.push_back(std::move(sc));
        }
    }
    if (out.scenes.empty()) out.scenes.push_back(SceneData{});

    // Every scene object must carry a stable id before the caller snapshots the
    // project (loadHistory compares against out.scenes); a just-added object
    // has none until here, and they are written back on the next save.
    ensureObjectIds(out);
    clampStartScene(out);

    readHudSection(root, out);

    readAudioSection(root, out);

    readTexQualitySection(root, out);
    readModelLodsSection(root, out);
    readModelAoSection(root, out);
    readModelUnitsSection(root, out);

    readSaveDataSection(root, out);

    readGradingsSection(root, out);

    readAmbienceSection(root, out);

    readLoadingScreensSection(root, out);

    readSplashSection(root, out);

    readCreditsSection(root, out);

    readSequencesSection(root, out);

    readAnimEditsSection(root, out);
    readAnimImportsSection(root, out);

    readPrefabsSection(root, out);
    readVuSection(root, out);
    readFactsSection(root, out);
    // A fact's id is what a player's save file is keyed by, so a
    // catalog authored before ids existed gets them before anything
    // can be written - the ensureObjectIds contract.
    ensureFactIds(out);
    readBlssShotsSection(root, out);

    // NOTE there is deliberately no "fold sky/lighting/fog into presets"
    // migration here any more. It was the pre-Ambience-Editor lift, gated on
    // `ambiencePresets.empty()` - and that gate stopped meaning "an old file"
    // the moment the Ambience Editor let you delete every preset (it has an
    // empty state and no last-one guard, so this is an ORDINARY state). On such
    // a project it re-manufactured a "Default" preset, bound each scene that
    // overrode sky/lighting/fog to a freshly invented preset named after the
    // scene, and CLEARED those three override flags - measured, not feared: an
    // emptied lighting example came back with `"ambiencePreset": "main"` and
    // its two ticks off. The values survived inside the preset, but the
    // structure the author chose did not. A pre-versioning file is refused at
    // the version::kMinFormatVersion gate long before this point, so the lift
    // had no one left to help.
    if (out.defaultAmbience < -1 ||
        out.defaultAmbience >= (int)out.ambiencePresets.size())
        out.defaultAmbience = -1;

    readMenusSection(root, out);

    readInputSection(root, out);
    // Backfill the built-in actions/preset: a project from before the Input Map
    // (or one whose "input" key was hand-trimmed) gets exactly the bindings
    // that used to be hardcoded, so it plays the same.
    ensureInputActions(out);
    // Same backfill for the save menu: a project from before it was editable
    // has no saveMenu entry and would otherwise bake no save panel at all.
    ensureSaveMenu(out);

    loadHeights(out);
    ensureHeightmap(out);
    loadSplat(out);  // reads <scene>.splat sidecars + reconciles with the layers

    // Editor-side state + window layout (the .tyra file holds the whole
    // project). All are clamped/validated where they are applied.
    if (const auto* ed = root.find("editor")) {
        if (const auto* v = ed->find("selectedObject"))
            out.selectedObject = (int)v->numberOr(-1);
        if (const auto* v = ed->find("gizmo")) out.gizmoOp = (int)v->numberOr(0);
        if (const auto* v = ed->find("gizmoSpace"))
            out.gizmoSpace = (int)v->numberOr(0);
        if (const auto* v = ed->find("viewMode")) out.viewMode = (int)v->numberOr(0);
        if (const auto* c = ed->find("cam");
            c && c->type == json::Value::Type::Array && c->arr.size() >= 6) {
            out.viewCamYaw = (float)c->arr[0].numberOr(0.8);
            out.viewCamPitch = (float)c->arr[1].numberOr(0.6);
            out.viewCamDist = (float)c->arr[2].numberOr(90.0);
            for (int k = 0; k < 3; ++k)
                out.viewCamTarget[k] = (float)c->arr[3 + k].numberOr(0.0);
        }
        if (const auto* v = ed->find("viewProjection"))
            out.viewProjection = (int)v->numberOr(0);
        if (const auto* v = ed->find("showFog")) out.viewShowFog = v->boolOr(true);
        if (const auto* v = ed->find("breakpoints");
            v && v->type == json::Value::Type::Array)
            for (const auto& jb : v->arr)
                if (jb.type == json::Value::Type::String && !jb.str.empty())
                    out.debugBreakpoints.push_back(jb.str);
    }
    // Window layouts: a "layouts" array + an "activeLayout" index. A project
    // that carries none is seeded with the built-in set.
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
    }
    // Top up the built-in set: a project saved before a built-in layout existed
    // keeps its own layouts, and would otherwise never see the new one. Only
    // recipe-backed built-ins are added (a user layout with the same name is
    // left alone), so this stays a one-time migration per project.
    {
        auto hasRecipe = [&](LayoutRecipe r) {
            for (const WindowLayout& L : out.windowLayouts)
                if (L.recipe == (int)r) return true;
            return false;
        };
        if (!out.windowLayouts.empty() && !hasRecipe(LayoutRecipe::Debugger))
            out.windowLayouts.push_back(
                {"Debugger", "", (int)LayoutRecipe::Debugger, {"debugger"}});
        if (!out.windowLayouts.empty() && !hasRecipe(LayoutRecipe::Procedural))
            out.windowLayouts.push_back(
                {"Procedural", "", (int)LayoutRecipe::Procedural, {"proc", "prefabs"}});
        if (!out.windowLayouts.empty() && !hasRecipe(LayoutRecipe::MenuDesigner))
            out.windowLayouts.push_back(
                {"Menu Designer", "", (int)LayoutRecipe::MenuDesigner,
                 {"menus", "menupreview", "fonts"}});
    }
    // A project must always have at least one layout, and activeLayout must be
    // in range (a hand-edited or corrupt file could break either).
    if (out.windowLayouts.empty()) seedBuiltinLayouts(out);
    if (out.activeLayout < 0 || out.activeLayout >= (int)out.windowLayouts.size())
        out.activeLayout = 0;

    // Same contract as the layouts: fonts[0] is the fallback every empty font
    // reference resolves to, so the list must never be empty.
    if (out.fonts.empty()) out.fonts.push_back(GameFont{});

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
                 << ", \"depth\": " << sc.terrain.depth
                 << (sc.terrain.enabled ? "" : ", \"enabled\": false") << " }, ";
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
                    if (const auto* v = t->find("enabled"))
                        sc.terrain.enabled = v->boolOr(true);
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
                  (o.decalProject ? 8 : 0) | (o.projShadow ? 128 : 0));
    fnvMix(h, (uint64_t)o.collisionMode);
    fnvMixS(h, o.layer);
    fnvMix(h, (uint64_t)o.primDetail);
    fnvMix(h, o.primRings ? 1 : 0);
    fnvMixF(h, o.drawDistance);
    // Cast shadow feeds the build-time AO bake (occluder tables + textures);
    // a live edit of it cannot show without a rebuild.
    fnvMix(h, o.castShadow ? 1 : 0);
    fnvMix(h, o.bakedLighting ? 1 : 0);
    fnvMix(h, o.dynamicLighting ? 1 : 0);
    fnvMix(h, o.prelit ? 1 : 0);
    // The four numbers this mesh hands the project's own VU1 microprogram.
    // They are BAKED into SCENE_OBJECTS and the live-link record carries only
    // transform + colour, so an edit of them cannot show without a rebuild -
    // and without this the chip would claim LIVE while the effect did not
    // change (docs/vu-authoring.md).
    for (int i = 0; i < 4; ++i) fnvMixF(h, o.vuParams[i]);
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
    // The three speed tiers are deliberately NOT in this hash: the walker
    // reads them from PlayerCtl::speeds, which Live Link streams into
    // (record v3), so a speed edit updates the running game instead of
    // flipping the chip amber. Look speed stays baked.
    fnvMixF(h, o.playerLookSpeed);
    fnvMixF(h, o.playerEyeHeight), fnvMixF(h, o.playerJumpSpeed);
    fnvMix(h, o.playerCanJump ? 1 : 0);
    fnvMixS(h, o.playerIdleClip), fnvMixS(h, o.playerWalkClip);
    fnvMixS(h, o.playerRunClip), fnvMixS(h, o.playerJumpClip);
    fnvMixS(h, o.playerSprintClip);
    fnvMixS(h, o.playerBackClip), fnvMixS(h, o.playerStrafeLeftClip);
    fnvMixS(h, o.playerStrafeRightClip);
    fnvMix(h, o.playerFaceCamera ? 1 : 0);
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
    fnvMixS(h, o.flashlightTexture);
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
    fnvMix(h, (o.soundAuto ? 1 : 0) | (o.soundOnPlayer ? 2 : 0) |
                  (o.soundReverb ? 4 : 0));
    fnvMixF(h, o.soundRange), fnvMixF(h, o.soundInterval);
    fnvMix(h, (unsigned)o.soundPriority);
    fnvMixF(h, o.cameraFov);
    // Texture feeds bake into side tables (CAM_FEEDS / OBJECT_FEEDS).
    fnvMix(h, (o.camFeed ? 1 : 0) | (o.camFeedTerrain ? 2 : 0));
    for (const auto& n : o.camFeedObjects) fnvMixS(h, n);
    fnvMixS(h, o.textureFeed);
    // A catch area is expanded into the baked side tables at build time; a
    // live one additionally bakes its candidate list and an area index.
    fnvMixS(h, o.catchArea);
    fnvMix(h, o.catchAreaLive ? 1 : 0);
    // A reverb zone bakes into REVERB_ZONES at build time - the box itself
    // moves live (it is read from the object table), but changing the preset
    // or the amount needs a rebuild.
    fnvMix(h, (o.reverbZone ? 1 : 0) | ((uint64_t)o.reverbPreset << 1));
    fnvMixF(h, o.reverbAmount);
    fnvMix(h, (uint64_t)o.reverbDelay | ((uint64_t)o.reverbFeedback << 8) |
                  ((uint64_t)(o.reverbPriority & 0xFFFF) << 16));
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
    // Scroller parameters + segment membership are baked (SCROLLERS /
    // SCROLLER_CLONES side tables + the appended clone objects), so any edit
    // must read as "rebuild needed" for Live Link.
    fnvMixF(h, o.scrollSpeed), fnvMixF(h, o.scrollAhead), fnvMixF(h, o.scrollBehind);
    fnvMix(h, (o.scrollAutostart ? 1 : 0));
    fnvMix(h, (uint64_t)o.scrollMaxClones);
    fnvMixF(h, o.scrollOverlap);
    fnvMix(h, (uint64_t)(uint32_t)o.scrollVarySeed);
    for (const ScrollSegment& s : o.scrollSegments) {
        fnvMixS(h, s.name);
        fnvMixF(h, s.length);
        for (const ScrollMember& m : s.objects) {
            fnvMixS(h, m.name);
            fnvMixF(h, m.chance), fnvMix(h, (uint64_t)(uint32_t)m.variant);
            fnvMixF(h, m.yawVary), fnvMixF(h, m.offsetVary), fnvMixF(h, m.scaleVary);
        }
    }
    // Build-time-baked transforms: a projected decal's transform IS the
    // projector, a point light's pose/color/falloff is baked into nearby
    // vertex colors. Folding them into the recipe makes any live edit of
    // these objects read as "rebuild needed" instead of silently no-op.
    if (o.type == PrimitiveType::Decal && o.decalProject) {
        fnvMix3(h, o.position), fnvMix3(h, o.rotation), fnvMix3(h, o.scale);
    }
    if (o.type == PrimitiveType::PointLight) {
        // A BAKED light's pose/color/falloff is baked into vertex colors, so
        // any edit needs a rebuild. A DYNAMIC light reads its object data
        // every frame - since livelink v4 its transform, color, brightness,
        // radius, flicker and spot angle STREAM instead, so they stay out of
        // the recipe. What still rebuilds: the dynamic flag itself, the beam
        // (its bags exist per light from scene setup) and the spot STYLE
        // (the pool's texture - gobo vs corona - is chosen at setup).
        if (!o.lightDynamic) {
            fnvMix3(h, o.position), fnvMix3(h, o.color);
            fnvMixF(h, o.lightBright), fnvMixF(h, o.lightRadius);
            fnvMixF(h, o.lightFlicker);
        }
        fnvMixF(h, o.lightDynamic ? 1.0f : 0.0f);
        fnvMixF(h, o.lightSpot ? 1.0f : 0.0f);
        fnvMixF(h, (float)o.lightBeam);
    }
    // An Area's box is what mirror/portal/camera-feed target lists were
    // expanded against at build time, so moving one changes baked tables even
    // though the runtime also reads it live (layer zones, the In Area trigger).
    // Folding the transform in keeps the LIVE chip honest instead of showing
    // half the edit.
    if (o.type == PrimitiveType::Area) {
        fnvMix3(h, o.position), fnvMix3(h, o.rotation), fnvMix3(h, o.scale);
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
    // Areas are referenced BY NAME from baked tables (layer zones, catch-area
    // expansions) that only exist for authored objects - a spawned clone would
    // be a volume nothing points at.
    if (o.type == PrimitiveType::Area) return false;
    if (o.type == PrimitiveType::Scroller) return false;  // baked clones + gen'd director
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
            fnvMixS(h, l.streamArea);  // baked as the zone's area object index
        }
        // Scrollers bake their belt layout (clone objects + SCROLLERS tables)
        // from their segments AND the current transforms of the member objects
        // they reference. A live session cannot restripe the belt, so fold the
        // belt params, segment membership and every member's transform in here
        // - editing any of them flips the context hash to "rebuild needed".
        for (const SceneObject& o : sc.objects) {
            if (o.type != PrimitiveType::Scroller) continue;
            fnvMix(h, 0x5D);  // scroller separator
            fnvMixF(h, o.scrollSpeed), fnvMixF(h, o.scrollAhead);
            fnvMixF(h, o.scrollBehind), fnvMix(h, o.scrollAutostart ? 1 : 0);
            fnvMixF(h, o.scrollOverlap);
            fnvMix(h, (uint64_t)(uint32_t)o.scrollVarySeed);
            fnvMix3(h, o.rotation);  // belt axis
            for (const ScrollSegment& s : o.scrollSegments) {
                fnvMixS(h, s.name), fnvMixF(h, s.length);
                for (const ScrollMember& sm : s.objects) {
                    const std::string& name = sm.name;
                    fnvMixS(h, name);
                    fnvMixF(h, sm.chance), fnvMix(h, (uint64_t)(uint32_t)sm.variant);
                    fnvMixF(h, sm.yawVary), fnvMixF(h, sm.offsetVary);
                    fnvMixF(h, sm.scaleVary);
                    for (const SceneObject& m : sc.objects)
                        if (m.name == name) {
                            fnvMix3(h, m.position), fnvMix3(h, m.rotation);
                            fnvMix3(h, m.scale);
                            break;
                        }
                }
            }
        }
    }
    // Animation clip edits are baked into the .tskl at build time, so a
    // retimed/trimmed/in-place/renamed clip cannot reach a running game - the LIVE
    // chip must flip to "rebuild" instead of silently streaming edits the
    // console will not show.
    fnvMixF(h, p.settings.animSourceFps), fnvMixF(h, p.settings.animPlayFps);
    for (const AnimClipEdit& e : p.animClipEdits) {
        fnvMixS(h, e.model), fnvMixS(h, e.clip), fnvMixS(h, e.rename);
        fnvMixF(h, e.timeScale), fnvMixF(h, e.trimStart), fnvMixF(h, e.trimEnd);
        fnvMix(h, e.inPlace ? 1u : 0u);
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

uint64_t inputLayoutHash(const Project& p) {
    // Deliberately COARSE: it answers "is this the same world the recording was
    // taken in", not "is anything different at all". Object transforms and
    // colours are excluded on purpose - moving a prop is exactly the kind of
    // edit somebody makes between recording a bug and replaying it, and the
    // per-frame fingerprint reports the consequence far better than a hash
    // that would refuse every recording after the first save.
    uint64_t h = 1469598103934665603ull;
    auto mix = [&h](uint64_t v) { h = (h ^ v) * 1099511628211ull; };
    auto mixStr = [&](const std::string& s) {
        for (char c : s) mix((uint64_t)(unsigned char)c);
        mix(0x1F);
    };
    mix(1);  // layout version - bump when the mix below changes
    mix(p.scenes.size());
    for (const SceneData& sc : p.scenes) mix(sc.objects.size());
    mix((uint64_t)p.startScene);
    mixStr(p.settings.multiplayer);
    mix(p.settings.p2JoinOnStart ? 1u : 0u);
    mix(p.settings.keyboardMouse ? 1u : 0u);
    mixStr(p.settings.videoSystem);
    // The input map decides what a button MEANS, so a rebind changes the run a
    // recording describes even though the recorded bits are unchanged.
    mix((uint64_t)p.input.activePreset);
    for (const InputAction& a : p.input.actions) {
        mixStr(a.name);
        mix((uint64_t)a.role);
    }
    for (const InputPreset& pr : p.input.presets) {
        mixStr(pr.name);
        for (const InputBinding& b : pr.bindings) {
            mixStr(b.action);
            mixStr(b.pad);
            mix((uint64_t)b.key);
            mix((uint64_t)b.mouse);
        }
    }
    return h;
}

// Generated VU files (docs/vu-authoring.md). They are named after the program
// they carry, so refreshGenerated cannot list them by path the way it lists
// everything else - and a leftover one is worse than a stale header, because
// Makefile.base globs src/**.vclpp and would assemble AND LINK a microprogram
// the project no longer has.
static bool isVuGenerated(const std::string& rel) {
    auto startsWith = [&](const char* pre) {
        const size_t n = std::strlen(pre);
        return rel.size() >= n && rel.compare(0, n, pre) == 0;
    };
    // NOT the container's, even though it shares the vu0_ prefix and the
    // directory. src/vu0/*.cpp is compiled and RUN inside the build container -
    // the host has no compiler to do it with - so a host-side sweep here would
    // delete a kernel it cannot regenerate, and the next build would link a
    // game against a driver whose microprogram had just been thrown away. The
    // container keeps its own ledger for exactly this job (src/vumain.cpp), and
    // the src/gen/vu_script* files of the VU1 half are left alone for the same
    // reason. The prefixes have to differ because the PANEL's kernel is named
    // by its author and defaults to "kernel".
    if (startsWith("src\\gen\\vu0_script")) return false;
    return startsWith("src\\gen\\vu_look") ||
           startsWith("src\\gen\\vu_custom_") ||  // pre-look name, still swept
           startsWith("src\\gen\\vu0_") ||
           startsWith("inc\\vu0_");
}

// The framework a project's own VU script is compiled against, copied INTO the
// project (docs/vu-authoring.md).
//
// It has to travel with the project rather than stay in the editor, for two
// reasons that pull the same way: the build container only ever sees the
// project directory, and VS Code needs the headers on disk to resolve
// `#include "vushader.hpp"` in the file the user is typing into. It lands
// OUTSIDE src/ on purpose - Makefile.base globs src/**/*.cpp for the PS2
// compiler, and these are host sources that would fail there loudly.
static const char* const kVuFrameworkFiles[] = {
    "vuir.hpp",  "vuir.cpp",     "vusim.hpp",    "vusim.cpp",   "vugen.hpp",
    "vugen.cpp", "vushader.hpp", "vushader.cpp", "vumain.cpp",
};

/** Where the editor's own sources are. Same shape as the engine lookup: next to
 * the exe first (a built editor sits in build/ inside its repo), then the
 * working directory (a run from the repo root). */
static fs::path editorSourceDir() {
    std::error_code ec;
    const std::string exe = platform::exePath();
    if (!exe.empty()) {
        const fs::path c = fs::path(exe).parent_path() / ".." / "src";
        if (fs::exists(c / "vushader.hpp", ec)) return fs::weakly_canonical(c, ec);
    }
    if (fs::exists(fs::path("src") / "vushader.hpp", ec)) return fs::path("src");
    return {};
}

static bool anyCppIn(const fs::path& dir) {
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) return false;
    for (const auto& e : fs::directory_iterator(dir, ec))
        if (e.is_regular_file(ec) && e.path().extension() == ".cpp") return true;
    return false;
}

bool hasVuScripts(const Project& p) {
    return anyCppIn(fs::path(p.dir) / "src" / "vu");
}

bool hasVuKernels(const Project& p) {
    return anyCppIn(fs::path(p.dir) / "src" / "vu0");
}

bool hasVuSources(const Project& p) {
    return hasVuScripts(p) || hasVuKernels(p);
}

/** Copy the framework in when the project has a script or a kernel, and take it
 * away again when the last one is deleted - a stale copy would be compiled by
 * the next build and quietly resurrect a program the project no longer
 * describes. */
static std::string syncVuFramework(const Project& p) {
    std::error_code ec;
    const fs::path dst = fs::path(p.dir) / "vugen";
    if (!hasVuSources(p)) {
        fs::remove_all(dst, ec);
        return {};
    }
    const fs::path src = editorSourceDir();
    if (src.empty())
        return "the VU framework sources are missing next to the editor - "
               "src/vu/*.cpp and src/vu0/*.cpp cannot be built";
    fs::create_directories(dst, ec);
    // THE MARKER GOES ON THE COPY, NOT ON THE SOURCE. These files are ordinary
    // hand-written sources in the editor's own repo, so a "generated" line
    // there would be a lie; in a project they are replaced on every sync, which
    // is exactly the thing the marker warns about. Same wording as every other
    // regenerated file, so CLAUDE.md's rule 1 and any grep for it cover these
    // too - without it `vugen/` was the one set of files in a generated project
    // that behaved like codegen output and said nothing.
    static const char* kCopyMarker =
        "// Generated by TyraX. Do not edit - regenerated on every build.\n"
        "// A copy of the editor's own VU framework source; change it there.\n";
    for (const char* name : kVuFrameworkFiles) {
        std::ifstream in(src / name, std::ios::binary);
        if (!in) return std::string("cannot read the VU framework file ") + name;
        std::ostringstream buf;
        buf << in.rdbuf();
        // Prepended, never appended to what is already there: the source has no
        // marker, so the result carries exactly one however many times this
        // runs.
        if (auto err = writeFile(dst / name, kCopyMarker + buf.str());
            !err.empty())
            return err;
    }
    return {};
}

// User-owned scripts live in the project's C++ namespace, which is derived from
// the project NAME - and renaming a project deliberately does not rewrite
// user-owned files. So a rename (or copying a project directory and renaming it,
// which is how people start from an example) silently leaves every script
// registering into a namespace that no longer exists.
//
// The PS2 toolchain's answer to that is forty lines of template noise about
// `no known conversion from 'Old::Thing*' to 'New::Script* const&'`, which says
// nothing about what actually happened. This says it in one line, before Docker
// is even contacted. TYRA_SCRIPT's argument is the check: the macro registers
// into <ns>::getScripts(), so a script whose class is qualified with anything
// else cannot compile, and there is no legitimate reason to write it that way.
std::string checkScriptNamespaces(const Project& p) {
    const fs::path dir = fs::path(p.dir) / "src" / "scripts";
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) return {};
    const std::string want = templates::projectNamespace(p);
    for (const auto& e : fs::directory_iterator(dir, ec)) {
        if (ec) break;
        if (!e.is_regular_file() || e.path().extension() != ".cpp") continue;
        std::ifstream in(e.path());
        std::string line;
        while (std::getline(in, line)) {
            const size_t at = line.find("TYRA_SCRIPT(");
            if (at == std::string::npos) continue;
            const size_t open = at + 12;
            const size_t sep = line.find("::", open);
            if (sep == std::string::npos) continue;  // unqualified: nothing to check
            std::string ns = line.substr(open, sep - open);
            while (!ns.empty() && (ns.front() == ' ' || ns.front() == '\t')) ns.erase(0, 1);
            while (!ns.empty() && (ns.back() == ' ' || ns.back() == '\t')) ns.pop_back();
            if (ns.empty() || ns == want) continue;
            return "Script " + e.path().filename().string() + " is in namespace \"" +
                   ns + "\" but this project's namespace is \"" + want +
                   "\" (it follows the project name). Renaming a project does not "
                   "rewrite user-owned scripts - edit src/scripts/" +
                   e.path().filename().string() + " and replace \"" + ns +
                   "\" with \"" + want + "\" (both the namespace block and the "
                   "TYRA_SCRIPT line), or delete the script if you do not need it.";
        }
    }
    return {};
}

void clampStartScene(Project& p) {
    // A hand-edited .tyra, a deleted scene or a peer on a different scene list
    // can all leave this pointing past the end - and it indexes the array the
    // GAME boots from, so an out-of-range value is a crash on the console rather
    // than a wrong-looking editor.
    if (p.startScene < 0 || p.startScene >= (int)p.scenes.size()) p.startScene = 0;
}

std::string refreshGenerated(const Project& p) {
    if (auto err = syncVuFramework(p); !err.empty()) return err;
    // ONCE. generate() is the whole codegen pass - every scene table, every
    // microprogram, every bake-derived header - and it also PRINTS the
    // diagnostics a build reports (a skipped procedural volume, a look that
    // could not claim a class). Calling it twice doubled both the work and
    // those lines, which is how this was noticed.
    const std::vector<templates::File> generated = templates::generate(p);
    for (const auto& f : generated) {
        const fs::path path = fs::path(p.dir) / templates::nativePath(f.relativePath);

        bool write = false;
        // The Makefile is fully generated (no ownership marker, like
        // docker-compose.yml): it carries the build-profile flags now - -g and
        // -leedebug for the crash reporter in debug, neither in release -
        // so it MUST refresh with the project, not just at creation.
        if (f.relativePath == "Makefile" ||
            f.relativePath == "docker-compose.yml" ||
            f.relativePath == "src\\main.cpp" ||
            f.relativePath == "inc\\terrain_config.hpp" ||
            f.relativePath == "inc\\scene_data.hpp" ||
            f.relativePath == ".vscode\\c_cpp_properties.json" ||
            f.relativePath == "src\\gen\\flow_graph.gen.cpp" ||
            f.relativePath == "src\\gen\\live_link.gen.cpp" ||
            f.relativePath == "src\\gen\\live_logic.gen.cpp" ||
            f.relativePath == "src\\gen\\live_time.gen.cpp" ||
            f.relativePath == "inc\\scripts\\live_logic.gen.hpp" ||
            f.relativePath == "src\\gen\\livelogic.built" ||
            f.relativePath == "src\\gen\\live_debug.gen.cpp" ||
            f.relativePath == "inc\\scripts\\live_debug.gen.hpp" ||
            f.relativePath == "src\\gen\\livedbg.sym" ||
            f.relativePath == "src\\gen\\live_pad.gen.cpp" ||
            f.relativePath == "inc\\live_pad.gen.hpp" ||
            f.relativePath == "src\\gen\\input_replay.gen.cpp" ||
            f.relativePath == "inc\\input_replay.gen.hpp" ||
            f.relativePath == "src\\gen\\live_tex.gen.cpp" ||
            f.relativePath == "src\\gen\\object_scripts.gen.cpp" ||
            f.relativePath == "src\\gen\\screen_fx.gen.cpp" ||
            f.relativePath == "inc\\scripts\\screen_fx.gen.hpp" ||
            f.relativePath == "inc\\scripts\\sequences.gen.hpp" ||
            f.relativePath == "src\\gen\\sequences.gen.cpp" ||
            // Endless-scroller runtime. MUST be here, not only in the --new
            // scaffold: flow_graph.gen.cpp includes scroller.gen.hpp
            // unconditionally, so a project scaffolded before this feature
            // existed (i.e. every project already on disk) regenerates that
            // include and then fails to compile with "scripts/scroller.gen.hpp:
            // No such file or directory" unless the pair is refreshed too.
            f.relativePath == "inc\\scripts\\scroller.gen.hpp" ||
            f.relativePath == "src\\gen\\scroller.gen.cpp" ||
            f.relativePath == "inc\\model_data.gen.hpp" ||
            f.relativePath == "inc\\hud_data.gen.hpp" ||
            f.relativePath == "inc\\font_data.gen.hpp" ||
            f.relativePath == "inc\\loading_data.gen.hpp" ||
            f.relativePath == "inc\\credits_data.gen.hpp" ||
            f.relativePath == "inc\\scripts\\credits.gen.hpp" ||
            f.relativePath == "src\\gen\\credits.gen.cpp" ||
            f.relativePath == "inc\\terrain_heights.gen.hpp" ||
            // World Facts store. Same reasoning as the scroller pair above:
            // flow_graph.gen.cpp and both game templates include it
            // unconditionally, so every existing project needs it refreshed
            // rather than written once at creation.
            f.relativePath == "inc\\facts.gen.hpp" ||
            f.relativePath == "inc\\nav_data.gen.hpp" ||
            f.relativePath == "inc\\scripts\\navigation.gen.hpp" ||
            f.relativePath == "src\\gen\\navigation.gen.cpp" ||
            f.relativePath == "inc\\texture_data.gen.hpp" ||
            f.relativePath == "inc\\decal_data.gen.hpp" ||
            f.relativePath == "inc\\ao_data.gen.hpp" ||
            // The trained BLSS network (docs/neural-upscaler.md). Only ever IN
            // `generated` while the upscaler is enabled - but when it is there
            // it MUST be rewritten, or a project that predates the feature (or
            // that was just retrained with --blss-train) keeps compiling the
            // untrained weights it was first scaffolded with.
            f.relativePath == "inc\\blss_net.gen.hpp" ||
            // The BLSS build refusal, in a TU of its own so the compiler prints
            // it once rather than once per includer of scene_data.hpp. Only in
            // `generated` while the project actually clashes; the sweep below
            // deletes it when it stops, which is the half that matters - a
            // stale refusal would block a build that is fine.
            f.relativePath == "src\\gen\\blss_interlock.gen.cpp" ||
            f.relativePath == "inc\\daynight.gen.hpp" ||
            f.relativePath == "inc\\probe_data.gen.hpp" ||
            f.relativePath == "inc\\prefab_data.gen.hpp" ||
            f.relativePath == "inc\\procedural.gen.hpp" ||
            f.relativePath == "src\\gen\\procedural.gen.cpp" ||
            f.relativePath == "inc\\save_system.gen.hpp" ||
            f.relativePath == "src\\save_system.gen.cpp" ||
            f.relativePath == "inc\\menu_data.gen.hpp" ||
            f.relativePath == "inc\\icon_data.gen.hpp" ||
            f.relativePath == "inc\\input_map.gen.hpp" ||
            f.relativePath == "src\\gen\\input_map.gen.cpp" ||
            f.relativePath == "inc\\scripts\\vu_programs.gen.hpp" ||
            f.relativePath == "inc\\scripts\\vu_scripts.gen.hpp" ||
            // Only ever IN `generated` while the project has no VU sources at
            // all (templates::generate leaves both stubs out otherwise, so the
            // container's real headers survive) - but when it is there it has
            // to be rewritten, or deleting the last kernel leaves a header
            // naming driver classes that no longer exist.
            f.relativePath == "inc\\scripts\\vu0_kernels.gen.hpp" ||
            f.relativePath == "src\\gen\\vu_programs.gen.cpp" ||
            // The project's own microprograms and their EE classes are named
            // after what they are, so they cannot be listed by hand - and a
            // stale one still LINKS (Makefile.base globs src/**.vclpp), which
            // is why the sweep below deletes the ones a rebuild did not
            // produce (docs/vu-authoring.md).
            isVuGenerated(f.relativePath)) {
            write = true;  // editor-owned, always in sync with project data
        } else if (f.relativePath == "src\\terrain_game.cpp" ||
                   f.relativePath == "inc\\terrain_game.hpp" ||
                   f.relativePath == "inc\\controls.hpp" ||
                   f.relativePath == "inc\\scripts\\script.hpp" ||
                   f.relativePath == "inc\\scripts\\flow_nodes.hpp") {
            // Regenerate only while the ownership marker is present: the user
            // deletes that line to take the file over.
            std::ifstream existing(path, std::ios::binary);
            if (!existing) {
                write = true;
            } else {
                std::stringstream content;
                content << existing.rdbuf();
                std::string firstLine = content.str().substr(0, content.str().find('\n'));
                write = firstLine.find("Generated by TyraX") != std::string::npos;
            }
        } else if (f.relativePath == ".vscode\\extensions.json") {
            // Write-once like the notices below, EXCEPT that a recommendation
            // the editor added later has to reach projects that already exist -
            // which is every project with the problem. So an existing file is
            // MERGED (see templates::vscodeExtensionsMerged): the ids the
            // editor knows about are ensured present, anything the user added
            // stays where it is, and an already-complete file is not rewritten.
            std::error_code ec;
            if (!fs::exists(path, ec)) {
                write = true;
            } else {
                std::ifstream existing(path, std::ios::binary);
                std::stringstream content;
                content << existing.rdbuf();
                const std::string merged =
                    templates::vscodeExtensionsMerged(content.str());
                if (!merged.empty()) {
                    if (auto err = writeFile(path, merged); !err.empty()) return err;
                }
                write = false;
            }
        } else if (f.relativePath == "THIRD-PARTY-NOTICES.txt" ||
                   // The launcher scripts and the repo hygiene files. Same
                   // write-once rule as the notices, and for the same reason:
                   // they have no ownership marker, so an existing one is the
                   // user's. What makes them belong HERE rather than only in
                   // project::create is that a project scaffolded before one of
                   // them existed never gets it otherwise - measured on this
                   // repo's own examples/, where 21 of 34 projects had no
                   // run.sh at all and a Linux user opening one found only a
                   // PowerShell launcher. run.ps1/windows-pcsx2.ps1 are listed
                   // beside it because the pair rule is the whole point: a
                   // platform's launcher must never be the one that goes
                   // missing.
                   f.relativePath == "run.ps1" ||
                   f.relativePath == "run.sh" ||
                   f.relativePath == "windows-pcsx2.ps1" ||
                   f.relativePath == ".gitattributes" ||
                   f.relativePath == "COLLABORATION.md" ||
                   f.relativePath == ".gitignore" ||
                   f.relativePath == "bin\\.gitignore" ||
                   f.relativePath == "obj\\.gitignore") {
            // Static content: write it once so existing projects pick it up on
            // the next build, but never clobber what the user may have added
            // (neither file has room for the ownership-marker line the ownable
            // sources above use). For the notices this is the load-bearing
            // behavior, not a nicety - it is the file an author extends with
            // their own credits, and regenerating over that would delete work
            // AND leave them shipping a file they no longer believe in.
            std::error_code ec;
            write = !fs::exists(path, ec);
        }

        if (write) {
            if (auto err = writeFile(path, f.content); !err.empty()) return err;
        }
    }

    // Migration: the project's own .gitignore is write-once (above), so a
    // project made before format migrations existed never learned to ignore
    // `_backup/` - and `--migrate` writes exactly that directory, a full copy
    // of the .tyra, objects/, heights, splat and node files. Without the rule
    // the first migration of an older project offers all of it for commit.
    // Same append-if-missing shape as res/.gitignore below.
    {
        const fs::path ignore = fs::path(p.dir) / ".gitignore";
        std::error_code ec;
        if (fs::exists(ignore, ec)) {
            std::ifstream in(ignore, std::ios::binary);
            std::stringstream content;
            content << in.rdbuf();
            in.close();
            std::string text = content.str();
            bool grew = false;
            if (text.find("_backup/") == std::string::npos) {
                if (!text.empty() && text.back() != '\n') text += '\n';
                text +=
                    "\n# Pre-migration snapshots of the project's own model "
                    "files, written by a format\n# migration "
                    "(docs/format-versioning.md). Local safety copies, not "
                    "source: the\n# history that matters is already in git.\n"
                    "_backup/\n";
                grew = true;
            }
            // Same again for the Debugger's saved captures: the game's own
            // screenshots land in screenshots/ as PNGs (docs/devkit.md), one
            // per capture, and a project made before that would offer every
            // one of them for commit.
            if (text.find("screenshots/") == std::string::npos) {
                if (!text.empty() && text.back() != '\n') text += '\n';
                text +=
                    "\n# Pictures the running game took of itself, kept by the "
                    "Debugger's Screen tab\n# (docs/devkit.md). Yours to look "
                    "at and to throw away.\nscreenshots/\n";
                grew = true;
            }
            // And the input recorder's working files (docs/input-replay.md).
            // bin/.gitignore already covers the whole directory, so this is
            // the readable list and the fallback for a project that took bin/
            // under its own control - the same reason every other channel is
            // spelled out there. Note recordings/ is deliberately NOT ignored:
            // a saved recording is meant to be committed next to the bug it
            // reproduces.
            if (text.find("bin/replay.") == std::string::npos) {
                if (!text.empty() && text.back() != '\n') text += '\n';
                text +=
                    "\n# The input recorder's working files "
                    "(docs/input-replay.md). These are the\n# CHANNEL, not the "
                    "recordings: a recording you want to keep is saved into\n"
                    "# recordings/*.tyrarep, which IS tracked on purpose.\n"
                    "bin/replay.in\nbin/replay.arm\nbin/replay.out\n"
                    "bin/replay.stop\nbin/replay.st\n";
                grew = true;
            }
            if (grew) {
                if (auto err = writeFile(ignore, text); !err.empty()) return err;
            }
        }
    }

    // Migration: res/.gitignore is written at project creation only (the user
    // may have added rules), but static models now bake to res/models/*.tmdl -
    // a few hundred KB per model, regenerated on every build. A project made
    // before that would start tracking them, so append the rule if it is
    // missing. Same shape as the .tskl/.tanm rules already in the file.
    {
        const fs::path ignore = fs::path(p.dir) / "res" / ".gitignore";
        std::error_code ec;
        if (fs::exists(ignore, ec)) {
            std::ifstream in(ignore, std::ios::binary);
            std::stringstream content;
            content << in.rdbuf();
            in.close();
            std::string text = content.str();
            bool grew = false;
            if (text.find("/models/*.tmdl") == std::string::npos) {
                if (!text.empty() && text.back() != '\n') text += '\n';
                text +=
                    "\n# Baked static-model output (the .obj next to it is the "
                    "source;\n# regenerated on every build - "
                    "docs/model-pipeline.md).\n/models/*.tmdl\n";
                grew = true;
            }
            // Same migration for the credits page strips: the roll lives in the
            // .tyra, these are only its pixels (docs/credits.md).
            if (text.find("/credits/pages/") == std::string::npos) {
                if (!text.empty() && text.back() != '\n') text += '\n';
                text +=
                    "\n# Baked credits page strips - regenerated on every build "
                    "(docs/credits.md).\n# The images an Image block points at "
                    "live in res/credits/ and stay checked in.\n"
                    "/credits/pages/\n";
                grew = true;
            }
            // And again for the memory card icon: res/save/ is icon.sys +
            // list.icn, rebaked from the .tyra on every build. Without this a
            // project made before the Save Editor starts tracking tens of KB
            // of derived bytes that churn whenever the icon settings change.
            if (text.find("/save/") == std::string::npos) {
                if (!text.empty() && text.back() != '\n') text += '\n';
                text +=
                    "\n# Baked memory card icon - icon.sys + list.icn, "
                    "regenerated on every\n# build from the Save Editor's "
                    "settings (docs/save-editor.md).\n/save/\n";
                grew = true;
            }
            if (grew)
                if (auto err = writeFile(ignore, text); !err.empty()) return err;
        }
    }

    // Sweep the BLSS build refusal when the project no longer clashes. Same
    // reasoning as the VU sweep below - Makefile.base globs src/**/*.cpp, so a
    // leftover would keep refusing a build that is now perfectly fine, and
    // there is no worse failure than a guard that fires after the thing it
    // guards against is gone (docs/neural-upscaler.md, Limitations).
    {
        bool wanted = false;
        for (const auto& f : generated)
            wanted |= f.relativePath == "src\\gen\\blss_interlock.gen.cpp";
        if (!wanted) {
            std::error_code ec;
            fs::remove(fs::path(p.dir) / "src" / "gen" / "blss_interlock.gen.cpp",
                       ec);
        }
    }

    // Sweep VU files a rebuild did NOT produce: a program removed from the
    // project leaves its .vclpp behind, and Makefile.base's glob would keep
    // assembling and linking it - a microprogram nobody asked for, taking micro
    // memory from the ones that are.
    {
        std::set<std::string> wanted;
        for (const auto& f : generated)
            if (isVuGenerated(f.relativePath)) wanted.insert(f.relativePath);
        std::error_code ec;
        for (const char* dir : {"src\\gen", "inc"}) {
            const fs::path d = fs::path(p.dir) / templates::nativePath(dir);
            if (!fs::exists(d, ec)) continue;
            for (const auto& e : fs::directory_iterator(d, ec)) {
                if (!e.is_regular_file()) continue;
                const std::string rel =
                    std::string(dir) + "\\" + e.path().filename().string();
                if (isVuGenerated(rel) && wanted.find(rel) == wanted.end())
                    fs::remove(e.path(), ec);
            }
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
            if (auto err = writeFile(fs::path(p.dir) / templates::nativePath(f.relativePath),
                                     f.content);
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
            if (auto err = writeFile(fs::path(p.dir) / templates::nativePath(f.relativePath),
                                     f.content);
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
    // "Checking memory card" overlay: baked text sprite, written when missing
    // so it stays user-replaceable like the save-menu sprites above.
    {
        const fs::path busy = fs::path(p.dir) / "res" / "hud" / "save-busy.png";
        std::error_code ec;
        if (!fs::exists(busy, ec)) {
            std::vector<unsigned char> png;
            if (!menubake::bakeTextPNG(savebake::busyText(), p, png))
                return "Save overlay bake failed (no usable TTF font found)";
            fs::create_directories(busy.parent_path(), ec);
            std::ofstream f(busy, std::ios::binary);
            if (!f) return "Cannot write save overlay: " + busy.string();
            f.write(reinterpret_cast<const char*>(png.data()),
                    (std::streamsize)png.size());
        }
    }
    // The async write's spinner sheet, same write-when-missing rule so an
    // author can drop in their own strip of savebake::kSpinnerFrames cells.
    {
        const fs::path spin = fs::path(p.dir) / "res" / "hud" / "save-spinner.png";
        std::error_code ec;
        if (!fs::exists(spin, ec)) {
            std::vector<unsigned char> png;
            if (!savebake::spinnerPNG(png)) return "Save spinner bake failed";
            fs::create_directories(spin.parent_path(), ec);
            std::ofstream f(spin, std::ios::binary);
            if (!f) return "Cannot write save spinner: " + spin.string();
            f.write(reinterpret_cast<const char*>(png.data()),
                    (std::streamsize)png.size());
        }
    }
    // Memory card icon (icon.sys + list.icn): derived from project data
    // (title, icon image), so always rebaked like the menu panels. The
    // generated save system copies them onto the card next to the slots.
    {
        const fs::path saveDir = fs::path(p.dir) / "res" / "save";
        std::error_code ec;
        fs::create_directories(saveDir, ec);
        const std::vector<unsigned char> sys = savebake::iconSys(p);
        const std::vector<unsigned char> icn = savebake::iconIcn(p);
        // Through writeFile, so an unchanged icon keeps its mtime. list.icn is
        // the biggest thing this bake produces (tens of KB) and it is rebaked
        // on EVERY build from unchanged project data - a raw rewrite made the
        // Runner's rsync ship it to the container every time.
        auto write = [&](const char* name,
                         const std::vector<unsigned char>& bytes) {
            return writeFile(saveDir / name,
                             std::string(reinterpret_cast<const char*>(bytes.data()),
                                         bytes.size()));
        };
        if (auto err = write("icon.sys", sys); !err.empty()) return err;
        if (auto err = write("list.icn", icn); !err.empty()) return err;
    }

    // Lens flare sprites: procedural (no font), written whenever the project
    // can show the flare - an authored per-scene amount OR a Set Flare node
    // that could raise it at runtime. FLARE_USED in scene_data.hpp gates the
    // game-side texture load, so it matches this exact predicate (see
    // templates::projectUsesFlare).
    if (templates::projectUsesFlare(p)) {
        for (int kind = 0; kind < 2; ++kind) {
            std::vector<unsigned char> png;
            if (!menubake::bakeFlarePNG(kind, png))
                return "Lens flare sprite bake failed";
            const fs::path path =
                fs::path(p.dir) / "res" / "hud" / menubake::flareFileName(kind);
            std::error_code ec;
            fs::create_directories(path.parent_path(), ec);
            std::ofstream f(path, std::ios::binary);
            if (!f) return "Cannot write flare sprite: " + path.string();
            f.write(reinterpret_cast<const char*>(png.data()),
                    (std::streamsize)png.size());
        }
    }
    // Blob shadows reuse the soft glow as their alpha mask - bake it even
    // when the flare is off (kind 0 only; the flare block above already
    // wrote it otherwise).
    if (!templates::projectUsesFlare(p) && p.settings.blobShadows) {
        std::vector<unsigned char> png;
        if (!menubake::bakeFlarePNG(0, png))
            return "Blob shadow sprite bake failed";
        const fs::path path =
            fs::path(p.dir) / "res" / "hud" / menubake::flareFileName(0);
        std::error_code ec;
        fs::create_directories(path.parent_path(), ec);
        std::ofstream f(path, std::ios::binary);
        if (!f) return "Cannot write blob shadow sprite: " + path.string();
        f.write(reinterpret_cast<const char*>(png.data()),
                (std::streamsize)png.size());
    }
    // Day/night cycle sky bodies (docs/day-night-cycle.md). Same arrangement as
    // the flare sprites above: baked only when a scene resolves to an enabled
    // cycle, and DAYCYCLE_USED gates the matching game-side texture load.
    if (const DayCycle* cyc = templates::projectMoonCycle(p)) {
        {
            std::vector<unsigned char> png;
            if (!menubake::bakeSunPNG(png)) return "Sun disc bake failed";
            const fs::path path = fs::path(p.dir) / "res" / "hud" / "sun-disc.png";
            std::error_code ec;
            fs::create_directories(path.parent_path(), ec);
            std::ofstream f(path, std::ios::binary);
            if (!f) return "Cannot write sun disc: " + path.string();
            f.write(reinterpret_cast<const char*>(png.data()),
                    (std::streamsize)png.size());
        }
        {
            // A user texture is a project asset path; empty falls back to the
            // embedded NASA map inside bakeMoonPNG.
            const std::string srcAbs =
                cyc->moonTexture.empty() ? std::string() : p.filePath(cyc->moonTexture);
            std::vector<unsigned char> png;
            if (!menubake::bakeMoonPNG(cyc->moonPhase, srcAbs, png))
                return "Moon disc bake failed";
            const fs::path path = fs::path(p.dir) / "res" / "hud" / "moon-disc.png";
            std::error_code ec;
            fs::create_directories(path.parent_path(), ec);
            std::ofstream f(path, std::ios::binary);
            if (!f) return "Cannot write moon disc: " + path.string();
            f.write(reinterpret_cast<const char*>(png.data()),
                    (std::streamsize)png.size());
        }
    }
    // Light-beam corona (Point Light > Beam): its own RGB-shaped sprite. The
    // night sky draws its stars through the SAME sprite - a star is a soft
    // radial dot, and an untextured quad would be a hard square - so a
    // starfield project bakes it whether or not it has a single beam.
    if (templates::projectUsesBeams(p) || templates::projectStarCycle(p)) {
        for (int kind = 2; kind < 3; ++kind) {
            std::vector<unsigned char> png;
            if (!menubake::bakeFlarePNG(kind, png))
                return "Lens flare sprite bake failed";
            const fs::path path =
                fs::path(p.dir) / "res" / "hud" / menubake::flareFileName(kind);
            std::error_code ec;
            fs::create_directories(path.parent_path(), ec);
            std::ofstream f(path, std::ios::binary);
            if (!f) return "Cannot write flare sprite: " + path.string();
            f.write(reinterpret_cast<const char*>(png.data()),
                    (std::streamsize)png.size());
        }
    }

    // The camera flashlight's gobo (docs/flashlight.md): the pool patch under
    // the beam takes its STs from the light's own frustum, so this image IS the
    // shape of the light. Gated exactly like the sprites above -
    // FLASHLIGHT_USED in scene_data.hpp reads the same predicate, and a project
    // with no flashlight pays no GS VRAM for it. Written through writeFile so
    // an unchanged bake keeps its mtime and the build stays incremental.
    if (templates::projectUsesFlashlight(p)) {
        std::vector<unsigned char> png;
        if (!menubake::bakeFlashGoboPNG(png))
            return "Flashlight gobo bake failed";
        if (auto err = writeFile(
                fs::path(p.dir) / "res" / "hud" / "flashlight-gobo.png",
                std::string(reinterpret_cast<const char*>(png.data()),
                            png.size()));
            !err.empty())
            return err;
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

        // The three textures a stylesheet can add (docs/menu-styles.md): the
        // row-state atlas, the scrolling row strip and the description atlas.
        // Each bake returns false when the menu does not need it, and that is
        // also the signal to DELETE a stale file - a menu that stops scrolling
        // must stop shipping a strip the game would still load into VRAM.
        struct StyleTex {
            bool (*bake)(const GameMenu&, const Project&,
                         std::vector<unsigned char>&);
            std::string (*name)(const std::string&);
            const char* what;
        };
        static const StyleTex kStyleTex[] = {
            {&menubake::bakeStateAtlasPNG, &menulayout::stateAtlasFileName,
             "menu row states"},
            {&menubake::bakeListPNG, &menulayout::listFileName, "menu row strip"},
            {&menubake::bakeDescAtlasPNG, &menulayout::descAtlasFileName,
             "menu descriptions"},
            {&menubake::bakeBgAnimPNG, &menulayout::bgAnimFileName,
             "menu background layer"},
        };
        for (const StyleTex& st : kStyleTex) {
            const fs::path path =
                fs::path(p.dir) / "res" / "menus" / st.name(m.name);
            std::vector<unsigned char> png;
            if (!st.bake(m, p, png)) {
                std::error_code rec;
                fs::remove(path, rec);
                continue;
            }
            std::ofstream tf(path, std::ios::binary);
            if (!tf)
                return std::string("Cannot write ") + st.what + ": " + path.string();
            tf.write(reinterpret_cast<const char*>(png.data()),
                     (std::streamsize)png.size());
        }
    }

    // The sheen band, shared by every menu whose sheet sweeps one. Procedural
    // like the flare sprites, and written only while something uses it - a
    // texture the game never draws still costs the disc and the loader.
    {
        bool wantSheen = false;
        for (const GameMenu& m : p.menus)
            wantSheen |= menustyle::animation(menulayout::sheetFor(m),
                                              menustyle::Animation::Panel) != nullptr;
        const fs::path path = fs::path(p.dir) / "res" / "menus" / "sheen.png";
        std::error_code ec;
        if (!wantSheen) {
            fs::remove(path, ec);
        } else {
            std::vector<unsigned char> png;
            if (!menubake::bakeSheenPNG(png)) return "Menu sheen bake failed";
            fs::create_directories(path.parent_path(), ec);
            std::ofstream f(path, std::ios::binary);
            if (!f) return "Cannot write menu sheen: " + path.string();
            f.write(reinterpret_cast<const char*>(png.data()),
                    (std::streamsize)png.size());
        }
    }

    // Credits rolls: the whole roll baked into res/credits/pages/<name>-<k>.png
    // page strips (+ the skip-hint sprite), derived from project data like the
    // menu panels above, so ALWAYS rebaked. That folder is then swept of
    // anything no roll claims - a shortened roll or a deleted one would
    // otherwise keep shipping pages the game never draws. The sweep is why the
    // bake has a folder of its OWN: a roll's Image blocks point at ordinary
    // assets in res/credits/, and those must survive a build.
    if (!p.credits.empty()) {
        const fs::path dir = fs::path(p.dir) / "res" / "credits" / "pages";
        std::error_code ec;
        fs::create_directories(dir, ec);
        std::set<std::string> expected;
        for (const CreditsRoll& r : p.credits) {
            std::vector<std::vector<unsigned char>> pages;
            menubake::CreditsLayout layout;
            if (!menubake::bakeCreditsPagesPNG(r, p, pages, layout))
                return "Credits bake failed (no usable TTF font found)";
            for (size_t k = 0; k < pages.size(); ++k) {
                const std::string file = menubake::creditsPageFileName(r.name, (int)k);
                std::ofstream f(dir / file, std::ios::binary);
                if (!f) return "Cannot write credits page: " + (dir / file).string();
                f.write(reinterpret_cast<const char*>(pages[k].data()),
                        (std::streamsize)pages[k].size());
                expected.insert(file);
            }
            if (!r.showSkipHint || !r.skippable || r.skipHint.empty()) continue;
            std::vector<unsigned char> png;
            const HudText hint = menubake::creditsHintText(r);
            if (!menubake::bakeTextPNG(hint, p, png))
                return "Credits skip hint bake failed (no usable TTF font found)";
            const std::string file = menubake::creditsHintFileName(r.name);
            std::ofstream f(dir / file, std::ios::binary);
            if (!f) return "Cannot write credits hint: " + (dir / file).string();
            f.write(reinterpret_cast<const char*>(png.data()),
                    (std::streamsize)png.size());
            expected.insert(file);
        }
        for (const auto& e : fs::directory_iterator(dir, ec)) {
            if (!e.is_regular_file()) continue;
            const std::string name = e.path().filename().string();
            if (name.size() > 4 && name.rfind(".png") == name.size() - 4 &&
                !expected.count(name))
                fs::remove(e.path(), ec);
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

    // Text icons ({{name}} placeholders, docs/text-icons.md). The built-in
    // pad-button images are GENERATED when their file is missing and never
    // overwritten afterwards - that is what makes "override an icon" simply
    // mean "replace the PNG". The icon sheet the runtime text path blits from
    // is always rebaked, like the font atlases: its rects live in
    // icon_data.gen.hpp and a stale sheet would misplace every icon.
    for (const TextIcon& ic : p.textIcons) {
        if (ic.path.empty()) continue;
        const fs::path path = fs::path(p.dir) / ic.path;
        std::error_code ec;
        if (fs::exists(path, ec)) continue;
        std::vector<unsigned char> png;
        if (!menubake::bakeBuiltinIconPNG(ic.name, menubake::kIconBakeSize, png))
            continue;  // a user icon with no file yet: not ours to invent
        fs::create_directories(path.parent_path(), ec);
        std::ofstream f(path, std::ios::binary);
        if (!f) return "Cannot write text icon: " + path.string();
        f.write(reinterpret_cast<const char*>(png.data()), (std::streamsize)png.size());
    }
    {
        std::vector<unsigned char> png;
        const fs::path path = fs::path(p.dir) / "res" / "hud" / "icons.png";
        std::error_code ec;
        if (menubake::bakeIconAtlasPNG(p, png)) {
            fs::create_directories(path.parent_path(), ec);
            std::ofstream f(path, std::ios::binary);
            if (!f) return "Cannot write icon sheet: " + path.string();
            f.write(reinterpret_cast<const char*>(png.data()),
                    (std::streamsize)png.size());
        } else {
            fs::remove(path, ec);  // no icons left: don't ship a stale sheet
        }
    }

    // The interaction prompts in text mode: baked like a HUD text into a fixed
    // file the codegen points the sprite at. Removed when the prompt is not on
    // text, so res/ never ships a sprite nothing draws.
    {
        struct PromptBake {
            const char* file;
            const HudText* text;
            bool on;
            const char* label;
        };
        const PromptBake bakes[] = {
            {"use-text.png", &p.usePromptText,
             p.usePromptIsText && !p.usePromptText.text.empty(), "USE"},
            {"pick-text.png", &p.pickPromptText,
             p.pickPromptIsText && !p.pickPromptText.text.empty(), "PICK UP"},
        };
        for (const PromptBake& b : bakes) {
            const fs::path path = fs::path(p.dir) / "res" / "hud" / b.file;
            std::error_code ec;
            if (!b.on) {
                fs::remove(path, ec);
                continue;
            }
            std::vector<unsigned char> png;
            // bakePromptPNG, not bakeTextPNG: the action glyphs are left out so
            // the game can draw the LIVE bindings' over the holes.
            std::vector<menubake::PromptIconSlot> slots;
            if (!menubake::bakePromptPNG(*b.text, p, png, slots))
                return std::string(b.label) +
                       " prompt text bake failed (no usable TTF font found)";
            fs::create_directories(path.parent_path(), ec);
            std::ofstream f(path, std::ios::binary);
            if (!f) return "Cannot write prompt text: " + path.string();
            f.write(reinterpret_cast<const char*>(png.data()),
                    (std::streamsize)png.size());
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
