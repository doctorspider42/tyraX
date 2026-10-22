from pathlib import Path
import hashlib,json,re
b=Path('F:/Projects/tyrax2-lab-20261001');f=b/'pmu-scene-physical-v2';out=b/'pmu-scene-source-controls-v2';assert not out.exists();out.mkdir();sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();p=f/'tyra/engine/inc/debug/night_ablation.hpp';s=p.read_text();s=s[s.index('// UNRELEASED private PMU source draft.'):].replace('#include "debug/night_sampler.hpp"','');assert not re.search(r'\b(?:mtc0|dmtc0)\b',s)
prefix='''#include <stdint.h>
#include <vector>
#include <cstdio>
#define PRIVATE_PMU_HOST 1
namespace NightAblation {inline bool valid=true;}
namespace NightSampler {inline uint32_t frame=0;}
'''
suffix='''
namespace NightPMU {inline U fakePcr=0,fakeI=100,fakeD=200,configureWrites=0,resetWrites=0,enableWrites=0,stopWrites=0;
 inline bool resetWhileEnabled=false;inline std::vector<char> trace;
 U hostReadPcr(){trace.push_back('P');return fakePcr;}
 U hostReadI(){trace.push_back('I');return fakeI++;}
 U hostReadD(){trace.push_back('D');return fakeD++;}
 void hostConfigureStopped(){++configureWrites;trace.push_back('C');fakePcr=stoppedPcr;}
 void hostResetEnable(){++resetWrites;++enableWrites;trace.push_back('R');if(fakePcr&0x80000000U)resetWhileEnabled=true;fakeI=fakeD=0;fakePcr=activePcr;trace.push_back('E');}
 void hostStopOwned(){++stopWrites;trace.push_back('S');fakePcr=stoppedPcr;}
}
static void reset(){using namespace NightPMU;selected=true;enabled=inWindow=active=knownStopped=cleanupVerified=false;phase=frameCalls=phaseCalls=phaseReads=rowCount=commonControlReads=resets=enables=stops=beforePcr=preSetupPcr=setupPcr=endPcr=setupCount=activeFrame=activeScene=cleanupPcr=0;fakePcr=configureWrites=resetWrites=enableWrites=stopWrites=0;fakeI=100;fakeD=200;resetWhileEnabled=false;trace.clear();NightAblation::valid=true;}
static void initialize(bool on){NightPMU::begin(0,on);NightPMU::begin(750,on);NightPMU::begin(799,on);}
int main(){using namespace NightPMU;int owner=0;
 for(bool on:{false,true}){reset();initialize(on);if(!NightAblation::valid||fakePcr!=stoppedPcr||configureWrites!=1||resetWrites||enableWrites||stopWrites)return 10;
  trace.clear();for(U i=0;i<128;++i){begin(900+i,on);NightSampler::frame=900+i;{SceneScope a(owner);fakeI+=7;fakeD+=11;}endFrame();if(fakePcr!=stoppedPcr||active||!knownStopped)return 11;}
  if(!NightAblation::valid||phaseCalls!=128||commonControlReads!=384||resets!=128||enables!=128||stops!=128||phaseReads!=(on?512U:0U)||rowCount!=(on?128U:0U)||resetWhileEnabled)return 12;
  std::vector<char> single=on?std::vector<char>({'P','R','E','I','D','I','D','P','S','P'}):std::vector<char>({'P','R','E','P','S','P'});if(trace.size()!=128*single.size())return 13;for(U i=0;i<128;++i)for(U j=0;j<single.size();++j)if(trace[i*single.size()+j]!=single[j])return 14;
  begin(1156,on);cleanup();if(!NightAblation::valid||!cleanupVerified||fakePcr!=stoppedPcr||stopWrites!=128)return 15;
 }
 reset();initialize(true);begin(900,true);NightSampler::frame=900;auto early=[&](){SceneScope a(owner);return;};early();endFrame();if(!NightAblation::valid||stops!=1||active||fakePcr!=stoppedPcr)return 16;
 // Unknown active setup and entry owners are never reset/stopped/reconfigured.
 for(U prior:{0x80000001U,activePcr}){reset();fakePcr=prior;initialize(true);if(NightAblation::valid||configureWrites||resetWrites||enableWrites||stopWrites||fakePcr!=prior)return 20;}
 for(U prior:{0x80000001U,activePcr,0x00000001U}){reset();initialize(true);fakePcr=prior;begin(900,true);NightSampler::frame=900;{SceneScope a(owner);}endFrame();if(NightAblation::valid||resetWrites||enableWrites||stopWrites||fakePcr!=prior)return 21;}
 // Unknown owner at pre-stop receives no stop write; primary lifetime safety
 // remains unresolved in this failure path, and this host case is not safety proof.
 reset();initialize(true);begin(900,true);NightSampler::frame=900;{SceneScope a(owner);fakePcr=0x80000001U;}endFrame();if(NightAblation::valid||stopWrites||fakePcr!=0x80000001U)return 22;
 reset();initialize(true);begin(900,true);NightSampler::frame=900;{SceneScope a(owner);SceneScope nested(owner);}endFrame();if(NightAblation::valid||resetWrites!=1||stopWrites!=1)return 23;
 reset();initialize(true);begin(900,true);NightSampler::frame=900;{SceneScope a(owner);owner=1;}owner=0;endFrame();if(NightAblation::valid||stopWrites!=1||fakePcr!=stoppedPcr)return 24;
 reset();initialize(true);begin(900,true);NightSampler::frame=900;{SceneScope a(owner);NightSampler::frame=901;}endFrame();if(NightAblation::valid||stopWrites!=1||fakePcr!=stoppedPcr)return 25;
 reset();initialize(true);begin(900,true);endFrame();if(NightAblation::valid||resetWrites||stopWrites)return 26;
 reset();initialize(true);begin(900,true);NightSampler::frame=900;{SceneScope a(owner);fakeI=0x80000000U;}endFrame();if(NightAblation::valid||stopWrites!=1)return 27;
 reset();initialize(true);fakePcr=activePcr;begin(1156,true);if(NightAblation::valid||stopWrites||fakePcr!=activePcr)return 28;
 reset();initialize(true);fakePcr=0x80000001U;cleanup();if(NightAblation::valid||stopWrites||fakePcr!=0x80000001U)return 29;
 if(resetWhileEnabled)return 30;
 std::puts("PASS_HOST_PMU_V2_ORDER_GUARDS_ONLY_NO_RUNTIME_SAFETY_CLAIM");return 0;}
'''
code=prefix+s+suffix;(out/'pmu.cpp').write_bytes(code.encode());(out/'preparation.json').write_bytes((json.dumps(dict(status='PREPARED_NOT_COMPILED_OR_EXECUTED',sourceSha256=sha(p),harnessSha256=hashlib.sha256(code.encode()).hexdigest(),hostWrappersOnly=True,nativeOpcodeOrRuntimeQualified=False,enabledLifetimeQualified=False,primaryOverflowReportSha256=sha(b/'pmu-overflow-review-v1/report.md')),indent=2)+'\n').encode())
sourcehop=dict(status='PASS_PMU_V2_SOURCE_HOP_STATIC_ONLY',sourceManifestSha256=sha(f/'target-source-manifest.json'),sourceFiles=501,pmuNoCountWrite=True,productionCoreRestored=False,productionQbufferRestored=False,primaryOverflowReportPin={'path':str(b/'pmu-overflow-review-v1/report.md'),'sha256':sha(b/'pmu-overflow-review-v1/report.md')},runtimeReleaseAccepted=False)
for name,key in [('stapip_core.cpp','productionCoreRestored'),('stapip_qbuffer_renderer.cpp','productionQbufferRestored')]:
 rel='tyra/engine/src/renderer/3d/pipeline/static/core/'+name;assert sha(f/rel)==sha(Path('F:/Projects/tyra-editor/vendor')/rel);sourcehop[key]=True
assert s.index('if(!knownStopped||actualPcr!=stoppedPcr)')<s.index('resetEnable();++resets')
assert s.index('if(!pmuOwned||actualPcr!=activePcr)')<s.index('stopOwned();++stops')
(out/'source-hop-static.json').write_bytes((json.dumps(sourcehop,indent=2)+'\n').encode());print('PMU_V2_SOURCE_HOST_HARNESS_PREPARED_STATIC_HOP_PASS')
