"""Read-only preflight: completed root release required before configuration writes or process launch."""
from pathlib import Path
import json,hashlib,os,re
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
def path(s):
 s=str(s)
 if os.name=='nt'and re.match(r'^/mnt/[a-z]/',s):return Path(s[5].upper()+':/'+s[7:])
 if os.name!='nt'and re.match(r'^[A-Za-z]:[\\/]',s):return Path('/mnt/'+s[0].lower()+'/'+s[3:].replace('\\','/'))
 return Path(s)
def load(p):return json.loads(p.read_text(encoding='utf-8-sig'))
def verify_fixture(fixture):
 fixture=path(fixture);assert fixture.name=='night-producer-probe-physical-v2'
 manifest=load(fixture/'target-source-manifest.json');assert manifest['frozen']and len(manifest['files'])==501
 for rel,h in manifest['files'].items():assert sha(fixture/rel)==h,rel
 release=load(fixture/'root-runtime-authority.json');freeze=fixture/'root-source-freeze.json';assert release['rootSourceFreezeSha256']==sha(freeze)
 for name in ['review','host']:assert sha(path(release[name]))==release[name+'Sha256']
 host=load(path(release['host']));assert len(host['sourcePins'])==6
 for source,h in host['sourcePins'].items():assert sha(path(source))==h,source
 expected=load(Path(__file__).with_name('host-authority.json'));assert host['sourcePins']==expected['sourcePins']
 assert host['pricingSourceManifestSha256']==sha(fixture/'target-source-manifest.json')
 native=load(fixture/'root-native-provenance.json');assert native['build_backend']=='native'and native['build_exit_code']==0 and native['verified_source_files']==501
 assert native['target_source_manifest_sha256']==sha(fixture/'target-source-manifest.json')
 game=fixture/'game/bin';assert sha(game/'vehicle-playground.elf')==native['selected_elf_sha256'];assert sha(game/'vehicle-playground.elf.sym')==native['selected_symbol_sha256']
 assert sha(path(native['build_log']))==native['build_log_sha256']
 assets=load(fixture/'runtime-assets-manifest.json')['files'];assert len(assets)==native['runtime_assets']==298
 for rel,h in assets.items():assert sha(game/rel)==h,rel
 return manifest
