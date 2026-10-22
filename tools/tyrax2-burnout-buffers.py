"""Offline GS buffer metadata only; no guest writes or emulator/hardware pricing."""
import argparse,hashlib,importlib.util,io,json,struct
from pathlib import Path

def module(name,path):
 spec=importlib.util.spec_from_file_location(name,path)
 result=importlib.util.module_from_spec(spec);spec.loader.exec_module(result)
 return result

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--state-reader',type=Path,required=True)
p.add_argument('--states',type=Path,required=True)
p.add_argument('--dumps',type=Path,required=True)
p.add_argument('--output',type=Path,required=True)
a=p.parse_args()
repo=Path(__file__).resolve().parents[1]
capture=repo/'.claude/skills/tyra-testing/scripts/pcsx2-capture.py'
reader=module('state_reader',a.state_reader);gs=module('capture_reader',capture)
sha=lambda b:hashlib.sha256(b).hexdigest()
names=('PRIM','PRMODECONT','TEXCLUT','SCANMSK','TEXA','FOGCOL','DIMX','DTHE','COLCLAMP','PABE',
 'BITBLTBUF','TRXDIR','TRXPOS','TRXREG','TRXREG_')
contexts=('XYOFFSET','TEX0','TEX1','CLAMP','MIPTBP1','MIPTBP2','SCISSOR','ALPHA','TEST','FBA','FRAME','ZBUF')

def env(data):
 assert len(data)>=316 and struct.unpack_from('<I',data)[0]==9,'GS freeze version'
 out={'PRMODE':0};offset=4
 for key in (*names,*(f'{n}_{c}' for c in (1,2) for n in contexts)):
  out[key]=struct.unpack_from('<Q',data,offset)[0];offset+=8
 return out

def frame(value):
 psm=(value>>24)&63
 assert psm in gs.PSM,'Unknown FRAME format'
 return {'raw':f'{value:016x}','block':(value&511)*32,'width64':(value>>16)&63,'format':gs.PSM[psm]}

states=[]
for slot in range(1,7):
 path=a.states/f'SLES-52584 (75BECC18).{slot:02d}.p2s'
 raw=path.read_bytes();members=reader.zip_members(raw);e=env(members['GS.bin'])
 states.append({'slot':slot,'stateSha256':sha(raw),'gsSha256':sha(members['GS.bin']),
  'frames':[frame(e[f'FRAME_{c}']) for c in (1,2)],'dthe':e['DTHE'],
  'version':members['PCSX2 Savestate Version.id'].hex()})

dumps=[]
for path in sorted(a.dumps.glob('*SLES-52584*.gs.zst')):
 raw=path.read_bytes();stream=io.BytesIO(gs.read_zst(path))
 def take(n):
  b=stream.read(n);assert len(b)==n,'Truncated GS dump';return b
 u32=lambda:struct.unpack('<I',take(4))[0]
 assert u32()==0xffffffff
 header=take(u32());version,size,offset,length,crc=struct.unpack_from('<5I',header)
 assert version==9 and crc==0x75becc18 and header[offset:offset+length]==b'SLES-52584'
 walker=gs.GsWalker(env(take(size)));regs=take(8192)
 privileged=[{'event':0,'vsync':0,'registers':gs.priv_regs(regs)}]
 event=0
 while stream.tell()<len(stream.getbuffer()):
  kind=take(1)[0];event+=1
  if kind==0:
   take(1);walker.packet(take(u32()))
  elif kind==1:
   take(1);walker.vsync+=1
  elif kind==2:u32()
  elif kind==3:
   current=gs.priv_regs(take(8192))
   if current!=privileged[-1]['registers']:
    privileged.append({'event':event,'vsync':walker.vsync,'registers':current})
  else:raise AssertionError(f'Unknown dump event {kind}')
 draws=[{'block':key[0],'format':key[1],'coverage':count,'primitives':walker.draws[key]}
  for key,count in sorted(walker.fill.items())]
 dumps.append({'name':path.name,'sha256':sha(raw),'events':event,'vsyncs':walker.vsync,
  'privilegedChanges':privileged,'targets':draws})
assert len(states)==6 and len(dumps)==2
report={'scope':'Offline formats/addresses only; drained snapshots and untimed dump events do not establish VBlank/copy timing or physical cost.',
 'readerSha256':sha(a.state_reader.read_bytes()),'captureSha256':sha(capture.read_bytes()),
 'auditSha256':sha(Path(__file__).read_bytes()),'states':states,'dumps':dumps}
a.output.write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2))
