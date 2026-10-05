"""Strict saved actual VU bytes decoder; no bool-only snapshot acceptance."""
from pathlib import Path
import struct,json,hashlib,argparse,importlib.util
BASES=(22,483);COUNT=0x3ff;SPRITE=0x800;STATE=0x8000
sha=lambda b:hashlib.sha256(b).hexdigest()
def need(v,s):
 if not v:raise ValueError(s)
def qw(b,i):need(0<=i<1024,'qword bounds');return b[i*16:(i+1)*16]
def words(b,i):return struct.unpack('<4I',qw(b,i))
def source(case,index,epoch):
 k=(0,1,2,0,2,3)[index%6];x=(.5,-.5,-.5,.5)[k];y=(-.5,-.5,.5,.5)[k]
 p=struct.pack('<4f',x,y,1.0 if case==2 and k in(1,2) else 0.0,1)
 s=struct.pack('<4f',float(k in(1,2)),y+.5,1,0)
 c=struct.pack('<4f',31+epoch-1+(8 if case==3 and index%6==1 else 0),63,95,128)
 return p,s,c
def tag(b):return int.from_bytes(b,'little')
def valid_quad(v):
 rgba=[struct.unpack('<4I',x[1])for x in v];xyz=[struct.unpack('<4I',x[2])for x in v];q=[struct.unpack('<4f',x[0])[2]for x in v]
 need(all(x==rgba[0]for x in rgba),'final nonconstant RGBA');need(all(x[2:]==xyz[0][2:]for x in xyz),'final nonconstant Z/F/ADC');need(all(x==q[0]for x in q)and .0001<=q[0]<=10000,'Q equality/domain')
 need(0<=xyz[0][2]<=0xffff0 and 0<=xyz[0][3]<=0xfff,'Z/F/ADC bounds');xy=[x[:2]for x in xyz]
 need(xy[0]==xy[3]and xy[2]==xy[4]and xy[0][0]==xy[5][0]and xy[1][0]==xy[2][0]and xy[0][1]==xy[1][1]and xy[2][1]==xy[5][1],'rectangle topology')
 need(0<xy[2][0]-xy[0][0]<=32767 and 0<abs(xy[2][1]-xy[0][1])<=32767,'rectangle spans')
 return (0,2)if xy[2][1]>xy[0][1]else(5,1)
