from pathlib import Path
import hashlib,json
b=Path('F:/Projects/tyrax2-lab-20261001');f=b/'light-pick-far-physical-v1';out=b/'light-pick-far-host-v1';assert not out.exists();out.mkdir();s=(f/'tyra/engine/src/renderer/core/renderer_core.cpp').read_text(encoding='utf8');start=s.index('const RendererCoreSpotLight* RendererCore::pickDynLight(');end=s.index('\nvoid RendererCore::beginFrame()',start);method=s[start:end];p=out/'probe.cpp'
p.write_bytes(('''#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include "debug/night_ablation.hpp"
struct Vec4 {float x=0,y=0,z=0,w=1;};struct Color {float r=0,g=0,b=0,a=0;};
struct RendererCoreSpotLight {Vec4 position,direction;Color color;float range=0,cosCutoff=0;bool enabled=false,point=false;};
struct Math {static float sqrtNonNegative(float x){return std::sqrt(x);}};
struct RendererCore {static constexpr unsigned DYN_LIGHTS_MAX=8;RendererCoreSpotLight spot,dynLights[8];unsigned dynLightCount=0;const RendererCoreSpotLight* pickDynLight(const Vec4&,const float&,int)const;};
'''+method+'''
uint32_t seed=0x52fda381;uint32_t random32(){seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;return seed;}
float raw(uint32_t x){float f;memcpy(&f,&x,4);return f;}
float real(){return (int32_t(random32()%2000001)-1000000)/1000.f;}
int main(){uint64_t selected=0,rejects=0,gateCases=0;
for(unsigned i=0;i<2000000;++i){RendererCore c;Vec4 center;center.x=real();center.y=real();center.z=real();float radius=std::fabs(real())*.1f;int skip=int(random32()%12)-2;c.dynLightCount=random32()%9;
for(unsigned j=0;j<=c.dynLightCount;++j){auto& l=j?c.dynLights[j-1]:c.spot;l.enabled=random32()&1;l.point=random32()&1;l.position.x=real();l.position.y=real();l.position.z=real();l.direction.x=real()*.001f;l.direction.y=real()*.001f;l.direction.z=real()*.001f;l.range=std::fabs(real())*.2f;l.color.r=real();l.color.g=real();l.color.b=real();l.cosCutoff=real()*.001f;
 if(i%13==0&&j)l=c.spot;
 if(i%29==0)l.range=std::nextafter(std::sqrt((l.position.x-center.x)*(l.position.x-center.x)+(l.position.y-center.y)*(l.position.y-center.y)+(l.position.z-center.z)*(l.position.z-center.z))-radius,(i&1)?INFINITY:-INFINITY);
 if(i%31==0){l.range=raw(random32());radius=raw(random32());l.position.x=raw(random32());}
}
NightAblation::farPickEnabled=false;const auto* a=c.pickDynLight(center,radius,skip);NightAblation::farPickEnabled=true;const auto* z=c.pickDynLight(center,radius,skip);if(a!=z){printf("MISMATCH %u\\n",i);return 1;}++selected;
}
for(unsigned i=0;i<20000000;++i){float x=raw(random32()),y=raw(random32()),z=raw(random32()),range=raw(random32()),radius=raw(random32());++gateCases;if(!NightAblation::farLightCandidate(x,y,z,range,radius))continue;++rejects;float d=Math::sqrtNonNegative(x*x+y*y+z*z)-radius;if(d<0)d=0;if(!(d>=range)){printf("GATE_MISMATCH %u\\n",i);return 2;}}
printf("PASS selected=%llu gateCases=%llu rejected=%llu\\n",(unsigned long long)selected,(unsigned long long)gateCases,(unsigned long long)rejects);
}
''').encode());(out/'input-pins.json').write_bytes((json.dumps(dict(rendererSha256=hashlib.sha256((f/'tyra/engine/src/renderer/core/renderer_core.cpp').read_bytes()).hexdigest(),headerSha256=hashlib.sha256((f/'tyra/engine/inc/debug/night_ablation.hpp').read_bytes()).hexdigest(),probeSha256=hashlib.sha256(p.read_bytes()).hexdigest(),hostLimits='Actual selection body; host sqrt is not R5900 sqrt. Actual target cold pointer comparison remains required.'),indent=2)+'\n').encode())
