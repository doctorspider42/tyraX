// Private external PC sampler. This TU never includes the measured physics TU.
#include <kernel.h>
#include <timer.h>
#include <cstdio>
#include <cstdint>

namespace PcSample {
constexpr unsigned Capacity=65536,Period=149381;
struct Record {unsigned pc,frame,scheduled,actual;};
alignas(64) static Record records[Capacity];
static volatile unsigned count=0,lost=0,callbacks=0;
static volatile int currentFrame=0;
static volatile bool active=false;
static bool enabled=false;
static int timer=-1;
static unsigned ticks(){unsigned value;asm volatile("mfc0 %0,$9":"=r"(value)::"memory");return value;}

// Only integer loads/stores/arithmetic are allowed in this interrupt callback.
static u64 sample(s32,u64 scheduled,u64 actual,void*,void* pc){
  callbacks=callbacks+1;
  if(active){
    const unsigned index=count;
    if(index<Capacity){
      records[index].pc=unsigned(reinterpret_cast<uintptr_t>(pc));
      records[index].frame=unsigned(currentFrame);
      records[index].scheduled=unsigned(scheduled);records[index].actual=unsigned(actual);
      count=index+1;
    } else lost=lost+1;
  }
  return Period;
}

__attribute__((noinline)) void calibrate(){
  const unsigned start=ticks();
  while(unsigned(ticks()-start)<29491200u)asm volatile("":::"memory");
}

static bool flush(int phase){
  active=false;asm volatile("":::"memory");
  const unsigned header[]={0x50435331u,1u,unsigned(phase),unsigned(enabled),count,lost,Period,sizeof(Record),callbacks,
    unsigned(reinterpret_cast<uintptr_t>(&calibrate))};
  char name[80];
  if(phase<0)snprintf(name,sizeof(name),"pc-sample-startup.bin");
  else snprintf(name,sizeof(name),"pc-sample-%d.bin",phase);
  FILE* f=fopen(name,"wb");
  const bool ok=f&&fwrite(header,sizeof(header),1,f)==1&&fwrite(records,sizeof(Record),header[4],f)==header[4];
  if(f)fclose(f);
  printf("PCSAMPLE phase=%d enabled=%u count=%u lost=%u period=%u file=%u\n",phase,header[3],header[4],header[5],Period,unsigned(ok));fflush(stdout);
  count=0;lost=0;callbacks=0;return ok;
}
}

extern "C" bool pcSampleConfigure(){
  using namespace PcSample;
  FILE* f=fopen("pc-sample.cfg","r");enabled=f!=nullptr;if(f)fclose(f);
  // This strong reference also links the SDK timer initializer used by crt0.
  const u64 initialized=GetTimerSystemTime();
  timer=AllocTimerCounter();
  if(timer<0||!initialized){printf("PCSAMPLE ERROR timer=%d initialized=%u\n",timer,unsigned(initialized!=0));return false;}
  if(StartTimerCounter(timer)<0)return false;
  if(enabled&&SetTimerHandler(timer,GetTimerCount(timer)+Period,sample,nullptr)<0)return false;
  currentFrame=0;active=enabled;calibrate();active=false;
  const bool calibrated=!enabled||count>=20;
  const bool saved=flush(-1);
  printf("PCSAMPLE startup accepted=%u calibration=0x%08x\n",unsigned(calibrated&&saved),unsigned(reinterpret_cast<uintptr_t>(&calibrate)));fflush(stdout);
  return calibrated&&saved;
}
extern "C" void pcSampleBegin(int,int frame,bool ready){
  PcSample::currentFrame=frame;asm volatile("":::"memory");
  PcSample::active=PcSample::enabled&&ready&&frame>=0&&frame<750;
}
extern "C" void pcSampleEnd(){PcSample::active=false;asm volatile("":::"memory");}
extern "C" void pcSampleFlush(int phase){
  PcSample::flush(phase);
  if(phase==13&&PcSample::timer>=0){StopTimerCounter(PcSample::timer);FreeTimerCounter(PcSample::timer);PcSample::timer=-1;}
}
