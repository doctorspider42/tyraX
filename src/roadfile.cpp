// Road tables on disk (docs/roads.md "Tables on disk"). See roadfile.hpp for
// the contract: the builder the codegen feeds, the directory it emits into
// scene_data.hpp, and --vehicle-check "road tables on disk".
#include "roadfile.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <sstream>
#include <vector>

#include "project.hpp"
#include "templates.hpp"

namespace roadfile {

// The file layout and the checksum: the core the game compiles too.
namespace core {
#include "roadstream_core.inl"
}  // namespace core

uint32_t bits(float v) {
    uint32_t w;
    std::memcpy(&w, &v, 4);
    return w;
}

float fromBits(uint32_t w) {
    float v;
    std::memcpy(&v, &w, 4);
    return v;
}

float asLiteral(const std::string& literal) {
    // strtof rounds a decimal to the nearest float, as the compiler does with
    // a float literal; the trailing F is where it stops.
    return std::strtof(literal.c_str(), nullptr);
}

void Builder::add(int kind, int scene, int row, int first, int count,
                  const std::vector<uint32_t>& words, int stride, int zAt) {
    Item it;
    it.kind = kind;
    it.scene = scene;
    it.row = row;
    it.first = first;
    it.count = count;
    it.at = (uint32_t)(core::RsFile::kHeader + payload_.size());
    it.bytes = (uint32_t)(words.size() * 4);
    std::string bytes(words.size() * 4, '\0');
    for (size_t i = 0; i < words.size(); ++i)
        core::RsFile::putWord(reinterpret_cast<unsigned char*>(&bytes[i * 4]), words[i]);
    it.sum = core::RsFile::sum(reinterpret_cast<const unsigned char*>(bytes.data()), it.bytes,
                               core::RsFile::kSeed);
    // The plan's box (roadStreamAddItem): min/max of x and z over the rows.
    for (int i = 0; i < count; ++i) {
        const float x = fromBits(words[(size_t)i * (size_t)stride]);
        const float z = fromBits(words[(size_t)i * (size_t)stride + (size_t)zAt]);
        if (i == 0) {
            it.x0 = it.x1 = x;
            it.z0 = it.z1 = z;
            continue;
        }
        if (x < it.x0) it.x0 = x;
        if (x > it.x1) it.x1 = x;
        if (z < it.z0) it.z0 = z;
        if (z > it.z1) it.z1 = z;
    }
    payload_ += bytes;
    items_.push_back(it);
}

uint32_t Builder::hash() const {
    return core::RsFile::sum(reinterpret_cast<const unsigned char*>(payload_.data()),
                             (unsigned int)payload_.size(),
                             core::RsFile::kSeed ^ (uint32_t)items_.size());
}

std::string Builder::file() const {
    std::string out(core::RsFile::kHeader, '\0');
    unsigned char* h = reinterpret_cast<unsigned char*>(&out[0]);
    core::RsFile::putWord(h, core::RsFile::kMagic);
    core::RsFile::putWord(h + 4, core::RsFile::kVersion);
    core::RsFile::putWord(h + 8, core::RsFile::kHeader);
    core::RsFile::putWord(h + 12, (uint32_t)items_.size());
    core::RsFile::putWord(h + 16, (uint32_t)payload_.size());
    core::RsFile::putWord(h + 20, hash());
    return out + payload_;
}

namespace {

std::string exact(float v) {
    // 9 significant digits: every float survives the round trip.
    char b[48];
    std::snprintf(b, sizeof(b), "%.9g", (double)v);
    std::string s = b;
    if (s.find_first_of(".eEn") == std::string::npos) s += ".0";
    return s + "F";
}

std::string hex(uint32_t v) {
    char b[16];
    std::snprintf(b, sizeof(b), "0x%08XU", (unsigned)v);
    return b;
}

}  // namespace

std::string Builder::directorySource() const {
    std::ostringstream out;
    size_t perKind[8] = {};
    size_t bytesPerKind[8] = {};
    for (const Item& it : items_) {
        if (it.kind >= 0 && it.kind < 8) {
            ++perKind[it.kind];
            bytesPerKind[it.kind] += it.bytes;
        }
    }
    static const char* kNames[8] = {"", "junction", "spill", "edge", "kerb",
                                    "bridge", "detail", "furniture"};
    out << "// Road tables on disk (docs/roads.md \"Tables on disk\"): this project streams\n"
           "// its roads, so the baked rows (junctions, spills, edges, kerbs, bridges,\n"
           "// details, furniture) are not in the ELF but in bin/roadfile/" << "roads.bin, one item per\n"
           "// streaming item, read when the ring wants it. The directory below is what\n"
           "// the plan needs without reading a byte: kind, scene, row, first vertex,\n"
           "// count, file offset and size, the item's checksum and its XZ box.\n";
    for (int k = 1; k < 8; ++k)
        if (perKind[k])
            out << "//   " << kNames[k] << ": " << perKind[k] << " items, " << bytesPerKind[k]
                << " bytes\n";
    out << "constexpr const char* ROAD_FILE_NAME = \"roadfile/roads.bin\";\n"
        << "constexpr unsigned int ROAD_FILE_HASH = " << hex(hash()) << ";\n"
        << "constexpr unsigned int ROAD_FILE_BYTES = " << payload_.size() << "U;\n"
        << "constexpr int ROAD_FILE_ITEM_COUNT = " << items_.size() << ";\n"
        << "struct RoadFileItem { unsigned short kind; unsigned short scene; int row; int first;"
           " int count; unsigned int at; unsigned int bytes; unsigned int sum; float x0, z0, x1,"
           " z1; };\n";
    if (items_.empty()) {
        out << "constexpr RoadFileItem ROAD_FILE_ITEMS[1] = {};\n";
        return out.str();
    }
    out << "constexpr RoadFileItem ROAD_FILE_ITEMS[" << items_.size() << "] = {\n";
    for (const Item& it : items_)
        out << "    {" << it.kind << ", " << it.scene << ", " << it.row << ", " << it.first << ", "
            << it.count << ", " << it.at << "U, " << it.bytes << "U, " << hex(it.sum) << ", "
            << exact(it.x0) << ", " << exact(it.z0) << ", " << exact(it.x1) << ", "
            << exact(it.z1) << "},\n";
    out << "};\n";
    return out.str();
}

// ---------------------------------------------------------------------------
// --vehicle-check "road tables on disk"
// ---------------------------------------------------------------------------
namespace {

// The numbers of a `<type> NAME[n] = { ... };` table in generated source, in
// order: floats (a trailing F), hex and decimal integers alike, as doubles -
// a float literal parsed with strtof is the float the compiler makes of it.
bool tableTokens(const std::string& src, const std::string& decl, std::vector<std::string>& out) {
    out.clear();
    const size_t at = src.find(decl);
    if (at == std::string::npos) return false;
    const size_t open = src.find("= {", at);
    if (open == std::string::npos) return false;
    const size_t close = src.find("};", open);
    if (close == std::string::npos) return false;
    std::string tok;
    for (size_t i = open + 3; i < close; ++i) {
        const char c = src[i];
        if (c == ',' || c == '{' || c == '}' || c == ' ' || c == '\n' || c == '\r') {
            if (!tok.empty()) out.push_back(tok);
            tok.clear();
            continue;
        }
        tok += c;
    }
    if (!tok.empty()) out.push_back(tok);
    return true;
}

std::vector<float> floatsOf(const std::string& src, const std::string& decl) {
    std::vector<std::string> t;
    std::vector<float> v;
    if (!tableTokens(src, decl, t)) return v;
    for (const std::string& s : t) v.push_back(asLiteral(s));
    return v;
}

std::vector<uint32_t> uintsOf(const std::string& src, const std::string& decl) {
    std::vector<std::string> t;
    std::vector<uint32_t> v;
    if (!tableTokens(src, decl, t)) return v;
    for (const std::string& s : t) v.push_back((uint32_t)std::strtoul(s.c_str(), nullptr, 0));
    return v;
}

// A row table: `width` numbers per row (scene first).
std::vector<std::vector<double>> rowsOf(const std::string& src, const std::string& decl,
                                        int width) {
    std::vector<std::string> t;
    std::vector<std::vector<double>> rows;
    if (!tableTokens(src, decl, t)) return rows;
    for (size_t i = 0; i + (size_t)width <= t.size(); i += (size_t)width) {
        std::vector<double> r;
        for (int k = 0; k < width; ++k) r.push_back(std::strtod(t[i + (size_t)k].c_str(), nullptr));
        rows.push_back(r);
    }
    return rows;
}

const templates::File* fileOf(const std::vector<templates::File>& files, const char* rel) {
    for (const templates::File& f : files)
        if (f.relativePath == rel) return &f;
    return nullptr;
}

}  // namespace

void check(void (*verdict)(bool, const char*)) {
    std::printf("-- road tables on disk --\n");

    // 1. The builder: header, offsets, checksums and boxes round-trip.
    {
        Builder b;
        std::vector<uint32_t> w1, w2;
        for (int i = 0; i < 6; ++i)
            for (float f : {(float)i, 0.5f * (float)i, -2.0f * (float)i, 0.25f, 0.75f})
                w1.push_back(bits(f));
        for (int i = 0; i < 3; ++i)
            for (float f : {10.0f + (float)i, 1.0f, 7.0f - (float)i, 0.9f}) w2.push_back(bits(f));
        b.add(kJunction, 0, 3, 0, 6, w1, 5, 2);
        b.add(kKerb, 1, 0, 0, 3, w2, 4, 2);
        const std::string f = b.file();
        const unsigned char* h = reinterpret_cast<const unsigned char*>(f.data());
        unsigned int items = 0, payload = 0, hash = 0;
        const bool parsed = core::RsFile::header(h, &items, &payload, &hash);
        verdict(parsed && items == 2 && payload == f.size() - core::RsFile::kHeader &&
                    hash == b.hash(),
                "the road file's header names its items, payload and checksum");
        bool round = true;
        const std::vector<uint32_t>* words[2] = {&w1, &w2};
        for (size_t k = 0; k < 2; ++k) {
            const Item& it = b.items()[k];
            round = round && it.at + it.bytes <= f.size() && it.bytes == words[k]->size() * 4;
            for (size_t i = 0; round && i < words[k]->size(); ++i)
                round = core::RsFile::word(h + it.at + i * 4) == (*words[k])[i];
            round = round && core::RsFile::sum(h + it.at, it.bytes, core::RsFile::kSeed) == it.sum;
        }
        verdict(round, "every item's bytes come back from its offset, checksum intact");
        const Item& j = b.items()[0];
        verdict(j.x0 == 0.0f && j.x1 == 5.0f && j.z0 == -10.0f && j.z1 == 0.0f,
                "an item's box is the plan's box over its rows");
        std::string bad = f;
        bad[b.items()[1].at + 5] ^= 0x10;
        const unsigned char* hb = reinterpret_cast<const unsigned char*>(bad.data());
        verdict(core::RsFile::sum(hb + b.items()[1].at, b.items()[1].bytes, core::RsFile::kSeed) !=
                    b.items()[1].sum,
                "one flipped bit fails the item's checksum");
        std::string notOurs = f;
        notOurs[0] = 'X';
        verdict(!core::RsFile::header(reinterpret_cast<const unsigned char*>(notOurs.data()), &items,
                                      &payload, &hash),
                "a file that is not a road table file is refused");
    }

    // 2. The LRU byte cache: the least recently wanted item goes first.
    {
        core::RsCache c;
        c.add(4, 100, 1);
        c.add(9, 200, 2);
        c.add(2, 50, 3);
        c.touch(4, 5);
        const int first = c.oldest();
        c.remove(first);
        const int second = c.oldest();
        verdict(first == 9 && second == 2 && c.total == 150 && c.find(9) < 0,
                "the road byte cache evicts the least recently wanted item");
    }

    // 3. The codegen, end to end: a streamed project with every baked kind,
    //    generated with the tables embedded and on disk. Every directory item,
    //    read from the file at its offset, must be EXACTLY the slice of the
    //    embedded table the embedded build expands for that item - the same
    //    floats, bit for bit - with the same box, and the directory must hold
    //    exactly the items the embedded plan makes.
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "tyrax-roadfile-check";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    Project p;
    p.name = "rfcheck";
    p.dir = dir.string();
    p.scenes.emplace_back();
    SceneData& sc = p.scenes.back();
    sc.name = "main";
    auto road = [&](const std::string& id, std::vector<float> pts, bool kerb) {
        SceneObject o;
        o.type = PrimitiveType::Road;
        o.id = id;
        o.name = id;
        o.roadPoints = std::move(pts);
        o.roadWidth = 8.0f;
        o.roadKerb = kerb;
        o.roadPavement = kerb ? 2.5f : 0.0f;
        o.roadDetails = kerb ? 0.5f : 0.0f;
        o.roadTexture = "res/materials/roads/road-2lane.mtl";
        o.roadIntersectionTexture = "res/materials/roads/road-junction.mtl";
        sc.objects.push_back(o);
        return &sc.objects.back();
    };
    road("a", {-60, 0, 0, 0, 60, 0}, true)->roadFurniture.lamps.spacing = 15.0f;
    road("b", {0, -60, 0, 0, 0, 60}, false)->roadRank = 2;  // the others spill onto it
    SceneObject* bridge = road("c", {-60, 80, 0, 80, 60, 80}, false);
    bridge->roadBridge = true;
    bridge->roadHeights = {0.0f, 6.0f, 0.0f};
    road("d", {-40, -30, 40, -32}, false)->roadEdgeFade = 1.5f;
    p.settings.roadStreamRadius = 150.0f;
    auto gen = [&](bool embed) {
        p.settings.roadStreamEmbedTables = embed;
        return templates::generate(p);
    };
    const std::vector<templates::File> embedded = gen(true);
    const std::vector<templates::File> onDisk = gen(false);
    const templates::File* eData = fileOf(embedded, "inc\\scene_data.hpp");
    const templates::File* dData = fileOf(onDisk, "inc\\scene_data.hpp");
    const templates::File* bin = fileOf(onDisk, ".res-baked\\roadfile\\roads.bin");
    verdict(eData && dData && bin && !fileOf(embedded, ".res-baked\\roadfile\\roads.bin"),
            "a streamed project writes bin/roadfile/roads.bin, an embedded one does not");
    if (!eData || !dData || !bin) {
        std::filesystem::remove_all(dir, ec);
        return;
    }
    const std::string& E = eData->content;
    const std::string& D = dData->content;
    const unsigned char* fb = reinterpret_cast<const unsigned char*>(bin->content.data());
    unsigned int fItems = 0, fPayload = 0, fHash = 0;
    const bool hdr = bin->content.size() >= core::RsFile::kHeader &&
                     core::RsFile::header(fb, &fItems, &fPayload, &fHash);
    std::vector<uint32_t> dHash;
    if (const size_t h = D.find("unsigned int ROAD_FILE_HASH = "); h != std::string::npos)
        dHash.push_back((uint32_t)std::strtoul(D.c_str() + h + 30, nullptr, 0));
    std::vector<std::string> dirTok;
    tableTokens(D, "RoadFileItem ROAD_FILE_ITEMS[", dirTok);
    std::vector<Item> dirItems;
    for (size_t i = 0; i + 12 <= dirTok.size(); i += 12) {
        Item it;
        it.kind = std::atoi(dirTok[i].c_str());
        it.scene = std::atoi(dirTok[i + 1].c_str());
        it.row = std::atoi(dirTok[i + 2].c_str());
        it.first = std::atoi(dirTok[i + 3].c_str());
        it.count = std::atoi(dirTok[i + 4].c_str());
        it.at = (uint32_t)std::strtoul(dirTok[i + 5].c_str(), nullptr, 0);
        it.bytes = (uint32_t)std::strtoul(dirTok[i + 6].c_str(), nullptr, 0);
        it.sum = (uint32_t)std::strtoul(dirTok[i + 7].c_str(), nullptr, 0);
        it.x0 = asLiteral(dirTok[i + 8]);
        it.z0 = asLiteral(dirTok[i + 9]);
        it.x1 = asLiteral(dirTok[i + 10]);
        it.z1 = asLiteral(dirTok[i + 11]);
        dirItems.push_back(it);
    }
    verdict(hdr && dHash.size() == 1 && dHash[0] == fHash && fItems == dirItems.size() &&
                fPayload + core::RsFile::kHeader == bin->content.size(),
            "the file's header matches the ELF's directory (hash, items, size)");

    // The embedded tables and rows, parsed from the embedded scene_data.hpp.
    struct Table {
        int kind;
        const char* rows;
        int rowWidth, firstAt, countAt;
        const char* verts;
        int stride, zAt;
        int minCount;
    };
    // Lit street lamps (docs/weather.md) add a `light` column to the
    // furniture rows; their pool rows are furniture items like any other.
    const int furnWidth =
        E.find("struct RoadFurnRt { int scene; int first; int count; int light; }") != std::string::npos
            ? 4
            : 3;
    // Puddles (docs/weather.md "Puddles") add a `wet` column to the detail
    // rows the same way.
    const int detailWidth =
        E.find("struct RoadDetailRt { int scene; int first; int count; int wet; }") != std::string::npos
            ? 4
            : 3;
    const Table tables[] = {
        {kJunction, "RoadJunctionRt ROAD_JUNCTIONS[", 6, 2, 3, "float ROAD_JUNCTION_VERTS[", 5, 2, 1},
        {kSpill, "RoadSpillRt ROAD_SPILLS[", 7, 2, 3, "float ROAD_SPILL_VERTS[", 5, 1, 1},
        {kEdge, "RoadSpillRt ROAD_EDGES[", 7, 2, 3, "float ROAD_EDGE_VERTS[", 5, 1, 1},
        {kKerb, "RoadKerbRt ROAD_KERBS[", 3, 1, 2, "float ROAD_KERB_VERTS[", 4, 2, 3},
        {kBridge, "RoadBridgeRt ROAD_BRIDGES[", 3, 1, 2, "float ROAD_BRIDGE_VERTS[", 4, 2, 3},
        {kDetail, "RoadDetailRt ROAD_DETAILS[", detailWidth, 1, 2, "float ROAD_DETAIL_VERTS[", 5, 2, 3},
        {kFurn, "RoadFurnRt ROAD_FURN[", furnWidth, 1, 2, "float ROAD_FURN_VERTS[", 3, 2, 3},
    };
    const std::vector<uint32_t> furnRgb = uintsOf(E, "unsigned int ROAD_FURN_RGB[");
    size_t expected = 0, matched = 0, perKindSeen = 0;
    bool exactBytes = true, boxes = true, sums = true, gone = true;
    for (const Table& t : tables) {
        const std::vector<std::vector<double>> rows = rowsOf(E, t.rows, t.rowWidth);
        const std::vector<float> verts = floatsOf(E, t.verts);
        if (D.find(t.verts) != std::string::npos) gone = false;
        bool any = false;
        for (size_t r = 0; r < rows.size(); ++r) {
            const int first = (int)rows[r][(size_t)t.firstAt];
            const int count = (int)rows[r][(size_t)t.countAt];
            if (count < t.minCount) continue;
            const bool pieces = t.kind == kJunction || t.kind == kEdge;
            for (int at = 0; at < count; at += pieces ? kPiece : count) {
                const int n = pieces ? std::min(kPiece, count - at) : count;
                any = true;
                ++expected;
                const Item* it = nullptr;
                for (const Item& d : dirItems)
                    if (d.kind == t.kind && d.row == (int)r && d.first == at) it = &d;
                if (!it) continue;
                ++matched;
                const size_t need = (size_t)(first + at + n) * (size_t)t.stride;
                if (verts.size() < need ||
                    (t.kind == kFurn && furnRgb.size() < (size_t)(first + n))) {
                    exactBytes = false;  // the embedded table is shorter than its rows
                    continue;
                }
                // The slice the embedded build expands for this item.
                std::vector<uint32_t> want;
                for (int v = 0; v < n * t.stride; ++v)
                    want.push_back(bits(verts[(size_t)(first + at) * (size_t)t.stride + (size_t)v]));
                if (t.kind == kFurn)
                    for (int v = 0; v < n; ++v) want.push_back(furnRgb[(size_t)(first + v)]);
                bool same = it->count == n && it->bytes == want.size() * 4 &&
                            it->at + it->bytes <= bin->content.size();
                for (size_t w = 0; same && w < want.size(); ++w)
                    same = core::RsFile::word(fb + it->at + w * 4) == want[w];
                exactBytes = exactBytes && same;
                if (same)
                    sums = sums && core::RsFile::sum(fb + it->at, it->bytes, core::RsFile::kSeed) ==
                                       it->sum;
                float x0 = 0, z0 = 0, x1 = 0, z1 = 0;
                for (int v = 0; v < n; ++v) {
                    const float x = verts[(size_t)(first + at + v) * (size_t)t.stride];
                    const float z = verts[(size_t)(first + at + v) * (size_t)t.stride + (size_t)t.zAt];
                    if (v == 0 || x < x0) x0 = x;
                    if (v == 0 || x > x1) x1 = x;
                    if (v == 0 || z < z0) z0 = z;
                    if (v == 0 || z > z1) z1 = z;
                }
                boxes = boxes && it->x0 == x0 && it->x1 == x1 && it->z0 == z0 && it->z1 == z1;
            }
        }
        if (any) ++perKindSeen;
    }
    verdict(perKindSeen == 7, "the fixture bakes every kind (junction, spill, edge, kerb, bridge, detail, furniture)");
    verdict(expected > 0 && matched == expected && dirItems.size() == expected,
            "the directory holds exactly the items the embedded plan makes");
    verdict(exactBytes, "item k's bytes are exactly the embedded table's floats for item k");
    verdict(sums, "every item's checksum matches its bytes");
    verdict(boxes, "every directory box is the box the embedded plan computes");
    verdict(gone, "no per-vertex road table is left in the ELF's sources");
    std::printf("  %zu items (%d kinds), %zu bytes on disk\n", dirItems.size(), (int)perKindSeen,
                bin->content.size());

    // 4. A stale file: the same project with one road moved writes a file whose
    //    header no longer matches this ELF's ROAD_FILE_HASH.
    {
        sc.objects[1].roadPoints[0] += 1.0f;
        const std::vector<templates::File> moved = gen(false);
        sc.objects[1].roadPoints[0] -= 1.0f;
        const templates::File* mbin = fileOf(moved, ".res-baked\\roadfile\\roads.bin");
        unsigned int mi = 0, mp = 0, mh = 0;
        verdict(mbin && core::RsFile::header(reinterpret_cast<const unsigned char*>(mbin->content.data()),
                                             &mi, &mp, &mh) &&
                    dHash.size() == 1 && mh != dHash[0],
                "the header hash detects a road file from another build (stale)");
    }

    // 5. The runtime pieces: a file-backed project gets the reader, the
    //    directory plan and the on-screen error; every anchor still matches;
    //    an embedded one gets none of it; streaming off writes no file at all.
    {
        std::string all, allEmbedded;
        for (const templates::File& f : onDisk)
            if (f.relativePath != ".res-baked\\roadfile\\roads.bin") all += f.content;
        for (const templates::File& f : embedded) allEmbedded += f.content;
        verdict(all.find("void TerrainGame::roadStreamIoMain") != std::string::npos &&
                    all.find("ROAD_FILE_ITEMS[fi]") != std::string::npos &&
                    all.find("roadStreamDrawError();") != std::string::npos &&
                    all.find("#include <fcntl.h>") != std::string::npos &&
                    all.find("road streaming: template anchor missing") == std::string::npos,
                "a file-backed project generates the reader, the directory plan and the error HUD");
        verdict(allEmbedded.find("roadStreamIoMain") == std::string::npos &&
                    allEmbedded.find("ROAD_FILE_ITEMS") == std::string::npos,
                "an embedded project generates no file code");
        verdict(all.find("void TerrainGame::buildRoads(int scene) {") == std::string::npos,
                "a file-backed project compiles no unused buildRoads");
        p.settings.roadStreamRadius = 0.0f;
        const std::vector<templates::File> off = gen(false);
        std::string offAll;
        for (const templates::File& f : off) offAll += f.content;
        verdict(!fileOf(off, ".res-baked\\roadfile\\roads.bin") && offAll.find("ROAD_FILE_") == std::string::npos,
                "streaming off writes no road file and no directory");
    }
    std::filesystem::remove_all(dir, ec);
}

}  // namespace roadfile
