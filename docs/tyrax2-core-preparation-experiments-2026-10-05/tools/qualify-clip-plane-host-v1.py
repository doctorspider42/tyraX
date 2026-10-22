from pathlib import Path
import importlib.util,json,hashlib,re
b=Path('F:/Projects/tyrax2-lab-20261001');c=b/'clip-plane-controls-v1';sp=importlib.util.spec_from_file_location('a',c/'analyze-night.py');m=importlib.util.module_from_spec(sp);sp.loader.exec_module(m);records=[]
for order in(0,1):
 old=b/('night-ablation-ps2-core-prefix-order0-20261005-retry1-evidence' if order==0 else 'night-ablation-ps2-core-prefix-order1-20261005-evidence');text=(old/'stdout.log').read_text(encoding='utf8');art=(old/'night-ablation.log').read_text(encoding='utf8');text=text.replace('kind=14','kind=17');text=re.sub(r'^.*NIGHTPROD [^\n]*\n','',text,flags=re.M)
 rows=[]
 for p in range(3):
  en=int((p==1)!=(order==1))
  for o in(750,1155):
   for st in range(5):
    calls=244 if st==0 else 0;tc=0;units=[calls if en else 0,calls if en else 0,0,0]
    rows.append(f'LOG: NIGHTPROD phase={p} offset={o} stage={st} enabled={en} calls={calls} units1={units[0]} units2={units[1]} units3={units[2]} units4={units[3]} timedCalls={tc} reads={tc*2} ticks={tc*100}\n')
 text+=''.join(rows)+'LOG: NIGHTKEYORACLE cases=4104 mismatches=0 alignment=64\n';r=m.analyze(text,art,'host',17,order,0,0);assert r['status']=='PASS_STRICT_NIGHT_MASKED_LOOP_CAPTURE'
 bads=[text.replace('stage=0','stage=7',1),text.replace('reads=0','reads=1',1),text.replace('ticks=0','ticks=1',1),text.replace('offset=750 stage=0','offset=751 stage=0',1),text.replace('NIGHTPROD phase=0 offset=750 stage=0','REMOVED phase=0 offset=750 stage=0',1),text.replace('units3=0','units3=1',1),text.replace('units4=0','units4=1',1)]
 for bad in bads:
  assert bad!=text
  try:m.analyze(bad,art,'host',17,order,0,0)
  except ValueError:pass
  else:raise AssertionError('malformed synthetic protocol accepted')
 records.append(dict(order=order,positiveAccepted=True,negativeRejected=len(bads),synthetic=True))
out=b/'clip-plane-host-controls-v1.json';assert not out.exists();out.write_bytes((json.dumps(dict(status='PASS_SYNTHETIC_CLIP_PLANE_DIALECT_CONTROLS',rows=records,runtimeAcceptance=False,parserSha256=hashlib.sha256((c/'analyze-night.py').read_bytes()).hexdigest()),indent=2)+'\n').encode());print(out)
