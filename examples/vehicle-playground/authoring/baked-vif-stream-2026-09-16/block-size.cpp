// Baked VIF stream: what one package's block costs, per program class.
//
//   g++ -O2 -std=c++17 -o block-size block-size.cpp && ./block-size
//
// Nothing is read from the tree - every constant is copied in and named next
// to the file it came from - so this runs anywhere and stays runnable when the
// spike is not compiled in. Re-run it after any edit to
// StaPipVU1Program::getMaxVertCount, to a cull program's
// addProgramQBufferDataToPacket (the stream count), or to the VU1 double
// buffer size, and update docs/baked-vif-stream.md's table with what it says.
//
// The block's layout, quadword by quadword (docs/baked-vif-stream.md):
//
//   1 qw   NOP NOP STCYCL UNPACK(num=2, dest=0, usetop)
//   2 qw   the scale/count quadword and the prim GIFtag
//   per uploaded stream:
//   1 qw   NOP NOP STCYCL UNPACK(num=n, dest=VERT_DATA+k*n, usetop)
//   n qw   that stream's vertices, INLINE - this is the whole cost
//   1 qw   NOP NOP FLUSH MSCAL<program> (package 0) or MSCNT (the rest)
//
// so blockQw = 3 + streams * (1 + n) + 1.

#include <cstdio>

namespace {

// vendor/tyra/.../stapip_qbuffer_renderer.cpp, setDoubleBuffer(): VU1 data
// memory 22..944 split in two, 460 quadwords a half, minus one.
constexpr int kBufferSize = 460 - 1;
// vendor/tyra/.../stapip_vu1_program.cpp, getMaxVertCount(): nine quadwords
// for the GIF tag block (StoreTyraGifTags*Alpha), then divide by what the EE
// uploads plus what the program writes, then round down to a multiple of 3.
constexpr int kTagBlockQw = 9;

struct Klass {
    const char* name;
    // The constructor's (elementsPerVertex, reglistCount) pair -
    // vendor/tyra/.../programs/cull/stapip_cull_*_vu1_program.cpp.
    int elementsPerVertex;
    int reglistCount;
    // How many streams addProgramQBufferDataToPacket actually unpacks. It is
    // NOT elementsPerVertex: cull_td budgets 4 and uploads 4 (verts, sts,
    // normals, colours), cull_tc budgets 3 and uploads 3, and a single-colour
    // bag drops the colour stream in every class.
    int streamsMulti;
    int streamsSingle;
};

const Klass kClasses[] = {
    {"cull_c   (colour)", 2, 2, 2, 1},
    {"cull_tc  (texture + colour)", 3, 3, 3, 2},
    {"cull_tce (matcap env)", 3, 3, 3, 2},
    {"cull_td  (texture + dir lights)", 4, 3, 4, 3},
    {"cull_d   (dir lights)", 3, 2, 3, 2},
};

int maxVertCount(const Klass& k, bool singleColor) {
    const int elems = singleColor ? k.elementsPerVertex - 1 : k.elementsPerVertex;
    int res = kBufferSize - kTagBlockQw;
    res /= (elems + k.reglistCount);
    return (res / 3) * 3;
}

int blockQw(int streams, int verts) { return 3 + streams * (1 + verts) + 1; }

void row(const Klass& k, bool single, int verts, const char* note) {
    const int streams = single ? k.streamsSingle : k.streamsMulti;
    const int qw = blockQw(streams, verts);
    std::printf("| %-33s | %-6s | %d | %3d | %5d | %6d | %5.1f |\n", k.name,
                single ? "single" : "many", streams, verts, qw, qw * 16,
                (double)(qw * 16) / verts);
    (void)note;
}

}  // namespace

int main() {
    std::printf("Block size per package. `n` is the package's vertex count.\n");
    std::printf("blockQw = 3 + streams * (1 + n) + 1\n\n");
    std::printf("| class | colour | streams |   n | qw | bytes | B/vert |\n");
    std::printf("| --- | --- | ---: | ---: | ---: | ---: | ---: |\n");
    for (const Klass& k : kClasses) {
        row(k, false, maxVertCount(k, false), "derived");
        row(k, true, maxVertCount(k, true), "derived");
    }

    std::printf("\nAt the package size the Motor District PINS (75 - the\n");
    std::printf("minimum over an object's passes, meshstrip::kRun):\n\n");
    std::printf("| class | colour | streams |   n | qw | bytes | B/vert |\n");
    std::printf("| --- | --- | ---: | ---: | ---: | ---: | ---: |\n");
    for (const Klass& k : kClasses) {
        row(k, false, 75, "pinned");
        row(k, true, 75, "pinned");
    }

    std::printf("\nAgainst what the same 75 vertices already cost as source\n");
    std::printf("arrays (16 bytes a quadword per uploaded stream), which\n");
    std::printf("NOTHING can free - the bbox cacher, the clip route and the\n");
    std::printf("generated game all still read them:\n\n");
    std::printf("| streams | arrays B/vert | baked B/vert | total B/vert |\n");
    std::printf("| ---: | ---: | ---: | ---: |\n");
    for (int streams = 2; streams <= 4; ++streams) {
        const double arrays = streams * 16.0;
        const double baked = (double)(blockQw(streams, 75) * 16) / 75.0;
        std::printf("| %d | %.1f | %.1f | %.1f |\n", streams, arrays, baked,
                    arrays + baked);
    }
    return 0;
}
