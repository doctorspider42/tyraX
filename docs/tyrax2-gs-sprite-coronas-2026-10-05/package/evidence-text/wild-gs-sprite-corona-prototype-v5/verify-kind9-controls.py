from pathlib import Path
import copy,hashlib,json,struct
import corona_controls as c
O=Path(__file__).resolve().parent;checks=0
def check(ok):
 global checks
 assert ok;checks+=1
def rejects(fn):
 try:fn()
 except ValueError:check(True);return
 raise AssertionError('negative accepted')
for order in [0,1]:
 cfg=c.parse_plan(f'9 {order} 0 0\n');check([c.source_expression('coronaSpriteEnabled',cfg,p) for p in range(3)]==([False,True,False] if order==0 else [True,False,True]));check(all(c.source_expression('poolTableEnabled',cfg,p) for p in range(3)));check(not c.source_expression('coronaSpriteEnabled',cfg,3))
for text in ['','9','9 0 0','9 0 0 0 0','9 2 0 0','9 0 1 0','9 0 0 1','8 0 0 0','4 0 0 0','-9 0 0 0','+9 0 0 0','9.0 0 0 0','9,0,0,0','9 0 0 4294967296','999999999999999999999999 0 0 0','9 0 0 0x0','9 0 0 0\x00','\uff19 0 0 0']:rejects(lambda t=text:c.parse_plan(t))
for text in ['9 0 0 0',' 009\t01\r\n0\v0\f','9 1 0 0\n']:check(c.parse_plan(text)[0]==9)
for kind,joint,restored in [(0,0,0),(1,31,0),(2,31,1),(3,0,7),(5,0,0),(6,0,0),(7,0,0)]:
 for order in [0,1]:cfg=c.parse_plan(f'{kind} {order} {joint} {restored}');check(not any(c.source_expression('coronaSpriteEnabled',cfg,p) for p in range(3)))
def log(order,requests=True):
 rows=[]
 for p in range(3):
  en=int((p==1)!=(order==1));rows.extend([f'LOG: NIGHTCORONAPHASE phase={p} first={p*1800} selected=1 enabled={en}',f'LOG: NIGHTTABLEPHASE phase={p} first={p*1800} selected=1 enabled=1 appliedSelected=1 appliedEnabled=1',f'LOG: NIGHTEXTRAPHASE phase={p} first={p*1800} extraMask=0 appliedExtraMask=0',f'LOG: NIGHTPHASE phase={p} first={p*1800} mask=0 appliedMask=0 sampler=1 countReads=262 samplePtr=1234'])
  for o in [750,1155]:
   cold=int(requests);req=en*cold;rows.append(f'LOG: NIGHTCORONAGATES phase={p} offset={o} selected=1 enabled={en} coldPackets={cold} eligible={cold} requested={req} fallback=0 sourceVertices={6*cold} requestedVertices={6*req} fogOn={cold} shaderLit={cold} invalid=0 acceptedOutputKnown=0')
 return '\n'.join(rows)+'\n'
for order in [0,1]:
 good=log(order);check(c.check_log(good,order)['coldRequests']==(2 if order==0 else 4));check(c.check_log(log(order,False),order)['actualAcceptedSpritesKnown'] is False)
 negatives=[good+good.splitlines()[0]+'\n','\n'.join(good.splitlines()[1:])]
 for a,b in [('acceptedOutputKnown=0','acceptedOutputKnown=1'),('invalid=0','invalid=1'),('sampler=1','sampler=0'),('mask=0 appliedMask=0','mask=2 appliedMask=2'),('selected=1 enabled=1 appliedSelected=1 appliedEnabled=1','selected=1 enabled=0 appliedSelected=1 appliedEnabled=0'),('requested=0','requested=1'),('coldPackets=1','coldPackets=0'),('offset=750','offset=900'),('fogOn=1','fogOn=2'),('acceptedOutputKnown=0','acceptedOutputKnown=0 extra=1')]:negatives.append(good.replace(a,b,1))
 for bad in negatives:rejects(lambda t=bad:c.check_log(t,order))
 rejects(lambda:c.check_log(good,1-order))
def vertex(x,y,st):return [struct.pack('<4f',*st),struct.pack('<4I',32,64,128,128),struct.pack('<4I',x,y,0x7fff8,128<<4)]
def snapshot(FGE,reverse):
 y0,y2=(200,100) if reverse else (100,200);vs=[vertex(100,y0,(0.,1.,1.,0.)),vertex(200,y0,(1.,1.,1.,0.)),vertex(200,y2,(1.,0.,1.,0.))];vs += [vs[0],vs[2],vertex(100,y2,(0.,0.,1.,0.))];prim=3|8|16|(FGE<<5)|64;tag=6|(1<<46)|(prim<<47)|(3<<60)|(0x412<<64);common={'schema':1,'epoch':12,'packageId':'corona-p0','sourceCount':6,'executed':True,'elfSha256':'a'*64,'tcImageSha256':'b'*64,'primitiveTagIndex':1};state=b'\x13'*16;off=dict(common,arm=0,inputCountWord=6,outputQwords=[state.hex(),tag.to_bytes(16,'little').hex()]+[b.hex() for v in vs for b in v]);pair=(5,1) if reverse else (0,2);newtag=(tag&~(0x7fff|(7<<47)))|2|(6<<47);on=dict(common,arm=1,inputCountWord=6|c.CORONA_FLAG,outputQwords=[state.hex(),newtag.to_bytes(16,'little').hex()]+[b.hex() for k in pair for b in vs[k]]);return off,on
