"""engine/lateprobe.py - RIG ENGINE ONLY (engine/setup.py): an event ring of
EE waits on the GPU. completePipelineFrame (deferred present + vsync) and
Vif1Queue::drain record {frame phase set by the game, kind, ticks, start}.
Harmless counters; the game dumps them (game/late.py)."""
import sys
import os
E = sys.argv[1].rstrip('/')


def edit(rel, a, b):
    p = os.path.join(E, rel)
    s = open(p, encoding='utf-8', newline='').read()
    if b in s:
        return
    assert s.count(a) == 1, (rel, a[:50])
    s = s.replace(a, b)
    open(p, 'w', encoding='utf-8', newline='').write(s)


edit('inc/debug/frame_profile.hpp', 'extern u32 tPre;\n',
     'extern u32 tPre;\n'
     '// LATEPROBE (rig): GPU waits by game phase.\n'
     'struct LateEvent { u32 frame; u16 phase, kind; u32 ticks; };\n'
     'extern volatile int probePhase;\n'
     'extern volatile u32 probeFrame;\n'
     'extern LateEvent lateRing[512];\n'
     'extern u32 lateCount;\n'
     'inline void lateRecord(u16 kind, u32 ticks) {\n'
     '  if (ticks < 29491) return;  // < 0.1 ms\n'
     '  LateEvent& e = lateRing[lateCount++ & 511];\n'
     '  e.frame = probeFrame; e.phase = (u16)probePhase; e.kind = kind; e.ticks = ticks;\n'
     '}\n')
edit('src/debug/frame_profile.cpp', 'u32 tPre = 0;\n',
     'u32 tPre = 0;\nvolatile int probePhase = 0;\nvolatile u32 probeFrame = 0;\n'
     'LateEvent lateRing[512];\nu32 lateCount = 0;\n')
edit('src/renderer/core/renderer_core.cpp',
     '''void RendererCore::completePipelineFrame() {
  if (!pipelineFramePending) return;
  pipelineFramePending = false; // waits must not recursively present this job
''', '''void RendererCore::completePipelineFrame() {
  if (!pipelineFramePending) return;
  pipelineFramePending = false; // waits must not recursively present this job
  u32 lateT0_;
  __asm__ volatile("mfc0 %0, $9" : "=r"(lateT0_));
  struct LateEnd_ { u32 t0; ~LateEnd_() { u32 t; __asm__ volatile("mfc0 %0, $9" : "=r"(t)); FrameProfile::lateRecord(1, t - t0); } } lateEnd_{lateT0_};
''')
edit('src/renderer/core/paths/path1/vif1_queue.cpp', '''void Vif1Queue::drain() {
  closeOpenChain();
''', '''void Vif1Queue::drain() {
  u32 lateT0_;
  __asm__ volatile("mfc0 %0, $9" : "=r"(lateT0_));
  struct LateEnd_ { u32 t0; ~LateEnd_() { u32 t; __asm__ volatile("mfc0 %0, $9" : "=r"(t)); FrameProfile::lateRecord(2, t - t0); } } lateEnd_{lateT0_};
  closeOpenChain();
''')
edit('src/renderer/core/paths/path1/vif1_queue.cpp', '#include "info/info.hpp"\n',
     '#include "info/info.hpp"\n#include "debug/frame_profile.hpp"\n')
print('lateprobe engine ok')
