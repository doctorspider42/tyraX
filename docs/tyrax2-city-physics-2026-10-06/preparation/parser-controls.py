from pathlib import Path
import subprocess,sys,json,re,hashlib
r=Path(__file__).parent;ref=r.parent/'tyrax2-city-physics-emulator-v1/capture-final';out=r/'parser-controls';assert not out.exists();out.mkdir()
log=ref/'runtime-emulator.log';text=(ref/'bin-closeout-samples.txt').read_text();cmd=[sys.executable,str(r/'analyze-physics.py'),str(log)]
def change(text,field,value,phase=0):
 pattern=r'(^LOG: CLOSEOUTVEH phase='+str(phase)+r' i=0 .*?\b'+field+r'=)\S+'
 result,n=re.subn(pattern,lambda m:m.group(1)+value,text,count=1,flags=re.M);assert n==1;return result
variants={'valid':text,'missing_execution':change(text,'executed','0'),'bad_plan':change(text,'planned','4'),'bad_dt':change(text,'dtBits','7fc00000'),'wrong_timer':change(text,'enabled','0'),'wrong_reads':change(text,'addedReads','1'),'missing_capture':change(text,'captured','127'),'scope_exceeds_loop':change(text,'elapsed','1073741824'),'disabled_has_elapsed':change(text,'elapsed','1',1),'duplicate_record':text+next(l for l in text.splitlines()if l.startswith('LOG: CLOSEOUTVEH '))+'\n'}
# Schema acceptance for a two-step row only: synthetic replay, not runtime activation.
two=change(change(change(text,'dtBits','3d088889'),'planned','2'),'executed','2');variants['synthetic_two_step_schema']=two
results=[]
for name,t in variants.items():
 p=out/(name+'.txt');p.write_text(t)
 run=subprocess.run(cmd+['--artifact',str(p),'--out',str(out/(name+'.json')),'--route','vehicle'],capture_output=True)
 accepted=name in ('valid','synthetic_two_step_schema');assert (run.returncode==0)==accepted,(name,run.stderr.decode(errors='replace'))
 results.append(dict(name=name,exitCode=run.returncode,expectAccepted=accepted))
record=dict(status='PASS_11_PHYSICS_PARSER_REPLAY_CONTROLS',parserSha256=hashlib.sha256((r/'analyze-physics.py').read_bytes()).hexdigest(),referenceLogSha256=hashlib.sha256(log.read_bytes()).hexdigest(),referenceSamplesSha256=hashlib.sha256((ref/'bin-closeout-samples.txt').read_bytes()).hexdigest(),results=results,twoStepRuntimeQualificationClaim=False,measurements=False)
(out/'controls.json').write_text(json.dumps(record,indent=2)+'\n');print(record['status'])
