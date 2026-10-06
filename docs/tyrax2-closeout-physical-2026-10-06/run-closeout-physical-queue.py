from pathlib import Path
import subprocess,time,json,re,sys
lab=Path('F:/Projects/tyrax2-lab-20261001')
queue=[('tyrax2-closeout-ps2-v4-kind0-order1-v1',0,1,None,None),('tyrax2-closeout-ps2-v4-kind1-order0-v1',1,0,None,None),('tyrax2-closeout-ps2-v4-kind1-order1-v1',1,1,None,None),('tyrax2-closeout-ps2-v4-kind2-order0-v1',2,0,None,None),('tyrax2-closeout-ps2-v4-kind2-order1-v1',2,1,None,None),('tyrax2-closeout-ps2-quiet-motion-v4-v1',3,0,'vehicle',lab/'tyrax2-closeout-motion-v1/quiet-v4/vehicle/game/bin'),('tyrax2-closeout-ps2-quiet-showcase-v4-v1',3,0,'gate',lab/'tyrax2-closeout-motion-v1/quiet-v4/showcase/game/bin')]
def ps(script,args):subprocess.run(['pwsh','-NoProfile','-File',str(lab/script)]+args,check=True)
for i,(name,kind,order,route,source)in enumerate(queue):
 run=lab/name
 if i:
  previous=lab/queue[i-1][0]
  ps('reset-owned-closeout.ps1',['-RunPath',str(previous)])
  time.sleep(8)
  args=['-Kind',str(kind),'-Order',str(order),'-RunName',name]
  if source:args+=['-SourceBin',str(source),'-ElfName','vehicle-playground'if route=='vehicle'else'showcase']
  ps('launch-closeout-physical.ps1',args)
 start=time.monotonic();last=None
 while True:
  b=(run/'ps2client.stdout').read_bytes()
  if b'freepad: DMA Busy'in b:raise RuntimeError(name+': DMA Busy; physical reset required')
  if any(s in b for s in (b'Texture missing:',b'TmdlLoader: cannot read',b'Failed to load texture')):raise RuntimeError(name+': missing assets')
  phase=re.findall(rb'LOG: CLOSEOUTPHASE phase=(\d+)',b)
  progress=phase[-1].decode()if phase else'loading'
  if progress!=last:print(name,'phase',progress,flush=True);last=progress
  if b'LOG: CLOSEOUTDONE'in b:break
  if time.monotonic()-start>300:raise RuntimeError(name+': no complete protocol after 300s')
  time.sleep(5)
 args=[sys.executable,str(lab/'finish-closeout-physical.py'),str(run)]
 if route:args+=['--route',route]
 subprocess.run(args,check=True)
 print('ACCEPTED_COMPLETE_PROTOCOL',name,flush=True)
print('QUEUE_COMPLETE_LAST_SHOWCASE_LEFT_RUNNING',flush=True)
