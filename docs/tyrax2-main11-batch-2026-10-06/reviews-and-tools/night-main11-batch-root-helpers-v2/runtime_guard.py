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

def verify_native_binding(proof,provenance,manifest_sha,elf_sha,symbol_sha,proof_sha,expected_proof_sha):
 assert proof_sha==expected_proof_sha,'native proof hash differs from release'
 assert proof['status'].startswith('PASS_')and not proof['blockers'],'native proof not qualified'
 assert provenance['build_backend']=='native'and provenance['build_exit_code']==0 and provenance['verified_source_files']==501,'native provenance qualification'
 assert proof['sourceFiles']==501,'proof source count'
 assert proof['sourceManifestSha256']==provenance['target_source_manifest_sha256']==manifest_sha,'proof manifest binding'
 assert proof['actualELFSha256']==provenance['selected_elf_sha256']==elf_sha,'proof ELF binding'
 assert proof['actualSymbolSha256']==provenance['selected_symbol_sha256']==symbol_sha,'proof symbol binding'

def verify_fixture(fixture):
 fixture=path(fixture);assert fixture.name=='night-main11-batch-physical-v2'
 manifest=load(fixture/'target-source-manifest.json');assert manifest['frozen']and len(manifest['files'])==501
 for rel,h in manifest['files'].items():assert sha(fixture/rel)==h,rel
 release=load(fixture/'root-runtime-authority.json');freeze=fixture/'root-source-freeze.json';assert release['rootSourceFreezeSha256']==sha(freeze)
 for name in ['review','host']:assert sha(path(release[name]))==release[name+'Sha256']
 host=load(path(release['host']));assert len(host['sourcePins'])==7
 for source,h in host['sourcePins'].items():assert sha(path(source))==h,source
 expected=load(Path(__file__).with_name('host-authority.json'));assert host['sourcePins']==expected['sourcePins']
 assert host['pricingSourceManifestSha256']==sha(fixture/'target-source-manifest.json')
 native=load(fixture/'root-native-provenance.json');assert native['build_backend']=='native'and native['build_exit_code']==0 and native['verified_source_files']==501
 assert native['target_source_manifest_sha256']==sha(fixture/'target-source-manifest.json')
 proof_path=path(release['native']);assert sha(proof_path)==release['nativeSha256'];proof=load(proof_path)
 game=fixture/'game/bin';verify_native_binding(proof,native,sha(fixture/'target-source-manifest.json'),sha(game/'vehicle-playground.elf'),sha(game/'vehicle-playground.elf.sym'),sha(proof_path),release['nativeSha256']);assert sha(game/'vehicle-playground.elf')==native['selected_elf_sha256'];assert sha(game/'vehicle-playground.elf.sym')==native['selected_symbol_sha256']
 assert sha(path(native['build_log']))==native['build_log_sha256']
 assets=load(fixture/'runtime-assets-manifest.json')['files'];assert len(assets)==native['runtime_assets']==298
 for rel,h in assets.items():assert sha(game/rel)==h,rel
 return manifest
