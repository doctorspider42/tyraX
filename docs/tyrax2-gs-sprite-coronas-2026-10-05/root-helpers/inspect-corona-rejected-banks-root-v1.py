from pathlib import Path
import importlib.util,struct
p=Path('F:/Projects/tyrax2-lab-20261001/wild-gs-sprite-corona-probe-runtime-v1/decode-vu1-output.py');s=importlib.util.spec_from_file_location('d',p);d=importlib.util.module_from_spec(s);s.loader.exec_module(d)
for name in ['corona-probe-case1-arm0-repeat1-20261005-v1-launch/state.p2s','corona-probe-case1-arm1-repeat1-20261005-v1-launch/rejected-state.p2s']:
 vu,m=d.extract(p.parent.parent/name);print(name)
 for i in [8,12,13,14,22,483]:print(i,struct.unpack_from('<4I',vu,i*16),struct.unpack_from('<4f',vu,i*16))
