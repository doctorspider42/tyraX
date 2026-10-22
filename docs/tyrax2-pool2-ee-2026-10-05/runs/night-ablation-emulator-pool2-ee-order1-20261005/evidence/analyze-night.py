from pathlib import Path
import importlib.util,json,argparse,hashlib
sp=importlib.util.spec_from_file_location('loop',Path(__file__).with_name('analyze-loop.py'));loop=importlib.util.module_from_spec(sp);sp.loader.exec_module(loop)
need=loop.need;n=loop.number

def plan(kind,order,joint,restored):
 need(order in (0,1),'order');need((kind==0 and joint==restored==0)or(kind==1 and joint==31 and restored==0)or(kind==2 and joint==31 and 0<restored<=31 and not(restored&~joint))or(kind==3 and joint==0 and 0<restored<=7)or(kind in(5,6,7)and joint==restored==0),'kind/mask plan')
 result=[]
 for p in range(3):
  middle=(p==1)!=(order==1);result.append((0,int(middle))if kind==0 else (joint if middle else 0,1)if kind==1 else (0,1)if kind in(3,5,6,7) else (joint&~restored if middle else joint,1))
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
 extraMasks=[restored if ((p==1)!=(order==1)) else 0 for p in range(3)]if kind==3 else [0,0,0]
 ep=loop.unique(loop.rows(stdout,'NIGHTEXTRAPHASE'),'phase',range(3))
 for p,row in ep.items():
  need(set(row)==set('phase first extraMask appliedExtraMask'.split()),'exact NIGHTEXTRAPHASE schema')
  for k,v in dict(first=p*1800,extraMask=extraMasks[p],appliedExtraMask=extraMasks[p]).items():need(n(row,k)==v,'extra phase '+k)
 eg=loop.rows(stdout,'NIGHTEXTRAGATES');need(len(eg)==6,'six extra sparse witnesses');em={}
 for row in eg:
  key=(n(row,'phase'),n(row,'offset'));need(key not in em,'duplicate extra gate');em[key]=row
 need(set(em)==set(gs),'extra / ordinary witness ownership');extraGateResult={}
 for(p,offset),row in em.items():
  need(set(row)==set('phase offset first frames extraMask'.split())|{k+str(bit)for bit in(1,2,4)for k in('attempted','executed','skipped','submitted')},'exact NIGHTEXTRAGATES schema')
  for k,v in dict(first=offset,frames=1,extraMask=extraMasks[p]).items():need(n(row,k)==v,'extra gate '+k)
  extraGateResult[f'{p}:{offset}']={}
  for bit in(1,2,4):
   e,x,s,t=[n(row,k+str(bit))for k in('attempted','executed','skipped','submitted')];need(x+s==e,'extra attempt partition');need((x==t==0 if extraMasks[p]&bit else s==0),'extra selected gate accounting')
   extraGateResult[f'{p}:{offset}'][str(bit)]=dict(attempted=e,executed=x,skipped=s,submitted=t,submissionAttemptWitness=t>0)
 r.update(extraDisabledMasks=extraMasks,extraGateWindows=extraGateResult,extraCounterFrames=[750,1155])
 variant=kind if kind in(5,6)else 0;wildFlags=[int((p==1)!=(order==1))for p in range(3)]if variant else [0,0,0]
 wp=loop.unique(loop.rows(stdout,'NIGHTWILDPHASE'),'phase',range(3))
 for p,row in wp.items():
  need(set(row)==set('phase first variant enabled appliedVariant appliedEnabled'.split()),'exact NIGHTWILDPHASE schema')
  for k,v in dict(first=p*1800,variant=variant,enabled=wildFlags[p],appliedVariant=variant,appliedEnabled=wildFlags[p]).items():need(n(row,k)==v,'wild phase '+k)
 wg=loop.rows(stdout,'NIGHTWILDGATES');need(len(wg)==6,'six sparse wild witnesses');wm={};fields=('invocations','eligible','applied','fallback','boundary','nonfinite','coldCompared','coldMismatches','inputUnits','outputUnits','invalid')
 for row in wg:
  key=(n(row,'phase'),n(row,'offset'));need(key not in wm,'duplicate wild witness');wm[key]=row
 need(set(wm)==set(gs),'wild witness ownership');wildResult={}
 for(p,offset),row in wm.items():
  need(set(row)==set('phase offset first frames variant enabled'.split())|{prefix+k[0].upper()+k[1:]for prefix in('plane','cone')for k in fields},'exact NIGHTWILDGATES schema')
  for k,v in dict(first=offset,frames=1,variant=variant,enabled=wildFlags[p]).items():need(n(row,k)==v,'wild witness '+k)
  result={}
  for prefix,id in(('plane',5),('cone',6)):
   c={k:n(row,prefix+k[0].upper()+k[1:])for k in fields};need(c['invalid']==c['coldMismatches']==0,'wild mismatch / overflow')
   need(c['eligible']+c['fallback']==c['invocations'],'wild eligibility partition');need(c['applied']==(c['eligible']if wildFlags[p]and variant==id else 0),'wild actual application')
   need(c['boundary']<=c['fallback']and c['nonfinite']<=c['fallback']and c['coldCompared']<=c['invocations'],'wild reason subsets');need(c['outputUnits']<=c['inputUnits'],'wild selected unit bounds')
   need(not variant or variant==id or all(v==0 for v in c.values()),'other variant counters must zero');result[prefix]=c
  wildResult[f'{p}:{offset}']=result
 r.update(wildVariant=variant,wildFlags=wildFlags,wildWindows=wildResult,wildCountersUntimedFrames=[750,1155],fullTimedWindowWildActivationObserved=False)
 tableSelected=int(kind==7);tableFlags=[int((p==1)!=(order==1))for p in range(3)]if tableSelected else [0,0,0]
 tp=loop.unique(loop.rows(stdout,'NIGHTTABLEPHASE'),'phase',range(3))
 for p,row in tp.items():
  need(set(row)==set('phase first selected enabled appliedSelected appliedEnabled'.split()),'exact NIGHTTABLEPHASE schema')
  for k,v in dict(first=p*1800,selected=tableSelected,enabled=tableFlags[p],appliedSelected=tableSelected,appliedEnabled=tableFlags[p]).items():need(n(row,k)==v,'table phase '+k)
 tg=loop.rows(stdout,'NIGHTTABLEGATES');need(len(tg)==6,'six sparse table witnesses');tm={};tableFields='invocations eligible applied fallback sourceVertices admittedVertices baselineColorQwords tableColorQwords coldCompared coldMismatches invalid'.split()
 for row in tg:
  key=(n(row,'phase'),n(row,'offset'));need(key not in tm,'duplicate table witness');tm[key]=row
 need(set(tm)==set(gs),'table witness ownership');tableResult={}
 for(p,offset),row in tm.items():
  need(set(row)==set('phase offset first frames selected enabled'.split())|set(tableFields),'exact NIGHTTABLEGATES schema')
  for k,v in dict(first=offset,frames=1,selected=tableSelected,enabled=tableFlags[p]).items():need(n(row,k)==v,'table witness '+k)
  c={k:n(row,k)for k in tableFields};need(all(v<=0xffffffff for v in c.values()),'table uint32 bounds');need(c['invalid']==c['coldMismatches']==0,'table mismatch / overflow')
  need(c['eligible']+c['fallback']==c['invocations'],'table eligibility partition');need(c['applied']==(c['eligible']if tableFlags[p]else 0),'table application')
  need(c['baselineColorQwords']==c['sourceVertices'],'table baseline source denominator');need(c['tableColorQwords']==2*c['applied'],'table two-entry descriptor')
  need(c['admittedVertices']<=c['sourceVertices']and c['coldCompared']<=c['invocations'],'table subsets');need((c['applied']>0 and c['applied']<=c['admittedVertices']<=75*c['applied'])or(c['applied']==c['admittedVertices']==0),'table admitted package bounds')
  need(tableSelected or all(v==0 for v in c.values()),'unselected table counters zero');tableResult[f'{p}:{offset}']=c
 r.update(poolTableSelected=bool(tableSelected),poolTableFlags=tableFlags,poolTableWindows=tableResult,poolTableCountersUntimedFrames=[750,1155],fullTimedWindowTableActivationObserved=False,tableColdComparisonIsSourceExpansionOnly=True)
 r['sparseCameraWitnesses']={f'{p}:{o}':row for(p,o),row in cameraMap.items()};r['cameraBitsEqualAcrossSparseWitnesses']=len({tuple(x[k]for k in ('positionX','positionY','positionZ','lookX','lookY','lookZ'))for x in cameras})==1
 for phase,row in r['by_phase'].items():
  stats=row['sample_stats'];den=128 if row['sampler'] else 0
  row['singlePresentPeriodEligibilityPercent']=100*stats.get('presentPeriod',{}).get('samples',0)/den if den else None
  row['singleRenderedPeriodEligibilityPercent']=100*stats.get('renderedPeriod',{}).get('samples',0)/den if den else None
  row['syntheticZeroMultipleRemainSeparated']=True
 r.update(sparseCounterFrames=[750,1155],fullTimedWindowActivationCountsObserved=False,eventCounterWritesInsideTaxWindow=False,status='PASS_STRICT_NIGHT_MASKED_LOOP_CAPTURE',planKind=kind,disabledMasks=[x[0] for x in specs],samplers=[x[1] for x in specs],gateWindows=gateResult,observerAbsoluteCommonCostPriced=False,observerEnabledContrastsOnly=kind==0,physicalGainAccepted=False)
 r['interpretation']='Observer net enabled tax' if kind==0 else 'Joint mask elapsed contrast' if kind==1 else 'Wild same-ELF variant elapsed contrast'if kind in(5,6,7)else 'Full ordinary scene extra cut elapsed contrast' if kind==3 else 'Add-back group elapsed contrast against joint-off';r['limitations']=['Only own source/ELF/plan contrasts; no cross-version or uniform subtraction.','Common plan/counter/presentation bookkeeping remains enabled even samplerOff and unpriced.','Attempt counters prove reached family branches, not qualified work; submitted counters are submission-attempt witnesses, not emitted/native DMA proof. No-emission groups must remain labelled inactive/unproven.','Changed adaptive interleave/env refresh work can mediate timings; exact cold context and epochs needed.','Retained state/epochs require actual warm restoration and visual control; no state rewind assumed.']
 return r
