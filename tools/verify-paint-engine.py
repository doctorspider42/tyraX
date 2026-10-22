"""Compile the actual capability/guard and preprocess every wrapper selection."""
import argparse
import json
import re
import subprocess
import tempfile
from pathlib import Path

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('engine',type=Path)
parser.add_argument('--baseline',type=Path,help='Pre-A10 engine header snapshot')
args=parser.parse_args()
core=args.engine/'inc/renderer/3d/pipeline/static/core'
header=(core/'stapip_qbuffer_renderer.hpp').read_text()
method=re.search(r'  bool supportsPaintVu\(const StaPipBag\* bag\) const \{.*?\n  \}',header,re.S)[0]
source=(args.engine/'src/renderer/3d/pipeline/static/core/stapip_core.cpp').read_text()
guard=re.search(r'#if TYRA_VU1_EXP_PAINT\n  if \(bag->texture.*?\n#endif',source,re.S)[0]
program=r'''
#include <cassert>
enum { TyraMVP=0, StaPipClassTextureEnv=16 };
struct Texture { void* texture; void* coordinates; bool coordinatesAreNormals; int textureFunction; bool paintVu; };
struct Color { void* many; void* single; };
struct Info { int transformationType; };
struct StaPipBag { Texture* texture; Color* color; void* lighting; void* billboard; Info* info; };
struct Repository { bool overridden=false; bool hasAnyOverride() const { return overridden; } };
struct Driver {
 bool vu1Clipping=true; unsigned residentClasses=31; Repository repository;
 METHOD
 unsigned submits=0;
 void render(StaPipBag* bag) {
   (void)bag;
   // NDEBUG semantics, while the test's own assert() stays enabled.
   #define TYRA_ASSERT(...) ((void)0)
   GUARD
   ++submits;
 }
};
int main() {
 for(int test=0;test<17;++test) {
  int data=0; Texture tex{&data,&data,true,3,true}; Color color{&data,nullptr}; Info info{TyraMVP};
  StaPipBag bag{&tex,&color,nullptr,nullptr,&info}; Driver driver;
  switch(test) {
   case 1: driver.vu1Clipping=false; break;
   case 2: driver.repository.overridden=true; break;
   case 3: driver.residentClasses&=~StaPipClassTextureEnv; break;
   case 4: tex.texture=nullptr; break;
   case 5: tex.coordinates=nullptr; break;
   case 6: tex.coordinatesAreNormals=false; break;
   case 7: tex.textureFunction=0; break;
   case 8: color.many=nullptr; break;
   case 9: color.single=&data; break;
   case 10: bag.lighting=&data; break;
   case 11: bag.billboard=&data; break;
   case 12: info.transformationType=99; break;
   case 13: bag.texture=nullptr; break;
   case 14: bag.color=nullptr; break;
   case 15: bag.info=nullptr; break;
   case 16: driver.residentClasses=0; break;
  }
  assert(driver.supportsPaintVu(&bag)==bool(TYRA_VU1_EXP_PAINT && test==0));
  assert(!driver.supportsPaintVu(nullptr));
  driver.render(&bag);
  assert(driver.submits==unsigned(!TYRA_VU1_EXP_PAINT || test==0 || test==13));
 }
}
'''.replace('METHOD',method).replace('GUARD',guard)
results={}
with tempfile.TemporaryDirectory() as tmp:
    cpp=Path(tmp)/'actual.cpp'; exe=Path(tmp)/'actual'
    if args.baseline:
        layout=[]
        for root in (args.baseline,args.engine):
            raw=(root/'inc/renderer/3d/pipeline/static/core/bag/stapip_texture_bag.hpp').read_text()
            cpp.write_text(re.sub(r'^#include[^\n]*','',raw,flags=re.M))
            run=subprocess.run(['g++','-E','-P','-x','c++','-DTYRA_VU1_EXP_PAINT=0',str(cpp)],
                               check=True,capture_output=True,text=True)
            layout.append(re.sub(r'\s+',' ',run.stdout).strip())
        assert layout[0]==layout[1], 'Default texture-bag declarations changed'
        results['defaultTextureBagDeclarationsUnchanged']=True
    for enabled in (0,1):
        for broken in (False,True) if enabled else (False,):
            code=program.replace('return;  // NDEBUG',';  // NDEBUG') if broken else program
            cpp.write_text(code)
            subprocess.run(['g++','-std=c++17','-Wall','-Wextra','-Werror',
                            '-fsanitize=address,undefined','-fno-omit-frame-pointer',
                            f'-DTYRA_VU1_EXP_PAINT={enabled}',str(cpp),'-o',str(exe)],check=True)
            run=subprocess.run([str(exe)],capture_output=True,text=True)
            assert (run.returncode==0)==(not broken),(enabled,broken,run.stderr)
            if broken: assert 'driver.submits' in run.stderr,run.stderr
            results[f'capability-{enabled}-guardMutation-{broken}']=run.returncode==0
    wrappers=args.engine/'src/renderer/3d/pipeline/static/core/programs'
    for enabled in (0,1):
        for folded in (0,1):
            for normalized in (0,1):
                symbols=[]
                for stem in ['cull/stapip_cull_tce','clip/stapip_clip_tc',
                             'clip/stapip_clip_td','clip/stapip_clip_tce']:
                    raw=(wrappers/(stem+'_vu1_program.cpp')).read_text().split('namespace Tyra {')[0]
                    raw=re.sub(r'^#include[^\n]*','',raw,flags=re.M)
                    cpp.write_text(raw)
                    run=subprocess.run(['g++','-E','-P','-x','c++',f'-DTYRA_VU1_EXP_PAINT={enabled}',
                        f'-DTYRA_VU1_EXP_EE_LIGHT_FOLD={folded}',f'-DTYRA_VU1_EXP_ENV_NORMALIZED={normalized}',
                        str(cpp)],check=True,capture_output=True,text=True)
                    names=re.findall(r'extern u32 (\w+)_CodeStart\b',run.stdout)
                    want=('StaPipVU1Cull_TCE'+('_ENVN' if normalized else '') if stem.startswith('cull/')
                          else 'StaPipVU1Clip_TC'+('_FOLD' if folded else ''))+('_PAINT' if enabled else '')
                    assert names==[want],(stem,enabled,folded,normalized,names,want)
                    symbols+=names
                assert len(set(symbols[1:]))==1
                results[f'wrappers-{enabled}{folded}{normalized}']=symbols
print(json.dumps(results,indent=2))
print('PASS actual capability/guard both flags; NDEBUG guard mutation rejected; 32 wrapper preprocesses')
