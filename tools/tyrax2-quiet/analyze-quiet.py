"""PRIVATE strict completed quiet sampler; no source/device mutation."""
from pathlib import Path
import argparse,json,re,statistics,hashlib
class Invalid(ValueError):pass
def need(test,message):
 if not test:raise Invalid(message)
def rows(text,label):
 out=[]
 for line in text.splitlines():
  m=re.search(r'LOG: '+label+r' (.*)',line)
  if not m:continue
  row={}
  for token in m[1].split():
   need('=' in token,'malformed '+label);k,v=token.split('=',1);need(k not in row,'duplicate field '+k);row[k]=v
  out.append(row)
 return out
def number(row,key):
 v=row.get(key,'');need(re.fullmatch('[0-9]+',v) is not None,'u32 '+key);n=int(v);need(n<=0xffffffff,'u32 overflow '+key);return n
def unique(xs,key,expected):
 out={}
 for row in xs:
  n=number(row,key);need(n not in out,'duplicate identity '+key);out[n]=row
 need(set(out)==set(expected),'incomplete identities '+key);return out
def stats(values):
 values=sorted(values);need(bool(values),'empty stats');n=len(values)
 return {'samples':n,'mean_ticks':statistics.mean(values),'median_ticks':statistics.median(values),'p95_ticks':values[min(n-1,(95*n+99)//100-1)],'max_ticks':values[-1],'mean_ms':statistics.mean(values)/294912,'above_16_667ms':sum(v>294912*1000/60 for v in values),'above_33_333ms':sum(v>294912*1000/30 for v in values)}
def analyze(stdout,artifact,order,environment):
 need(not re.search(r'LOG: (SIZING|SEND|REPLAY|BAGMETRIC|TLCAL|DISPATCH)',stdout+artifact),'stale diagnostic protocol')
 need(not re.search(r'HWTRACE complete|HWTRACE configuration_rejected',stdout),'trace capture active')
 need(not any(rows(stdout,k) for k in ('QUIETRAW','QUIETCHUNK','QUIETWINDOW')),'file rows leaked into stdout')
 for line in artifact.splitlines():need(re.fullmatch(r'LOG: (QUIETRAW|QUIETCHUNK|QUIETWINDOW) .+',line) is not None,'unexpected authoritative artifact row')
 config=rows(stdout,'QUIETCONFIG');done=rows(stdout,'QUIETDONE');need(len(config)==len(done)==1,'unique complete config/done')
 for k,v in {'order':order,'valid':1,'profile':0,'hardwareTrace':0,'ringBytes':0,'ordinaryClocks':1,'fixedDt':0,'sampleBytes':6144,'chunkBytes':420,'phases':3,'phaseFrames':1800,'taxFirst':800,'taxLoops':320,'chunkFrames':64,'sampleFirst':900,'sampleCount':128}.items():need(number(config[0],k)==v,'config '+k)
 for k in ('width','height','refreshMilliHz'):need(number(config[0],k)>0,'actual video '+k)
 need(number(config[0],'video')<=2 and number(config[0],'display')<=4 and number(config[0],'depth')<=2,'actual video enums')
 need(number(config[0],'requested')<=1 and number(config[0],'night')==0,'authored request/opaque mood')
 workload=rows(stdout,'QUIETWORKLOAD');need(len(workload)==1,'one opaque authored workload marker')
 need(workload[0].get('model')=='firstPerson','supported generated model')
 for k,v in {'nightKnown':0,'authoredSaveValuesUnchanged':1,'scriptModeOpaque':1}.items():need(number(workload[0],k)==v,'workload '+k)
 quality=rows(stdout,'QUIETQUALITY');need(len(quality)==1,'one initial quality record')
 for k in ('rasterWidth','rasterHeight','scaleX','scaleY'):need(number(quality[0],k)>0,'initial raster '+k)
 for k,v in {'order':order,'valid':1,'loops':5400}.items():need(number(done[0],k)==v,'done '+k)
 phases=unique(rows(stdout,'QUIETPHASE'),'phase',range(3));windows=unique(rows(artifact,'QUIETWINDOW'),'phase',range(3))
 contexts=rows(stdout,'QUIETCONTEXT');need(len(contexts)==6,'six sparse contexts')
 context_map={}
 for row in contexts:
  key=(number(row,'phase'),number(row,'offset'));need(key not in context_map,'duplicate context');context_map[key]=row
  for k in ('night','video','display','depth','width','height','refreshMilliHz'):need(number(row,k)==number(config[0],k),'sparse video/mood drift '+k)
  need(number(row,'requested')<=1,'actual sparse pipeline request')
  need(number(row,'scene')==0,'scene changed')
  for k in ('dtBits','clockBits'):need(re.fullmatch('[0-9a-fA-F]{8}',row.get(k,'')) is not None,'ordinary clock bits '+k)
  for k in ('rasterWidth','rasterHeight','scaleX','scaleY'):need(number(row,k)>0,'actual raster '+k)
  for k in ('ilChoice','ilProbing','ilPipelined','frameYield'):need(number(row,k)<=1,'selector flag '+k)
  for k in ('ilBlock','ilFrame','ilAccepted'):need(re.fullmatch('-?[0-9]+',row.get(k,'')) is not None,'selector state '+k)
 need(set(context_map)=={(p,o) for p in range(3) for o in (750,1155)},'context identities')
 raw=rows(artifact,'QUIETRAW');chunks=rows(artifact,'QUIETCHUNK');expected_samples=sum(128 for p in range(3) if (p==1)!=(order==1));need(len(raw)==expected_samples,'complete raw count');need(len(chunks)==15,'complete chunk count')
 buffer=None;result={}
 for p in range(3):
  enabled=int((p==1)!=(order==1));pr=phases[p];wr=windows[p]
  for k,v in {'first':p*1800,'sampler':enabled,'countReads':262 if enabled else 6}.items():need(number(pr,k)==v,'phase '+k)
  pointer=pr.get('samplePtr');need(pointer is not None and re.fullmatch('[0-9a-fA-F]{8}',pointer) and int(pointer,16)!=0,'sample pointer')
  if buffer is None:buffer=pointer
  need(pointer==buffer,'common sample buffer drift')
  for k,v in {'sampler':enabled,'raw':128 if enabled else 0,'chunks':5,'valid':1}.items():need(number(wr,k)==v,'window '+k)
  cs=unique([x for x in chunks if number(x,'phase')==p],'i',range(5));elapsed=pacing=rendered=synthetic=0
  for i,c in cs.items():
   need(number(c,'first')==p*1800+800+i*64 and number(c,'loops')==64,'chunk loop ownership')
   e,s=number(c,'elapsed'),number(c,'pacing');need(0<e<0x80000000 and s<=e,'chunk elapsed/pacing')
   total,r,y=[number(c,k) for k in ('totalFlips','renderedFlips','syntheticFlips')];need(total==r+y,'chunk presentation partition')
   elapsed+=e;pacing+=s;rendered+=r;synthetic+=y
  rr=unique([x for x in raw if number(x,'phase')==p],'i',range(128) if enabled else [])
  values={k:[] for k in ('entryPeriod','wholeLoop','pacing','nonPacing','presentPeriod','renderedPeriod')};ineligible={'present':0,'rendered':0,'zero':0,'multiple':0,'synthetic':0}
  for i,s in rr.items():
   need(number(s,'frame')==p*1800+900+i,'sample engine-loop ownership')
   period,whole,stall=[number(s,k) for k in ('entryPeriod','wholeLoop','pacing')];need(0<period<0x80000000 and 0<whole<0x80000000 and stall<=whole,'sample clock/pacing bounds')
   total,r,y=[number(s,k) for k in ('totalFlips','renderedFlips','syntheticFlips')];need(total==r+y,'sample presentation partition');kind=number(s,'lastKind');need(kind<=2,'presentation kind')
   if total==1:need((kind==2)==(y==1),'single flip kind')
   sequence,context=number(s,'sequence'),number(s,'context')
   if kind==1:need(sequence>0 and context<=2,'known pipeline completion owner')
   else:need(sequence==0 and (context<=2 or (total==0 and context==0xffffffff)),'compatibility/synthetic sequence unknown; actual context required when a flip occurred')
   for k,v in [('entryPeriod',period),('wholeLoop',whole),('pacing',stall),('nonPacing',whole-stall)]:values[k].append(v)
   for count,k,label in [(total,'presentPeriod','present'),(r,'renderedPeriod','rendered')]:
    if count==1:
     v=number(s,k);need(0<v<0x80000000,'eligible '+k+' bounds');values[k].append(v)
    else:ineligible[label]+=1
   ineligible['zero']+=total==0;ineligible['multiple']+=total>1;ineligible['synthetic']+=y
  result[str(p)]={'sampler':enabled,'tax_loop_count':320,'elapsed_ticks_u64':elapsed,'pacing_ticks_u64':pacing,'non_pacing_ticks_u64':elapsed-pacing,'mean_non_pacing_ms':(elapsed-pacing)/320/294912,'rendered_completions':rendered,'synthetic_completions':synthetic,'sample_stats':{k:stats(v) for k,v in values.items() if v},'presentation_ineligible':ineligible}
 first,middle,last=[result[str(p)]['mean_non_pacing_ms'] for p in range(3)];delta=[middle-first,middle-last] if order==0 else [first-middle,last-middle]
 return {'status':'quiet_cadence_complete','environment':environment,'order':order,'engine_loops':5400,'sample_rows':expected_samples,'chunk_rows':15,'common_sample_buffer':buffer,'by_phase':result,'sampler_on_minus_off_mean_per_tax_loop_ms':delta,'sampler_on_minus_off_total_tax_window_ms':[v*320 for v in delta],'sampler_on_minus_off_per_instrumented_loop_proxy_ms':[v*320/129 for v in delta],'instrumented_loops_per_on_phase_including_prime':129,'outer_control_spread_ms':abs(last-first),'config':config[0],'quality':quality[0],'workload':workload[0],'night_known':False,'script_mode_opaque':True,'contexts':context_map_to_json(context_map),'native_sixty_fps_accepted':False,'tv_photon_cadence_accepted':False,'pure_cpu_bill':False,'uniform_observer_subtraction_accepted':False,'limitations':['Ordinary clocks and real physics can drift between arms; adaptive interleave can respond to sampler timing, so net tax includes induced selector response.','Chunk modulo totals assume no missed entireCount wrap between64-loop boundaries; host duration/independent wide-clock sanity required.','Elapsed minus existing pacing retains hardware waits/preemption/deferred previous render completion.','Completed flip timestamps are engine flip-call returns; synthetic completions are separate from rendered frames.','OFF keeps common phase/chunk gates, buffer and presentation metadata stores; this residual apparatus is not priced.','Whole-loop end timestamp precedes sampler bookkeeping; chunk tax includes that bookkeeping.','Per-instrumented-loop129 tax proxy uses phase-total bracket difference and cannot correct each sample uniformly.']}
def context_map_to_json(xs):return {f'{p}:{o}':v for (p,o),v in xs.items()}
def main():
 p=argparse.ArgumentParser();p.add_argument('stdout',type=Path);p.add_argument('--artifact',type=Path,required=True);p.add_argument('--expected-order',type=int,choices=[0,1],required=True);p.add_argument('--environment',choices=['host','emulator','ps2'],required=True);p.add_argument('-o',type=Path,required=True);a=p.parse_args()
 try:r=analyze(a.stdout.read_text(errors='replace'),a.artifact.read_text(encoding='utf8',errors='strict'),a.expected_order,a.environment);code=0
 except (Invalid,OSError,ValueError,KeyError) as e:r={'status':'rejected','issues':[str(e)]};code=1
 r['artifact_sha256']=hashlib.sha256(a.artifact.read_bytes()).hexdigest() if a.artifact.is_file() else None;a.o.write_text(json.dumps(r,indent=2)+'\n',encoding='utf8');print(r['status']);return code
if __name__=='__main__':raise SystemExit(main())
