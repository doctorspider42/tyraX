"""engine/gpuwait.py - RIG ENGINE ONLY (engine/setup.py): per-frame totals of
every EE wait on the GPU inside Vif1Queue, no threshold:
probeGpuWait[0] ring-full back-pressure spins (both submit paths),
probeGpuWait[1] waitFor(), probeGpuWait[2] drain(). Ticks; the game zeroes
them each frame (game/gpuwait.py). Idempotent."""
import sys
root = sys.argv[1].rstrip('/') + '/'


def edit(rel, pairs):
    p = root + rel
    s = open(p, encoding='utf-8', newline='').read()
    assert '\r' not in s
    for a, b in pairs:
        if b in s:
            continue
        assert s.count(a) == 1, (rel, a[:60], s.count(a))
        s = s.replace(a, b)
    open(p, 'w', encoding='utf-8', newline='').write(s)


edit('inc/debug/frame_profile.hpp', [
    ('extern LateEvent lateRing[512];\n',
     'extern LateEvent lateRing[512];\nextern volatile u32 probeGpuWait[4];\n')])
edit('src/debug/frame_profile.cpp', [
    ('LateEvent lateRing[512];\n', 'LateEvent lateRing[512];\nvolatile u32 probeGpuWait[4];\n')])
Q = 'src/renderer/core/paths/path1/vif1_queue.cpp'
GW = ('{ u32 gw0_; __asm__ volatile("mfc0 %%0, $9" : "=r"(gw0_)); %s'
      ' u32 gw1_; __asm__ volatile("mfc0 %%0, $9" : "=r"(gw1_)); FrameProfile::probeGpuWait[%d] += gw1_ - gw0_; }')
edit(Q, [
    ('    while (tail - head >= kDepth) advanceIfIdle();\n',
     '    ' + GW % ('while (tail - head >= kDepth) advanceIfIdle();', 0) + '\n'),
    ('  while (tail - head >= kRingDepth) advanceIfIdle();\n',
     '  ' + GW % ('while (tail - head >= kRingDepth) advanceIfIdle();', 0) + '\n'),
    ('  while (static_cast<s32>(completed - sequence) < 0) advanceIfIdle();\n#if TYRA_VIF1_QUEUE_HOLD\n  advanceIfIdle();  // notice',
     '  ' + GW % ('while (static_cast<s32>(completed - sequence) < 0) advanceIfIdle();', 1) +
     '\n#if TYRA_VIF1_QUEUE_HOLD\n  advanceIfIdle();  // notice'),
    ('  while (running) advanceIfIdle();\n#if TYRA_VIF1_QUEUE_HOLD\n  closeSegmentIfIdle();\n#endif\n  dma_channel_wait(DMA_CHANNEL_VIF1, 0);\n',
     '  ' + GW % ('while (running) advanceIfIdle();\n#if TYRA_VIF1_QUEUE_HOLD\n  closeSegmentIfIdle();\n#endif\n  dma_channel_wait(DMA_CHANNEL_VIF1, 0);', 2) + '\n'),
])
print('gpuwait engine ok')
