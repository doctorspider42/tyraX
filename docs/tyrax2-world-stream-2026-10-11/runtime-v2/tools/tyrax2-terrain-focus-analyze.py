"""A28 travel, quiet transport, held terrain contracts and raster qualification."""
from pathlib import Path
base = Path(__file__).with_name('tyrax2-fixed-header-analyze.py').read_text()
finish = "exec(compile(source,str(Path(__file__)),'exec'))"
assert base.count(finish) == 1
exec(compile(base.replace(finish, ''), str(Path(__file__)), 'exec'))
replace("plan['fixedHeader'] and plan['experiment']=='A26'",
        "plan['terrainFocus'] and not plan['fixedHeader'] and plan['experiment']=='A28'")
replace("assert 'A26 fixed header differs from SDK auto close' in program", """assert 'A26 fixed header differs from SDK auto close' in program and '  if(false){' in program
probe=(root/'tyra/engine/inc/debug/motion_probe.hpp').read_text()
game=(root/'game/src/terrain_game.cpp').read_text()
physics=(root/'game/src/gen/game_physics.gen.cpp').read_text()
assert 'OrdinaryLog::enabled=offset<gasStart || finishPending;' in probe
assert game.index('TerrainFocus::x=cameraLookAt.x;TerrainFocus::z=cameraLookAt.z;')<game.index('if (scriptCtx.shakeAmp >= 0.0F)')
reverse=plan.get('reverse',False)
assert 'stableTerrainFocus=ClipTrial::mode'+('!=7' if reverse else '==7')+' || TerrainFocus::checkpoint' in physics
assert 'frame<32' in physics and 'ClipConsumer::observe=frame==31' in physics
assert 'A28 stale held-focus terrain/edge contract' in physics
""")
replace("report['consumerInputEqual']=True;report['headerOracle']=checks;report['inputs']=inputs", r'''def focusEvidence(log):
 windows=re.findall(r'LOGWINDOWBEGIN phase=(\d+) quiet=1 mode=(\d+)[^\n]*\n(.*?)LOGWINDOWEND phase=(\d+)',log,re.S)
 assert len(windows)==count
 for phase,(start,mode,body,end) in enumerate(windows):
  assert int(start)==int(end)==phase and int(mode)==plan['modes'][phase]
  assert not re.search(r'LOG: |open name host:/log.txt flag 302',body)
 contracts=re.findall(r'TERRAINFOCUSCHECK phase=(\d+) arm=(\d+) checks=(\d+) x=([^ ]+) z=([^\r\n]+)',log)
 assert len(contracts)==count*5
 for phase in range(count):
  group=[v for v in contracts if int(v[0])==phase]
  assert [int(v[1]) for v in group]==list(range(5))
  assert all(int(v[2])==25 and v[2:]==group[0][2:] for v in group)
  assert all(math.isfinite(float(v[3])) and math.isfinite(float(v[4])) and abs(float(v[3]))<.000001 for v in group)
 return contracts
report['heldTerrainContracts']=focusEvidence(text)
for bad in (text.replace('LOGWINDOWBEGIN phase=0','MISSINGWINDOW phase=0'),
 text.replace('LOGWINDOWBEGIN phase=1 quiet=1 mode=7','LOGWINDOWBEGIN phase=1 quiet=1 mode=7\nLOG: injected'),
 text.replace('TERRAINFOCUSCHECK phase=0 arm=1','MISSINGFOCUS phase=0 arm=1'),
 text.replace('TERRAINFOCUSCHECK phase=0 arm=1 checks=25','TERRAINFOCUSCHECK phase=0 arm=1 checks=24')):
 try:focusEvidence(bad)
 except AssertionError:pass
 else:raise AssertionError('A28 focus/quiet negative escaped')
report['negativeControls']+=['missing-quiet-window','ordinary-output-in-gas','missing-terrain-contract','missing-resident-chunk']
report['quietBothArms']=True
report['consumerInputEqual']=True;report['headerOracle']=checks;report['inputs']=inputs''')
replace('fixed-header-{platform}.json', 'terrain-focus-{platform}.json')
replace("output=a.output or root/f'terrain-focus-{platform}.json'", """report['reverse']=reverse
report['phaseRoles']=[('candidate' if mode!=7 else 'baseline') if reverse else ('candidate' if mode==7 else 'baseline') for mode in plan['modes']]
if a.physical and reverse:
 contrasts=[]
 for base in (0,half):
  control=phases[base+1];candidates=[phases[base],phases[base+2]]
  contrasts.append(dict(hour=control['hour'],mode=0,
   savedMeanMs=control['meanWorkMs']-statistics.mean(r['meanWorkMs'] for r in candidates),
   savedSpatialMs=statistics.mean(control['spatialMeanWorkMs'])-statistics.mean(statistics.mean(r['spatialMeanWorkMs']) for r in candidates),
   candidateP95Ms=max(r['p95WorkMs'] for r in candidates),maxControlP95Ms=control['p95WorkMs'],
   eachCandidateFaster=all(r['meanWorkMs']<control['meanWorkMs'] and statistics.mean(r['spatialMeanWorkMs'])<statistics.mean(control['spatialMeanWorkMs']) for r in candidates)))
 report['contrasts']=contrasts
 report['firstOrderGate']=all(c['savedMeanMs']>0 and c['savedSpatialMs']>0 and c['candidateP95Ms']<=c['maxControlP95Ms']+.15 and c['eachCandidateFaster'] for c in contrasts)
output=a.output or root/f'terrain-focus-{platform}.json'""")
replace('Same output; fixed header close; elapsed net work, not pure EE cycles',
        'Stable pre-shake terrain focus; held-focus exact output only; moving LOD transitions may differ; elapsed net work, not pure EE cycles')
exec(compile(source, str(Path(__file__)), 'exec'))
