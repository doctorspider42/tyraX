from pathlib import Path
import hashlib,json,os
r=Path('/mnt/f/Projects/tyrax2-lab-20261001/ps2link-reset-breadcrumbs-v1')
assert os.name=='posix'
h=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
m=json.loads((r/'source-manifest.json').read_text());n=json.loads((r/'native-proof.json').read_text())
assert h(r/'source-manifest.json')==n['sourceManifestSha256']
for name,digest in m['files'].items():assert h(r/'source'/name)==digest,name
for name,pin in n['artifacts'].items():assert h(r/name)==pin['sha256'],name
for name,digest in n['actualObjectsFinalHigh'].items():assert h(r/'source'/name)==digest,name
for name,digest in n['logs'].items():assert h(r/name)==digest,name
for name,pin in n['linkMaps'].items():assert h(r/name)==pin['sha256'],name
for name,digest in n['toolchain'].items():assert h(Path(name))==digest,name
assert n['gsBackgroundStoreBytes']==8 and n['actualGSStoreInstruction']=='sd a1,224(v0)'
actual=(r/'actual-resetDiagEarly-final.txt').read_text()
assert 'sd\ta1,224(v0)' in actual
reset=(r/'source/ee/cmdHandler.c').read_text()
body=reset.split('void pkoReset(void)\n{',1)[1]
assert body.index('ResetEE(')<body.index('init_scr();')<body.index('ExecPS2(')
assert '\n    printf(' not in body
main=(r/'source/ee/ps2link.c').read_text()
assert '*(volatile u64 *)0x120000E0' in main and 'volatile unsigned int resetDiagStage = 1;' in main
proof=dict(status='ROOT_REVIEWED_PRIVATE_RESETDIAG_NATIVE_ONLY',sourceManifestSha256=h(r/'source-manifest.json'),
           nativeProofSha256=h(r/'native-proof.json'),sourceFilesVerified=len(m['files']),
           actualObjectsMapsLogsArtifactsAndToolchainVerified=True,actualGS64BitStoreVerified=True,
           originalResetOrderingReviewed=True,noEEStdioBeforeSafeScreenReviewed=True,
           userReportedResidentVersion='r7',hardwareExecuted=False,resetFixed=False,
           rootReviewedScope='Private diagnostics only. Early BGCOLOR can remain hidden; ledger may be unreachable during EE hang. Initial low-packed boot and reset-to-fresh-game qualification still required. No hot resident replacement authorized by this review.')
(r/'root-review.json').write_bytes((json.dumps(proof,indent=2)+'\n').encode())
print('PASS root resetdiag:',len(m['files']),'source pins, three artifacts, actual objects/toolchain, sd and reset ordering')
