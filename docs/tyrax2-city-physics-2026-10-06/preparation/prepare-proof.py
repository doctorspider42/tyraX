from pathlib import Path
r=Path(__file__).parent;parent=r.parent/'tyrax2-city-day-drive-v1'
s=(parent/'prove-native.py').read_text().replace('tyrax2-city-day-drive-v1','tyrax2-city-physics-v2').replace('PASS_PRIVATE_DAY_DRIVE_NATIVE_SOURCE_OBJECT_LINK_BINDING','PASS_PRIVATE_PHYSICS_PROBE_NATIVE_SOURCE_OBJECT_LINK_BINDING')
s=s.replace("('NightSampler::chunks',420)","('NightSampler::chunks',420),('VehicleProbe::samples',3072),('VehicleProbe::captured',12)")
s=s.replace("('engine/renderer/core/renderer_core',em,['inc/debug/night_sampler.hpp'])", "('engine/renderer/core/renderer_core',em,['inc/debug/night_sampler.hpp']),('game/gen/game_vehicles.gen',gm,['inc/game_runtime.gen.hpp',str(em/'inc/debug/vehicle_probe.hpp')])")
(r/'prove-native.py').write_bytes(s.encode())
s=(parent/'root-release.py').read_text().replace('tyrax2-city-day-drive-v1','tyrax2-city-physics-v2').replace('PASS_PRIVATE_DAY_DRIVE_NATIVE_SOURCE_OBJECT_LINK_BINDING','PASS_PRIVATE_PHYSICS_PROBE_NATIVE_SOURCE_OBJECT_LINK_BINDING').replace('native-build-v2.log','native-build.log')
marker="roles=[('native-proof'"
pos=s.index(marker)
extra='''assert sha(r/'tyra/engine/inc/debug/night_sampler.hpp')==sha(r.parent/'tyrax2-city-day-drive-v1/tyra/engine/inc/debug/night_sampler.hpp')
body=(game/'src/gen/game_vehicles.gen.cpp').read_text();a=body.index('void TerrainGame::stepVehicles(float dt)');b=body.index('// Speed feel',a);body=body[a:b]
assert body.count('VehicleProbe::begin(dt)')==body.count('VehicleProbe::planned')==body.count('VehicleProbe::executed()')==body.count('VehicleProbe::end()')==1
assert body.index('VehicleProbe::executed()')<body.index('updateVehicles(h)')<body.index('updateVehicleTutorial(dt)')<body.index('VehicleProbe::end()')
probe=(r/'tyra/engine/inc/debug/vehicle_probe.hpp').read_text();assert probe.count('NightSampler::ticks()')==2
assert 'phase==0 || (phase==1?(i>=40&&i<88):(i<40||i>=88))' in probe
'''
s=s[:pos]+extra+s[pos:]
s=s.replace('Representative-view diagnostic only; common observer footprint unpriced.','Added physics scope: actual executed-substep count; two bounded Count reads only when enabled, no new waits; 512 extra reads separately accounted. Parked forward/reverse short-window calibration; common metadata footprint unpriced. Representative-view diagnostic only.')
(r/'root-release.py').write_bytes(s.encode())
