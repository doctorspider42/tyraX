"""Private same-ELF early env-prefix publication on disjoint native windows."""
import argparse
import hashlib
import json
import shutil
from pathlib import Path

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('out', type=Path)
p.add_argument('source', type=Path)
a = p.parse_args()
sha = lambda f: hashlib.sha256(f.read_bytes()).hexdigest()
generator_sha = sha(Path(__file__))
repo = Path(__file__).resolve().parent
helper_pins = {name: sha(repo / name) for name in
               ('tyrax2-native-segments.hpp', 'tyrax2-native-segment-mode.hpp')}
assert not a.out.exists()
binding = json.loads((a.source / 'native-binding.json').read_text())
for rel, digest in binding['files'].items():
    assert sha(a.source / rel) == digest, rel
assert sha(a.source / 'game/bin/vehicle-playground.elf') == binding['elfSha256']
assert sha(a.source / 'tyra/engine/bin/libtyra.a') == binding['engineArchiveSha256']
for rel in ('world-cell-price-emulator.json', 'physical-v1/world-cell-price-ps2.json'):
    report = json.loads((a.source / rel).read_text())
    assert report['valid'] and report['variant'] == 'audio-event'
    assert report['elfSha256'] == binding['elfSha256']

def ignore(path, names):
    return [n for n in names if n in ('obj', '.git') or
            (Path(path).name == 'bin' and
             (n in ('log.txt', 'drive-probe.csv') or n.startswith('triple-present-'))) or
            (n.startswith(('tape-', 'command-', 'cell-', 'source-', 'moving-', 'world-price-'))
             and n.endswith('.bin'))]

for part in ('game', 'tyra/engine'):
    shutil.copytree(a.source / part, a.out / part, ignore=ignore)
for rel in ('tyra/Makefile.base', 'authored-manifest.json'):
    shutil.copy2(a.source / rel, a.out / rel)
for src, dest in [('tyrax2-native-segments.hpp', 'native_segments.hpp'),
                  ('tyrax2-native-segment-mode.hpp', 'native_segment_mode.hpp')]:
    assert sha(repo / src) == helper_pins[src], 'Helper changed during generation'
    shutil.copy2(repo / src, a.out / 'tyra/engine/inc/debug' / dest)

def edit(rel, old, new):
    file = a.out / rel
    text = file.read_text()
    assert text.count(old) == 1, (rel, old, text.count(old))
    file.write_text(text.replace(old, new))

moving = 'game/inc/world_moving.hpp'
edit(moving, '#pragma once', '#pragma once\n#include "debug/native_segment_mode.hpp"')
edit(moving, 'AudioIdle::set(p<6 && modes[p]);',
     'AudioIdle::set(true);NativeSegments::enabled=p<6 && modes[p];')
edit(moving, 'WheelVu::reset();AudioIdle::reset();',
     'WheelVu::reset();AudioIdle::reset();NativeSegments::reset();')
edit(moving, 'variant=audio-event\\n",p,unsigned(AudioIdle::enabled));',
     'variant=native-segments\\n",p,unsigned(NativeSegments::enabled));')
edit(moving, 'WheelVu::witness(phase);AudioIdle::witness(phase);',
     'WheelVu::witness(phase);AudioIdle::witness(phase);NativeSegments::witness(phase);')
game = 'game/src/terrain_game.cpp'
edit(game, '  const bool movingAudio=AudioIdle::enabled;',
     '  const bool movingAudio=AudioIdle::enabled;\n  const bool movingSegments=NativeSegments::enabled;')
edit(game, 'AudioIdle::set(arm==1?!movingAudio:movingAudio);',
     'AudioIdle::set(true);NativeSegments::enabled=arm==1?!movingSegments:movingSegments;\n'
     '  const unsigned segmentBaseline=NativeSegments::earlyPublishes;')
edit(game, 'AudioIdle::witness(MotionProbe::phase,int(arm));',
     'AudioIdle::witness(MotionProbe::phase,int(arm));NativeSegments::witness(MotionProbe::phase,int(arm),segmentBaseline);')
edit(game, 'AudioIdle::set(movingAudio);',
     'AudioIdle::set(movingAudio);NativeSegments::enabled=movingSegments;')
edit(game, '      PaintCellPrice::reset();PaintCellPrice::held=true;WheelVu::reset();AudioIdle::reset();',
     '      PaintCellPrice::reset();PaintCellPrice::held=true;WheelVu::reset();AudioIdle::reset();NativeSegments::startAudit();')
edit(game, '    PaintCellPrice::held=false;',
     '    if(i==31)NativeSegments::finishAudit(MotionProbe::phase,arm);\n    PaintCellPrice::held=false;')
physics = 'game/src/gen/game_physics.gen.cpp'
edit(physics, '  if (FastJoin::checkpoint) sharedEnvBasisValid=false;',
     '  if (FastJoin::checkpoint || NativeSegments::audit) sharedEnvBasisValid=false;')
edit(physics, '    core.envMap.end();',
     '    core.envMap.end();\n    Tyra::Vif1Queue::publishNativeSegment();')
header = 'tyra/engine/inc/renderer/core/paths/path1/vif1_queue.hpp'
edit(header, '  static void flushRecording();',
     '  static void flushRecording();\n  static bool publishNativeSegment();')
