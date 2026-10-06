from pathlib import Path
import hashlib, json, shutil

lab = Path('F:/Projects/tyrax2-lab-20261001')
repo = Path('F:/Projects/tyra-editor')
out = repo / 'docs/tyrax2-main11-batch-2026-10-06'
assert not (out/'payload-manifest.json').exists(), 'Never overwrite a completed archive'
assert (lab/'main11-v2-failure-diagnosis-v1/report.md').exists(), 'Await source diagnosis'
assert (lab/'night-main11-batch-v2-root-emulator-summary.json').exists()
out.mkdir(exist_ok=True) # Resume this new partial archive only; copy checks exact existing bytes.
sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
bins = {}

def copy(src, dst):
    if '__pycache__' in src.parts or src.suffix in ('.pyc', '.pyo'): return
    data = src.read_bytes()
    if src.suffix.lower() in ('.elf', '.sym', '.o', '.a', '.exe', '.dll') or data[:4] == b'\x7fELF':
        bins[str(src)] = {'sha256':hashlib.sha256(data).hexdigest(), 'bytes':len(data)}
        return
    dst.parent.mkdir(parents=True, exist_ok=True)
    if dst.exists(): assert dst.read_bytes() == data, str(dst)
    else: dst.write_bytes(data)

def tree(src, dst):
    for p in sorted(src.rglob('*')):
        if p.is_file(): copy(p, dst/p.relative_to(src))

for version in ('v1','v2'):
    name = 'night-main11-batch-physical-' + version
    f = lab/name
    m = json.loads((f/'target-source-manifest.json').read_text(encoding='utf-8'))
    assert m['frozen'] and len(m['files']) == 501
    for rel,digest in m['files'].items():
        p=f/rel; assert sha(p)==digest, rel
        dst=out/'source-blobs'/digest
        if dst.exists():assert dst.read_bytes()==p.read_bytes()
        else:dst.write_bytes(p.read_bytes()) # Source inventory includes generated textual livedbg.sym.
    for p in f.iterdir():
        if p.is_file():copy(p,out/'fixtures'/name/p.name)
    for p in (f/'game/bin').glob('*.elf*'):copy(p,out/'fixtures'/name/'binary'/p.name)
    provenance=json.loads((f/'root-native-provenance.json').read_text(encoding='utf-8'))
    rawlog=provenance['build_log']
    log=Path('F:/'+rawlog[7:]) if rawlog.startswith('/mnt/f/') else Path(rawlog)
    assert sha(log)==provenance['build_log_sha256']
    copy(log,out/'builds'/log.name)
    err=log.with_suffix('.err')
    if err.exists():copy(err,out/'builds'/err.name)

for p in sorted(lab.iterdir()):
    if p.is_dir() and ('main11' in p.name):
        if p.name.startswith('night-main11-batch-physical-'):continue
        target='runs' if p.name.startswith('night-ablation-') else 'reviews-and-tools'
        tree(p,out/target/p.name)
    elif p.is_file() and 'main11' in p.name:
        copy(p,out/'reviews-and-tools'/p.name)

copy(lab/'verify-main11-archive-v1.py',out/'reviews-and-tools/verify-main11-archive-v1.py')
(out/'.gitattributes').write_text('* -text whitespace=-trailing-space,-space-before-tab,cr-at-eol\n',encoding='utf-8')
(out/'binary-hashes.json').write_text(json.dumps(bins,indent=2)+'\n',encoding='utf-8')
(out/'README.md').write_text('''# Main-only eleven-box batching evidence, 2026-10-06

Both private native-built emulator captures completed 5,400 loops with zero accepted candidate group activation. V2 retained all guards and deferred private preparation until global loop600; WHY diagnoses different light selection/effective spot and a changing light key. No PS2 pricing, production promotion, or batching speed claim follows.

Each frozen fixture has501 source postimages mapped to content-addressed source-blobs. Fixtures, native/source/ABI/parser proofs, helpers, rejection records, diagnostics, root summaries, owned launch/capture logs and images are preserved. ELF/symbol/native-object/static-archive/executable bytes are hash-only in binary-hashes.json; caches are omitted. Runtime asset manifests/provenance preserve asset identities, not duplicate generated binary assets. No old archive was overwritten.

Run the external LAB verify-main11-archive-v1.py for filesystem verification, or with --staged for the Git index after staging this archive. Payload-manifest.json binds every archived payload except itself. The verifier also checks all1,002 source references and exact unique blob closure; it does not replay execution or validate hash-only binary contents from the archive alone.
''',encoding='utf-8')
manifest={str(p.relative_to(out)).replace('\\','/'):{'sha256':sha(p),'bytes':p.stat().st_size}
          for p in sorted(out.rglob('*')) if p.is_file() and p.name!='payload-manifest.json'}
(out/'payload-manifest.json').write_text(json.dumps(manifest,indent=2)+'\n',encoding='utf-8')
print(json.dumps({'status':'NEW_MAIN11_ARCHIVE_CREATED','payloads':len(manifest),'binaryHashes':len(bins)}))
