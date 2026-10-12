"""Private emulator launch and strict census analysis for tyrax2-motion-probe."""
import csv
import hashlib
import json
import os
import re
import statistics
import subprocess
import sys
import shutil
from pathlib import Path

mode, folder = sys.argv[1:3]
root = Path(folder).resolve()
if mode in ('selftest','selftest-lifetime'):
    lifetime_trial=mode=='selftest-lifetime'
    import tempfile
    import copy
    fields='phase frame angle whole pacing period update render flips bags outside direct replay partial packages expandVerts copyBytes snapshotBytes bakeMiss churn probeCapture paint rebuild missNew missBBox missContent missPrim missStreams missProgram missDepth missSize expired evicted reserveFailed cachePeakBytes cachePeakEntries'.split()
    reference=[]
    for phase in range(6):
        for i in range(360):
            angle=i if lifetime_trial or phase in (1,3,4) else (359 if phase>=2 else 0)
            row=dict.fromkeys(fields,0)
            row.update(phase=phase,frame=phase*1200+300+i,angle=angle,whole=10000+angle,pacing=10,period=10010+angle,update=100,render=9900+angle,flips=1)
            if lifetime_trial: row['frame']=phase*1800+1200+i
            if (phase<3 if lifetime_trial else phase!=3): row.update(bags=10,outside=2,direct=5,replay=4,partial=3)
            reference.append(row)
    cases={'reference':reference}
    cases['missing']=reference[:-1]
    for name,index,key,value in [('angle',360,'angle',9),('disabled',1080,'bags',1),('accounting',0,'render',1),('paint',0,'paint',1),('selected',720,'angle',1),('reasons',360,'missNew',1),('lastParked',1800,'angle',1)]:
        data=copy.deepcopy(reference);data[index][key]=value;cases[name]=data
    if lifetime_trial:
        data=copy.deepcopy(reference);data[360]['bags']=11;cases['workload']=data
    result={}
    with tempfile.TemporaryDirectory() as temp:
        folder=Path(temp)
        for name,data in cases.items():
            with (folder/('motion-lifetime.csv' if lifetime_trial else 'motion-probe.csv')).open('w',newline='') as f:
                writer=csv.DictWriter(f,fieldnames=fields);writer.writeheader();writer.writerows(data)
            run=subprocess.run([sys.executable,str(Path(__file__).resolve()),'analyze-lifetime' if lifetime_trial else 'analyze',str(folder)],capture_output=True,text=True)
            result[name]=run.returncode==0
    assert result=={name:name=='reference' for name in cases},result
    root.mkdir(exist_ok=True,parents=True)
    (root/'parser-controls.json').write_text(json.dumps(result,indent=2)+'\n')
    print(result)