queue = a.out / 'tyra/engine/src/renderer/core/paths/path1/vif1_queue.cpp'
text = queue.read_text()
text = '#include "debug/native_segment_mode.hpp"\n' + text
anchor = 'FrameVifWriter* nativeWriter = nullptr;'
assert text.count(anchor) == 1
text = text.replace(anchor, anchor + '''
NativeSegments::Cursor nativeSegments[2] = {
    NativeSegments::Cursor(nullptr, 0), NativeSegments::Cursor(nullptr, 0)};
bool segmentBank = false;
bool segmentWindowOpen() {
  return segmentBank && !nativeSegments[currentBank].sealed();
}
void resetNativeWindow() {
  TYRA_ASSERT(!nativeInFlight, "Native segment reset before reader completion");
  if (segmentBank) {
    nativeSegments[currentBank] = NativeSegments::Cursor(nativeBanks[currentBank], kNativeQwords);
    nativeWriter = &nativeSegments[currentBank].writer();
  } else nativeWriter->reset();
}''')
# nativeInFlight is declared after nativeWriter in the original source.
start = text.index('NativeSegments::Cursor nativeSegments[2]')
end = text.index('\nu32 nativePendingSequence', start)
helpers = text[start:end]
text = text[:start] + text[end:]
anchor = 'void (*beforeFrameSubmit)(void*) = nullptr;'
assert text.count(anchor) == 1
text = text.replace(anchor, helpers + '\n' + anchor)
assert text.count('nativeWriter->reset();') == 6  # five original sites + helper OFF arm
text = text.replace('nativeWriter->reset();', 'resetNativeWindow();')
text = text.replace('} else resetNativeWindow();', '} else nativeWriter->reset();', 1)
assert text.count('    if (nativeInFlight) {') == 1
text = text.replace('    if (nativeInFlight) {', '    if (nativeInFlight && !segmentWindowOpen()) {')
assert text.count('if (nativeInFlight || !nativeWriter->availableQwords())') == 1
text = text.replace('if (nativeInFlight || !nativeWriter->availableQwords())',
                    'if ((nativeInFlight && !segmentWindowOpen()) || !nativeWriter->availableQwords())')
anchor = '  nativeInFlight = 0;\n  resetNativeWindow();\n  frameBatchStart = nativeBatches;'
assert text.count(anchor) == 1
text = text.replace(anchor, '''  nativeInFlight = 0;
  segmentBank = pipeline && NativeSegments::enabled;
  nativeWriter = &nativeStorage[currentBank];
  resetNativeWindow();
  NativeSegments::earlyThisFrame = false;
  frameBatchStart = nativeBatches;''')
anchor = '  const u32 qwords = nativeWriter->finish();'
assert text.count(anchor) == 1
text = text.replace(anchor, '''  const auto sealed = segmentBank ? nativeSegments[currentBank].seal() : NativeSegments::Span{};
  const u32 qwords = segmentBank ? sealed.qwords : nativeWriter->finish();
  TYRA_ASSERT(qwords, "Native segment finalization failed");
  NativeSegments::auditPrefix(nativeWriter->data(),qwords);''')
anchor = '  ++nativeBatches;\n#endif\n}\n\nbool Vif1Queue::registerImmutableSpan'
assert text.count(anchor) == 1
text = text.replace(anchor, '''  ++nativeBatches;
  if (segmentBank) nativeSegments[currentBank].advance();
#endif
}

bool Vif1Queue::publishNativeSegment() {
#if TYRA_FRAME_PIPELINE_SUPPORT
  if (!frameRecording || !framePipelined || !segmentBank ||
      NativeSegments::earlyThisFrame) return false;
  closeOpenChain();
  if (!nativePendingSequence) return false;
  flushRecording();
  NativeSegments::earlyThisFrame = true;
  ++NativeSegments::earlyPublishes;
  return true;
#else
  return false;
#endif
}

bool Vif1Queue::registerImmutableSpan''')
queue.write_text(text)
core = 'tyra/engine/src/renderer/core/renderer_core.cpp'
file = a.out / core
file.write_text('#include "debug/native_segment_mode.hpp"\n' + file.read_text())
edit(core, '  static_cast<RendererCore*>(user)->completePipelineFrame();', '''  auto* core = static_cast<RendererCore*>(user);
  const auto recordingContext = core->gs.getRecordingContext();
  core->completePipelineFrame();
  if (NativeSegments::enabled) core->gs.setRecordingContext(recordingContext);''')
engine = 'tyra/engine/src/engine.cpp'
file = a.out / engine
file.write_text('#include "debug/native_segment_mode.hpp"\n' + file.read_text())
edit(engine, '  MotionProbe::end(renderer.core.getStallTotal());', '''  if(MotionProbe::gas()) {
    ++NativeSegments::gasFrames;
    if(NativeSegments::earlyThisFrame)++NativeSegments::gasPublishes;
  }
  MotionProbe::end(renderer.core.getStallTotal());''')
manifest = json.loads((a.source / 'world-cell-price-manifest.json').read_text())
assert sha(Path(__file__)) == generator_sha, 'Generator changed during generation'
manifest.update(baseElfSha256=binding['elfSha256'], generatorSha256=generator_sha,
                segmentHelperHashes=helper_pins,
                variant='native-segments', nativeSegments=True, status='UNQUALIFIED',
                comparison='common event audio/direct terrain/VU paint/hybrid wheels: one prefix versus early env prefix',
                limits=['No new framebuffer or source arena; native windows share existing fenced bank.',
                        'Sole final FINISH, retained ordered queue sequence/immutable bank leases.',
                        'Source snapshots, draws/geometry/quality/targets unchanged.',
                        'Gas/held activation only, no additional target clocks.',
                        'Common OFF/ON final held audit forces capture outside price; ordinary moving reuse unchanged.',
                        'Host storage checks do not qualify actual queue/context/source lifetime.'])
(a.out / 'world-cell-price-manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
print(a.out, 'UNQUALIFIED early native segments')
