"""Strict offline final inside lattice/Pool2 VU decoder; no devices on import."""
from pathlib import Path
import struct,json,hashlib,argparse,importlib.util
BASES=(22,483);TABLE=0x400;LATTICE=0x800;STATE=0x8000;FIELDS=0x3ff
sha=lambda b:hashlib.sha256(b).hexdigest()
def need(x,msg):
 if not x:raise ValueError(msg)
def qword(b,i):
 need(0<=i<1024,'qword address');return b[i*16:(i+1)*16]
def words(b,i):return struct.unpack('<4I',qword(b,i))
def tag(b):
 low,high=struct.unpack('<QQ',b);nr=(low>>60)&15;nr=nr or 16
 return dict(loops=low&0x7fff,eop=(low>>15)&1,flg=(low>>58)&3,nreg=nr,registers=[(high>>(4*i))&15 for i in range(nr)])
def input_source(index):
 member,occurrence=divmod(index,96);cell,corner=divmod(occurrence,6)
 grid=(cell//4)*5+cell%4+(0,1,6,0,6,5)[corner];ix,iz=grid%5,grid//5
 return struct.pack('<4f',-4.0+member*4.0+ix,-2.0+iz,(ix*ix+iz)*0.0625+member*0.125,1.0),struct.pack('<4f',ix*0.25,iz*0.25,1.0,0.0)
def color(member,repeats,integer=False):
 epoch=repeats-1;v=(31+epoch,63,95,128)if member==0 else(95-epoch,15,1,128)
 return struct.pack('<4I'if integer else'<4f',*v)
def expected_unique(source,n):
 seen={};positions=[];sts=[];indices=[]
 for i in range(n):
  p,s=input_source(source+i);key=(p,s)
  if key not in seen:seen[key]=len(positions);positions.append(p);sts.append(s)
  indices.append(seen[key])
 return positions,sts,indices
def decode(data,case,arm,repeats=1):
 need(len(data)==16384 and case in('A96','B192')and arm in(0,1)and repeats in(1,2,3),'capture shape/config')
 expected={75,21}if case=='A96'else{75,42};banks={}
 for base in BASES:
  packed=words(data,base)[3];n=packed&FIELDS
  need(n in expected and n not in banks and packed&~(FIELDS|TABLE|LATTICE|STATE)==0,'exact final package header')
  source=(0 if n==75 else 75)if case=='A96'else(75 if n==75 else 150)
  crossing=source//96!=(source+n-1)//96
  lattice=bool(arm and not crossing)
  need(bool(packed&LATTICE)==lattice and bool(packed&TABLE)==(not lattice),'actual route marker')
  state=9 if packed&STATE else 1
  if lattice:
   ps,ss,index=expected_unique(source,n);u=len(ps);t=n//3;desc=words(data,base+2)
   need(desc==(u,t,4+2*u+t,4+5*u+t),'lattice descriptor unique/tri/input/cache/kick')
   need(0<u<n and u<=25 and t<=25 and desc[3]+9+3*n<=461,'lattice extent bounds')
   need(qword(data,base+3)==color(source//96,repeats),'actual lattice input coefficient epoch')
   for i in range(u):
    need(qword(data,base+4+i)==ps[i]and qword(data,base+4+u+i)==ss[i],'unique input position/ST bits')
   for i in range(t):
    need(words(data,base+4+2*u+i)==tuple(index[3*i:3*i+3])+ (0,),'actual triangle indices/order/reserved')
   kick=base+desc[3];layout='lattice';descriptor=desc
  else:
   for i in range(n):
    p,s=input_source(source+i)
    need(qword(data,base+2+i)==p and qword(data,base+2+n+i)==s,'Pool2 input position/ST bits')
   desc=words(data,base+2+2*n);first=min(n,96-source%96)
   need(desc==(first,n,1,0),'Pool2 descriptor run/count/version/reserved')
   need(qword(data,base+3+2*n)==color(source//96,repeats)and qword(data,base+4+2*n)==color((source+n-1)//96,repeats),'Pool2 input coefficient epochs')
   kick=base+2+2*n+3;layout='pool2';descriptor=desc
  end=kick+state+3*n;need(end<=base+461 and end<=944,'actual output bounds')
  headers=[qword(data,kick+i)for i in range(state)];prim=tag(headers[-1])
  need(prim==dict(loops=n,eop=1,flg=0,nreg=3,registers=[2,1,4]),'packed primitive ST RGBAQ XYZF2')
  if state==9:
   for i in(0,2,4,6):
    h=tag(headers[i]);need(h['loops']==1 and h['flg']==0 and h['nreg']==1 and h['registers']==[14],'material A+D tag')
  output=data[(kick+state)*16:end*16]
  for i in range(n):need(output[(3*i+1)*16:(3*i+2)*16]==color((source+i)//96,repeats,True),'actual final output RGBA epoch')
  fog=[(struct.unpack_from('<I',output,(3*i+2)*16+12)[0]>>4)&255 for i in range(n)]
  need(len(set(fog))>=2 and all(0<f<255 for f in fog),'actual nonconstant interior fog coefficients required')
  banks[n]=dict(base=base,count=n,sourceOffset=source,layout=layout,stateQwords=state,kickQword=kick,descriptor=descriptor,headerBytes=b''.join(headers),outputBytes=output,epoch=repeats,inputTopologyValidated=True,fogValues=fog,nonconstantInteriorFogValidated=True)
 need(set(banks)==expected,'complete final bank counts');return banks
def compare(a,b,case,repeats=1):
 x,y=decode(a,case,0,repeats),decode(b,case,1,repeats);packages=[]
 for n in sorted(x):
  need(x[n]['stateQwords']==y[n]['stateQwords']and x[n]['headerBytes']==y[n]['headerBytes'],'actual material/primitive bytes differ')
  need(x[n]['outputBytes']==y[n]['outputBytes'],'actual STQ/RGBAQ/XYZF2/ADC payload differs')
  packages.append(dict(count=n,sourceOffset=x[n]['sourceOffset'],baselineLayout=x[n]['layout'],candidateLayout=y[n]['layout'],headerSha256=sha(x[n]['headerBytes']),outputSha256=sha(x[n]['outputBytes']),outputQwords=3*n,fogValues=x[n]['fogValues'],fogUnique=sorted(set(x[n]['fogValues']))))
 return dict(status='PASS_ACTUAL_FINAL_FOG_LATTICE_POOL2_PACKED_OUTPUT_BYTE_EQUAL',case=case,repeats=repeats,packages=packages,finalEpochRGBAValidated=True,inputTopologyValidated=True,nonconstantInteriorFogValidated=True,first75Overwritten=case=='B192',wholeBankCompared=False,GSConsumptionRasterOrPerformanceAccepted=False)
def extract(state):
 p=Path(__file__).parent/'savestate-reader-pinned.py';sp=importlib.util.spec_from_file_location('reader',p);r=importlib.util.module_from_spec(sp);sp.loader.exec_module(r)
 raw=Path(state).read_bytes();need(len(raw)<=512*1024*1024,'state bound');m=r.zip_members(raw)
 for key in('vu1Memory.bin','vu1MicroMem.bin'):need(key in m and len(m[key])==16384,'exact actual VU member')
 v=m['PCSX2 Savestate Version.id'];need(len(v)==36 and struct.unpack_from('<I',v)[0]==0x9a590000 and v[4:].split(b'\0')[0]in(b'v2.9.93',b'2.9.93'),'exact pinned schema/tag')
 return m['vu1Memory.bin'],m['vu1MicroMem.bin']
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('--baseline',type=Path,required=True);p.add_argument('--table',type=Path,required=True);p.add_argument('--case',choices=('A96','B192'),required=True);p.add_argument('--repeats',type=int,choices=(1,2,3),required=True);p.add_argument('--savestates',action='store_true');p.add_argument('--out',type=Path,required=True);a=p.parse_args();need(not a.out.exists(),'NEW report')
 if a.savestates:x,m=extract(a.baseline);y,n=extract(a.table);need(m==n,'same resident microcode');micro=[sha(m),sha(n)]
 else:x=a.baseline.read_bytes();y=a.table.read_bytes();micro=[]
 report=compare(x,y,a.case,a.repeats);report['microcodeSha256']=micro;report['inputFiles']={str(f):sha(f.read_bytes())for f in(a.baseline,a.table,Path(__file__))};report['rootOwnedSourceNativeConfigProtocolRequired']=True
 a.out.write_text(json.dumps(report,indent=2)+'\n');print(report['status'])
