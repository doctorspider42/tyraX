// Injected inside MotionProbe, after its clock and timing definitions.
namespace RoadTrace {
enum Metric { VifWait, GsWait, OtherWait, WaitCalls, SubmitCalls, SubmitQwords,
  FlushCalls, FlushQwords, CapacityFlush, FallbackFlush, DrainCalls,
  BankWaitCalls, SequenceWaitCalls, BarrierFlush, ArenaRetry, Errors, MetricCount };
inline const char* metricNames="vifWait,gsWait,otherWait,waitCalls,submitCalls,submitQwords,flushCalls,flushQwords,capacityFlush,fallbackFlush,drainCalls,bankWaitCalls,sequenceWaitCalls,barrierFlush,arenaRetry,errors";
inline U metrics[MetricCount]{}, frameMetrics[1200][MetricCount]{}, view=0, count=0, depth=0;
struct Record { U offset,id,view,vertices,elapsed,wait,c[Count]; };
inline Record records[4096]{};
inline Record* currentRecord=nullptr;
inline FILE *framesFile=nullptr,*bagsFile=nullptr;
inline bool window() {
  if(offset<gasStart)return false;
  const U t=(offset-gasStart)%240;
  return (t>=12 && t<20) || (t>=24 && t<32) || (t>=180 && t<188);
}
inline void note(Counter c,U n) { if(currentRecord)currentRecord->c[c]+=n; }
inline void metric(Metric m,U n=1) { if(clocks)metrics[m]+=n; }
struct Camera { U old; explicit Camera(U v):old(view){view=v;} ~Camera(){view=old;} };
struct Bag {
  Record* row=nullptr;U start=0;
  Bag(bool selected,U id,U vertices) {
    if(!clocks || !selected || !window())return;
    if(currentRecord || !id || count==4096){metric(Errors);valid=false;return;}
    row=&records[count++];*row={offset,id,view,vertices,0,0,{}};
    currentRecord=row;start=ticks();
  }
  ~Bag(){if(row){row->elapsed=ticks()-start;currentRecord=nullptr;}}
};
// Leaf waits only. No vsync/presentation pacing is included here.
struct Wait {
  bool active;U start=0;Metric kind;
  explicit Wait(Metric k):active(clocks),kind(k){
    if(active){if(depth++)metric(Errors);metric(WaitCalls);start=ticks();}
  }
  ~Wait(){if(active){U dt=ticks()-start;metrics[kind]+=dt;if(currentRecord)currentRecord->wait+=dt;--depth;}}
};
inline void open() {
  framesFile=fopen("host:road-frames.csv","w");bagsFile=fopen("host:road-bags.csv","w");
  if(!framesFile || !bagsFile){valid=false;return;}
  fprintf(framesFile,"phase,offset,%s\n",metricNames);
  fprintf(bagsFile,"phase,offset,id,view,vertices,elapsed,wait");
  for(U i=0;i<Count;++i)fprintf(bagsFile,",c%u",i);
  fprintf(bagsFile,"\n");
}
inline void begin(){memset(metrics,0,sizeof(metrics));}
inline void save(U index){
  if(depth || currentRecord || index>=1200){metric(Errors);valid=false;return;}
  memcpy(frameMetrics[index],metrics,sizeof(metrics));if(metrics[Errors])valid=false;
}
inline void dump(){
  if(framesFile && bagsFile){
    for(U i=0;i<size;++i){fprintf(framesFile,"%u,%u",phase,rows[i].offset);
      for(U v:frameMetrics[i])fprintf(framesFile,",%u",v);fprintf(framesFile,"\n");}
    for(U i=0;i<count;++i){const auto& r=records[i];
      fprintf(bagsFile,"%u,%u,%u,%u,%u,%u,%u",phase,r.offset,r.id,r.view,r.vertices,r.elapsed,r.wait);
      for(U v:r.c)fprintf(bagsFile,",%u",v);fprintf(bagsFile,"\n");}
    if(fflush(framesFile) || fflush(bagsFile) || ferror(framesFile) || ferror(bagsFile))valid=false;
  }else valid=false;
  count=0;
  if(phase+1==phaseCount){
    if(framesFile && fclose(framesFile))valid=false;
    if(bagsFile && fclose(bagsFile))valid=false;
    framesFile=bagsFile=nullptr;
  }
}
}
