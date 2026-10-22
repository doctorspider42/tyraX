"""Offline precursor decoder only; actual sealed VU layout must bind this ABI."""
from pathlib import Path
import struct,json,hashlib,argparse,importlib.util,zipfile,io
BASES=(22,483);FLAG=0x400;STATE=0x8000;FIELDS=0x3ff
sha=lambda b:hashlib.sha256(b).hexdigest()
def need(ok,msg):
 if not ok:raise ValueError(msg)
def qword(b,index):
 need(0<=index<1024,'VU qword range');return b[index*16:(index+1)*16]
def words(b,index):return struct.unpack('<4I',qword(b,index))
def tag(b):
 low,high=struct.unpack('<QQ',b);nreg=(low>>60)&15;nreg=nreg or 16
 return dict(loops=low&0x7fff,eop=(low>>15)&1,pre=(low>>46)&1,prim=(low>>47)&0x7ff,flg=(low>>58)&3,nreg=nreg,registers=[(high>>(4*i))&15 for i in range(nreg)])
def decode(data,case,arm):
 need(len(data)==16384,'exact VU1 local memory size');need(case in('A96','B192')and arm in(0,1),'case/arm')
 expected={75,21}if case=='A96'else{75,42};banks={}
 for base in BASES:
  packed=words(data,base)[3];n=packed&FIELDS
  need(n in expected and n not in banks,'final unique named package counts');need(packed&~(FIELDS|FLAG|STATE)==0,'inside TC flags only')
  table=bool(packed&FLAG);state=9 if packed&STATE else 1;need(table==bool(arm),'actual TC table marker disagrees with requested arm')
  extent=2+2*n+3 if table else 2+3*n;kick=base+extent;end=kick+state+3*n;need(end<=base+461 and end<=944,'bank output capacity')
  descriptor=None
  if table:
   desc=words(data,base+2+2*n);need(desc[1:]==(n,1,0),'descriptor N/version/reserved');need(0<desc[0]<=n and desc[0]%3==0,'triangle run boundary')
   first=(21 if case=='B192'and n==75 else n);need(desc[0]==first,'actual member96 run boundary');descriptor=desc
  headers=[qword(data,kick+i)for i in range(state)];prim=tag(headers[-1]);need(prim['loops']==n and prim['eop']==1 and prim['flg']==0 and prim['nreg']==3 and prim['registers']==[2,1,4],'packed ST RGBAQ XYZF2 primitive tag')
  if state==9:
   for i in(0,2,4,6):
    t=tag(headers[i]);need(t['loops']==1 and t['flg']==0 and t['nreg']==1 and t['registers']==[14],'one-loop A+D state tag')
  output=data[(kick+state)*16:end*16];need(len(output)==n*3*16,'complete native output')
  banks[n]=dict(base=base,count=n,table=table,stateQwords=state,kickQword=kick,descriptor=descriptor,primitive=prim,headerBytes=b''.join(headers),outputBytes=output)
 need(set(banks)==expected,'final banks match case');return banks
def compare(a,b,case):
 x,y=decode(a,case,0),decode(b,case,1);result=[]
 for n in sorted(x):
  need(x[n]['stateQwords']==y[n]['stateQwords'],'material state route differs')
  need(x[n]['headerBytes']==y[n]['headerBytes'],'actual material/primitive payload mismatch')
  need(x[n]['outputBytes']==y[n]['outputBytes'],'actual STQ RGBAQ XYZF2/ADC payload mismatch')
  result.append(dict(count=n,legacyBase=x[n]['base'],tableBase=y[n]['base'],legacyKick=x[n]['kickQword'],tableKick=y[n]['kickQword'],stateQwords=x[n]['stateQwords'],headerSha256=sha(x[n]['headerBytes']),outputSha256=sha(x[n]['outputBytes']),outputQwords=n*3))
 return dict(status='PASS_TESTED_ACTUAL_PACKED_VU_PAYLOAD_BYTE_EQUAL_ONLY',case=case,packages=result,baselineVu1Sha256=sha(a),tableVu1Sha256=sha(b),wholeBankCompared=False,first75Overwritten=case=='B192',GSConsumptionRasterUniversalEqualityOrPerformanceAccepted=False)
def extract(state):
 sp=importlib.util.spec_from_file_location('reader',Path(__file__).with_name('savestate-reader-pinned.py'));r=importlib.util.module_from_spec(sp);sp.loader.exec_module(r);raw=Path(state).read_bytes();need(len(raw)<=512*1024*1024,'snapshot archive bound')
 with zipfile.ZipFile(io.BytesIO(raw))as z:
  infos=z.infolist();need(len(infos)<=128 and len({i.filename for i in infos})==len(infos),'bounded unique ZIP members');byName={i.filename:i for i in infos};m={}
  for name,size in [('PCSX2 Savestate Version.id',36),('vu1Memory.bin',16384),('vu1MicroMem.bin',16384)]:
   need(name in byName,'required member '+name);info=byName[name];need(info.file_size==size and info.compress_size<=65536,'exact bounded selected member '+name);m[name]=r.read_member(raw,z,info)
 version=m['PCSX2 Savestate Version.id'];need(struct.unpack_from('<I',version)[0]==0x9a590000,'exact schema');need(version[4:].split(b'\0')[0]in(b'v2.9.93',b'2.9.93'),'exact emulator tag');return m['vu1Memory.bin'],m['vu1MicroMem.bin']

if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('--baseline',type=Path,required=True);p.add_argument('--table',type=Path,required=True);p.add_argument('--case',choices=['A96','B192'],required=True);p.add_argument('--savestates',action='store_true');p.add_argument('--out',type=Path,required=True);a=p.parse_args();need(not a.out.exists(),'immutable NEW report');micro=[]
 if a.savestates:
  x,m=extract(a.baseline);y,n=extract(a.table);micro=[sha(m),sha(n)];need(m==n,'same correctness ELF resident microcode must match')
 else:x=a.baseline.read_bytes();y=a.table.read_bytes()
 report=compare(x,y,a.case);report['microcodeSha256']=micro;report['inputFiles']={str(f):sha(f.read_bytes())for f in(a.baseline,a.table,Path(__file__))};report['rootPausedCompletionTextConfigAuthorityRequired']=True;a.out.write_text(json.dumps(report,indent=2)+'\n',encoding='utf8');print(report['status'])
