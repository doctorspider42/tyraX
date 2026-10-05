"""Apply exact private diagnostic deltas to a verified old V6 source restoration."""
from pathlib import Path
import argparse,json,hashlib
p=argparse.ArgumentParser();p.add_argument('--baseline-source',type=Path,required=True);p.add_argument('--version',type=int,choices=(1,2),required=True);p.add_argument('--out',type=Path,required=True);a=p.parse_args()
root=Path(__file__).resolve().parent;out=a.out.resolve();assert not out.exists() and out.parent.is_dir()
assert out!=root and root not in out.parents
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
archive=json.loads((root/'SHA256.json').read_text())
for name,h in archive['files'].items():assert sha(root/name)==h,name
base=root.parent/'tyrax2-gs-sprite-coronas-2026-10-05/package/evidence-text/wild-gs-sprite-corona-physical-v2/target-source-manifest.json'
assert sha(base)=='b9582677a88857c64d7c807e8c7acfb170d4aa84b5ef826bdd62734f1e26ebe3'
old=json.loads(base.read_text())['files'];assert len(old)==500
for name,h in old.items():assert sha(a.baseline_source/name)==h,name
manifest=json.loads((root/f'fixture-v{a.version}/target-source-manifest.json').read_text())
assert manifest['frozen'] and len(manifest['files'])==502
out.mkdir()
for name,h in manifest['files'].items():
 src=root/f'source-v{a.version}'/name
 if not src.is_file():src=a.baseline_source/name
 assert sha(src)==h,name;dst=out/name;dst.parent.mkdir(parents=True,exist_ok=True);dst.write_bytes(src.read_bytes());assert sha(dst)==h
actual={str(p.relative_to(out)).replace('\\','/'):sha(p)for p in out.rglob('*')if p.is_file()}
assert actual==manifest['files']
print(json.dumps(dict(status='PASS_EXACT_PRIVATE_CORONA24_SOURCE_RESTORATION_ONLY',version=a.version,sourceFiles=502,buildOrDeviceRun=False)))
