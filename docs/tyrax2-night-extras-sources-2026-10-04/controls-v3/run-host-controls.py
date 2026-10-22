from pathlib import Path
import subprocess,json,importlib.util,hashlib,argparse
p=Path(__file__).parent;ap=argparse.ArgumentParser();ap.add_argument('--gate-header',type=Path,required=True);ap.add_argument('--host-dir',default='host');a=ap.parse_args();header=a.gate_header;host=p/a.host_dir;host.mkdir(exist_ok=False)
sp=importlib.util.spec_from_file_location('night',p/'analyze-night.py');m=importlib.util.module_from_spec(sp);sp.loader.exec_module(m);sha=lambda f:hashlib.sha256(f.read_bytes()).hexdigest();commands=[];runs=[];negatives=[]
plans=[(0,0,0),(1,31,0),(2,31,3)]+[(3,0,g)for g in(1,2,4,7)]
for opt in('O0','O2'):
 exe=host/f'controls-{opt}.exe';cmd=['C:/Users/pawel/scoop/apps/mingw/current/bin/g++.exe','-std=c++17','-'+opt,'-Wall','-Wextra','-Werror','-static','-I',str(p),'-I',str(header.parent.parent),str(p/'host-controls.cpp'),'-o',str(exe)];r=subprocess.run(cmd,capture_output=True,text=True);(host/f'build-{opt}.txt').write_text(r.stdout+r.stderr);assert r.returncode==0,r.stderr;commands.append(cmd)
 for kind,joint,restored in plans:
  for order in(0,1):
   stem=f'{opt}-{kind}-{order}-{joint}-{restored}';artifact=host/(stem+'-artifact.log');r=subprocess.run([str(exe),str(kind),str(order),str(joint),str(restored),str(artifact)],cwd=host,capture_output=True,text=True);(host/(stem+'-stdout.log')).write_text(r.stdout);(host/(stem+'-run.txt')).write_text(r.stderr);assert r.returncode==0,r.stderr;art=artifact.read_text();v=m.analyze(r.stdout,art,'host',kind,order,joint,restored);runs.append(dict(plan=[kind,order,joint,restored],optimization=opt,sampleRows=v['sample_rows'],extraMasks=v['extraDisabledMasks'],checks=r.stderr.strip(),buildExit=0,runExit=0))
   if opt=='O0' and(kind,order,joint,restored)==(3,0,0,1):
    t=r.stdout;line=lambda prefix:next(x for x in t.splitlines()if x.startswith(prefix));remove=lambda prefix:'\n'.join(x for x in t.splitlines()if not x.startswith(prefix))
    mutations={
     'extraUnknownPhaseField':(t.replace('NIGHTEXTRAPHASE phase=0','NIGHTEXTRAPHASE junk=0 phase=0',1),art),
     'extraUnknownGateField':(t.replace('NIGHTEXTRAGATES phase=0','NIGHTEXTRAGATES junk=0 phase=0',1),art),
     'extraMissingPhase':(remove('LOG: NIGHTEXTRAPHASE phase=2 '),art),
     'extraDuplicatePhase':(t+'\n'+line('LOG: NIGHTEXTRAPHASE phase=0 '),art),
     'extraMissingGate':(remove('LOG: NIGHTEXTRAGATES phase=2 offset=1155 '),art),
     'extraDuplicateGate':(t+'\n'+line('LOG: NIGHTEXTRAGATES phase=0 offset=750 '),art),
     'extraWrongFirst':(t.replace('NIGHTEXTRAPHASE phase=1 first=1800','NIGHTEXTRAPHASE phase=1 first=1801',1),art),
     'extraWrongApplied':(t.replace('extraMask=1 appliedExtraMask=1','extraMask=1 appliedExtraMask=0',1),art),
     'extraWrongSubset':(t.replace('extraMask=1 appliedExtraMask=1','extraMask=8 appliedExtraMask=8',1),art),
     'extraGateWrongMask':(t.replace('frames=1 extraMask=1','frames=1 extraMask=0',1),art),
     'extraSelectedSubmit':(t.replace('frames=1 extraMask=1 attempted1=1 executed1=0 skipped1=1 submitted1=0','frames=1 extraMask=1 attempted1=1 executed1=0 skipped1=1 submitted1=1',1),art),
     'extraPartition':(t.replace('frames=1 extraMask=1 attempted1=1 executed1=0 skipped1=1','frames=1 extraMask=1 attempted1=1 executed1=0 skipped1=0',1),art),
     'extraEnabledSkip':(t.replace('frames=1 extraMask=0 attempted1=1 executed1=1 skipped1=0','frames=1 extraMask=0 attempted1=1 executed1=0 skipped1=1',1),art),
     'extraWrongColdWindow':(t.replace('NIGHTEXTRAGATES phase=0 offset=750 first=750 frames=1','NIGHTEXTRAGATES phase=0 offset=800 first=800 frames=1',1),art),
     'ordinaryMaskNonzeroKind3':(t.replace('mask=0 appliedMask=0','mask=1 appliedMask=1',1),art),
     'samplerOffKind3':(t.replace('sampler=1 countReads=262','sampler=0 countReads=6',1),art),
     'ordinaryReservedCounter':(t.replace('attempted32=0','attempted32=1',1),art),
     'phaseWrongFirst':(t.replace('NIGHTPHASE phase=1 first=1800','NIGHTPHASE phase=1 first=1801',1),art),
     'malformedPointer':(t.replace('samplePtr=','samplePtr=z',1),art),
     'driftPointer':(t.replace(line('LOG: NIGHTPHASE phase=1 ').split('samplePtr=')[1],'12345678',1) if False else t.replace(line('LOG: NIGHTPHASE phase=1 '),line('LOG: NIGHTPHASE phase=1 ').split('samplePtr=')[0]+'samplePtr=12345678',1),art),
     'missingRaw':(t,'\n'.join(x for x in art.splitlines()if not x.startswith('LOG: NIGHTRAW phase=2 i=127 '))),
     'unknownRawField':(t,art.replace('NIGHTRAW phase=0','NIGHTRAW junk=0 phase=0',1)),
     'missingCamera':(remove('LOG: NIGHTCAMERA phase=2 offset=1155 '),art),
    }
    for name,(x,y)in mutations.items():
     assert(x,y)!=(t,art),'mutant did not apply '+name
     try:m.analyze(x,y,'host',kind,order,joint,restored)
     except(ValueError,KeyError):negatives.append(name)
     else:raise AssertionError('accepted '+name)
   if opt=='O0'and(kind,order,joint,restored)==(0,0,0,0):
    x=r.stdout.replace('extraMask=0 appliedExtraMask=0','extraMask=1 appliedExtraMask=1',1)
    try:m.analyze(x,art,'host',kind,order,joint,restored)
    except(ValueError,KeyError):negatives.append('oldKindExtraMustZero')
    else:raise AssertionError('old extra accepted')
