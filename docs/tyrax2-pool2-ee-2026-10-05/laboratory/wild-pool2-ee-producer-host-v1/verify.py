import pathlib, hashlib, json, subprocess
ROOT=pathlib.Path(__file__).resolve().parent
LAB=ROOT.parent
OLD=LAB/'wild-pool-table-physical-v3'
NEW=LAB/'wild-pool2-ee-physical-v1'
def sha(b): return hashlib.sha256(b).hexdigest()
def extract(s, anchor):
    start=s.index(anchor); opening=s.index('{',start); depth=1; i=opening+1
    while depth:
        depth+=(s[i]=='{')-(s[i]=='}'); i+=1
    return s[start:i]
manifest={}
def source(p):
    b=p.read_bytes(); manifest[str(p)]={'sha256':sha(b),'bytes':len(b)}
    return b.decode()
table=NEW/'tyra/engine/inc/renderer/3d/pipeline/static/core/stapip_pool_color_table.hpp'
source(table)
bag=NEW/'game/inc/bag_array.gen.hpp'; source(bag)
oldbag=OLD/'game/inc/bag_array.gen.hpp'; source(oldbag)
assert bag.read_bytes()==oldbag.read_bytes()
(ROOT/'bag_array.gen.hpp').write_bytes(bag.read_bytes())
(ROOT/'stapip_pool_color_table.hpp').write_bytes(table.read_bytes())
shim=r'''#pragma once
#include <cstdint>
#include "stapip_pool_color_table.hpp"
using u32=uint32_t;
namespace Tyra {
struct alignas(16) Vec4 {float x=0,y=0,z=0,w=0;};
struct alignas(16) Color {
 union {struct {float r,g,b,a;}; float rgba[4];};
 Color():r(0),g(0),b(0),a(0){} Color(float x,float y,float z,float t):r(x),g(y),b(z),a(t){}
};
struct M4x4 {void identity(){}};
struct StaPipInfoBag {M4x4* model=nullptr; int shadingType=0,additiveBlendFix=0; bool dateLit=true;};
struct StaPipColorBag {Color* single=nullptr; Color* many=nullptr; const unsigned int* contentVersion=nullptr;};
struct StaPipTextureBag {void* texture=nullptr; Vec4* coordinates=nullptr; const unsigned int* contentVersion=nullptr;};
struct StaPipLightingBag {Vec4* normals=nullptr; const unsigned int* contentVersion=nullptr;};
struct StaPipBag {StaPipInfoBag* info=nullptr; StaPipColorBag* color=nullptr; StaPipTextureBag* texture=nullptr;
 StaPipLightingBag* lighting=nullptr; Vec4* vertices=nullptr; u32 count=0; const unsigned int* contentVersion=nullptr;
 unsigned int bboxVersion=0; ExperimentalPoolTable::View* experimentalPoolTable=nullptr;};
}
constexpr int TyraShadingGouraud=1;
'''
(ROOT/'tyra').write_text(shim)
prefix=r'''#include <algorithm>
#include <memory>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <limits>
#include <string>
#include "tyra"
#include "bag_array.gen.hpp"
namespace Vehicle_playground {unsigned int g_contentStamp=0;}
using namespace Vehicle_playground;
using namespace Tyra;
unsigned int g_bboxStamp=0;
namespace NightAblation {bool poolTableEnabled=false; constexpr int LightEffects=1,Pools=2; void submitted(int){} void extraSubmitted(int){}}
struct StubPipeline {struct Core {size_t calls=0; void render(StaPipBag*){++calls;}} core;};
struct LightPool {BagArray<Vec4> verts,sts; Color color; std::unique_ptr<StaPipInfoBag> info=std::make_unique<StaPipInfoBag>(); std::unique_ptr<StaPipTextureBag> texBag=std::make_unique<StaPipTextureBag>();};
'''
parts=[prefix]
for label,base in [('Legacy',OLD),('Candidate',NEW)]:
    cpp=source(base/'game/src/gen/game_lighting.gen.cpp')
    hpp=source(base/'game/inc/terrain_game.hpp')
    func=extract(cpp,'void TerrainGame::poolBatchFlush()')
    add=extract(cpp,'void TerrainGame::poolBatchAdd(')
    decl=extract(hpp,'struct PoolBatch {')
    for name,s in [('flush',func),('add',add),('declaration',decl)]:
        (ROOT/f'{label}-{name}.txt').write_bytes(s.encode())
        manifest[f'{label}-{name}']={'sha256':sha(s.encode()),'bytes':len(s.encode())}
    parts += [f'namespace {label} {{\nstruct TerrainGame {{\n',decl+' poolBatch_;\nStubPipeline stapip;\nvoid poolBatchFlush(); void poolBatchAdd(const LightPool&,float);\n};\n',add+'\n'+func+'\n}\n']
parts.append((ROOT/'tests.inc').read_text())
(ROOT/'oracle.cpp').write_text('\n'.join(parts))
compiler=pathlib.Path('C:/Users/pawel/scoop/apps/mingw/current/bin/g++.exe')
runs=[]
for opt in ['-O0','-O2']:
    exe=ROOT/('oracle'+opt+'.exe')
    cmd=[str(compiler),'-std=c++17',opt,'-Wall','-Wextra','-Werror','-static','-I',str(ROOT),str(ROOT/'oracle.cpp'),'-o',str(exe)]
    result=subprocess.run(cmd,capture_output=True,text=True)
    (ROOT/(opt+'-compile.log')).write_text(result.stdout+result.stderr)
    if result.returncode: raise RuntimeError(result.stderr)
    run=subprocess.run([str(exe)],capture_output=True,text=True)
    (ROOT/(opt+'-run.log')).write_text(run.stdout+run.stderr)
    runs.append({'optimization':opt,'command':cmd,'compile_exit':result.returncode,'run_exit':run.returncode,'stdout':run.stdout,'stderr':run.stderr,'exe_sha256':sha(exe.read_bytes())})
    if run.returncode: break
manifest['oracle.cpp']={'sha256':sha((ROOT/'oracle.cpp').read_bytes())}
manifest['tests.inc']={'sha256':sha((ROOT/'tests.inc').read_bytes())}
(ROOT/'proof.json').write_text(json.dumps({'sources':manifest,'runs':runs,'limitations':'Host-only actual extracted producer/BagArray/materializeRange semantics. No DMA/cache/target ABI, pixel, native or hardware performance proof.'},indent=2))
print(json.dumps(runs,indent=2))
