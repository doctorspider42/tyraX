#include <cmath>
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
void invertAffine(const float* m, float* inv3x3, float* invT) {
  const float a = m[0], b = m[4], c = m[8];
  const float d = m[1], e = m[5], f = m[9];
  const float g = m[2], h = m[6], i = m[10];
  float det = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
  if (det > -1e-12F && det < 1e-12F) det = 1e-12F;
  const float id = 1.0F / det;
  inv3x3[0] = (e * i - f * h) * id;
  inv3x3[1] = (c * h - b * i) * id;
  inv3x3[2] = (b * f - c * e) * id;
  inv3x3[3] = (f * g - d * i) * id;
  inv3x3[4] = (a * i - c * g) * id;
  inv3x3[5] = (c * d - a * f) * id;
  inv3x3[6] = (d * h - e * g) * id;
  inv3x3[7] = (b * g - a * h) * id;
  inv3x3[8] = (a * e - b * d) * id;
  invT[0] = -(inv3x3[0] * m[12] + inv3x3[1] * m[13] + inv3x3[2] * m[14]);
  invT[1] = -(inv3x3[3] * m[12] + inv3x3[4] * m[13] + inv3x3[5] * m[14]);
  invT[2] = -(inv3x3[6] * m[12] + inv3x3[7] * m[13] + inv3x3[8] * m[14]);
}

