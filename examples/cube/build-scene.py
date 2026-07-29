#!/usr/bin/env python3
"""Authors the cube example: prefab rooms scattered on a 3D lattice at runtime.

Four room prefabs (identical geometry, different colours, one of them with a
turning block inside) and one runtime Procedural volume that stamps them onto a
3 x 3 x 3 grid. Kept as a script because 24 wall slabs times four variants is
not something anyone should read as JSON - this file IS the readable form.

Re-run it after `tyrax-editor --new` to rebuild the example from scratch.
"""
import json
import os

HERE = os.path.dirname(os.path.abspath(__file__))
TYRA = os.path.join(HERE, "cube.tyra")

ROOM = 14.0        # outer size of one room, world units
WALL = 0.5         # slab thickness
HOLE = 5.0         # width of every doorway / hatch
DOOR = 6.0         # doorway height (the walls' opening reaches the floor)
LEVELS = 3         # rooms per axis

# --- one room ---------------------------------------------------------------
# A hollow cube with a hatch through the middle of all six faces. Each face is
# four slabs around the hole; that is 24 boxes, and because every one of them is
# plain static geometry they merge into a SINGLE draw call per room-coloured
# chunk (see docs/prefabs.md).
H = ROOM / 2.0
Q = (ROOM - HOLE) / 2.0        # width of a side slab
SIDE = Q / 2.0 + HOLE / 2.0    # its centre offset from the face centre


def box(name, pos, scale, color, collide="box"):
    return {
        "name": name,
        "type": "box",
        "position": [round(v, 3) for v in pos],
        "rotation": [0, 0, 0],
        "scale": [round(v, 3) for v in scale],
        "color": color,
        "physics": False,
        "collision": collide,
        "castShadow": False,
        "detail": 1,
    }


def face(axis, sign, color, tag, door=False):
    """One wall/floor/ceiling of the room.

    A vertical wall is three slabs around a DOORWAY that reaches the floor - a
    hatch centred at mid-height would be authentic to the film and unreachable
    by a 1.8-unit player. Floors and ceilings keep the centred square hatch:
    those you look through, not walk through.
    """
    out = []
    a, b = [i for i in range(3) if i != axis]
    if door:
        parts = [(-SIDE, 0.0, Q, ROOM),            # left of the doorway
                 (SIDE, 0.0, Q, ROOM),             # right of it
                 (0.0, (DOOR + ROOM) / 2.0 - H, HOLE, ROOM - DOOR)]  # lintel
    else:
        parts = [(-SIDE, 0.0, Q, ROOM),
                 (SIDE, 0.0, Q, ROOM),
                 (0.0, -SIDE, HOLE, Q),
                 (0.0, SIDE, HOLE, Q)]
    for k, (da, db, sa, sb) in enumerate(parts):
        pos = [0.0, 0.0, 0.0]
        scale = [0.0, 0.0, 0.0]
        # Inset by half a thickness: two neighbouring rooms then have two
        # TOUCHING slabs instead of two coincident ones, which is the
        # difference between a wall and a z-fighting stripe.
        pos[axis] = sign * (H - WALL * 0.5)
        scale[axis] = WALL
        pos[a] = da
        scale[a] = sa
        pos[b] = db
        scale[b] = sb
        # The prefab origin sits on the room's FLOOR, so everything lifts by H.
        pos[1] += H
        out.append(box("%s%d" % (tag, k), pos, scale, color))
    return out


def room_prefab(pid, name, color, accent=None):
    # Floors and ceilings a shade darker than the walls: six faces of one flat
    # colour read as a single blob at this lighting, and the room has to look
    # like a room from the inside.
    deck = [round(c * 0.72, 3) for c in color]
    objs = []
    objs += face(1, -1, deck, name + "-floor")
    objs += face(1, 1, deck, name + "-ceil")
    objs += face(0, -1, color, name + "-wx", door=True)
    objs += face(0, 1, color, name + "-px", door=True)
    objs += face(2, -1, color, name + "-wz", door=True)
    objs += face(2, 1, color, name + "-pz", door=True)
    if accent:
        # The one member that is NOT merged: it carries a flow graph, so it
        # needs an identity of its own and takes a clone-pool slot. That split
        # is the whole runtime story of a prefab - see the Prefabs window.
        spin = box(name + "-core", [0.0, 2.6, 0.0], [2.4, 2.4, 2.4], accent,
                   collide="box")
        spin["flowGraph"] = {
            "nextId": 10,
            "nodes": [
                {"id": 1, "type": "OnStart", "pos": [40, 60], "str": ""},
                {"id": 2, "type": "SpinObject", "pos": [300, 60], "str": "",
                 "num": [18, 26, 0, 0]},
            ],
            "links": [{"id": 5, "from": 1, "to": 2}],
        }
        objs.append(spin)
    return {"id": pid, "name": name,
            "notes": "One room of the cube: 20 merged slabs, a doorway through "
                     "every wall and a hatch through the floor and ceiling.",
            "objects": objs}


