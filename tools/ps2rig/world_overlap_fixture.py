"""Private same-ELF schedule screen: chained body SAT with interleaved world work."""
from pathlib import Path
import argparse
import json
import shutil
import subprocess
import zipfile


def install(repo, root):
    common = Path(subprocess.check_output(['git', '-C', str(repo), 'rev-parse',
        '--path-format=absolute', '--git-common-dir'], text=True).strip())
    allowed = (common.parent / '.tmp').resolve()
    assert root.is_relative_to(allowed) and root != allowed
    metadata = root / 'metadata.json'
    meta = json.loads(metadata.read_text(encoding='utf8'))
    assert meta.get('body_sweep_comparison') and meta.get('fixed_simulation_step')
    assert not meta.get('world_overlap_comparison')
    project = root / 'district'
    path = project / 'src/gen/game_physics.gen.cpp'
    source = path.read_text(encoding='utf8')
    baseline = project / 'src/gen/collision_baseline.gen.cpp'
    backup=root / 'before-world-overlap.zip'
    if backup.exists():
        with zipfile.ZipFile(backup) as z:
            for f in [path, baseline, metadata]:
                assert z.read(f.relative_to(root).as_posix())==f.read_bytes()
    else:
        with zipfile.ZipFile(backup, 'x', zipfile.ZIP_DEFLATED) as z:
            for f in [path, baseline, metadata]:
                z.write(f, f.relative_to(root))

    def replace(old, new):
        nonlocal source
        assert source.count(old) == 1, old
        source = source.replace(old, new, 1)

    # A common body index and micro backend isolate scheduling from algorithm changes.
    replace('  static BodySweepProbe::Index bodyPairs;', '''  static Tyra::BroadPhase bodyPairs;
  static int bodyPairPhase=-1;
  if(bodyPairPhase!=CB::phase){
    bodyPairs=Tyra::BroadPhase(Tyra::BroadPhase::kGrid,8.0F);
    bodyPairPhase=CB::phase;
  }''')
    replace('  const bool useMicro = false; // grid A/B: canonical EE physics in both arms',
            '  const bool useMicro = microBatch.begin(); // same backend in both schedules')
    replace('  RbCand cand[48];', '''  static std::vector<RbContact> worldContacts;
  worldContacts.clear();
  bool emittingWorld=false;
  const bool overlapWorld=!CB::baseline;
  RbCand cand[48];''')
    # Contact storage is staged in both arms so the solver receives world then body.
    replace('      contacts.push_back(c);',
            '      (emittingWorld?worldContacts:contacts).push_back(c);')
    start = source.index('  { CB::StageScope phaseScope(1);\n  // --- 2.')
    end = source.index('  { CB::StageScope phaseScope(2);', start)
    world = source[start:end]
    opening = '''  { CB::StageScope phaseScope(1);
  // --- 2. contacts against the world ----------------------------------------
  for (int si = 0; si < awakeCount; ++si) {'''
    suffix='  }\n\n  }\n'
    assert world.startswith(opening) and world.endswith(suffix)
    # A whole awake body's world work is a cooperative chunk; no shared scratch
    # survives the call, and no velocity or predicted pose is modified here.
    body = world[len(opening):-len(suffix)]
    replacement = '''  int nextWorld=0;
  auto runWorldBody = [&]() {
    if(nextWorld>=awakeCount)return;
    CB::StageScope phaseScope(1);
    const int si=nextWorld++;
    emittingWorld=true;
''' + body + '''    emittingWorld=false;
  };
  if(!overlapWorld)while(nextWorld<awakeCount)runWorldBody();
  const unsigned worldBeforeBodies=CB::stageTicks[1];

'''
    source = source[:start] + replacement + source[end:]
    replace('    if(completed>=0)consumeMicro(completed,completedCount,results);',
            '''    if(overlapWorld)runWorldBody();
    if(completed>=0)consumeMicro(completed,completedCount,results);''')
    replace('    launchMicro();finishMicro();', '''    if(queued)launchMicro();
    if(overlapWorld)while(nextWorld<awakeCount)runWorldBody();
    finishMicro();''')
    marker='''  }
  { CB::SolverScope solverScope;'''
    replace(marker, '''  }
  // World scopes were nested in body scheduling; keep outer stage accounting disjoint.
  CB::stageTicks[2]-=CB::stageTicks[1]-worldBeforeBodies;
  while(nextWorld<awakeCount)runWorldBody();
  const unsigned mergeStart=CB::ticks();
  worldContacts.insert(worldContacts.end(),contacts.begin(),contacts.end());
  contacts.swap(worldContacts);
  CB::stageTicks[2]+=CB::ticks()-mergeStart;
  { CB::SolverScope solverScope;''')
    path.write_bytes(source.encode())
    meta.update(world_overlap_comparison=True, micro_dispatch=True,
                body_sweep_comparison=False,
                candidate='Private world-contact chunks interleaved after chained VU0 body SAT launch',
                same_elf_admission_qualified=False, scene_timing_qualified=False,
                active_mode='world-overlap-unqualified',
                comparison='Both schedules: cached world,8m body grid, chained micro SAT and canonical confirmations; candidate defers world chunks until micro launch, stages world-first contacts. Six/three solver unchanged.')
    metadata.write_text(json.dumps(meta, indent=2), encoding='utf8')


