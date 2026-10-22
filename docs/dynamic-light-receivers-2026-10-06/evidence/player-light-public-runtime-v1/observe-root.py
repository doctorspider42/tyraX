"""ROOT ONLY owned emulator snapshots and optional ordinary CLI pad route."""
from pathlib import Path
import argparse,json,os,re,subprocess,time
from runtime_common import sha,load,path,verify_review
p=argparse.ArgumentParser();p.add_argument('--launch',type=Path,required=True);p.add_argument('--editor',type=Path);p.add_argument('--pad-file',type=Path);p.add_argument('--out',type=Path,required=True);a=p.parse_args();d=load(a.launch);assert not a.out.exists();a.out.mkdir(parents=True);assert sha(path(d['review']))==d['reviewSha256'];record,fixture=verify_review(path(d['review']));assert record==d['buildRecord'];env=os.environ.copy();env['DISPLAY']=':93'
def owned():
 proc=Path('/proc')/str(d['pid']);args=[x.decode()for x in(proc/'cmdline').read_bytes().split(b'\0')if x];assert(proc/'exe').resolve()==Path(d['exe'])and sha(Path(d['exe']))==d['exeSha256'];assert args[args.index('-datapath')+1]==d['profile'];assert args[args.index('-elf')+1]==d['elf'];return args
def frame():
 owned();tree=subprocess.check_output(['xwininfo','-root','-tree'],env=env,text=True);ids=re.findall(r'(0x[0-9a-fA-F]+) "[^"\n]*"',tree);matches=[]
 for wid in ids:
  prop=subprocess.run(['xprop','-id',wid,'_NET_WM_PID'],env=env,capture_output=True,text=True)
  if re.search(r'=\s*'+str(d['pid'])+r'\s*$',prop.stdout):matches.append(wid)
 assert len(matches)==1,'exact emulator owned window required';return matches[0]
debug=fixture/'bin/livedbg.bin';deadline=time.monotonic()+45
while time.monotonic()<deadline:
 owned()
 if debug.exists()and debug.stat().st_size>0:break
 time.sleep(.1)
else:raise RuntimeError('No normal livedbg frame evidence; loading/first-render unqualified')
before=sha(debug);progressDeadline=time.monotonic()+15
while time.monotonic()<progressDeadline and sha(debug)==before:
 owned();time.sleep(.1)
assert sha(debug)!=before,'no changing debug snapshot; runtime progress unqualified'
images=[]
for label in('boot','after-pad'):
 if label=='after-pad':
  if not a.pad_file:break
  assert a.editor and a.pad_file.is_file();target=str(fixture)
  if a.editor.suffix.lower()=='.exe'and target.startswith('/mnt/'):target=target[5].upper()+':/'+target[7:]
  pad=subprocess.run([str(a.editor),'--pad',target,a.pad_file.read_text(encoding='utf8')],capture_output=True,timeout=45);(a.out/'pad.stdout').write_bytes(pad.stdout);(a.out/'pad.stderr').write_bytes(pad.stderr);assert pad.returncode==0,'actual ordinary RemotePad rejected'
  time.sleep(.25)
 wid=frame();out=a.out/(label+'.png');subprocess.run(['import','-window',wid,str(out)],env=env,check=True,timeout=10);images.append(dict(label=label,path=str(out),sha256=sha(out),window=wid,liveDebugSha256=sha(debug)))
verify_review(path(d['review']));owned();proof=dict(status='PASS_OWNED_EMULATOR_PROGRESS_AND_CAPTURE_IDENTITY_ONLY',launchSha256=sha(a.launch),images=images,padApplied=bool(a.pad_file),qualitativeVisualReviewPending=True,actualReceiverActivationQualified=False,hardwarePerformanceQualified=False,helperSha256=sha(Path(__file__)));(a.out/'capture-proof.json').write_text(json.dumps(proof,indent=2)+'\n',encoding='utf8');print(proof['status'])
