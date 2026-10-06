#include <cstdio>
#include <stdexcept>
struct Vec4 {};struct M4x4 {};struct Color {};struct DirLights {};struct Texture {};
enum {TyraMVP=0,OtherTransform=1,PipelineInfoBagFrustumCulling_None=0,PipelineInfoBagFrustumCulling_Precise=1};
struct Info {M4x4* model;int transformationType;bool fullClipChecks;int frustumCulling;};
struct Colors {Color* single;Color* many;};
struct Lighting {M4x4* lightMatrix;Vec4* normals;DirLights* dirLights;};
struct TextureBag {Texture* texture;Vec4* coordinates;bool coordinatesAreNormals;};
struct Billboard {};
struct Bag {Vec4* vertices;Info* info;Colors* color;Lighting* lighting;TextureBag* texture;Billboard* billboard;};
static int assertionIndex;
struct BadAssertion {int index;};
#define TYRA_ASSERT(condition, ...) do {int i=assertionIndex++;if(!(condition))throw BadAssertion{i};}while(0)
static int validate(Bag* bag,bool frustumCull){assertionIndex=0;try{
  TYRA_ASSERT(bag->vertices != nullptr,
              "Vertices are required in 3D render bag!");
  TYRA_ASSERT(bag->info != nullptr, "Info bag is required in 3D render bag!");
  TYRA_ASSERT(bag->info->model != nullptr,
              "Info bag's model pointer is empty!");
  TYRA_ASSERT(bag->color != nullptr, "Color bag is required in 3D render bag!");
  TYRA_ASSERT(bag->color->single || bag->color->many,
              "At least one color is required in 3D render bag!");
  TYRA_ASSERT((!bag->color->many && !bag->lighting) ||
                  (bag->color->many && !bag->lighting) ||
                  (!bag->color->many && bag->lighting),
              "Multicolor is not supported with lighting, please choose one!");
  TYRA_ASSERT(
      !bag->lighting || (bag->lighting->lightMatrix && bag->lighting->normals &&
                         bag->lighting->dirLights),
      "If you want lighting, please provide light matrix normals and dir "
      "lights!");
  // Modified by TyraX: a billboard bag carries a texture bag purely for the
  // per-particle params channel - the image itself is optional there.
  TYRA_ASSERT(!bag->texture || ((bag->texture->texture || bag->billboard) &&
                                bag->texture->coordinates),
              "If you want texture, please provide texture and coordinates!");
  // Modified by TyraX: particle billboards (centers expanded on VU1).
  // frustumCulling None is SAFE here (unlike ordinary bags - see the
  // "never submit with None" pitfall): the billboard programs cull every
  // quad whose corner leaves the GS raster window / depth range, so
  // off-screen centers never wrap the 4096-px window.
  TYRA_ASSERT(!bag->billboard ||
                  (bag->texture && bag->texture->coordinates &&
                   bag->color->many && !bag->lighting &&
                   !bag->info->fullClipChecks &&
                   bag->info->frustumCulling ==
                       PipelineInfoBagFrustumCulling_None),
              "Billboard bags need per-particle params in the texture "
              "coordinates slot, per-particle colors, no lighting, no "
              "frustum culling and no clip checks (VU1 culls per quad)!");
  // Modified by TyraX: env (matcap) bags - normals in the ST slot, ST
  // computed on VU1 (cull_tce + as_is_tce / clip_tce). No lighting - the
  // env programs derive no dir-light color.
  TYRA_ASSERT(!bag->texture || !bag->texture->coordinatesAreNormals ||
                  bag->lighting == nullptr,
              "Env (matcap) bags do not support lighting!");
  TYRA_ASSERT(bag->info->transformationType == TyraMVP ||
                  (!bag->info->fullClipChecks && !frustumCull),
              "Please disable clip checks and frustum culling if not using MVP "
              "matrix!");
  TYRA_ASSERT(!(!frustumCull && bag->info->fullClipChecks == true),
              "Full clip checks are not supported with frustum culling = off!");

return assertionIndex;}catch(BadAssertion bad){return -1-bad.index;}}
int main(){Vec4 vertices,normals,coordinates;M4x4 model;Color color;DirLights dirs;Texture tex;Billboard billboard;
 Info info{&model,TyraMVP,true,PipelineInfoBagFrustumCulling_Precise};Colors colors{&color,nullptr};Lighting light{&model,&normals,&dirs};TextureBag texture{&tex,&coordinates,false};Bag good{&vertices,&info,&colors,nullptr,&texture,nullptr};
 if(validate(&good,true)!=12)return 10;
 for(int i=0;i<12;++i){Info qi=info;Colors qc=colors;Lighting ql=light;TextureBag qt=texture;Bag q=good;q.info=&qi;q.color=&qc;q.texture=&qt;bool frustum=true;
 switch(i){case 0:q.vertices=nullptr;break;case 1:q.info=nullptr;break;case 2:qi.model=nullptr;break;case 3:q.color=nullptr;break;case 4:qc.single=nullptr;break;case 5:qc.many=&color;q.lighting=&ql;break;case 6:q.lighting=&ql;ql.normals=nullptr;break;case 7:qt.coordinates=nullptr;break;case 8:q.billboard=&billboard;break;case 9:qt.coordinatesAreNormals=true;q.lighting=&ql;break;case 10:qi.transformationType=OtherTransform;break;case 11:frustum=false;break;}
 int v=validate(&q,frustum);if(v!=-1-i){std::printf("invalid%d got%d\n",i,v);return 20+i;}}
 // Positive lit / no texture and valid particle contracts.
 Bag lit=good;lit.texture=nullptr;lit.lighting=&light;if(validate(&lit,true)!=12)return 40;
 Info bi=info;bi.fullClipChecks=false;bi.frustumCulling=PipelineInfoBagFrustumCulling_None;Colors bc{nullptr,&color};TextureBag bt{nullptr,&coordinates,false};Bag particle{&vertices,&bi,&bc,nullptr,&bt,&billboard};if(validate(&particle,false)!=12)return 41;
 std::puts("PASS_EXTRACTED_TWELVE_ASSERTIONS_12_INVALID_3_VALID");return 0;}
