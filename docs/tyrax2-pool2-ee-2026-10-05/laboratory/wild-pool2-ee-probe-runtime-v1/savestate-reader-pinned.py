"""Exact-tag PCSX2 paused savestate decode and optional stock PINE client. No guest writes."""
from pathlib import Path
import argparse,io,json,hashlib,socket,struct,time,zipfile,zlib,ctypes,ctypes.util
COMMIT='94d86c891b1621c0b252e4fc2e155bf90274dcc0'
OFFSETS=dict(cpuSize=1160,cpuAlign=8,pc=680,cycle=1088,count=580,sp=464,ra=496,delaySlot=676)
def sha(b):return hashlib.sha256(b).hexdigest()
def need(v,m):
 if not v:raise ValueError(m)
def read_member(data,z,info):
 need(not info.flag_bits&1,'encrypted ZIP not supported')
 need(0<=info.file_size<=256*1024*1024 and 0<=info.compress_size<=256*1024*1024,'member bounds')
 offset=info.header_offset;need(offset>=0 and offset+30<=len(data),'local ZIP header bounds')
 header=struct.unpack_from('<4s5H3I2H',data,offset);need(header[0]==b'PK\x03\x04','local ZIP signature');need(header[3]==info.compress_type,'local/central compression mismatch');need(header[2]==info.flag_bits,'local/central flags mismatch');need(bool(info.flag_bits&8) or header[6:9]==(info.CRC,info.compress_size,info.file_size),'local/central CRC and size mismatch')
 start=offset+30+header[-2]+header[-1];end=start+info.compress_size;need(end<=len(data) and end<=z.start_dir,'compressed member bounds')
 localname=data[offset+30:offset+30+header[-2]].decode('utf-8' if info.flag_bits&0x800 else 'cp437');need(localname==info.filename,'local/central filename mismatch')
 compressed=data[start:end]
 if info.compress_type==93:
  try:
   import zstandard
   raw=zstandard.ZstdDecompressor().decompress(compressed,max_output_size=info.file_size)
  except ImportError:
   library=ctypes.util.find_library('zstd');need(library,'method93 requires zstandard module or system libzstd')
   lib=ctypes.CDLL(library);lib.ZSTD_decompress.argtypes=[ctypes.c_void_p,ctypes.c_size_t,ctypes.c_void_p,ctypes.c_size_t];lib.ZSTD_decompress.restype=ctypes.c_size_t;lib.ZSTD_isError.argtypes=[ctypes.c_size_t];lib.ZSTD_isError.restype=ctypes.c_uint;lib.ZSTD_getErrorName.argtypes=[ctypes.c_size_t];lib.ZSTD_getErrorName.restype=ctypes.c_char_p
   destination=ctypes.create_string_buffer(max(1,info.file_size));source=ctypes.create_string_buffer(compressed);n=lib.ZSTD_decompress(destination,info.file_size,source,len(compressed));need(not lib.ZSTD_isError(n),'ZSTD decompression: '+str(lib.ZSTD_getErrorName(n)));need(n==info.file_size,'ZSTD decompressed size');raw=destination.raw[:n]
 elif info.compress_type in (0,8):raw=z.read(info)
 else:raise ValueError('unsupported explicit ZIP compression '+str(info.compress_type))
 need(len(raw)==info.file_size,'member uncompressed size');need(zlib.crc32(raw)&0xffffffff==info.CRC,'member CRC32');return raw

def zip_members(data):
 with zipfile.ZipFile(io.BytesIO(data)) as z:
  infos=z.infolist();need(len(infos)<=128,'ZIP entry count bound');need(len({i.filename for i in infos})==len(infos),'duplicate ZIP entries');need(sum(i.file_size for i in infos)<=512*1024*1024,'ZIP total size bound')
  return {i.filename:read_member(data,z,i) for i in infos}

def text_from_elf(path):
 b=Path(path).read_bytes();need(b[:6]==b'\x7fELF\x01\x01','ELF32 little endian required');h=struct.unpack_from('<16sHHIIIIIHHHHHH',b);need(h[1:3]==(2,8),'MIPS executable required');off,esz,count,names=h[6],h[11],h[12],h[13];need(esz==40 and 0<names<count,'ELF sections');sections=[struct.unpack_from('<IIIIIIIIII',b,off+i*esz) for i in range(count)];s=sections[names];st=b[s[4]:s[4]+s[5]];ts=[s for s in sections if st[s[0]:st.index(0,s[0])]==b'.text'];need(len(ts)==1,'one .text');t=ts[0];raw=b[t[4]:t[4]+t[5]];need(len(raw)==t[5] and t[2]&4,'executable .text complete');return dict(path=str(Path(path).resolve()),sha256=sha(b),textAddress=t[3],textBytes=t[5],textSha256=sha(raw)),raw

