from pathlib import Path
import hashlib,json,shutil
lab=Path('F:/Projects/tyrax2-lab-20261001');repo=Path('F:/Projects/tyra-editor')
out=repo/'docs/tyrax2-claude-trials-2026-10-06'
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
def copy(src,dst):
 dst.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(src,dst);assert sha(src)==sha(dst)
def tree(src,dst):
 assert src.exists(),src
 for f in src.rglob('*'):
  if f.is_file() and '__pycache__' not in f.parts and f.suffix in ('.json','.jsonl','.py','.ps1','.md','.log','.err','.cfg','.txt','.cpp','.hpp','.h','.png','.run'):
   copy(f,dst/f.relative_to(src))
for family in ('assert-gate','companion-census','tex1-owned'):
 fixture=lab/(family+'-physical-v1');m=json.loads((fixture/'target-source-manifest.json').read_text());assert m['frozen'] and len(m['files'])==501
 for name in ('target-source-manifest.json','root-native-provenance.json','root-source-freeze.json','root-runtime-authority.json','runtime-assets-manifest.json','root-native-command-exit.json','root-draft-source-authority.json'):
  copy(fixture/name,out/'fixtures'/fixture.name/name)
 for rel,h in m['files'].items():
  src=fixture/rel;assert sha(src)==h,(family,rel)
  if not (out/'source-blobs'/h).exists():copy(src,out/'source-blobs'/h)
 for suffix in ('controls','root-helpers','native-root-review','target-abi','source-controls'):
  src=lab/(family+'-'+suffix+'-v1')
  if src.exists():tree(src,out/'controls'/src.name)
 for src in lab.iterdir():
  if src.is_file() and family in src.name and src.suffix in ('.py','.ps1','.json','.txt'):
   copy(src,out/'helpers'/src.name)
 stem='night-ablation-emulator-'+family+'-order0-20261006'
 tree(lab/(stem+'-evidence'),out/'emulator'/stem)
 tree(lab/(stem+'-launch'),out/'emulator-launch'/stem)
for order in (0,1):
 stem=f'night-ablation-ps2-assert-gate-order{order}-20261006'
 tree(lab/(stem+'-evidence'),out/'physical'/stem)
 for suffix in ('.err','-launcher.log','-launcher.err','-postrun-reset.log','-postrun-reset.err'):
  src=lab/(stem+suffix)
  if src.exists():copy(src,out/'physical-host'/src.name)
stem='night-ablation-ps2-companion-census-order1-20261006'
tree(lab/(stem+'-launch'),out/'rejected'/stem)
for suffix in ('.log','.err','-launcher.log','-launcher.err'):
 src=lab/(stem+suffix)
 if src.exists():copy(src,out/'rejected'/src.name)
for stem in ('assert-gate-between-orders-reset-20261006',):
 for ext in ('.log','.err','.pid'):
  src=lab/(stem+ext)
  if src.exists():copy(src,out/'physical-host'/src.name)
for n in (52,53,54):copy(lab/f'night-ablation-native-v{n}.log',out/'native'/f'night-ablation-native-v{n}.log')
tree(lab/'pmu-completion-review-20261006',out/'primary-review/pmu-completion')
# Preserve primary citations/hashes and review, not the complete Sony manual.
for name in ('report.md','source-pins.json','binutils-mips-opc.c'):
 copy(lab/'pmu-overflow-review-v1'/name,out/'primary-review/pmu-overflow'/name)
for name in ('pmu-scene-proposal-v1.md','pmu-scene-proposal-v2.md','claude-proposals-independent-review-root-v1.json','freeze-claude-trial-root-v1.py','prepare-claude-trial-native-root-v1.py','verify-claude-trials-root-v1.py'):
 copy(lab/name,out/'helpers'/name)
copy(lab/'wild-pool-table-runtime-tools-v3/normalize-physical.py',out/'helpers/normalize-physical.py')
copy(lab/'minimal-stage-stapip-physical-v10/tools/complete-native-provenance.py',out/'helpers/complete-native-provenance.py')
for family in ('tex1-owned-independent-review-v1','night-main11-source-review-v1','pmu-scene-independent-review-v2'):
 tree(lab/family,out/'primary-review'/('night-main11' if family=='night-main11-source-review-v1' else family))
tree(lab/'tex1-owned-class-layout-root-v1',out/'controls/tex1-owned-class-layout-root-v1')
for name in ('compile-tex1-class-layout-root-v1.py','freeze-claude-trial-root-v2.py','prepare-claude-trial-native-root-v2.py'):
 copy(lab/name,out/'helpers'/name)
for version in (1,2):
 fixture=lab/f'pmu-scene-physical-v{version}';m=json.loads((fixture/'target-source-manifest.json').read_text());assert not m['frozen']and len(m['files'])==501
 copy(fixture/'target-source-manifest.json',out/'unreleased-drafts'/fixture.name/'target-source-manifest.json')
 for rel,h in m['files'].items():
  src=fixture/rel;assert sha(src)==h,(fixture,rel)
  if not (out/'source-blobs'/h).exists():copy(src,out/'source-blobs'/h)
 for suffix in ('controls','root-helpers','source-controls'):
  src=lab/f'pmu-scene-{suffix}-v{version}'
  if src.exists():tree(src,out/'unreleased-drafts'/src.name)
tree(lab/'pmu-scene-failure-latch-before-v1',out/'unreleased-drafts/pmu-failure-latch-before')
tree(lab/'pmu-wrapper-compile-root-v1',out/'unreleased-drafts/pmu-wrapper-compile-root-v1')
copy(lab/'compile-pmu-wrappers-root-v1.py',out/'helpers/compile-pmu-wrappers-root-v1.py')
copy(Path(__file__),out/'helpers'/Path(__file__).name)
files={f.relative_to(out).as_posix():sha(f) for f in sorted(out.rglob('*')) if f.is_file() and f.name!='payload-manifest.json'}
(out/'payload-manifest.json').write_bytes((json.dumps({'status':'FROZEN_CLAUDE_TRIAL_EVIDENCE','files':files,'fileCount':len(files),'sourceBlobCount':len(list((out/'source-blobs').iterdir())),'policy':'Only two accepted kind25 physical same-ELF contrasts are hardware prices. Kind24 emulator is sparse activation only; kind28 TEX1 emulator parity is not a hardware price; rejected start is no runtime evidence. PMU source drafts are not released or full-game built; standalone wrapper compile and host controls are not runtime/lifetime qualification. ELF/symbol/object binaries and assets omitted; their hashes retained. Sony manual identified by URL/hash without redistribution.'},indent=2)+'\n').encode())
print('Archived',len(files),'files')
