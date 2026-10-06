"""Pure identity closure shared by root release and runtime preflight."""
def verify_native_binding(release, proof, provenance, manifest_sha, elf_sha,
                          symbol_sha, proof_sha):
 assert release['nativeSha256']==proof_sha,'released native proof hash drift'
 assert proof['status'].startswith('PASS_')and not proof['blockers'],'native audit rejected'
 assert provenance['build_backend']=='native'and provenance['build_exit_code']==0,'native build rejected'
 assert provenance['verified_source_files']==proof['sourceFiles']==501,'native source count'
 assert proof['sourceManifestSha256']==provenance['target_source_manifest_sha256']==manifest_sha,'native source identity'
 assert proof['actualELFSha256']==provenance['selected_elf_sha256']==elf_sha,'native ELF identity'
 assert proof['actualSymbolSha256']==provenance['selected_symbol_sha256']==symbol_sha,'native symbol identity'
