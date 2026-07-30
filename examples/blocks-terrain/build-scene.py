#!/usr/bin/env python3
"""Authors the blocks-terrain scene into the .tyra + objects/ files.

Kept next to the example because a graph this size is unpleasant to build by
hand in the node editor and impossible to review as JSON: this script IS the
readable form of what the volume does. Re-run it after `tyrax-editor --new` to
rebuild the example from scratch; the editor treats the result as an ordinary
project from then on.
"""
import json
import os

HERE = os.path.dirname(os.path.abspath(__file__))
TYRA = os.path.join(HERE, "blocks-terrain.tyra")

BLOCK = 1.5        # world size of one cube - deliberately SHORTER than
                   # the 1.8 player, so a single-block step never fills
                   # the screen and the walker can climb it in a stride
LEVELS = 14        # tallest column, in blocks (the collision field packs a
                   # column into one 32-bit word, so 32 is the ceiling)
BASE_Y = 0.0

def node(nid, ntype, x, y, nums=None, strs=None, rows=None):
    n = {"id": nid, "type": ntype, "pos": [x, y]}
    if nums:
        n["nums"] = nums
    if strs:
        n["strs"] = strs
    if rows:
        n["rows"] = [{"s": r[0], "v": list(r[1])} for r in rows]
    return n


def link(lid, a, apin, b, bpin):
    return {"id": lid, "from": a, "fromPin": apin, "to": b, "toPin": bpin}


# --- the graph -------------------------------------------------------------
# Blocks Fill makes the landscape and emits only the blocks that have a visible
# face; each one carries `depth` (0 = the top of its column, 1 = the one under
# it, ...) and `height` (its world Y). Five Filter by Attribute branches then
# decide WHICH block it is, and two Merge Points put them back together:
#
#   depth 0 + low          -> sand        depth 1 -> dirt
#   depth 0 + high         -> snow        depth 2 -> stone
#   depth 0 + in between   -> grass
#
# Nothing about that chain is block-specific: it is the ordinary filter/pick
# vocabulary reading two attributes one source node happened to write.
nodes = [
    node(1, "BlocksFill", 40, 360, nums={
        "block": BLOCK, "levels": LEVELS, "floor": 3, "scale": 26.0,
        "octaves": 4, "relief": 1.0, "depth": 3, "base": BASE_Y}),

    # --- the surface (depth 0), split three ways by height ---
    node(10, "FilterRange", 320, 40, nums={"min": 0.0, "max": 0.0, "falloff": 0.0},
         strs={"attr": "depth"}),
    node(11, "FilterRange", 560, 40, nums={"min": 18.0, "max": 999.0, "falloff": 1.5},
         strs={"attr": "height"}),
    node(12, "PickAsset", 820, 40,
         rows=[("res/models/block-snow.obj", (100, 1, 1, 0))]),

    node(20, "FilterRange", 320, 180, nums={"min": 0.0, "max": 0.0, "falloff": 0.0},
         strs={"attr": "depth"}),
    node(21, "FilterRange", 560, 180, nums={"min": -99.0, "max": 6.0, "falloff": 1.5},
         strs={"attr": "height"}),
    node(22, "PickAsset", 820, 180,
         rows=[("res/models/block-sand.obj", (100, 1, 1, 0))]),

    node(30, "FilterRange", 320, 320, nums={"min": 0.0, "max": 0.0, "falloff": 0.0},
         strs={"attr": "depth"}),
    node(31, "FilterRange", 560, 320, nums={"min": 6.0, "max": 18.0, "falloff": 0.0},
         strs={"attr": "height"}),
    node(32, "PickAsset", 820, 320,
         rows=[("res/models/block-grass.obj", (100, 1, 1, 0))]),

    # --- and what is under it ---
    node(40, "FilterRange", 320, 460, nums={"min": 1.0, "max": 1.0, "falloff": 0.0},
         strs={"attr": "depth"}),
    node(41, "PickAsset", 820, 460,
         rows=[("res/models/block-dirt.obj", (100, 1, 1, 0))]),

    node(45, "FilterRange", 320, 600, nums={"min": 2.0, "max": 99.0, "falloff": 0.0},
         strs={"attr": "depth"}),
    node(46, "PickAsset", 820, 600,
         rows=[("res/models/block-stone.obj", (100, 1, 1, 0))]),

    node(50, "Merge", 1080, 200),
    node(51, "Merge", 1080, 480),
    node(55, "Merge", 1300, 340),
    node(60, "Output", 1560, 340, nums={
        # A chunk is 24 units = 12 blocks across: big enough that the merge is
        # worth it, small enough that the far half of the map culls away.
        "cell": 24.0, "draw": 0.0, "shadow": 0, "collide": 0, "detail": 0,
        "budget": 90000, "maxinst": 12000}),
]
links = [
    link(100, 1, 0, 10, 0), link(101, 10, 0, 11, 0), link(102, 11, 0, 12, 0),
    link(110, 1, 0, 20, 0), link(111, 20, 0, 21, 0), link(112, 21, 0, 22, 0),
    link(120, 1, 0, 30, 0), link(121, 30, 0, 31, 0), link(122, 31, 0, 32, 0),
    link(130, 1, 0, 40, 0), link(131, 40, 0, 41, 0),
    link(140, 1, 0, 45, 0), link(141, 45, 0, 46, 0),
    link(150, 12, 0, 50, 0), link(151, 22, 0, 50, 1), link(152, 32, 0, 50, 2),
    link(160, 41, 0, 51, 0), link(161, 46, 0, 51, 1),
    link(170, 50, 0, 55, 0), link(171, 51, 0, 55, 1),
    link(180, 55, 0, 60, 0),
]

