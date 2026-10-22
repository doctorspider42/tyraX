from pathlib import Path
import hashlib,json,re,subprocess,sys
root=Path(__file__).parent;out=root/(sys.argv[1] if len(sys.argv)>1 else 'host-runtime-results');out.mkdir(exist_ok=False);runs=[]
def analyze(name,stdout,artifact,order,reject=False):
 d=out/name;d.mkdir(exist_ok=True);(d/'stdout.log').write_text(stdout);(d/'quiet-cadence.log').write_text(artifact)
 r=subprocess.run([sys.executable,str(root/'analyze-quiet.py'),str(d/'stdout.log'),'--artifact',str(d/'quiet-cadence.log'),'--expected-order',str(order),'--environment','host','-o',str(d/'analysis.json')],capture_output=True,text=True)
 j=json.loads((d/'analysis.json').read_text());good=(r.returncode!=0 and j['status']=='rejected') if reject else r.returncode==0;runs.append({'name':name,'expected_rejection':reject,'passed':good,'issues':j.get('issues')})
for order in (0,1):
 for mixed in (0,1):
  name=f'actual-order{order}-mixed{mixed}';d=out/name;d.mkdir();r=subprocess.run([str(root/'host-runtime.exe'),str(order),str(mixed)],cwd=d,capture_output=True,text=True);assert r.returncode==0,r.stderr
  analyze(name,r.stdout,(d/'quiet-cadence.log').read_text(),order)
src=out/'actual-order0-mixed0';s=(src/'stdout.log').read_text();f=(src/'quiet-cadence.log').read_text()
for order in (0,1):
 name=f'actual-sync-order{order}';d=out/name;d.mkdir();r=subprocess.run([str(root/'host-runtime.exe'),str(order),'0','0'],cwd=d,capture_output=True,text=True);assert r.returncode==0
 analyze(name,r.stdout,(d/'quiet-cadence.log').read_text(),order)
def change(text,key,value):return re.sub(r'\b'+key+r'=\S+',key+'='+str(value),text,count=1)
for key,value in [('profile',2),('hardwareTrace',1),('ringBytes',1966080),('fixedDt',1),('ordinaryClocks',0),('sampleBytes',8193),('countReads',7)]:analyze('bad-'+key,change(s,key,value),f,0,True)
for key,value in [('elapsed',0x80000000),('pacing',640001),('loops',63),('totalFlips',65),('frame',2701),('entryPeriod',0x80000000),('wholeLoop',0),('lastKind',3),('renderedFlips',0),('presentPeriod',0)]:analyze('bad-'+key,s,change(f,key,value),0,True)
analyze('missing-done',re.sub(r'^.*QUIETDONE.*\n','',s,flags=re.M),f,0,True)
analyze('duplicate-row',s,f+f.splitlines()[0]+'\n',0,True)
analyze('missing-row',s,'\n'.join(f.splitlines()[1:])+'\n',0,True)
analyze('wrong-order',s,f,1,True)
analyze('file-repair-from-stdout',s+f.splitlines()[0]+'\n','\n'.join(f.splitlines()[1:])+'\n',0,True)
for key,value in [('rasterWidth',0),('ilChoice',2),('dtBits','bad'),('sequence',0),('context',99)]:
 analyze('bad-context-'+key,change(s,key,value) if key in ('rasterWidth','ilChoice','dtBits') else s,change(f,key,value) if key in ('sequence','context') else f,0,True)
analyze('missing-sparse-context',re.sub(r'^.*QUIETCONTEXT.*\n','',s,count=1,flags=re.M),f,0,True)
analyze('ordinary-selector-drift-permitted',change(s,'ilChoice',1),f,0)
analyze('ordinary-request-drift-permitted',re.sub(r'(QUIETCONTEXT[^\n]*requested=)1',r'\g<1>0',s,count=1),f,0)
analyze('missing-opaque-workload',re.sub(r'^.*QUIETWORKLOAD.*\n','',s,flags=re.M),f,0,True)
analyze('authored-save-contract',change(s,'authoredSaveValuesUnchanged',0),f,0,True)
analyze('claimed-known-mood',change(s,'nightKnown',1),f,0,True)
summary={'status':'quiet_host_controls_complete' if all(x['passed'] for x in runs) else 'quiet_host_control_gap','runs':runs,'sources':{name:hashlib.sha256((root/name).read_bytes()).hexdigest() for name in ('quiet_cadence.hpp','quiet_runtime.hpp','analyze-quiet.py','header-oracle.cpp','host-runtime.cpp')},'host_state_fields':'specification only; actual sampler/runtime header executed','sample_buffer_bytes':6144,'chunk_buffer_bytes':420}
(out/'summary.json').write_text(json.dumps(summary,indent=2)+'\n');print(summary['status']);print([x for x in runs if not x['passed']]);raise SystemExit(0 if all(x['passed'] for x in runs) else 1)
