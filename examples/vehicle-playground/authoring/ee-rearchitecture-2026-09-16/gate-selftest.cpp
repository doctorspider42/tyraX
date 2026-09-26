// Host self-test for the acceptance gate's leg 1 - docs/baked-stream-acceptance-gate.md.
//
// The gate's whole claim is one sentence: TWO CHAINS THAT HAND VIF1 THE SAME
// WORDS HASH THE SAME, HOWEVER THEY WERE BUILT. That is what makes it an output
// check rather than a structure check, and it is what the old counter gate
// could not do - a run of packages under one DMA REF tag cannot cross a packet
// flush boundary, so pinning `packetFlushes` pins the prize.
//
// A claim like that should be demonstrated, not argued, and it does not need a
// PS2 to demonstrate: it is a property of the decoder. This builds two chains
// over the SAME synthetic geometry - one in the shape the control writes
// (a CNT with the scale quadword and the GIFtag, one REF per vertex stream,
// a CNT carrying the program kick: seven chain quadwords per package) and one
// in the shape the baked stream writes (ONE REF over a transcoded, NOP-padded,
// tag-free block covering a whole run of packages) - folds both, and checks the
// hashes against each other and against six deliberate defects.
//
// It runs the ENGINE'S OWN decoder by including the real translation unit, so
// it cannot drift away from what the console folds. Nothing is read from the
// tree, so it keeps working with the gate compiled out.
//
//   g++ -O2 -std=c++17 -I stub -I ../../../../vendor/tyra/engine/inc \
//       -DTYRA_STAPIP_VIFHASH=1 -o gate-selftest gate-selftest.cpp && ./gate-selftest

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cstdint>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#endif

#include "renderer/3d/pipeline/static/core/stapip_vif_hash.hpp"
// The decoder itself. Including the .cpp is deliberate: this test must exercise
// the shipped code, not a copy of it.
#include "../../../../vendor/tyra/engine/src/renderer/3d/pipeline/static/core/stapip_vif_hash.cpp"

using Tyra::StaPipVifHash;

// ---------------------------------------------------------------------------
// A DMA tag's address field is 32 bits, because a PS2 address is. On a 64-bit
// host an ordinary allocation does not fit in one, and the decoder - which is
// the SHIPPED decoder, casting the field to a pointer exactly as the DMAC
// resolves it - would follow a truncated address into nothing. So everything a
// REF tag can name is placed in a low arena reserved below 4 GB. That is not a
// workaround for the test; it is the test being faithful about what a REF can
// reach.
// ---------------------------------------------------------------------------
static u8* lowArena = nullptr;
static size_t lowUsed = 0, lowSize = 0;

