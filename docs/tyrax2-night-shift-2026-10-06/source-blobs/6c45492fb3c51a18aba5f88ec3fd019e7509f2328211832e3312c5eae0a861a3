/*
# TyraX addition: the canonical VIF1 word-stream hash - see the header for the
# design and for why it is not a counter gate.
*/

#include "renderer/3d/pipeline/static/core/stapip_vif_hash.hpp"

#if TYRA_STAPIP_VIFHASH

#include <packet2.h>
#include <dma_tags.h>
#include "debug/debug.hpp"
#include "renderer/3d/pipeline/static/core/stapip_qbuffer.hpp"

namespace Tyra {

// VIFcodes. Only the ones the static pipeline can emit are decoded; anything
// else latches `broken`, because the decoder derives how many DATA words follow
// from the code it just read and a wrong length desynchronises everything after
// it. Failing loudly is the only honest option.
static const u32 kVifNop = 0x00;
static const u32 kVifStcycl = 0x01;
static const u32 kVifStmod = 0x05;
static const u32 kVifMark = 0x07;
static const u32 kVifFlushE = 0x10;
static const u32 kVifFlush = 0x11;
static const u32 kVifFlushA = 0x13;
static const u32 kVifMscal = 0x14;
static const u32 kVifMscalf = 0x15;
static const u32 kVifMscnt = 0x17;
static const u32 kVifStmask = 0x20;
static const u32 kVifStrow = 0x30;
static const u32 kVifStcol = 0x31;

// DMA source-chain tag ids, by hardware number rather than by an SDK macro:
// this file is compiled in a gate arm and must not acquire a dependency the
// ordinary build does not already have.
static const u32 kTagRefe = 0;
static const u32 kTagCnt = 1;
// (NEXT = 2, CALL = 5, RET = 6 are refused below rather than decoded.)
static const u32 kTagRef = 3;
static const u32 kTagRefs = 4;
static const u32 kTagEnd = 7;

void StaPipVifHash::streamWord(u32 w) {
  ++words;
  // An open UNPACK is eating: this word is DATA, not a code. That distinction
  // is the whole reason this has to be a state machine rather than a scan.
  if (pending > 0) {
    foldWord(w);
    fold(pendingPool ? pool : (pendingGeo ? geo : uni), w);
    --pending;
    return;
  }
  if (broken) return;

  const u32 cmd = (w >> 24) & 0xFFu;
  const u32 num = (w >> 16) & 0xFFu;
  const u32 imm = w & 0xFFFFu;

  // NOP is DROPPED. A baked block pads every header quadword with two of them
  // so its inline payload stays quadword aligned; that padding is real transfer
  // and must not read as a difference. A NOP does nothing to VIF1, so this is
  // sound rather than convenient.
  if (cmd == kVifNop) return;

  if ((cmd & 0x60u) == 0x60u) {
    // UNPACK. The transferred word count follows from the format: (vn + 1)
    // elements of (32 >> vl) bits each, `num` times, rounded up to a word.
    // With cl >= wl - and this pipeline only ever writes STCYCL cl=1 wl=1 -
    // the cycle registers do not change how many words are TRANSFERRED.
    const u32 vn = (cmd >> 2) & 0x3u;
    const u32 vl = cmd & 0x3u;
    const u32 n = num == 0 ? 256u : num;
    const u32 bitsPerElem = (vn + 1u) * (32u >> vl);
    const u32 bits = n * bitsPerElem;
    pending = (bits + 31u) / 32u;
    // Bit 14 of the address immediate is "use TOP", i.e. the unpack lands in
    // the VU1 DOUBLE BUFFER. That is the honest split between geometry and the
    // absolute-address uniforms (MVP at 0, lights at 4, options at 8, ALPHA at
    // 21 - stapip_vu1_shared_defines.h), and it is a property of the code
    // rather than a threshold someone has to keep in step.
    pendingGeo = (imm & 0x4000u) != 0u;
    pendingPool = pendingGeo && fromPool;
    // Canonical form: the code's meaning, never its encoding position. The
    // 0x01 tag byte keeps a folded code out of the texture marker's space.
    foldWord(0x01000000u | (cmd << 16) | ((num & 0xFFu) << 8));
    foldWord(imm);
    fold(ctrl, 0x01000000u | (cmd << 16) | ((num & 0xFFu) << 8));
    fold(ctrl, imm);
    return;
  }

  switch (cmd) {
    // Zero-data codes. STCYCL and the MSCAL family carry everything they mean
    // in num/imm, which is exactly what the picture depends on: which program
    // is kicked, and at what address.
    case kVifStcycl:
    case kVifStmod:
    case kVifMark:
    case kVifFlushE:
    case kVifFlush:
    case kVifFlushA:
    case kVifMscal:
    case kVifMscalf:
    case kVifMscnt:
      foldWord(0x01000000u | (cmd << 16) | ((num & 0xFFu) << 8) | 1u);
      foldWord(imm);
      fold(ctrl, 0x01000000u | (cmd << 16) | ((num & 0xFFu) << 8) | 1u);
      fold(ctrl, imm);
      return;
    // Fixed-length codes. Not emitted by this pipeline today, but their length
    // is unambiguous, so decoding them is strictly better than latching broken
    // if a writer ever starts using one.
    case kVifStmask:
      pending = 1;
      foldWord(0x01000000u | (cmd << 16) | 2u);
      return;
    case kVifStrow:
    case kVifStcol:
      pending = 4;
      foldWord(0x01000000u | (cmd << 16) | 3u);
      return;
    default:
      // Everything else, including MPG and DIRECT, whose lengths this decoder
      // has no business guessing. Latch and say so.
      broken = true;
      brokenCmd = cmd;
      return;
  }
}

void StaPipVifHash::foldChain(const void* base, u32 qwc) {
  if (base == nullptr || qwc == 0) return;
  fromPool = false;
  chainQw += qwc;
  const qword_t* src = reinterpret_cast<const qword_t*>(base);

  u32 i = 0;
  while (i < qwc) {
    const dma_tag_t* tag = reinterpret_cast<const dma_tag_t*>(&src[i]);
    const u32 tagQwc = static_cast<u32>(tag->QWC);
    const u32 id = static_cast<u32>(tag->ID);
    const u32* vif = reinterpret_cast<const u32*>(&src[i]) + 2;

    // The DMA TAG ITSELF IS NOT FOLDED, and that is the entire design. Its id,
    // its qwc and the address it names are how the chain was BUILT; only the
    // two VIFcodes it carries in its upper half, and the payload it transfers,
    // reach VIF1. Fold those and nothing else, and a seven-quadword-per-package
    // chain and a single REF over a run of packages become the same hash.
    streamWord(vif[0]);
    streamWord(vif[1]);

    const qword_t* payload = nullptr;
    bool stop = false;
    if (id == kTagCnt || id == kTagEnd) {
      // END still transfers whatever qwc it carries before the chain stops, so
      // it is CNT plus a stop rather than a bare terminator.
      payload = &src[i + 1];
      i += 1 + tagQwc;
      stop = (id == kTagEnd);
      if (i > qwc) {
        broken = true;
        brokenCmd = 0xD00u;  // a tag that runs off the end of its own packet
        return;
      }
    } else if (id == kTagRef || id == kTagRefs || id == kTagRefe) {
      const u32 addr = static_cast<u32>(tag->ADDR);
      // A DMA tag's address field drops its low four bits, so a misaligned REF
      // transfers from somewhere else - and this decoder, reading the pointer
      // as given, would not notice. Check it here rather than hope the hash
      // catches it; it cannot.
      if ((addr & 0xFu) != 0u) {
        broken = true;
        brokenCmd = 0xA11u;  // unaligned REF address
        return;
      }
      payload = reinterpret_cast<const qword_t*>(addr);
      i += 1;
      stop = (id == kTagRefe);
    } else {
      // NEXT, CALL and RET. None is written by this pipeline - bakeBlock
      // refuses them too, and retained-static-commands.md explains why a
      // position-relative tag may never appear here. Latch rather than guess
      // where control would go.
      broken = true;
      brokenCmd = 0xB00u | id;
      return;
    }

    // Which BUFFER this tag's payload came out of. A CNT carries its data
    // inline in the packet; only a REF can name a copy pool.
    fromPool = (id != kTagCnt && id != kTagEnd) &&
               StaPipQBuffer::isPoolAddress(payload);
    for (u32 q = 0; q < tagQwc; ++q) {
      const u32* qw = reinterpret_cast<const u32*>(&payload[q]);
      streamWord(qw[0]);
      streamWord(qw[1]);
      streamWord(qw[2]);
      streamWord(qw[3]);
    }
    if (stop) return;
  }
}

}  // namespace Tyra

#endif  // TYRA_STAPIP_VIFHASH
