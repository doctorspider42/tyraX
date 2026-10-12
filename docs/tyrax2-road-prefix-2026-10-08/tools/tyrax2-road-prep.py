"""Private selected-road preparation clocks; normal drawing in all three arms."""
import importlib.util
import json
import os
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
out, source = [Path(p).resolve() for p in sys.argv[1:3]]
prefix = '--prefix' in sys.argv
subprocess.run([sys.executable, str(ROOT/'tools/tyrax2-vu-paint.py'),
                str(out), str(source), '--roads-day'], check=True,
               env={**os.environ, 'PYTHONUTF8': '1'})


def edit(path, old, new, count=1):
    text = path.read_text(encoding='utf-8-sig')
    assert text.count(old) == count, (path, old, text.count(old), count)
    path.write_bytes(text.replace(old, new).encode())


engine = out/'tyra/engine'
core = engine/'src/renderer/3d/pipeline/static/core'
edit(out/'game/src/gen/game_physics.gen.cpp',
     'part.envTexBag->paintVu = MotionProbe::variant() == 6;',
     'part.envTexBag->paintVu = true;')
edit(engine/'inc/renderer/3d/pipeline/static/core/bag/stapip_bag.hpp',
     '  StaPipBag();', '  bool diagnosticRoad = false;\n  StaPipBag();')
proc = next(p for p in (out/'game/src').rglob('*.cpp')
            if '    c.colors.bind(c.colorBag);' in p.read_text(encoding='utf-8-sig'))
edit(proc, '    c.colors.bind(c.colorBag);',
     '    c.colors.bind(c.colorBag);\n    c.bag->diagnosticRoad = c.owner == -3;')
edit(out/'game/src/gen/game_vehicles.gen.cpp',
     '    submitHeavy(c.bag.get());',
     '    TYRA_ASSERT(c.bag->diagnosticRoad,"Road preparation ownership");\n    submitHeavy(c.bag.get());')
header = engine/'inc/debug/motion_probe.hpp'
edit(header, '  clocks=false;\n#ifdef MOTION_SCOPE_TRIAL',
     '  clocks=sample && variant()==6;\n#ifdef MOTION_SCOPE_TRIAL')
edit(header, 'active(clocks && partialDepth)', 'active(clocks && coreDepth)')
edit(header, 'children>elapsed || !partialDepth', 'children>elapsed || !coreDepth')
edit(core/'stapip_core.cpp', '  MotionProbe::add(MotionProbe::Bags);',
     '  MotionProbe::Scope probeCore(false, bag->diagnosticRoad);\n  MotionProbe::add(MotionProbe::Bags);')
# Historical partialTicks column now covers ALL selected dispatch routes.
edit(core/'stapip_core.cpp', '  TYRA_ATTRIB_MARK(attribReplayStart);',
     '  MotionProbe::Scope probeDispatch(true, MotionProbe::coreDepth != 0);\n  TYRA_ATTRIB_MARK(attribReplayStart);')
