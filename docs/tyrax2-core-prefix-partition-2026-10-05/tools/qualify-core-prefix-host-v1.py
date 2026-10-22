from pathlib import Path
import importlib.util,json,hashlib,re
b=Path('F:/Projects/tyrax2-lab-20261001');c=b/'core-prefix-controls-v1';sp=importlib.util.spec_from_file_location('a',c/'analyze-night.py');m=importlib.util.module_from_spec(sp);sp.loader.exec_module(m);records=[]
for order in(0,1):
 old=b/f'night-ablation-ps2-producers-order{order}-20261005-evidence';text=(old/'stdout.log').read_text(encoding='utf8');art=(old/'night-ablation.log').read_text(encoding='utf8');text=text.replace('kind=13','kind=14');text=re.sub(r'^.*NIGHTPROD [^\n]*\n','',text,flags=re.M)
 rows=[]
 for p in range(3):
  en=int((p==1)!=(order==1))
  for o in(750,1155):
   for st in range(5):
    calls=135 if st==0 else 134 if st in(1,2)else 0;tc=calls*320 if en and o==1155 else 0;units=[49794,0,1,0]if st==0 else[49000,114,0,0]if st==1 else[49000,0,0,0]if st==2 else[0,0,0,0]
    rows.append(f'LOG: NIGHTPROD phase={p} offset={o} stage={st} enabled={en} calls={calls} units1={units[0]} units2={units[1]} units3={units[2]} units4={units[3]} timedCalls={tc} reads={tc*2} ticks={tc*100}\n')
 text+=''.join(rows);r=m.analyze(text,art,'host',14,order,0,0);assert r['status']=='PASS_STRICT_NIGHT_MASKED_LOOP_CAPTURE'
 bads=[text.replace('stage=0','stage=7',1),text.replace('reads=86400','reads=86401',1),text.replace('ticks=4320000','ticks=4294967296000000',1),text.replace('offset=750 stage=0','offset=751 stage=0',1),text.replace('NIGHTPROD phase=0 offset=750 stage=0','REMOVED phase=0 offset=750 stage=0',1),text.replace('stage=3 enabled='+str(int(order==1))+' calls=0','stage=3 enabled='+str(int(order==1))+' calls=1',1),text.replace('calls=134 units1=49000 units2=114','calls=134 units1=49000 units2=135',1)]
 for bad in bads:
  assert bad!=text
  try:m.analyze(bad,art,'host',14,order,0,0)
  except ValueError:pass
  else:raise AssertionError('malformed synthetic protocol accepted')
 records.append(dict(order=order,positiveAccepted=True,negativeRejected=len(bads),synthetic=True))
out=b/'core-prefix-host-controls-v1.json';assert not out.exists();out.write_bytes((json.dumps(dict(status='PASS_SYNTHETIC_CORE_PREFIX_DIALECT_CONTROLS',rows=records,runtimeAcceptance=False,parserSha256=hashlib.sha256((c/'analyze-night.py').read_bytes()).hexdigest()),indent=2)+'\n').encode());print(out)
