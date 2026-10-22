"""Require actual interrupted-PC leaf hits; no renderer performance inference."""
import argparse,copy,csv,hashlib,json,re
from pathlib import Path

def validate(log,rows,symbols):
    init=re.findall(r'PCINIT used=(\d+) unused=(\d+) a=([0-9a-f]+)\.\.([0-9a-f]+) b=([0-9a-f]+)\.\.([0-9a-f]+) noPg=1',log)
    assert len(init)==1 and int(init[0][1])>0
    ranges=[]
    for leaf,i in [('a',2),('b',4)]:
        low,high=(int(init[0][j],16) for j in (i,i+1))
        assert 0x100000<=low<high<0x2000000
        for end,address in [('begin',low),('end',high)]:
            found=re.findall(r'^([0-9a-f]{8}) [Tt] pc_leaf_'+leaf+'_'+end+r'$',symbols,re.M)
            assert len(found)==1 and int(found[0],16)==address
        ranges.append((low,high))
    assert ranges[0][1]<=ranges[1][0] or ranges[1][1]<=ranges[0][0]
    stages=re.findall(r'PCCAL phase=(\d+) periodUs=(\d+) callbacks=(\d+) rows=(\d+) dropped=(\d+) afterStop=(\d+)',log)
    assert len(stages)==4 and [int(s[0]) for s in stages]==list(range(4))
    done=re.findall(r'PCDONE rows=(\d+) qualificationOnly=1',log)
    assert len(done)==1 and int(done[0])==len(rows)<=2048
    assert all(tuple(map(int,s[4:]))==(0,0) and int(s[2])==int(s[3]) for s in stages)
    result=[]
    previous=0
    for phase in range(4):
        selected=[r for r in rows if r['phase']==phase]
        assert len(selected)>=40
        assert int(stages[phase][3])-previous==len(selected)
        previous=int(stages[phase][3])
        assert int(stages[phase][1])==(997 if phase<2 else 1733)
        low,high=ranges[phase&1]
        hits=sum(low<=r['pc']<high for r in selected)
        assert hits/len(selected)>=.8, (phase,hits,len(selected))
        assert all(r['region']==phase&1 and r['actual']>=r['scheduled'] for r in selected)
        assert all(a['actual']<b['actual'] for a,b in zip(selected,selected[1:]))
        result.append(dict(phase=phase,samples=len(selected),callbackLeafHits=hits,
                           epcLeafHits=sum(low<=r['epc']<high for r in selected),
                           callbackEqualsEpc=sum(r['pc']==r['epc'] for r in selected)))
    assert {r['phase'] for r in rows}==set(range(4))
    return result

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('fixture',type=Path)
    p.add_argument('--target',choices=('emulator','physical'),required=True)
    p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    root=a.fixture.resolve();sha=lambda f:hashlib.sha256(f.read_bytes()).hexdigest()
    binding=json.loads((root/'native-binding.json').read_text())
    for rel,digest in binding['files'].items():assert sha(root/rel)==digest,rel
    folder=root/('physical/bin' if a.target=='physical' else 'game/bin')
    logpath=root/('physical/console.log' if a.target=='physical' else 'emulator/emulator.log')
    assert sha(folder/'vehicle-playground.elf')==binding['elfSha256']
    log=logpath.read_text(errors='replace');symbols=(root/'symbols.txt').read_text()
    assert not re.search(r'^\w+ [TWtw] (?:_mcount|__mcount)$',symbols,re.M),'Call-entry instrumentation linked'
    manifest=json.loads((root/'pc-calibration-manifest.json').read_text())
    sdk=root/'sdk'
    if not sdk.exists():sdk=Path('C:/Users/pawel/AppData/Local/tyra-editor/toolchain/ps2sdk')
    for rel,digest in manifest['sdkPins'].items():assert sha(sdk/rel)==digest,rel
    with (folder/'pc-calibration.csv').open() as f:rows=[{k:int(v) for k,v in r.items()} for r in csv.DictReader(f)]
    phases=validate(log,rows,symbols);negatives=[]
    for name in ('missing-stop','fake-last-function','overflow','missing-row','false-epc-only'):
        badlog=log;bad=copy.deepcopy(rows)
        if name=='missing-stop':badlog=log.replace('afterStop=0','afterStop=1')
        elif name=='overflow':badlog=log.replace('dropped=0','dropped=1')
        elif name=='missing-row':bad=bad[:-1]
        elif name=='fake-last-function':
            for r in bad:r['pc']=rows[0]['pc']
        else:
            for r in bad:r['pc']=0x80000180
        try:validate(badlog,bad,symbols)
        except AssertionError:negatives.append(name)
        else:raise AssertionError('Calibration mutation escaped: '+name)
    result=dict(valid=True,qualificationOnly=True,target=a.target,elfSha256=binding['elfSha256'],
                logSha256=sha(logpath),csvSha256=sha(folder/'pc-calibration.csv'),phases=phases,
                negativeControls=negatives,limitations=['known leaf calibration, not renderer timing or sampler bias qualification',
                                                       'CP0 EPC may differ from callback address; retain both raw'])
    assert not a.output.exists();a.output.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
if __name__=='__main__':main()
