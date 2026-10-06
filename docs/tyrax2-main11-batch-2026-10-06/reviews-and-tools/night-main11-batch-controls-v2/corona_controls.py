"""Strict kind9 controls and completed-output decoder, offline only."""
from pathlib import Path
import argparse,json,re,struct,hashlib
BASE=Path(__file__).resolve().parent
U32=(1<<32)-1
DEFINES=(BASE/'source-controls/stapip_vu1_shared_defines.h').read_text()
COUNT_MASK=int(re.search(r'#define VU1_STAPIP_COUNT_MASK (0x[0-9A-Fa-f]+)',DEFINES).group(1),16)
CORONA_FLAG=int(re.search(r'#define VU1_STAPIP_CORONA_SPRITE_FLAG (0x[0-9A-Fa-f]+)',DEFINES).group(1),16)
def require(x,msg='invalid'): 
 if not x:raise ValueError(msg)
def source_expression(name,c,p=0):
 s=(BASE/'source-controls/night_plan.hpp').read_text();m=re.search(r'inline bool '+name+r'\([^\n]+?\)\{return ([^;]+);\}',s);require(m is not None,'source expression');e=m.group(1)
 for k,v in zip(['kind','order','joint','restored'],c):e=e.replace('c.'+k,str(v))
 e=e.replace('&&',' and ').replace('||',' or ');return bool(eval(e,{'__builtins__':{}},{'p':p}))
def parse_plan(text):
 require(re.fullmatch(r'[\t\n\v\f\r ]*[0-9]+[\t\n\v\f\r ]+[0-9]+[\t\n\v\f\r ]+[0-9]+[\t\n\v\f\r ]+[0-9]+[\t\n\v\f\r ]*',text) is not None,'strict four decimal tokens')
 values=tuple(int(v) for v in text.split());require(all(v<=U32 for v in values),'unsigned overflow');require(source_expression('valid',values),'source validity');return values
def strict_row(line,prefix,keys):
 require(line.startswith(prefix),'row prefix');rest=line[len(prefix):];pairs=rest.split(' ');require(len(pairs)==len(keys),'row field count');result={}
 for pair,key in zip(pairs,keys):
  if key=='samplePtr':require(re.fullmatch(key+r'=[0-9a-fA-F]{8}',pair) is not None,'sample pointer');v=int(pair.split('=')[1],16)
  else:require(re.fullmatch(key+r'=[0-9]+',pair) is not None,'row field');v=int(pair.split('=')[1])
  require(v<=U32,'row overflow');result[key]=v
 return result
