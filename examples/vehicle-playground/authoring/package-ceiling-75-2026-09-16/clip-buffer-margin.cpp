// Does a CLIP package still fit its VU1 double-buffer half at the new package
// ceiling?  (docs/render-submission-attribution.md, "Round four")
//
// Round three raised the question and answered it for 81 with a hand
// calculation.  This round actually moves the ceiling (72 -> 75 via the
// rounding step), and the ceiling feeds clipPackageSize(), so the clip
// programs' memory footprint moves with it.  A clip fan-out that overruns its
// buffer half is a corruption bug that PCSX2 cannot see, so it gets a harness
// rather than a paragraph - and the harness is what found that relaxing the
// rounding ALONE leaves the untextured single-colour class one quadword of
// margin, which is why clipDivisor moved from 5 to 6 in the same commit.
//
// Everything here is transcribed from the sources named in each comment.  It
// needs nothing from the tree, so it is also the check to re-run after any
// edit to getMaxVertCount, clipPackageSize/clipDivisor, or a clip .vclpp's
// buffer layout.
//
//   g++ -O2 -std=c++17 -o clip-buffer-margin clip-buffer-margin.cpp
//   ./clip-buffer-margin

#include <cstdio>
#include <initializer_list>

// ---------------------------------------------------------------------------
// StaPipQBufferRenderer::setDoubleBuffer(), verbatim.
// ---------------------------------------------------------------------------
static const unsigned kLastItemAddr = 21;  // VU1_STAPIP_LAST_ITEM_ADDR
static const unsigned kDBufferEnd = 944;   // VU1_STAPIP_DBUFFER_END

static unsigned bufferSize() {
  const unsigned startingAddr = kLastItemAddr + 1;
  unsigned size = (kDBufferEnd - startingAddr) / 2;
  return size - 1;  // "we don't want to upload into the 2nd buffer's 1st addr"
}

// ---------------------------------------------------------------------------
// StaPipVU1Program::getMaxVertCount(), with the rounding step as a parameter.
// roundTo 9 is what shipped before this round, 3 is what ships now.
// ---------------------------------------------------------------------------
static unsigned maxVertCount(unsigned elementsPerVertex, unsigned reglistCount,
                             bool singleColor, unsigned bufSize,
                             unsigned roundTo) {
  unsigned res = bufSize - 9;  // the GIF tag block reserve
  const unsigned colorElements =
      singleColor ? elementsPerVertex - 1 : elementsPerVertex;
  res /= (colorElements + reglistCount);
  return (res / roundTo) * roundTo;
}

// StaPipCore::clipPackageSize() / clipDivisor().  6 with VU1 clipping on.
static unsigned clipPackageSize(unsigned maxVert, unsigned divisor) {
  const unsigned size = maxVert / divisor;
  return (size / 3) * 3;
}

// ---------------------------------------------------------------------------
// One clip program's data-memory footprint for a package of N vertices.
//
// Layout, read off stapip_clip_tc_vu1.vclpp and confirmed against the other
// four (the .vclpp macro bodies are where uploadedX / outQw / tagBlock come
// from - they are NOT the constructor's elementsPerVertex / reglistCount):
//
//   buffer+0        scale / packed vertex count
//   buffer+1        prim giftag
//   buffer+2        vertexData, N quadwords
//   +N              the next uploaded stream ... one per `uploaded`
//   destAddress  =  buffer + 2 + uploaded*N
//                   tagBlock quadwords of GIF tags, then the emitted
//                   vertices, outQw quadwords each.
// ---------------------------------------------------------------------------
struct ClipClass {
  const char* name;
  unsigned uploadedMulti;   // streams the EE uploads, per-vertex-colour bag
  unsigned uploadedSingle;  // ... single-colour bag
  unsigned outQw;           // quadwords the program STORES per emitted vertex
  unsigned tagBlock;        // quadwords of GIF tag block it writes first
  // getMaxVertCount's own sizing pair, which is NOT the same thing: it budgets
  // elementsPerVertex + reglistCount, and the d/td classes spend part of that
  // budget on an uploaded normal stream rather than on output registers.
  unsigned elementsPerVertex, reglistCount;
  bool multiReachable, singleReachable;  // StaPipCore::render refuses the rest
};