elif mode == 'scope-selftest':
    # Exercise the actual RAII ledger with a deterministic host clock.
    import tempfile
    header=Path(__file__).with_name('tyrax2-drive-probe.hpp').read_text()
    clock='inline U ticks() { U t; asm volatile("mfc0 %0,$9":"=r"(t)::"memory"); return t; }'
    assert header.count(clock)==1
    header=header.replace(clock,'inline U fakeTick=0; inline U ticks() { return ++fakeTick; }')
    test=r'''
#include "probe.hpp"
#include <cassert>
int main() {
  using namespace MotionProbe;
  { Scope core; Scope part(true); Wait wait; }
  assert(fakeTick==0 && timing[CoreCalls]==0);
  clocks=true;
  { Wait outside; }
  assert(fakeTick==0 && timing[WaitCalls]==0);
  { Scope core; Scope part(true); Wait first; { Wait nested; } }
  assert(fakeTick==6 && timing[CoreTicks]==5 && timing[PartialTicks]==3);
  assert(timing[CoreWaitTicks]==1 && timing[PartialWaitTicks]==1 && timing[WaitCalls]==1);
  assert(coreDepth==0 && partialDepth==0 && waitDepth==0 && timing[ScopeErrors]==0);
  memset(timing,0,sizeof(timing));fakeTick=0xfffffffeU;
  { Scope core; Scope unselected(true,false); Wait wait; }
  assert(timing[CoreTicks]==3 && timing[CoreWaitTicks]==1 && timing[PartialTicks]==0);
  { Scope core; Scope illegalCore; }
  assert(timing[ScopeErrors]==1 && coreDepth==0);
  clocks=false;
  U previousTick=fakeTick;
  { Scope core; Wait wait; }
  assert(fakeTick==previousTick);
  clocks=true;memset(timing,0,sizeof(timing));fakeTick=0;
  { Bucket outside(SubmitTicks); }
  assert(fakeTick==0);
  { Scope core; Scope partial(true); Bucket submit(SubmitTicks); { Bucket payload(PayloadTicks); } }
  assert(timing[SubmitTicks]==2 && timing[PayloadTicks]==1 && timing[PartialTicks]==5);
  assert(timing[SubmitCalls]==1 && timing[PayloadCalls]==1 && currentBucket==nullptr);
  assert(timing[ScopeErrors]==0 && coreDepth==0 && partialDepth==0);
}
'''
    with tempfile.TemporaryDirectory() as tmp:
        path=Path(tmp)
        (path/'probe.hpp').write_text(header)
        (path/'test.cpp').write_text(test)
        subprocess.run(['g++','-std=c++17','-O3','-Wall','-Wextra',str(path/'test.cpp'),'-o',str(path/'test')],check=True)
        subprocess.run([str(path/'test')],check=True)
    record=dict(passed=True,checks=['disabled clocks','wait outside Core','nested wait union','nested scope containment','u32 wrap','unselected partial','recursive Core rejected','balanced depths','bucket outside Partial','exclusive nested buckets'],headerSha256=hashlib.sha256(Path(__file__).with_name('tyrax2-drive-probe.hpp').read_bytes()).hexdigest())
    (root/'scope-selftest.json').write_text(json.dumps(record,indent=2)+'\n')
    print(record)
elif mode == 'verify':
    stage=Path('/home/spider/.cache/tyrax/native')
    key=lambda s:hashlib.sha256(s.encode()).hexdigest()[:24]
    project=stage/'projects'/key(str(root/'game'))
    engine=stage/'engines'/key('/mnt/c/Users/pawel/AppData/Local/tyra-editor/native-build')/'tyra/engine'
    files={}
    for rel,mirror in [('game/src',project/'src'),('game/inc',project/'inc'),('tyra/engine/src',engine/'src'),('tyra/engine/inc',engine/'inc')]:
        for p in (root/rel).rglob('*'):
            if p.is_file():
                target=mirror/p.relative_to(root/rel)
                digest=hashlib.sha256(p.read_bytes()).hexdigest()
                assert target.is_file() and hashlib.sha256(target.read_bytes()).hexdigest()==digest,str(p)
                files[str(p.relative_to(root))]=digest
    elf=root/'game/bin/vehicle-playground.elf'
    assert elf.read_bytes()==(project/'bin/vehicle-playground.elf').read_bytes()
    tool=stage/'toolchains'/key('/mnt/c/Users/pawel/AppData/Local/tyra-editor/toolchain')/'ee/bin/mips64r5900el-ps2-elf-nm'
    symbols=subprocess.check_output([str(tool),'-C',str(project/'bin/vehicle-playground.elf')],text=True)
    # The shipped ELF may be stripped; the preserved symbol ELF is authoritative.
    if 'MotionProbe::rows' not in symbols:
        candidates=list((project/'bin').glob('*.sym'))
        assert len(candidates)==1,candidates
        symbols=subprocess.check_output([str(tool),'-C',str(candidates[0])],text=True)
    assert len(re.findall(r'^.* MotionProbe::rows$',symbols,re.M))==1
    (root/'symbols.txt').write_text(symbols)
    record=dict(files=files,elfSha256=hashlib.sha256(elf.read_bytes()).hexdigest(),engineArchiveSha256=hashlib.sha256((engine/'bin/libtyra.a').read_bytes()).hexdigest(),projectMirror=str(project),engineMirror=str(engine))
    (root/'native-binding.json').write_text(json.dumps(record,indent=2)+'\n')
    print('Native mirror/source/ELF agreement:',len(files),'files',record['elfSha256'])
