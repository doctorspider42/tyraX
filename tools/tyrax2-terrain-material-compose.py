"""Compose the district's static daylight ground shadows into chunk textures.

This deliberately measures a quality/VRAM trade. It preserves the source ground
tile frequency only at 2048 pixels per chunk, which exceeds one GS texture.
"""
import argparse
import hashlib
import json
from pathlib import Path

import numpy as np
from PIL import Image


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("bin", type=Path)
    parser.add_argument("out", type=Path)
    parser.add_argument("--resolution", type=int, default=128, choices=(128, 256, 512, 1024, 2048))
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=False)
    base_path = args.bin / "textures/district-ground.png"
    base = np.asarray(Image.open(base_path).convert("RGB"), dtype=np.float32)
    assert base.shape == (64, 64, 3), "Fixture-specific material input"
    resolution = args.resolution
    # Each 64-world-unit chunk repeats its source tile 32 times. Pixel centres
    # follow the repeating texture; composition is at the target page frequency.
    coords = (np.arange(resolution, dtype=np.float32) + .5) * (32 * 64 / resolution) - .5
    lo = np.floor(coords).astype(np.int32)
    blend = coords - lo
    # Bilinear input resampling (an approximation of GS filtering/quantisation).
    row0 = base[(lo % 64)[:, None], (lo % 64)[None, :]]
    row1 = base[(lo % 64)[:, None], ((lo + 1) % 64)[None, :]]
    row2 = base[((lo + 1) % 64)[:, None], (lo % 64)[None, :]]
    row3 = base[((lo + 1) % 64)[:, None], ((lo + 1) % 64)[None, :]]
    x = blend[None, :, None]
    y = blend[:, None, None]
    tiled = (row0 * (1-x) + row1*x)*(1-y) + (row2*(1-x)+row3*x)*y
    sources = {"textures/district-ground.png": hashlib.sha256(base_path.read_bytes()).hexdigest()}
    pages = []
    for mask_path in sorted((args.bin / "gshadow").glob("s0_*.png")):
        parts = mask_path.stem.split("_")
        assert len(parts) == 3
        mask_image = Image.open(mask_path).convert("RGBA").resize((resolution, resolution), Image.Resampling.BILINEAR)
        mask = np.asarray(mask_image, dtype=np.float32)
        # Source PNG alpha is converted by the loader to GS's 0..128 domain.
        # A static shadow is Cs*a + Cd*(1-a). Baking before directional vertex
        # modulation is generally different when Cs is nonzero; report this.
        alpha = mask[:, :, 3:4] / 255.0
        composed = np.clip(np.rint(mask[:, :, :3]*alpha+tiled*(1-alpha)), 0, 255).astype(np.uint8)
        target = args.out / (mask_path.stem + ".png")
        image = Image.fromarray(composed).quantize(colors=256, method=Image.Quantize.MEDIANCUT)
        image.save(target)
        sources[str(mask_path.relative_to(args.bin))] = hashlib.sha256(mask_path.read_bytes()).hexdigest()
        pages.append(dict(cx=int(parts[1]), cz=int(parts[2]), file=target.name,
                          sha256=hashlib.sha256(target.read_bytes()).hexdigest(),
                          textureBytes=resolution*resolution+1024))
    manifest = dict(version=1, sourceSha256=sources, resolution=resolution, pages=pages,
                    sourceRepeatsPerChunk=32, sourceTileSize=64, detailPreservingResolution=2048,
                    residentTextureBytes=sum(page["textureBytes"] for page in pages),
                    sourceTexelsPerOutputTexel=2048/resolution,
                    qualityEquivalent=False,
                    limits=["Static daylight only: dynamic sun/shadow policy is not represented.",
                            "Bilinear filtering, palette quantisation and pass rounding change the result.",
                            "Baked shadow colour is modulated by vertex lighting; original shadow colour is not.",
                            "2048px requires texture subdivision for the GS; this tool alone does not implement it."])
    (args.out / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    print(json.dumps({k:v for k,v in manifest.items() if k not in ("pages","sourceSha256")}, indent=2))


if __name__ == "__main__":
    main()
