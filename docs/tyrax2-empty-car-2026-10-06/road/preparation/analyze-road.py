"""Strict completed private closeout parser; never device/source mutation."""
from pathlib import Path
import argparse,json,math,re,statistics,struct
p=argparse.ArgumentParser();p.add_argument('stdout',type=Path);p.add_argument('--artifact',type=Path,required=True);p.add_argument('--out',type=Path,required=True);p.add_argument('--route',choices=['vehicle'],default='vehicle');a=p.parse_args()
def rows(text,label):
 result=[]
 for line in text.splitlines():
  line=re.sub(r'^\[\s*\d+\.\d+\]\s+(?=LOG: (?:CLOSEOUT|EMPTYRENDER|ROADCONTEXT))', '', line)
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
stdout=a.stdout.read_text(encoding='utf8',errors='replace');artifact=a.artifact.read_text(encoding='utf8');configs=rows(stdout,'CLOSEOUTCONFIG');dones=rows(stdout,'CLOSEOUTDONE');assert len(configs)==len(dones)==1;cfg=configs[0];done=dones[0];order=n(cfg,'order');kind=n(cfg,'kind');assert order==0 and kind==4
assert n(cfg,'valid')==n(done,'valid')==1 and n(done,'loops')==5400 and n(done,'order')==order and n(done,'kind')==kind
for k,v in dict(phases=3,phaseFrames=1800,taxFirst=800,taxLoops=320,sampleFirst=900,sampleCount=128,ordinaryClocks=1,addedWaits=0,frameProfile=0,hardwareTrace=0,devkit=0,sampleBytes=6144,chunkBytes=420).items():assert n(cfg,k)==v
phases=index(rows(stdout,'CLOSEOUTPHASE'),'phase',range(3));windows=index(rows(artifact,'CLOSEOUTWINDOW'),'phase',range(3));contexts=rows(stdout,'CLOSEOUTCONTEXT');assert len(contexts)==6
assert all(re.fullmatch(r'LOG: CLOSEOUT(CHUNK|RAW|WINDOW) .+',l)for l in artifact.splitlines())
raw=rows(artifact,'CLOSEOUTRAW');chunks=rows(artifact,'CLOSEOUTCHUNK');assert len(chunks)==15;results={}
contextmap={}
for phase in range(3):
 middle=(phase==1)!=(order==1);players=0;hidden=0;sampler=1;pr=phases[phase];wr=windows[phase]
 for k,v in dict(first=phase*1800,players=players,geometryHidden=hidden,sampler=sampler,countReads=262 if sampler else 6).items():assert n(pr,k)==v
 for k,v in dict(sampler=sampler,raw=128 if sampler else 0,chunks=5,valid=1).items():assert n(wr,k)==v
 cold=index([r for r in contexts if n(r,'phase')==phase],'offset',[750,1155]);contextmap[phase]=cold
 for r in cold.values():
  for k,v in dict(players=players,scene=0,requested=1,valid=1).items():assert n(r,k)==v
  assert n(r,'sceneGeneration')>0
  if a.route=='vehicle':
   assert n(r,'vehicleDriver')==0
  else:
   assert n(r,'vehicleDriver')==-1 and n(r,'portalOther')==0
  assert 59900<=n(r,'refreshMilliHz')<=60010 and n(r,'width')>0 and n(r,'height')>0 and n(r,'depth')in(0,1,2)
  assert n(r,'oneArm')==1 and n(r,'route')==4
  for key in (('positionX','positionY','positionZ','lookX','lookY','lookZ','playerX','playerY','playerZ')+ (('vehicleX','vehicleY','vehicleZ','vehicleSpeed')if a.route=='vehicle'else())):
   assert re.fullmatch('[0-9a-fA-F]{8}',r[key]) and (int(r[key],16)&0x7f800000)!=0x7f800000,key
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
positionKeys=('vehicleX','vehicleY','vehicleZ')if a.route=='vehicle'else('playerX','playerY','playerZ')
moved={p:any(contextmap[p][750][k]!=contextmap[p][1155][k] for k in positionKeys)for p in range(3)}
assert any(moved.values()),'No actual cold endpoint movement established; route activation missing'
renderRows=rows(stdout,'EMPTYRENDER');assert len(renderRows)==6
renderWitnesses={}
for phase in range(3):
 cold=index([r for r in renderRows if n(r,'phase')==phase],'offset',[750,1155]);renderWitnesses[phase]=cold
 for r in cold.values():assert n(r,'acceptedBags')>0 and n(r,'acceptedVertices')>0 and n(r,'billboards')==0 and n(r,'valid')==1
