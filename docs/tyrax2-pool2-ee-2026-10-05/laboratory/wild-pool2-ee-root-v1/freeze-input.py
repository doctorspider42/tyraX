"""Root-only freeze after actual source/host review; no build or launch."""
from pathlib import Path
import argparse, hashlib, importlib.util, json
p=argparse.ArgumentParser(); p.add_argument('fixture', type=Path); p.add_argument('--review',type=Path,required=True); p.add_argument('--host',type=Path,required=True); a=p.parse_args()
sha=lambda f:hashlib.sha256(f.read_bytes()).hexdigest()
assert a.fixture.is_dir() and a.review.is_file() and a.host.is_file()
assert not (a.fixture/'root-source-freeze.json').exists()
repo=Path('F:/Projects/tyra-editor')
spec=importlib.util.spec_from_file_location('quiet_source',repo/'tools/tyrax2-quiet-fixture.py'); m=importlib.util.module_from_spec(spec); spec.loader.exec_module(m)
files=m.source_inputs(a.fixture,'vehicle-playground.tyra')
assert len(files)==499, 'reviewed exact source count mismatch'
for f in ('native_call_cache.hpp','spr_transaction_storage.hpp'):
 assert not (a.fixture/'tyra/engine/inc/renderer/core/paths/path1'/f).exists(), 'Rejected prototype footprint'
headers=('night_ablation.hpp','night_plan.hpp','night_sampler.hpp','night_runtime.hpp','quiet_runtime.hpp','quiet_cadence.hpp')
for name in headers: assert (a.fixture/'tyra/engine/inc/debug'/name).is_file(), name
manifest={'frozen':True,'status':'ROOT_FROZEN_SOURCE_NATIVE_RUNTIME_PENDING','files':files,'reviewSha256':sha(a.review),'hostProofSha256':sha(a.host)}
(a.fixture/'target-source-manifest.json').write_bytes((json.dumps(manifest,indent=2)+'\n').encode())
record={'status':'ROOT_SOURCE_FREEZE_ONLY','sourceManifestSha256':sha(a.fixture/'target-source-manifest.json'),'sourceFiles':len(files),'review':str(a.review),'reviewSha256':sha(a.review),'host':str(a.host),'hostSha256':sha(a.host),'helperSha256':sha(Path(__file__)),'nativeAccepted':False,'runtimeAccepted':False}
(a.fixture/'root-source-freeze.json').write_bytes((json.dumps(record,indent=2)+'\n').encode()); print(json.dumps(record,indent=2))
