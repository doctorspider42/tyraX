from pathlib import Path
import importlib.util,subprocess,json,hashlib
p=Path(__file__).parent
host=Path('F:/Projects/tyrax2-lab-20261001/wild-pool-lattice-host-v1')
spec=importlib.util.spec_from_file_location('night',p/'analyze-night.py');m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
positive=[];negative=[]
for order in (0,1):
 stdout=host/f'runtime-kind8-order{order}-stdout.log';artifact=host/f'runtime-kind8-order{order}-artifact.log'
 text=stdout.read_text();art=artifact.read_text()
 result=m.analyze(text,art,'host',8,order,0,0)
 assert result['poolTableFlags']==[1,1,1]
 assert result['poolLatticeFlags']==([0,1,0]if order==0 else[1,0,1])
 assert result['sample_rows']==384 and result['chunk_rows']==15
 report=p/f'cli-kind8-order{order}.json';assert not report.exists()
 cmd=['python',str(p/'analyze-night-cli.py'),'--stdout',str(stdout),'--artifact',str(artifact),'--environment','host','--kind','8','--order',str(order),'--joint','0','--restored','0','--report',str(report)]
 run=subprocess.run(cmd,capture_output=True);assert run.returncode==0,run.stdout+run.stderr
 positive.append(dict(order=order,samples=384,chunks=15,tableFlags=result['poolTableFlags'],latticeFlags=result['poolLatticeFlags'],cliCommand=cmd,cliStdout=run.stdout.decode().strip()))
 if order!=0:continue
 lines=text.splitlines()
 get=lambda prefix:next(line for line in lines if line.startswith(prefix))
 phase=get('LOG: NIGHTLATTICEPHASE phase=1 ')
 on=get('LOG: NIGHTLATTICEGATES phase=1 offset=750 ')
 off=get('LOG: NIGHTLATTICEGATES phase=0 offset=750 ')
 tablePhase=get('LOG: NIGHTTABLEPHASE phase=0 ')
 tableGate=get('LOG: NIGHTTABLEGATES phase=0 offset=750 ')
 def replacefield(line,key,value):
  tokens=line.split();matches=[i for i,t in enumerate(tokens)if t.startswith(key+'=')];assert len(matches)==1
  tokens[matches[0]]=key+'='+str(value);return text.replace(line,' '.join(tokens),1)
 mutants={
  'missingLatticePhase':('\n'.join(x for x in lines if x!=phase),art),
  'duplicateLatticePhase':(text+'\n'+phase,art),
  'unknownLatticePhaseKey':(text.replace(phase,phase+' junk=0'),art),
  'missingLatticeGate':('\n'.join(x for x in lines if x!=on),art),
  'duplicateLatticeGate':(text+'\n'+on,art),
  'unknownLatticeGateKey':(text.replace(on,on+' junk=0'),art),
  'wrongLatticeSelected':(replacefield(phase,'selected',0),art),
  'wrongLatticeFlag':(replacefield(phase,'enabled',0),art),
  'wrongGateFlag':(replacefield(on,'enabled',0),art),
  'wrongColdOffset':(replacefield(on,'offset',800),art),
  'wrongColdPhase':(replacefield(on,'phase',3),art),
  'wrongPartition':(replacefield(on,'invocations',3),art),
  'wrongApplied':(replacefield(on,'applied',0),art),
  'counterInvalid':(replacefield(on,'invalid',1),art),
  'counterOverflow':(replacefield(on,'source',4294967296),art),
  'negativeCounter':(replacefield(on,'unique',-1),art),
  'nondecimalCounter':(replacefield(on,'unique','0x60'),art),
  'duplicateField':(text.replace(on,on+' source=150'),art),
  'malformedToken':(text.replace(on,on+' broken'),art),
  'noTransformReduction':(replacefield(on,'unique',150),art),
  'excessTransformCount':(replacefield(on,'unique',151),art),
  'zeroTransformCount':(replacefield(on,'unique',0),art),
  'offPreparation':(replacefield(off,'eligible',1),art),
  'offApplied':(replacefield(off,'applied',1),art),
  'offReducedTransforms':(replacefield(off,'unique',96),art),
  'idea2OffPhase':(replacefield(tablePhase,'enabled',0),art),
  'idea2OffAppliedPhase':(replacefield(tablePhase,'appliedEnabled',0),art),
  'idea2OffGate':(replacefield(tableGate,'enabled',0),art),
  'unknownContextKey':(text.replace('NIGHTCONTEXT phase=0','NIGHTCONTEXT junk=0 phase=0',1),art),
  'sourceMismatch':(text.replace('schema=1 kind=8','schema=1 kind=7',1),art),
  'ordinaryMask':(text.replace('mask=0 appliedMask=0','mask=1 appliedMask=1',1),art),
  'samplerOff':(text.replace('sampler=1 countReads=262','sampler=0 countReads=6',1),art),
  'wrongCountReads':(text.replace('countReads=262','countReads=261',1),art),
  'wrongFreshFirst':(text.replace('phase=0 first=0','phase=0 first=1800',1),art),
  'wrongClockMode':(text.replace('ordinaryClocks=1','ordinaryClocks=0',1),art),
  'fixedDt':(text.replace('fixedDt=0','fixedDt=1',1),art),
  'profilingActive':(text.replace('profile=0','profile=1',1),art),
  'wrongSampleAbi':(text.replace('sampleBytes=6144','sampleBytes=6140',1),art),
  'incompleteDone':(text.replace('loops=5400','loops=5399',1),art),
  'invalidDone':(text.replace('NIGHTDONE order=0 valid=1','NIGHTDONE order=0 valid=0',1),art),
  'missingRaw':(text,'\n'.join(x for x in art.splitlines()if not x.startswith('LOG: NIGHTRAW phase=2 i=127 '))),
  'duplicateRaw':(text,art+'\n'+next(x for x in art.splitlines()if x.startswith('LOG: NIGHTRAW phase=0 i=0 '))),
  'wrongRawEpoch':(text,art.replace('phase=0 i=0 frame=900','phase=0 i=0 frame=2700',1)),
  'fileRowsInStdout':(text+'\n'+next(x for x in art.splitlines()if x.startswith('LOG: NIGHTRAW')),art),
  'unknownArtifact':(text,art+'\nLOG: OTHER ignored=1'),
 }
 # Coherent-looking zero-activation counters must still be rejected.
 noOn=text.replace(on,'LOG: NIGHTLATTICEGATES phase=1 offset=750 selected=1 enabled=1 invocations=2 eligible=0 applied=0 fallback=2 source=150 unique=150 invalid=0')
 mutants['noPositiveActivation']=(noOn,art)
 for name,(bad,ba)in mutants.items():
  assert (bad,ba)!=(text,art),name+' did not mutate'
  try:m.analyze(bad,ba,'host',8,order,0,0)
  except (ValueError,KeyError):negative.append(name)
  else:raise AssertionError('accepted '+name)
 try:m.analyze(text,art,'host',7,order,0,0)
 except (ValueError,KeyError):negative.append('kind8CannotRelabelToKind7')
 else:raise AssertionError('accepted kind relabel')
sha=lambda f:hashlib.sha256(f.read_bytes()).hexdigest()
proof={'status':'PASS_KIND8_STRICT_AUTHORITATIVE_CLI_HOST_POSITIVE_NEGATIVE','positive':positive,'negative':negative,'parserPins':{f.name:sha(f)for f in(p/'analyze-loop.py',p/'analyze-night.py',p/'analyze-night-cli.py',Path(__file__))},'hostInputPins':{f.name:sha(f)for f in host.glob('runtime-kind8-*-*.log')},'actualTargetActivationAccepted':False,'actualSdkBytesAccepted':False,'ordinarySamplerAbiChanged':False,'kindRelabelingUsed':False}
(p/'proof.json').write_text(json.dumps(proof,indent=2)+'\n',encoding='utf-8')
print(proof['status'],len(positive),len(negative))
