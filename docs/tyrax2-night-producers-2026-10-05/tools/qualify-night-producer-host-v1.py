from pathlib import Path
import importlib.util,json,hashlib,re
b=Path('F:/Projects/tyrax2-lab-20261001');c=b/'night-producer-controls-v1';sp=importlib.util.spec_from_file_location('a',c/'analyze-night.py');m=importlib.util.module_from_spec(sp);sp.loader.exec_module(m);records=[]
for order in(0,1):
 old=b/f'night-ablation-ps2-light-far-order{order}-20261005-evidence';text=(old/'stdout.log').read_text(encoding='utf8');art=(old/'night-ablation.log').read_text(encoding='utf8');text=text.replace('kind=12','kind=13').replace('NIGHTFARPICKPHASE','NIGHTPRODPHASE');text=re.sub(r'^.*NIGHTFARPICKGATES[^\n]*\n','',text,flags=re.M)
 rows=[]
 for p in range(3):
  en=int((p==1)!=(order==1))
  for o in(750,1155):
   for st in range(5):
    tc=320 if en and o==1155 and st>=2 else 0
    rows.append(f'LOG: NIGHTPROD phase={p} offset={o} stage={st} enabled={en} calls={int(st>=2)} units1=0 units2=0 units3=0 units4=0 timedCalls={tc} reads={tc*2} ticks={tc*100}\n')
 text+=''.join(rows);r=m.analyze(text,art,'host',13,order,0,0);assert r['status']=='PASS_STRICT_NIGHT_MASKED_LOOP_CAPTURE'
 bads=[text.replace('stage=0','stage=7',1),text.replace('reads=640','reads=641',1),text.replace('ticks=32000','ticks=4294967296000',1),text.replace('offset=750 stage=0','offset=751 stage=0',1),text.replace('NIGHTPROD phase=0 offset=750 stage=0','REMOVED phase=0 offset=750 stage=0',1)]
 for bad in bads:
  assert bad!=text
  try:m.analyze(bad,art,'host',13,order,0,0)
  except ValueError:pass
  else:raise AssertionError('malformed synthetic protocol accepted')
 records.append(dict(order=order,positiveAccepted=True,negativeRejected=len(bads),synthetic=True))
out=b/'night-producer-host-controls-v1.json';assert not out.exists();out.write_bytes((json.dumps(dict(status='PASS_SYNTHETIC_PRODUCER_DIALECT_CONTROLS',rows=records,runtimeAcceptance=False,parserSha256=hashlib.sha256((c/'analyze-night.py').read_bytes()).hexdigest()),indent=2)+'\n').encode());print(out)
