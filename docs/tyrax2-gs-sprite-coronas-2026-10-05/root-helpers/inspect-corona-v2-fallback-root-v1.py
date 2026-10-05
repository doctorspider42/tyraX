from pathlib import Path
import importlib.util,struct
p=Path('F:/Projects/tyrax2-lab-20261001');s=importlib.util.spec_from_file_location('d',p/'wild-gs-sprite-corona-probe-runtime-v2/decode-vu1-output.py');d=importlib.util.module_from_spec(s);s.loader.exec_module(d)
for arm,n in [(0,'state.p2s'),(1,'rejected-state.p2s')]:
 vu,m=d.extract(p/f'corona-probe-v2-case1-arm{arm}-repeat1-20261005-v2-launch'/n);b=22;cnt=d.words(vu,b)[3];kick=b+2+216+bool(cnt&0x800);state=9 if cnt&0x8000 else 1;prim=kick+state-1;t=d.tag(d.qw(vu,prim));v=[[d.qw(vu,kick+state+i*3+j) for j in range(3)]for i in range(6)];print('arm',arm,'header',hex(cnt),'type',(t>>47)&7,'loops',t&0x7fff)
 for x in v:print('STQ',struct.unpack('<4f',x[0]),'RGBA',struct.unpack('<4I',x[1]),'XYZF',struct.unpack('<4I',x[2]))
 try:print('quadpredicate',d.valid_quad(v))
 except Exception as e:print('quadpredicateFAIL',e)
