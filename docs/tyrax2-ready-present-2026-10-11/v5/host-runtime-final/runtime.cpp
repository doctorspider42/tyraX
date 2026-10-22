
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
bool Vif1Queue::completeReadyPresentation(u32 sequence) {
#if TYRA_FRAME_PIPELINE_SUPPORT
  if(!sequence || protectRecording || !beforeFrameSubmit)return false;
  advanceIfIdle();
  if(static_cast<s32>(completed-sequence)<0)return false;
  const auto writer=nativeWriter;const u32 mark=writer?writer->checkpoint():0;
  const auto arena=chainArena;const u32 used=arena?arena->used():0;
  const u32 bank=currentBank;const bool hadPrefix=nativePendingSequence!=0;
  {
    ReadyRecordingSuspend suspended(frameRecording,protectRecording,path3FencePending,nativePendingSequence);
    beforeFrameSubmit(beforeFrameUser);
  }
  TYRA_ASSERT(nativeWriter==writer && (!writer || writer->checkpoint()==mark) &&
              chainArena==arena && (!arena || arena->used()==used) && currentBank==bank,
              "A39 current prefix mutated during old presentation");
  if(hadPrefix)++ReadyTrial::protectedPrefixes;
  return true;
#else
  (void)sequence;return false;
#endif
}


void RendererCore::tryReadyPresent(){
  if(!ReadyTrial::enabled || !pipelineFramePending)return;
  ++ReadyTrial::checks;
  if(!pipelineFrameActive || !settings.isHybridOutput() || settings.isFieldRendering() ||
     blss.isEnabled() || gs.isRasterRedirected() || !gs.readyHybridPresent() || !sync.check())return;
  const u8 context=gs.getRecordingContext();
  if(Vif1Queue::completeReadyPresentation(pipelineSequence)){
    TYRA_ASSERT(!pipelineFramePending,"A39 old job not consumed");++ReadyTrial::presented;
  }
  gs.setRecordingContext(context);
}


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
