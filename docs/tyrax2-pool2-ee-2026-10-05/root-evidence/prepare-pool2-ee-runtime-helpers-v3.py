from pathlib import Path
import shutil,json,hashlib
lab=Path('F:/Projects/tyrax2-lab-20261001');out=lab/'wild-pool2-ee-root-v3'
assert not out.exists();shutil.copytree(lab/'wild-pool2-ee-root-v2',out)
freezer=(lab/'wild-pool-table-runtime-tools-v3/freeze-completed.py').read_text()
assert "fixture.name=='wild-pool-table-physical-v3'" in freezer
freezer=freezer.replace("fixture.name=='wild-pool-table-physical-v3'","fixture.name=='wild-pool2-ee-physical-v2'")
(out/'freeze-completed.py').write_bytes(freezer.encode())
for name in ('qualify-emulator.py','finish-physical.ps1','finish-physical-no-reset.ps1'):
 p=out/name;s=p.read_text();assert 'wild-pool-table-runtime-tools-v3/freeze-completed.py' in s
 s=s.replace('wild-pool-table-runtime-tools-v3/freeze-completed.py','wild-pool2-ee-root-v3/freeze-completed.py');p.write_bytes(s.encode())
p=out/'launch-emulator.py';s=p.read_text();old="s=re.sub(r'^(StartPaused|ShowOnStartup)\\s*=.*$',r'\\1 = false',s,flags=re.M);(configdir/'PCSX2.ini').write_text(s)"
new="""s=re.sub(r'^(StartPaused|ShowOnStartup)\\s*=.*$',r'\\1 = false',s,flags=re.M)
# Host-only correctness profile: suppress known missing-ALSA SDL error modal.
audio=re.search(r'^\\[SPU2/Output\\]\\n(.*?)(?=^\\[|\\Z)',s,re.M|re.S);assert audio
block,n=re.subn(r'^Backend\\s*=.*$','Backend = Null',audio.group(0),flags=re.M);assert n==1
s=s[:audio.start()]+block+s[audio.end():]
(configdir/'PCSX2.ini').write_text(s)"""
assert old in s;s=s.replace(old,new);p.write_bytes(s.encode())
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
(out/'helper-pins.json').write_bytes((json.dumps({'files':{p.name:sha(p)for p in out.iterdir()if p.is_file()and p.name!='helper-pins.json'},
 'scope':'Fixture-specific evidence closure; separate emulator host audio Null; physical ELF/cfg/audio unchanged'},indent=2)+'\n').encode())
attempt=lab/'night-ablation-emulator-pool2-ee-order0-20261005-launch'
(attempt/'root-raster-rejection.json').write_bytes((json.dumps({'status':'REJECT_PHASE_RASTER_OCCLUDED_BY_OWNED_HOST_ERROR_MODAL',
 'ownedPid':16934,'observedErrorWindow':'0x200014','observedGameWindow':'0x20000a',
 'errorGeometry':'500x128+686+480','gameGeometry':'640x480+537+310',
 'phaseImages':{p.name:sha(p)for p in attempt.glob('phase-*.png')},
 'gameCompleted5400':True,'rawEvidenceStillRequiresReparse':True,
 'helperFailure':'Inherited freeze-completed.py rejected new fixture name before creating evidence dir',
 'nextAttempt':'same ELF, fresh host Null profile; original captures preserved'},indent=2)+'\n').encode())
print('V3 helpers bind new fixture and host Null; attempt1 raster rejection preserved')