if prefix:
    edit(header, '  TimingCount };',
         '  PrefixHead, PrefixBounds, PrefixTexture, PrefixLight, PrefixObject, TimingCount };')
    edit(header, 'payloadCalls\\n',
         'payloadCalls,prefixHead,prefixBounds,prefixTexture,prefixLight,prefixObject\\n')
    edit(header, 'struct Wait {', '''struct Prefix {
  bool active; Timing kind=PrefixHead; U stamp=0;
  explicit Prefix(bool selected):active(clocks && selected) { if(active)stamp=ticks(); }
  void next(Timing k) { if(active){const U now=ticks();timing[kind]+=now-stamp;stamp=now;kind=k;} }
  void finish(){if(active){timing[kind]+=ticks()-stamp;active=false;}}
  ~Prefix(){finish();}
};
struct Wait {''')
    edit(core/'stapip_core.cpp',
         '  MotionProbe::Scope probeCore(false, bag->diagnosticRoad);',
         '  MotionProbe::Scope probeCore(false, bag->diagnosticRoad);\n  MotionProbe::Prefix probePrefix(bag->diagnosticRoad);')
    for anchor, stage in [('  HardwareTrace::Scope traceBounds(', 'PrefixBounds'),
                          ('  TYRA_ATTRIB_MARK(attribTextureStart);', 'PrefixTexture'),
                          ('  TYRA_ATTRIB_MARK(attribLightStart);', 'PrefixLight'),
                          ('  TYRA_ATTRIB_MARK(attribObjectDataStart);', 'PrefixObject')]:
        edit(core/'stapip_core.cpp', anchor,
             '  probePrefix.next(MotionProbe::'+stage+');\n'+anchor)
    edit(core/'stapip_core.cpp',
         '  MotionProbe::Scope probeDispatch(true, MotionProbe::coreDepth != 0);',
         '  probePrefix.finish();\n  MotionProbe::Scope probeDispatch(true, MotionProbe::coreDepth != 0);')
sites = [
    ('src/renderer/3d/pipeline/static/core/bag/packaging/stapip_bag_packager.cpp',
     '  HardwareTrace::Scope trace("Package_create");', 'Create', 2),
    ('src/renderer/3d/pipeline/static/core/bag/packaging/stapip_bag_packager.cpp',
     '  HardwareTrace::Scope trace("Package_classify");', 'Classify', 1),
    ('src/renderer/3d/pipeline/static/core/stapip_qbuffer.cpp',
     '  HardwareTrace::Scope trace("QBuffer_copy");', 'Fill', 4),
    ('src/renderer/3d/pipeline/static/core/stapip_qbuffer.cpp',
     'void StaPipQBuffer::fillByPointer(const StaPipBagPackage& pkg) {', 'Fill', 1),
    ('src/renderer/3d/pipeline/static/core/stapip_qbuffer_renderer.cpp',
     '  HardwareTrace::Scope trace("Packet_build");', 'Packet', 1),
    ('src/renderer/core/paths/path1/vif1_queue.cpp',
     'u32 Vif1Queue::submit(const void* chain, u32 qwords, bool* sourceCopied) {', 'Submit', 1),
    ('inc/renderer/core/paths/path1/frame_chain_arena.hpp',
     '  static bool copyQwords(void* destination, const void* source, uint32_t count) {', 'Payload', 1),
]
for rel, needle, category, count in sites:
    path = engine/rel
    text = path.read_text(encoding='utf-8-sig')
    if '#include "debug/motion_probe.hpp"' not in text:
        path.write_bytes(('#include "debug/motion_probe.hpp"\n'+text).encode())
    edit(path, needle, needle+'\n  MotionProbe::Bucket probeBucket(MotionProbe::'+category+'Ticks);', count)
edit(out/'game/src/terrain_game.cpp',
     '    printf("LOG: TURNPINS phase=',
     '    printf("LOG: ROADPREP phase=%u clocks=%u normalDrawing=1\\n",MotionProbe::phase,unsigned(MotionProbe::variant()==6));\n    printf("LOG: TURNPINS phase=')
spec = importlib.util.spec_from_file_location('quiet', ROOT/'tools/tyrax2-quiet-fixture.py')
quiet = importlib.util.module_from_spec(spec)
spec.loader.exec_module(quiet)
(out/'source-manifest.json').write_text(json.dumps(dict(
    experiment='road-preparation-scopes', modes=[0,6,0], constantVuPaint=True,
    prefixPartition=prefix,
    normalRoadDrawing=True, partialTicksMeaning='all selected dispatch routes',
    bucketsMeaning='exclusive nested elapsed; copies/submission may mix pending owners',
    sourceProject=str(source), sites=sites,
    files=quiet.source_inputs(out,'vehicle-playground.tyra')), indent=2)+'\n')
print(out)