def check_log(text,order):
 cfg=parse_plan(f'9 {order} 0 0');phases={};gates={};table={};masks={};extras={}
 for rawLine in text.splitlines():
  # Match the qualified loop parser's LOG search; preserve the entire record.
  record=re.search(r'LOG: (.*)',rawLine)
  if record is None:continue
  line='LOG: '+record[1]
  if line.startswith('LOG: NIGHTCORONAPHASE'):
   r=strict_row(line,'LOG: NIGHTCORONAPHASE ',['phase','first','selected','enabled']);p=r['phase'];require(p<3 and p==len(phases) and p not in phases,'duplicate/out-of-order phase');require(r=={'phase':p,'first':p*1800,'selected':1,'enabled':int(source_expression('coronaSpriteEnabled',cfg,p))},'phase arm');phases[p]=r
  elif line.startswith('LOG: NIGHTCORONAGATES'):
   keys=['phase','offset','selected','enabled','coldPackets','eligible','requested','fallback','sourceVertices','requestedVertices','fogOn','shaderLit','invalid','acceptedOutputKnown'];r=strict_row(line,'LOG: NIGHTCORONAGATES ',keys);p=r['phase'];o=r['offset'];require(p in phases and p<3 and o in [750,1155] and (p,o) not in gates,'gate epoch');en=int(source_expression('coronaSpriteEnabled',cfg,p));require(r['selected']==1 and r['enabled']==en and r['invalid']==r['acceptedOutputKnown']==0,'gate arm/validity');require(r['coldPackets']==r['eligible']+r['fallback'] and r['requested']==en*r['eligible'],'gate partition');require(r['fogOn']<=r['coldPackets'] and r['shaderLit']<=r['coldPackets'],'facts count');require(r['coldPackets']*3<=r['sourceVertices']<=r['coldPackets']*75,'cold source range');require(r['eligible']*6+r['fallback']*3<=r['sourceVertices'],'eligible source minimum');require(r['requested']*6<=r['requestedVertices']<=r['requested']*72 and r['requestedVertices']%6==0 and r['requestedVertices']<=r['sourceVertices'],'vertices');gates[p,o]=r
  elif line.startswith('LOG: NIGHTTABLEPHASE'):
   r=strict_row(line,'LOG: NIGHTTABLEPHASE ',['phase','first','selected','enabled','appliedSelected','appliedEnabled']);p=r['phase'];require(p<3 and p not in table,'table phase');require(r['first']==p*1800 and all(r[k]==1 for k in ['selected','enabled','appliedSelected','appliedEnabled']),'fixed table');table[p]=r
  elif line.startswith('LOG: NIGHTEXTRAPHASE'):
   r=strict_row(line,'LOG: NIGHTEXTRAPHASE ',['phase','first','extraMask','appliedExtraMask']);p=r['phase'];require(p<3 and p not in extras and r['first']==1800*p and r['extraMask']==r['appliedExtraMask']==0,'full extras');extras[p]=r
  elif line.startswith('LOG: NIGHTPHASE '):
   r=strict_row(line,'LOG: NIGHTPHASE ',['phase','first','mask','appliedMask','sampler','countReads','samplePtr']);p=r['phase'];require(p<3 and p not in masks and r['first']==1800*p and r['mask']==r['appliedMask']==0 and r['sampler']==1,'full effects');masks[p]=r
 require(set(phases)==set(table)==set(extras)==set(masks)=={0,1,2},'missing phase records');require(set(gates)=={(p,o) for p in range(3) for o in [750,1155]},'missing sparse gates')
 requests=sum(r['requested'] for r in gates.values());return {'status':'PASS_KIND9_STRICT_LOG_REQUESTS_ONLY','order':order,'arms':[phases[p]['enabled'] for p in range(3)],'coldRequests':requests,'actualAcceptedSpritesKnown':False,'activation':'REQUESTED_OUTPUT_UNKNOWN' if requests else 'ZERO_COLD_REQUESTS_CACHE_REPLAY_OR_INACTIVE_UNKNOWN'}
def qw(h):require(isinstance(h,str) and re.fullmatch('[0-9a-fA-F]{32}',h) is not None,'qword hex');return bytes.fromhex(h)
def u4(b):return struct.unpack('<4I',b)
def f32(x):return struct.unpack('<f',struct.pack('<f',x))[0]
def valid_quad(v):
 c=[u4(x[1]) for x in v];xyz=[u4(x[2]) for x in v];q=[struct.unpack('<4f',x[0])[2] for x in v];require(all(x==c[0] for x in c) and all(0<=x<=255 for x in c[0]),'nonconstant final RGBA');require(all(x[2:]==xyz[0][2:] for x in xyz),'nonconstant final Z/F');require(0<=xyz[0][2]<=0xffff0 and 0<=xyz[0][3]<=0xfff,'Z/F/ADC domain');require(all(x==q[0] for x in q) and f32(0.0001)<=q[0]<=10000,'Q domain/equality');xy=[x[:2] for x in xyz];require(all(0<=n<=65535 for x in xy for n in x),'XY domain');require(xy[0]==xy[3] and xy[2]==xy[4] and xy[0][0]==xy[5][0] and xy[1][0]==xy[2][0] and xy[0][1]==xy[1][1] and xy[2][1]==xy[5][1],'rectangle');require(0<xy[2][0]-xy[0][0]<=32767 and 0<abs(xy[2][1]-xy[0][1])<=32767,'span');return (0,2) if xy[2][1]>xy[0][1] else (5,1)