PREFABS = [
    room_prefab("cube9700000000a1", "room-steel", [0.62, 0.66, 0.72]),
    room_prefab("cube9700000000a2", "room-amber", [0.72, 0.55, 0.22]),
    room_prefab("cube9700000000a3", "room-jade", [0.30, 0.58, 0.45]),
    room_prefab("cube9700000000a4", "room-red", [0.62, 0.24, 0.24],
                accent=[0.95, 0.85, 0.35]),
]

# --- the volume -------------------------------------------------------------
# Scatter on Grid with Levels = 3 is a full 3D lattice; Pick Prefab turns each
# cell into one of the four rooms. Nothing else is needed - no randomness beyond
# which room lands where, which is exactly the film's premise.
volume = {
    "id": "cube9700000000b1",
    "name": "the-cube",
    "type": "scatter",
    "position": [0, 0, 0],
    "rotation": [0, 0, 0],
    "scale": [LEVELS * ROOM, 4, LEVELS * ROOM],
    "color": [0.9, 0.5, 0.5],
    "physics": False,
    "castShadow": False,
    "collision": "none",
    "procGraph": {
        "seed": 3,
        "nextId": 100,
        "runtime": True,
        "runAtStart": True,
        "seedMode": 0,
        "nodes": [
            {"id": 1, "type": "ScatterGrid", "pos": [40, 200],
             "nums": {"spacing": ROOM, "jitter": 0.0, "snap": 0,
                      "levels": LEVELS, "levelstep": ROOM}},
            {"id": 2, "type": "PickPrefab", "pos": [400, 200],
             "rows": [{"s": p["name"], "v": [w, 1, 1, 0]}
                      for p, w in zip(PREFABS, (34, 26, 26, 14))]},
            {"id": 3, "type": "Output", "pos": [760, 200],
             "nums": {"cell": 128.0, "draw": 0.0, "shadow": 0, "collide": 0,
                      "detail": 0, "budget": 40000, "maxinst": 256}},
        ],
        "links": [
            {"id": 10, "from": 1, "fromPin": 0, "to": 2, "toPin": 0},
            {"id": 11, "from": 2, "fromPin": 0, "to": 3, "toPin": 0},
        ],
    },
}

# Reshuffle the whole cube on TRIANGLE: same rooms, different arrangement.
regen = {
    "id": "cube9700000000b2",
    "name": "shuffler",
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
             "str": "the-cube", "num": [-1, 0, 0, 0]},
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
    with open(os.path.join(HERE, "objects", obj["id"] + ".json"), "w",
              encoding="utf-8") as f:
        json.dump(obj, f, indent=1)
        f.write("\n")


def main():
    write_object(volume)
    write_object(regen)
    # The manifest is edited as JSON rather than by text substitution: the
    # prefab library is a whole section and splicing it in by regex is how this
    # script produced a malformed .tyra twice. The editor rewrites the file in
    # its own layout on the first save, so the reformat costs nothing.
    manifest = json.load(open(TYRA, encoding="utf-8"))
    pid = player_id()
    manifest["scenes"][0]["objects"] = [pid, volume["id"], regen["id"]]
    manifest["prefabs"] = PREFABS
    # Black sky, no dome, in the AMBIENCE preset - that is the layer that
    # actually drives a scene's sky and light (a per-scene override is only
    # consulted when the preset does not). The cube's outer shell is open
    # (every room is the same prefab, so the edge ones have doorways to
    # nowhere) and a blue sky through them would give the game away.
    amb = manifest["ambience"][0]
    amb["skyColor"] = [0.02, 0.02, 0.03]
    amb["skyTopColor"] = [0.0, 0.0, 0.01]
    amb["skyDome"] = False
    amb["ambient"] = 0.40
    amb["diffuse"] = 0.60
    with open(TYRA, "w", encoding="utf-8") as f:
        json.dump(manifest, f, indent=2)
        f.write("\n")
    # The player starts inside the middle room of the middle level.
    ppath = os.path.join(HERE, "objects", pid + ".json")
    p = open(ppath, encoding="utf-8").read()
    p = p.replace('"position": [0, 0, 0]', '"position": [0, %.1f, 0]' % ROOM, 1)
    open(ppath, "w", encoding="utf-8").write(p)
    print("scene written")


if __name__ == "__main__":
    main()
