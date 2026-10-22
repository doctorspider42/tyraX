"""Extract actual A39 Core/queue methods; model hardware flags, not target timing."""
import argparse,hashlib,json,subprocess
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__);p.add_argument('fixture',type=Path);p.add_argument('out',type=Path)
a=p.parse_args();a.out.mkdir(parents=True,exist_ok=False)
engine=a.fixture/'tyra/engine';sha=lambda f:hashlib.sha256(f.read_bytes()).hexdigest()
queue=engine/'src/renderer/core/paths/path1/vif1_queue.cpp';core=engine/'src/renderer/core/renderer_core.cpp'
def section(file,start,end):
 s=file.read_text();at=s.index(start);return s[at:s.index(end,at)]
methods=section(queue,'bool Vif1Queue::completeReadyPresentation','void Vif1Queue::waitFor')+'\n'+section(core,'void RendererCore::tryReadyPresent','void RendererCore::beginFrameRecording')
guard=engine/'inc/debug/ready_guard.hpp'
prefix=r'''
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include "ready_guard.hpp"
#define TYRA_FRAME_PIPELINE_SUPPORT 1
#define TYRA_ASSERT(x,...) assert(x)
using u32=uint32_t;using s32=int32_t;using u8=uint8_t;
namespace ReadyTrial {bool enabled;unsigned checks,presented,protectedPrefixes;}
namespace Tyra {
struct Cursor {u32 position=17;u32 checkpoint(){return position;}u32 used(){return position;}};
Cursor w,a;Cursor* nativeWriter=&w;Cursor* chainArena=&a;
u32 nativePendingSequence=0,completed=0,currentBank=1,hardwareCompleted=0;
bool frameRecording=true,protectRecording=false,path3FencePending=true;
void(*beforeFrameSubmit)(void*)=nullptr;void* beforeFrameUser=nullptr;
unsigned polls=0,callbacks=0,flushes=0,resets=0;
void advanceIfIdle(){++polls;completed=hardwareCompleted;}
struct Vif1Queue {static bool completeReadyPresentation(u32);};
struct Settings {bool hybrid=true,field=false;bool isHybridOutput(){return hybrid;}bool isFieldRendering(){return field;}};
struct GS {bool redirect=false,free=true;u8 context=1;bool isRasterRedirected(){return redirect;}bool readyHybridPresent(){return free;}u8 getRecordingContext(){return context;}void setRecordingContext(u8 c){context=c;}};
struct Sync {bool finish=true;bool check(){return finish;}};
struct BLSS {bool enabled=false;bool isEnabled(){return enabled;}};
struct RendererCore {Settings settings;GS gs;Sync sync;BLSS blss;
 bool pipelineFramePending=true,pipelineFrameActive=true;u32 pipelineSequence=42;void tryReadyPresent();};
void present(void* p){
 auto& c=*static_cast<RendererCore*>(p);
 ++callbacks;if(nativePendingSequence)++flushes;
 if(!protectRecording)++resets;
 assert(!frameRecording && protectRecording && !path3FencePending && !nativePendingSequence);
 c.pipelineFramePending=false;c.gs.context=0;
}
'''
suffix=r'''
}
int main(){using namespace Tyra;unsigned tests=0;
 for(unsigned mask=0;mask<1024;++mask)for(u32 sequence:{1u,42u,0xfffffff0u,0xffffffffu}){
  RendererCore c;ReadyTrial::enabled=mask&1;c.pipelineFramePending=mask&2;
  c.pipelineFrameActive=mask&4;c.settings.hybrid=mask&8;c.settings.field=mask&16;
  c.blss.enabled=mask&32;c.gs.redirect=mask&64;c.gs.free=mask&128;
  c.sync.finish=mask&256;bool dmaReady=mask&512;c.pipelineSequence=sequence;
  nativePendingSequence=sequence+101;completed=sequence-1;
  hardwareCompleted=dmaReady?sequence:sequence-1;
  frameRecording=true;protectRecording=false;path3FencePending=true;
  beforeFrameSubmit=present;beforeFrameUser=&c;
  callbacks=flushes=resets=polls=0;ReadyTrial::checks=ReadyTrial::presented=ReadyTrial::protectedPrefixes=0;
  const u32 reserved=nativePendingSequence;
  bool expected=ReadyTrial::enabled && c.pipelineFramePending && c.pipelineFrameActive && c.settings.hybrid &&
    !c.settings.field && !c.blss.enabled && !c.gs.redirect && c.gs.free && c.sync.finish && dmaReady;
  c.tryReadyPresent();assert(callbacks==unsigned(expected));
  assert(ReadyTrial::presented==unsigned(expected) && ReadyTrial::protectedPrefixes==unsigned(expected));
  assert(!flushes && !resets && w.position==17 && a.position==17 && currentBank==1);
  assert(nativePendingSequence==reserved && frameRecording && !protectRecording && path3FencePending && c.gs.context==1);
  ++tests;
 }
 beforeFrameSubmit=present;beforeFrameUser=nullptr;callbacks=0;protectRecording=false;
 assert(!Vif1Queue::completeReadyPresentation(0) && !callbacks);++tests;
 protectRecording=true;assert(!Vif1Queue::completeReadyPresentation(42) && !callbacks);++tests;
 protectRecording=false;beforeFrameSubmit=nullptr;
 assert(!Vif1Queue::completeReadyPresentation(42) && !callbacks);++tests;
 std::printf("READYRUNTIME %u actual-method controls pass (modeled hardware)\n",tests);
}
'''
(a.out/'ready_guard.hpp').write_bytes(guard.read_bytes())
file=a.out/'runtime.cpp';file.write_text(prefix+methods+suffix)
for name,flags in [('optimized',['-O2']),('sanitized',['-O1','-g','-fsanitize=address,undefined'])]:
 exe=a.out/name
 subprocess.run(['g++','-std=c++17','-Wall','-Wextra',*flags,str(file),'-o',str(exe)],check=True)
 result=subprocess.run([str(exe)],check=True,capture_output=True,text=True)
 (a.out/f'{name}.log').write_text(result.stdout+result.stderr)
 assert 'READYRUNTIME 4099 actual-method controls pass' in result.stdout
 print(result.stdout,end='')
(a.out/'source-pins.json').write_text(json.dumps({str(f.relative_to(a.fixture)):sha(f) for f in (queue,core,guard)},indent=2)+'\n')