def decode_pair(off,on,elf_sha,tc_sha):
 require(re.fullmatch('[0-9a-f]{64}',elf_sha) is not None and re.fullmatch('[0-9a-f]{64}',tc_sha) is not None,'expected native hash format')
 keys={'schema','arm','epoch','packageId','sourceCount','inputCountWord','executed','elfSha256','tcImageSha256','primitiveTagIndex','outputQwords'}
 for s,arm in [(off,0),(on,1)]:
  require(set(s)==keys and s['schema']==1 and s['arm']==arm and s['executed'] is True,'snapshot schema/actual execution evidence');require(s['elfSha256']==elf_sha and s['tcImageSha256']==tc_sha,'snapshot native pins');require(type(s['epoch']) is int and s['epoch']>=0 and isinstance(s['packageId'],str) and s['packageId']!='' and isinstance(s['outputQwords'],list),'epoch/package/output')
  require(type(s['sourceCount']) is int and 0<s['sourceCount']<=75 and s['sourceCount']%3==0,'count');require(type(s['inputCountWord']) is int and 0<=s['inputCountWord']<=U32 and s['inputCountWord']&COUNT_MASK==s['sourceCount'] and s['inputCountWord']&~(COUNT_MASK|CORONA_FLAG|0x8000)==0,'input count');require(type(s['primitiveTagIndex']) is int and s['primitiveTagIndex']>=0,'tag index')
 for k in ['epoch','packageId','sourceCount','primitiveTagIndex']:require(off[k]==on[k],'paired epoch/package/count')
 require(off['inputCountWord']&CORONA_FLAG==0,'off marker');n=off['sourceCount'];i=off['primitiveTagIndex'];ob=[qw(h) for h in off['outputQwords']];nb=[qw(h) for h in on['outputQwords']];require(len(ob)>=i+1+3*n and len(nb)>i,'snapshot range');require(ob[:i]==nb[:i],'material state changed');tag=int.from_bytes(ob[i],'little');ot=int.from_bytes(nb[i],'little');require(tag&0x7fff==n and (tag>>47)&7==3,'baseline triangle tag');require((tag>>58)&3==0 and (tag>>60)&15==3 and (tag>>64)&0xfff==0x412,'expected packed ST/RGBAQ/XYZF2')
 if ot==tag:
  require(nb[i:i+1+3*n]==ob[i:i+1+3*n],'fallback mutated output');return {'status':'PASS_BOUND_OUTPUT_PAIR_FALLBACK','decodedSprites':0,'sourceCount':n,'markerRequested':bool(on['inputCountWord']&CORONA_FLAG)}
 require(on['inputCountWord']&CORONA_FLAG and n<=72 and n%6==0,'success request domain');expected=(tag&~(0x7fff|(7<<47)))|(n//3)|(6<<47);require(ot==expected,'type/count-only tag change incl FGE');vertices=[ob[i+1+j:i+4+j] for j in range(0,3*n,3)];out=[]
 for start in range(0,n,6):
  pair=valid_quad(vertices[start:start+6]);out.extend(vertices[start+k] for k in pair)
 flat=[b for triple in out for b in triple];require(nb[i+1:i+1+len(flat)]==flat,'wrong compact endpoints');return {'status':'PASS_BOUND_OUTPUT_PAIR_SPRITE_DATA','decodedSprites':n//6,'sourceCount':n,'markerRequested':True,'FGE':(tag>>(47+5))&1,'limits':'Caller must independently establish capture provenance and completed execution; this offline decoder does not execute VU or prove pixels.'}
def main():
 ap=argparse.ArgumentParser();sub=ap.add_subparsers(dest='mode',required=True);p=sub.add_parser('plan');p.add_argument('--order',type=int,choices=[0,1],required=True);p.add_argument('--out',type=Path,required=True);p=sub.add_parser('log');p.add_argument('--order',type=int,choices=[0,1],required=True);p.add_argument('--input',type=Path,required=True);p=sub.add_parser('decode');p.add_argument('--off',type=Path,required=True);p.add_argument('--on',type=Path,required=True);p.add_argument('--elf-sha256',required=True);p.add_argument('--tc-sha256',required=True);a=ap.parse_args()
 if a.mode=='plan':text=f'9 {a.order} 0 0\n';parse_plan(text);a.out.open('x',encoding='ascii').write(text);print(text,end='')
 elif a.mode=='log':print(json.dumps(check_log(a.input.read_text(),a.order),indent=2))
 else:print(json.dumps(decode_pair(json.loads(a.off.read_text()),json.loads(a.on.read_text()),a.elf_sha256,a.tc_sha256),indent=2))
if __name__=='__main__':main()
