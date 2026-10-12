"""Owned Xvfb rotation smoke captures, selected by read-only PINE phase/offset."""
import json
import os
import re
import socket
import struct
import subprocess
import sys
import time
from pathlib import Path
root=Path(sys.argv[1]);driving='--drive' in sys.argv[2:]
wanted=set(int(v) for v in sys.argv[2:] if v!='--drive')
low,high=(1400,1640) if driving else (200,440)
prefix='drive' if driving else 'park'
symbols=(root/'symbols.txt').read_text()
addresses={name:int(re.search(r'^([0-9a-f]+) \w MotionProbe::'+name+r'$',symbols,re.M)[1],16) for name in ('phase','offset')}
s=socket.socket(socket.AF_UNIX,socket.SOCK_STREAM);s.settimeout(5)
socket_path=Path(os.environ.get('XDG_RUNTIME_DIR','/tmp'))/'pcsx2.sock.29417'
if not socket_path.exists():socket_path=Path('/tmp/pcsx2.sock.29417')
s.connect(str(socket_path))
def recv(n):
    data=b''
    while len(data)<n:
        part=s.recv(n-len(data));assert part;data+=part
    return data
def read(name):
    payload=b'\x02'+struct.pack('<I',addresses[name])
    s.sendall(struct.pack('<I',len(payload)+4)+payload)
    n=struct.unpack('<I',recv(4))[0];r=recv(n-4);assert r[0]==0
    return struct.unpack('<I',r[1:])[0]
records=[];deadline=time.time()+600
while wanted and time.time()<deadline:
    p,o=read('phase'),read('offset')
    if p>max(wanted):break
    if p in wanted and low<=o<high:
        target=root/'emulator'/(prefix+'-phase-'+str(p)+'.png')
        subprocess.run(['import','-display',':97','-window','root',str(target)],check=True)
        after=[read('phase'),read('offset')]
        assert after[0]==p and low<=after[1]<high+40,after
        records.append(dict(phase=p,beforeOffset=o,afterOffset=after[1],file=target.name))
        wanted.remove(p)
    time.sleep(.05)
s.close()
(root/'emulator'/(prefix+'-captures.json')).write_text(json.dumps(dict(captures=records,missed=sorted(wanted)),indent=2)+'\n')
print(records, 'missed',sorted(wanted))
