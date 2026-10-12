"""Compare parsed VIF command/payload records, ignoring only plain NOP commands."""
import argparse
import hashlib
import json
import re
import runpy
import struct
import subprocess
import tempfile
from pathlib import Path

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('fixture',type=Path);p.add_argument('--phase',type=int,default=0)
a=p.parse_args();root=a.fixture.resolve();repo=Path(__file__).resolve().parents[1]
decoder=runpy.run_path(str(repo/'tools/tyrax2-command-lineage.py'),run_name='decoder')
log=(root/'emulator/emulator.log').read_text(errors='replace')
rows=re.findall(r'COMMANDCAP stage=(\d+) valid=1 flushes=1 submissions=1 qwords=(\d+) bytes=(\d+) mpg=0 calls=(\d+)',log)
counts={int(s):int(qw) for s,qw,_,_ in rows}
records=[];hashes=[];sizes=[]
with tempfile.TemporaryDirectory() as tmp:
    tmp=Path(tmp);parser=tmp/'parser'
    subprocess.run(['g++','-std=c++17','-O3','-Wall','-Wextra','-Werror',
                    '-I'+str(root/'tyra/engine/inc'),str(repo/'tools/verify-command-lineage.cpp'),
                    '-o',str(parser)],check=True)
    for stage in range(a.phase*3,a.phase*3+3):
        raw=(root/'game/bin'/f'command-{stage}-closure.bin').read_bytes()
        words,_=decoder['flatten'](raw,counts[stage])
        stream=struct.pack('<'+str(len(words))+'I',*words)
        (tmp/'stream.bin').write_bytes(stream)
        result=subprocess.run([str(parser),str(tmp/'stream.bin')],capture_output=True,text=True)
        assert result.returncode==0,result.stderr
        current=[]
        for line in result.stdout.splitlines():
            at,word,pending=map(int,line.split())
            if word==0:continue
            current.append((word,struct.pack('<'+str(pending)+'I',*words[at+1:at+1+pending])))
        records.append(current);hashes.append(hashlib.sha256(stream).hexdigest());sizes.append(len(stream))
pairs=[]
for left,right in ((0,1),(0,2),(1,2)):
    x,y=records[left],records[right]
    first=next((i for i,(v,w) in enumerate(zip(x,y)) if v!=w),None)
    changed=None
    if first is not None:
        changed={'index':first,'commands':[hex(x[first][0]),hex(y[first][0])],
                 'payloadBytes':[len(x[first][1]),len(y[first][1])]}
        before,after=x[first][1],y[first][1]
        changed['wordDifferences']=[{'word':i,'before':hex(v),'after':hex(w)}
            for i,(v,w) in enumerate(zip(struct.unpack('<'+str(len(before)//4)+'I',before),
                                        struct.unpack('<'+str(len(after)//4)+'I',after))) if v!=w][:16]
    pairs.append({'arms':[left,right],'exactStreamEqual':hashes[left]==hashes[right],
                  'plainNopInsensitiveEqual':x==y,'commandCounts':[len(x),len(y)],
                  'streamBytes':[sizes[left],sizes[right]],'firstDifferentCommand':changed})
    pairs[-1]['differentCommandRecords']=sum(v!=w for v,w in zip(x,y))+abs(len(x)-len(y))
report={'phase':a.phase,'qualifiedObserver':False,'comparisons':pairs,
        'limits':['Partial cold analysis, not full observer/raster qualification.',
                  'Only zero command words are ignored; zero payload words remain data.',
                  'NOP-insensitive equality is not equal timing or initial VIF/VU/GS state.']}
(root/f'command-token-diff-phase{a.phase}.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report))
