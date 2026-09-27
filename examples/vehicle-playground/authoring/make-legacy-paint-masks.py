"""Curated paint masks for the three imported Motor District cars.

The authored Ravager/Pica/Strix masks come from their Blender paint functions.
These older GLBs have no procedural atlas source, so this script reads their
embedded image and selects the body pigment while protecting wheels, glass,
lamps and neutral trim. Re-run after changing one of the source GLBs and inspect
the masks before committing them; their thresholds belong to THESE atlases.
Requires Pillow. Run from anywhere: python make-legacy-paint-masks.py.
"""

import io
import json
import struct
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "res" / "textures"


def embedded_image(path):
    data = path.read_bytes()
    jlen = struct.unpack_from("<I", data, 12)[0]
    gltf = json.loads(data[20:20 + jlen])
    binary = 20 + jlen + 8
    view = gltf["bufferViews"][gltf["images"][0]["bufferView"]]
    start = binary + view.get("byteOffset", 0)
    return Image.open(io.BytesIO(data[start:start + view["byteLength"]])).convert("RGB")


def cc96(x, y, r, g, b):
    # Mustard body, excluding grey wheel art and blue/black glass. The lamp
    # lenses are separate source materials and do not use these orange texels.
    return r > 65 and g > 30 and r > g * 1.18 and b < g * 0.60


def rally(x, y, r, g, b):
    # The wheel occupies the atlas's upper-left 56x55 block. Its tan hub must
    # keep the source colour even though it matches the body pigment.
    return not (x < 56 and y < 56) and r > 70 and g > 48 and \
        r > g * 1.12 and g > b * 1.12


def tristar(x, y, r, g, b):
    # The black-backed wheel image is the large right-hand square. The body
    # uses rainbow strips outside it; neutral highlights stay untinted.
    if x >= 128 and 48 <= y < 209:
        return False
    span = max(r, g, b) - min(r, g, b)
    return max(r, g, b) > 55 and span > 28


for name, model, select in (
    ("cc96", ROOT / "res/models/cc96-strip-study/cc96-strip-study.glb", cc96),
    ("rally", ROOT / "res/models/ggbot-rally.glb", rally),
    ("tristar", ROOT / "res/models/tristar-efficient.glb", tristar),
):
    image = embedded_image(model)
    mask = Image.new("L", image.size)
    mask.putdata([255 if select(x, y, *image.getpixel((x, y))) else 0
                  for y in range(image.height) for x in range(image.width)])
    OUT.mkdir(parents=True, exist_ok=True)
    dest = OUT / f"{name}-paint-mask.png"
    mask.save(dest)
    print(f"{name}: {sum(v != 0 for v in mask.getdata())} paint texels -> {dest}")
