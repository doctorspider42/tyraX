"""Root-only one observed debugger Run click. Default prepares a plan, never controls UI."""
from pathlib import Path
import argparse,json,hashlib,struct,importlib.util,os,subprocess,re
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
def need(x,s):
 if not x:raise ValueError(s)
def main():
 p=argparse.ArgumentParser();p.add_argument('--owned-launch',type=Path,required=True);p.add_argument('--observation',type=Path,required=True);p.add_argument('--out',type=Path,required=True);p.add_argument('--execute',action='store_true');q=p.parse_args()
 need(not q.out.exists(),'NEW action report');r={};client=None
 try:
  launch=json.loads(q.owned_launch.read_text());obs=json.loads(q.observation.read_text());pid=int(launch['pid']);wid=int(str(obs['window']),0)
  need(obs['pid']==pid and obs['observedAction']=='Run' and obs['observedPhase']=='ELF-entry-pause','owned observed paused-entry Run only')
  need(obs['width']==1322 and obs['height']==974 and obs['x']==110 and obs['y']==47,'only root-observed geometry/click supported')
  screenshot=Path(obs['screenshot']);need(sha(screenshot)==obs['screenshotSha256'],'observed screenshot pin')
  elf=Path(launch['elf']);need(sha(elf)==launch['elfSha256'],'boot ELF pin');entry=struct.unpack_from('<I',elf.read_bytes(),24)[0]
  need(int(str(obs['observedPC']),0)==entry,'observed exact actual ELF entry PC')
  need(sha(Path(launch['ini']))==launch['profileBeforeSha256'],'prelaunch profile drift')
  r=dict(status='PREPARED_OWNED_OBSERVED_RUN_EVENT_NOT_EXECUTED',pid=pid,window=wid,entryPC=f'{entry:08x}',observationSha256=sha(q.observation),screenshotSha256=sha(screenshot),launchSha256=sha(q.owned_launch),helperSha256=sha(Path(__file__)),execute=False)
  if q.execute:
   need(os.name=='posix','Linux owned X11 control only');proc=Path('/proc')/str(pid);need(proc.exists(),'owned PID alive')
   need(sha(proc/'exe')==launch['exeSha256'],'exact owned executable')
   cmd=(proc/'cmdline').read_bytes();need(str(elf).encode()in cmd and str(Path(launch['profile'])).encode()in cmd,'owned ELF/profile process')
   env=os.environ.copy();env['DISPLAY']=obs['display']
   def run(args):return subprocess.check_output(args,env=env,text=True)
   props=run(['xprop','-id',str(wid),'_NET_WM_PID','WM_NAME','_NET_WM_NAME'])
   need(re.search(r'_NET_WM_PID\([^)]*\)\s*=\s*'+str(pid)+r'\b',props),'window PID ownership')
   need('PCSX2 Debugger'in props,'owned debugger window identity')
   geo=run(['xdotool','getwindowgeometry','--shell',str(wid)])
   values=dict(line.split('=',1)for line in geo.splitlines()if '='in line)
   need(values.get('WIDTH')=='1322' and values.get('HEIGHT')=='974','observed client geometry unchanged')
   stdout=Path(launch['profile']).parent/'stdout.log';before=stdout.read_bytes()
   need(b'POOL2PROBE_READY'not in before and b'POOL2PROBE_REJECT'not in before,'never release completed/rejected halt')
   h=Path(__file__).parents[1]/'wild-pool2-static-probe-design-v2/savestate-reader-pinned.py';sp=importlib.util.spec_from_file_location('reader',h);m=importlib.util.module_from_spec(sp);sp.loader.exec_module(m)
   client=m.Pine(unix=launch['socket']);need(client.status()==1,'paused before Run event')
   # Actual XTEST mouse event into the observed client rectangle, not unfocused XSendEvent key input.
   subprocess.run(['xdotool','mousemove','--window',str(wid),'110','47','click','1'],env=env,check=True)
   r.update(status='PASS_SINGLE_OBSERVED_OWNED_RUN_EVENT_DISPATCH_ONLY',execute=True,pineStatusBefore=1,pineStatusAfter=client.status(),windowProperties=props,windowGeometry=geo,stdoutBeforeSha256=hashlib.sha256(before).hexdigest(),guestResumeOrFinalHaltAccepted=False)
 except Exception as e:r=dict(status='REJECTED_OWNED_OBSERVED_RUN_ACTION',issue=str(e),helperSha256=sha(Path(__file__)))
 finally:
  if client:client.close()
 q.out.write_text(json.dumps(r,indent=2)+'\n',encoding='utf8');print(r['status']);return 1 if r['status'].startswith('REJECTED')else 0
if __name__=='__main__':raise SystemExit(main())
