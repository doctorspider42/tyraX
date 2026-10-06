from pathlib import Path
lab=Path('F:/Projects/tyrax2-lab-20261001')
copies={
 'night-main11-batch-build-native-v1.ps1':'night-main11-batch-v2-build-native-v1.ps1',
 'freeze-night-main11-batch-root-v1.py':'freeze-night-main11-batch-v2-root-v1.py',
 'compile-night-main11-batch-abi-root-v1.py':'compile-night-main11-batch-v2-abi-root-v1.py',
 'audit-night-main11-batch-native-root-v1.py':'audit-night-main11-batch-v2-native-root-v1.py',
 'release-main11-native-emulator-root-v2.py':'release-main11-v2-native-emulator-root-v1.py',
}
for old,new in copies.items():
 p=lab/new
 assert not p.exists(),p
 s=(lab/old).read_text().replace('night-main11-batch-physical-v1','night-main11-batch-physical-v2').replace("a.family+'-physical-v1'","a.family+'-physical-v2'")
 s=s.replace('night-ablation-native-v55','night-ablation-native-v56')
 if old.startswith('release-'):
  for name in ['root-helpers','native-root-review','controls','source-controls','independent-review']:
   s=s.replace('night-main11-batch-'+name+'-v1','night-main11-batch-'+name+'-v2')
  s=s.replace("len(read(source)['checks'])==33","len(read(source)['checks'])==36").replace("len(read(control)['checks'])==52","len(read(control)['checks'])==66")
  s=s.replace("lab/'night-main11-batch-independent-review-v2/parser-repair-review.md'","lab/'night-main11-batch-independent-review-v2/source-pins.json'")
  s=s.replace('33 source controls, 52 parser controls','36 source controls, 66 parser controls').replace('Native V55','Native V56')
  s=s.replace('Initial carrier preparation','Deferred frame600 carrier preparation')
 p.write_bytes(s.encode())
 print(new)
