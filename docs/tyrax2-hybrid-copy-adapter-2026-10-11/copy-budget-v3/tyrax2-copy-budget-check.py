"""Check original full-resolution packet, pixel coverage and isolated completion."""
import argparse, collections, hashlib, json, re, statistics, struct
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('root',type=Path);p.add_argument('--platform',choices=['emulator','physical'],required=True)
p.add_argument('--output',type=Path)
a=p.parse_args();root=a.root.resolve();sha=lambda f:hashlib.sha256(f.read_bytes()).hexdigest()
build=json.loads((root/'build.json').read_text())
assert all(sha(root/n)==h for n,h in build['files'].items())
folder=root/a.platform
launch=json.loads((folder/('launch.json' if a.platform=='emulator' else 'editor-deploy.json')).read_text())
assert launch['elfSha256']==sha(root/'game/bin/vehicle-playground.elf')
log=folder/('emulator.log' if a.platform=='emulator' else 'console.log')
bin=root/'game/bin' if a.platform=='emulator' else folder/'bin'
text=log.read_text(errors='replace')

def trace(text):
    assert len(re.findall(r'COPYBOOT version=1 mode=480p width=448 height=448',text))==1
    done=re.findall(r'COPYDONE reserved=1 completed=(\d+) errors=(\d+) originalQw=(\d+) finishQw=(\d+) srcShot=(-?\d+) dstShot=(-?\d+) restored=(\d+)',text)
    assert done==[('120','0','79','81','0','0','1')],done
    rows=[list(map(int,row)) for row in re.findall(r'COPY index=(\d+) begin=(\d+) done=(\d+) a0=(\d+) b0=(\d+) a1=(\d+) b1=(\d+) late=(\d+)',text)]
    assert [r[0] for r in rows]==list(range(120))
    for _,begin,end,a0,b0,a1,b1,late in rows:
        assert 0<((end-begin)&0xffffffff)<294912000
        assert late==int(a1>=b1 or a1<a0)
        assert a0<b0 and a0<65536 and max(a1,b0,b1)<65536
    return rows

def packet(raw):
    assert len(raw)==81*16
    words=list(struct.iter_unpack('<QQ',raw));pos=0;loops=[];ads=[]
    while pos<len(words):
        tag,regs=words[pos];pos+=1;n=tag&0x7fff
        assert (tag>>58)&3==0 and (tag>>60)&15==1 and regs==14
        assert pos+n<=len(words);loops.append(n);ads.extend(words[pos:pos+n]);pos+=n
    assert loops==[67,4,1,1,1,1]
    assert ads[-1]==(1,0x61)
    tex=ads[1][0];frame=ads[4][0]
    assert ads[1][1]==6 and tex&0x3fff==0 and (tex>>14)&63==7 and (tex>>20)&63==0
    assert (tex>>26)&15==9 and (tex>>30)&15==9
    assert ads[4][1]==0x4c and frame&511==98 and (frame>>16)&63==7 and (frame>>24)&63==2
    assert ads[9]==(1,0x45)
    for c in range(14):
        x0=c*32;x1=x0+32
        expected=[((x0*16+8)|(8<<16),3),
                  (((2048+x0)*16)|(2048*16<<16),5),
                  ((x1*16+8)|((448*16+8)<<16),3),
                  (((2048+x1)*16)|((2048+448)*16<<16),5)]
        assert ads[11+c*4:15+c*4]==expected
    return dict(qwords=81,originalQwords=79,columns=14,width=448,height=448,finish=True)

def pixels(source,dest):
    assert struct.unpack_from('<4I',source)==(448,448,0,1)
    assert struct.unpack_from('<4I',dest)==(448,448,2,1)
    assert len(source)==16+448*448*4 and len(dest)==16+448*448*2
    src=[v[:3] for v in struct.iter_unpack('<4B',source[16:])]
    colors=((37,91,173),(199,55,83),(67,211,109),(233,181,29))
    assert collections.Counter(src)==collections.Counter({c:50176 for c in colors})
    assert all(v==colors[(i%448)//224+(i//448//224)*2] for i,v in enumerate(src))
    dst=[((v&31)<<3,((v>>5)&31)<<3,((v>>10)&31)<<3) for v, in struct.iter_unpack('<H',dest[16:])]
    error=max(abs(x-y) for s,d in zip(src,dst) for x,y in zip(s,d))
    assert error<=7,error
    return dict(checkedPixels=len(src),eachQuadrantPixels=50176,maxChannelError=error)

rows=trace(text);raw=(bin/'copy-packet.bin').read_bytes()
source=(bin/'copy-source.bin').read_bytes();dest=(bin/'copy-dest.bin').read_bytes()
pkt=packet(raw);pix=pixels(source,dest);controls={}
cases={'missing_copy':lambda:trace(re.sub(r'COPY index=3 .*','',text,count=1)),
       'wrong_late':lambda:trace(text.replace('late=0','late=1',1)),
       'not_restored':lambda:trace(text.replace('restored=1','restored=0')),
       'packet_truncated':lambda:packet(raw[:-16]),
       'source_corrupt':lambda:pixels(source[:16]+bytes([0])*4+source[20:],dest),
       'destination_corrupt':lambda:pixels(source,dest[:16]+bytes([0])*2+dest[18:])}
for name,fn in cases.items():
    try:fn();controls[name]=False
    except (AssertionError,ValueError,struct.error):controls[name]=True
assert all(controls.values())
times=[((r[2]-r[1])&0xffffffff)/294912 for r in rows]
result=dict(valid=True,platform=a.platform,elfSha256=launch['elfSha256'],
    logSha256=sha(log),packet=pkt,pixels=pix,controls=controls,
    completionMs=dict(min=min(times),median=statistics.median(times),p95=sorted(times)[113],max=max(times)),
    lateCopies=sum(r[7] for r in rows),allObservedInsideBlank=all(not r[7] for r in rows),
    remainingLimits=['Isolated scene-free consumer cost, not game FPS or EE packet gain.',
                    'Empirical120-copy maximum is not universal WCET or full IRQ dispatch qualification.',
                    'Appended FINISH differs from stock Hybrid2 synchronization.',
                    'Full-game timer/queue/source/raster ownership remains unqualified.',
                    'Emulator completion time is not physical cost.'])
(a.output or root/(a.platform+'-copy.json')).write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps(result,indent=2))
