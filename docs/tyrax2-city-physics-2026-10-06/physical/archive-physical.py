from pathlib import Path
import shutil,json,hashlib,datetime
repo=Path('F:/Projects/tyra-editor');lab=Path('F:/Projects/tyrax2-lab-20261001');dest=repo/'docs/tyrax2-city-physics-2026-10-06';run=lab/'tyrax2-closeout-ps2-city-physics-v1';prep=lab/'tyrax2-city-physics-v2';sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
inventory=json.loads((dest/'inventory.json').read_text());assert len(inventory['files'])==42
for n,h in inventory['files'].items():assert sha(dest/n)==h,n
def copy(src,name):
 p=dest/name;assert not p.exists();p.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(src,p);inventory['files'][name]=sha(p)
for f in ['owned-launch.json','launch-inputs.json','strict-result.json']:copy(run/f,'physical/'+f)
for f in(run/'completed-prefix').iterdir():copy(f,'physical/completed-prefix/'+f.name)
copy(run/'bin/closeout.cfg','physical/closeout.cfg')
copy(prep/'freeze-physical.py','physical/freeze-physical.py');copy(prep/'archive-physical.py','physical/archive-physical.py')
result=json.loads((run/'strict-result.json').read_text());drive=result['pairedRaw']['0'];transitions=result['oneToMultiStepTransitions'];heavy=[v for v in drive if v['nonPacingMs']>16.68333]
reads=sum(2*v['timer']for values in result['pairedRaw'].values()for v in values);assert reads==512
record=dict(status='ROOT_ACCEPTED_BOUNDED_PHYSICAL_SUBSTEP_ATTRIBUTION',UTC=datetime.datetime.now(datetime.timezone.utc).isoformat(),sourceHead='9b5253c03',nativeELFSha256=sha(run/'bin/vehicle-playground.elf'),physicalResultSha256=sha(run/'strict-result.json'),actualTwoStepRuntimeQualified=True,drivingTwoStepLoops=21,drivingOneStepLoops=107,overBudgetDrivingLoops=len(heavy),oneToTwoTransitions=len(transitions),transitionsWithPrecedingMeasuredOverBudgetLoop=sum(v['previousWorkMs']>16.68333 for v in transitions),example=dict(priorFrame=917,priorSteps=1,priorWorkMs=23.610293918185764,priorVehicleScopeMs=0.5287543402777778,nextFrame=918,nextSteps=2,nextEntryMs=33.367838541666664),addedScopedCountReads=reads,scopeMeanOverBudgetMs=result['driveOverBudgetScopeMeanMs'],outsideScopeMeanOverBudgetMs=result['driveOverBudgetResidualMeanMs'],observerTaxResolved=False,observerContrastsMs=[result['parkedObserverCalibration'][str(p)]['netActiveObserverContrastMs']for p in (1,2)],causalExtraSubstepPrice=False,physicalTV='pending',terminalClientExitClaim=False,productionChange=False,scopeLimit='stepVehicles includes cache invalidation/updateVehicles/tutorial; excludes smoke/skids/debris, remaining logic and renderer. Residual contains existing waits/preemption and is not pure EE or proven renderer cost.')
p=dest/'physical/root-qualification.json';p.write_text(json.dumps(record,indent=2)+'\n');inventory['files']['physical/root-qualification.json']=sha(p)
(dest/'inventory.json').write_text(json.dumps(inventory,indent=2)+'\n');print('ARCHIVED_PHYSICAL',len(inventory['files']),'pins')
