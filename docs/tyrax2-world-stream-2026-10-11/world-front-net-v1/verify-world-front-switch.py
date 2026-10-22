"""Qualify actual A35 warm/producer switching with explicit storage shims."""
import argparse
import hashlib
import json
import subprocess
import tempfile
from pathlib import Path


def qualify(root):
    binding = json.loads((root / 'native-binding.json').read_text())
    paths = ['game/src/terrain_game.cpp',
             'tyra/engine/inc/renderer/3d/pipeline/static/core/stapip_core.hpp',
             'tyra/engine/src/renderer/3d/pipeline/static/core/stapip_core.cpp']
    texts = []
    for path in paths:
        data = (root / path).read_bytes()
        assert hashlib.sha256(data).hexdigest() == binding['files'][path], path
        texts.append(data.decode())
    main, header, core = texts
    body = main[main.index('void TerrainGame::prepareWorldOwners(){'):main.rfind('\n}')].strip()
    start = header.index('  bool prepareWorldGeometry(StaPipBag* b){')
    wrapper = header[start:header.index('\n  }', start) + 4]
    start = core.index('bool StaPipCore::prepareWorldTicket(')
    guard = core[core.index('  if(', start):core.index('  const u32 capacity=', start)]
    condition = 'if(epoch==worldSeenEpoch && worldSeenActive==WorldRuntime::active && frontSeenActive==WorldFront::active)return;'
    assert condition in body
    assert 'prepareWorldTicket(b);return body;' in wrapper
    assert 'WorldRuntime::active=true;WorldFront::active=ClipTrial::mode==7;prepareWorldOwners();' in main
    engine = (root / 'tyra/engine/src/engine.cpp').read_text()
    assert engine.index('MotionProbe::begin(') < engine.index('game->loop(') < engine.index('MotionProbe::end(')
    pre = r'''
#include <cassert>
#include <vector>
using u32=unsigned;
namespace WorldRuntime{bool active=true,cold=false;}
unsigned builds=0,collects=0,releases=0,tickets=0,epoch=1;
namespace WorldFront{bool active=false;unsigned live=0;constexpr unsigned OwnerLimit=1024;
struct Owner{static void releaseAll(){++releases;live=0;}};}
struct BagOwner{void reset(){}};
struct StaPipBag{BagOwner worldOwner;};
bool ticketShape(StaPipBag* b){return b!=nullptr;}
struct Ptr{StaPipBag* p;StaPipBag* get(){return p;}};
struct QBuffer{bool isVU1ClippingEnabled(){return true;}bool hasProgramOverrides(){return false;}
bool prepareWorldGeometry(StaPipBag*,unsigned){if(!WorldRuntime::active && !WorldRuntime::cold)return false;++builds;return true;}};
struct Blss{bool wantsProxies(){return false;}};
struct Renderer{Blss blss;};
struct Core{QBuffer qbufferRenderer;Renderer storage;Renderer* rendererCore=&storage;
unsigned getWorldEpoch(){return epoch;}
unsigned getMaxVertCountByBag(StaPipBag*){return 72;}
bool prepareWorldTicket(StaPipBag* b){GUARD ++tickets;++WorldFront::live;return true;}
WRAPPER
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
    test = r'''
int main(){TerrainGame game;game.prepareWorldOwners();assert(builds==6 && tickets==0 && collects==1 && releases==1);
for(unsigned i=0;i<256;++i)game.prepareWorldOwners();assert(builds==6 && tickets==0 && collects==1);
assert(game.stapip.core.prepareWorldGeometry(game.bags));assert(builds==7 && tickets==0);
WorldFront::active=true;game.prepareWorldOwners();assert(builds==13 && tickets==6 && collects==2);
for(unsigned i=0;i<256;++i)game.prepareWorldOwners();assert(builds==13 && tickets==6 && collects==2);
assert(game.stapip.core.prepareWorldGeometry(game.bags));assert(builds==14 && tickets==7);
++epoch;game.prepareWorldOwners();assert(builds==20 && tickets==13 && collects==3);
WorldFront::active=false;game.prepareWorldOwners();assert(builds==26 && tickets==13 && collects==4 && WorldFront::live==0);
assert(game.stapip.core.prepareWorldGeometry(game.bags));assert(builds==27 && tickets==13);
WorldRuntime::cold=true;++epoch;game.prepareWorldOwners();assert(builds==33 && tickets==19 && collects==5);
WorldRuntime::cold=false;++epoch;game.prepareWorldOwners();assert(builds==39 && tickets==19 && collects==6 && WorldFront::live==0);
WorldRuntime::active=false;game.prepareWorldOwners();assert(builds==39 && tickets==19 && collects==7);
assert(!game.stapip.core.prepareWorldGeometry(game.bags));
}
'''
    cases = {'positive': (body, guard),
             'per-frame-sweep': (body.replace(condition, ''), guard),
             'missing-front-transition': (body.replace(' && frontSeenActive==WorldFront::active', ''), guard),
             'baseline-ticket-cost': (body, ''),
             'retained-baseline-tickets': (body.replace('if(!WorldFront::active)WorldFront::Owner::releaseAll();', ''), guard)}
    with tempfile.TemporaryDirectory() as folder:
        temp = Path(folder)
        for name, (function, producer_guard) in cases.items():
            source, exe = temp / (name + '.cpp'), temp / name
            source.write_text(pre.replace('GUARD', producer_guard).replace('WRAPPER', wrapper) + function + test)
            subprocess.run(['g++', '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror',
                            '-Wno-misleading-indentation', '-Wno-unused-but-set-variable',
                            '-Wno-unused-parameter', str(source), '-o', str(exe)], check=True)
            run = subprocess.run([str(exe)], capture_output=True)
            assert (run.returncode == 0) == (name == 'positive'), name
    return dict(valid=True, elfSha256=binding['elfSha256'], frames=512, negativeControls=4,
                scope='actual warm helper/wrapper/guard; storage and compiler shims, not rendering proof',
                sources={path: binding['files'][path] for path in paths})


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('fixture', type=Path)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    result = qualify(args.fixture)
    text = json.dumps(result, indent=2) + '\n'
    if args.output:
        assert not args.output.exists()
        args.output.write_text(text)
    print(text, end='')
