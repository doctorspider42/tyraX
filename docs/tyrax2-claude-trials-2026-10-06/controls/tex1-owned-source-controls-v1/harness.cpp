#include <cstdint>
#include <cstring>
#include <cassert>
#include <cstdio>
using u64=uint64_t;
#define LOD_FORMULAIC				0
#define LOD_USE_K					1
#define LOD_MAG_NEAREST				0
#define LOD_MAG_LINEAR				1
#define LOD_MIN_NEAREST				0
#define LOD_MIN_LINEAR				1
#define LOD_MIN_NEAR_MIPMAP_NEAR	2
#define LOD_MIN_NEAR_MIPMAP_LINE	3
#define LOD_MIN_LINE_MIPMAP_NEAR	4
#define LOD_MIN_LINE_MIPMAP_LINE	5
#define LOD_MIPMAP_REGISTER			0
#define LOD_MIPMAP_CALCULATE		1
#define GS_SET_TEX1(LCM, MXL, MMAG, MMIN, MTBA, L, K)                   \
    (u64)((LCM)&0x00000001) << 0 | (u64)((MXL)&0x00000007) << 2 |       \
        (u64)((MMAG)&0x00000001) << 5 | (u64)((MMIN)&0x00000007) << 6 | \
        (u64)((MTBA)&0x00000001) << 9 | (u64)((L)&0x00000003) << 19 |   \
        (u64)((K)&0x00000FFF) << 32
typedef struct {
	unsigned char calculation;
	unsigned char max_level;
	unsigned char mag_filter;
	unsigned char min_filter;
	unsigned char mipmap_select;
	unsigned char l;
	float k;
} lod_t;
constexpr int TyraLinear=1;
struct PipelineInfoBag {int textureMappingType=1,antiAliasingEnabled=0,blendingEnabled=0,shadingType=0;};
struct Prim {int antialiasing=0,blending=0,shading=0;};
struct StaPipQBufferRenderer {lod_t* lod=nullptr;Prim store;Prim* prim=&store;bool ownedTex1=false;u64 ownedTex1Linear=0,ownedTex1Nearest=0,ownedTex1Current=0;
 void init(lod_t* p){lod=p;ownedTex1=false;}
 void enableOwnedTex1();void setInfo(PipelineInfoBag* bag);
 u64 emit(bool on){const bool useOwnedTex1=ownedTex1&&on;return useOwnedTex1 ? ownedTex1Current : GS_SET_TEX1(lod->calculation,lod->max_level,lod->mag_filter,lod->min_filter,lod->mipmap_select,lod->l,(int)(lod->k*16.0F));}
};
void StaPipQBufferRenderer::enableOwnedTex1() {
  ownedTex1Linear = GS_SET_TEX1(lod->calculation,lod->max_level,LOD_MAG_LINEAR,LOD_MIN_LINEAR,lod->mipmap_select,lod->l,(int)(lod->k*16.0F));
  ownedTex1Nearest = GS_SET_TEX1(lod->calculation,lod->max_level,LOD_MAG_NEAREST,LOD_MIN_NEAREST,lod->mipmap_select,lod->l,(int)(lod->k*16.0F));
  ownedTex1Current = GS_SET_TEX1(lod->calculation,lod->max_level,lod->mag_filter,lod->min_filter,lod->mipmap_select,lod->l,(int)(lod->k*16.0F));
  ownedTex1 = true;
}
void StaPipQBufferRenderer::setInfo(PipelineInfoBag* bag) {
  prim->antialiasing = bag->antiAliasingEnabled;
  prim->blending = bag->blendingEnabled;
  prim->shading = bag->shadingType;

  if (bag->textureMappingType == TyraLinear) {
    lod->mag_filter = LOD_MAG_LINEAR;
    lod->min_filter = LOD_MIN_LINEAR;
  } else {
    lod->mag_filter = LOD_MAG_NEAREST;
    lod->min_filter = LOD_MIN_NEAREST;
  }
  if(ownedTex1)ownedTex1Current=bag->textureMappingType==TyraLinear?ownedTex1Linear:ownedTex1Nearest;
}
u64 baseline(const lod_t& l){return GS_SET_TEX1(l.calculation,l.max_level,l.mag_filter,l.min_filter,l.mipmap_select,l.l,(int)(l.k*16.0F));}
int main(){lod_t l{};l.calculation=1;l.mag_filter=LOD_MAG_LINEAR;l.min_filter=LOD_MIN_LINEAR;StaPipQBufferRenderer r;r.init(&l);assert(!r.ownedTex1);r.enableOwnedTex1();unsigned compared=0;
 // First bag, before any setInfo; then keep original send-before-setInfo ordering.
 assert(r.emit(true)==baseline(l));++compared;PipelineInfoBag bag;
 for(unsigned i=0;i<100000;++i){assert(r.emit(true)==baseline(l));assert(r.emit(false)==baseline(l));compared+=2;bag.textureMappingType=(i*17u%7u)<3u?TyraLinear:0;r.setInfo(&bag);}
 // A public generic init resets opt-in, then caller mutates each semantic field.
 r.init(&l);assert(!r.ownedTex1);unsigned mutableChecks=0;
 for(unsigned i=0;i<10000;++i){
  l.calculation=i&3;assert(r.emit(true)==baseline(l));++mutableChecks;
  l.max_level=i&15;assert(r.emit(true)==baseline(l));++mutableChecks;
  l.mag_filter=i&3;assert(r.emit(true)==baseline(l));++mutableChecks;
  l.min_filter=i&15;assert(r.emit(true)==baseline(l));++mutableChecks;
  l.mipmap_select=i&3;assert(r.emit(true)==baseline(l));++mutableChecks;
  l.l=i&7;assert(r.emit(true)==baseline(l));++mutableChecks;
  l.k=float(int(i%4097)-2048)/16.0F;assert(r.emit(true)==baseline(l));++mutableChecks;
 }
 printf("compared=%u mutableChecks=%u firstBag=1 mixedFilters=1 reinitGeneric=1\n",compared,mutableChecks);
}
