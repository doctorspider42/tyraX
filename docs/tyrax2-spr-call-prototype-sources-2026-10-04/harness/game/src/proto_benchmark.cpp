// PRIVATE benchmark: root must merge candidate APIs before native compilation.
#include "debug/quiet_runtime.hpp"
#include "renderer/core/paths/path1/vif1_queue.hpp"
namespace QuietRuntime {
static unsigned protoMode=0,protoGuard=1;
static Tyra::SprPrefixStats sprStart{};
static decltype(Tyra::Vif1Queue::experimentalCallStats()) callStart{};
static bool protoValid=false,statsPrimed=false;
static bool readMode(const char* path,unsigned& out){
 FILE* f=fopen(path,"r");if(!f)return false;int c;do{c=fgetc(f);}while(c!=EOF&&isspace(static_cast<unsigned char>(c)));
 bool ok=c=='0'||c=='1'||c=='2'||c=='4'||c=='6';out=ok?unsigned(c-'0'):0;
 while((c=fgetc(f))!=EOF)if(!isspace(static_cast<unsigned char>(c)))ok=false;
 if(ferror(f))ok=false;
 fclose(f);return ok;
}
void protoInit(){protoValid=readMode("host:proto-mode.cfg",protoMode)&&readFlag("host:proto-guard.cfg",protoGuard);QuietCadence::valid=QuietCadence::valid&&protoValid;}
void protoBeforeLoop(unsigned index){
 const unsigned phase=index/1800,offset=index%1800;if(phase>=3||offset!=0)return;
 const bool arm=(phase==1)!=(QuietCadence::order==1);const unsigned actual=arm?protoMode:0;
 // Merged root setter MUST return strict bool success, drain changed state,
 // and apply CALL enable; no fallback to a silently failed requested arm.
 const bool applied=Tyra::Vif1Queue::setExperimentalReplayMode(actual,true,protoGuard!=0);
 QuietCadence::valid=QuietCadence::valid&&applied;statsPrimed=false;
 printf("LOG: PROTOMODE phase=%u offset=0 selected=%u actual=%u arm=%u sampler=1 guard=%u reserve=1 applied=%u\n",phase,protoMode,actual,arm?1u:0u,protoGuard,applied?1u:0u);fflush(stdout);
}
static Tyra::SprPrefixStats sprFinish{};
static decltype(Tyra::Vif1Queue::experimentalCallStats()) callFinish{};
void protoAfterLoop(unsigned index){
 const unsigned phase=index/1800,offset=index%1800;if(phase>=3)return;
 if(offset==799){sprStart=Tyra::Vif1Queue::experimentalSprStatistics();callStart=Tyra::Vif1Queue::experimentalCallStats();statsPrimed=true;}
 if(offset==1119){sprFinish=Tyra::Vif1Queue::experimentalSprStatistics();callFinish=Tyra::Vif1Queue::experimentalCallStats();QuietCadence::valid=QuietCadence::valid&&statsPrimed;}
 if(offset!=1155)return;
#define D(S,F,K) unsigned(F.K-S.K)
 printf("LOG: PROTOSTATS phase=%u offset=1155 first=800 frames=320 primed=%u prefixes=%u qwords=%u chunks=%u completions=%u refused=%u guardFailures=%u nativeCommands=%u calls=%u hits=%u builds=%u fallbacks=%u singleRefCalls=%u multiRecordCalls=%u avoidedNativeRecords=%u targetBytes=%u replayedRecords=%u\n",phase,statsPrimed?1u:0u,D(sprStart,sprFinish,prefixes),D(sprStart,sprFinish,qwords),D(sprStart,sprFinish,chunks),D(sprStart,sprFinish,completions),D(sprStart,sprFinish,refused),D(sprStart,sprFinish,guardFailures),D(callStart,callFinish,nativeCommands),D(callStart,callFinish,calls),D(callStart,callFinish,hits),D(callStart,callFinish,builds),D(callStart,callFinish,fallbacks),D(callStart,callFinish,singleRefCalls),D(callStart,callFinish,multiRecordCalls),D(callStart,callFinish,avoidedNativeRecords),D(callStart,callFinish,targetBytes),D(callStart,callFinish,replayedRecords));fflush(stdout);
#undef D
}
}
