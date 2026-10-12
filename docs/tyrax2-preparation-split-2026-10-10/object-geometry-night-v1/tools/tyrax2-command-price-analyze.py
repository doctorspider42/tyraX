"""Validate A18 three-route rasters and report physical inclusive timings only."""
import argparse,hashlib,json,re,struct,statistics
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__);p.add_argument('fixture',type=Path);p.add_argument('--physical',action='store_true')
p.add_argument('--archive',action='store_true');p.add_argument('--output',type=Path)
p.add_argument('--allow-legacy-observer',action='store_true',help='Historical V3 comparison only, not clean final pricing')
a=p.parse_args();root=a.fixture
split=(root/'tyra/engine/inc/debug/preparation_split.hpp').exists()
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
if a.archive:
    assert a.output and not a.output.resolve().is_relative_to(root.resolve()),'Archive reanalysis must write outside evidence'
    inventory=json.loads((root/'archive-sha256.json').read_text())
    for rel,h in inventory.items():assert sha(root/rel)==h,rel
b=json.loads((root/'native-binding.json').read_text())
for rel,h in b['files'].items():assert sha(root/rel)==h,rel
presentation_fence='core.sync.align2D(); // Fence the hybrid presentation copy in EVERY arm.' in (root/'game/src/gen/game_physics.gen.cpp').read_text(encoding='utf-8-sig')
if a.physical:assert presentation_fence,'Physical pricing requires equal hybrid presentation completion'
sampler_suspended='MotionProbe::suspended=true;' in (root/'game/src/terrain_game.cpp').read_text(encoding='utf-8-sig')
if a.physical:assert sampler_suspended or a.allow_legacy_observer,'Suspend the old outer drive sampler before final pricing'
folder=root/('physical/bin' if a.physical else 'game/bin')
log=root/('physical/console.log' if a.physical else 'emulator/emulator.log')
t=log.read_text(errors='replace')
launch=json.loads((root/('physical/editor-deploy.json' if a.physical else 'emulator/launch.json')).read_text(encoding='utf-8-sig'))
assert sha(folder/'vehicle-playground.elf')==b['elfSha256']==launch['elfSha256']
if a.physical:
    pins=json.loads((root/'physical/launch-inputs.json').read_text(encoding='utf-8-sig'))
    assert not any(Path(rel).match('command-*.bin') or Path(rel).match('tape-*.bin') for rel in pins)
    asset_hashes=json.loads((root/'physical-bin-hashes.json').read_text()) if a.archive else {}
    for rel,h in pins.items():
        assert (sha(folder/rel) if (folder/rel).exists() else asset_hashes.get(rel))==h,rel
assert 'Assertion failed' not in t
assert 'std::bad_alloc' not in t and 'terminate called' not in t,'Fatal runtime failure'
if (folder/'log.txt').exists():assert 'Assertion failed' not in (folder/'log.txt').read_text(errors='replace')
done=re.findall(r'PRICEDONE stages=(\d+) samples=(\d+) commands=(\d+) phase=(\d+) offset=(\d+)',t)
assert len(done)==1
stages,n,commands,phase,offset=map(int,done[0]);assert (stages,n,commands)==(10,1152,0 if split else 448)
config=folder/'three-path.cfg'
expected=tuple(map(int,config.read_text().split())) if config.exists() else (0,1380)
assert (phase,offset)==expected
freeze=re.findall(r'PRICEFREEZE phase=(\d+) offset=(\d+) z=([^ ]+) speed=([^ ]+) yaw=([^\r\n]+)',t)
assert len(freeze)==1 and tuple(map(int,freeze[0][:2]))==(phase,offset)
frozen=t[t.index('PRICEFREEZE'):]
if sampler_suspended:assert 'DRIVEPHASE' not in frozen and 'TARGETCOUNTS' not in frozen
for name in re.findall(r'open name host:([^\s]+)',frozen):
    assert Path(name.lstrip('/')).name.startswith(('tape-','command-')),('Unexpected frozen IO',name)