static void lowInit(size_t bytes) {
  lowSize = bytes;
#ifdef _WIN32
  for (uintptr_t base = 0x20000000u; base < 0x70000000u; base += 0x10000000u) {
    lowArena = static_cast<u8*>(VirtualAlloc(reinterpret_cast<LPVOID>(base),
                                             bytes, MEM_COMMIT | MEM_RESERVE,
                                             PAGE_READWRITE));
    if (lowArena != nullptr) break;
  }
#else
  void* p = mmap(reinterpret_cast<void*>(0x20000000u), bytes,
                 PROT_READ | PROT_WRITE,
                 MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
  if (p == MAP_FAILED) p = mmap(nullptr, bytes, PROT_READ | PROT_WRITE,
                                MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  lowArena = (p == MAP_FAILED) ? nullptr : static_cast<u8*>(p);
#endif
  if (lowArena == nullptr ||
      reinterpret_cast<uintptr_t>(lowArena) + bytes > 0xFFFFFFFFull) {
    std::fprintf(stderr,
                 "could not reserve a sub-4GB arena; a DMA REF address is 32 "
                 "bits and this test cannot be honest without one\n");
    std::exit(2);
  }
}

/** Copy `n` quadwords into the low arena, 16-byte aligned, and return where. */
static qword_t* lowPut(const qword_t* src, size_t n) {
  lowUsed = (lowUsed + 15u) & ~static_cast<size_t>(15u);
  if (lowUsed + n * 16 > lowSize) {
    std::fprintf(stderr, "arena full\n");
    std::exit(2);
  }
  qword_t* dst = reinterpret_cast<qword_t*>(lowArena + lowUsed);
  std::memcpy(dst, src, n * 16);
  lowUsed += n * 16;
  return dst;
}

// ---------------------------------------------------------------------------
// The scene. Small enough to reason about by hand, shaped like the real thing:
// a bag of N packages, each 8 vertices in three streams (position, ST, colour),
// which is `cull_tc` with the numbers shrunk.
// ---------------------------------------------------------------------------
static const unsigned kPackages = 5;
static const unsigned kVerts = 8;
static const unsigned kStreams = 3;
// VU1 destination addresses, as stapip_vu1_shared_defines.h lays them out:
// mesh constants at 0, then the vertex streams.
static const unsigned kDestConst = 0;
static const unsigned kDestVert = 2;
static const unsigned kProgramAddr = 0x1a4;

struct Geometry {
  // [stream][package][vertex] -> one quadword. In the LOW arena, because the
  // control chain names these arrays with REF tags - exactly as the engine's
  // per-stream REFs name the bag's own arrays in place.
  qword_t* stream[kStreams];
};

static u32 mix(u32 a, u32 b, u32 c) { return a * 2654435761u + b * 40503u + c; }

static Geometry makeGeometry() {
  Geometry g;
  std::vector<qword_t> tmp(kPackages * kVerts);
  for (unsigned s = 0; s < kStreams; ++s) {
    for (unsigned i = 0; i < kPackages * kVerts; ++i)
      for (unsigned w = 0; w < 4; ++w) tmp[i].sw[w] = mix(s, i, w);
    g.stream[s] = lowPut(tmp.data(), tmp.size());
  }
  return g;
}

// --- VIFcode and DMA tag construction --------------------------------------

static u32 vifNop() { return 0u; }
static u32 vifStcycl(u32 cl, u32 wl) {
  return (0x01u << 24) | (wl << 8) | cl;
}
// UNPACK V4_32, `num` elements, to `dest`, usetop.
static u32 vifUnpackV4_32(u32 num, u32 dest) {
  return (0x6Cu << 24) | ((num & 0xFFu) << 16) | (dest & 0x3FFu) | 0x4000u;
}
static u32 vifFlush() { return 0x11u << 24; }
static u32 vifMscal(u32 addr) { return (0x14u << 24) | (addr & 0xFFFFu); }
static u32 vifMscnt() { return 0x17u << 24; }

// A chain quadword: a DMA tag in the low 8 bytes, two VIFcodes in the high 8.
static qword_t tagQw(u32 id, u32 qwc, const void* addr, u32 vif0, u32 vif1) {
  qword_t q;
  q.sw[0] = (qwc & 0xFFFFu) | (id << 28);
  q.sw[1] = static_cast<u32>(reinterpret_cast<uintptr_t>(addr));
  q.sw[2] = vif0;
  q.sw[3] = vif1;
  return q;
}
static const u32 kCnt = 1, kRef = 3, kEnd = 7;

// ---------------------------------------------------------------------------
// Arm A: the chain the CONTROL writes. Seven quadwords per package -
// docs/baked-vif-stream.md, "Chain quadwords".
// ---------------------------------------------------------------------------
static std::vector<qword_t> buildControlChain(const Geometry& g,
                                              unsigned fromPkg, unsigned toPkg,
                                              bool* kickState = nullptr) {
  std::vector<qword_t> c;
  // StaPipCore::render clears lastProgramName ONCE PER BAG, not per packet, so
  // a bag split across two packets emits MSCNT at the head of the second - it
  // does NOT re-kick. Threading the state here is what makes this test model
  // the engine rather than a plausible engine.
  bool localKick = false;
  bool& kicked = kickState != nullptr ? *kickState : localKick;
  for (unsigned p = fromPkg; p < toPkg; ++p) {
    // CNT + 2 inline quadwords: the scale/count quadword and the prim GIFtag.
    c.push_back(tagQw(kCnt, 2, nullptr, vifStcycl(1, 1),
                      vifUnpackV4_32(2, kDestConst)));
    qword_t scale;
    scale.sw[0] = 0x45000000u;  // 2048.0f
    scale.sw[1] = 0x45000000u;
    scale.sw[2] = 0x47000000u;  // RendererCoreDepth::scale
    scale.sw[3] = kVerts;
    c.push_back(scale);
    qword_t giftag;
    giftag.sw[0] = kVerts;  // NLOOP
    giftag.sw[1] = 0x00008000u;
    giftag.sw[2] = 0x00000412u;
    giftag.sw[3] = 0u;
    c.push_back(giftag);
    // One REF per vertex stream, naming the bag's arrays in place.
    for (unsigned s = 0; s < kStreams; ++s) {
      const qword_t* at = &g.stream[s][p * kVerts];
      c.push_back(tagQw(kRef, kVerts, at, vifStcycl(1, 1),
                        vifUnpackV4_32(kVerts, kDestVert + s * kVerts)));
    }
    // CNT with qwc 0: FLUSH plus the program kick.
    c.push_back(tagQw(kCnt, 0, nullptr, vifFlush(),
                      kicked ? vifMscnt() : vifMscal(kProgramAddr)));
    kicked = true;
  }
  c.push_back(tagQw(kEnd, 0, nullptr, vifNop(), vifNop()));
  return c;
}

// ---------------------------------------------------------------------------
// Arm B: the BAKED shape. The same words, transcoded into one tag-free block
// that a single REF replays - the two VIF NOPs go in FRONT of each header
// quadword's real codes, so the inline payload stays quadword aligned.
// ---------------------------------------------------------------------------
static std::vector<qword_t> buildBakedBlock(const Geometry& g,
                                            unsigned fromPkg, unsigned toPkg,
                                            bool* kickState = nullptr) {
  std::vector<qword_t> b;
  bool localKick = false;
  bool& kicked = kickState != nullptr ? *kickState : localKick;
  auto header = [&](u32 vif0, u32 vif1) {
    qword_t h;
    h.sw[0] = vifNop();
    h.sw[1] = vifNop();
    h.sw[2] = vif0;
    h.sw[3] = vif1;
    b.push_back(h);
  };
  for (unsigned p = fromPkg; p < toPkg; ++p) {
    header(vifStcycl(1, 1), vifUnpackV4_32(2, kDestConst));
    qword_t scale;
    scale.sw[0] = 0x45000000u;
    scale.sw[1] = 0x45000000u;
    scale.sw[2] = 0x47000000u;
    scale.sw[3] = kVerts;
    b.push_back(scale);
    qword_t giftag;
    giftag.sw[0] = kVerts;
    giftag.sw[1] = 0x00008000u;
    giftag.sw[2] = 0x00000412u;
    giftag.sw[3] = 0u;
    b.push_back(giftag);
    for (unsigned s = 0; s < kStreams; ++s) {
      header(vifStcycl(1, 1), vifUnpackV4_32(kVerts, kDestVert + s * kVerts));
      for (unsigned v = 0; v < kVerts; ++v)
        b.push_back(g.stream[s][p * kVerts + v]);
    }
    header(vifFlush(), kicked ? vifMscnt() : vifMscal(kProgramAddr));
    kicked = true;
  }
  return b;
}

static std::vector<qword_t> buildBakedChain(const std::vector<qword_t>& block) {
  // The block goes into the low arena, because that is what a REF may name.
  const qword_t* at = lowPut(block.data(), block.size());
  std::vector<qword_t> c;
  c.push_back(
      tagQw(kRef, static_cast<u32>(block.size()), at, vifNop(), vifNop()));
  c.push_back(tagQw(kEnd, 0, nullptr, vifNop(), vifNop()));
  return c;
}

// --- Running the real decoder ----------------------------------------------

struct Result {
  u64 hash;
  bool broken;
  u32 words;
};

static Result fold(const std::vector<std::vector<qword_t>>& chains) {
  StaPipVifHash h;
  for (const auto& c : chains) h.foldChain(c.data(), static_cast<u32>(c.size()));
  Result r;
  r.broken = h.isBroken();
  r.words = h.getWords();
  h.endFrame();
  r.hash = h.getRing((h.getRingAt() + StaPipVifHash::kRing - 1) %
                     StaPipVifHash::kRing);
  r.words = h.getWords();
  return r;
}

static int failures = 0;
static void expect(bool ok, const char* what) {
  std::printf("  %-58s %s\n", what, ok ? "ok" : "FAIL");
  if (!ok) ++failures;
}

int main() {
  lowInit(8u << 20);
  const Geometry g = makeGeometry();

  // --- The claim ------------------------------------------------------------
  std::printf("The claim: same words, different chains, same hash\n");

  const auto ctlChain = buildControlChain(g, 0, kPackages);
  const auto block = buildBakedBlock(g, 0, kPackages);
  const auto bakedChain = buildBakedChain(block);

  const Result ctl = fold({ctlChain});
  const Result baked = fold({bakedChain});

  std::printf("  control chain  %3zu qw   baked chain  %3zu qw (+ %3zu qw block)\n",
              ctlChain.size(), bakedChain.size(), block.size());
  std::printf("  control hash   %08x%08x\n", (u32)(ctl.hash >> 32), (u32)ctl.hash);
  std::printf("  baked   hash   %08x%08x\n", (u32)(baked.hash >> 32), (u32)baked.hash);
  expect(!ctl.broken && !baked.broken, "neither arm broke the decoder");
  expect(ctl.hash == baked.hash,
         "36 chain qw and 2 chain qw hash IDENTICALLY");
  expect(ctlChain.size() != bakedChain.size(),
         "...and the two chains really are different shapes");

  // --- What the cadence does and does not do to the stream -------------------
  //
  // Be precise here, because it is the load-bearing distinction. Moving the
  // flush cadence DOES change what VIF1 receives, by exactly the per-packet end
  // tag and the re-kick at the head of each packet - the engine emits both
  // there today. The gate is right to see that; it is a real change to the
  // stream. What the gate must NOT do is see the packet BOUNDARY as such, i.e.
  // it must agree between two arms that chose the same cadence but built the
  // chain differently. That is the case the counter gate forbade outright,
  // because `packetFlushes` can only be held equal by holding the cadence.
  std::printf("\nThe cadence is invisible: 1 packet vs 3 over the same bag\n");
  const Result oneFlush = fold({buildControlChain(g, 0, kPackages)});
  bool k3 = false;
  const auto p0 = buildControlChain(g, 0, 2, &k3);
  const auto p1 = buildControlChain(g, 2, 4, &k3);
  const auto p2 = buildControlChain(g, 4, kPackages, &k3);
  const Result threeFlushes = fold({p0, p1, p2});
  expect(threeFlushes.hash == oneFlush.hash,
         "THREE packets hash exactly as ONE does");
  expect(threeFlushes.words == oneFlush.words + 2 * 2,
         "...differing only by two extra end tags, whose slots are NOP");

  // The case the counter gate forbade outright: a different cadence AND a
  // different chain construction, against the one-packet control.
  bool kb = false;
  const auto blockA = buildBakedBlock(g, 0, 2, &kb);
  const auto blockB = buildBakedBlock(g, 2, kPackages, &kb);
  const Result bakedSplit = fold({buildBakedChain(blockA),
                                  buildBakedChain(blockB)});
  expect(bakedSplit.hash == ctl.hash,
         "two baked packets == one unbaked packet: THE PRIZE IS CHECKABLE");

  // --- Six defects, each of which MUST change the hash ----------------------
  std::printf("\nSix defects, each of which must be caught\n");

  {  // 1. one stale vertex, deep inside the referenced payload
    std::vector<qword_t> c = block;
    // A payload quadword rather than a header: headers carry two leading NOPs.
    size_t at = 0;
    for (size_t i = 0; i < c.size(); ++i)
      if (c[i].sw[0] != 0u || c[i].sw[1] != 0u) { at = i; }
    c[at].sw[1] ^= 1u;
    expect(fold({buildBakedChain(c)}).hash != baked.hash,
           "one vertex word flipped, deep inside a REF payload");
  }
  {  // 2. a dropped package
    const auto c = buildBakedBlock(g, 0, kPackages - 1);
    expect(fold({buildBakedChain(c)}).hash != baked.hash,
           "one package dropped from the run");
  }
  {  // 3. packages reordered
    std::vector<qword_t> c = buildBakedBlock(g, 2, kPackages);
    const auto head = buildBakedBlock(g, 0, 2);
    c.insert(c.end(), head.begin(), head.end());
    expect(fold({buildBakedChain(c)}).hash != baked.hash,
           "the same packages, submitted in the wrong order");
  }
  {  // 4. the wrong program kicked
    std::vector<qword_t> c = buildBakedBlock(g, 0, kPackages);
    for (auto& q : c)
      if (q.sw[3] == vifMscal(kProgramAddr)) q.sw[3] = vifMscal(kProgramAddr + 8);
    expect(fold({buildBakedChain(c)}).hash != baked.hash,
           "MSCAL naming a different microprogram address");
  }
  {  // 5. a wrong GIFtag (NLOOP), which no vertex compare would see
    std::vector<qword_t> c = buildBakedBlock(g, 0, kPackages);
    for (auto& q : c)
      if (q.sw[1] == 0x00008000u && q.sw[0] == kVerts) { q.sw[0] = kVerts - 1; break; }
    expect(fold({buildBakedChain(c)}).hash != baked.hash,
           "the prim GIFtag's NLOOP off by one");
  }
  {  // 6. a texture change that moved relative to the draws
    StaPipVifHash a, b;
    a.foldChain(ctlChain.data(), (u32)ctlChain.size());
    a.foldTextureMutation(0);
    a.foldChain(ctlChain.data(), (u32)ctlChain.size());
    a.endFrame();
    b.foldChain(ctlChain.data(), (u32)ctlChain.size());
    b.foldChain(ctlChain.data(), (u32)ctlChain.size());
    b.foldTextureMutation(0);
    b.endFrame();
    expect(a.getRing(0) != b.getRing(0),
           "a texture upload between a different pair of draws");
  }

  // --- NOP padding must NOT change the hash ---------------------------------
  std::printf("\nAnd one thing that must NOT be caught\n");
  {
    // Extra NOPs anywhere a VIFcode may stand. The baked block's own alignment
    // padding is made of these - 40 bytes per package of real transfer - and if
    // they counted, the two arms could never agree.
    std::vector<qword_t> c;
    c.push_back(tagQw(kCnt, 1, nullptr, vifNop(), vifNop()));
    qword_t nops;
    nops.sw[0] = nops.sw[1] = nops.sw[2] = nops.sw[3] = vifNop();
    c.push_back(nops);
    for (const auto& q : bakedChain)
      if (((q.sw[0] >> 28) & 7u) != kEnd) c.push_back(q);
    c.push_back(tagQw(kEnd, 0, nullptr, vifNop(), vifNop()));
    const Result padded = fold({c});
    expect(padded.hash == baked.hash, "six extra VIF NOPs change nothing");
    expect(padded.words > baked.words,
           "...although they really were transferred");
  }

  // --- The decoder fails loudly rather than silently ------------------------
  std::printf("\nAnd the failure mode is loud, not silent\n");
  {
    std::vector<qword_t> c = bakedChain;
    // MPG: a code whose data length this decoder has no business guessing.
    c[0].sw[2] = (0x4Au << 24);
    StaPipVifHash h;
    h.foldChain(c.data(), (u32)c.size());
    expect(h.isBroken() && h.getBrokenCmd() == 0x4Au,
           "an unknown VIFcode latches `broken` and names itself");
  }
  {
    // A REF whose address is not quadword aligned. The DMAC drops the low four
    // bits, so it would transfer from somewhere else - and the decoder, reading
    // the pointer as given, could never notice through the hash.
    std::vector<qword_t> c = bakedChain;
    c[0].sw[1] += 4u;
    StaPipVifHash h;
    h.foldChain(c.data(), (u32)c.size());
    expect(h.isBroken() && h.getBrokenCmd() == 0xA11u,
           "a misaligned REF address is refused, not hashed");
  }

  std::printf("\n%s (%d failure%s)\n", failures == 0 ? "PASS" : "FAIL",
              failures, failures == 1 ? "" : "s");
  return failures == 0 ? 0 : 1;
}
