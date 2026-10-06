from pathlib import Path
import json,sys,copy,hashlib
b=Path('F:/Projects/tyrax2-lab-20261001');h=b/'assert-gate-root-helpers-v1';sys.path.insert(0,str(h));from native_binding import verify_native_binding
# Pure symbolic hash controls. These are NOT source/native/runtime evidence.
r={'nativeSha256':'proof'};p={'status':'PASS_HOST_SYMBOLIC','blockers':[],'sourceFiles':501,'sourceManifestSha256':'manifest','actualELFSha256':'elf','actualSymbolSha256':'symbol'};n={'build_backend':'native','build_exit_code':0,'verified_source_files':501,'target_source_manifest_sha256':'manifest','selected_elf_sha256':'elf','selected_symbol_sha256':'symbol'}
verify_native_binding(r,p,n,'manifest','elf','symbol','proof');results=['positive_symbolic_identity_closure']
for which,key in [('release','nativeSha256'),('proof','sourceManifestSha256'),('proof','actualELFSha256'),('proof','actualSymbolSha256'),('provenance','target_source_manifest_sha256'),('provenance','selected_elf_sha256'),('provenance','selected_symbol_sha256')]:
 rr,pp,nn=copy.deepcopy((r,p,n));{'release':rr,'proof':pp,'provenance':nn}[which][key]='changed'
 try:verify_native_binding(rr,pp,nn,'manifest','elf','symbol','proof')
 except AssertionError:results.append('rejected_'+which+'_'+key)
 else:raise AssertionError('mutation admitted '+key)
for i,label in enumerate(('current_manifest','actual_ELF','actual_symbol','actual_proof_hash')):
 args=['manifest','elf','symbol','proof'];args[i]='changed'
 try:verify_native_binding(r,p,n,*args)
 except AssertionError:results.append('rejected_'+label)
 else:raise AssertionError('mutation admitted '+label)
out=b/'assert-gate-native-binding-host-controls-v1.json';assert not out.exists()
out.write_bytes((json.dumps(dict(status='PASS_PURE_HOST_NATIVE_IDENTITY_CLOSURE_MUTATIONS',results=results,actualNativeBuildAccepted=False,physicalRuntimeAccepted=False,symbolicFixturesOnly=True,verifierSha256=hashlib.sha256((h/'native_binding.py').read_bytes()).hexdigest()),indent=2)+'\n').encode());print('PASS_PURE_HOST_NATIVE_IDENTITY_CLOSURE_MUTATIONS',len(results))