cap=re.findall(r'COMMANDCAP valid=1 flushes=1 submissions=1 qwords=(\d+) bytes=(\d+) mpg=0 calls=(\d+) first=([0-9a-f]+) programEqual=1',t)
assert len(cap)==1 and 'COMMANDREADY' in t
qw,closure_bytes,vu_calls=int(cap[0][0]),int(cap[0][1]),int(cap[0][2])
exact_allocation=(root/'tyra/engine/inc/debug/command_size.hpp').exists()
heap_rows=[];allocated_bytes=None
if exact_allocation:
    mem=re.findall(r'COMMANDMEM allocated=(\d+) used=(\d+) limit=(\d+)',t)
    assert len(mem)==1
    allocated_bytes,used_bytes,limit=map(int,mem[0])
    assert used_bytes==closure_bytes and allocated_bytes==(used_bytes+63)//64*64 and allocated_bytes<=limit==4*1024*1024
    heap_rows=[tuple(map(int,r)) for r in re.findall(r'PRICEHEAP stage=(\d+) arena=(\d+) used=(\d+) free=(\d+)',t)]
    assert len(heap_rows)==10 and [r[0] for r in heap_rows]==list(range(10))
    assert all(arena>=used and arena>=free for _,arena,used,free in heap_rows)
assert len((folder/'command-closure.bin').read_bytes())==closure_bytes and qw*16<closure_bytes
assert sha(folder/'command-program-before.bin')==sha(folder/'command-program-after.bin')
expected_modes=[0]*10 if split else [0,0,1,0,2,0,2,0,1,0]
checks=[tuple(map(int,x)) for x in re.findall(r'PRICECHECK stage=(\d+) mode=(\d+) mismatches=(\d+) mask=(\d+) bags=(\d+) events=(\d+) bytes=(\d+) lights=(\d+) target=(\d+) buffers=(\d+) frameYield=(\d+)',t)]
assert len(checks)==10 and [x[0] for x in checks]==list(range(10))
assert [x[1] for x in checks]==expected_modes
assert all(x[2:4]==(0,0) and x[4:]==checks[0][4:] for x in checks)
assert checks[0][4]>0 and checks[0][5]>=checks[0][4]
assert (checks[0][7]>0)==(phase==3),'Night light prefix disagrees with phase'
images={}
for view in ('main','env'):
    pixels=[];dimensions=None;hashes=[]
    for i in range(10):
        path=folder/f'tape-{i}-{view}.bin';raw=path.read_bytes();w,h,psm,stage=struct.unpack('<4I',raw[:16])
        assert stage==i and psm in (0,2) and w>0 and h>0
        assert len(raw)==16+w*h*(2 if psm==2 else 4) and len(set(raw[16:]))>8
        if dimensions is None:dimensions=(w,h,psm)
        assert dimensions==(w,h,psm)
        pixels.append(raw[16:]);hashes.append(sha(path))
    differences=[sum(x!=y for x,y in zip(pixels[0],image)) for image in pixels]
    assert not any(differences),(view,differences)
    images[view]=dict(dimensions=dimensions,rawSha256=hashes,differingBytes=differences)
raw=(folder/'command-times.bin').read_bytes();assert len(raw)==1152*24
rows=list(struct.iter_unpack('<6I',raw))
assert [(r[0],r[1]) for r in rows]==[(s,k) for s in range(1,10) for k in range(64,192)]
assert all(r[2]>r[3] and r[4]<=r[2] and r[5]<=r[2] for r in rows)
assert all((r[4]>0)==(expected_modes[r[0]]==2) for r in rows)
report=dict(valid=True,rasterEqual=True,presentationFence=presentation_fence,legacySamplerSuspended=sampler_suspended,phase=phase,offset=offset,pose=dict(zip(('z','speed','yaw'),map(float,freeze[0][2:]))),
            target='physical' if a.physical else 'emulator',elfSha256=b['elfSha256'],
            logSha256=sha(log),timesSha256=sha(folder/'command-times.bin'),checks=checks,images=images,
            closure=dict(qwords=qw,bytes=closure_bytes,vuCalls=vu_calls,sha256=sha(folder/'command-closure.bin')),
            scope='Frozen Scene incl roads and live capture; pinned target/order; no update/HUD/postFX; not moving FPS or pure EE/VU/GS cost')
