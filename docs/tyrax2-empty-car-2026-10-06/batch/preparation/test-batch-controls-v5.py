from pathlib import Path
import json,re,subprocess,hashlib
lab=Path('F:/Projects/tyrax2-lab-20261001');r=lab/'tyrax2-closeout-road-two-cars-light-buildings-80k-shadow-batch-v1';out=r/'parser-controls-v5';assert not out.exists();out.mkdir();parent=lab/'tyrax2-closeout-ps2-two-cars-light-buildings-80k-shadow-v2';source=parent/'stdout.utf8.log';artifact=parent/'bin/closeout-samples.txt';text=source.read_text(encoding='utf8');baseart=artifact.read_text(encoding='utf8')
def fields(line):return dict(x.split('=',1)for x in line.split()[2:])
baseline={}
for line in text.splitlines():
 if 'phase=0 'in line and line.startswith('LOG: ')and 'offset='in line:baseline[(line.split()[1],int(fields(line)['offset']))]=line
def synthetic(order):
 lines=[]
 for line in text.splitlines():
  if not line.startswith('LOG: '):lines.append(line);continue
  line=line.replace('order=0','order='+str(order)).replace('route=10','route=11');f=fields(line);phase=int(f.get('phase',0));on=int((phase!=1)!=(order==1));label=line.split()[1]
  if 'offset'in f:
   line=baseline[(label,int(f['offset']))].replace('phase=0','phase='+str(phase)).replace('route=10','route=11');f=fields(line)
   if label=='BUILDINGRENDER':
    line=re.sub(r'acceptedBags=\d+','acceptedBags='+str(24 if on else 300),line)
    if not on:
     for i in range(50):line=re.sub(r'\brefs'+str(i)+r'=\d+','refs'+str(i)+'='+str(7 if i%2==0 else 5),line)
   if label=='EMPTYRENDER':line=re.sub(r'acceptedBags=\d+','acceptedBags='+str(37 if on else 313),line)
   if label=='CLOSEOUTCONTEXT':lines.append(f'LOG: BATCHCONTEXT phase={phase} offset={f["offset"]} enabled={on} mappedOwners={50 if on else 0} retainedBatches=24 valid=1')
  if label=='CLOSEOUTPHASE':lines.append(f'LOG: BATCHPHASE phase={phase} enabled={on} order={order} retainedBatches=24')
  lines.append(line)
 return 'LOG: BATCHPREP owners=50 parts=300 vertices=70800 matrix=0 impostor=0 dirty=0 unmapped=0 empty=0 errors=0 authored=55 mapSize=55 runtimePool=119 geometryPool=119 valid=1\n'+'\n'.join(lines)+'\n'
proof=[]
def trial(name,s,art,expect):
 log=out/(name+'.log');af=out/(name+'.samples');log.write_bytes(s.encode());af.write_bytes(art.encode());rr=subprocess.run(['python',str(r/'analyze-batch.py'),str(log),'--artifact',str(af),'--out',str(out/(name+'.json'))],capture_output=True);assert (rr.returncode==0)==expect,(name,rr.stderr.decode(errors='replace')[-1000:]);proof.append({'name':name,'expectedAccepted':expect,'actualAccepted':rr.returncode==0})
for order in(0,1):
 s=synthetic(order);trial('positive-order'+str(order),s,baseart,True)
 broken=s.splitlines();target=next(i for i,l in enumerate(broken)if l.startswith('LOG: LIGHTCONTEXT phase=1 offset=750 '));broken[target]=re.sub(r' x=[0-9a-fA-F]{8}', ' x=3f000000',broken[target]);trial('unmatched-point'+str(order),'\n'.join(broken)+'\n',baseart,False)
 broken=s.splitlines();target=next(i for i,l in enumerate(broken)if l.startswith('LOG: CLOSEOUTCONTEXT phase=1 offset=750 '));broken[target]=re.sub(r' positionX=[0-9a-fA-F]{8}', ' positionX=3f000000',broken[target]);trial('unmatched-camera'+str(order),'\n'.join(broken)+'\n',baseart,False)
 trial('prebuild-snapshot'+str(order),s.replace('retainedBatches=24','retainedBatches=0'),baseart,False)
 trial('prebuild-owner-map'+str(order),s.replace('mappedOwners=50','mappedOwners=0'),baseart,False)
 trial('missing-solo-prep'+str(order),s.replace('LOG: BATCHPREP owners=50 parts=300 vertices=70800 matrix=0 impostor=0 dirty=0 unmapped=0 empty=0 errors=0 authored=55 mapSize=55 runtimePool=119 geometryPool=119 valid=1\n',''),baseart,False)
 trial('empty-solo-parts'+str(order),s.replace('parts=300 vertices=70800','parts=0 vertices=70800'),baseart,False)
 trial('wrong-solo-input'+str(order),s.replace('parts=300 vertices=70800','parts=300 vertices=70799'),baseart,False)
 trial('duplicate-solo-prep'+str(order),'LOG: BATCHPREP owners=50 parts=300 vertices=70800 matrix=0 impostor=0 dirty=0 unmapped=0 empty=0 errors=0 authored=55 mapSize=55 runtimePool=119 geometryPool=119 valid=1\n'+s,baseart,False)
 trial('undersized-runtime-pool'+str(order),s.replace('runtimePool=119','runtimePool=54'),baseart,False)
 trial('undersized-geometry-pool'+str(order),s.replace('geometryPool=119','geometryPool=54'),baseart,False)
 trial('wrong-authored-map'+str(order),s.replace('mapSize=55','mapSize=56'),baseart,False)
 trial('prep-error-bit'+str(order),s.replace('errors=0 authored=55','errors=2 authored=55'),baseart,False)
 trial('missing-batch'+str(order),'\n'.join(l for l in s.splitlines()if not l.startswith('LOG: BATCHCONTEXT '))+'\n',baseart,False)
 trial('mapping-broken'+str(order),s.replace('mappedOwners=50','mappedOwners=49'),baseart,False)
 trial('input-lost'+str(order),s.replace('acceptedVertices=80187','acceptedVertices=80186'),baseart,False)
 trial('lost-high-owner'+str(order),re.sub(r'\brefs49=\d+','refs49=0',s),baseart,False)
 trial('misrouted-off'+str(order),s.replace('acceptedBags=313','acceptedBags=37'),baseart,False)
 trial('missing-blob'+str(order),s.replace('acceptedVertices=54 valid=1','acceptedVertices=0 valid=1'),baseart,False)
 slow=baseart.replace('syntheticFlips=0','syntheticFlips=1');slow=re.sub(r'totalFlips=(\d+)',lambda m:'totalFlips='+str(int(m[1])+1),slow)
 trial('synthetic-flips-reported'+str(order),s,slow,True)
for name,data in [('order0.cfg','0 4\n'),('order1.cfg','1 4\n')]: (r/name).write_bytes(data.encode())
(out/'proof.json').write_bytes((json.dumps({'status':'PASS_FORTY_STATIC_BATCH_PARSER_CONTROLS','syntheticNotRuntime':True,'parserSha256':hashlib.sha256((r/'analyze-batch.py').read_bytes()).hexdigest(),'sourceParentLogSha256':hashlib.sha256(source.read_bytes()).hexdigest(),'cases':proof},indent=2)+'\n').encode());print('PASS40')