for FGE in [0,1]:
 for reverse in [False,True]:
  off,on=snapshot(FGE,reverse);result=c.decode_pair(off,on,'a'*64,'b'*64);check(result['decodedSprites']==1 and result['FGE']==FGE);fallback=copy.deepcopy(off);fallback['arm']=1;fallback['inputCountWord']|=c.CORONA_FLAG;check(c.decode_pair(off,fallback,'a'*64,'b'*64)['decodedSprites']==0)
  for index in range(6):
   for lane in range(4):
    bad=copy.deepcopy(off);idx=2+index*3+1;words=list(struct.unpack('<4I',bytes.fromhex(bad['outputQwords'][idx])));words[lane]+=1;bad['outputQwords'][idx]=struct.pack('<4I',*words).hex();rejects(lambda:c.decode_pair(bad,on,'a'*64,'b'*64))
   bad=copy.deepcopy(off);idx=2+index*3+2;words=list(struct.unpack('<4I',bytes.fromhex(bad['outputQwords'][idx])));words[3]^=16;bad['outputQwords'][idx]=struct.pack('<4I',*words).hex();rejects(lambda:c.decode_pair(bad,on,'a'*64,'b'*64))
  for change in ['epoch','executed','hash','FGE','marker','endpoint','state','reglist','count']:
   bad=copy.deepcopy(on)
   if change=='epoch':bad['epoch']+=1
   elif change=='executed':bad['executed']=False
   elif change=='hash':bad['elfSha256']='c'*64
   elif change in ['FGE','reglist']:tag=int.from_bytes(bytes.fromhex(bad['outputQwords'][1]),'little')^(1<<(52 if change=='FGE' else 64));bad['outputQwords'][1]=tag.to_bytes(16,'little').hex()
   elif change=='marker':bad['inputCountWord']&=~c.CORONA_FLAG
   elif change=='endpoint':bad['outputQwords'][-1]='00'*16
   elif change=='state':bad['outputQwords'][0]='00'*16
   elif change=='count':bad['inputCountWord']|=0x200
   rejects(lambda:c.decode_pair(off,bad,'a'*64,'b'*64))
r=(O/'tyra/engine/inc/debug/night_runtime.hpp').read_text();check('Tyra::ExperimentalCoronaSprite::enabled=NightAblation::coronaSpriteSelected&&NightPlan::coronaSpriteEnabled(planConfig,p);' in r);check('coronaSpriteSelected=planConfig.kind==9&&NightSampler::valid;' in r);check('collectCounters=p<3&&(o==750||o==1155)' in r);check('acceptedOutputKnown=0' in r);check('coronaCounter=NightAblation::CoronaCounter{};' in r)
vcl='tyra/engine/src/renderer/3d/pipeline/static/core/programs/cull/stapip_cull_tc_vu1.vclpp';check((O/vcl).read_bytes()==(O.parent/'wild-gs-sprite-corona-prototype-v4'/vcl).read_bytes())
ab=(O/'tyra/engine/inc/debug/night_ablation.hpp').read_text()
check('if(!coronaSpriteSelected||!collectCounters)return;' in ab)
check('if(value>~uint32_t(0)-field)' in ab)
check('requested!=(enabled&&eligible)' in ab)
check('count>72||count%6!=0' in ab)
check('decodedSprites' not in ab)
# Cold admission invariant model: any impossible arm/count combination invalidates the run.
def counter_bad(eligible,requested,enabled,count):return requested!=(enabled and eligible) or (eligible and (count==0 or count>72 or count%6!=0))
for en in [False,True]:
 for eligible in [False,True]:
  for count in [6,12,72]:check(not counter_bad(eligible,en and eligible,en,count))
for count in [0,1,3,75,78]:check(counter_bad(True,True,True,count))
check(counter_bad(False,True,True,6));check(counter_bad(True,True,False,6))
for field,value in [(c.U32,1),(c.U32-3,4),(c.U32-72,73)]:check(value>c.U32-field)
check(not (72>c.U32-(c.U32-72)))
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest();proof={'status':'PASS_HOST_KIND9_STRICT_CONTROLS_AND_OFFLINE_DECODER','checks':checks,'parserSemantics':'strict decimal lexer with validity/arm/table expressions extracted from current C++ source; target C++ I/O not executed','positiveOrders':[0,1],'decoderPositiveCases':'FGE0/1, both Y orientations and unchanged fallback','negativeCases':'plan syntax/overflow/semantic errors; missing/duplicate/wrong arms; effect cuts; misleading accepted counter; varying F/every RGBA lane; wrong epoch/native pins/state/FGE/reglist/input marker/count/endpoints','actualRuntimeCaptured':False,'actualVUSpritesOrPixelsOrGainAccepted':False,'checkerSha256':sha(Path(__file__)),'controlsSha256':sha(O/'corona_controls.py')};(O/'kind9-host-controls-proof.json').write_text(json.dumps(proof,indent=2)+'\n',encoding='utf-8');print(json.dumps(proof,indent=2))