cliCmd=['python',str(p/'analyze-night-cli.py'),'--stdout',str(host/'O2-3-0-0-7-stdout.log'),'--artifact',str(host/'O2-3-0-0-7-artifact.log'),'--environment','host','--kind','3','--order','0','--joint','0','--restored','7','--report',str(host/'actual-cli-kind3.json')];cli=subprocess.run(cliCmd,capture_output=True,text=True);assert cli.returncode==0,cli.stderr;commands.append(cliCmd);(host/'actual-cli-command.log').write_text(cli.stdout+cli.stderr)
files=[p/n for n in('night_sampler.hpp','night_plan.hpp','night_runtime.hpp','quiet_runtime.hpp','quiet_cadence.hpp','analyze-loop.py','analyze-night.py','analyze-night-cli.py','host-controls.cpp','run-host-controls.py','README.md')]+[header]
proof=dict(status='PASS_PREPARED_NIGHT_V3_EXTRA_SUBSET_ACTUAL_HEADER_HOST_CONTROLS_ONLY',positiveCompleteTranscripts=len(runs),negativeParserGuards=negatives,runs=runs,commands=commands,sourcePins={str(f):sha(f)for f in files},artifactPins={str(f):sha(f)for f in host.iterdir()if f.is_file()},sourceGateHeaderSha256=sha(header),samplerOnReads262OffReads6=True,ordinaryOldKindsExtraZero=True,extraCountersUntimed7501155Only=True,nativeOrRuntimeAccepted=False,productionGainAccepted=False,notFullOldSuiteRerun=True)
(host/'proof.json').write_text(json.dumps(proof,indent=2)+'\n');print('PASS',len(runs),len(negatives),sha(host/'proof.json'))
