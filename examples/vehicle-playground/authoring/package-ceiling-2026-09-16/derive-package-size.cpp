// Scratch harness: reproduces StaPipQBufferRenderer::setDoubleBuffer() and
// StaPipVU1Program::getMaxVertCount() verbatim, and sweeps the two knobs that
// could raise the package size: VU1_STAPIP_DBUFFER_END and the /3/3 rounding.
#include <cstdio>
#include <cstdint>
#include <initializer_list>
typedef uint16_t u16; typedef uint32_t u32; typedef uint8_t u8;

// vendor/tyra/engine/inc/.../stapip_vu1_shared_defines.h
static const u16 VU1_STAPIP_LAST_ITEM_ADDR = 21;
static const u16 VU1_STAPIP_DBUFFER_END    = 944;

// StaPipQBufferRenderer::setDoubleBuffer(), verbatim
static u16 bufferSizeFor(u16 dbufferEnd) {
  u16 startingAddr = VU1_STAPIP_LAST_ITEM_ADDR + 1;
  const u16 bufferMaxSize = dbufferEnd;
  u16 bufferSize = (u16)((bufferMaxSize - startingAddr) / 2);
  bufferSize -= 1;
  return bufferSize;
}

// StaPipVU1Program::getMaxVertCount(), verbatim (round9 = the /3/3*3*3 step)
static u16 getMaxVertCount(bool singleColorEnabled, u16 bufferSize,
                           u8 elementsPerVertex, u8 reglistCount,
                           bool round9) {
  u16 res = bufferSize - 9;
  u8 colorElementsPerVertex =
      singleColorEnabled ? (u8)(elementsPerVertex - 1) : elementsPerVertex;
  res /= (colorElementsPerVertex + reglistCount);
  if (round9) { res = res / 3 / 3; res = (u16)(res * 3 * 3); }
  else        { res = res / 3;     res = (u16)(res * 3); }
  return res;
}

struct Cls { const char* name; u8 elem; u8 reglist; };
// (elementsPerVertex, reglistCount) read off each program's ctor.
static const Cls kCull[] = {
  {"cull_c   (colour)",              2, 2},
  {"cull_d   (dir lights)",          3, 2},
  {"cull_tc  (tex + colour)",        3, 3},
  {"cull_tce (tex + env/matcap)",    3, 3},
  {"cull_td  (tex + dir lights)",    4, 3},
};

int main() {
  const u16 bs = bufferSizeFor(VU1_STAPIP_DBUFFER_END);
  printf("shipping: DBUFFER_END=%u -> bufferSize=%u, usable res=%u\n\n",
         VU1_STAPIP_DBUFFER_END, bs, (unsigned)(bs - 9));

  printf("%-34s %8s %8s %8s %8s\n", "program class", "divisor", "raw", "/9", "/3");
  for (const auto& c : kCull)
    for (int single = 0; single < 2; ++single) {
      u8 ce = single ? (u8)(c.elem - 1) : c.elem;
      char nm[96]; snprintf(nm, sizeof nm, "%s %s", c.name, single ? "[1 colour]" : "[N colours]");
      printf("%-34s %8u %8u %8u %8u\n", nm, (unsigned)(ce + c.reglist),
             (unsigned)((bs - 9) / (ce + c.reglist)),
             (unsigned)getMaxVertCount(single, bs, c.elem, c.reglist, true),
             (unsigned)getMaxVertCount(single, bs, c.elem, c.reglist, false));
    }

  // TerrainGame::minPackageSize(): 8 combos, (lit && !single) refused by the engine
  // (StaPipCore::render asserts !(color->many && lighting)).
  printf("\nminPackageSize() over the REACHABLE combos:\n");
  u32 mn = 0;
  for (int single = 0; single < 2; ++single)
   for (int lit = 0; lit < 2; ++lit)
    for (int tex = 0; tex < 2; ++tex) {
      if (lit && !single) continue;
      const Cls& c = lit ? (tex ? kCull[4] : kCull[1]) : (tex ? kCull[2] : kCull[0]);
      u32 v = getMaxVertCount(single, bs, c.elem, c.reglist, true);
      printf("  single=%d lit=%d tex=%d  %-30s -> %u\n", single, lit, tex, c.name, v);
      if (v && (!mn || v < mn)) mn = v;
    }
  printf("  => minPackageSize = %u  (meshstrip::kRun must equal this)\n", mn);

  // How far can DBUFFER_END be pushed, and what does it buy the binding class?
  printf("\nsweep of VU1_STAPIP_DBUFFER_END for cull_tc [N colours] (divisor 6):\n");
  printf("%10s %12s %8s %8s   %s\n", "END", "bufferSize", "/9", "/3", "note");
  const u16 ends[] = {906, 944, 956, 1004, 1014, 1016, 1024};
  for (u16 e : ends) {
    u16 b = bufferSizeFor(e);
    printf("%10u %12u %8u %8u   %s\n", e, b,
           (unsigned)getMaxVertCount(false, b, 3, 3, true),
           (unsigned)getMaxVertCount(false, b, 3, 3, false),
           e == 944 ? "SHIPPING (clip scratch 944..1023 = 80 qw)"
           : e == 1024 ? "entire VU1 data memory, clip scratch DELETED"
           : e == 906 ? "smallest END that still yields 72"
           : e == 1014 ? "smallest END that yields 81 (scratch would be 10 qw)"
           : "");
  }

  // What a doubled package would need.
  printf("\nwhat N vertices per package would cost, cull_tc (6 qw/vertex):\n");
  for (u32 n : {72u, 81u, 90u, 108u, 144u}) {
    u32 half = 9 + n * 6;                 // tags + vertices, one double-buffer half
    u32 total = 22 + 2 * (half + 1);      // constants + both halves (+1 for the -=1)
    printf("  %3u verts -> half %4u qw, double buffer %4u qw, + 22 consts = %4u of 1024  %s\n",
           n, half, 2 * (half + 1), total, total <= 1024 ? "fits" : "DOES NOT FIT");
  }
  return 0;
}
