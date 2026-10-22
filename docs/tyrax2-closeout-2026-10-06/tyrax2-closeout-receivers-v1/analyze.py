"""Strict completed private closeout parser; never device/source mutation."""
from pathlib import Path
import argparse,json,math,re,statistics
p=argparse.ArgumentParser();p.add_argument('stdout',type=Path);p.add_argument('--artifact',type=Path,required=True);p.add_argument('--out',type=Path,required=True);a=p.parse_args()
def rows(text,label):
 result=[]
 for line in text.splitlines():
  line=re.sub(r'^\[\s*\d+\.\d+\]\s+(?=LOG: CLOSEOUT)', '', line)
  if not line.startswith('LOG: '+label+' '):continue
  fields=line.split()[2:];r={}
  for pair in fields:
   assert '='in pair,pair;k,v=pair.split('=',1);assert k not in r;k.strip();r[k]=v
  result.append(r)
 return result
n=lambda r,k:int(r[k],10)
def index(rs,key,expected):
 assert len(rs)==len(expected);result={n(r,key):r for r in rs};assert len(result)==len(rs)and set(result)==set(expected);return result
def stats(values):
 if not values:return None
 v=sorted(x/294912 for x in values)
 def percentile(p):
  f=(len(v)-1)*p;lo=int(f);hi=math.ceil(f);return v[lo]+(v[hi]-v[lo])*(f-lo)
 return dict(count=len(v),meanMs=statistics.mean(v),p50Ms=percentile(.5),p95Ms=percentile(.95),maxMs=max(v),over16_68=sum(x>16.68 for x in v),over16_68Percent=100*sum(x>16.68 for x in v)/len(v),percentileMethod='linear interpolation over sorted raw values')
