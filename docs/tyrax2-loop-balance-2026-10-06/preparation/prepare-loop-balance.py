from pathlib import Path
import shutil,json,hashlib,re
lab=Path('F:/Projects/tyrax2-lab-20261001');src=lab/'tyrax2-city-day-drive-v1';dst=lab/'tyrax2-loop-balance-v1'
assert not dst.exists();dst.mkdir()
ignore=shutil.ignore_patterns('obj','__pycache__','.git','*.elf','*.sym','*.log','log.txt','closeout-samples.txt','ps2link.run')
shutil.copytree(src/'tyra',dst/'tyra',ignore=ignore)
shutil.copytree(src/'vehicle/game',dst/'vehicle/game',ignore=ignore)
def edit(rel,old,new,count=1):
 p=dst/rel;t=p.read_text(encoding="utf8");assert t.count(old)==count,(rel,old,t.count(old));p.write_bytes(t.replace(old,new).encode())
def include(rel):
 p=dst/rel;t=p.read_text(encoding="utf8");p.write_bytes(('#include "debug/loop_balance.hpp"\n'+t).encode())
eng='tyra/engine/'
include(eng+'src/engine.cpp')
edit(eng+'src/engine.cpp','game->loop();','LoopBalance::gameStart(); game->loop(); LoopBalance::gameEnd();')
include('vehicle/game/src/terrain_game.cpp')
edit('vehicle/game/src/terrain_game.cpp','  engine->renderer.beginFrame(CameraInfo3D(&cameraPosition, &cameraLookAt, &cameraUp));','  LoopBalance::renderStart();\n  engine->renderer.beginFrame(CameraInfo3D(&cameraPosition, &cameraLookAt, &cameraUp));')
edit(eng+'inc/debug/night_sampler.hpp','#include <stdint.h>','#include <stdint.h>\n#include "loop_balance.hpp"')
edit(eng+'inc/debug/night_sampler.hpp','if(readSample){start=now;stallStart=cumulativePacing;atStart=present;sampling=true;}','if(readSample){start=now;stallStart=cumulativePacing;atStart=present;sampling=true;LoopBalance::begin(index,now);}')
edit(eng+'inc/debug/night_sampler.hpp','if(offset==sampleFirst-1){primed=true;','if(offset==sampleFirst-1){LoopBalance::cancel();primed=true;')
edit(eng+'inc/debug/night_sampler.hpp','// Zero/multiple presentations','LoopBalance::finish(end,s.wholeLoop,s.pacing);\n // Zero/multiple presentations')
rc=eng+'src/renderer/core/renderer_core.cpp';include(rc)
edit(rc,'pipelineFramePending = false; // waits must not recursively present this job','pipelineFramePending = false; // waits must not recursively present this job\n  LoopBalance::completion(pipelineSequence,pipelineContext);')
edit(rc,'pipelineFramePending = true;','pipelineFramePending = true;\n    LoopBalance::submitted(pipelineSequence,pipelineContext,NightSampler::expectedIndex ? NightSampler::frame : 0xffffffffu);')
for k in ['Pipeline','Synchronous','Synthetic']:
 p=dst/rc;t=p.read_text(encoding="utf8");needle=next(l for l in t.splitlines()if 'NightSampler::onPresent(t1, NightSampler::PresentKind::'+k in l);edit(rc,needle,'  LoopBalance::pace(t0,t1);\n'+needle)
edit(rc,'    while (*vif1Stat & kVif1Busy) {\n    }','    { LoopBalance::Wait w(LoopBalance::Vif); while (*vif1Stat & kVif1Busy) {\n    } }')
sy=eng+'src/renderer/core/renderer_core_sync.cpp';include(sy)
edit(sy,'  while (!check()) {\n  }','  { LoopBalance::Wait w(LoopBalance::Gs); while (!check()) {\n  } }')
q=eng+'src/renderer/core/paths/path1/vif1_queue.cpp';include(q)
for loop in ['while (tail - head >= kDepth) advanceIfIdle();','while (tail - head >= kRingDepth) advanceIfIdle();','while (static_cast<s32>(completed - sequence) < 0) advanceIfIdle();','while (running) advanceIfIdle();','while (*stat & 0x1f000003) {}']:
 edit(q,loop,'{ LoopBalance::Wait w(LoopBalance::Vif); '+loop+' }')
edit(q,'dma_channel_wait(DMA_CHANNEL_VIF1, 0);','{ LoopBalance::Wait w(LoopBalance::Vif); dma_channel_wait(DMA_CHANNEL_VIF1, 0); }',3)
fs=eng+'inc/renderer/core/paths/path1/frame_submission.hpp';include(fs)
edit(fs,'if (!Vif1Queue::recordingFrame()) dma_channel_wait(channel, timeout);','if (!Vif1Queue::recordingFrame()) { LoopBalance::Wait w(channel==DMA_CHANNEL_VIF1?LoopBalance::Vif:LoopBalance::Gif); dma_channel_wait(channel, timeout); }')
edit(fs,'if (!Vif1Queue::recordingFrame()) draw_wait_finish();','if (!Vif1Queue::recordingFrame()) { LoopBalance::Wait w(LoopBalance::Gs); draw_wait_finish(); }')
edit(fs,'while (*stat & 0x1f000003) {}','{ LoopBalance::Wait w(LoopBalance::Vif); while (*stat & 0x1f000003) {} }')
edit(fs,'dma_channel_wait(channel, 0);','{ LoopBalance::Wait w(channel==DMA_CHANNEL_VIF1?LoopBalance::Vif:LoopBalance::Gif); dma_channel_wait(channel, 0); }',2)
two=eng+'src/renderer/core/2d/renderer_core_2d.cpp';include(two)
edit(two,'  while (*kVif1Stat & kVif1Busy) {\n  }','  { LoopBalance::Wait w(LoopBalance::Vif); while (*kVif1Stat & kVif1Busy) {\n  } }')
edit(eng+'inc/debug/closeout.hpp','  if(ferror(file))valid=false;','  if(mode(p))LoopBalance::write(file,p,i);\n  if(ferror(file))valid=false;')
edit(eng+'inc/debug/closeout.hpp','unsigned(complete())','unsigned(complete() && LoopBalance::valid)')
for name in ['prove-native.py','root-release.py','analyze-day-drive.py']:
 t=(src/name).read_text(encoding="utf8").replace('tyrax2-city-day-drive-v1','tyrax2-loop-balance-v1');(dst/name).write_bytes(t.encode())
print(dst)

