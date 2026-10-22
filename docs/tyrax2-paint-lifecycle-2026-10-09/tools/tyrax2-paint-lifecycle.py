"""Private A08 accepted paint basis / EE representation lifecycle experiment."""
import json
import shutil
import subprocess
import sys
from pathlib import Path
repo=Path(__file__).resolve().parents[1]
out,source=[Path(p).resolve() for p in sys.argv[1:3]]
subprocess.run([sys.executable,str(repo/'tools/tyrax2-vu-paint.py'),str(out),str(source),'--roads-day'],check=True)
def edit(path,old,new):
    data=path.read_text(encoding='utf-8-sig')
    assert data.count(old)==1,(path,old,data.count(old))
    path.write_bytes(data.replace(old,new).encode())
engine=out/'tyra/engine'
shutil.copy2(repo/'tools/tyrax2-paint-lifecycle.hpp',engine/'inc/debug/paint_lifecycle.hpp')
hdr=out/'game/inc/terrain_game.hpp'
edit(hdr,'#include <tyra>','#include <tyra>\n#include "debug/paint_lifecycle.hpp"')
edit(hdr,'    bool envPaintValid = false;','    bool envPaintValid = false;\n    PaintLife::State paintLife;\n    unsigned paintMapVersion = 0;')
core=engine/'inc/renderer/3d/pipeline/static/core/stapip_core.hpp'
edit(core,'  void setVU1Clipping(const bool& enabled);','''  void setVU1Clipping(const bool& enabled);
  bool supportsPaintVu(const StaPipBag* bag) const {
    return qbufferRenderer.isVU1ClippingEnabled() && !qbufferRenderer.hasProgramOverrides() &&
      bag && bag->texture && bag->texture->coordinatesAreNormals && bag->color &&
      bag->color->many && !bag->color->single && !bag->lighting && !bag->billboard &&
      bag->info && bag->info->transformationType == TyraMVP;
  }''')
p=out/'game/src/gen/game_physics.gen.cpp'
edit(p,'part.envTexBag->paintVu = MotionProbe::variant() == 6;',
     'part.envTexBag->paintVu = PaintLife::mode != 0 && stapip.core.supportsPaintVu(part.envBag.get());')
edit(p,'      bool paintChanged = !part.envPaintValid ||','''      const unsigned normalStamp = part.envTexBag->contentVersion ? *part.envTexBag->contentVersion : 0;
      bool paintChanged = part.paintLife.changed(part.envTexBag->coordinates,part.envBag->count,normalStamp) || !part.envPaintValid ||''')
edit(p,'''        if(part.envTexBag->paintVu) {
          part.envTexBag->paintForward = fwdL;
          part.envTexBag->paintHalf = hL;
        } else {''','''        part.paintLife.accept(part.envTexBag->coordinates,part.envBag->count,normalStamp,&fwdL.x,&hL.x);
        part.envTexBag->paintForward = fwdL;
        part.envTexBag->paintHalf = hL;
      }
      const bool refreshEe = part.paintLife.needsEe(part.envTexBag->paintVu);
      if(refreshEe) {
        fwdL = part.envTexBag->paintForward;
        hL = part.envTexBag->paintHalf;''')
edit(p,'''        } // private EE paint alternative
      }
    }
    if(vehiclePaintFor(objectIndex))''','''        part.paintLife.eeReady = true;
      }
      TYRA_ASSERT(!memcmp(&part.envTexBag->paintForward.x,part.paintLife.forward,12) &&
                  !memcmp(&part.envTexBag->paintHalf.x,part.paintLife.half,12),
                  "Private paint accepted basis mismatch");
      PaintLife::check(part.paintLife,part.envTexBag->paintVu,paintChanged,refreshEe,
          part.shownLod,part.envTexBag->coordinates,part.envColorBag->many,part.envBag->count);
      TYRA_ASSERT(!PaintLife::errors,"Private paint lifecycle oracle mismatch");
    }
    if(vehiclePaintFor(objectIndex))''')
# A same-pointer normal edit must rebuild the distinct-normal map too.
edit(p,'        if (part.paintMapSrc != nrm || part.paintMapCount != n) {',
     '        if (part.paintMapVersion != normalStamp || part.paintMapSrc != nrm || part.paintMapCount != n) {\n          part.paintMapVersion = normalStamp;')
edit(p,'      tier = vehicleLodTier(i, tier);','''      tier = vehicleLodTier(i, tier);
      if(vehiclePaintFor(i)) tier = (MotionProbe::offset / 240) % 3;''')
game=out/'game/src/terrain_game.cpp'
edit(game,'void TerrainGame::loop() {','''void TerrainGame::loop() {
  const unsigned paintMode=(MotionProbe::offset/30)%6;
  static unsigned oldPaintMode=~0U;
  if(paintMode!=oldPaintMode) {
    engine->renderer.core.synchronizeFrame();
    static Tyra::StaPipCullTCEVU1Program paintOverride;
    stapip.core.setProgramOverride(Tyra::StaPipCullTextureEnv,nullptr);
    stapip.core.setVU1Clipping(paintMode!=2);
    if(paintMode==4) stapip.core.setProgramOverride(Tyra::StaPipCullTextureEnv,&paintOverride);
    oldPaintMode=paintMode;
  }
  PaintLife::mode=paintMode;''')
# The override is the built-in image installed through the real override API.
edit(game,'#include "game_runtime.gen.hpp"','#include "game_runtime.gen.hpp"\n#include "renderer/3d/pipeline/static/core/programs/cull/stapip_cull_tce_vu1_program.hpp"')
probe=engine/'inc/debug/motion_probe.hpp'
edit(probe,'#include <stdio.h>','#include <stdio.h>\n#include "debug/paint_lifecycle.hpp"')
edit(probe,'    printf("LOG: DRIVEPHASE', '    PaintLife::report(phase);\n    printf("LOG: DRIVEPHASE')
(out/'paint-lifecycle-manifest.json').write_text(json.dumps(dict(
    experiment='A08 private lifecycle correctness, not performance',
    modes=['EE','VU','legacy fallback','VU','custom override fallback','VU'],
    modeFrames=30,lodFrames=240,phaseFrames=2040,
    oracle='every submitted EE paint colour vs accepted basis',
    limitations=['physical hardware blocked','custom shader is identical built-in image','not full raster comparison']),indent=2)+'\n')
print(out)
