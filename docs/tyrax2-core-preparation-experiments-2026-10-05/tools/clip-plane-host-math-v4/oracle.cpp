#include <cstdint>
using u8=uint8_t;
#include <cstdio>
#include <cstring>
#define VU1_CLIP_XY_BAND 0.9F
struct Vec4 {union{struct{float x,y,z,w;};float xyzw[4];};};struct Plane {Vec4 normal;float distance;};
namespace {
inline float eeClipMul(float a,float b){
#if defined(__mips__)
 float out;asm("mul.s %0, %1, %2" : "=f"(out) : "f"(a),"f"(b));return out;
#else
 return a*b;
#endif
}
// PRIVATE: transpose the work; share hardware-rounded coefficient products
// for one matrix row across all8planes. Inputs never alias Core plane storage:
// computeClipObjectSpacePlanes is private and both callers pass local MVP.
template<unsigned Row> inline void specializedClipRow(Plane* __restrict__ out,const float* __restrict__ m,float nearZ,float farZ){
 const float* v=m+Row*4;
 const float zx=eeClipMul(0.0F,v[0]),zy=eeClipMul(0.0F,v[1]),zz=eeClipMul(0.0F,v[2]),zw=eeClipMul(0.0F,v[3]);
 const float px=eeClipMul(1.0F,v[0]),py=eeClipMul(1.0F,v[1]),pz=eeClipMul(1.0F,v[2]),pw=eeClipMul(1.0F,v[3]);
 const float nx=eeClipMul(-1.0F,v[0]),ny=eeClipMul(-1.0F,v[1]),nz=eeClipMul(-1.0F,v[2]),bw=eeClipMul(VU1_CLIP_XY_BAND,v[3]);
 if constexpr(Row<3){
  out[0].normal.xyzw[Row]=((zx+zy)+nz)+zw;
  out[1].normal.xyzw[Row]=((zx+zy)+pz)+zw;
  out[2].normal.xyzw[Row]=((nx+zy)+zz)+bw;
  out[3].normal.xyzw[Row]=((px+zy)+zz)+bw;
  out[4].normal.xyzw[Row]=((zx+ny)+zz)+bw;
  out[5].normal.xyzw[Row]=((zx+py)+zz)+bw;
  out[6].normal.xyzw[Row]=((zx+zy)+nz)+pw;
  out[7].normal.xyzw[Row]=((zx+zy)+pz)+pw;
 }else{
  out[0].distance=(((nearZ+zx)+zy)+nz)+zw;
  out[1].distance=(((-farZ+zx)+zy)+pz)+zw;
  out[2].distance=(((0.0F+nx)+zy)+zz)+bw;
  out[3].distance=(((0.0F+px)+zy)+zz)+bw;
  out[4].distance=(((0.0F+zx)+ny)+zz)+bw;
  out[5].distance=(((0.0F+zx)+py)+zz)+bw;
  out[6].distance=(((0.0F+zx)+zy)+nz)+pw;
  out[7].distance=(((0.0F+zx)+zy)+pz)+pw;
 }
}
inline void specializedClipPlanes(Plane* __restrict__ out,const float* __restrict__ m,float nearZ,float farZ){
 specializedClipRow<0>(out,m,nearZ,farZ);specializedClipRow<1>(out,m,nearZ,farZ);
 specializedClipRow<2>(out,m,nearZ,farZ);specializedClipRow<3>(out,m,nearZ,farZ);
 for(unsigned i=0;i<8;++i)out[i].normal.w=1.0F;
}
}


void reference(Plane* clipObjectSpacePlanes,const float* matrix,float nearZ,float farZ){
  const float b = VU1_CLIP_XY_BAND;
  const float planes[8][5] = {
      {0.0F, 0.0F, -1.0F, 0.0F, nearZ},  // near
      {0.0F, 0.0F, 1.0F, 0.0F, -farZ},   // far
      {-1.0F, 0.0F, 0.0F, b, 0.0F},      // right
      {1.0F, 0.0F, 0.0F, b, 0.0F},       // left
      {0.0F, -1.0F, 0.0F, b, 0.0F},      // bottom (projection flips Y)
      {0.0F, 1.0F, 0.0F, b, 0.0F},       // top
      // Modified by TyraX: EE-only, never uploaded. The cull program's clipw
      // tests |z| < |w| on top of x and y, so a package may only take that
      // path when it is inside the exact near (z <= w) and far (z >= -w)
      // planes too. The guard band's own near constant is DELIBERATELY looser
      // (PlanesClipAlgorithm::clipMargin), which leaves a thin shell in front
      // of the near plane where the clipper draws a triangle the cull program
      // would ADC away - a hole at point blank range.
      {0.0F, 0.0F, -1.0F, 1.0F, 0.0F},  // exact near
      {0.0F, 0.0F, 1.0F, 1.0F, 0.0F},   // exact far
  };

  const float* m = matrix;
  for (u8 i = 0; i < 8; ++i) {
    const float* p = planes[i];
    Plane& out = clipObjectSpacePlanes[i];
    out.normal.x = p[0] * m[0] + p[1] * m[1] + p[2] * m[2] + p[3] * m[3];
    out.normal.y = p[0] * m[4] + p[1] * m[5] + p[2] * m[6] + p[3] * m[7];
    out.normal.z = p[0] * m[8] + p[1] * m[9] + p[2] * m[10] + p[3] * m[11];
    out.normal.w = 1.0F;
    out.distance = p[4] + p[0] * m[12] + p[1] * m[13] + p[2] * m[14] +
                   p[3] * m[15];
  }
}

int main(){uint32_t state=0x19751230;unsigned mismatches=0;for(unsigned trial=0;trial<1000000;++trial){float m[16];for(float& v:m){state^=state<<13;state^=state>>17;state^=state<<5;uint32_t bits=(state&0x807fffffu)|(((state>>24)%190+20)<<23);if(trial<2)bits=trial?0x80000000u:0u;memcpy(&v,&bits,4);}Plane a[8],b[8];reference(a,m,1.25F,-1234.5F);specializedClipPlanes(b,m,1.25F,-1234.5F);for(unsigned i=0;i<8;++i)if(memcmp(a[i].normal.xyzw,b[i].normal.xyzw,16)||memcmp(&a[i].distance,&b[i].distance,4))++mismatches;}printf("matrices=1000000 planeComparisons=8000000 mismatches=%u\n",mismatches);return mismatches?1:0;}
