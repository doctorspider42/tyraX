from pathlib import Path
import hashlib,json,importlib.util,subprocess,sys,os
sys.dont_write_bytecode=True
O=Path(__file__).resolve().parent;L=O.parent;H=O/'host-v2';H.mkdir(exist_ok=False)
def module(name,p):s=importlib.util.spec_from_file_location(name,p);m=importlib.util.module_from_spec(s);s.loader.exec_module(m);return m
m=module('newnight',O/'analyze-night.py');base=module('oldnight',L/'wild-pool-lattice-controls-v1/analyze-night.py');positive=[];negative=[];regress=[];pins={};sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
def row(line):return {k:int(v) for k,v in (t.split('=') for t in line.split()[2:])}
def cli(tfile,afile,kind,order,report):
 cmd=[sys.executable,str(O/'analyze-night-cli.py'),'--stdout',str(tfile),'--artifact',str(afile),'--environment','host','--kind',str(kind),'--order',str(order),'--joint','0','--restored','0','--report',str(report)];r=subprocess.run(cmd,capture_output=True,text=True,env={**os.environ,'PYTHONDONTWRITEBYTECODE':'1'});return r,cmd
for order in [0,1]:
 src=L/'wild-pool-lattice-host-v1'/f'runtime-kind8-order{order}-stdout.log';ar=L/'wild-pool-lattice-host-v1'/f'runtime-kind8-order{order}-artifact.log';pins[str(src)]=sha(src);pins[str(ar)]=sha(ar);lines=[]
 for line in src.read_text().splitlines():
  if line.startswith('LOG: NIGHTPLAN '):line=line.replace('kind=8','kind=9')
  elif line.startswith('LOG: NIGHTLATTICEPHASE '):
   d=row(line);p=d['phase'];en=int((p==1)!=(order==1));line=f'LOG: NIGHTCORONAPHASE phase={p} first={p*1800} selected=1 enabled={en}'
  elif line.startswith('LOG: NIGHTLATTICEGATES '):
   d=row(line);p,o=d['phase'],d['offset'];en=int((p==1)!=(order==1));line=f'LOG: NIGHTCORONAGATES phase={p} offset={o} selected=1 enabled={en} coldPackets=2 eligible=1 requested={en} fallback=1 sourceVertices=81 requestedVertices={en*6} fogOn=2 shaderLit=1 invalid=0 acceptedOutputKnown=0'
  lines.append(line)
 text='\n'.join(lines)+'\n';art=ar.read_text();tf=H/f'kind9-order{order}-stdout.log';af=H/f'kind9-order{order}-artifact.log';tf.write_text(text,encoding='utf-8');af.write_text(art,encoding='utf-8');r=m.analyze(text,art,'host',9,order,0,0);assert r['planKind']==9 and r['engine_loops']==5400 and r['sample_rows']==384 and r['chunk_rows']==15 and r['samplers']==[1]*3 and r['poolTableFlags']==[1]*3 and r['disabledMasks']==[0]*3 and r['coronaAcceptedOutputKnown'] is False and r['physicalGainAccepted'] is False
 cr,cmd=cli(tf,af,9,order,H/f'kind9-order{order}-cli.json');assert cr.returncode==0,cr.stderr;positive.append({'kind':9,'order':order,'samples':384,'chunks':15,'engineLoops':5400,'requests':r['coronaColdRequests'],'outputKnown':False,'cli':cmd,'cliExit':0})
 # Zero cold observations may be valid replay; cannot promote it as inactivity or gain.
 z=text
 for line in text.splitlines():
  if line.startswith('LOG: NIGHTCORONAGATES '):
   d=row(line);p,o,en=d['phase'],d['offset'],d['enabled'];zero=f'LOG: NIGHTCORONAGATES phase={p} offset={o} selected=1 enabled={en} coldPackets=0 eligible=0 requested=0 fallback=0 sourceVertices=0 requestedVertices=0 fogOn=0 shaderLit=0 invalid=0 acceptedOutputKnown=0';z=z.replace(line,zero)
 zr=m.analyze(z,art,'host',9,order,0,0);assert zr['coronaOutputStatus']=='ZERO_COLD_REQUESTS_CACHE_REPLAY_OR_INACTIVE_UNKNOWN';positive.append({'kind':9,'order':order,'zeroColdRequests':True,'outputKnown':False,'classification':zr['coronaOutputStatus']})
 if order!=0:continue
 get=lambda prefix:next(x for x in lines if x.startswith(prefix));phase=get('LOG: NIGHTCORONAPHASE phase=1 ');on=get('LOG: NIGHTCORONAGATES phase=1 offset=750 ');off=get('LOG: NIGHTCORONAGATES phase=0 offset=750 ');tp=get('LOG: NIGHTTABLEPHASE phase=0 ')
 def change(line,key,value):
  toks=line.split();idx=[i for i,t in enumerate(toks)if t.startswith(key+'=')];assert len(idx)==1;toks[idx[0]]=key+'='+str(value);return text.replace(line,' '.join(toks),1)
 mutants={
 'missingCoronaPhase':('\n'.join(x for x in lines if x!=phase),art),'duplicateCoronaPhase':(text+phase+'\n',art),'unknownCoronaPhaseField':(text.replace(phase,phase+' junk=0'),art),
 'wrongCoronaSelected':(change(phase,'selected',0),art),'wrongCoronaArm':(change(phase,'enabled',0),art),'wrongCoronaFirst':(change(phase,'first',1801),art),
 'missingCoronaGate':('\n'.join(x for x in lines if x!=on),art),'duplicateCoronaGate':(text+on+'\n',art),'unknownCoronaGateField':(text.replace(on,on+' junk=0'),art),
 'partition':(change(on,'coldPackets',3),art),'onRequested':(change(on,'requested',0),art),'offRequested':(change(off,'requested',1),art),'invalidCounter':(change(on,'invalid',1),art),
 'falseAcceptedKnown':(change(on,'acceptedOutputKnown',1),art),'uint32Overflow':(change(on,'sourceVertices',4294967296),art),'negativeCounter':(change(on,'requested',-1),art),
 'requestedVertexRange':(change(on,'requestedVertices',78),art),'sourceVertexRange':(change(on,'sourceVertices',151),art),'fogSubset':(change(on,'fogOn',3),art),'shaderSubset':(change(on,'shaderLit',3),art),
 'wrongSparseOffset':(change(on,'offset',900),art),'tableOff':(change(tp,'enabled',0),art),'maskCut':(text.replace('mask=0 appliedMask=0','mask=1 appliedMask=1',1),art),
 'samplerOff':(text.replace('sampler=1 countReads=262','sampler=0 countReads=6',1),art),'readCountWrong':(text.replace('countReads=262','countReads=261',1),art),
 'loopsIncomplete':(text.replace('loops=5400','loops=5399',1),art),'fixedDt':(text.replace('fixedDt=0','fixedDt=1',1),art),'profile':(text.replace('profile=0','profile=1',1),art),'ordinaryClocksOff':(text.replace('ordinaryClocks=1','ordinaryClocks=0',1),art),
 'missingRaw':(text,'\n'.join(x for x in art.splitlines()if not x.startswith('LOG: NIGHTRAW phase=2 i=127 '))),
 'duplicateRaw':(text,art+next(x for x in art.splitlines()if x.startswith('LOG: NIGHTRAW phase=0 i=0 '))+'\n'),
 'rawEpoch':(text,art.replace('phase=0 i=0 frame=900','phase=0 i=0 frame=2700',1)),
 'missingChunk':(text,'\n'.join(x for x in art.splitlines()if not x.startswith('LOG: NIGHTCHUNK phase=0 i=0 '))),
 'artifactLeak':(text+next(x for x in art.splitlines()if x.startswith('LOG: NIGHTRAW'))+'\n',art),'artifactUnexpected':(text,art+'LOG: OTHER ignored=1\n'),
 'kind9AsKind7':(text.replace('schema=1 kind=9','schema=1 kind=7',1),art),'jointNonzero':(text,art)}
 for name,(bad,ba) in mutants.items():
  assert name=='jointNonzero' or (bad,ba)!=(text,art)
  try:m.analyze(bad,ba,'host',9,order,1 if name=='jointNonzero' else 0,0)
  except (ValueError,KeyError):negative.append(name)
  else:raise AssertionError('accepted '+name)
 for other in [7,8]:
  try:m.analyze(text,art,'host',other,order,0,0)
  except (ValueError,KeyError):negative.append(f'cannotRelabel9To{other}')
  else:raise AssertionError('dialect relabeled')
 # CLI also rejects rather than merely library checking three representative corruptions.
 for name in ['falseAcceptedKnown','missingRaw','kind9AsKind7']:
  bad,ba=mutants[name];bf=H/f'negative-{name}-stdout.log';baf=H/f'negative-{name}-artifact.log';bf.write_text(bad,encoding='utf-8');baf.write_text(ba,encoding='utf-8');rr,cmd=cli(bf,baf,9,order,H/f'negative-{name}-cli.json');assert rr.returncode==1 and json.loads((H/f'negative-{name}-cli.json').read_text())['status']=='REJECTED_NIGHT_CAPTURE';negative.append('CLI-'+name)
