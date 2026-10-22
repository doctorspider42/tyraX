from pathlib import Path
from collections import Counter
import json,struct,hashlib
root=Path(__file__).parent/'burnout/live'
records=[json.loads((root/f'capture-{i}.json').read_text()) for i in (102,103)]
rams=[(root/f'ram-{i}.bin').read_bytes() for i in (102,103)]
for record,ram in zip(records,rams):
    at=int(record['dma']['TADR'],16);stack=[]
    assert record['pc']=='0x1f1a3c' and record['cpuRegisters']['4']=='0x145'
    for i,t in enumerate(record['tags']):
        assert at==int(t['address'],16) and not at%16
        word=struct.unpack_from('<4I',ram,at);n=word[0]&65535;kind=word[0]>>28&7
        assert n==t['qwc'] and kind==t['id']
        ref=word[1]&0x1fffffff
        if kind in (0,2,3,4,5):assert not word[1]&0x8000000f
        if kind in (0,3,4):assert ref+n*16<=len(ram)
        else:assert at+16+n*16<=len(ram)
        if kind in (0,7):assert i==len(record['tags'])-1 and not stack
        elif kind==5:
            assert len(stack)<2
            stack.append(at+16+n*16);at=ref
        elif kind==6:assert stack;at=stack.pop()
        elif kind==2:at=ref
        elif kind==1:at+=16+n*16
        else:at+=16
calls=[[int(t['ref'],16) for t in r['tags'] if t['id']==5] for r in records]
report={'callCounts':[len(c) for c in calls],'uniqueTargets':[len(set(c)) for c in calls],'sameOrderedTargets':calls[0]==calls[1],'sharedTargets':len(set(calls[0])&set(calls[1]))}
shared=set(calls[0])&set(calls[1]);runs=[]
for addr in sorted(shared):
    signatures=[];sizes=[]
    for ram in rams:
        at=addr;chunks=[]
        for _ in range(20000):
            tag=struct.unpack_from('<4I',ram,at);size=tag[0]&65535;kind=tag[0]>>28&7
            assert kind in (1,3,6),(hex(at),kind)
            chunks.append(ram[at:at+16]);at+=16
            if kind in (1,6):chunks.append(ram[at:at+size*16]);at+=size*16
            else:
                ref=tag[1]&0x1fffffff;chunks.append(ram[ref:ref+size*16])
            if kind==6:break
        else:raise RuntimeError('bound')
        data=b''.join(chunks);sizes.append(len(data));signatures.append(hashlib.sha256(data).hexdigest())
    runs.append({'target':hex(addr),'bytes':sizes,'unchanged':signatures[0]==signatures[1]})
report['subchains']=runs
report['unchangedTargets']=sum(r['unchanged'] for r in runs)
report['unchangedUniqueBytes']=sum(r['bytes'][0] for r in runs if r['unchanged'])
streams=[]
for record,ram in zip(records,rams):
    stream=[]
    for t in record['tags']:
        addr=int(t['address'],16);tag=struct.unpack_from('<4I',ram,addr);size=t['qwc'];kind=t['id']
        stream.extend(tag[2:4])
        payload=(tag[1]&0x1fffffff) if kind in (0,3,4) else addr+16
        stream.extend(struct.unpack_from('<'+'I'*(size*4),ram,payload))
    at=0;cl=wl=1;commands=Counter();unpacks=Counter();values=Counter()
    while at<len(stream):
        code=stream[at];at+=1;cmd=(code>>24)&127;num=(code>>16)&255;imm=code&65535;payload=0
        commands[hex(cmd)]+=1
        if cmd==1:cl=(imm&255) or 256;wl=(imm>>8) or 256
        elif cmd in (0x20,):payload=1
        elif cmd in (0x30,0x31):payload=4
        elif cmd==0x4a:payload=(num or 256)*2
        elif cmd in (0x50,0x51):payload=imm*4
        elif cmd>=0x60:
            n=num or 256;vn=((cmd>>2)&3)+1;vl=cmd&3
            supplied=n if wl<=cl else (n//wl)*cl+min(n%wl,cl)
            bits=16 if vl==3 else vn*(32>>vl)
            payload=(supplied*bits+31)//32
            key=f'V{vn}-{32>>vl if vl<3 else 5}'
            unpacks[key]+=1;values[key]+=supplied
        else:assert cmd in (0,2,3,4,5,6,7,0x10,0x11,0x13,0x14,0x15,0x17), (at,hex(code))
        at+=payload
        assert at<=len(stream),(at,len(stream))
    streams.append({'streamWords':len(stream),'commands':dict(commands),'unpacks':dict(unpacks),'suppliedVectors':dict(values)})
report['vif']=streams
(root/'reuse.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps({k:v for k,v in report.items() if k!='subchains'}))
