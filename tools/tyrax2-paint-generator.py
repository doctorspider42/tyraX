"""A09 whole-image selection with generator-owned paint and fog readbacks."""
import hashlib
import json
import re
import subprocess
import sys
from pathlib import Path

repo = Path(__file__).resolve().parents[1]
out, source, generated = [Path(p).resolve() for p in sys.argv[1:4]]
subprocess.run([sys.executable, str(repo/'tools/tyrax2-paint-raster.py'),
                str(out), str(source)], check=True)

def edit(path, old, new):
    data = path.read_text(encoding='utf-8-sig')
    assert data.count(old) == 1, (path, old, data.count(old))
    path.write_bytes(data.replace(old, new).encode())

core = out/'tyra/engine/src/renderer/3d/pipeline/static/core'
images = ['cull/stapip_cull_tce_vu1', 'cull/stapip_cull_tce_envn_vu1',
          'clip/stapip_clip_tc_vu1', 'clip/stapip_clip_tc_fold_vu1',
          'clip/stapip_clip_tce_vu1']
symbols = {}
inputs = {}
for stem in images:
    base = ('cull/stapip_cull_tce_vu1' if stem.startswith('cull/') else
            'clip/stapip_clip_tce_vu1' if stem.endswith('_tce_vu1') else
            'clip/stapip_clip_tc_vu1')
    src = generated/(base+'_paint.vclpp')
    text = src.read_text()
    inputs[str(src)] = hashlib.sha256(src.read_bytes()).hexdigest()
    # Both bodies use the same input ABI. Preserve existing FOLD/ENVN bodies;
    # only the generator-owned prepass is transplanted into separate images.
    match = re.search(r'^    ilw\.y\s+paintMode,.*?^paintDone:\n', text, re.M|re.S)
    assert match, src
    original = (core/'programs'/(stem+'.vclpp')).read_text()
    old = re.search(r'\n    ; Private paint prepass\..*?^paintDone:\n', original, re.M|re.S)
    assert old, stem
    image = original.replace(old[0], '\n'+match[0])
    name = re.search(r'^\.name (\w+)$', image, re.M)[1]
    symbols[name] = name+'_PAINT'
    image = image.replace('.name '+name, '.name '+name+'_PAINT')
    image = re.sub(r'^(#vuprog \w+)$', r'\1Paint', image, flags=re.M)
    (core/'programs'/(stem+'_paint.vclpp')).write_bytes(image.encode())

for stem in ['cull/stapip_cull_tce_vu1', 'clip/stapip_clip_tc_vu1',
             'clip/stapip_clip_td_vu1', 'clip/stapip_clip_tce_vu1']:
    path = core/'programs'/(stem+'_program.cpp')
    data = path.read_text()
    changed = 0
    for old, new in symbols.items():
        for suffix in ['_CodeStart', '_CodeEnd']:
            changed += data.count(old+suffix)
            data = data.replace(old+suffix, new+suffix)
    assert changed == 8, (stem, changed)
    path.write_bytes(data.encode())

header = out/'game/inc/paint_raster.hpp'
edit(header, 'const unsigned group = step / 3, shape = group / 3, basis = group % 3;',
     '''const unsigned group = (step / 3) % 9, shape = group / 3, basis = group % 3;
    const bool fog = step >= 27;
    info.fogDisabled = !fog;
    const float near = engine->renderer.core.getSettings().getNear();
    if(fog) engine->renderer.core.setFog(Tyra::Color(70,95,120),near,near*24.0F);
    else engine->renderer.core.disableFog();
    printf("PAINTFOG step=%u enabled=%u scale=%f offset=%f\\n",step,unsigned(fog),
           engine->renderer.core.fog.scale,engine->renderer.core.fog.offset);''')
edit(header, 'step/9,(step/3)%3,paintRasterCull', '(step/9)%3,(step/3)%3,paintRasterCull')
edit(header, 'if (step >= 27)', 'if (step >= 54)')
edit(header, 'if (step < 27)', 'if (step < 54)')
edit(header, 'PAINTRASTERDONE cases=27', 'PAINTRASTERDONE cases=54')
manifest = json.loads((out/'paint-raster-manifest.json').read_text())
manifest.update(cases=54, experiment='A09 generated paint prepass, separate whole images',
                fogModes=['disabled','enabled'], generatorInputs=inputs,
                fogRangeNearMultiples=[1,24],
                images=symbols, wrappersShareClipImage=['TC','TD','TCE'],
                limitations=['retains handwritten FOLD/ENVN bodies',
                             'synthetic geometry, not game night/lifecycle',
                             'no physical qualification or performance claim'])
manifest['harnessSha256'] = hashlib.sha256(header.read_bytes()).hexdigest()
(out/'paint-raster-manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
print(out)