volume = {
    "id": "b10c5v01000000a1",
    "name": "world",
    "type": "scatter",
    "position": [0, 8, 0],
    "rotation": [0, 0, 0],
    # The lattice covers the whole map; Y only has to contain the tallest
    # column, and nothing clips against it in a runtime volume.
    "scale": [66, 44, 66],
    "color": [0.4, 0.7, 1.0],
    "physics": False,
    "castShadow": False,
    "collision": "none",
    "procGraph": {
        "seed": 7,
        "nextId": 200,
        "runtime": True,
        "runAtStart": True,
        "seedMode": 0,
        "nodes": nodes,
        "links": links,
    },
}

# --- the regenerate button -------------------------------------------------
# TRIANGLE rebuilds the world with a fresh seed. A raw On Button rather than an
# input action on purpose: this is a demo control, not something a player should
# be able to rebind onto their jump key. It is also where a runtime volume's
# randomness genuinely comes from - the console clock at the moment the player
# pressed the button.
regen = {
    "id": "b10c5r36656e00a2",
    "name": "regenerator",
    "type": "empty",
    "position": [0, 1, 0],
    "rotation": [0, 0, 0],
    "scale": [1, 1, 1],
    "color": [0.9, 0.6, 0.2],
    "physics": False,
    "flowGraph": {
        "nextId": 10,
        "nodes": [
            {"id": 1, "type": "OnButton", "pos": [40, 60], "str": "Triangle"},
            {"id": 2, "type": "GenerateVolume", "pos": [320, 60],
             "str": "world", "num": [-1, 0, 0, 0]},
        ],
        "links": [{"id": 5, "from": 1, "to": 2}],
    },
}


def player_id():
    """The scaffolded Player object's id - `--new` picks a fresh one every time,
    so it is looked up rather than pinned."""
    for name in sorted(os.listdir(os.path.join(HERE, "objects"))):
        if not name.endswith(".json"):
            continue
        body = open(os.path.join(HERE, "objects", name), encoding="utf-8").read()
        if '"type": "player"' in body:
            return name[:-5]
    raise SystemExit("no Player object in objects/ - run tyrax-editor --new first")


def write_object(obj):
    path = os.path.join(HERE, "objects", obj["id"] + ".json")
    with open(path, "w", encoding="utf-8") as f:
        json.dump(obj, f, indent=1)
        f.write("\n")


def main():
    write_object(volume)
    write_object(regen)
    pid = player_id()
    # The manifest is edited as JSON rather than by text substitution - the
    # editor rewrites the file in its own layout on the first save anyway.
    manifest = json.load(open(TYRA, encoding="utf-8"))
    manifest["scenes"][0]["objects"] = [pid, volume["id"], regen["id"]]
    with open(TYRA, "w", encoding="utf-8") as f:
        json.dump(manifest, f, indent=2)
        f.write("\n")
    # The player starts above the tallest column and drops onto the world.
    ppath = os.path.join(HERE, "objects", pid + ".json")
    p = open(ppath, encoding="utf-8").read()
    p = p.replace('"position": [0, 0, 0]', '"position": [0, 40, 0]', 1)
    open(ppath, "w", encoding="utf-8").write(p)
    print("scene written")


if __name__ == "__main__":
    main()
