"""Stage private A16 target rejection raster or full-scene drive fixtures."""
import argparse
import importlib.util
import json
import shutil
from pathlib import Path

repo=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('out',type=Path)
p.add_argument('source',type=Path,help='A10 disabled for raster; A13 forward for drive')
p.add_argument('--drive',action='store_true')
a=p.parse_args();out=a.out.resolve();source=a.source.resolve()
assert not out.exists(),'Refuse to overwrite evidence'
spec=importlib.util.spec_from_file_location('quiet',repo/'tools/tyrax2-quiet-fixture.py')
quiet=importlib.util.module_from_spec(spec);spec.loader.exec_module(quiet)
base=source/'tyra' if a.drive else repo/'vendor/tyra'
shutil.copytree(base/'engine',out/'tyra/engine',ignore=quiet.ignore)
shutil.copy2(base/'Makefile.base',out/'tyra/Makefile.base')
if a.drive:
    shutil.copytree(source/'game',out/'game',ignore=quiet.ignore)
    shutil.copy2(source/'authored-manifest.json',out/'authored-manifest.json')
    physics=out/'game/src/gen/game_physics.gen.cpp'
    data=physics.read_text(encoding='utf-8-sig')
    data=quiet.once(data,'MotionProbe::variant()==6 && stapip.core.supportsPaintVu(part.envBag.get())',
                    'false && stapip.core.supportsPaintVu(part.envBag.get())')
    physics.write_text(data)
else:
    (out/'game/src').mkdir(parents=True);(out/'game/inc').mkdir();(out/'game/.res-baked').mkdir()
    shutil.copy2(source/'game/Makefile',out/'game/Makefile')
    main=(source/'game/src/main.cpp').read_text(encoding='utf-8-sig')
    main=quiet.once(main,'#include "terrain_game.hpp"\n#include "paint_raster.hpp"','#include "target_audit_game.hpp"')
    main=quiet.once(main,'PaintRasterGame game(&engine);','TargetAuditGame game(&engine);')
    (out/'game/src/main.cpp').write_text(main)
    shutil.copy2(repo/'tools/tyrax2-target-audit.hpp',out/'game/inc/target_audit_game.hpp')
engine=out/'tyra/engine'
shutil.copy2(repo/'tools/tyrax2-target-reject.hpp',engine/'inc/debug/target_reject.hpp')
header='''#pragma once
#include "debug/target_reject.hpp"
namespace TargetAudit {
inline bool enabled=false;
inline float size=0;
inline unsigned tested=0,rejected=0;
inline bool active() { return enabled; }
}
'''
if a.drive:
    header=header.replace('#include "debug/target_reject.hpp"','#include "debug/target_reject.hpp"\n#include "debug/motion_probe.hpp"')
    header=header.replace('return enabled;','return MotionProbe::variant()==6;')
(engine/'inc/debug/target_audit.hpp').write_text(header)
view=engine/'src/renderer/core/3d/renderer_core_3d.cpp'
data=view.read_text(encoding='utf-8-sig')
data=quiet.once(data,'namespace Tyra {','#include "debug/target_audit.hpp"\nnamespace Tyra {')
data=quiet.once(data,'  projection = M4x4::perspective(envFov, size, size,','  TargetAudit::size = size;\n  projection = M4x4::perspective(envFov, size, size,')
data=quiet.once(data,'void RendererCore3D::pushPortalView(const Vec4& position, const Vec4& lookAt) {',
                'void RendererCore3D::pushPortalView(const Vec4& position, const Vec4& lookAt) {\n  TargetAudit::size = 0;')
data=quiet.once(data,'void RendererCore3D::popEnvView(const CameraInfo3D& cameraInfo) {',
                'void RendererCore3D::popEnvView(const CameraInfo3D& cameraInfo) {\n  TargetAudit::size = 0;')
view.write_text(data)
core=engine/'src/renderer/3d/pipeline/static/core/stapip_core.cpp'
data=core.read_text(encoding='utf-8-sig')
if a.drive:
    data=quiet.once(data,'  qbufferRenderer.sendObjectData(bag, &mvp, texBuffers);',
                    '  qbufferRenderer.setInfo(bag->info);\n  qbufferRenderer.sendObjectData(bag, &mvp, texBuffers);')
    data=quiet.once(data,'  qbufferRenderer.setClipperMVP(&mvp);\n\n  qbufferRenderer.setInfo(bag->info);',
                    '  qbufferRenderer.setClipperMVP(&mvp);')
data=quiet.once(data,'namespace Tyra {','#include "debug/target_audit.hpp"\nnamespace Tyra {')
hook='''  if (TargetAudit::size > 0 && TargetAudit::active() && bbox &&
      bag->info->transformationType == TyraMVP && !bag->billboard &&
      qbufferRenderer.isVU1ClippingEnabled() && !qbufferRenderer.hasProgramOverrides()) {
    ++TargetAudit::tested;
    const CoreBBox* bounds = bbox->getMainBBox();
    if (TargetReject::outside(mvp.data, &(*bounds)[0].x, &(*bounds)[7].x, TargetAudit::size)) {
      ++TargetAudit::rejected;
      recordOutsideBag(bag);
      return;
    }
  }
'''
data=quiet.once(data,'#if TYRA_STAPIP_GUARD_BAND_BAGS\n',hook+'\n#if TYRA_STAPIP_GUARD_BAND_BAGS\n')
core.write_text(data)
if a.drive:
    probe=engine/'inc/debug/motion_probe.hpp'
    data=probe.read_text(encoding='utf-8-sig')
    # Keep counters outside sampled frame exports and reset at each scene boundary.
    data=quiet.once(data,'namespace MotionProbe {','namespace TargetAudit { extern unsigned tested,rejected; }\nnamespace MotionProbe {')
    data=quiet.once(data,'    printf("LOG: DRIVEPHASE',
                    '    printf("TARGETCOUNTS phase=%u tested=%u rejected=%u\\n",phase,TargetAudit::tested,TargetAudit::rejected);\n    TargetAudit::tested=TargetAudit::rejected=0;\n    printf("LOG: DRIVEPHASE')
    probe.write_text(data)
manifest=dict(experiment='A16 private target OUTSIDE-only gate',drive=a.drive,
              filterOrder='fixed in all arms',paint='EE in all drive arms',
              order=['off','on','off'],guardBand='unchanged',source=str(source),
              limitations=['Not promoted','Physical raster and performance pending'])
(out/'target-audit-manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
print(out)