if a.physical:
    report['units']='ms from EE Count / 294912; pacing subtracted, startup included'
    stats=[]
    for stage in range(1,10):
        rr=[r for r in rows if r[0]==stage];work=[(r[2]-r[3])/294912 for r in rr]
        stats.append(dict(stage=stage,mode=('ordinary','list','commands')[expected_modes[stage]],
            mean=statistics.mean(work),median=statistics.median(work),p95=sorted(work)[121],maximum=max(work),
            startup=statistics.mean(r[4]/294912 for r in rr),prepareInclusive=statistics.mean(r[5]/294912 for r in rr)))
    report['stages']=stats
    report['comparisons']=[dict(stage=s,mode=stats[s-1]['mode'],
        savedVsBefore=stats[s-2]['mean']-stats[s-1]['mean'],savedVsAfter=stats[s]['mean']-stats[s-1]['mean']) for s in (2,4,6,8)]
if exact_allocation:
    report['diagnosticMemory']=dict(closureAllocatedBytes=allocated_bytes,heapRows=heap_rows,scope='Allocator arena/used/free blocks, not total free EE RAM; off-clock stage checks')
if split:
    source=(root/'game/src/gen/game_physics.gen.cpp').read_text(encoding='utf-8-sig')
    header=(root/'tyra/engine/inc/debug/command_pricing.hpp').read_text(encoding='utf-8-sig')
    split_header=(root/'tyra/engine/inc/debug/preparation_split.hpp').read_text(encoding='utf-8-sig')
    geometry='#define PREPARATION_SPLIT_GEOMETRY 1' in split_header
    for rel,kind,count in (
        ('renderer/3d/pipeline/static/core/stapip_core.cpp','StaPip',2),
        ('renderer/3d/pipeline/static/core/stapip_qbuffer_renderer.cpp','Geometry' if geometry else 'StaPip',1),
        ('renderer/core/paths/path1/vif1_queue.cpp','Queue',9)):
        hooked=(root/'tyra/engine/src'/rel).read_text(encoding='utf-8-sig')
        assert hooked.count('PreparationSplit::Scope preparationSplit(PreparationSplit::'+kind+');')==count,'Missing scope hooks'
    if geometry:
        core_source=(root/'tyra/engine/src/renderer/3d/pipeline/static/core/stapip_core.cpp').read_text(encoding='utf-8-sig')
        assert core_source.count('PreparationSplit::Scope geometrySplit(PreparationSplit::Geometry);')==1
        assert core_source.index('qbufferRenderer.sendObjectData(bag, &mvp, texBuffers);') < core_source.index('PreparationSplit::Scope geometrySplit') < core_source.index('HardwareTrace::Scope traceDispatch("Dispatch");')
    assert 'constexpr U modes[10]={0,0,1,2,1,0,1,2,1,0};' in split_header
    assert 'constexpr U modes[stages]={0,0,0,0,0,0,0,0,0,0};' in header
    assert source.index('prepare=TapePricing::tick()-start;') < source.index('PreparationSplit::boundary();') < source.index('engine->renderer.endFrame();core.synchronizeFrame();')
    assert source.index('const unsigned whole=TapePricing::tick()-start;') < source.index('PreparationSplit::end();')
    width=7 if geometry else 5; fields=4+2*width; row_bytes=fields*4; f=4+width
    assert len(re.findall(r'SPLITDONE samples=1152 bytes='+str(1152*row_bytes),t))==1
    split_raw=(folder/'command-split.bin').read_bytes();assert len(split_raw)==1152*row_bytes
    split_rows=list(struct.iter_unpack('<'+str(fields)+'I',split_raw))
    policies=[0,0,1,2,1,0,1,2,1,0]
    for r,s in zip(rows,split_rows):
        assert r[:2]==s[:2] and s[2]==policies[r[0]] and s[3]==0,'Split identity/policy/balance'
        prep,finish=s[4:f],s[f:]
        total=lambda part:part[0]+part[1]+(part[5] if geometry else 0)
        assert total(prep)<=r[5] and total(finish)<=r[2]-r[5],'Overlapping split'
        for part in (prep,finish):
            if s[2]==0:assert not any(part),'Off observer active'
            else:
                assert part[2]>0 and part[3]>0,'Missing category activation'
                if geometry:assert part[6]>0,'Missing geometry activation'
                if s[2]==1:
                    assert part[0]==part[1]==part[4]==0,'Count-only made clocks'
                    if geometry:assert part[5]==0,'Count-only geometry clocks'
                else:
                    assert part[0]>0 and part[1]>0 and 0<part[4]<=2*(part[2]+part[3]+(part[6] if geometry else 0)),'Invalid clocks'
                    if geometry:assert part[5]>0,'Missing geometry clocks'
    # Frozen identical scenes must have stable scope activation in count/timed arms.
    counts={(s[6],s[7],s[f+2],s[f+3])+((s[10],s[f+6]) if geometry else ()) for s in split_rows if s[2]}
    assert len(counts)==1,('Scope activation drift',counts)
    report['scope']='All-ordinary frozen scene, exclusive elapsed StaPip/queue plus residual; queue includes waits/preemption; observer footprint retained; not live FPS'
    report.pop('comparisons',None)
    result=dict(policies=policies,counts=list(next(iter(counts))),sha256=sha(folder/'command-split.bin'),observerBytes=1152*row_bytes,geometrySplit=geometry)
    if geometry:report['scope']+='; StaPip means object setup/maintenance excluding geometry dispatch and queue'
    if a.physical:
        for stat in report['stages']:stat['observer']=('off','count','timed')[policies[stat['stage']]]
        result['timedStages']=[]
        for stage in (3,7):
            rr=[(r,s) for r,s in zip(rows,split_rows) if r[0]==stage]
            means=lambda fn:statistics.mean(fn(r,s)/294912 for r,s in rr)
            result['timedStages'].append(dict(stage=stage,
                prepare=means(lambda r,s:r[5]),staPip=means(lambda r,s:s[4]),queue=means(lambda r,s:s[5]),
                residual=means(lambda r,s:r[5]-s[4]-s[5]-(s[9] if geometry else 0)),
                finish=means(lambda r,s:r[2]-r[5]),finishStaPip=means(lambda r,s:s[f]),finishQueue=means(lambda r,s:s[f+1]),
                finishResidual=means(lambda r,s:r[2]-r[5]-s[f]-s[f+1]-(s[f+5] if geometry else 0)),
                pacing=means(lambda r,s:r[3]),reads=statistics.mean(s[8]+s[f+4] for r,s in rr)))
            if geometry:result['timedStages'][-1].update(geometry=means(lambda r,s:s[9]),finishGeometry=means(lambda r,s:s[f+5]))
        result['observerContrasts']=[dict(stage=stage,
            timedMinusCountBefore=stats[stage-1]['mean']-stats[stage-2]['mean'],
            timedMinusCountAfter=stats[stage-1]['mean']-stats[stage]['mean'],
            prepareTimedMinusCountBefore=stats[stage-1]['prepareInclusive']-stats[stage-2]['prepareInclusive'],
            prepareTimedMinusCountAfter=stats[stage-1]['prepareInclusive']-stats[stage]['prepareInclusive'],
            timedMinusOffBefore=stats[stage-1]['mean']-stats[stage-3]['mean'],
            timedMinusOffAfter=stats[stage-1]['mean']-stats[stage+1]['mean']) for stage in (3,7)]
        result['countMinusOff']=[dict(countStage=c,offStage=o,work=stats[c-1]['mean']-stats[o-1]['mean'],
            prepare=stats[c-1]['prepareInclusive']-stats[o-1]['prepareInclusive']) for c,o in ((2,1),(4,5),(6,5),(8,9))]
        result['limits']='Finish is raw elapsed including pacing, not additive to non-pacing work. Do not subtract observer contrasts as a uniform correction. Off retains branches/code/data footprint.'
    report['preparationSplit']=result
output=a.output or root/('command-price-physical.json' if a.physical else 'command-price-emulator.json')
output.write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps({k:v for k,v in report.items() if k not in ('images','checks')},indent=2))
