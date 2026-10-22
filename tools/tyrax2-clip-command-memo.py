"""P08 private memo of original clip-TC DMA stream commands, not payloads."""
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
out, source = [Path(p).resolve() for p in sys.argv[1:3]]
reverse = '--reverse' in sys.argv
subprocess.run([sys.executable, str(ROOT/'tools/tyrax2-vu-paint.py'), str(out),
    str(source), '--roads-day'] + (['--reverse'] if reverse else []),
    check=True, env={**os.environ, 'PYTHONUTF8': '1'})
def edit(path, old, new):
    text = path.read_text(encoding='utf-8-sig')
    assert text.count(old) == 1, (path, old, text.count(old))
    path.write_bytes(text.replace(old, new).encode())
engine = out/'tyra/engine'
game = out/'game'
edit(game/'inc/terrain_config.hpp', 'constexpr int INTERLEAVE_PASSES = 1;', 'constexpr int INTERLEAVE_PASSES = 2;')
edit(game/'src/gen/game_physics.gen.cpp', 'part.envTexBag->paintVu = MotionProbe::variant() == 6;', 'part.envTexBag->paintVu = true;')
edit(engine/'inc/renderer/3d/pipeline/static/core/bag/stapip_bag.hpp', '  StaPipBag();', '  unsigned diagnosticRoadId=0;\n  StaPipBag();')
edit(game/'src/gen/game_collision.gen.cpp', '    c.colors.bind(c.colorBag);',
    '    c.colors.bind(c.colorBag);\n    c.bag->diagnosticRoadId=c.owner==-3?unsigned(&c-procChunks.data())+1:0;')
(engine/'inc/debug/clip_command_memo.hpp').write_bytes((ROOT/'tools/tyrax2-clip-command-memo.hpp').read_bytes())
edit(engine/'inc/debug/motion_probe.hpp', 'inline bool observing(){return false;}',
    'inline U cmdCalls=0,cmdHits=0,cmdChecks=0,cmdFailed=0,cmdApplied=0,cmdPadding=0;\ninline bool observing(){return false;}')
program=engine/'src/renderer/3d/pipeline/static/core/stapip_vu1_program.cpp'
edit(program, '#include "renderer/core/gs/renderer_core_depth.hpp"',
    '#include "renderer/core/gs/renderer_core_depth.hpp"\n#include "debug/motion_probe.hpp"\n#include "debug/clip_command_memo.hpp"')
edit(program, '  addProgramQBufferDataToPacket(packet, buffer);', r'''  const bool selected = name == StaPipClipTextureColor &&
      (buffer->bag->diagnosticRoadId == 24 || buffer->bag->diagnosticRoadId == 34);
  const bool cold = MotionProbe::offset >= 1000 && MotionProbe::offset < 1100;
  if (!selected || (!cold && MotionProbe::variant() != 6)) {
    addProgramQBufferDataToPacket(packet, buffer);
    return;
  }
  static ClipCommandMemo::Cache cache;
  const ClipCommandMemo::Key key{reinterpret_cast<u32>(buffer->vertices),
      reinterpret_cast<u32>(buffer->sts), reinterpret_cast<u32>(buffer->colors),
      buffer->size, unsigned(buffer->bag->color->single != nullptr)};
  auto* start = packet->next;
  const auto result = cache.emit(key, reinterpret_cast<uint32_t*>(start), cold, [&]() {
    addProgramQBufferDataToPacket(packet, buffer);
    return static_cast<uint32_t>(packet->next - start);
  });
  TYRA_ASSERT(result.equal && result.qw >= 2 && result.qw <= 3,
      "Clip command memo changed packet bytes");
  packet->next = start + result.qw;
  if (cold) {
    ++MotionProbe::cmdCalls;
    if (result.hit) {
      ++MotionProbe::cmdHits; ++MotionProbe::cmdChecks;
      if (MotionProbe::variant() == 6) ++MotionProbe::cmdApplied;
    }
    if (!result.equal) ++MotionProbe::cmdFailed;
    if (result.paddingDifferent) ++MotionProbe::cmdPadding;
  }''')
edit(game/'src/terrain_game.cpp',
    '  if(MotionProbe::phase<MotionProbe::phaseCount && MotionProbe::offset==1100) {', r'''  if(MotionProbe::offset==1000)
    MotionProbe::cmdCalls=MotionProbe::cmdHits=MotionProbe::cmdChecks=MotionProbe::cmdFailed=MotionProbe::cmdApplied=MotionProbe::cmdPadding=0;
  if(MotionProbe::phase<MotionProbe::phaseCount && MotionProbe::offset==1100) {
    printf("LOG: CLIPCMD phase=%u calls=%u hits=%u checks=%u failed=%u applied=%u interleave=%u\n",
      MotionProbe::phase,MotionProbe::cmdCalls,MotionProbe::cmdHits,MotionProbe::cmdChecks,
      MotionProbe::cmdFailed,MotionProbe::cmdApplied,unsigned(INTERLEAVE_PASSES));
    printf("LOG: CMDPADDING phase=%u changed=%u\n",MotionProbe::phase,MotionProbe::cmdPadding);
    if(!MotionProbe::cmdHits || MotionProbe::cmdFailed)MotionProbe::valid=false;''')
spec=importlib.util.spec_from_file_location('quiet', ROOT/'tools/tyrax2-quiet-fixture.py')
quiet=importlib.util.module_from_spec(spec); spec.loader.exec_module(quiet)
(out/'source-manifest.json').write_text(json.dumps(dict(experiment='clip-command-body-memo',
    modes=[6,0,6] if reverse else [0,6,0], ids=[24,34], entries=128,
    freshHeaderAndKick=True, cachedPayload=False, fixedInterleave=2, constantVuPaint=True,
    files=quiet.source_inputs(out,'vehicle-playground.tyra')),indent=2)+'\n')
print(out)
