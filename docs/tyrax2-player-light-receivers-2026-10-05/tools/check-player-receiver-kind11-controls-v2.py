from pathlib import Path
import json,importlib.util,hashlib
b=Path('F:/Projects/tyrax2-lab-20261001');c=b/'player-light-receivers-controls-v2';sp=importlib.util.spec_from_file_location('analysis',c/'analyze-night.py');mod=importlib.util.module_from_spec(sp);sp.loader.exec_module(mod);e=b/'night-ablation-emulator-player-receivers-kind11-order0-20261005-evidence';raw=(e/'stdout.log').read_text();a=(e/'night-ablation.log').read_text();r=mod.analyze(raw,a,'emulator',11,0,0,0);assert r['receiverModes']==[0,2,0]and r['extraDisabledMasks']==[0,1,0]
for key,row in r['extraGateWindows'].items():
 if key.startswith('1:'):assert row['1']['executed']==row['1']['submitted']==0 and row['1']['skipped']>0
 else:assert row['1']['submitted']>0
 assert row['2']['submitted']>0 and row['4']['submitted']>0
rows=[]
for name,text in [('lost_cut',raw.replace('extraMask=1 appliedExtraMask=1','extraMask=0 appliedExtraMask=0')),('wrong_mode',raw.replace('mode=2','mode=1')),('disabled_beams',raw.replace('extraMask=1 appliedExtraMask=1','extraMask=3 appliedExtraMask=3'))]:
 try:mod.analyze(text,a,'emulator',11,0,0,0)
 except (ValueError,AssertionError) as ex:rows.append(dict(name=name,rejected=True,reason=str(ex)))
 else:raise AssertionError(name)
p=b/'player-receiver-kind11-controls-v2.json';assert not p.exists();p.write_bytes((json.dumps(dict(status='PASS_ACTUAL_POOL_CUT_RECEIVERS_AND_RETAINED_EFFECTS',source=str(e),negativeControls=rows,scenePoolColdSubmits=[r['extraGateWindows'][f'{p}:750']['1']['submitted']for p in range(3)],beamCoronaColdSubmits=[r['extraGateWindows'][f'{p}:750']['2']['submitted']for p in range(3)],vehicleGlowColdSubmits=[r['extraGateWindows'][f'{p}:750']['4']['submitted']for p in range(3)]),indent=2)+'\n').encode());print('PASS')