// Worst-case quadwords a clip package of `n` input vertices occupies in its
// double-buffer half.  7 output triangles per input triangle is exact:
// Sutherland-Hodgman on a convex polygon gains at most one vertex per plane,
// and the plane loop runs exactly 6 times (planePtr stops at PLANES_ADDR + 12),
// so a triangle reaches at most 9 vertices and fans to at most 7 triangles.
static unsigned footprint(const ClipClass& c, bool single, unsigned n) {
  const unsigned uploaded = single ? c.uploadedSingle : c.uploadedMulti;
  const unsigned outVerts = (n / 3) * 7 * 3;
  return 2 + uploaded * n + c.tagBlock + outVerts * c.outQw;
}

int main() {
  const ClipClass classes[] = {
      // name                        upN upS out tag epv rc  N?     1?
      {"clip_c   (colour)", 2, 1, 2, 7, 2, 2, true, true},
      {"clip_d   (dir lights)", 0, 2, 2, 7, 2, 3, false, true},
      {"clip_tc  (tex + colour)", 3, 2, 3, 9, 3, 3, true, true},
      {"clip_tce (tex + env)", 3, 2, 3, 9, 3, 3, true, true},
      {"clip_td  (tex + dir lights)", 0, 3, 3, 9, 3, 4, false, true},
  };
  const unsigned buf = bufferSize();
  printf("VU1 double-buffer half: %u quadwords (DBUFFER_END=%u)\n", buf,
         kDBufferEnd);
  printf(
      "Only combinations StaPipCore::render can reach are listed: it asserts\n"
      "that a bag never carries both per-vertex colours and lighting, so the\n"
      "d/td classes are always single-colour.\n\n");

  struct Arm {
    const char* label;
    unsigned roundTo, divisor;
  };
  const Arm arms[] = {
      {"BEFORE this round: rounding /9, clipDivisor 5", 9, 5},
      {"naive relaxation:  rounding /3, clipDivisor 5", 3, 5},
      {"SHIPPING:          rounding /3, clipDivisor 6", 3, 6},
  };

  for (const Arm& a : arms) {
    printf("=== %s ===\n", a.label);
    printf("%-30s %-8s %8s %8s %8s %8s\n", "class", "colours", "maxVert",
           "clipPkg", "used", "margin");
    unsigned worst = ~0u;
    for (const ClipClass& c : classes) {
      for (int si = 0; si < 2; ++si) {
        const bool single = si == 1;
        if (single ? !c.singleReachable : !c.multiReachable) continue;
        const unsigned mv = maxVertCount(c.elementsPerVertex, c.reglistCount,
                                         single, buf, a.roundTo);
        const unsigned n = clipPackageSize(mv, a.divisor);
        const unsigned u = footprint(c, single, n);
        const int margin = (int)buf - (int)u;
        if (margin >= 0 && (unsigned)margin < worst) worst = (unsigned)margin;
        printf("%-30s %-8s %8u %8u %8u %+8d%s\n", c.name, single ? "1" : "N",
               mv, n, u, margin, u > buf ? "   *** OVERRUN ***" : "");
      }
    }
    printf("  tightest margin over every reachable class: %u quadwords\n\n",
           worst);
  }

  // The number the strip bake is pinned to: the smallest package size over
  // every combination StaPipCore::render can actually reach.  This is
  // TerrainGame::minPackageSize(), and meshstrip::kRun must equal it.
  struct Combo {
    unsigned epv, rc;
    bool single;
  };
  const Combo reachable[] = {{2, 2, false}, {2, 2, true}, {3, 3, false},
                             {3, 3, true},  {2, 3, true}, {3, 4, true}};
  for (unsigned roundTo : {9u, 3u}) {
    unsigned mn = ~0u;
    for (const Combo& k : reachable) {
      const unsigned v = maxVertCount(k.epv, k.rc, k.single, buf, roundTo);
      if (v < mn) mn = v;
    }
    printf("minPackageSize() with /%u rounding = %u  (meshstrip::kRun)\n",
           roundTo, mn);
  }
  return 0;
}