stdout=a.stdout.read_text(encoding='utf8',errors='replace');artifact=a.artifact.read_text(encoding='utf8');configs=rows(stdout,'CLOSEOUTCONFIG');dones=rows(stdout,'CLOSEOUTDONE');assert len(configs)==len(dones)==1;cfg=configs[0];done=dones[0];order=n(cfg,'order');kind=n(cfg,'kind');assert order in(0,1)and kind in(0,1,2)
assert n(cfg,'valid')==n(done,'valid')==1 and n(done,'loops')==5400 and n(done,'order')==order and n(done,'kind')==kind
for k,v in dict(phases=3,phaseFrames=1800,taxFirst=800,taxLoops=320,sampleFirst=900,sampleCount=128,ordinaryClocks=1,addedWaits=0,frameProfile=0,hardwareTrace=0,devkit=0,sampleBytes=6144,chunkBytes=420).items():assert n(cfg,k)==v
phases=index(rows(stdout,'CLOSEOUTPHASE'),'phase',range(3));windows=index(rows(artifact,'CLOSEOUTWINDOW'),'phase',range(3));contexts=rows(stdout,'CLOSEOUTCONTEXT');assert len(contexts)==6
assert all(re.fullmatch(r'LOG: CLOSEOUT(CHUNK|RAW|WINDOW) .+',l)for l in artifact.splitlines())
raw=rows(artifact,'CLOSEOUTRAW');chunks=rows(artifact,'CLOSEOUTCHUNK');assert len(chunks)==15;results={}
contextmap={}
for phase in range(3):
 middle=(phase==1)!=(order==1);players=int(kind==2 or(kind==0 and middle));hidden=int(kind==2 and middle);sampler=int(kind!=1 or middle);pr=phases[phase];wr=windows[phase]
 for k,v in dict(first=phase*1800,players=players,geometryHidden=hidden,sampler=sampler,countReads=262 if sampler else 6).items():assert n(pr,k)==v
 for k,v in dict(sampler=sampler,raw=128 if sampler else 0,chunks=5,valid=1).items():assert n(wr,k)==v
 cold=index([r for r in contexts if n(r,'phase')==phase],'offset',[750,1155]);contextmap[phase]=cold
 for r in cold.values():
  for k,v in dict(players=players,scene=0,night=1,paused=1,hourZero=1,requested=1,valid=1).items():assert n(r,k)==v
  assert n(r,'vehicleDriver')>=0 and n(r,'sceneGeneration')>0
  assert 59900<=n(r,'refreshMilliHz')<=60010 and n(r,'width')>0 and n(r,'height')>0 and n(r,'depth')in(0,1,2)
  assert [r[k].lower()for k in ('positionX','positionY','positionZ','lookX','lookY','lookZ')]==['00000000','4028b436','c2a19b64','00000000','3f9a5e32','c2949b64']
  dt=int(r['dtBits'],16);assert 0<dt<0x7f800000
  if kind==2:
   for k,v in dict(geometryMatched=11,geometryReady=1,geometryRejected=0,visibleMask=0 if hidden else 2047,activeMask=2047,savedMask=2047).items():assert n(r,k)==v
 rs=index([r for r in raw if n(r,'phase')==phase],'i',range(128)if sampler else[]);cs=index([r for r in chunks if n(r,'phase')==phase],'i',range(5));elapsed=pacing=rendered=synthetic=0
 values={k:[]for k in ('entryPeriod','wholeLoop','nonPacingLoop','singleRenderedPeriod')}
 for i,r in cs.items():
  assert n(r,'first')==phase*1800+800+i*64 and n(r,'loops')==64
  e,w=n(r,'elapsed'),n(r,'pacing');assert 0<=w<=e<0x80000000;assert n(r,'totalFlips')==n(r,'renderedFlips')+n(r,'syntheticFlips');elapsed+=e;pacing+=w;rendered+=n(r,'renderedFlips');synthetic+=n(r,'syntheticFlips')
 for i,r in rs.items():
  assert n(r,'frame')==phase*1800+900+i
  for k in ('entryPeriod','wholeLoop','pacing','presentPeriod','renderedPeriod'):assert 0<=n(r,k)<0x80000000
  assert n(r,'pacing')<=n(r,'wholeLoop')and n(r,'totalFlips')==n(r,'renderedFlips')+n(r,'syntheticFlips');assert n(r,'lastKind')in(0,1,2)
  for k in ('entryPeriod','wholeLoop'):values[k].append(n(r,k))
  values['nonPacingLoop'].append(n(r,'wholeLoop')-n(r,'pacing'))
  if n(r,'renderedFlips')==1:values['singleRenderedPeriod'].append(n(r,'renderedPeriod'))
 results[phase]=dict(players=players,geometryHidden=hidden,sampler=sampler,chunkMeanNonPacingMs=(elapsed-pacing)/320/294912,chunkMeanEntryMs=elapsed/320/294912,perChunkMeanEntryMs=[n(cs[i],'elapsed')/64/294912 for i in range(5)],maxObservedChunkMeanEntryMs=max(n(c,'elapsed')/64/294912 for c in cs.values()),renderedCompletions=rendered,syntheticCompletions=synthetic,rawStats={k:stats(v)for k,v in values.items()})
assert len(raw)==sum(128 if r['sampler']else 0 for r in results.values())
for key in ('video','display','depth','width','height','refreshMilliHz','sceneGeneration','vehicleDriver'):assert len({r[key]for r in contexts})==1,key
outer=(results[0]['chunkMeanNonPacingMs']+results[2]['chunkMeanNonPacingMs'])/2;delta=results[1]['chunkMeanNonPacingMs']-outer
result=dict(status='PASS_COMPLETED_PRIVATE_CLOSEOUT_PROTOCOL',order=order,kind=kind,byPhase=results,outerControlSpreadMs=abs(results[2]['chunkMeanNonPacingMs']-results[0]['chunkMeanNonPacingMs']),middleMinusOuterMeanNonPacingMs=delta,enabledMinusDisabledMeanNonPacingMs=delta if order==0 else-delta,hardwarePerformanceQualified=False,nativeSixtyFpsAccepted=False,publicCommonCodeCostUnpriced=True,compiledOutTraceFootprintUnpriced=True,limitations=['Raw tails use128loops per enabled phase, not all5400 and not64loop chunk percentiles.','Loop intervals and flip-return cadence are not photon cadence. Pipelining retains deferred hardware waits.','Sampler calibration is net bracket effect, not a per-sample uniform correction; common gates/buffers/metadata remain.','Ordinary dt and world/audio/particle evolution can drift despite exact frozen camera and fixed night.','Geometry kind2 intentionally removes eleven authored objects; not equivalent-output optimization.'])
a.out.write_text(json.dumps(result,indent=2)+'\n',encoding='utf8');print(result['status'])