def parse_bytes(data,layout,elf,expected_pc=None):
 need(layout==OFFSETS,'exact source-derived x64 layout required')
 members=zip_members(data)
 if True:
  names=list(members);required=['PCSX2 Savestate Version.id','PCSX2 Internal Structures.dat','eeMemory.bin'];need(all(names.count(n)==1 for n in required),'unique required ZIP entries')
  version=members[required[0]];need(len(version)==36,'exact version indicator size');sv=struct.unpack_from('<I',version)[0];need(sv==0x9a590000,'exact savestate version');tag=version[4:].split(b'\0',1)[0].decode();need(tag in ['v2.9.93','2.9.93'],'exact emulator tag required')
  internals=members[required[1]];marker=b'cpuRegs'+bytes(25);need(internals.count(marker)==1,'unique cpuRegs32 tag');start=internals.index(marker)+32;cpu=internals[start:start+layout['cpuSize']];need(len(cpu)==layout['cpuSize'],'full actual cpuRegisters');need(internals.find(b'Cycles'+bytes(26),start+len(cpu))>=start+len(cpu),'following Cycles tag required')
  u32=lambda off:struct.unpack_from('<I',cpu,off)[0];u64=lambda off:struct.unpack_from('<Q',cpu,off)[0]
  registers=dict(pc=u32(layout['pc']),cycle=u64(layout['cycle']),cp0Count=u32(layout['count']),spLow64=u64(layout['sp']),raLow64=u64(layout['ra']),isDelaySlot=u32(layout['delaySlot']));need(registers['isDelaySlot'] in [0,1],'delaySlot state');need(registers['pc']%4==0,'aligned EE PC');need(expected_pc is None or registers['pc']==expected_pc,'expected EE breakpoint PC')
  info,text=text_from_elf(elf);ram=members[required[2]];physical=info['textAddress']&0x1fffffff;need(physical+len(text)<=len(ram),'guest .text within EE RAM');need(ram[physical:physical+len(text)]==text,'guest .text differs from exact boot ELF')
  return dict(status='PASS_exact_tag_paused_state_decode_text_identity',sourceCommit=COMMIT,savestateVersion=sv,savestateTag=tag,stateSha256=sha(data),internalsSha256=sha(internals),cpuRawSha256=sha(cpu),cpuRegsPayloadOffset=start,offsets=layout,registers=registers,elf=info,eeMemoryBytes=len(ram),unchangedGuestText=True,limits=['Registers decoded from raw emulator serialization; root must bind this exact layout source to Linux64 compiler result.','cycle is internal64-bit emulated schedule time, not physical EE cost.','Snapshot completion and EE breakpoint identity require separate paused PINE status bracket.'])

def stable_read(path,timeout=20):
 path=Path(path);deadline=time.monotonic()+timeout;previous=None;stable=0;last_issue='file absent'
 while time.monotonic()<deadline:
  try:
   stat=path.stat();identity=(stat.st_size,stat.st_mtime_ns);stable=stable+1 if identity==previous else 0;previous=identity
   if stable>=2:
    b=path.read_bytes();zip_members(b);time.sleep(.25);need(path.read_bytes()==b,'snapshot still changing');return b
  except (OSError,zipfile.BadZipFile,ValueError) as e:last_issue=str(e)
  time.sleep(.25)
 raise TimeoutError('savestate did not stabilize: '+last_issue)

class Pine:
 def __init__(self,unix=None,tcp=None):
  self.s=socket.socket(socket.AF_UNIX if unix else socket.AF_INET,socket.SOCK_STREAM);self.s.settimeout(5);self.s.connect(unix if unix else ('127.0.0.1',tcp))
 def close(self):self.s.close()
 def recv(self,n):
  out=b''
  while len(out)<n:
   b=self.s.recv(n-len(out));need(b,'PINE disconnected');out+=b
  return out
 def request(self,payload):
  self.s.sendall(struct.pack('<I',len(payload)+4)+payload);length=struct.unpack('<I',self.recv(4))[0];need(5<=length<=450000,'PINE length');reply=self.recv(length-4);need(reply[0]==0,'PINE rejected');return reply[1:]
 def status(self):
  b=self.request(bytes([15]));need(len(b)==4,'status size');return struct.unpack('<I',b)[0]
 def save(self,slot):need(0<=slot<=255,'u8 slot');need(not self.request(bytes([9,slot])),'save acknowledgement shape')
 def read32(self,address):
  b=self.request(bytes([2])+struct.pack('<I',address));need(len(b)==4,'read32 shape');return struct.unpack('<I',b)[0]

def main():
 p=argparse.ArgumentParser();p.add_argument('--state',type=Path);p.add_argument('--layout',type=Path);p.add_argument('--elf',type=Path);p.add_argument('--expect-pc',type=lambda s:int(s,0));p.add_argument('--unix');p.add_argument('--tcp',type=int);p.add_argument('--save-slot',type=int);p.add_argument('--status',action='store_true');p.add_argument('--read32',type=lambda s:int(s,0));p.add_argument('--timeout',type=float,default=20);p.add_argument('--out',type=Path,required=True);a=p.parse_args();need(not a.out.exists(),'immutable new report required');need(not (a.unix and a.tcp),'one transport');client=None
 try:
  r={};capture=a.save_slot is not None
  if a.unix or a.tcp:
   client=Pine(a.unix,a.tcp);r['pineStatusBefore']=client.status()
   if capture:
    need(a.state and not a.state.exists(),'fresh unused savestate path required');need(r['pineStatusBefore']==1,'must be paused before snapshot');client.save(a.save_slot)
   if a.read32 is not None:r['read32']={'address':a.read32,'value':client.read32(a.read32)}
  need(not capture or client is not None,'save requires actual PINE connection')
  if a.state:
   need(a.layout and a.elf,'decode requires layout and exact ELF');layout=json.loads(a.layout.read_text(encoding='utf-8-sig'));r.update(parse_bytes(stable_read(a.state,a.timeout),layout,a.elf,a.expect_pc));r['layoutReportSha256']=sha(a.layout.read_bytes());r['statePath']=str(a.state.resolve())
  if client:
   r['pineStatusAfter']=client.status()
   if capture:need(r['pineStatusAfter']==1,'must remain paused after capture')
  r.setdefault('status','PASS_stock_PINE_response_only');r['helperSha256']=sha(Path(__file__).read_bytes());code=0
 except Exception as e:r={'status':'REJECTED','issue':str(e)};code=1
 finally:
  if client:client.close()
 a.out.write_bytes((json.dumps(r,indent=2)+'\n').encode());print(r['status']);return code
if __name__=='__main__':raise SystemExit(main())
