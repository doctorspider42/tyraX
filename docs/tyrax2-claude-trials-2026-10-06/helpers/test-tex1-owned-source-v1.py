from pathlib import Path
import subprocess,json,re,hashlib
b=Path('F:/Projects/tyrax2-lab-20261001');d=b/'tex1-owned-source-controls-v1';d.mkdir(exist_ok=True);sdk='/home/spider/.cache/tyrax/native/toolchains/eff2c8918589264d49aa9a0f/ps2sdk/'
for rel in ('common/include/gs_gp.h','ee/include/draw_sampling.h'):
 p=d/Path(rel).name;r=subprocess.run(['wsl','-d','Ubuntu','--','cat',sdk+rel],capture_output=True);assert r.returncode==0;p.write_bytes(r.stdout)
def block(s,anchor):
 i=s.index(anchor);j=s.index('{',i);level=1;k=j+1
 while level:
  if s[k]=='{':level+=1
  elif s[k]=='}':level-=1
  k+=1
 return s[i:k]
src=b/'tex1-owned-physical-v1/tyra/engine/src/renderer/3d/pipeline/static/core/stapip_qbuffer_renderer.cpp';s=src.read_text(encoding='utf8');gs=(d/'gs_gp.h').read_text(encoding='utf8');lines=gs.splitlines();i=next(i for i,x in enumerate(lines)if x.startswith('#define GS_SET_TEX1'));parts=[lines[i]]
while parts[-1].rstrip().endswith(chr(92)):i+=1;parts.append(lines[i])
macro='\n'.join(parts);sam=(d/'draw_sampling.h').read_text(encoding='utf8');defines='\n'.join(x for x in sam.splitlines()if x.startswith('#define LOD_'));lod=re.search(r'typedef struct[\s\S]*?} lod_t;',sam)
# Extract the exact lod_t definition, not any preceding unrelated typedef.
end=sam.index('} lod_t;')+len('} lod_t;');start=sam.rfind('typedef struct',0,end);lod=sam[start:end]
enable=block(s,'void StaPipQBufferRenderer::enableOwnedTex1()');info=block(s,'void StaPipQBufferRenderer::setInfo(')
expr=re.search(r'q\.dw\[0\] = useOwnedTex1 \? ownedTex1Current : GS_SET_TEX1[^;]+;',s)[0].replace('q.dw[0] =','return')
code='''#include <cstdint>
#include <cstring>
#include <cassert>
#include <cstdio>
using u64=uint64_t;
'''+defines+'\n'+macro+'\n'+lod+'''
constexpr int TyraLinear=1;
struct PipelineInfoBag {int textureMappingType=1,antiAliasingEnabled=0,blendingEnabled=0,shadingType=0;};
struct Prim {int antialiasing=0,blending=0,shading=0;};
struct StaPipQBufferRenderer {lod_t* lod=nullptr;Prim store;Prim* prim=&store;bool ownedTex1=false;u64 ownedTex1Linear=0,ownedTex1Nearest=0,ownedTex1Current=0;
 void init(lod_t* p){lod=p;ownedTex1=false;}
 void enableOwnedTex1();void setInfo(PipelineInfoBag* bag);
 u64 emit(bool on){const bool useOwnedTex1=ownedTex1&&on;'''+expr+'''}
};
'''+enable+'\n'+info+'''
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
 printf("compared=%u mutableChecks=%u firstBag=1 mixedFilters=1 reinitGeneric=1\\n",compared,mutableChecks);
}
'''
p=d/'harness.cpp';p.write_bytes(code.encode());wp='/mnt/f/Projects/tyrax2-lab-20261001/tex1-owned-source-controls-v1/';r=subprocess.run(['wsl','-d','Ubuntu','--','g++','-std=c++17','-O2',wp+'harness.cpp','-o',wp+'harness'],capture_output=True);(d/'compile.stdout').write_bytes(r.stdout);(d/'compile.stderr').write_bytes(r.stderr);assert r.returncode==0,r.stderr.decode();r=subprocess.run(['wsl','-d','Ubuntu','--',wp+'harness'],capture_output=True);(d/'oracle.stdout').write_bytes(r.stdout);assert r.returncode==0,r.stdout+r.stderr
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();q=dict(status='PASS_ACTUAL_SOURCE_HOST_GS_TEX1_ORACLE_ONLY',oracle=r.stdout.decode().strip(),sourceSha256=sha(src),sdkPins={sdk+'common/include/gs_gp.h':sha(d/'gs_gp.h'),sdk+'ee/include/draw_sampling.h':sha(d/'draw_sampling.h')},payloads={p.name:sha(p)for p in d.iterdir()if p.is_file()and p.name!='proof.json'},actualTargetAbiQualified=False);(d/'proof.json').write_bytes((json.dumps(q,indent=2)+'\n').encode());print(q['status'],q['oracle'])
