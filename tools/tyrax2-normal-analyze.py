"""Validate A17 counts and six complete routes. Never report intrusive timings."""
import argparse
import csv
import hashlib
import json
import math
from pathlib import Path
import re


def validate(rows, text):
    assert 'DRIVEDONE valid=1' in text
    assert sorted({r['phase'] for r in rows}) == list(range(6))
    records = [dict((k,int(v)) for k,v in re.findall(r'(\w+)=(\d+)',line))
               for line in text.splitlines() if 'NORMALCENSUS phase=' in line]
    assert [(r['phase'],r['bucket']) for r in records] == [(p,b) for p in range(6) for b in range(3)]
    for c in records:
        assert c['calls']==c['hits']+c['misses']
        assert c['misses']==c['conflicts']+c['changed']+c['cold']
        assert c['repeat']<=c['misses'] and c['outside']<=c['misses']
        assert c['outsideVertices']<=c['vertices'] and c['roots']<=c['vertices']
        assert c['overflow']==0 and c['calls']>0
    phases=[]
    for p in range(6):
        g=[r for r in rows if r['phase']==p]
        assert 120<len(g)<=1200
        assert [r['offset'] for r in g]==list(range(1200,1200+len(g)))
        assert all(all(math.isfinite(v) for v in r.values()) for r in g)
        assert all(r['vehicles']==3 and r['driver']>=0 and r['gas']==(r['offset']>=1320) for r in g)
        assert all(r[k]==0 for r in g for k in ('captureFrozen','captureBlocked','envSkipped','paintVu','scopeErrors'))
        assert abs(g[0]['z']+74.303497)<.1 and g[-1]['z']>=108
        assert all(r['z']<108 for r in g[:-1])
        assert all(b['z']>=a['z'] for a,b in zip(g,g[1:]))
        assert all(abs(r['speed'])<.001 for r in g[:120]) and max(r['speed'] for r in g)>5
        angles=[math.degrees(math.atan2(r['lookX']-r['eyeX'],r['lookZ']-r['eyeZ'])) for r in g[120:]]
        assert min(angles)<-50 and max(angles)>50
        assert all(sum(r[k] for r in g[120:])>0 for k in ('envDrawn','probeCapture','paint'))
        assert re.search(rf'NIGHTROADS phase={p} hour={12 if p<3 else 0}\.000000 paused=1 chunks=66 lights='+('0' if p<3 else '[1-9][0-9]*')+r'\b',text)
        assert re.search(rf'DRIVEPHASE phase={p} .*valid=1 rows={len(g)}\b',text)
        assert f'TARGETCOUNTS phase={p} tested=0 rejected=0' in text
        phases.append(dict(phase=p,rows=len(g),driveFrames=len(g)-120,lastZ=g[-1]['z'],cameraDegrees=[min(angles),max(angles)]))
    return dict(phases=phases,counts=records)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('fixture',type=Path)
    parser.add_argument('--physical',action='store_true')
    parser.add_argument('--output',type=Path)
    args=parser.parse_args();root=args.fixture
    sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
    binding=json.loads((root/'native-binding.json').read_text())
    for rel,digest in binding['files'].items():assert sha(root/rel)==digest,rel
    platform='physical' if args.physical else 'emulator'
    folder=root/('physical/bin' if args.physical else 'game/bin')
    log=root/('physical/console.log' if args.physical else 'emulator/emulator.log')
    launch=json.loads((root/('physical/editor-deploy.json' if args.physical else 'emulator/launch.json')).read_text(encoding='utf-8-sig'))
    assert sha(folder/'vehicle-playground.elf')==binding['elfSha256']==launch['elfSha256']
    if args.physical:
        assert launch['runner']=='src/runner.cpp::resetPs2Link/deployToPs2'
        for rel,digest in json.loads((root/'physical/launch-inputs.json').read_text(encoding='utf-8-sig')).items():assert sha(folder/rel)==digest,rel
    text=log.read_text(errors='replace')
    rows=[{k:float(v) for k,v in r.items()} for r in csv.DictReader((folder/'drive-probe.csv').open())]
    result=validate(rows,text)
    controls=[]
    for name,data,logtext in [('missing-row',rows[:-1],text),('no-completion',rows,text.replace('DRIVEDONE valid=1','DRIVEDONE valid=0')),
                             ('no-census',rows,text.replace('NORMALCENSUS','REMOVED')),
                             ('bad-count',rows,re.sub(r'overflow=0','overflow=1',text))]:
        try:validate(data,logtext)
        except AssertionError:controls.append(name)
        else:raise AssertionError('Accepted negative control: '+name)
    result.update(platform=platform,valid=True,negativeControls=controls,elfSha256=binding['elfSha256'],
                  csvSha256=sha(folder/'drive-probe.csv'),logSha256=sha(log),sourceFiles=len(binding['files']),
                  scope='Intrusive counts, no timing claim. repeat means matching observed pointer/count/stamps, not an allocation identity.')
    (args.output or root/f'normal-{platform}.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(result,indent=2))


if __name__=='__main__':main()
