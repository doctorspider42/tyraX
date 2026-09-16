/*
# TyraX addition: the acceptance gate for a restructured static pipeline.
#
# docs/baked-stream-acceptance-gate.md is the design; this is leg 1 of it.
#
# WHY THIS EXISTS. Every earlier renderer round on this branch was accepted by
# "the picture is byte-identical AND packetFlushes / the FTCLIP routing counts /
# the submitted vertices / the triangles did not move". That gate cannot check a
# change to the SUBMISSION STRUCTURE itself, because a run of packages under one
# DMA REF tag cannot cross a packet flush boundary - so pinning packetFlushes
# pins the prize. docs/baked-vif-stream.md says exactly that in its own Limits
# section, and it is why the spike could not deliver one.
#
# WHAT IT CHECKS INSTEAD. The DMAC does not interpret DMA tags inside referenced
# data; it feeds every word of a REF payload to VIF1, and with tag-transfer
# enabled it feeds VIF1 the upper eight bytes of every chain tag as well. So
# what VIF1 actually receives is ONE UNDIFFERENTIATED WORD STREAM, and that
# stream - not the chain that delivered it - decides the picture. (That is the
# same argument docs/baked-vif-stream.md uses to justify a REF payload carrying
# inline unpacks. It is load bearing twice.)
#
# This walks the finished chain just before it is sent, flattens it (inline data
# for CNT, the referenced quadwords for REF), decodes the resulting word stream
# THE WAY VIF1 DOES - read a VIFcode, consume the data words it takes, read the
# next - and folds a canonical form into a per-frame rolling hash. Two chains
# that hand VIF1 the same words hash the same however they were built:
#
#   - seven chain quadwords per package and one REF covering a run of packages
#     produce the SAME hash;
#   - the flush cadence is invisible to it, which is the whole point;
#   - a package dropped, reordered, given the wrong GIFtag, kicked with the
#     wrong program or fed one stale vertex is NOT invisible to it.
#
# NOP IS DROPPED BEFORE FOLDING, and that is required rather than cosmetic: a
# baked block pads each header quadword with two VIF NOPs so its inline payload
# stays quadword-aligned, which is 40 bytes per package of real transfer that
# must not read as a difference. A NOP does nothing to VIF1, so dropping it is
# sound.
#
# AN UNEXPECTED VIFCODE FAILS LOUDLY. It must: the decoder computes how many
# data words follow from the code it just read, so one unknown code and
# everything after it is noise. `broken` latches and the readout says so instead
# of printing a hash that means nothing. Grepping every packet2_* call under
# src/renderer/3d/pipeline/static/ closes the set this pipeline emits at NOP,
# STCYCL, FLUSHE, FLUSH+MSCAL, FLUSH+MSCNT and UNPACK V4_32.
#
# TEXTURE MUTATIONS FOLD INTO THE SAME HASH, IN ORDER. VIF1 path 1 is not the
# only thing this pipeline gives the GS: textures upload on the GIF channel and
# StaPipQBufferRenderer::beforeTextureMutation enforces the ordering by FLUSHING
# THE PENDING PACKET. So a change to the flush cadence can move a texture upload
# relative to the draws around it - a real defect that a pure VIF-word hash is
# blind to. Folding a marker for each mutation makes the hash model the whole of
# this pipeline's contribution to GS state as an ORDERED SEQUENCE OF DRAWS AND
# TEXTURE CHANGES. The cadence stays invisible; the relative order does not.
#
# WHAT IT CANNOT SEE, and the reason leg 2 (byte-identical pixels) is not
# optional: this reads EE-visible memory at CHAIN-BUILD time and the DMAC reads
# it later. Everything in that gap - a block freed or overwritten while a REF to
# it is still live, a cache line the DMAC sees stale - is invisible here BY
# CONSTRUCTION, and that gap is exactly where Probe B's corruption lived
# (examples/vehicle-playground/authoring/ee-probes-2026-09-16). PCSX2 emulates no
# EE data cache, so leg 2 there checks logic and leg 2 on the console checks
# coherency; they are different checks wearing one name.
#
# A GATE ARM IS NEVER A TIMING ARM. Folding reads every payload quadword the
# frame submits - about 167 000 of them, 2.7 MB, in the Motor District's garage
# day - so a build carrying this runs at roughly 15 Hz. That is fine and it is
# the design: correctness and milliseconds are separate ELFs. Never read a
# millisecond off a build with TYRA_STAPIP_VIFHASH on.
*/
#pragma once

