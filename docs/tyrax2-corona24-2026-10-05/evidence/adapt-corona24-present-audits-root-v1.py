from pathlib import Path
import shutil
lab=Path('/mnt/f/Projects/tyrax2-lab-20261001')
for name,new in [('audit-corona24-probe-native-root-v1.py','audit-corona24-probe-native-root-v2.py'),('compile-corona24-abi-root-v1.py','compile-corona24-abi-root-v2.py')]:
 t=(lab/name).read_text().replace('corona24-probe-physical-v1','corona24-probe-physical-v2')
 if name.startswith('audit'):
  marker="probeSource="
  extra='''original=read(pin(F.parent/'corona24-probe-physical-v1/target-source-manifest.json'))
assert set(original['files'])==set(m['files'])
assert {k for k,h in m['files'].items()if h!=original['files'][k]}=={'game/inc/pool2_static_probe.hpp'}
assert 'for(unsigned observer=0;observer<3;++observer)graph_wait_vsync();' in (F/'game/inc/pool2_static_probe.hpp').read_text()
'''
  t=t.replace(marker,extra+marker)
 (lab/new).write_text(t)
root=lab/'corona24-probe-runtime-v2';assert not root.exists()
shutil.copytree(lab/'corona24-probe-runtime-v1',root,ignore=shutil.ignore_patterns('__pycache__'))
for p in root.glob('*.py'):
 t=p.read_text().replace('corona24-probe-physical-v1','corona24-probe-physical-v2').replace('232<=a.slot<=243','244<=a.slot<=251').replace('corona-probe-v3-case','corona-probe-v4-case').replace('20261005-v3','20261005-v4');p.write_text(t)
print('Present observer audit/runtime overlays ready')
