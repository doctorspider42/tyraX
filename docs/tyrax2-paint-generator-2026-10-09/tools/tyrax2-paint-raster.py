"""Generate isolated, target-assembled C16 EE/VU/EE GS readback cases."""
import hashlib
import json
import shutil
import subprocess
import sys
from pathlib import Path
from PIL import Image

repo = Path(__file__).resolve().parents[1]
out, source = [Path(p).resolve() for p in sys.argv[1:3]]
subprocess.run([sys.executable, str(repo/'tools/tyrax2-vu-paint.py'), str(out), str(source), '--roads-day'], check=True)

def edit(path, old, new):
    data = path.read_text(encoding='utf-8-sig')
    assert data.count(old) == 1, (path, old)
    path.write_bytes(data.replace(old,new).encode())

shutil.copy2(repo/'tools/tyrax2-paint-raster.hpp', out/'game/inc/paint_raster.hpp')
main = out/'game/src/main.cpp'
edit(main, '#include "terrain_game.hpp"', '#include "terrain_game.hpp"\n#include "paint_raster.hpp"')
edit(main, 'Vehicle_playground::TerrainGame game(&engine);', 'PaintRasterGame game(&engine);')
q = out/'tyra/engine/src/renderer/3d/pipeline/static/core/stapip_qbuffer_renderer.cpp'
edit(q, 'namespace Tyra {', 'extern unsigned paintRasterCull, paintRasterClip;\nnamespace Tyra {')
for name, counter in [('cull','Cull'),('clip','Clip')]:
    marker = f'void StaPipQBufferRenderer::{name}(StaPipQBuffer* buffer) {{'
    edit(q, marker, marker+f'\n  if(buffer->size) ++paintRaster{counter};')
im = Image.new('RGBA',(64,64))
im.putdata([(16+x*2,24+y*2,40+(x^y),128) for y in range(64) for x in range(64)])
im.save(out/'game/bin/paint-raster.png')
manifest = dict(experiment='A07 isolated target-assembled paint raster', cases=27,
                shapes=['inside','near-crossing','side-crossing'],bases=3,
                order=['EE','VU','EE'],candidateUploadedColors='deliberately incorrect',
                exclusions=['game lifecycle','fallback','performance','physical PS2 qualification'])
manifest['harnessSha256']=hashlib.sha256((out/'game/inc/paint_raster.hpp').read_bytes()).hexdigest()
manifest['textureSha256']=hashlib.sha256((out/'game/bin/paint-raster.png').read_bytes()).hexdigest()
(out/'paint-raster-manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
print(out)
