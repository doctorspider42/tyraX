#!/usr/bin/env python3
"""Authors the texture-atlas example: a shelf of small props whose textures the
build packs into ONE shared GS page, with a measurable VRAM saving.

The point of the fixture is arithmetic, not decoration. A palettized texture is
charged by the GS for a width rounded up to 128 texels, so a 32x32 4-bit prop
texture holds 512 bytes of pixels and occupies **3.25 KB** of VRAM on its own -
six times its own size. Thirty of them cost 97.5 KB apart and 32.25 KB on one
256x256 4-bit page. That is the era's reason for atlasing, and this scene is
built to show it rather than to argue it.

Two authoring rules make the shared palette free here, and both are era-typical:
every prop is drawn from ONE 16-colour palette (so a 4-bit page can represent
all thirty exactly), and every texture is a flat-shaded 32x32 tile with a bold
glyph, so a wrong UV remap is instantly visible as the wrong glyph on a crate.

Re-run it after `tyrax-editor --new texture-atlas <dir> 40 40 fpp` to rebuild
the example from scratch. It only writes res/materials/props/, the object bodies and the
scene's object list; everything else is what --new produced.
"""
import json
import os
import struct
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
TYRA = os.path.join(HERE, "texture-atlas.tyra")
# res/materials, res/models and res/textures are the folders the texture
# bake processes - a material anywhere else is copied verbatim and never
# atlased (the atlas report says so per texture).
PROPS = os.path.join(HERE, "res", "materials", "props")

# --- the one palette every prop is drawn from -------------------------------
# 16 colours, so a 4-bit page (which shares ONE palette across every member)
# can hold all of them exactly. Authoring against a shared palette is what
# makes atlasing free at 4 bits; art with thirty independent palettes is what
# makes it lossy. Four material families plus ink, so the yard reads as a yard.
PALETTE = [
    (0x1a, 0x16, 0x12),  # 0  ink / outline
    (0x2e, 0x27, 0x1f),  # 1  deep shadow
    (0x5a, 0x3d, 0x24),  # 2  crate wood, dark
    (0x8a, 0x5d, 0x33),  # 3  crate wood
    (0xba, 0x84, 0x4c),  # 4  crate wood, light
    (0x3c, 0x44, 0x4a),  # 5  steel, dark
    (0x66, 0x71, 0x78),  # 6  steel
    (0x99, 0xa4, 0xab),  # 7  steel, light
    (0x27, 0x4a, 0x33),  # 8  crate paint, green
    (0x46, 0x7d, 0x4e),  # 9  crate paint, green light
    (0x6e, 0x27, 0x27),  # 10 crate paint, red
    (0xa8, 0x44, 0x38),  # 11 crate paint, red light
    (0x7a, 0x66, 0x1e),  # 12 hazard yellow, dark
    (0xc8, 0xa8, 0x33),  # 13 hazard yellow
    (0xd8, 0xd2, 0xc4),  # 14 paper / label
    (0xf2, 0xf0, 0xe8),  # 15 highlight
]

# family -> (body, shade, light, glyph ink)
FAMILIES = [
    ("wood", 3, 2, 4, 0),
    ("steel", 6, 5, 7, 0),
    ("green", 9, 8, 15, 0),
    ("red", 11, 10, 15, 0),
    ("hazard", 13, 12, 15, 0),
]

# 4x6 pixel glyphs, one per prop, so a mis-mapped UV shows up as the wrong
# character rather than as "slightly different brown".
GLYPHS = {
    "0": ["1111", "1001", "1001", "1001", "1001", "1111"],
    "1": ["0010", "0110", "0010", "0010", "0010", "0111"],
    "2": ["1111", "0001", "1111", "1000", "1000", "1111"],
    "3": ["1111", "0001", "0111", "0001", "0001", "1111"],
    "4": ["1001", "1001", "1111", "0001", "0001", "0001"],
    "5": ["1111", "1000", "1111", "0001", "0001", "1111"],
    "6": ["1111", "1000", "1111", "1001", "1001", "1111"],
    "7": ["1111", "0001", "0010", "0100", "0100", "0100"],
    "8": ["1111", "1001", "1111", "1001", "1001", "1111"],
    "9": ["1111", "1001", "1111", "0001", "0001", "1111"],
}


def write_png_p8(path, w, h, palette, indices):
    """A palettized PNG - the editor's bake reads the palette size to decide
    4 vs 8 bits, and a 16-entry one is what keeps these at 4."""
    plte = b"".join(bytes(c) for c in palette)
    raw = b"".join(b"\x00" + bytes(indices[y * w:(y + 1) * w]) for y in range(h))

    def chunk(tag, data):
        c = tag + data
        return struct.pack(">I", len(data)) + c + struct.pack(">I", zlib.crc32(c))

    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 3, 0, 0, 0))
    png += chunk(b"PLTE", plte)
    png += chunk(b"IDAT", zlib.compress(raw, 9))
    png += chunk(b"IEND", b"")
    with open(path, "wb") as f:
        f.write(png)


