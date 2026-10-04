from pathlib import Path
import hashlib,json,subprocess
lab=Path('/mnt/f/Projects/tyrax2-lab-20261001');baseline=lab/'night-ablation-physical-v2';proposal=lab/'pool-batch-color-split-proposal-v1';out=lab/'pool-batch-color-split-target-compile-v1';assert not out.exists();out.mkdir()
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
manifest=baseline/'target-source-manifest.json'
for rel,digest in json.loads(manifest.read_text())['files'].items():assert sha(baseline/rel)==digest,rel
tool=Path('/home/spider/.cache/tyrax/native/toolchains/eff2c8918589264d49aa9a0f');compiler=tool/'ee/bin/mips64r5900el-ps2-elf-g++';source=proposal/'source/game/src/gen/game_lighting.gen.cpp'
args=[str(compiler),'-g','-D_EE','-G0','-Wall','-O3']
for include in (baseline/'game/inc',baseline/'tyra/engine/inc',tool/'ps2sdk/ee/include',tool/'ps2sdk/common/include',tool/'ps2sdk/ports/include'):args+=['-I',str(include)]
args+=['-MMD','-MP','-MF',str(out/'candidate.d'),'-c','-o',str(out/'candidate.o'),str(source)]
r=subprocess.run(args,cwd=baseline/'game',stdout=subprocess.PIPE,stderr=subprocess.STDOUT);(out/'compile.log').write_bytes(r.stdout)
record=dict(status='PASS_ACTUAL_R5900_TRANSLATION_UNIT_ONLY' if r.returncode==0 else 'FAIL_ACTUAL_R5900_TRANSLATION_UNIT',exitCode=r.returncode,command=args,sourceSha256=sha(source),baselineManifestSha256=sha(manifest),compilerSha256=sha(compiler),hostProofSha256=sha(proposal/'host-proof.json'),sourceProofSha256=sha(proposal/'source-proof.json'),compileLogSha256=sha(out/'compile.log'),objectSha256=sha(out/'candidate.o')if (out/'candidate.o').exists()else None,linkedElfAccepted=False,deviceRuntimeAccepted=False,colorOnlyActivationMeasured=False,performanceAccepted=False)
(out/'proof.json').write_text(json.dumps(record,indent=2)+'\n');print(record['status'],sha(out/'proof.json'));assert r.returncode==0