assert moved[1],'Actual driving phase endpoint motion missing'
def fp(r,key):
 assert re.fullmatch('[0-9a-fA-F]{8}',r[key]),key
 value=struct.unpack('<f',struct.pack('<I',int(r[key],16)))[0];assert math.isfinite(value),key
 return value
assert fp(contextmap[1][1155],'vehicleZ')>=44.0,'Driving endpoint did not reach the second bend'
roadRows=rows(stdout,'ROADCONTEXT');assert len(roadRows)==6
roadWitnesses={}
for phase in range(3):
 cold=index([r for r in roadRows if n(r,'phase')==phase],'offset',[750,1155]);roadWitnesses[phase]=cold
 for r in cold.values():
  assert n(r,'paved')==4 and n(r,'grounded')==1,'Cold wheel pavement/contact missing'
  assert abs(fp(r,'roadY')-.12)<.001 and abs(fp(r,'centerOffset'))<4.0
  assert abs(fp(r,'grip')-1.0)<.001 and abs(fp(r,'cover')-1.0)<.001
for cold in renderWitnesses.values():
 for r in cold.values():
  for key in ('roadBags','roadVertices','carBags','carVertices'):assert n(r,key)>0,key
  assert n(r,'roadBags')+n(r,'carBags')==n(r,'acceptedBags') and n(r,'roadVertices')+n(r,'carVertices')==n(r,'acceptedVertices')

portalCounts=None
if a.route=='gate':
 ordered=[contextmap[p][o] for p in range(3)for o in (750,1155)]
 portalCounts={k:[n(r,k)for r in ordered]for k in ('portal0','portal1')}
 for counts in portalCounts.values():assert counts==sorted(counts) and counts[-1]>0,'actual portal roundtrip branch activation missing'
result=dict(status='PASS_COMPLETED_PRIVATE_ROAD_AND_ONE_CAR_PROTOCOL',renderColdWitnesses=renderWitnesses,route='same-one-car-plus-authored-s-road',roadColdWitnesses=roadWitnesses,portalColdCounts=portalCounts,byPhase=results,movedAtColdEndpoints=moved,oneArm=True,matchedAB=False,hardwarePerformanceQualified=False,nativeSixtyFpsAccepted=False,commonControllerObserverCostUnpriced=True,limitations=['Cold positions/contact and accepted render counts are six sparse witnesses, not whole-run wheel path or image proof; centerOffset uses piecewise authored-anchor approximation, not exact tessellated Catmull-Rom centerline.','Phase0 is initial parked; phase1 bounded virtual-pad steering/throttle/braking; phase2 neutral post-drive, not matched savings. Invisible analytic y0 floor retains driving physics; public tessellated road supplies normal raised contact geometry. Controller changed and included in the price; no causal road-only subtraction.','Worst window is maximum observed64-loop mean, not worst frame or p99.','128 raw loops per phase do not cover all measured chunks.','Emulator qualifies controller/scene correctness only; hardware cadence remains separate.','Private quieting, controller and observer footprint remain unpriced; hardware performance needs root qualification.'])
a.out.write_text(json.dumps(result,indent=2)+'\n',encoding='utf8');print(result['status'])