def make_prop(index):
    """One 32x32 prop face: a plank/panel body, a border, and its number."""
    name, body, shade, light, ink = FAMILIES[index % len(FAMILIES)]
    px = [body] * (32 * 32)

    def rect(x0, y0, x1, y1, c):
        for y in range(y0, y1):
            for x in range(x0, x1):
                px[y * 32 + x] = c

    # planks or panel lines, depending on the family
    if name in ("wood", "green", "red"):
        for y in range(0, 32, 8):
            rect(0, y, 32, y + 1, shade)
            rect(0, y + 7, 32, y + 8, shade)
    else:
        for x in range(0, 32, 8):
            rect(x, 0, x + 1, 32, shade)
    # a corner brace, and the outline
    rect(0, 0, 32, 1, ink)
    rect(0, 31, 32, 32, ink)
    rect(0, 0, 1, 32, ink)
    rect(31, 0, 32, 32, ink)
    rect(2, 2, 6, 6, light)
    rect(26, 26, 30, 30, light)

    # the number, two digits, centred on a label patch
    text = "%02d" % index
    rect(9, 11, 23, 21, 14)
    rect(9, 11, 23, 12, ink)
    rect(9, 20, 23, 21, ink)
    for d, ch in enumerate(text):
        gx = 11 + d * 6
        for gy, row in enumerate(GLYPHS[ch]):
            for gxx, bit in enumerate(row):
                if bit == "1":
                    px[(13 + gy) * 32 + gx + gxx] = ink
    # The box primitive samples V from the bottom, so a label drawn the obvious
    # way arrives UPSIDE DOWN on the crate - and an upside-down "5" is a
    # convincing "2", which is exactly the sort of thing this fixture exists to
    # make obvious rather than subtle. Flip the tile vertically.
    return [px[(31 - y) * 32 + x] for y in range(32) for x in range(32)]


PROP_COUNT = 30


def main():
    os.makedirs(PROPS, exist_ok=True)
    for i in range(PROP_COUNT):
        write_png_p8(os.path.join(PROPS, "prop-%02d.png" % i), 32, 32, PALETTE,
                     make_prop(i))
        with open(os.path.join(PROPS, "prop-%02d.mtl" % i), "w",
                  newline="\n") as f:
            f.write("newmtl prop-%02d\n" % i)
            f.write("Kd 1.0 1.0 1.0\n")
            f.write("map_Kd prop-%02d.png\n" % i)

    with open(TYRA, encoding="utf-8") as f:
        proj = json.load(f)

    # --- the yard: six rows of five crates, on a plinth, facing the player ---
    ids = []
    objdir = os.path.join(HERE, "objects")
    for old in os.listdir(objdir):
        if old.startswith("atlas"):
            os.remove(os.path.join(objdir, old))
    for i in range(PROP_COUNT):
        col, row = i % 5, i // 5
        oid = "a71a5000000000%02x" % (0x10 + i)
        obj = {
            "id": oid,
            "name": "prop-%02d" % i,
            "type": "box",
            "position": [round(-4.4 + col * 2.2, 3), round(1.0 + row * 1.3, 3),
                         round(-6.0 - (row % 2) * 0.15, 3)],
            "rotation": [0, 0, 0],
            "scale": [1.8, 1.1, 1.0],
            "color": [1, 1, 1],
            "physics": False,
            "material": "res/materials/props/prop-%02d.mtl" % i,
        }
        with open(os.path.join(objdir, oid + ".json"), "w",
                  newline="\n") as f:
            json.dump(obj, f, separators=(", ", ": "))
        ids.append(oid)

    # The player starts facing the yard: at yaw 0 an fpp camera looks along
    # +Z and the props are at -Z, so a fresh --new project would spawn you
    # looking at an empty field.
    for fn in os.listdir(objdir):
        path = os.path.join(objdir, fn)
        with open(path, encoding="utf-8") as f:
            body = json.load(f)
        if body.get("type") == "player":
            body["position"] = [0, 0, 2.5]
            body["rotation"] = [0, 180, 0]
            with open(path, "w", newline="\n") as f:
                json.dump(body, f, separators=(", ", ": "))

    scene = proj["scenes"][0]
    keep = [o for o in scene["objects"] if not o.startswith("a71a5")]
    scene["objects"] = keep + ids

    # The project's own settings: 4-bit textures (the era default, and what
    # makes the arithmetic interesting) with atlasing ON.
    proj["settings"]["textureQuant"] = "4bit"
    proj["settings"]["textureAtlas"] = True
    proj["settings"]["showMemory"] = True

    with open(TYRA, "w", encoding="utf-8", newline="\n") as f:
        json.dump(proj, f, indent=2)
    print("wrote %d props into %s" % (PROP_COUNT, PROPS))


if __name__ == "__main__":
    main()
