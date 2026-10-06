from pathlib import Path
import hashlib, json, shutil, subprocess, sys

lab = Path('F:/Projects/tyrax2-lab-20261001')
repo = Path('F:/Projects/tyra-editor')
out = repo/'docs/tyrax2-claude-hardware-2026-10-06'
sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
stems = [
    'night-ablation-ps2-tex1-owned-order1-20261006',
    'night-ablation-ps2-tex1-owned-order0-20261006',
    'night-ablation-ps2-companion-census-order1-retry1-20261006',
    'night-ablation-ps2-companion-census-order0-20261006',
]

def copy(src, dst):
    dst.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(src, dst)
    assert sha(src) == sha(dst)

def tree(src, dst):
    assert src.is_dir(), src
    for f in sorted(src.rglob('*')):
        if f.is_file() and '__pycache__' not in f.parts and f.suffix in ('.json','.jsonl','.py','.ps1','.md','.log','.err','.cfg','.txt','.cpp','.hpp','.h','.run'):
            copy(f, dst/f.relative_to(src))

def summarize():
    rows=[]
    for stem in stems:
        d=json.loads((lab/(stem+'-evidence')/'strict-analysis.json').read_text())
        assert d['status']=='PASS_STRICT_NIGHT_MASKED_LOOP_CAPTURE' and d['environment']=='ps2' and d['engine_loops']==5400
        ms=[d['by_phase'][str(i)]['mean_non_pacing_ms'] for i in range(3)]
        row={'stem':stem,'order':d['order'],'phaseNonPacingMs':ms,'outerDriftMs':abs(ms[0]-ms[2])}
        if 'companion-census' in stem:
            row['nightFlags']=d['phaseNightFlags']
            row['nightMinusDayMs']=(ms[0]+ms[2])/2-ms[1] if d['order']==1 else ms[1]-(ms[0]+ms[2])/2
            cold=[]
            for phase in range(3):
                for offset in (750,1155):
                    roles=[d['companionCensus'][f'{phase}:{offset}:{r}'] for r in range(5)]
                    cold.append({'phase':phase,'offset':offset,'night':int(roles[0]['night']),
                        'enteredByRole':[int(r['f0']) for r in roles],
                        'totals':{f'f{i}':sum(int(r[f'f{i}']) for r in roles) for i in range(25)}})
            row['coldWitnesses']=cold
        else:
            row['savingMs']=ms[1]-(ms[0]+ms[2])/2 if d['order']==1 else (ms[0]+ms[2])/2-ms[1]
        rows.append(row)
    return {'status':'FOUR_ACCEPTED_PHYSICAL_RUNS','rows':rows,'limitations':'Kind24 compares authored day/night workloads, not observer tax or optimization gain. Kind28 contrasts are private same-ELF enabled differences; common code/state cost remains unpriced. Sparse witnesses are not every-frame work. No production promotion or full-night60 acceptance. Physical human visual confirmation remains pending.'}

if '--verify' in sys.argv:
    m=json.loads((out/'payload-manifest.json').read_text())
    assert sha(repo/'docs/tyrax2-claude-trials-2026-10-06/payload-manifest.json')==m['sourceArchiveSha256']
    actual={p.relative_to(out).as_posix():sha(p) for p in out.rglob('*') if p.is_file() and p.name!='payload-manifest.json'}
    assert actual==m['files']
    for stem in stems:
        evidence=out/'physical'/stem
        d=json.loads((evidence/'strict-analysis.json').read_text())
        assert d['engine_loops']==5400 and d['environment']=='ps2' and d['status']=='PASS_STRICT_NIGHT_MASKED_LOOP_CAPTURE'
        machine=json.loads((evidence/'machine-evidence.json').read_text())
        assert machine['status']=='PASS_COMPLETED_NIGHT_SOURCE_NATIVE_CFG_ASSET_BOUND_RUNTIME'
        family='companion-census' if 'companion-census' in stem else 'tex1-owned'
        fixture=repo/'docs/tyrax2-claude-trials-2026-10-06/fixtures'/(family+'-physical-v1')
        assert sha(fixture/'target-source-manifest.json')==sha(evidence/'target-source-manifest.json')==machine['sourceManifestSha256']
        assert sha(evidence/'night-ablation.log')==machine['liveArtifactSha256']
        for rel,h in machine['files'].items():
            if (evidence/rel).exists():assert sha(evidence/rel)==h,rel
        assert sha(fixture/'root-native-provenance.json')==sha(evidence/'root-native-provenance.json')
    if '--staged' in sys.argv:
        entries=dict(m['files']);entries['payload-manifest.json']=sha(out/'payload-manifest.json')
        for rel,h in entries.items():
            raw=subprocess.check_output(['git','show',':'+(out/rel).relative_to(repo).as_posix()])
            assert hashlib.sha256(raw).hexdigest()==h
    print('PASS_FOUR_RUN_HARDWARE_ARCHIVE_HASHES',len(actual))
    sys.exit(0)

assert not out.exists(), 'Never overwrite an existing archive'
summary=summarize()
for stem in stems:
    evidence=lab/(stem+'-evidence')
    tree(evidence,out/'physical'/stem)
    for suffix in ('.err','-launcher.log','-launcher.err','-postrun-reset.log','-postrun-reset.err','-postrun-reset.pid'):
        src=lab/(stem+suffix)
        if src.exists():copy(src,out/'host'/src.name)
for stem in ('tex1-owned-between-orders-reset-20261006','companion-census-between-orders-reset-20261006'):
    for suffix in ('.log','.err','.pid'):
        src=lab/(stem+suffix)
        if src.exists():copy(src,out/'host'/src.name)
for name in ('night-main11-batch-review-v1','pmu-mode-primary-review-v1'):
    tree(lab/name,out/'review'/name)
copy(lab/'tex1-owned-physical-contrast-root-v1.json',out/'tex1-contrast.json')
(out/'physical-summary.json').write_text(json.dumps(summary,indent=2)+'\n',encoding='utf-8')
copy(Path(__file__),out/'helpers'/Path(__file__).name)
files={p.relative_to(out).as_posix():sha(p) for p in sorted(out.rglob('*')) if p.is_file()}
(out/'payload-manifest.json').write_text(json.dumps({'status':'FROZEN_HARDWARE_FOLLOWUP','files':files,'fileCount':len(files),'sourceArchive':'../tyrax2-claude-trials-2026-10-06/payload-manifest.json','sourceArchiveSha256':sha(repo/'docs/tyrax2-claude-trials-2026-10-06/payload-manifest.json'),'policy':summary['limitations']},indent=2)+'\n',encoding='utf-8')
print('Archived',len(files),'payload files')