elif mode=='stage':
    dest=root/'physical/bin'
    dest.mkdir(parents=True)
    for p in (root/'game/bin').iterdir():
        if p.name in ('launch.scene','livedbg.bin','livedbg.cmd','rendercost.txt','motion-probe.csv','motion-lifetime.csv','drive-probe.csv','static-plan.csv','static-plan-analysis.json','producer-probe.csv','producer-bins.csv','producer-analysis.json','subrange-analysis.json','analysis.json','log.txt','ps2link.run','sky-analysis.json','sky-prepared-analysis.json','road-geometry.bin','road-geometry.json') or p.suffix=='.sym': continue
        if p.is_dir(): shutil.copytree(p,dest/p.name)
        else: shutil.copy2(p,dest/p.name)
    (dest/'ps2link.run').write_text('ps2link',encoding='ascii')
    pins={str(p.relative_to(dest)):hashlib.sha256(p.read_bytes()).hexdigest() for p in dest.rglob('*') if p.is_file()}
    (dest.parent/'launch-inputs.json').write_text(json.dumps(pins,indent=2)+'\n')
    print('Physical inputs staged:',len(pins))
elif mode == 'emulator':
    runtime=root/'emulator'
    runtime.mkdir()
    ini=runtime/'profile/PCSX2/inis'
    ini.mkdir(parents=True)
    source=Path('/mnt/f/Projects/tyra-editor/docs/tyrax2-loop-balance-2026-10-06/emulator/PCSX2.ini')
    text=source.read_text()
    text=re.sub(r'^PINESlot\s*=.*$', 'PINESlot = 29417',text,flags=re.M)
    text=re.sub(r'^(StartPaused|ShowOnStartup)\s*=.*$',r'\1 = false',text,flags=re.M)
    (ini/'PCSX2.ini').write_text(text)
    env=os.environ.copy()
    env.update(DISPLAY=':97',QT_QPA_PLATFORM='xcb',LIBGL_ALWAYS_SOFTWARE='1')
    env.pop('WAYLAND_DISPLAY',None)
    xvfb=subprocess.Popen(['Xvfb',':97','-screen','0','1280x800x24'],stdout=(runtime/'xvfb.log').open('w'),stderr=subprocess.STDOUT,start_new_session=True)
    import time
    time.sleep(1)
    assert xvfb.poll() is None
    elf=root/'game/bin/vehicle-playground.elf'
    args=['/home/spider/tyrax2-debugger-linux-v1/squashfs-root/AppRun','-datapath',str(runtime/'profile'),'-logfile',str(runtime/'emulator.log'),'-batch','-nogui','-elf',str(elf)]
    p=subprocess.Popen(args,cwd=elf.parent,env=env,stdout=(runtime/'stdout.log').open('w'),stderr=subprocess.STDOUT,start_new_session=True)
    record=dict(pid=p.pid,xvfbPid=xvfb.pid,args=args,elfSha256=hashlib.sha256(elf.read_bytes()).hexdigest())
    (runtime/'launch.json').write_text(json.dumps(record,indent=2)+'\n')
    print(json.dumps(record))
