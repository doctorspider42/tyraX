"""Root-only fresh correctness fixture construction; no build/device operation."""
from pathlib import Path
import argparse,json,hashlib,shutil,difflib
P=Path(__file__).parent;sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
def need(x,s):
 if not x:raise ValueError(s)
def main():
 a=argparse.ArgumentParser();a.add_argument('--base',type=Path,required=True);a.add_argument('--out',type=Path,required=True);a.add_argument('--execute',action='store_true');q=a.parse_args()
 b=q.base.resolve();o=q.out.resolve();need(not o.exists(),'NEW output only');need(o!=b and b not in o.parents,'isolated output')
 m=json.loads((b/'target-source-manifest.json').read_text(encoding='utf-8-sig'))
 need(m['frozen'] is True and len(m['files'])==499,'exact frozen integrated Pool2 source required')
 for rel,h in m['files'].items():need(sha(b/rel)==h,'base source drift '+rel)
 s=P.parent/'wild-pool-table-proposal-v3/source-proof.json';need(sha(s)=='e63230b7e051ab28cab541c003792ce783109ba9467d3221a80145845b109145','sealed proposal changed')
 sealed=json.loads(s.read_text())
 for rel,h in sealed['outputPins'].items():need(sha(b/rel)==h,'qualified target overlay mismatch '+rel)
 rel='game/src/terrain_game.cpp';old=(b/rel).read_bytes();txt=old.decode('utf8')
 anchor='#include "game_runtime.gen.hpp"';init='void TerrainGame::init() {';loop='void TerrainGame::loop() {'
 for token in(anchor,init,loop):need(txt.count(token)==1,'unique diagnostic insertion '+token)
 nl='\r\n' if b'\r\n' in old else '\n'
 new=txt.replace(anchor,anchor+nl+'#include "pool2_static_probe.hpp"',1).replace(init,init+nl+'  Pool2StaticProbe::init(engine, stapip);'+nl+'  return;  // dedicated correctness ELF: ordinary init is excluded',1).replace(loop,loop+nl+'  Pool2StaticProbe::loop(engine, stapip);'+nl+'  return;  // no new ordinary game/VU job after probe completion',1).encode('utf8')
 plan=dict(status='PREPARED_ROOT_ONLY_FRESH_POOL2_STATIC_CORRECTNESS_SOURCE_NOT_NATIVE_ACCEPTED',base=str(b),baseManifestSha256=sha(b/'target-source-manifest.json'),baseFreezeSha256=sha(b/'root-source-freeze.json'),sealedProposalSha256=sha(s),changedSource=rel,newSource='game/inc/pool2_static_probe.hpp',newSourceSha256=sha(P/'pool2_static_probe.hpp'),oldGameSha256=sha(b/rel),newGameSha256=hashlib.sha256(new).hexdigest(),nativeBuildOrDeviceInvoked=False)
 if not q.execute:print(json.dumps(plan,indent=2));return
 o.mkdir()
 for name in('game','tyra'):shutil.copytree(b/name,o/name,ignore=shutil.ignore_patterns('bin','obj','build','__pycache__','.git'))
 (o/rel).write_bytes(new);shutil.copyfile(P/'pool2_static_probe.hpp',o/'game/inc/pool2_static_probe.hpp')
 files=dict(m['files']);files[rel]=plan['newGameSha256'];files['game/inc/pool2_static_probe.hpp']=plan['newSourceSha256']
 for rel,h in files.items():need(sha(o/rel)==h,'reconstruction drift '+rel)
 (o/'source.patch').write_text(''.join(difflib.unified_diff(txt.splitlines(True),new.decode().splitlines(True),fromfile='a/game/src/terrain_game.cpp',tofile='b/game/src/terrain_game.cpp')),encoding='utf8',newline='')
 (o/'README.md').write_text((P/'OVERLAY-README.md').read_text(encoding='utf8'),encoding='utf8')
 (o/'target-source-manifest.json').write_text(json.dumps(dict(frozen=False,status='PRIVATE_POOL2_STATIC_CORRECTNESS_INPUT_NOT_NATIVE_ACCEPTED',sourceFiles=500,files=files),indent=2)+'\n',encoding='utf8')
 plan['all500ReconstructedExact']=True;plan['unchanged499BaseSources']=498;plan['sourceManifestSha256']=sha(o/'target-source-manifest.json');plan['helperSha256']=sha(Path(__file__))
 (o/'probe-source-proof.json').write_text(json.dumps(plan,indent=2)+'\n',encoding='utf8');print(json.dumps(plan,indent=2))
if __name__=='__main__':main()