// Builds the object-space spot light for this mesh: transforms the world
// light through the inverse model matrix, expresses the range in object
// units via the (assumed near-uniform) mesh scale, and precomputes the
// constants the cull VU1 programs and the EE clipper both consume.
StaPipClipperSpot buildSpotForBagOriginal(const RendererCoreSpotLight& spot,
                                  const M4x4* model) {
  StaPipClipperSpot out;
  out.enabled = spot.enabled;
  if (!spot.enabled) return out;

  float inv[9], invT[3];
  invertAffine(model->data, inv, invT);

  out.position.x = inv[0] * spot.position.x + inv[1] * spot.position.y +
                   inv[2] * spot.position.z + invT[0];
  out.position.y = inv[3] * spot.position.x + inv[4] * spot.position.y +
                   inv[5] * spot.position.z + invT[1];
  out.position.z = inv[6] * spot.position.x + inv[7] * spot.position.y +
                   inv[8] * spot.position.z + invT[2];
  out.position.w = 1.0F;

  Vec4 dir;
  dir.x = inv[0] * spot.direction.x + inv[1] * spot.direction.y +
          inv[2] * spot.direction.z;
  dir.y = inv[3] * spot.direction.x + inv[4] * spot.direction.y +
          inv[5] * spot.direction.z;
  dir.z = inv[6] * spot.direction.x + inv[7] * spot.direction.y +
          inv[8] * spot.direction.z;
  float dirLen2 = dir.x * dir.x + dir.y * dir.y + dir.z * dir.z;
  if (dirLen2 < 1e-10F) dirLen2 = 1.0F;

  // A world direction through the inverse scales by 1/s (uniform scale s),
  // so |dir|^2 = 1/s^2 - reuse it to express the range in object units.
  const float objRange2 = spot.range * spot.range * dirLen2;

  const float invDirLen = 1.0F / Math::sqrtNonNegative(dirLen2);
  out.direction.x = dir.x * invDirLen;
  out.direction.y = dir.y * invDirLen;
  out.direction.z = dir.z * invDirLen;
  out.direction.w = 0.0F;

  out.color[0] = spot.color.r;
  out.color[1] = spot.color.g;
  out.color[2] = spot.color.b;
  out.invRange2 = 1.0F / objRange2;

  // Point (omni) light through the SAME spot constants: zero direction makes
  // the axial term t = max(0, d.dir) collapse to 0, so the cone factor
  // becomes (0 - cosCut2*dist2)*invSoft = dist2 * invSoft with cosCut2 = -1.
  // invSoft is sized to saturate that to 1 within ~1% of the range - the
  // radial falloff alone shapes the light. No VU1 change, no micro memory.
  if (spot.point) {
    out.direction.x = 0.0F;
    out.direction.y = 0.0F;
    out.direction.z = 0.0F;
    out.cosCut2 = -1.0F;
    out.invSoft = 1.0e4F / objRange2;
    return out;
  }

  out.cosCut2 = spot.cosCutoff * spot.cosCutoff;
  // The VU1/EE cone term is clamp01((t^2 - cosCut2*dist2) * invSoft). Its
  // SIGN is the exact angular cutoff and is distance-independent, but its
  // MAGNITUDE scales with dist2 - so sizing invSoft off the full range
  // (objRange2 * (1 - cosCut2)) made the beam ramp up across the whole
  // range: dim on everything close to the lamp, fully bright only near its
  // far end. On the camera flashlight that reads as "it doesn't light what
  // I'm looking at". Saturate at a fraction of the range instead; the
  // cutoff ANGLE is unchanged, the edge just gets crisper.
  constexpr float kFullBrightAt = 0.18F;  // of the range, on the axis
  const float coneBase =
      objRange2 * kFullBrightAt * kFullBrightAt * (1.0F - out.cosCut2);
  out.invSoft = coneBase > 1e-10F ? spot.softness / coneBase : 0.0F;
  return out;
}
// PRIVATE complete numerical result memo; no borrowing or selection changes.
struct SpotResultKey { unsigned v[29]; };
struct SpotResultEntry { bool valid=false;SpotResultKey key{};StaPipClipperSpot result{}; };
inline SpotResultEntry spotResults[32]{};
static bool makeSpotResultKey(SpotResultKey& key,const RendererCoreSpotLight& s,const M4x4* m){
  memcpy(key.v,m->data,64);
  const float f[12]={s.position.x,s.position.y,s.position.z,s.direction.x,s.direction.y,s.direction.z,
    s.color.r,s.color.g,s.color.b,s.range,s.cosCutoff,s.softness};
  memcpy(key.v+16,f,48);key.v[28]=s.point?1:0;
  for(unsigned i=0;i<28;++i)if((key.v[i]&0x7f800000u)==0x7f800000u)return false;
  return s.enabled;
}
static unsigned spotResultSlot(const SpotResultKey& k){
  unsigned h=k.v[0]^(k.v[5]*33u)^(k.v[10]*17u)^(k.v[12]*7u)^(k.v[13]*3u)^k.v[14]^(k.v[16]*5u)^k.v[17]^(k.v[18]*11u)^k.v[25]^k.v[28];
  h^=h>>16;h*=0x7feb352du;h^=h>>15;return h&31u;
}
static unsigned spotResultDifference(const StaPipClipperSpot& a,const StaPipClipperSpot& b){
  float av[14]={a.position.x,a.position.y,a.position.z,a.position.w,a.direction.x,a.direction.y,a.direction.z,a.direction.w,a.color[0],a.color[1],a.color[2],a.invRange2,a.cosCut2,a.invSoft};
  float bv[14]={b.position.x,b.position.y,b.position.z,b.position.w,b.direction.x,b.direction.y,b.direction.z,b.direction.w,b.color[0],b.color[1],b.color[2],b.invRange2,b.cosCut2,b.invSoft};
  unsigned d=a.enabled!=b.enabled;for(unsigned i=0;i<14;++i)if(memcmp(av+i,bv+i,4))++d;return d;
}
StaPipClipperSpot buildSpotForBag(const RendererCoreSpotLight& s,const M4x4* m){
  SpotResultKey key{};const bool eligible=s.enabled&&makeSpotResultKey(key,s,m);
  SpotResultEntry* e=eligible?spotResults+spotResultSlot(key):nullptr;
  const bool reused=NightAblation::producerEnabled&&e&&e->valid&&memcmp(e->key.v,key.v,sizeof(key.v))==0;
  StaPipClipperSpot result=reused?e->result:buildSpotForBagOriginal(s,m);
  if(NightAblation::producerEnabled&&e&&!reused){e->key=key;e->result=result;e->valid=true;}
  if(NightAblation::producerSelected&&NightAblation::collectCounters){
    const auto reference=buildSpotForBagOriginal(s,m);unsigned d=spotResultDifference(result,reference);
    NightAblation::producerCount(0,0);NightAblation::producerCount(0,1,eligible?1:0);
    NightAblation::producerCount(0,2,reused?1:0);NightAblation::producerCount(0,3,15);
    NightAblation::producerCount(0,4,d);if(d)NightAblation::valid=false;
  }
  return result;
}

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
 printf("PASS_ACTUAL_SOURCE_HOST_ROUTING compared=%u mutations=%u nonfiniteOrSignedZero=%u reuse=%u\n",compared,mutations,nonfinite,NightAblation::cold[0][2]);
}