def add_probe(repo, root):
    meta=json.loads((root/'metadata.json').read_text(encoding='utf8'))
    assert meta.get('world_overlap_comparison')
    path=root/'district/src/gen/game_physics.gen.cpp'
    s=path.read_text(encoding='utf8')
    assert 'world_overlap_probe.hpp' not in s
    s=s.replace('#include "debug/body_sweep_probe.hpp"',
                '#include "debug/body_sweep_probe.hpp"\n#include "debug/world_overlap_probe.hpp"',1)
    assert s.count('#include "debug/world_overlap_probe.hpp"')==1
    for old,new in [
        ('  bool emittingWorld=false;', '  bool emittingWorld=false,worldPacketActive=false;'),
        ('    emittingWorld=true;', '    emittingWorld=true;\n    const unsigned worldStart=WorldOverlapProbe::beginWorld(worldPacketActive);'),
        ('    emittingWorld=false;', '    WorldOverlapProbe::endWorld(worldPacketActive,worldStart);\n    emittingWorld=false;'),
        ('    inflight=-1;', '    inflight=-1;worldPacketActive=false;'),
        ('    microBatch.start(microJobs[filling],queued);', '    microBatch.start(microJobs[filling],queued);worldPacketActive=true;'),
        ('  contacts.swap(worldContacts);','  contacts.swap(worldContacts);\n  WorldOverlapProbe::capture(contacts);')]:
        assert s.count(old)==1,old;s=s.replace(old,new,1)
    path.write_bytes(s.encode())
    path=root/'district/src/gen/collision_baseline.gen.cpp';s=path.read_text(encoding='utf8')
    old='#include "debug/body_sweep_probe.hpp"';assert s.count(old)==1
    s=s.replace(old,old+'\n#include "debug/world_overlap_probe.hpp"',1)
    old='BodySweepProbe::flush(CB::phase);';assert s.count(old)==1
    s=s.replace(old,old+'WorldOverlapProbe::flush(CB::phase);',1);path.write_bytes(s.encode())
    shutil.copyfile(repo/'tools/ps2rig/world_overlap_probe.hpp',root/'engine/engine/inc/debug/world_overlap_probe.hpp')
    meta['world_overlap_timing_probe']=False
    (root/'metadata.json').write_text(json.dumps(meta,indent=2),encoding='utf8')


if __name__ == '__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--scratch', required=True, type=Path)
    p.add_argument('--repo', type=Path, default=Path(__file__).resolve().parents[2])
    p.add_argument('--add-probe', action='store_true', help='Upgrade an installed uninstrumented prototype')
    a=p.parse_args()
    if not a.add_probe:install(a.repo.resolve(), a.scratch.resolve())
    add_probe(a.repo.resolve(), a.scratch.resolve())