elif mode in ('analyze','analyze-lifetime'):
    lifetime_trial=mode=='analyze-lifetime'
    source=root/('motion-lifetime.csv' if lifetime_trial else 'motion-probe.csv')
    phase_length=1800 if lifetime_trial else 1200
    sample_start=1200 if lifetime_trial else 300
    rows=list(csv.DictReader(source.open()))
    assert len(rows)==2160,len(rows)
    rows=[{k:int(v) for k,v in r.items()} for r in rows]
    assert all(v>=0 for r in rows for v in r.values())
    result={'units':'milliseconds; EE Count 294912000 ticks/sec','phases':[]}
    timing=('whole','pacing','period','update','render')
    counters=list(rows[0])[9:]
    for phase in range(6):
        cohort=rows[phase*360:(phase+1)*360]
        observed=phase<3 if lifetime_trial else phase!=3
        assert [r['frame'] for r in cohort]==list(range(phase*phase_length+sample_start,phase*phase_length+sample_start+360))
        assert all(r['phase']==phase and r['whole']>=r['pacing'] and r['whole']==r['update']+r['render'] for r in cohort)
        if lifetime_trial or phase in (1,3,4): assert [r['angle'] for r in cohort]==list(range(360))
        else: assert len({r['angle'] for r in cohort})==1
        if not observed: assert all(r[k]==0 for r in cohort for k in counters)
        else:
            assert all(r['bags']>0 for r in cohort)
            assert all(r['replay']<=r['direct'] and r['outside']+r['direct']+r['partial']<=r['bags'] for r in cohort)
            assert all(r['paint']==0 for r in cohort)
        if 'missNew' in cohort[0]:
            reasons='missNew missBBox missContent missPrim missStreams missProgram missDepth missSize'.split()
            assert all(sum(r[k] for k in reasons)==r['bakeMiss'] for r in cohort)
        work=[(r['whole']-r['pacing'])/294912 for r in cohort]
        worst=max(range(360),key=lambda i:work[i])
        result['phases'].append(dict(phase=phase,observed=observed,means={k:statistics.mean(r[k] for r in cohort)/294912 for k in timing},workMean=statistics.mean(work),workP95=sorted(work)[341],workMax=max(work),longPeriods=sum(r['period']>294912*25 for r in cohort),flips=sum(r['flips'] for r in cohort),counterMeans={k:statistics.mean(r[k] for r in cohort) for k in counters},worst=cohort[worst]))
    if not lifetime_trial:
        assert rows[720]['angle']==result['phases'][1]['worst']['angle']
        assert rows[1800]['angle']==rows[720]['angle'] and rows[0]['angle']==0
    else:
        # A policy experiment must not silently become a different scene.
        for i in range(360):
            for key in ('bags','outside','direct','partial','expandVerts','copyBytes','probeCapture','paint','rebuild'):
                assert rows[i][key]==rows[360+i][key]==rows[720+i][key],(i,key)
        result['lifetimes']=[250,1000,250,250,1000,250]
        result['comparisons']=[]
        for first in (0,3):
            phases=[rows[p*360:(p+1)*360] for p in range(first,first+3)]
            work=lambda r:(r['whole']-r['pacing'])/294912
            result['comparisons'].append(dict(
                phases=list(range(first,first+3)),
                meanSavingsVsBefore=statistics.mean(work(a)-work(b) for a,b in zip(phases[0],phases[1])),
                meanSavingsVsAfter=statistics.mean(work(a)-work(b) for a,b in zip(phases[2],phases[1])),
                angle72Work=[work(p[72]) for p in phases],
                angle72Snapshots=[p[72]['snapshotBytes'] for p in phases],
                peakLiveBytes=[max(r['cachePeakBytes'] for r in p) for p in phases]))
    result['sourceSha256']=hashlib.sha256(source.read_bytes()).hexdigest()
    (root/'analysis.json').write_text(json.dumps(result,indent=2)+'\n')
    for p in result['phases']:
        print(p['phase'], 'work',round(p['workMean'],3),'p95',round(p['workP95'],3),'max',round(p['workMax'],3),'long',p['longPeriods'],'counts',p['counterMeans'])
else:
    raise ValueError(mode)
