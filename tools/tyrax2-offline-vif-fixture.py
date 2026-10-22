"""Private final-VIF experiment: native bootstrap or exact offline replay fixture."""
import argparse
import hashlib
import json
import shutil
from pathlib import Path


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('out', type=Path)
    p.add_argument('source', type=Path)
    p.add_argument('--bootstrap', action='store_true')
    p.add_argument('--compiled', type=Path)
    p.add_argument('--ps2-compiled', type=Path)
    a = p.parse_args()
    repo = Path(__file__).resolve().parent
    binding = json.loads((a.source/'native-binding.json').read_text())
    for name, digest in binding['files'].items():
        assert sha(a.source/name) == digest, name
    assert sha(a.source/'game/bin/vehicle-playground.elf') == binding['elfSha256']
    assert not a.out.exists()
    def ignore(path, names):
        return [n for n in names if n in ('obj', '.git', 'log.txt') or
                (Path(path).name == 'bin' and (n.endswith(('.csv', '.tga', '.sym')) or
                 n.startswith(('tape-', 'command-', 'cell-', 'source-', 'moving-', 'world-price-', 'triple-present-'))))]
    for relative in ('game', 'tyra/engine'):
        shutil.copytree(a.source/relative, a.out/relative, ignore=ignore)
    for name in ('tyra/Makefile.base', 'authored-manifest.json', 'offline-terrain-ps2.bin'):
        shutil.copy2(a.source/name, a.out/name)
    def edit(name, old, new):
        path = a.out/name
        text = path.read_text()
        assert text.count(old) == 1, (name, old, text.count(old))
        path.write_text(text.replace(old, new))
    engine = 'tyra/engine/'
    shutil.copy2(repo/'tyrax2-offline-vif-engine.hpp',a.out/engine/'inc/debug/offline_vif.hpp')
    qbuffer = engine+'src/renderer/3d/pipeline/static/core/stapip_qbuffer_renderer.cpp'
    path=a.out/qbuffer
    path.write_text('#include "debug/offline_vif.hpp"\n'+path.read_text())
    edit(qbuffer, '      WorldCellDirect::appendSdk(sources[p],bindings[p],body,budget);', '''      const unsigned begin=body.size();
      const auto address=bindings[p].program->getDestinationAddress();
      if(OfflineVifEngine::append){
        const auto& prim=bindings[p].prim;
        const unsigned primBits=VU_GS_PRIM(prim.type,prim.shading,unsigned(sources[p].textured),
          prim.fogging,prim.blending,prim.antialiasing,prim.mapping_type,0,prim.colorfix);
        if(!OfflineVifEngine::append(p,address,sources[p].capacity,sources[p].count,
             sources[p].run,sources[p].textured,primBits,body,budget))return false;
      }else WorldCellDirect::appendSdk(sources[p],bindings[p],body,budget);
      if(OfflineVifEngine::capture){
        OfflineVifEngine::capture->push_back(std::vector<WorldBody::Quad>(body.begin()+begin,body.end()));
        OfflineVifEngine::metadata->insert(OfflineVifEngine::metadata->end(),
          {address,sources[p].capacity,sources[p].count,sources[p].run,unsigned(sources[p].textured)});
      }
      if(OfflineVifEngine::audit && OfflineVifEngine::append){
        std::vector<WorldBody::Quad> reference;
        WorldCellDirect::appendSdk(sources[p],bindings[p],reference,budget);
        TYRA_ASSERT(reference.size()==body.size()-begin &&
          !memcmp(reference.data(),body.data()+begin,reference.size()*16),"Offline final VIF SDK mismatch");
        ++OfflineVifEngine::audits;
      }''')
    physics = 'game/src/gen/game_physics.gen.cpp'
    edit(physics, '  const int lod=terrainLodStep(cx,cz);',
         '  const int lod=OfflineTerrain::forcing?OfflineTerrain::own:terrainLodStep(cx,cz);')
    edit(physics, '  const unsigned edge[]={', '  unsigned edge[]={')
    edit(physics, '  const float* bounds=OfflineTerrain::bounds(*entry,edge);',
         '  if(OfflineTerrain::forcing){memcpy(edge,OfflineTerrain::edges,16);ch.lodW=edge[0];ch.lodE=edge[1];ch.lodN=edge[2];ch.lodS=edge[3];}\n  const float* bounds=OfflineTerrain::bounds(*entry,edge);')
    edit('game/inc/terrain_game.hpp', '  void auditOfflineTerrain();',
         '  void auditOfflineTerrain();\n  void dumpOfflineVif();')
    game = 'game/src/terrain_game.cpp'
    for name in (game,physics):
        path=a.out/name
        path.write_text('#include "offline_vif.hpp"\n'+path.read_text())
    # Context is scoped to the synchronous compiler; the resulting DMA bytes
    # retain the existing WorldOwner lifetime and queue leases.
    edit(game, '  return stapip.core.compileCompleteWorldCell(ch.completeCell,ordered);',
         '  OfflineVif::Context offline(ch.cx,ch.cz,ch.lod,ch.lodW,ch.lodE,ch.lodN,ch.lodS);\n  return stapip.core.compileCompleteWorldCell(ch.completeCell,ordered);')
    # Indentation in the inherited fixture may differ.
    if a.bootstrap:
        (a.out/'game/inc/offline_vif.hpp').write_text('#pragma once\n#include "debug/offline_vif.hpp"\nnamespace OfflineVif { struct Context { Context(int,int,int,int,int,int,int){} }; }\n')
        edit(game, 'OfflineTerrain::load();auditOfflineTerrain();', 'OfflineTerrain::load();dumpOfflineVif();')
        path = a.out/physics
        path.write_text(path.read_text()+'\n'+(repo/'tyrax2-offline-vif-dump.inc').read_text())
    else:
        assert a.compiled and a.ps2_compiled
        # Same checked table ABI, two geometry streams plus four empty streams.
        header = (repo/'tyrax2-offline-terrain-runtime.hpp').read_text()
        header = header.replace('OfflineTerrain', 'OfflineVifTables').replace('offline-terrain.bin','offline-vif.bin').replace('OFFLINELOAD','VIFTABLELOAD')
        (a.out/'game/inc/offline_vif_tables.hpp').write_text(header)
        shutil.copy2(repo/'tyrax2-offline-vif-runtime.hpp',a.out/'game/inc/offline_vif.hpp')
        edit(game, 'OfflineTerrain::load();auditOfflineTerrain();', 'OfflineTerrain::load();OfflineVif::load();auditOfflineTerrain();')
        edit(game, '  WorldCellComplete::directAudit=MotionProbe::phase<6 && MotionProbe::offset==1100;',
             '  OfflineVifEngine::audit=MotionProbe::phase<6 && MotionProbe::offset==1100;\n  WorldCellComplete::directAudit=MotionProbe::phase<6 && MotionProbe::offset==1100;')
        edit('game/inc/world_moving.hpp', 'OfflineTerrain::restores=OfflineTerrain::fallbacks=0;',
             'OfflineTerrain::restores=OfflineTerrain::fallbacks=0;OfflineVifEngine::restores=OfflineVifEngine::audits=0;')
        edit('game/inc/world_moving.hpp', '#include "offline_terrain.hpp"', '#include "offline_terrain.hpp"\n#include "debug/offline_vif.hpp"')
        edit('game/inc/world_moving.hpp', '  printf("OFFLINEPHASE',
             '  printf("VIFPHASE phase=%u enabled=%u restores=%u audits=%u\\n",phase,unsigned(OfflineTerrain::enabled),OfflineVifEngine::restores,OfflineVifEngine::audits);\n  printf("OFFLINEPHASE')
        reports = {}
        for platform, source, target in [('emulator',a.compiled,a.out/'game/bin/offline-vif.bin'),('ps2',a.ps2_compiled,a.out/'offline-vif-ps2.bin')]:
            report = json.loads((source/'report.json').read_text())
            assert report['valid'] and report['oraclePlatform']==platform
            assert sha(source/'offline-terrain.bin')==report['outputSha256']
            shutil.copy2(source/'offline-terrain.bin', target)
            shutil.copy2(source/'metadata.json', a.out/('vif-metadata-'+platform+'.json'))
            reports[platform] = report
        # Metadata only contains structural/program bindings and is identical.
        assert (a.out/'vif-metadata-emulator.json').read_bytes()==(a.out/'vif-metadata-ps2.json').read_bytes()
        metadata = json.loads((a.out/'vif-metadata-emulator.json').read_text())
        rows = ',\n'.join('  {'+','.join(map(str,row))+'}' for row in metadata['entries'])
        (a.out/'game/inc/offline_vif_bindings.hpp').write_text('#pragma once\nnamespace OfflineVif {\ninline const unsigned bindings[][11]={\n'+rows+'\n};\n}\n')
    manifest = json.loads((a.source/'world-cell-price-manifest.json').read_text())
    manifest.update(status='UNQUALIFIED', finalVifOffline=not a.bootstrap, vifBootstrap=a.bootstrap,
                    vifGeneratorSha256=sha(Path(__file__)), baseElfSha256=binding['elfSha256'])
    if not a.bootstrap:
        manifest.update(vifReports=reports, vifBinaryByPlatform={k:v['outputSha256'] for k,v in reports.items()},
                        vifRuntimeSha256=sha(repo/'tyrax2-offline-vif-runtime.hpp'),
                        vifEngineSha256=sha(repo/'tyrax2-offline-vif-engine.hpp'))
        manifest['limits'].append('Final geometry VIF/UNPACK/kick bytes are offline; camera/clip state and material/VRAM bindings remain live. Raw offline terrain is enabled in the same role, so net saving includes both stages.')
    (a.out/'world-cell-price-manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
    print(a.out)


if __name__=='__main__':
    main()
