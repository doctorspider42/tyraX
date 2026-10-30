"""Qualify the shared-clock prototype without calling changed trajectories a speedup."""
from pathlib import Path
import argparse,csv,hashlib,json,math,statistics,subprocess,sys


def analyze(root,output,admission=None):
    meta=json.loads((root/'metadata.json').read_text(encoding='utf8'))
    assert meta.get('unified_timeline_comparison')
    multiplier=meta['timeline_multiplier'];shadow=meta['shadow_enabled'];phases=meta['expected_phases']
    errors=[];metrics=[];hashes={};arms={};differences=[]
    def check(ok,why):
        if not ok:errors.append(why)
    def rows(name):
        p=root/name;hashes[name]=hashlib.sha256(p.read_bytes()).hexdigest()
        return list(csv.DictReader(p.open(encoding='utf8',newline='')))
    log=(root/'hardware.log').read_text(encoding='utf8',errors='replace')
    check(hashlib.sha256((root/'vehicle-playground.elf').read_bytes()).hexdigest()==meta['elf_sha256'],'ELF hash mismatch')
    check(f'UNIFIEDCLOCK startup multiplier={multiplier}\n' in log,'wrong runtime clock mode')
    check(f'CB DONE phases={14 if shadow else 28}' in log,'incomplete log')
    check([int(p.stem.split('-')[-1]) for p in sorted(root.glob('collision-phase-*.csv'))]==phases,'wrong phase set')
    for bad in ['CB ERROR','VUSATDIFF ','WORLDGRIDDIFF ','Assertion failed','Failed to load','EE exception']:
        check(bad not in log,'log diagnostic: '+bad)
    check('VUSATCHECK cases=2048 failures=0 ' in log,'missing SAT startup')
    check('VUSATPRECISION cases=50000 failures=0' in log,'missing precision startup')
    for image in [0,1]:
        check(f'VUSHARED replay image={image} accepted=1' in log,f'missing shared replay {image}')
        check(any(f'VUSHARED image={image} ' in l and 'cases=48000' in l and 'failures=0' in l for l in log.splitlines()),f'missing shared startup {image}')
        check(f'VUSATREPLAY reference={image} saved=1 raw=1 hit=1 accepted=1' in log,f'missing positive replay {image}')
    if not shadow or multiplier==2:
        check(admission is not None,'missing nominal same-ELF admission')
        if admission:
            old=json.loads(admission.read_text(encoding='utf8'))
            check(old['qualified'] and old['metadata']['shadow_enabled'] and old['metadata']['timeline_multiplier']==1 and old['metadata']['elf_sha256']==meta['elf_sha256'],'nominal admission mismatch')
    for phase in phases:
        data=rows(f'collision-phase-{phase:02}.csv');clock=rows(f'unified-timeline-{phase}.csv')
        check([int(r['frame']) for r in data]==list(range(150,750)),f'phase {phase}: measured frame set')
        check([int(r['frame']) for r in clock]==list(range(750)),f'phase {phase}: clock frame set')
        arm=1-int(data[0]['baseline']);key=tuple(int(data[0][k]) for k in ['repeat','added','layout'])
        arms.setdefault(key,{})[arm]=clock
        for r in clock:
            check(int(r['enabled'])==arm,f'phase {phase}: wrong arm')
            check(int(r['steps'])==multiplier and int(r['physics'])==(multiplier if arm else 1) and int(r['vehicles'])==multiplier,f'phase {phase}: schedule coverage')
            check(int(r['errors'])==0 and int(r['bodies'])>0 and int(r['cars'])>0,f'phase {phase}: invalid state or missing participants')
            check(abs(float(r['frame_dt'])-multiplier/60)<1e-7 and abs(float(r['step_dt'])*multiplier-float(r['frame_dt']))<1e-7,f'phase {phase}: clock interval')
        for r in data:
            check(r['contacts']==r['generated_contacts'] and int(r['dropped_contacts'])==int(r['push_overflow'])==0 and r['push_candidates']==r['push_tested'],f'phase {phase}: omitted contacts/pushes')
            check(all(math.isfinite(float(r[k])) for k in ['car_z','car_speed','velocity_energy','max_depth']),f'phase {phase}: nonfinite')
        micro=rows(f'vu0-phase-{phase:02}.csv')
        check(len(micro)==1 and int(micro[0]['jobs'])>0 and int(micro[0]['validation_errors'])==int(micro[0]['missed_reference_hits'])==0,f'phase {phase}: micro oracle')
        classes=rows(f'vu0-class-{phase}.csv')
        check(len(classes)==1 and sum(int(classes[0][k]) for k in ['proved_negative','confirmed_negative','confirmed_ccd','confirmed_tie','confirmed_parallel','confirmed_positive'])==int(micro[0]['jobs'])==int(classes[0]['jobs']),f'phase {phase}: incomplete micro classifications')
        world=rows(f'world-grid-{phase}.csv')
        check(len(world)==1 and int(world[0]['errors'])==0 and int(world[0]['verified'])==(int(world[0]['queries']) if shadow else 0),f'phase {phase}: world oracle')
        check(int(world[0]['queries'])>0 if arm else int(world[0]['queries'])==0,f'phase {phase}: world oracle coverage')
        fingerprints=rows(f'world-overlap-{phase}.csv')
        check([int(r['frame']) for r in fingerprints]==list(range(750)) if shadow else not fingerprints,f'phase {phase}: ordered contact coverage')
        friction=rows(f'zero-friction-{phase}.csv')
        check(len(friction)==1 and int(friction[0]['enabled'])==1 and 0<int(friction[0]['frames'])<=750*(multiplier if arm else 1) and 0<int(friction[0]['skipped'])<=int(friction[0]['visits']),f'phase {phase}: shared friction guard coverage')
        drive={int(r['frame']) for r in data if r['state']=='drive'}
        metrics.append(dict(phase=phase,arm='shared-clock' if arm else 'legacy-clock',
            mean_drive_combined_ms=statistics.mean(int(r['ticks']) for r in clock if int(r['frame']) in drive)/294912,
            mean_drive_object_physics_ms=statistics.mean(int(r['physics_ticks']) for r in data if int(r['frame']) in drive)/294912))
    if multiplier==1:
        # Reuse the established full collision admission, including canonical
        # shadows, raw initialized snapshots, workload equality and replay gates.
        base=output.with_suffix('.collision.json')
        command=[sys.executable,str(Path(__file__).with_name('world_overlap_analyze.py')),str(root),'--output',str(base)]
        if not shadow:command+=['--admission',str(admission)]
        result=subprocess.run(command,capture_output=True,text=True)
        check(result.returncode==0,'collision gate: '+result.stdout.strip()+result.stderr.strip())
        if shadow:
            for key,pair in arms.items():
                check(set(pair)=={0,1},f'{key}: missing arm')
                if set(pair)!={0,1}:continue
                for x,y in zip(pair[0],pair[1]):
                    if any(x[k]!=y[k] for k in ['hash_a','hash_b','bodies','cars']):differences.append(dict(workload=key,frame=int(x['frame'])))
            check(not differences,'nominal end-state fingerprint mismatch')
    report=dict(metadata=meta,qualified=not errors,performance_qualified=not errors and not shadow and multiplier==1,
        normal_clock_fps_qualified=False,errors=sorted(set(errors)),metrics=metrics,
        end_state_mismatches=differences[:12],raw_sha256=hashes,
        log_sha256=hashlib.sha256((root/'hardware.log').read_bytes()).hexdigest(),
        scope='Private shared-clock orchestration only. Detectors, legacy vehicle response and EE solver remain separate; VU0 body-pair dispatch is unchanged. Nominal admission requires matching workload/contacts and end-state fingerprints. Double-step runs deliberately change trajectories/workload and qualify only schedule coverage/numerical-oracle screening, never a speedup or complete gameplay correctness. No normal-clock FPS claim.')
    output.write_text(json.dumps(report,indent=2),encoding='utf8')
    print(json.dumps(dict(qualified=report['qualified'],performance_qualified=report['performance_qualified'],errors=report['errors'],first_mismatches=differences[:2])))
    return report


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('root',type=Path);p.add_argument('--output',required=True,type=Path);p.add_argument('--admission',type=Path);a=p.parse_args()
    if not analyze(a.root,a.output,a.admission)['qualified']:raise SystemExit(1)
