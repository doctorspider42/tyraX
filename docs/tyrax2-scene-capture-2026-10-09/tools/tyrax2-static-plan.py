"""Private same-geometry descriptor/classification intervention, not production."""
import importlib.util
import json
import subprocess
import sys
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
out=Path(sys.argv[1]).resolve()
verify='--verify' in sys.argv[2:]
subprocess.run([sys.executable,str(ROOT/'tools/tyrax2-motion-probe.py'),str(out),
                '--geometry-sweep','--production-defaults']+(['--verify-partial','--verify-expansion'] if verify else []),check=True)
engine=out/'tyra/engine'
game=out/'game'
def edit(path,old,new,count=1):
    text=path.read_text(encoding='utf-8-sig')
    assert text.count(old)==count,(path,old,text.count(old))
    path.write_bytes(text.replace(old,new).encode())

(engine/'inc/debug/motion_probe.hpp').write_bytes((ROOT/'tools/tyrax2-static-plan.hpp').read_bytes())
(engine/'inc/debug/static_plan.hpp').write_bytes(
    ('#define STATIC_PLAN_VERIFY '+str(int(verify))+'\n').encode()+
    (ROOT/'tools/tyrax2-static-plan-cache.hpp').read_bytes())
p=game/'src/terrain_game.cpp'
t=p.read_text()
start=t.index('  // Private repeatable angular sweep')
end=t.index('  engine->renderer.beginFrame(CameraInfo3D',start)
t=t[:start]+'''  const unsigned view=MotionProbe::view();
  cameraPosition.set(0.0F, 2.2F, view==0 ? -74.3035F : -20.0F);
  cameraLookAt.set(view==2 ? 35.0F : 0.0F, 2.2F, view==0 ? -40.0F : 15.0F);
  scriptCtx.hudSuppressed=true;
  if(MotionProbe::offset==250 && MotionProbe::phase<MotionProbe::phaseCount){
    printf("STATICVIEW phase=%u eye=%f,%f,%f look=%f,%f,%f hour=%f paused=%u\\n",MotionProbe::phase,cameraPosition.x,cameraPosition.y,cameraPosition.z,cameraLookAt.x,cameraLookAt.y,cameraLookAt.z,daynight::g_hour,unsigned(daynight::g_paused));fflush(stdout);
  }
  MotionProbe::renderBegin();
'''+t[end:]
start=t.index('  // Correctness-only lifecycle continuation')
end=t.index('  if (scriptCtx.requestScene >= 0)',start)
t=t[:start]+t[end:]
p.write_bytes(t.encode())
p=game/'src/gen/game_collision.gen.cpp'
edit(p,'    submitHeavy(b.bag.get());','    MotionProbe::registerOwner(b.bag.get());\n    submitHeavy(b.bag.get());')
if '#include "debug/motion_probe.hpp"' not in p.read_text():p.write_bytes(b'#include "debug/motion_probe.hpp"\n'+p.read_bytes())
p=engine/'src/renderer/3d/pipeline/static/core/stapip_core.cpp'
edit(p,'  MotionProbe::add(MotionProbe::Bags);','  MotionProbe::CoreScope staticScope(bag);\n  MotionProbe::add(MotionProbe::Bags);')

base='renderer/3d/pipeline/static/core/bag/packaging/stapip_bag_packager'
p=engine/('inc/'+base+'.hpp')
t=p.read_text()
needle='  CoreBBoxFrustum checkFrustum('
declarations='''  StaPipBagPackage* createOriginal(u16*, StaPipBag*, u16, StaPipBagPackage*);
  StaPipBagPackage* createOriginal(u16*, const StaPipBagPackage&, u16, StaPipBagPackage*);

'''
assert t.count(needle)==1
p.write_bytes(t.replace(needle,declarations+needle).encode())
p=engine/('src/'+base+'.cpp')
t='#include "debug/static_plan.hpp"\n'+p.read_text()
t=t.replace('StaPipBagPackager::create(', 'StaPipBagPackager::createOriginal(')
assert t.count('u16 size) {')==2
t=t.replace('u16 size) {','u16 size, StaPipBagPackage* prepared) {')
for pool in ('bagPackagesPool','splitPackagesPool'):
    t=t.replace(f'if ({pool}.size() <',f'if (!prepared && {pool}.size() <')
    t=t.replace(f'StaPipBagPackage* result = {pool}.data();',f'StaPipBagPackage* result = prepared ? prepared : {pool}.data();')
for data in ('data','pkg.bag'):
    begin=t.index('    result[i].bag = '+data+';')
    end=t.index('    result[i].clipPlaneMask = 0;',begin)
    t=t[:begin]+'    if (!prepared) {\n'+t[begin:end]+'    }\n'+t[end:]
wrappers='''
StaPipBagPackage* StaPipBagPackager::create(u16* count, StaPipBag* bag, u16 size) {
  StaticPlan::Key key{bag,bag->vertices,bag->texture?bag->texture->coordinates:nullptr,
    bag->color->many,bag->lighting?bag->lighting->normals:nullptr,
    bag->count,size,maxVertCount,bag->bboxVersion,0};
  return StaticPlan::run(count,key,objectSpacePlanes,clipObjectSpacePlanes,capturePlaneMasks,
    [&](StaPipBagPackage* ready){return createOriginal(count,bag,size,ready);});
}
StaPipBagPackage* StaPipBagPackager::create(u16* count,const StaPipBagPackage& pkg,u16 size) {
  StaticPlan::Key key{pkg.bag,pkg.vertices,pkg.sts,pkg.colors,pkg.normals,
    pkg.size,size,maxVertCount,pkg.bag->bboxVersion,pkg.indexOf1By3BBox};
  return StaticPlan::run(count,key,objectSpacePlanes,clipObjectSpacePlanes,capturePlaneMasks,
    [&](StaPipBagPackage* ready){return createOriginal(count,pkg,size,ready);});
}
'''
t=t.replace('}  // namespace Tyra',wrappers+'\n}  // namespace Tyra')
p.write_bytes(t.encode())
spec=importlib.util.spec_from_file_location('quiet',ROOT/'tools/tyrax2-quiet-fixture.py')
quiet=importlib.util.module_from_spec(spec);spec.loader.exec_module(quiet)
(out/'source-manifest.json').write_text(json.dumps({'experiment':'static-plan','oracle':verify,
    'files':quiet.source_inputs(out,'vehicle-playground.tyra')},indent=2)+'\n')
print(out)
