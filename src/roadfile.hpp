#pragma once

#include <cstdint>
#include <string>
#include <vector>

// Road tables on disk (docs/roads.md "Tables on disk", format v104). A project
// that streams its roads (roadStreamRadius > 0) no longer bakes the big
// per-vertex road tables into the ELF - ROAD_JUNCTION_VERTS (node patches,
// paint, pavements, bridge decks), ROAD_SPILL_VERTS, ROAD_EDGE_VERTS,
// ROAD_KERB_VERTS (kerbs and rails), ROAD_BRIDGE_VERTS, ROAD_DETAIL_VERTS and
// ROAD_FURN_VERTS/ROAD_FURN_RGB. The codegen writes them to bin/roadfile/roads.bin, one
// ITEM per streaming item, and the ELF keeps a directory: per item its kind,
// scene, row, first vertex, count, file offset and size, checksum and XZ box -
// everything the streaming plan needs without reading a byte. The game reads
// an item's bytes when the ring wants it (a reader thread, ahead of the build
// radius), expands them exactly as the embedded rows were expanded, and frees
// them.
//
// Host-only (the codegen and --vehicle-check). The file layout and the
// checksum live in src/roadstream_core.inl (RsFile), which the game compiles
// too.
namespace roadfile {

// The items' kinds - the generated runtime's RS_* values.
enum Kind { kJunction = 1, kSpill = 2, kEdge = 3, kKerb = 4, kBridge = 5, kDetail = 6, kFurn = 7 };

// A junction row and a soft-edge row are cut into pieces of this many
// vertices (buildRoads' upload chunk); every other row is one item.
inline constexpr int kPiece = 1800;

struct Item {
    int kind = 0, scene = 0, row = 0, first = 0, count = 0;
    uint32_t at = 0, bytes = 0, sum = 0;
    float x0 = 0, z0 = 0, x1 = 0, z1 = 0;
};

// The value a C++ float literal (as the codegen prints it, "1.25F") becomes
// when the compiler parses it: the file must hold EXACTLY the floats the
// embedded table would have held, and most road tables are printed with 6-7
// significant digits, which is not a float's round trip.
float asLiteral(const std::string& literal);

class Builder {
public:
    // One item: `words` are its rows exactly as they go to disk (floats
    // bit-cast, a furniture row's colours after its vertices). The XZ box is
    // taken over the first `count` vertices of `stride` words, z at word
    // `zAt` - the box the embedded plan computes from the same rows.
    void add(int kind, int scene, int row, int first, int count,
             const std::vector<uint32_t>& words, int stride, int zAt);
    const std::vector<Item>& items() const { return items_; }
    // The whole file: header + payload.
    std::string file() const;
    // The payload's checksum (= the header's, = ROAD_FILE_HASH).
    uint32_t hash() const;
    // The scene_data.hpp block: ROAD_FILE_NAME, _HASH, _BYTES, the directory.
    std::string directorySource() const;

private:
    std::vector<Item> items_;
    std::string payload_;
};

// Float -> its 32-bit pattern, and back.
uint32_t bits(float v);
float fromBits(uint32_t w);

// --vehicle-check "road tables on disk".
void check(void (*verdict)(bool, const char*));

}  // namespace roadfile
