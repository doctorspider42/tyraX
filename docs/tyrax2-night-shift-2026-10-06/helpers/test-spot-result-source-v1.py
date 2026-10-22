from pathlib import Path
import subprocess,json,hashlib
b=Path('/mnt/f/Projects/tyrax2-lab-20261001');f=b/'spot-result-physical-v1';out=b/'spot-result-source-controls-v1';assert not out.exists();out.mkdir();p=f/'tyra/engine/src/renderer/3d/pipeline/static/core/stapip_qbuffer_renderer.cpp';s=p.read_text();start=s.index('void invertAffine(');end=s.index('}  // namespace',start);body=s[start:end]
stub='''#include <cmath>
#include <cstring>
#include <cassert>
#include <cstdio>
#include <cstdint>
struct Vec4 {float x=0,y=0,z=0,w=1;};
struct M4x4 {float data[16];};
struct Color {float r=0,g=0,b=0;};
struct RendererCoreSpotLight {bool enabled=false,point=false;Vec4 position,direction;Color color;float range=1,cosCutoff=.8F,softness=1;};
struct StaPipClipperSpot {bool enabled=false;Vec4 position,direction;float color[3]={};float invRange2=0,cosCut2=0,invSoft=0;};
namespace Math {float sqrtNonNegative(float x){return std::sqrt(x);}}
namespace NightAblation {bool producerEnabled=true,producerSelected=true,collectCounters=true,valid=true;unsigned cold[5][5]={};void producerCount(unsigned s,unsigned k,unsigned n=1){cold[s][k]+=n;}}
'''
tests='''
unsigned rng=0x12345678;unsigned next(){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return rng;}
float val(){return int(next()%20001)-10000.0F;}
int main(){
 unsigned compared=0,mutations=0,nonfinite=0;
 for(unsigned i=0;i<100000;++i){
  M4x4 m{};m.data[0]=.5F+(next()%100)/30.0F;m.data[5]=.5F+(next()%100)/20.0F;m.data[10]=.5F+(next()%100)/40.0F;m.data[15]=1;
  m.data[1]=(next()%20)*.01F;m.data[4]=(next()%20)*-.02F;m.data[12]=val();m.data[13]=val();m.data[14]=val();
  RendererCoreSpotLight l{};l.enabled=i%17!=0;l.point=i%2;l.range=1+(next()%100);l.position.x=val();l.position.y=val();l.position.z=val();l.direction.x=.3F;l.direction.y=.8F;l.direction.z=.4F;l.color.r=next()%128;l.color.g=next()%128;l.color.b=next()%128;l.cosCutoff=.7F;l.softness=.4F;
  for(unsigned repeat=0;repeat<3;++repeat){auto a=buildSpotForBag(l,&m),r=buildSpotForBagOriginal(l,&m);assert(spotResultDifference(a,r)==0);++compared;}
  for(unsigned k=0;k<28;++k){
   float* fields[]={&l.position.x,&l.position.y,&l.position.z,&l.direction.x,&l.direction.y,&l.direction.z,&l.color.r,&l.color.g,&l.color.b,&l.range,&l.cosCutoff,&l.softness};float* ptr=k<16?m.data+k:fields[k-16];unsigned old;memcpy(&old,ptr,4);unsigned changed=old^1u;memcpy(ptr,&changed,4);
   auto a=buildSpotForBag(l,&m),r=buildSpotForBagOriginal(l,&m);assert(spotResultDifference(a,r)==0);memcpy(ptr,&old,4);++mutations;
  }
  l.point=!l.point;assert(spotResultDifference(buildSpotForBag(l,&m),buildSpotForBagOriginal(l,&m))==0);++mutations;
  if(i%100==0){unsigned special[]={0x80000000u,0x7f800000u,0xff800000u,0x7fc12345u};for(auto bits:special){memcpy(&m.data[3],&bits,4);assert(spotResultDifference(buildSpotForBag(l,&m),buildSpotForBagOriginal(l,&m))==0);++nonfinite;}}
  if(i%1000==0)for(auto& e:spotResults)e.valid=false;
 }
 assert(NightAblation::valid&&NightAblation::cold[0][2]>0&&NightAblation::cold[0][4]==0);
 printf("PASS_ACTUAL_SOURCE_HOST_ROUTING compared=%u mutations=%u nonfiniteOrSignedZero=%u reuse=%u\\n",compared,mutations,nonfinite,NightAblation::cold[0][2]);
}
'''
cpp=out/'actual-source.cpp';cpp.write_bytes((stub+body+tests).encode());r=subprocess.run(['g++','-std=c++17','-O2',str(cpp),'-o',str(out/'check')],stdout=subprocess.PIPE,stderr=subprocess.STDOUT);(out/'compile.log').write_bytes(r.stdout);assert r.returncode==0,r.stdout.decode();r=subprocess.run([str(out/'check')],stdout=subprocess.PIPE,stderr=subprocess.STDOUT);(out/'run.log').write_bytes(r.stdout);assert r.returncode==0,r.stdout.decode();sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();proof=dict(status='PASS_ACTUAL_SOURCE_HOST_ONLY_KEY_MUTATION_PARITY',sourceSha256=sha(p),generatedHarnessSha256=sha(cpp),logSha256=sha(out/'run.log'),hostOnly=True,physicalParityNotEstablished=True);(out/'proof.json').write_text(json.dumps(proof,indent=2)+'\n');print(r.stdout.decode())