def decode(data,case,arm,repeats=1):
 need(len(data)==16384 and case in(1,2,3,4)and arm in(0,1)and repeats in(1,2,3),'capture/config shape')
 expected={75,21}if case==4 else{72};banks={};ignored=[]
 # Actual options must enable both nonconstant-capable fog and the selected spot loop.
 option=words(data,8);need(struct.unpack('<h',qw(data,8)[4:6])[0]<0,'actual signed low16 spot loop active');need(qw(data,8)[8:]==bytes.fromhex('01007fc20048b744'),'exact authored fog 19..23 coefficients');need(b''.join(qw(data,i)for i in range(12,15))==bytes.fromhex('0000000000000000000080bf0ad7233c00000000000000000000803ffd04f73c0000804000000041000040411bf0a23e'),'exact authored actual spot qwords 12..14')
 for base in BASES:
  packed=words(data,base)[3];n=packed&COUNT
  if n not in expected:ignored.append(dict(base=base,reason='not final requested package count'));continue
  need(words(data,base)[2]==struct.unpack('<I',struct.pack('<f',32767.5))[0],'actual 16bit depth scale');sourceOffset=75 if case==4 and n==21 else 0
  if any(qw(data,base+2+2*n+i)!=source(case,sourceOffset+i,repeats)[2]for i in range(n)):
   ignored.append(dict(base=base,reason='source colors not final requested epoch'));continue
  need(n not in banks and packed&~(COUNT|SPRITE|STATE)==0,'unique current bank/header flags')
  marker=bool(packed&SPRITE);need(marker==bool(arm and case!=4),'actual request marker')
  for i in range(n):
   p,s,c=source(case,sourceOffset+i,repeats)
   need(qw(data,base+2+i)==p and qw(data,base+2+n+i)==s and qw(data,base+2+2*n+i)==c,'actual source topology/ST/color epoch')
  kick=base+2+3*n+int(marker);state=9 if packed&STATE else 1;primitive=kick+state-1
  need(kick+state+3*n<=base+461 and kick+state+3*n<=944,'original output extent')
  headers=[qw(data,kick+i)for i in range(state)];t=tag(headers[-1]);typ=(t>>47)&7;loops=t&0x7fff
  need((t>>58)&3==0 and (t>>60)&15==3 and (t>>64)&0xfff==0x412 and (t>>15)&1==1,'packed GIF header')
  need((t>>(47+5))&1==1,'actual primitive FGE enabled')
  sprite=bool(arm and case==1);need(typ==(6 if sprite else 3)and loops==(n//3 if sprite else n),'actual completed primitive route')
  if marker:
   originalTriangle=(t&~(0x7fff|(7<<47)))|n|(3<<47)
   expectedRequest=(originalTriangle&~(0x7fff|(7<<47)))|(n//3)|(6<<47)
   need(tag(qw(data,kick-1))==expectedRequest,'actual inline source sprite request tag')
  out=[qw(data,kick+state+i)for i in range(3*(n//3 if sprite else n))]
  vertices=[out[i:i+3]for i in range(0,len(out),3)]
  fog=[(struct.unpack('<4I',v[2])[3]>>4)&255 for v in vertices];rgba=[struct.unpack('<4I',v[1])for v in vertices]
  need(all(0<f<255 for f in fog),'actual fog interior witness')
  if case==2:need(len(set(fog))>=2,'negative case requires genuinely nonuniform final F')
  if case==3:need(any(len(set(rgba[i:i+6]))>=2 for i in range(0,len(rgba),6)),'negative case requires genuinely nonuniform final RGBA')
  if case==1:
   need(len(set(fog))==1 and len(set(rgba))==1,'positive actual constant fog/light output witness')
   if not arm:
    for i in range(0,n,6):valid_quad(vertices[i:i+6])
  banks[n]=dict(base=base,count=n,sourceOffset=sourceOffset,kickQword=kick,stateQwords=state,primitiveTag=hex(t),sprite=sprite,marker=marker,headerBytes=b''.join(headers),outputBytes=b''.join(out),vertices=vertices,fogValues=fog,rgbaValues=rgba,epoch=repeats,inputTopologyValidated=True)
 need(set(banks)==expected,'complete latest-epoch bank coverage');return banks
def compare(a,b,case,repeats=1):
 x,y=decode(a,case,0,repeats),decode(b,case,1,repeats);need(qw(a,8)==qw(b,8) and a[12*16:15*16]==b[12*16:15*16],'paired actual fog/spot uniforms equality');packages=[]
 for n in sorted(x):
  off,on=x[n],y[n];need(off['stateQwords']==on['stateQwords'],'material layout equality');need(off['headerBytes'][:-16]==on['headerBytes'][:-16],'material prefix equality')
  ot,nt=int(off['primitiveTag'],16),int(on['primitiveTag'],16)
  if on['sprite']:
   need(nt==(ot&~(0x7fff|(7<<47)))|(n//3)|(6<<47),'type/count-only primitive change')
   expected=[]
   for i in range(0,n,6):
    pair=valid_quad(off['vertices'][i:i+6]);expected.extend(off['vertices'][i+k]for k in pair)
   need(on['vertices']==expected,'actual compact endpoints equality')
  else:need(off['headerBytes']==on['headerBytes']and off['outputBytes']==on['outputBytes'],'actual complete triangle fallback byte equality')
  packages.append(dict(count=n,sourceOffset=off['sourceOffset'],sprite=on['sprite'],decodedSprites=n//6 if on['sprite'] else 0,baselineOutputSha256=sha(off['outputBytes']),candidateOutputSha256=sha(on['outputBytes']),fogUnique=sorted(set(off['fogValues'])),rgbaUnique=sorted(set(off['rgbaValues']))))
 return dict(status='PASS_ACTUAL_CORONA_SAVED_VU_OUTPUT_PAIR',case=case,repeats=repeats,packages=packages,sourceFinalEpochValidated=True,inputTopologyValidated=True,actualFogLightUniformsValidated=True,actualPositiveSpriteValidated=case==1,actualNegativeOutputWitnessValidated=case in(2,3),splitPackageFallbackValidated=case==4,GSConsumptionRasterOrPerformanceAccepted=False)
def extract(state):
 p=Path(__file__).parent/'savestate-reader-pinned.py';sp=importlib.util.spec_from_file_location('reader',p);r=importlib.util.module_from_spec(sp);sp.loader.exec_module(r);raw=Path(state).read_bytes();need(len(raw)<=512*1024*1024,'state size');m=r.zip_members(raw)
 need(all(k in m and len(m[k])==16384 for k in('vu1Memory.bin','vu1MicroMem.bin')),'actual VU members');v=m['PCSX2 Savestate Version.id'];need(len(v)==36 and struct.unpack_from('<I',v)[0]==0x9a590000 and v[4:].split(b'\0')[0]in(b'v2.9.93',b'2.9.93'),'pinned savestate schema');return m['vu1Memory.bin'],m['vu1MicroMem.bin']
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('--baseline',type=Path,required=True);p.add_argument('--table',type=Path,required=True);p.add_argument('--case',type=int,choices=(1,2,3,4),required=True);p.add_argument('--repeats',type=int,choices=(1,2,3),required=True);p.add_argument('--savestates',action='store_true');p.add_argument('--out',type=Path,required=True);a=p.parse_args();need(not a.out.exists(),'NEW output')
 if a.savestates:x,m=extract(a.baseline);y,n=extract(a.table);need(m==n,'paired resident microcode equality');micro=sha(m)
 else:x=a.baseline.read_bytes();y=a.table.read_bytes();micro=None
 report=compare(x,y,a.case,a.repeats);report['microcodeSha256']=micro;report['inputFiles']={str(f):sha(f.read_bytes())for f in(a.baseline,a.table,Path(__file__))};report['rootOwnedSourceNativeConfigProtocolRequired']=True;a.out.write_bytes((json.dumps(report,indent=2)+'\n').encode());print(report['status'])
