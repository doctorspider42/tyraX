from pathlib import Path
import json,hashlib
r=Path('/mnt/f/Projects/tyrax2-lab-20261001/tyrax2-city-physics-v2');p=r/'vehicle';sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
native=json.loads((p/'native-proof.json').read_text());source=json.loads((p/'source-review.json').read_text())
assert native['status']=='PASS_PRIVATE_PHYSICS_PROBE_NATIVE_SOURCE_OBJECT_LINK_BINDING'
assert native['sourceReviewSha256']==sha(p/'source-review.json')
for name,digest in source['files'].items():
 f=p/'game'/name[5:] if name.startswith('game/')else r/name
 assert sha(f)==digest
for m in native['actualSourceMirrors'].values():assert sha(Path(m['actualPath']))==m['sha256']
for m in native['objects'].values():assert sha(Path(m['actualObject']))==m['sha256']
game=p/'game';elf=game/'bin/vehicle-playground.elf';assert sha(elf)==native['actualELFSha256']
assert 'TYRA_FRAME_PROFILE=0 -DTYRA_HARDWARE_TRACE=0 -DTYRAX_PRIVATE_QUIET_LOGS=1' in (p/'native-build.log').read_text()
engine=(r/'tyra/engine/src/engine.cpp').read_text();assert engine.index('Closeout::begin')<engine.index('pad.update()')<engine.index('game->loop()')<engine.index('Closeout::after')
header=(r/'tyra/engine/inc/debug/closeout.hpp').read_text();assert 'restricted=false;' in header
controller=(game/'inc/motion_route.hpp').read_text();assert 'route == 1 && frame < 1800U' in controller
terrain=(game/'src/terrain_game.cpp').read_text();assert 'dayFrame==1700' in terrain and 'daynight::g_hour != 12.0F' in terrain
assert sha(r/'tyra/engine/inc/debug/night_sampler.hpp')==sha(r.parent/'tyrax2-city-day-drive-v1/tyra/engine/inc/debug/night_sampler.hpp')
body=(game/'src/gen/game_vehicles.gen.cpp').read_text();a=body.index('void TerrainGame::stepVehicles(float dt)');b=body.index('// Speed feel',a);body=body[a:b]
assert body.count('VehicleProbe::begin(dt)')==body.count('VehicleProbe::planned')==body.count('VehicleProbe::executed()')==body.count('VehicleProbe::end()')==1
assert body.index('VehicleProbe::executed()')<body.index('updateVehicles(h)')<body.index('updateVehicleTutorial(dt)')<body.index('VehicleProbe::end()')
probe=(r/'tyra/engine/inc/debug/vehicle_probe.hpp').read_text();assert probe.count('NightSampler::ticks()')==2
assert 'phase==0 || (phase==1?(i>=40&&i<88):(i<40||i>=88))' in probe
roles=[('native-proof',p/'native-proof.json'),('source-review',p/'source-review.json'),('elf',elf),('symbol',Path(str(elf)+'.sym')),('sample-config',game/'bin/closeout.cfg')]
roles += [('asset',f) for f in (game/'bin').rglob('*') if f.is_file() and f not in [x[1] for x in roles] and f.name not in ('log.txt','closeout-samples.txt','ps2link.run')]
review=dict(status='ROOT_REVIEWED_CLOSEOUT_INPUTS',rootReviewed=True,noAddedWaitsReviewed=True,ordinarySamplerOnlyReviewed=True,sourceFrozenByRoot=True,fixture=str(game),inputs=[dict(role=role,path=str(f),sha256=sha(f))for role,f in roles],reviewScope='Primary agent inspected sampler and engine hooks, sparse midpoint stores, completed-chunk selection, one-time cold car relocation, parked camera override after normal camera update, unchanged physical logic and profiles compiled out. Added physics scope: actual executed-substep count; two bounded Count reads only when enabled, no new waits; 512 extra reads separately accounted. Parked forward/reverse short-window calibration; common metadata footprint unpriced. Representative-view diagnostic only. Parent source base5b6cd898; later public difference is disabled frame_profile scope helper only. No exact moving/park subtraction claim.')
(r/'root-review.json').write_text(json.dumps(review,indent=2)+'\n')
print('ROOT_REVIEWED_CLOSEOUT_INPUTS',len(roles),sha(elf))
