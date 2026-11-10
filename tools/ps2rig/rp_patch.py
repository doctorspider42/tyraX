"""rp_patch.py <engine-root-copy>: record-pass CEILING probe (rig only).

With Cld::arm == 1, a stamp-pass draw whose record list called it wholly
inside the frustum (PassDraw::rec == 1) and whose stamp record is valid is
emitted from the stamp alone: candidacy flush, LRU touch, head copy, MVP +
fog patch, bookkeeping - every other per-draw check skipped (facts,
revision, model, visibility, epochs, texture / light re-resolve, prim
state, entry and wrap checks). UNSAFE (stale spot / TEX0 possible): it
prices the floor of an emission that reads only a record.
Counts: CLDRP lines (fast draws / frames) every 600 frames.
"""
import sys, pathlib
eng = pathlib.Path(sys.argv[1]) / "engine"
p = eng / "src/renderer/3d/pipeline/static/core/stapip_qbuffer_renderer.cpp"
t = p.read_text(encoding="utf-8")
if "cld_probe.hpp" not in t:
    t = '#include "debug/cld_probe.hpp"\n' + t
anchor = """    if (!(r.flags & kRecValid) || r.bag != bag) goto general;
    // ---- producer facts: the class revision (tagged bag) or the full check
"""
assert t.count(anchor) == 1
probe = """    if (!(r.flags & kRecValid) || r.bag != bag) goto general;
    if (!dry && Cld::arm == 2 && d.rec == 1 && !CrossPass::xonly[slot] &&
        s->qw != 0) {
      // ceiling of the whole draw: not emitted at all (EE cost only).
      static u32 skFast = 0, skFrames = 0, skLast = 0;
      if (DrawEpochs::g.frame != skLast) {
        skLast = DrawEpochs::g.frame;
        if (++skFrames % 600 == 0) printf("CLDSK skipped=%u frames=%u\\n", skFast, skFrames);
      }
      ++skFast;
      continue;
    }
    if (!dry && Cld::arm == 1 && d.rec == 1 && !CrossPass::xonly[slot] &&
        s->qw != 0) {
      static u32 rpFast = 0, rpFrames = 0, rpLastFrame = 0;
      if (DrawEpochs::g.frame != rpLastFrame) {
        rpLastFrame = DrawEpochs::g.frame;
        if (++rpFrames % 600 == 0) printf("CLDRP fast=%u frames=%u\\n", rpFast, rpFrames);
      }
      ++rpFast;
      const Texture* tex = static_cast<const Texture*>(s->texture);
      const bool cand = (r.flags & kRecCand) != 0;
      const bool enabled = submissionBatchScope && cand;
      if (objectDataPending &&
          (!enabled || packet2_get_qw_count(packets[context]) + kWorstBagPacketSize >
                           packetSize))
        flushPendingPacket();
      submissionBatchCandidate = enabled;
      if (enabled && tex != nullptr) submissionPacketHasTexture = true;
      if (tex != nullptr && r.tb != nullptr && r.allocIdx >= 0 &&
          static_cast<u32>(r.allocIdx) < T.residentCount()) {
        T.touchResident(r.allocIdx);
        T.clut.psm = r.clutPsm;
        T.clut.load_method = r.clutLoad;
        T.clut.address = r.clutAddr;
        r.tb->info.function = r.func;
      }
      packet2_t* packet = packets[context];
      if (!objectDataPending) {
        startPacket(packet);
        packetWrapSet = false;
      }
      qword_t* dst = packet->next;
      const u32 qw = s->qw;
      const qword_t* src = s->bytes;
#pragma GCC unroll 1
      for (u32 i = 0; i < qw; ++i) dst[i] = src[i];
      qword_t* mq = &dst[s->mvpAt + 1];
      if (r.flags & kRecIdent) {
        const qword_t* id = reinterpret_cast<const qword_t*>(stampFrame.mvpIdent.data);
        mq[0] = id[0];
        mq[1] = id[1];
        mq[2] = id[2];
        mq[3] = id[3];
      } else {
        stampMvp(reinterpret_cast<float*>(mq), vpd, s->model.data);
      }
      stampFogWords(dst[s->optAt + 1], reinterpret_cast<const float*>(mq), s->fogOff,
                    s->fogEligible, s->lo, s->hi);
      packet->next = dst + qw;
      if (s->wrapBlock && r.wanted != nullptr) {
        packetWrap = *r.wanted;
        packetWrapSet = true;
      }
      s->entry->framesLeftToDestroy = StaPipBakedStreams::kLifetimeFrames;
      s->frame = stampHits + stampCaptures;
      lastProgramName = s->program;
      baked.countHit();
      objectDataPending = true;
      ++submissionBatchBags;
      if (!submissionBatchCandidate ||
          packet2_get_qw_count(packet) + kStampMaxQw > packetSize)
        flushPendingPacket();
      ++stampHits;
      continue;
    }
    // ---- producer facts: the class revision (tagged bag) or the full check
"""
t = t.replace(anchor, probe)
p.write_bytes(t.encode("utf-8"))
print("rp probe in")
