#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdint>
using u32=uint32_t;
#include "debug/night_ablation.hpp"
struct Vec4 {float x=0,y=0,z=0,w=1;};struct Color {float r=0,g=0,b=0,a=0;};
struct RendererCoreSpotLight {Vec4 position,direction;Color color;float range=0,cosCutoff=0;bool enabled=false,point=false;};
struct Math {static float sqrtNonNegative(float x){return std::sqrt(x);}};
struct RendererCore {static constexpr unsigned DYN_LIGHTS_MAX=8;RendererCoreSpotLight spot,dynLights[8];unsigned dynLightCount=0;const RendererCoreSpotLight* pickDynLight(const Vec4&,const float&,int)const;};
const RendererCoreSpotLight* RendererCore::pickDynLight(
    const Vec4& worldCenter, const float& worldRadius, int skipSlot) const {
  const auto choose=[&](bool fast)->const RendererCoreSpotLight* {
  // Score = luminance * quadratic falloff at the sphere's NEAREST point, so
  // a big mesh near a torch competes fairly with the camera flashlight.
  const RendererCoreSpotLight* best = &spot;
  float bestScore = -1.0F;

  const RendererCoreSpotLight* candidates[DYN_LIGHTS_MAX + 1];
  u32 count = 0;
  if (spot.enabled) candidates[count++] = &spot;
  for (u32 i = 0; i < dynLightCount; i++) {
    // Modified by TyraX: the bag's named exclusion (its light is drawn
    // projected, per pixel, by the game's shadow pass instead).
    if ((int)i == skipSlot) continue;
    candidates[count++] = &dynLights[i];
  }

  for (u32 i = 0; i < count; i++) {
    const auto* l = candidates[i];
    const float dx = l->position.x - worldCenter.x;
    const float dy = l->position.y - worldCenter.y;
    const float dz = l->position.z - worldCenter.z;
    if(fast && NightAblation::collectCounters)++NightAblation::farPickCounter.candidates;
    if(fast && NightAblation::farLightCandidate(dx,dy,dz,l->range,worldRadius)){
      if(NightAblation::collectCounters){++NightAblation::farPickCounter.rejected;if(NightAblation::farPickEnabled)++NightAblation::farPickCounter.applied;}
      continue;
    }
    float d = Math::sqrtNonNegative(dx * dx + dy * dy + dz * dz) - worldRadius;
    if (d < 0.0F) d = 0.0F;
    if (d >= l->range) continue;
    const float att = 1.0F - d / l->range;
    float score =
        (l->color.r + l->color.g + l->color.b) * (1.0F / 3.0F) * att * att;
    if (!l->point) {
      // Spot cone: down-rank when the whole sphere sits outside the cone
      // (approximate - sin(angle) ~ radius/distance). Never zero: an aimed
      // flashlight sweeping onto a mesh must not pop a torch off mid-swing
      // when both scores are close.
      const float dist = d + worldRadius;
      if (dist > 1e-4F) {
        const float cosAng = -(dx * l->direction.x + dy * l->direction.y +
                               dz * l->direction.z) /
                             dist;
        if (cosAng + worldRadius / dist < l->cosCutoff) score *= 0.05F;
      }
    }
    if (score > bestScore) {
      bestScore = score;
      best = l;
    }
  }
  return best;
  };
  const auto* result=choose(NightAblation::farPickEnabled);
  if(NightAblation::farPickSelected&&NightAblation::collectCounters){
    auto& c=NightAblation::farPickCounter;++c.calls;++c.compared;
    const auto* other=choose(!NightAblation::farPickEnabled);
    if(result!=other){++c.mismatches;NightAblation::valid=false;}
  }
  return result;
}

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
NightAblation::farPickEnabled=false;const auto* a=c.pickDynLight(center,radius,skip);NightAblation::farPickEnabled=true;const auto* z=c.pickDynLight(center,radius,skip);if(a!=z){printf("MISMATCH %u\n",i);return 1;}++selected;
}
for(unsigned i=0;i<20000000;++i){float x=raw(random32()),y=raw(random32()),z=raw(random32()),range=raw(random32()),radius=raw(random32());++gateCases;if(!NightAblation::farLightCandidate(x,y,z,range,radius))continue;++rejects;float d=Math::sqrtNonNegative(x*x+y*y+z*z)-radius;if(d<0)d=0;if(!(d>=range)){printf("GATE_MISMATCH %u\n",i);return 2;}}
printf("PASS selected=%llu gateCases=%llu rejected=%llu\n",(unsigned long long)selected,(unsigned long long)gateCases,(unsigned long long)rejects);
}
