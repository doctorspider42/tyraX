"""Check one-pass shared vehicle contacts and report changed-workload hardware costs."""
from pathlib import Path
import argparse,csv,hashlib,json,math,statistics


def analyze(root,output,admission=None):
    meta=json.loads((root/'metadata.json').read_text(encoding='utf8'));assert meta.get('vehicle_contacts_comparison')
    shadow=meta['shadow_enabled'];isolation=meta['vehicle_contact_isolation'];multiplier=meta['vehicle_contact_clock_multiplier']
    phases=meta['expected_phases'];errors=[];hashes={};metrics=[];pairs={}
    log=(root/'hardware.log').read_text(encoding='utf8',errors='replace')
    def check(ok,why):
        if not ok:errors.append(why)
    def rows(name):
        p=root/name;hashes[name]=hashlib.sha256(p.read_bytes()).hexdigest()
        with p.open(encoding='utf8',newline='') as f:return list(csv.DictReader(f))
    check(hashlib.sha256((root/'vehicle-playground.elf').read_bytes()).hexdigest()==meta['elf_sha256'],'ELF hash mismatch')
    check(f'VEHCONTACTMODE isolate={int(isolation)} multiplier={multiplier}' in log,'wrong contact scenario')
    check(f'CB DONE phases={14 if shadow else 28}' in log,'incomplete hardware run')
    check([int(p.stem.split('-')[-1]) for p in sorted(root.glob('collision-phase-*.csv'))]==phases,'wrong phase set')
    for bad in ['CB ERROR','VUSATDIFF ','WORLDGRIDDIFF ','Assertion failed','EE exception','Failed to load']:
        check(bad not in log,'log diagnostic: '+bad)
    check('VUSATCHECK cases=2048 failures=0 ' in log,'missing SAT startup')
    check('VUSATPRECISION cases=50000 failures=0' in log,'missing precision startup')
    for image in [0,1]:
        check(f'VUSHARED replay image={image} accepted=1' in log,f'missing shared replay {image}')
        check(any(f'VUSHARED image={image} ' in l and 'cases=48000' in l and 'failures=0' in l for l in log.splitlines()),f'missing shared corpus {image}')
        check(f'VUSATREPLAY reference={image} saved=1 raw=1 hit=1 accepted=1' in log,f'missing late replay {image}')
    if not shadow:
        check(admission is not None,'missing same-ELF admission')
        if admission:
            old=json.loads(admission.read_text(encoding='utf8'))
            check(old['qualified'] and old['metadata']['shadow_enabled'] and
                old['metadata']['elf_sha256']==meta['elf_sha256'] and
                old['metadata']['vehicle_contact_isolation']==isolation and
                old['metadata']['vehicle_contact_clock_multiplier']==multiplier,'admission mismatch')
    for phase in phases:
        data=rows(f'collision-phase-{phase:02}.csv');vehicle=rows(f'vehicle-contacts-{phase}.csv')
        check([int(r['frame']) for r in data]==list(range(150,750)),f'phase {phase}: measured frame coverage')
        check([int(r['frame']) for r in vehicle]==list(range(750)),f'phase {phase}: shared frame coverage')
        arm=1-int(data[0]['baseline']);key=tuple(int(data[0][k]) for k in ['repeat','added','layout']);pairs.setdefault(key,{})[arm]=vehicle
        for r in vehicle:
            check(int(r['enabled'])==arm,f'phase {phase}: wrong arm')
            check(int(r['full_steps'])==1,f'phase {phase}: extra/missing full physics step')
            check(int(r['vehicle_steps'])==multiplier,f'phase {phase}: wrong vehicle substeps')
            check(int(r['cars'])==3,f'phase {phase}: missing chassis')
            check(int(r['errors'])==int(r['missing'])==0,f'phase {phase}: contact/physics diagnostics')
            check(all(math.isfinite(float(r[k])) for k in ['max_depth','max_momentum_ratio','max_shape_ratio','car_correction']),f'phase {phase}: nonfinite shared state')
            if arm and shadow:
                check(int(r['shape_checks'])==24,f'phase {phase}: missing collider checks')
                check(int(r['reference_pairs'])>0,f'phase {phase}: no exhaustive pair checks')
                check(float(r['max_shape_ratio'])<=1 and float(r['max_momentum_ratio'])<=1,f'phase {phase}: numerical budget exceeded')
            else:check(int(r['shape_checks'])==int(r['reference_pairs'])==int(r['impulse_updates'])==0,f'phase {phase}: diagnostic work in control/timing')
            if not arm:check(int(r['contacts'])==int(r['pairs'])==0,f'phase {phase}: common contacts in legacy control')
        total={k:sum(int(r[k]) for r in vehicle) for k in ['pairs','contacts','impulse_updates','reference_hits','woken','missing']}
        if isolation:check(total['contacts']==total['reference_hits']==0,f'phase {phase}: isolation had vehicle/prop interaction')
        elif arm:
            check(total['pairs']>0 and total['contacts']>0 and sum(float(r['car_correction']) for r in vehicle)>0,f'phase {phase}: no shared reaction coverage')
            if shadow:check(total['reference_hits']>0 and total['impulse_updates']>0,f'phase {phase}: no canonical/impulse coverage')
        for r in data:
            check(r['generated_contacts']==r['contacts'] and int(r['dropped_contacts'])==int(r['push_overflow'])==0,f'phase {phase}: omitted contacts')
            check(r['push_candidates']==r['push_tested'],f'phase {phase}: omitted legacy bumper candidates')
            if arm:check(int(r['pushes'])==int(r['push_candidates'])==0,f'phase {phase}: legacy bumper also ran')
            check(all(math.isfinite(float(r[k])) for k in ['car_z','car_speed','velocity_energy','max_depth']),f'phase {phase}: nonfinite benchmark state')
        micro=rows(f'vu0-phase-{phase:02}.csv');classes=rows(f'vu0-class-{phase}.csv');world=rows(f'world-grid-{phase}.csv')
        check(len(micro)==1 and int(micro[0]['jobs'])>0 and int(micro[0]['validation_errors'])==int(micro[0]['missed_reference_hits'])==0,f'phase {phase}: micro oracle')
        check(len(classes)==1 and sum(int(classes[0][k]) for k in ['proved_negative','confirmed_negative','confirmed_ccd','confirmed_tie','confirmed_parallel','confirmed_positive'])==int(micro[0]['jobs'])==int(classes[0]['jobs']),f'phase {phase}: incomplete result classes')
        check(len(world)==1 and int(world[0]['errors'])==0 and int(world[0]['verified'])==(int(world[0]['queries']) if shadow else 0),f'phase {phase}: world oracle')
        friction=rows(f'zero-friction-{phase}.csv')
        check(len(friction)==1 and int(friction[0]['enabled'])==1 and 0<int(friction[0]['frames'])<=750 and 0<int(friction[0]['skipped'])<=int(friction[0]['visits']),f'phase {phase}: friction/solve coverage')
        drive={int(r['frame']) for r in data if r['state']=='drive'}
        metrics.append(dict(phase=phase,arm='shared-contact-solver' if arm else 'legacy-bumper',coverage=total,
            mean_drive_combined_ms=statistics.mean(int(r['combined_ticks']) for r in vehicle if int(r['frame']) in drive)/294912,
            mean_drive_physics_ms=statistics.mean(int(r['physics_ticks']) for r in data if int(r['frame']) in drive)/294912,
            mean_drive_solver_ms=statistics.mean(int(r['solver_ticks']) for r in data if int(r['frame']) in drive)/294912,
            final_car_z=float(data[-1]['car_z'])))
    differences=[]
    if isolation and shadow:
        for key,pair in pairs.items():
            check(set(pair)=={0,1},f'{key}: incomplete isolation pair')
            if set(pair)!={0,1}:continue
            for a,b in zip(pair[0],pair[1]):
                if a['hash_a']!=b['hash_a'] or a['hash_b']!=b['hash_b']:differences.append(dict(workload=key,frame=int(a['frame'])))
        check(not differences,'isolation end-state mismatch')
    result=dict(metadata=meta,qualified=not errors,performance_qualified=not shadow and isolation and not errors,
        changed_workload_costs_qualified=not shadow and not isolation and not errors,
        normal_clock_fps_qualified=False,production_integration_qualified=False,prior_source_equivalence_qualified=False,
        errors=sorted(set(errors)),metrics=metrics,first_isolation_differences=differences[:8],raw_sha256=hashes,
        log_sha256=hashlib.sha256((root/'hardware.log').read_bytes()).hexdigest(),
        scope='Private same-ELF one-pass shared vehicle/prop contact prototype. World and car/car handling remain specialized; collision yaw is constrained to the existing vehicle model. Impact trajectories/workloads deliberately differ, so measured costs are not equal-workload speedups or normal-clock FPS. Isolation admission requires all750 ordered end-state fingerprints to match, no car/prop contacts, exhaustive pair checks, unchanged chassis collider and finite-mass impulse budgets. No claim of exact rotational CCD, original-source equivalence or production readiness.')
    output.write_text(json.dumps(result,indent=2),encoding='utf8')
    print(json.dumps(dict(qualified=result['qualified'],errors=result['errors'],first_isolation_differences=differences[:2])))
    return result


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('root',type=Path);p.add_argument('--output',required=True,type=Path);p.add_argument('--admission',type=Path);a=p.parse_args()
    if not analyze(a.root,a.output,a.admission)['qualified']:raise SystemExit(1)
