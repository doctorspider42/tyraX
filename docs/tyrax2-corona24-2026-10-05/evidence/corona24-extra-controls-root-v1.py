from pathlib import Path
import subprocess,importlib.util,struct,json,hashlib,tempfile
lab=Path('/mnt/f/Projects/tyrax2-lab-20261001');root=lab/'corona24-probe-runtime-v1'
s=importlib.util.spec_from_file_location('d',root/'decode-vu1-output.py');d=importlib.util.module_from_spec(s);s.loader.exec_module(d)
# Shared all-six Z must still be reachable by binary32 FTOI4, not just equal.
vs=[];xy=[(100,100),(200,100),(200,200),(100,100),(200,200),(100,200)]
for x,y in xy:vs.append([struct.pack('<4f',0,0,.5,0),struct.pack('<4I',1,2,3,128),struct.pack('<4I',x,y,0x1000001,191<<4)])
try:d.valid_quad(vs);raise AssertionError('Unreachable float-collision integer accepted')
except ValueError as e:assert 'reachable Z' in str(e)
for v in vs:v[2]=struct.pack('<4I',*struct.unpack('<2I',v[2][:8]),0x1000000,191<<4)
assert d.valid_quad(vs)==(0,2)
# Postprocessor must refuse a changed allocation/schedule, never guess VF00.
patch=lab/'corona24-probe-physical-v1/tyra/engine/src/renderer/3d/pipeline/static/core/programs/cull/corona_flag_interlock.py'
original=lab/'wild-gs-sprite-corona-tc-native-v23-v6/artifacts/stapip_cull_tc_vu1.o.vsm'
with tempfile.TemporaryDirectory() as td:
 p=Path(td)/'changed.vsm';p.write_bytes(original.read_bytes())
 r=subprocess.run(['python3',str(patch),str(p)],capture_output=True)
 assert r.returncode!=0 and b'AssertionError' in r.stderr
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
q=dict(status='PASS_CORONA24_UNREACHABLE_Z_AND_CHANGED_SCHEDULE_NEGATIVE_CONTROLS',inputPins={str(p):sha(p)for p in (Path(__file__),root/'decode-vu1-output.py',patch,original)},runtimeAccepted=False)
(lab/'corona24-extra-controls-root-v1.json').write_text(json.dumps(q,indent=2)+'\n');print(q['status'])
