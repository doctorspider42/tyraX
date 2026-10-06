from pathlib import Path
import argparse
p=argparse.ArgumentParser();p.add_argument('--family',choices=('companion-census','assert-gate','tex1-owned'),required=True);p.add_argument('--version',type=int,required=True);a=p.parse_args()
lab=Path('F:/Projects/tyrax2-lab-20261001')
for old,new in [('object-route-build-native-v1.ps1',f'{a.family}-build-native-v1.ps1'),('compile-object-route-abi-root-v1.py',f'compile-{a.family}-abi-root-v1.py'),('audit-object-route-native-root-v1.py',f'audit-{a.family}-native-root-v1.py')]:
 dst=lab/new;assert not dst.exists(),dst
 s=(lab/old).read_text(encoding='utf8').replace('object-route',a.family).replace('night-ablation-native-v49',f'night-ablation-native-v{a.version}')
 dst.write_bytes(s.encode())
print('Prepared root-only native/ABI/mirror audit scripts',a.family,a.version)
