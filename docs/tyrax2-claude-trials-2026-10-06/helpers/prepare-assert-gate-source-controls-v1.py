from pathlib import Path
import hashlib,json
b=Path('F:/Projects/tyrax2-lab-20261001');f=b/'assert-gate-physical-v1';out=b/'assert-gate-source-controls-v1';assert not out.exists();out.mkdir()
s=(f/'tyra/engine/src/renderer/3d/pipeline/static/core/stapip_core.cpp').read_text();start=s.index('  TYRA_ASSERT(bag->vertices != nullptr,',s.index('void StaPipCore::render'));end=s.index('#if TYRA_VU1_EXP_ENV_NORMALIZED',start);a=s[start:end];assert a.endswith('  }\n');a=a[:-4];start2=s.index('  TYRA_ASSERT(bag->info->transformationType',end);end2=s.index('  }',start2);z=s[start2:end2];assert a.count('TYRA_ASSERT(')==10 and z.count('TYRA_ASSERT(')==2
prefix='''#include <cstdio>
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
'''
suffix='''return assertionIndex;}catch(BadAssertion bad){return -1-bad.index;}}
int main(){Vec4 vertices,normals,coordinates;M4x4 model;Color color;DirLights dirs;Texture tex;Billboard billboard;
 Info info{&model,TyraMVP,true,PipelineInfoBagFrustumCulling_Precise};Colors colors{&color,nullptr};Lighting light{&model,&normals,&dirs};TextureBag texture{&tex,&coordinates,false};Bag good{&vertices,&info,&colors,nullptr,&texture,nullptr};
 if(validate(&good,true)!=12)return 10;
 for(int i=0;i<12;++i){Info qi=info;Colors qc=colors;Lighting ql=light;TextureBag qt=texture;Bag q=good;q.info=&qi;q.color=&qc;q.texture=&qt;bool frustum=true;
 switch(i){case 0:q.vertices=nullptr;break;case 1:q.info=nullptr;break;case 2:qi.model=nullptr;break;case 3:q.color=nullptr;break;case 4:qc.single=nullptr;break;case 5:qc.many=&color;q.lighting=&ql;break;case 6:q.lighting=&ql;ql.normals=nullptr;break;case 7:qt.coordinates=nullptr;break;case 8:q.billboard=&billboard;break;case 9:qt.coordinatesAreNormals=true;q.lighting=&ql;break;case 10:qi.transformationType=OtherTransform;break;case 11:frustum=false;break;}
 int v=validate(&q,frustum);if(v!=-1-i){std::printf("invalid%d got%d\\n",i,v);return 20+i;}}
 // Positive lit / no texture and valid particle contracts.
 Bag lit=good;lit.texture=nullptr;lit.lighting=&light;if(validate(&lit,true)!=12)return 40;
 Info bi=info;bi.fullClipChecks=false;bi.frustumCulling=PipelineInfoBagFrustumCulling_None;Colors bc{nullptr,&color};TextureBag bt{nullptr,&coordinates,false};Bag particle{&vertices,&bi,&bc,nullptr,&bt,&billboard};if(validate(&particle,false)!=12)return 41;
 std::puts("PASS_EXTRACTED_TWELVE_ASSERTIONS_12_INVALID_3_VALID");return 0;}
'''
code=prefix+a+z+suffix;(out/'assertions.cpp').write_bytes(code.encode())
review=dict(status='PREPARED_NOT_COMPILED_OR_EXECUTED',sourceSha256=hashlib.sha256((f/'tyra/engine/src/renderer/3d/pipeline/static/core/stapip_core.cpp').read_bytes()).hexdigest(),harnessSha256=hashlib.sha256(code.encode()).hexdigest(),assertions=12,knownInvalidCases=12,validCases=3,limitations=['Exact original expression/message bytes, host representative pointer fields only; no target ABI claim.','Standalone validation expressions do not include earlier fog dereferences or ENV_NORMALIZED operation.','NDEBUG macro not used; trapping replaced by first-failure exception to test original short-circuit order.','Source-only draft; root may compile/run separately.'])
(out/'preparation.json').write_bytes((json.dumps(review,indent=2)+'\n').encode());print(review['status'])