#include <tamtypes.h>

/** The gate. Default 0, in the style of TYRA_STAPIP_ATTRIB and
 * TYRA_STAPIP_BAKED_STREAM: one load and branch per bag flush when off, and
 * nothing of the fold linked into a release build. */
#ifndef TYRA_STAPIP_VIFHASH
#define TYRA_STAPIP_VIFHASH 0
#endif

/** How often the readout prints, in frames. A host: write has a period and may
 * not land inside a sampling window - but a gate arm never samples, so this is
 * only about not drowning the log. */
#ifndef TYRA_STAPIP_VIFHASH_PERIOD
#define TYRA_STAPIP_VIFHASH_PERIOD 120
#endif

#if TYRA_STAPIP_VIFHASH

namespace Tyra {

/**
 * The canonical VIF1 word-stream hash. One instance lives in
 * StaPipQBufferRenderer; StaPipCore::onFrameEnd reads and prints it.
 */
class StaPipVifHash {
 public:
  /** The reflection probe alternates every other frame, so a per-frame hash has
   * period two and a single value means nothing. The readout prints a ring, and
   * two arms are compared as SEQUENCES. Eight is comfortably more than any
   * cadence in this scene. */
  static const u32 kRing = 8;

  StaPipVifHash() { reset(); }

  /**
   * Fold one finished DMA chain, exactly as the DMAC will read it. Called from
   * sendPacket() after the chain is complete and before the send, so the bytes
   * folded are the bytes the DMAC is about to fetch.
   */
  void foldChain(const void* base, u32 qwc);

  /**
   * Fold a texture mutation, in sequence with the draws around it. `key` should
   * identify WHAT changed; the gate only needs it to be a deterministic
   * function of the mutation, not a stable address between arms.
   */
  void foldTextureMutation(u32 key) {
    foldWord(kTextureMarker);
    foldWord(key);
  }

  /** Close the frame: bank the hash into the ring and start a new one. */
  void endFrame() {
    ring[ringAt] = accum;
    ringAt = (ringAt + 1) % kRing;
    if (ringCount < kRing) ++ringCount;
    accum = kFnvOffset;
    lastChainQw = chainQw;
    lastWords = words;
    chainQw = words = 0;
    ++frames;
  }

  u64 getRing(u32 i) const { return ring[i % kRing]; }
  u32 getRingCount() const { return ringCount; }
  u32 getRingAt() const { return ringAt; }
  u32 getFrames() const { return frames; }
  /** Latched true by the first VIFcode the decoder does not know. A hash taken
   * after this is meaningless and the readout must say so rather than print
   * it. */
  bool isBroken() const { return broken; }
  u32 getBrokenCmd() const { return brokenCmd; }
  /** Chain quadwords and flattened VIF words folded this frame - diagnostics
   * that explain a hash difference, never gate conditions. */
  u32 getChainQw() const { return lastChainQw; }
  u32 getWords() const { return lastWords; }

  void reset() {
    accum = kFnvOffset;
    for (u32 i = 0; i < kRing; ++i) ring[i] = 0;
    ringAt = ringCount = frames = 0;
    broken = false;
    brokenCmd = 0;
    pending = 0;
    chainQw = words = lastChainQw = lastWords = 0;
  }

 private:
  static const u64 kFnvOffset = 0xcbf29ce484222325ULL;
  static const u64 kFnvPrime = 0x100000001b3ULL;
  /** A word no VIFcode fold can produce, so a texture mutation can never be
   * confused with a draw. Folded codes carry 0x01 in their top byte (see
   * streamWord); this carries 0xFE. */
  static const u32 kTextureMarker = 0xFE000001u;

  void foldWord(u32 w) {
    accum = (accum ^ static_cast<u64>(w)) * kFnvPrime;
  }

  /** One word of the flattened stream, interpreted as VIF1 interprets it. */
  void streamWord(u32 w);

  u64 accum;
  u64 ring[kRing];
  u32 ringAt, ringCount, frames;
  /** Data words still owed to the VIFcode currently open. This is the whole
   * reason the decoder has to be stateful: a word is only a VIFcode when no
   * unpack is still eating. */
  u32 pending;
  u32 chainQw, words;
  /** The previous frame's totals, banked by endFrame so the readout prints a
   * FRAME rather than a running sum. */
  u32 lastChainQw, lastWords;
  bool broken;
  u32 brokenCmd;
};

}  // namespace Tyra

#endif  // TYRA_STAPIP_VIFHASH
