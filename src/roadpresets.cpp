// Road presets (docs/roads.md "Road presets"). See roadpresets.hpp.
#include "roadpresets.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>

#include "json.hpp"
#include "project.hpp"
#include "roadbridge.hpp"
#include "roaddetail.hpp"
#include "roadrail.hpp"
#include "roadtex.hpp"

namespace roadpresets {
namespace {

std::string mtl(const char* stem) { return std::string(roadtex::kDir) + "/" + stem + ".mtl"; }

roadfurn::Line line(float spacing, int side, float offset) {
    roadfurn::Line l;
    l.spacing = spacing;
    l.side = side;
    l.offset = offset;
    return l;
}

std::vector<Preset> makeBuiltins() {
    std::vector<Preset> out;
    // Every paved preset shares rank Local and the road-junction intersection
    // material, so ANY two of them meet in a node patch (a different rank or
    // material would make the higher road run through instead - docs/roads.md
    // "Crossings").
    {
        Preset p;
        p.key = "city-street";
        p.name = "City street";
        p.help = "Two lanes, kerbs, 2.5-unit slab pavements, zebras at its junctions, "
                 "give-way signs, staggered street lamps and a little wear.";
        p.width = 7.5f;
        p.surface = mtl("road-2lane");
        p.intersection = mtl("road-junction");
        p.markings = 2;
        p.kerb = true;
        p.pavement = 2.5f;
        p.pavementMaterial = mtl("pavement-slabs");
        p.details = 0.5f;
        p.furniture.lamps = line(16.0f, roadfurn::kAlternate, 0.6f);
        p.furniture.signs = roadfurn::kSignGiveWay;
        out.push_back(p);
    }
    {
        Preset p;
        p.key = "avenue-tram";
        p.name = "Avenue with tram";
        p.help = "Four lanes with two flush tram tracks down the middle, kerbs and "
                 "3-unit pavements, lamps on both walks, traffic lights at its "
                 "four-way junctions.";
        p.width = 13.5f;
        p.surface = mtl("road-4lane");
        p.intersection = mtl("road-junction");
        p.markings = 2;
        p.kerb = true;
        p.pavement = 3.0f;
        p.pavementMaterial = mtl("pavement-slabs");
        p.kind = roadrail::kTram;
        p.tracks = 2;
        p.details = 0.4f;
        p.furniture.lamps = line(14.0f, roadfurn::kBoth, 0.6f);
        p.furniture.signs = roadfurn::kSignGiveWay;
        p.furniture.signals = true;
        out.push_back(p);
    }
    {
        Preset p;
        p.key = "boulevard";
        p.name = "Boulevard";
        p.help = "Four lanes, kerbs, wide 4-unit pavements lined with trees on both "
                 "sides, staggered lamps, zebras and traffic lights.";
        p.width = 13.5f;
        p.surface = mtl("road-4lane");
        p.intersection = mtl("road-junction");
        p.markings = 2;
        p.kerb = true;
        p.pavement = 4.0f;
        p.pavementMaterial = mtl("pavement-slabs");
        p.details = 0.4f;
        p.furniture.trees = line(10.0f, roadfurn::kBoth, 2.6f);
        p.furniture.lamps = line(20.0f, roadfurn::kAlternate, 0.6f);
        p.furniture.signs = roadfurn::kSignGiveWay;
        p.furniture.signals = true;
        out.push_back(p);
    }
    {
        Preset p;
        p.key = "country-road";
        p.name = "Country road";
        p.help = "Worn two-lane asphalt with dirt-and-gravel verges, no kerbs, soft "
                 "edges fading into the ground and give-way signs.";
        p.width = 8.0f;
        p.surface = mtl("road-country");
        p.intersection = mtl("road-junction");
        p.edgeFade = 0.75f;
        p.grip = 0.95f;
        p.details = 0.3f;
        p.furniture.signs = roadfurn::kSignGiveWay;
        out.push_back(p);
    }
    {
        Preset p;
        p.key = "dirt-track";
        p.name = "Dirt track";
        p.help = "A rutted dirt track, rank Track: it stops at a street's edge and "
                 "trails mud onto it; two tracks meet on a dirt patch.";
        p.width = 5.0f;
        p.surface = mtl("road-dirt");
        p.intersection = mtl("road-dirt-junction");
        p.rank = 0;
        p.spill = 3.0f;
        p.edgeFade = 1.5f;
        p.grip = 0.6f;
        p.markings = 0;
        out.push_back(p);
    }
    {
        Preset p;
        p.key = "railway";
        p.name = "Railway";
        p.help = "A single-track railway on a ballast bed, rank Track so a street "
                 "runs over it as a level crossing (the Kind = Railway preset).";
        p.width = roadtex::kSleeperLength + 1.0f;
        p.surface = mtl("rail-ballast");
        p.intersection = mtl("rail-junction");
        p.rank = 0;
        p.spill = 0.0f;
        p.markings = 0;
        p.grip = 0.7f;
        p.kind = roadrail::kRail;
        out.push_back(p);
    }
    {
        Preset p;
        p.key = "highway";
        p.name = "Highway";
        p.help = "Four wide lanes with solid edge lines and a double centre line, "
                 "no kerbs or pavements, tall lamps on both sides.";
        p.width = 15.0f;
        p.surface = mtl("road-highway");
        p.intersection = mtl("road-junction");
        p.details = 0.15f;
        p.furniture.lamps = line(30.0f, roadfurn::kBoth, 1.0f);
        out.push_back(p);
    }
    {
        Preset p;
        p.key = "alley";
        p.name = "Alley";
        p.help = "A narrow cobbled lane with no markings or kerbs and plenty of wear.";
        p.width = 4.5f;
        p.surface = mtl("road-cobble");
        p.intersection = mtl("road-junction");
        p.markings = 0;
        p.grip = 0.9f;
        p.details = 0.6f;
        out.push_back(p);
    }
    return out;
}

// Recipes only presets use; the seeded ones come from roadtex::presets().
bool extraRecipe(const std::string& stem, roadtex::RoadTexParams* out) {
    roadtex::RoadTexParams p;
    if (stem == "road-country") {
        p.lanes = 2;
        p.edge.style = roadtex::kLineNone;
        p.width = 8.0f;
        p.shoulder = 0.9f;
        p.wear = 0.55f;
        p.grime = 0.4f;
        p.cracks = 0.45f;
        p.seed = 11;
    } else if (stem == "road-highway") {
        p.lanes = 4;
        p.width = 15.0f;
        p.centre.style = roadtex::kLineDouble;
        for (int k = 0; k < 3; ++k) p.centre.colour[k] = roadtex::kYellow[k];
        p.centre.width = 0.12f;
        p.edge.width = 0.2f;
        p.wear = 0.25f;
        p.seed = 12;
    } else if (stem == "road-dirt-junction") {
        p.surface = roadtex::kDirt;
        p.lanes = 0;
        p.edge.style = roadtex::kLineNone;
        p.intersection = true;
        p.wear = 0.6f;
        p.seed = 13;
    } else {
        return false;
    }
    *out = p;
    return true;
}

std::string fmt(float v) {
    char b[32];
    std::snprintf(b, sizeof b, "%.6g", v);
    return b;
}

}  // namespace

const std::vector<Preset>& builtins() {
    static const std::vector<Preset> table = makeBuiltins();
    return table;
}

std::string keyOf(const std::string& name) {
    std::string s;
    for (char ch : name) {
        const unsigned char u = (unsigned char)ch;
        if (std::isalnum(u)) s += (char)std::tolower(u);
        else if (!s.empty() && s.back() != '-') s += '-';
    }
    while (!s.empty() && s.back() == '-') s.pop_back();
    return s.empty() ? "preset" : s;
}

const Preset* find(const std::vector<Preset>& project, const std::string& keyOrName) {
    const std::string k = keyOf(keyOrName);
    for (const Preset& p : project)
        if (p.key == keyOrName || keyOf(p.name) == k || p.key == k) return &p;
    for (const Preset& p : builtins())
        if (p.key == keyOrName || keyOf(p.name) == k || p.key == k) return &p;
    return nullptr;
}

std::vector<const Preset*> all(const std::vector<Preset>& project) {
    std::vector<const Preset*> out;
    for (const Preset& p : builtins()) out.push_back(&p);
    for (const Preset& p : project) out.push_back(&p);
    return out;
}

bool materialRecipe(const std::string& stem, roadtex::RoadTexParams* out) {
    for (const roadtex::Preset& pr : roadtex::presets())
        if (stem == pr.name) {
            *out = pr.params;
            return true;
        }
    return extraRecipe(stem, out);
}

std::vector<std::string> ensureMaterials(const std::string& projectDir, const Preset& p,
                                         std::string* err) {
    namespace fs = std::filesystem;
    std::vector<std::string> written;
    if (projectDir.empty()) return written;
    const std::string prefix = std::string(roadtex::kDir) + "/";
    for (const std::string* path : {&p.surface, &p.intersection, &p.pavementMaterial}) {
        if (path->empty() || path->rfind(prefix, 0) != 0) continue;
        std::error_code ec;
        if (fs::exists(fs::path(projectDir) / *path, ec)) continue;
        const std::string stem = fs::path(*path).stem().string();
        roadtex::RoadTexParams rp;
        if (!materialRecipe(stem, &rp)) continue;
        std::string e;
        const std::string out = roadtex::writeAssets(projectDir, stem, rp, &e);
        if (out.empty()) {
            if (err) *err = e;
            continue;
        }
        written.push_back(out);
    }
    // The node paint and the details atlas are generated assets too.
    if (p.markings > 0) roadtex::ensurePaintTexture(projectDir);
    if (p.details > 0.0f) roaddetail::ensureAtlas(projectDir);
    return written;
}

void apply(const Preset& p, SceneObject& o, float unitsPerMeter) {
    o.roadWidth = std::clamp(p.width, 1.0f, 24.0f);
    o.roadSampleStep = std::clamp(p.sampleStep, 1.0f, 2.0f);
    o.roadGrip = std::clamp(p.grip, 0.1f, 1.5f);
    o.roadRank = std::clamp(p.rank, 0, 2);
    o.roadSpill = std::clamp(p.spill, 0.0f, 8.0f);
    o.roadEdgeFade = std::clamp(p.edgeFade, 0.0f, 4.0f);
    o.roadMarkings = std::clamp(p.markings, 0, 2);
    o.roadKerb = p.kerb;
    o.roadKerbHeight = p.kerbHeight;
    o.roadKerbWidth = p.kerbWidth;
    o.roadPavement = std::clamp(p.pavement, 0.0f, 6.0f);
    o.roadPavementMaterial = p.pavementMaterial;
    o.roadTexture = p.surface;
    o.roadIntersectionTexture = p.intersection;
    o.roadKind = std::clamp(p.kind, 0, 2);
    o.roadTracks = std::clamp(p.tracks, 1, 2);
    const float upm = unitsPerMeter > 0.0f ? unitsPerMeter : 1.0f;
    o.roadRailGauge = p.gauge > 0.0f ? std::clamp(p.gauge, 0.3f, 3.0f)
                                     : std::clamp(roadrail::kStandardGauge * upm, 0.3f, 3.0f);
    // A railway's bed is laid out for its sleepers, which scale with the gauge
    // (the Kind = Railway preset's rule).
    if (o.roadKind == roadrail::kRail && p.gauge <= 0.0f)
        o.roadWidth = std::clamp(p.width * o.roadRailGauge / roadrail::kStandardGauge, 1.0f, 24.0f);
    o.roadDetails = std::clamp(p.details, 0.0f, 1.0f);
    o.roadDetailSeed = p.detailSeed;
    o.roadFurniture = p.furniture;
}

Preset fromRoad(const SceneObject& o, const std::string& name) {
    Preset p;
    p.name = name;
    p.key = keyOf(name);
    p.help = "Saved from a road in this project.";
    p.width = o.roadWidth;
    p.sampleStep = o.roadSampleStep;
    p.grip = o.roadGrip;
    p.rank = o.roadRank;
    p.spill = o.roadSpill;
    p.edgeFade = o.roadEdgeFade;
    p.markings = o.roadMarkings;
    p.kerb = o.roadKerb;
    p.kerbHeight = o.roadKerbHeight;
    p.kerbWidth = o.roadKerbWidth;
    p.pavement = o.roadPavement;
    p.surface = o.roadTexture;
    p.intersection = o.roadIntersectionTexture;
    p.pavementMaterial = o.roadPavementMaterial;
    p.kind = o.roadKind;
    p.tracks = o.roadTracks;
    p.gauge = o.roadRailGauge;
    p.details = o.roadDetails;
    p.detailSeed = o.roadDetailSeed;
    p.furniture = o.roadFurniture;
    return p;
}

bool matches(const Preset& p, const SceneObject& o, float unitsPerMeter) {
    SceneObject t = o;
    apply(p, t, unitsPerMeter);
    return t == o;
}

void applyRailway(SceneObject& o, const std::string& projectDir, float unitsPerMeter,
                  bool full) {
    namespace fs = std::filesystem;
    if (full && o.roadRailGauge == roadrail::kStandardGauge && unitsPerMeter > 0.0f)
        o.roadRailGauge = std::clamp(roadrail::kStandardGauge * unitsPerMeter, 0.3f, 3.0f);
    const float s = o.roadRailGauge / roadrail::kStandardGauge;
    for (const roadtex::Preset& pr : roadtex::presets()) {
        if (std::string(pr.name).rfind("rail-", 0) != 0) continue;
        std::error_code ec;
        const fs::path m = fs::path(projectDir) / roadtex::kDir / (std::string(pr.name) + ".mtl");
        if (!projectDir.empty() && !fs::exists(m, ec)) {
            std::string err;
            roadtex::writeAssets(projectDir, pr.name, pr.params, &err);
        }
    }
    const bool dbl = o.roadTracks >= 2;
    const std::string single = roadtex::kDefaultBallast, twin = roadtex::kDefaultBallastDouble;
    if (full || o.roadTexture == single || o.roadTexture == twin)
        o.roadTexture = dbl ? twin : single;
    o.roadWidth = std::clamp(
        (roadtex::kSleeperLength + 1.0f + (dbl ? roadrail::kTrackSpacingRail : 0.0f)) * s, 1.0f,
        24.0f);
    if (!full) return;
    o.roadIntersectionTexture = roadtex::kDefaultRailJunction;
    o.roadRank = 0;
    o.roadSpill = 0.0f;
    o.roadKerb = false;
    o.roadMarkings = 0;
    o.roadGrip = 0.7f;
}

std::string toJson(const std::vector<Preset>& list) {
    if (list.empty()) return "";
    const Preset d;
    std::string out = "[";
    for (size_t i = 0; i < list.size(); ++i) {
        const Preset& p = list[i];
        std::string body = "\"key\": \"" + json::escape(p.key) + "\", \"name\": \"" +
                           json::escape(p.name) + "\"";
        auto add = [&](const std::string& kv) { body += ", " + kv; };
        auto f = [&](const char* k, float v, float dv) {
            if (v != dv) add("\"" + std::string(k) + "\": " + fmt(v));
        };
        auto n = [&](const char* k, int v, int dv) {
            if (v != dv) add("\"" + std::string(k) + "\": " + std::to_string(v));
        };
        auto s = [&](const char* k, const std::string& v) {
            if (!v.empty()) add("\"" + std::string(k) + "\": \"" + json::escape(v) + "\"");
        };
        s("help", p.help);
        f("width", p.width, d.width);
        f("sampleStep", p.sampleStep, d.sampleStep);
        f("grip", p.grip, d.grip);
        n("rank", p.rank, d.rank);
        f("spill", p.spill, d.spill);
        f("edgeFade", p.edgeFade, d.edgeFade);
        n("markings", p.markings, d.markings);
        if (p.kerb) add("\"kerb\": true");
        f("kerbHeight", p.kerbHeight, d.kerbHeight);
        f("kerbWidth", p.kerbWidth, d.kerbWidth);
        f("pavement", p.pavement, d.pavement);
        s("surface", p.surface);
        s("intersection", p.intersection);
        s("pavementMaterial", p.pavementMaterial);
        n("kind", p.kind, d.kind);
        n("tracks", p.tracks, d.tracks);
        f("gauge", p.gauge, d.gauge);
        f("details", p.details, d.details);
        n("detailSeed", p.detailSeed, d.detailSeed);
        const std::string furn = roadfurn::toJson(p.furniture);
        if (!furn.empty()) add("\"furniture\": " + furn);
        out += (i ? ", {" : "{") + body + "}";
    }
    return out + "]";
}

void fromJson(const json::Value& v, std::vector<Preset>& out) {
    out.clear();
    if (v.type != json::Value::Type::Array) return;
    for (const json::Value& e : v.arr) {
        if (e.type != json::Value::Type::Object) continue;
        Preset p;
        auto f = [&](const char* k, float& dst) {
            if (const auto* x = e.find(k)) dst = (float)x->numberOr(dst);
        };
        auto n = [&](const char* k, int& dst) {
            if (const auto* x = e.find(k)) dst = (int)x->numberOr(dst);
        };
        auto s = [&](const char* k, std::string& dst) {
            if (const auto* x = e.find(k)) dst = x->stringOr("");
        };
        s("key", p.key);
        s("name", p.name);
        s("help", p.help);
        f("width", p.width);
        f("sampleStep", p.sampleStep);
        f("grip", p.grip);
        n("rank", p.rank);
        f("spill", p.spill);
        f("edgeFade", p.edgeFade);
        n("markings", p.markings);
        if (const auto* x = e.find("kerb")) p.kerb = x->boolOr(false);
        f("kerbHeight", p.kerbHeight);
        f("kerbWidth", p.kerbWidth);
        f("pavement", p.pavement);
        s("surface", p.surface);
        s("intersection", p.intersection);
        s("pavementMaterial", p.pavementMaterial);
        n("kind", p.kind);
        n("tracks", p.tracks);
        f("gauge", p.gauge);
        f("details", p.details);
        n("detailSeed", p.detailSeed);
        if (const auto* x = e.find("furniture")) roadfurn::fromJson(*x, p.furniture);
        if (p.name.empty()) p.name = p.key.empty() ? "Preset" : p.key;
        if (p.key.empty()) p.key = keyOf(p.name);
        out.push_back(std::move(p));
    }
}

}  // namespace roadpresets
