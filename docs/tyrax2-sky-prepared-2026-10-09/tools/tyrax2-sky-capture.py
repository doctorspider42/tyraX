"""Read-only PINE selected stationary screenshots on the owned hidden Xvfb."""
import json
import os
import re
import socket
import struct
import subprocess
import sys
import time
from pathlib import Path
root=Path(sys.argv[1]);symbols=(root/'symbols.txt').read_text()
addresses={n:int(re.search(r'^([0-9a-f]+) \w MotionProbe::'+n+r'$',symbols,re.M)[1],16) for n in ('phase','offset')}
sockpath=Path(os.environ.get('XDG_RUNTIME_DIR','/tmp'))/'pcsx2.sock.29417'
if not sockpath.exists():sockpath=Path('/tmp/pcsx2.sock.29417')
s=socket.socket(socket.AF_UNIX,socket.SOCK_STREAM);s.settimeout(5);s.connect(str(sockpath))
def recv(n):
    data=b''
    while len(data)<n:
        chunk=s.recv(n-len(data));assert chunk;data+=chunk
    return data
def read(n):
    s.sendall(struct.pack('<IBI',9,2,addresses[n]))
    size=struct.unpack('<I',recv(4))[0];data=recv(size-4)
    if data[0]!=0:return None
    return struct.unpack('<I',data[1:])[0]
wanted=set(range(6));records=[];deadline=time.time()+700
while wanted and time.time()<deadline:
    p,o=read('phase'),read('offset')
    if p is None or o is None:
        time.sleep(.1);continue
    if p>5:break
    if p in wanted and 1240<=o<1290:
        path=root/'emulator'/('park-phase-'+str(p)+'.png')
        subprocess.run(['import','-display',':97','-window','root',str(path)],check=True)
        after=[read('phase'),read('offset')];assert after[0]==p and after[1]<1320
        records.append(dict(phase=p,before=o,after=after[1],file=path.name));wanted.remove(p)
    time.sleep(.025)
s.close()
(root/'emulator/park-captures.json').write_text(json.dumps(dict(records=records,missed=sorted(wanted)),indent=2)+'\n')
assert not wanted,wanted
print(records)
