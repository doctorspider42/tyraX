from pathlib import Path
import importlib.util,json,argparse,hashlib
sp=importlib.util.spec_from_file_location('loop',Path(__file__).with_name('analyze-loop.py'));loop=importlib.util.module_from_spec(sp);sp.loader.exec_module(loop)
need=loop.need;n=loop.number

def plan(kind,order,joint,restored):
 need(order in (0,1),'order');need((kind==0 and joint==restored==0)or(kind==1 and joint==31 and restored==0)or(kind==2 and joint==31 and 0<restored<=31 and not(restored&~joint)),'kind/mask plan')
 result=[]
 for p in range(3):
  middle=(p==1)!=(order==1);result.append((0,int(middle))if kind==0 else (joint if middle else 0,1)if kind==1 else (joint&~restored if middle else joint,1))
 return result

def analyze(stdout,artifact,environment,kind,order,joint,restored):
 specs=plan(kind,order,joint,restored);r=loop.analyze(stdout,artifact,order,environment,[x[1] for x in specs]);phases=loop.unique(loop.rows(stdout,'NIGHTPHASE'),'phase',range(3))
 for p,(mask,sampler) in enumerate(specs):
  row=phases[p];need(set(row)==set('phase first mask appliedMask sampler countReads samplePtr'.split()),'exact NIGHTPHASE schema')
  for k,v in dict(mask=mask,appliedMask=mask,sampler=sampler,countReads=262 if sampler else 6).items():need(n(row,k)==v,'actual masked phase '+k)
  need(r['by_phase'][str(p)]['rendered_completions']>0,'no real presentation')
 config=loop.rows(stdout,'NIGHTPLAN');need(len(config)==1,'one plan');need(set(config[0])==set('schema kind order joint restored phases'.split()),'exact NIGHTPLAN schema')
 for k,v in dict(schema=1,kind=kind,order=order,joint=joint,restored=restored,phases=3).items():need(n(config[0],k)==v,'plan '+k)
 need(len(loop.rows(stdout,'NIGHTGATES'))==6,'six sparse gate summaries required')
 gs={}
 for row in loop.rows(stdout,'NIGHTGATES'):
  key=(n(row,'phase'),n(row,'offset'));need(key not in gs,'duplicate gate identity');gs[key]=row
 need(set(gs)=={(p,o)for p in range(3)for o in (750,1155)},'sparse gate identities')
 gateResult={}
 for (p,offset),row in gs.items():
  mask,sampler=specs[p]
  need(set(row)==set('phase offset first frames mask'.split())|{k+str(b)for b in (1,2,4,8,16,32,64)for k in ('attempted','executed','skipped','submitted')},'exact NIGHTGATES schema')
  for k,v in dict(first=offset,frames=1,mask=mask).items():need(n(row,k)==v,'sparse gate identity '+k)
  gateResult[f'{p}:{offset}']={}
  for bit in (1,2,4,8,16,32,64):
   e,x,s,t=[n(row,k+str(bit)) for k in ('attempted','executed','skipped','submitted')];need(x+s==e,'attempt gate partition');need(bit<=16 or e==x==s==t==0,'reserved unimplemented counters must zero')
   need((x==t==0 if mask&bit else s==0),'actual selected gate mismatch');gateResult[f'{p}:{offset}'][str(bit)]={'attempted':e,'executed':x,'skipped':s,'submitted':t,'submissionAttemptWitness':t>0}
 cameras=loop.rows(stdout,'NIGHTCAMERA');need(len(cameras)==6,'six sparse camera witnesses');cameraMap={}
 for row in cameras:
  need(set(row)==set('phase offset positionX positionY positionZ lookX lookY lookZ sceneGeneration mask'.split()),'exact NIGHTCAMERA schema')
  key=(n(row,'phase'),n(row,'offset'));need(key not in cameraMap,'duplicate camera identity');cameraMap[key]=row;need(key in gs,'camera/gate phase ownership');need(n(row,'mask')==specs[key[0]][0],'camera actual mask')
  for name in ('positionX','positionY','positionZ','lookX','lookY','lookZ'):need(loop.re.fullmatch('[0-9a-fA-F]{8}',row[name]) is not None,'camera float bits')
 need(set(cameraMap)==set(gs),'camera identities');need(len({n(x,'sceneGeneration')for x in cameras})==1,'scene generation drift');need(n(r['config'],'night')==1,'night baseline required')
 r['sparseCameraWitnesses']={f'{p}:{o}':row for(p,o),row in cameraMap.items()};r['cameraBitsEqualAcrossSparseWitnesses']=len({tuple(x[k]for k in ('positionX','positionY','positionZ','lookX','lookY','lookZ'))for x in cameras})==1
 for phase,row in r['by_phase'].items():
  stats=row['sample_stats'];den=128 if row['sampler'] else 0
  row['singlePresentPeriodEligibilityPercent']=100*stats.get('presentPeriod',{}).get('samples',0)/den if den else None
  row['singleRenderedPeriodEligibilityPercent']=100*stats.get('renderedPeriod',{}).get('samples',0)/den if den else None
  row['syntheticZeroMultipleRemainSeparated']=True
 r.update(sparseCounterFrames=[750,1155],fullTimedWindowActivationCountsObserved=False,eventCounterWritesInsideTaxWindow=False,status='PASS_STRICT_NIGHT_MASKED_LOOP_CAPTURE',planKind=kind,disabledMasks=[x[0] for x in specs],samplers=[x[1] for x in specs],gateWindows=gateResult,observerAbsoluteCommonCostPriced=False,observerEnabledContrastsOnly=kind==0,physicalGainAccepted=False)
 r['interpretation']='Observer net enabled tax' if kind==0 else 'Joint mask elapsed contrast' if kind==1 else 'Add-back group elapsed contrast against joint-off';r['limitations']=['Only own source/ELF/plan contrasts; no cross-version or uniform subtraction.','Common plan/counter/presentation bookkeeping remains enabled even samplerOff and unpriced.','Attempt counters prove reached family branches, not qualified work; submitted counters are submission-attempt witnesses, not emitted/native DMA proof. No-emission groups must remain labelled inactive/unproven.','Changed adaptive interleave/env refresh work can mediate timings; exact cold context and epochs needed.','Retained state/epochs require actual warm restoration and visual control; no state rewind assumed.']
 return r