# Exact accepted legacy captures and inherited negative raw-completeness guards, no recompilation.
for opt in ['O0','O2']:
 for kind in [5,6,7]:
  for order in [0,1]:
   stem=f'{opt}-{kind}-{order}-0-0';tf=L/'wild-controls-v2/host'/f'{stem}-stdout.log';af=L/'wild-controls-v2/host'/f'{stem}-artifact.log';t=tf.read_text();a=af.read_text();old=base.analyze(t,a,'host',kind,order,0,0);new=m.analyze(t,a,'host',kind,order,0,0)
   for key,value in old.items():assert new[key]==value,(kind,order,key)
   assert not new['coronaSpriteSelected'] and new['sample_rows']==384;regress.append({'kind':kind,'order':order,'optimization':opt,'allInheritedResultFieldsEqual':True});pins[str(tf)]=sha(tf);pins[str(af)]=sha(af)
   bad='\n'.join(a.splitlines()[1:])
   try:m.analyze(t,bad,'host',kind,order,0,0)
   except (ValueError,KeyError):negative.append(f'regression-{stem}-incompleteArtifact')
   else:raise AssertionError('legacy completeness weakened')
   if opt=='O2':rr,cmd=cli(tf,af,kind,order,H/f'regression-{stem}-cli.json');assert rr.returncode==0;regress[-1]['cliExit']=0
proof={'status':'PASS_KIND9_FULL_AUTHORITATIVE_SAMPLER_CLI_HOST_CONTROLS','positive':positive,'negative':negative,'legacyKind567Regression':regress,'allInheritedLoopAnalyzerBytesUnchanged':True,'kindRelabelingUsed':False,'hostKind9InputsSyntheticAdaptedFromBoundKind8HostSamplingOnly':True,'actualRuntimeNativeOutputPixelsOrGainAccepted':False,'parserPins':{n:sha(O/n)for n in ['analyze-loop.py','analyze-night.py','analyze-night-cli.py','corona_controls.py','source-controls/night_plan.hpp','source-controls/stapip_vu1_shared_defines.h',Path(__file__).name]},'qualifiedPriorInputs':pins,'hostArtifactPins':{p.name:sha(p)for p in H.iterdir() if p.is_file()}}
(O/'proof.json').write_text(json.dumps(proof,indent=2)+'\n',encoding='utf-8');print(proof['status'],len(positive),len(negative),len(regress))
