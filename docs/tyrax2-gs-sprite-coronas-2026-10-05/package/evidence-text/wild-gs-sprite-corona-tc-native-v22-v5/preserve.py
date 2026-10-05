"""Read-only native V5 observation; writes only this unique artifact archive."""
from pathlib import Path
import hashlib,json,shutil,sys
ROOT=Path(__file__).resolve().parent;LAB=ROOT.parent
AUDIT=LAB/'wild-gs-sprite-corona-native-prep-v1/audit-native.py'
F=LAB/'wild-gs-sprite-corona-physical-v1'
assert not (ROOT/'proof.json').exists(),'immutable NEW archive'
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
code=AUDIT.read_text();prefix=code.split('# V11 compiler artifacts are bound directly')[0]
# Derive mirror/dependencies/linked images/budgets from the reviewed actual auditor.
# Stop before its intentional resident-with-billboards assertion and preserve rejection.
sys.argv=[str(AUDIT),'--fixture',str(F),'--abi',str(ROOT/'not-executed-abi'),'--out',str(ROOT/'not-created-audit')]
scope={'__file__':str(AUDIT),'__name__':'archive_observation'}
exec(compile(prefix,str(AUDIT),'exec'),scope)
tc=scope['tc'];budgets=scope['budgets'];programs=scope['programs'];image=scope['image'];pins=scope['pins'];E=scope['E']
assert budgets['VU1Clip']['withBillboardsWords']==2046 and budgets['EEClip']['withBillboardsWords']==1846
assert programs['StaPipVU1Cull_TC_CodeStart']['bytes']==4096
rows={}
def copy(p,name):
 assert p.is_file();h=sha(p);q=ROOT/name;assert not q.exists();q.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(p,q);assert sha(p)==sha(q)==h;rows[name]={'originalPath':str(p),'sha256':h,'bytes':p.stat().st_size}
for p in sorted(tc.parent.glob(tc.name+'*')):copy(p,'artifacts/'+p.name)
rel='engine/src/renderer/3d/pipeline/static/core/programs/cull/stapip_cull_tc_vu1.vclpp'
copy(F/'tyra'/rel,'source/stapip_cull_tc_vu1.vclpp')
copy(E/rel,'source/mirror-stapip_cull_tc_vu1.vclpp')
assert rows['source/stapip_cull_tc_vu1.vclpp']['sha256']==rows['source/mirror-stapip_cull_tc_vu1.vclpp']['sha256']
for rel in ['engine/src/renderer/3d/pipeline/shared/tyra_macros.i','engine/inc/renderer/3d/pipeline/static/core/programs/stapip_vu1_shared_defines.h']:
 copy(F/'tyra'/rel,'source/'+Path(rel).name)
for name in ['target-source-manifest.json','root-source-freeze.json','root-native-command-exit.json','root-native-provenance.json']:copy(F/name,'authority/'+name)
copy(scope['log'],'authority/native-build.log')
p=ROOT/'artifacts/linked-tc-image.bin';assert not p.exists();p.write_bytes(image);rows['artifacts/linked-tc-image.bin']={'originalPath':'actual object section range verified identical to linked ELF symbol range','sha256':hashlib.sha256(image).hexdigest(),'bytes':len(image)}
report=dict(status='REJECT_ACTUAL_NATIVE_V5_RESIDENT_BUDGET_WITH_BILLBOARDS',blockers=['VU1Clip with unchanged billboards requires2046 words, exceeding draw-finish limit2042 by4. No eviction or execution authorized.'],sourceManifestSha256=sha(F/'target-source-manifest.json'),sourceFiles=500,mirroredInputs=491,actualELFSha256=sha(scope['elfp']),actualSymbolSha256=sha(scope['symp']),actualTC=programs['StaPipVU1Cull_TC_CodeStart'],residentBudgets=budgets,linkedTCEqualsActualAssembledObject=True,archive=rows,observedInputs=pins,helperSha256=sha(Path(__file__)),parentAuditorSha256=sha(AUDIT),compilerExecuted=False,cacheMutations=False,devicesExecuted=False,nativeTargetAccepted=False,limits=['Actual compiler/link success does not override resident budget rejection.','Main-only occupancy fitting is insufficient; unchanged billboard residency is mandatory.','Scheduled register/MAC/delay semantic review documented separately; no runtime acceptance.'])
(ROOT/'proof.json').write_bytes((json.dumps(report,indent=2)+'\n').encode());print(report['status']);print(report['actualTC']);print(budgets)
