"""Qualify actual A44 command snapshots and compare delivered VIF across held arms."""
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
p.add_argument('fixture',type=Path);p.add_argument('baseline',type=Path)
a=p.parse_args();root=a.fixture.resolve();base=a.baseline.resolve()
repo=Path(__file__).resolve().parents[1]
sha=lambda f:hashlib.sha256(f.read_bytes()).hexdigest()
source=runpy.run_path(str(repo/'tools/verify-world-source.py'),run_name='source_decoder')
lineage=runpy.run_path(str(repo/'tools/tyrax2-command-lineage.py'),run_name='command_decoder')
binding=json.loads((root/'native-binding.json').read_text())
baselineBinding=json.loads((base/'native-binding.json').read_text())
for folder,b in ((root,binding),(base,baselineBinding)):
    for rel,digest in b['files'].items():assert sha(folder/rel)==digest,rel
    assert sha(folder/'game/bin/vehicle-playground.elf')==b['elfSha256']
manifest=json.loads((root/'world-command-source-manifest.json').read_text())
assert manifest['baseElfSha256']==baselineBinding['elfSha256']
assert json.loads((root/'emulator/launch.json').read_text())['elfSha256']==binding['elfSha256']
baselineReport=json.loads((base/'world-source-emulator.json').read_text())
assert baselineReport['valid'] and baselineReport['logSha256']==sha(base/'emulator/emulator.log')
for rel,digest in baselineReport['files'].items():assert sha(base/rel)==digest
logpath=root/'emulator/emulator.log';log=logpath.read_text(errors='replace')
assert 'DRIVEDONE valid=1' in log and log.count('A44SCENESTEP fixed=1')==1
assert not re.search(r'COMMANDSNAP busy|Assertion failed|bad_alloc|DMA Busy',log)
pattern=r'COMMANDCAP stage=(\d+) valid=(\d+) flushes=(\d+) submissions=(\d+) qwords=(\d+) bytes=(\d+) mpg=(\d+) calls=(\d+) first=([0-9a-f]+) programEqual=(\d+)'
rows=re.findall(pattern,log)
assert len(rows)==9 and [int(x[0]) for x in rows]==list(range(9))
mem=re.findall(r'COMMANDMEM stage=(\d+) allocated=(\d+) used=(\d+) limit=(\d+) base=(\d+)',log)
assert len(mem)==9 and [int(x[0]) for x in mem]==list(range(9))
assert all(int(x[3])==manifest['capacityBytes'] for x in mem)
assert re.findall(r'A44SOURCECONFIG enabled=(\d+) reserve=(\d+)',log)==[('1','2097152')]
captures=[];pins={};negatives=[];sourceChanges=[]
with tempfile.TemporaryDirectory() as temp:
    temp=Path(temp);parser=temp/'parser'
    subprocess.run(['g++','-std=c++17','-O3','-Wall','-Wextra','-Werror',
                    '-I'+str(root/'tyra/engine/inc'),str(repo/'tools/verify-command-lineage.cpp'),
                    '-o',str(parser)],check=True)
    for row,memory in zip(rows,mem):
        stage,valid,flushes,submissions,qw,size,mpg,calls,first,equal=row
        stage=int(stage);qw=int(qw);size=int(size)
        assert (valid,flushes,submissions,mpg,equal)==('1','1','1','0','1')
        assert int(calls)>0 and int(memory[2])==size and size<=int(memory[1])<=int(memory[3])
        for name in ('closure','program-before','program-after','data-before','data-after',
                     'state-before','state-after'):
            path=root/'game/bin'/f'command-{stage}-{name}.bin'
            pins[path.relative_to(root).as_posix()]=sha(path)
            if name.startswith(('program-','data-')):assert path.stat().st_size==16384
            if name.startswith('state-'):assert path.stat().st_size==72
        before=root/'game/bin'/f'command-{stage}-program-before.bin'
        after=root/'game/bin'/f'command-{stage}-program-after.bin'
        assert before.read_bytes()==after.read_bytes()
        raw=(root/'game/bin'/f'command-{stage}-closure.bin').read_bytes()
        assert len(raw)==size
        words,dma=lineage['flatten'](raw,qw)
        assert dma['base']==int(memory[4]) and dma['ownedBytes']==size
        for name,broken in (('truncated',raw[:-16]),('badReference',bytearray(raw))):
            if name=='badReference':
                ref=dma['references'][0];struct.pack_into('<I',broken,ref['tag']*16+4,ref['address']+16)
            try:lineage['flatten'](broken,qw)
            except (AssertionError,struct.error):continue
            raise AssertionError('Malformed closure accepted: '+name)
        negatives=['truncated','badReference']
        stream=struct.pack('<'+str(len(words))+'I',*words)
        (temp/'stream.bin').write_bytes(stream)
        checked=subprocess.run([str(parser),str(temp/'stream.bin')],capture_output=True,text=True)
        assert checked.returncode==0,checked.stderr
        parsed=[tuple(map(int,line.split())) for line in checked.stdout.splitlines()]
        kicks=[w for _,w,_ in parsed if (w>>24)&127 in (0x14,0x15,0x17)]
        assert len(kicks)==int(calls) and kicks[0]==int(first,16)
        captures.append({'stage':stage,'streamSha256':hashlib.sha256(stream).hexdigest(),
                         'stream':stream,'calls':len(kicks),'rootQwords':qw,
                         'ownedBytes':size,'commands':len(parsed)})
        src=root/'game/bin'/f'source-{stage}.bin';old=base/'game/bin'/f'source-{stage}.bin'
        x=source['decode'](src.read_bytes(),stage);y=source['decode'](old.read_bytes(),stage)
        pins[src.relative_to(root).as_posix()]=sha(src)
        if x!=y:sourceChanges.append(stage)
imageMismatches=[]
for stage in range(36):
    for target in ('main','env','display'):
        rel=f'game/bin/cell-{stage}-{target}.bin'
        pins[rel]=sha(root/rel)
        if (root/rel).read_bytes()!=(base/rel).read_bytes():imageMismatches.append([stage,target])
comparisons=[]
for phase in range(3):
    for left,right in ((0,1),(0,2),(1,2)):
        x,y=captures[phase*3+left]['stream'],captures[phase*3+right]['stream']
        comparisons.append({'phase':phase,'arms':[left,right],
                            'differentBytes':sum(v!=w for v,w in zip(x,y))+abs(len(x)-len(y)),
                            'sameLength':len(x)==len(y)})
for row in captures:del row['stream']
report={'valid':not sourceChanges and not imageMismatches,'experiment':'A44-actual-command-source',
        'elfSha256':binding['elfSha256'],'baselineElfSha256':baselineBinding['elfSha256'],
        'logSha256':sha(logpath),'captures':captures,'streamComparisons':comparisons,
        'sourceChangedStages':sourceChanges,'imageMismatches':imageMismatches,
        'negativeControls':negatives,'files':pins,'promotionAllowed':False,'physicalPrice':False,
        'limits':manifest['limits']}
(root/'world-command-source-emulator.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report))
assert report['valid'],'Command observer changes source or raster'
