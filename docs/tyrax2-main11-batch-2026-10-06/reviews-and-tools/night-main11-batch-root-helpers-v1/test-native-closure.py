"""Pure symbolic closure tests. No ELF, native build or runtime proof is created."""
from pathlib import Path
import argparse,copy,json
from runtime_guard import verify_native_binding
p=argparse.ArgumentParser();p.add_argument('--out',type=Path,required=True);a=p.parse_args();assert not a.out.exists(),'unique output file required'
proof=dict(status='PASS_SYMBOLIC_HOST_FIXTURE_ONLY',blockers=[],sourceFiles=501,sourceManifestSha256='manifest',actualELFSha256='elf',actualSymbolSha256='symbol')
native=dict(build_backend='native',build_exit_code=0,verified_source_files=501,target_source_manifest_sha256='manifest',selected_elf_sha256='elf',selected_symbol_sha256='symbol')
args=['manifest','elf','symbol','proof','proof'];verify_native_binding(proof,native,*args);checks=['positive_symbolic_consistent_identity']
for where,fields in [('proof',['sourceManifestSha256','actualELFSha256','actualSymbolSha256','sourceFiles','blockers','status']),('native',['target_source_manifest_sha256','selected_elf_sha256','selected_symbol_sha256','verified_source_files','build_backend','build_exit_code'])]:
 for field in fields:
  q,r=copy.deepcopy(proof),copy.deepcopy(native);target=q if where=='proof'else r
  target[field]=['blocker']if field=='blockers'else 500 if field.endswith('Files')or field=='verified_source_files'else 1 if field=='build_exit_code'else 'CHANGED'
  try:verify_native_binding(q,r,*args)
  except (AssertionError,KeyError):checks.append('reject_'+where+'_'+field)
  else:raise AssertionError(where+field)
for i,name in enumerate(['current_manifest','current_ELF','current_symbol','native_proof_hash','release_native_hash']):
 changed=args.copy();changed[i]='CHANGED'
 try:verify_native_binding(proof,native,*changed)
 except AssertionError:checks.append('reject_'+name)
 else:raise AssertionError(name)
a.out.write_text(json.dumps(dict(status='PASS_PURE_HOST_NATIVE_IDENTITY_CLOSURE_ONLY',checks=checks,nativeRuntimeAccepted=False,fabricatedRuntimeAuthority=False),indent=2)+'\n',encoding='utf8');print('PASS_PURE_HOST_NATIVE_IDENTITY_CLOSURE_ONLY',len(checks))
