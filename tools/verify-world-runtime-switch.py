"""Compile actual owner warm-sweep/producer guard with small host storage shims."""
import argparse
import subprocess
import tempfile
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__);p.add_argument('fixture',type=Path);a=p.parse_args()
main=(a.fixture/'game/src/terrain_game.cpp').read_text()
start=main.index('void TerrainGame::prepareWorldOwners(){')
body=main[start:main.rfind('\n}')].strip()
guard='if(!WorldRuntime::active && !WorldRuntime::cold)return false;'
renderer=(a.fixture/'tyra/engine/src/renderer/3d/pipeline/static/core/stapip_qbuffer_renderer.cpp').read_text()
assert guard in renderer
pre=r'''
#include <cassert>
#include <vector>
using u32=unsigned;
namespace WorldRuntime{bool active=false,cold=false;}
unsigned builds=0,collects=0,releases=0,epoch=1;
struct Owner{bool live=false;void reset(){live=false;++releases;}};
struct StaPipBag{Owner worldOwner;};
struct Ptr{StaPipBag* p;StaPipBag* get(){return p;}};
struct Core{unsigned getWorldEpoch(){return epoch;}
bool prepareWorldGeometry(StaPipBag* b){GUARD if(!b)return false;++builds;b->worldOwner.live=true;return true;}
void collectWorldOwners(){++collects;}};
struct Stapip{Core core;};
struct Layer{Ptr bag;};
struct Terrain{Ptr bag,aoBag,emisBag,gsBag;std::vector<Layer> layerPasses;};
struct Proc{Ptr bag;};
struct TerrainGame{Stapip stapip;u32 worldSeenEpoch=0;bool worldSeenActive=false;
StaPipBag bags[6];std::vector<Proc> procChunks;std::vector<Terrain> terrainChunks;
TerrainGame(){procChunks.push_back({{bags}});terrainChunks.push_back({{bags+1},{bags+2},{bags+3},{bags+4},{{{bags+5}}}});}
void prepareWorldOwners();};
'''
test=r'''
int main(){TerrainGame game;game.prepareWorldOwners();assert(builds==0 && collects==1);
for(unsigned i=0;i<256;++i)game.prepareWorldOwners();assert(collects==1);
WorldRuntime::active=true;game.prepareWorldOwners();assert(builds==6 && collects==2);
for(unsigned i=0;i<256;++i)game.prepareWorldOwners();assert(builds==6 && collects==2);
assert(game.stapip.core.prepareWorldGeometry(game.bags));assert(builds==7);
WorldRuntime::active=false;game.prepareWorldOwners();assert(collects==3);
for(auto& b:game.bags)assert(!b.worldOwner.live);
assert(!game.stapip.core.prepareWorldGeometry(game.bags));assert(builds==7);
WorldRuntime::cold=true;++epoch;game.prepareWorldOwners();assert(builds==13 && collects==4);
WorldRuntime::cold=false;++epoch;game.prepareWorldOwners();assert(builds==13 && collects==5);
WorldRuntime::active=true;game.prepareWorldOwners();assert(builds==19 && collects==6);
}
'''
condition='if(epoch==worldSeenEpoch && worldSeenActive==WorldRuntime::active)return;'
assert condition in body
cases={'positive':(body,guard),
       'per-frame-sweep':(body.replace(condition,''),guard),
       'missing-role-transition':(body.replace(condition,'if(epoch==worldSeenEpoch)return;'),guard),
       'baseline-compiler-cost':(body,'')}
with tempfile.TemporaryDirectory() as folder:
    root=Path(folder)
    for name,(function,compilerGuard) in cases.items():
        src=root/(name+'.cpp');exe=root/name
        src.write_text(pre.replace('GUARD',compilerGuard)+function+test)
        subprocess.run(['g++','-std=c++17','-O2','-Wall','-Wextra','-Werror','-Wno-misleading-indentation',str(src),'-o',str(exe)],check=True)
        run=subprocess.run([str(exe)],capture_output=True)
        assert (run.returncode==0)==(name=='positive'),name
print('PASS actual A31 warm/producer logic frames=512 controls=3 role-transition=1 net-cost=1')
