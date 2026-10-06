from pathlib import Path
import argparse,re,json,hashlib
p=argparse.ArgumentParser();p.add_argument('--log',type=Path,required=True);p.add_argument('--samples',type=Path,required=True);p.add_argument('--out',type=Path,required=True);a=p.parse_args()
def rows(text,family):
 result=[]
 for line in text.splitlines():
  line=re.sub(r'^\[\s*\d+\.\d+\]\s*','',line).rstrip('\r')
  if not line.startswith('LOG: '+family+' '):continue
  values={}
  for pair in line.split()[2:]:
   k,v=pair.split('=');assert k not in values;values[k]=int(v,16)if k.startswith(('position','look','vehicleX','vehicleY','vehicleZ','vehicleSpeed','playerX','playerY','playerZ'))else int(v)
  result.append(values)
 return result
text=a.log.read_text(errors='replace');samples=a.samples.read_text(errors='strict')
assert not re.search(r'(?:ASSERT|TRAP|ERROR:|freepad: DMA Busy)',text)
c=rows(text,'CLOSEOUTCONFIG');assert len(c)==1;c=c[0]
assert c['order']==0 and c['kind']==3 and c['valid']==1 and c['ordinaryClocks']==1 and c['addedWaits']==0 and c['frameProfile']==c['hardwareTrace']==0
done=rows(text,'CLOSEOUTDONE');assert len(done)==1 and done[0]['valid']==1 and done[0]['loops']==5400
chunks=rows(samples,'CLOSEOUTCHUNK');raw=rows(samples,'CLOSEOUTRAW');windows=rows(samples,'CLOSEOUTWINDOW')
assert len(chunks)==15 and len(raw)==384 and len(windows)==3
assert {(x['phase'],x['i'])for x in chunks}=={(ph,i)for ph in range(3)for i in range(5)}
assert {(x['phase'],x['i'])for x in raw}=={(ph,i)for ph in range(3)for i in range(128)}
assert all(x['valid']==1 and x['raw']==128 and x['chunks']==5 for x in windows)
witness=rows(text,'STRIPPACK');assert len(witness)==6
assert {(x['phase'],x['offset'])for x in witness}=={(ph,i)for ph in range(3)for i in (750,1155)}
for x in witness:
 assert x['fixed']==1 and x['batches']==28 and x['stripped']==21
 assert x['enabled']==(x['phase']!=1) and x['residentVertices']==(9096 if x['phase']==1 else 7998)
context=rows(text,'CLOSEOUTCONTEXT');assert len(context)==6
fields=['positionX','positionY','positionZ','lookX','lookY','lookZ','vehicleX','vehicleY','vehicleZ','playerX','playerY','playerZ']
reference=tuple(context[0][k]for k in fields)
for x in context:
 assert x['valid']==1 and x['route']==0 and x['scene']==0 and x['night']==0 and x['paused']==1 and x['hourZero']==1 and x['requested']==1
 assert tuple(x[k]for k in fields)==reference
results=[]
for ph in range(3):
 cc=[x for x in chunks if x['phase']==ph];rr=[x for x in raw if x['phase']==ph]
 for x in cc:
  assert x['loops']==64 and x['first']==ph*1800+800+x['i']*64 and 0<x['elapsed']<0x80000000 and 0<=x['pacing']<=x['elapsed']
  assert x['totalFlips']==x['renderedFlips']+x['syntheticFlips']and x['syntheticFlips']==0
 for x in rr:
  assert x['frame']==ph*1800+900+x['i'] and 0<x['wholeLoop']<0x80000000 and 0<=x['pacing']<=x['wholeLoop']
  assert x['totalFlips']==x['renderedFlips']+x['syntheticFlips']and x['syntheticFlips']==0
 elapsed=sum(x['elapsed']for x in cc);pacing=sum(x['pacing']for x in cc);rendered=sum(x['renderedFlips']for x in cc)
 results.append(dict(phase=ph,candidate=ph!=1,coarseLoops=320,loopMs=elapsed/320/294912,workMs=(elapsed-pacing)/320/294912,pacingMs=pacing/320/294912,renderedFlips=rendered,renderedHz=rendered*294912000/elapsed,rawWorkMs=sum(x['wholeLoop']-x['pacing']for x in rr)/128/294912,chunkWorkMs=[(x['elapsed']-x['pacing'])/64/294912 for x in cc]))
mean=(results[0]['workMs']+results[2]['workMs'])/2
report=dict(countClockHz=294912000,status='PASS_STRIP_BATCH_STATIONARY_COMPARISON',inputLogSha256=hashlib.sha256(a.log.read_bytes()).hexdigest(),inputSamplesSha256=hashlib.sha256(a.samples.read_bytes()).hexdigest(),arms=results,oldMinusCandidateWorkMs=results[1]['workMs']-mean,candidateOuterSpreadMs=abs(results[0]['workMs']-results[2]['workMs']),residentVertexReduction=1098,residentVertexReductionPercent=1098/9096*100,limitations='Elapsed EE work includes interrupts and retained common observer/selector footprint. Stationary daylight full-city comparison only; resident batch counts are not visible submitted counts. No moving/night/general60FPS claim; no physical TV/audio qualification.')
a.out.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
