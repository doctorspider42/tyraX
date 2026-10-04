"""Rebuild the grove layout in a project already scaffolded with --new.

Usage: python examples/impostor-grove/generate_scene.py PROJECT_DIRECTORY
Requires Pillow for the small procedural material textures. No downloaded assets.
"""
import hashlib
import json
import math
from pathlib import Path
import random
import sys
from PIL import Image

root = Path(sys.argv[1]).resolve()
manifest = root / "impostor-grove.tyra"
data = json.loads(manifest.read_text(encoding="utf-8"))
rng = random.Random(20260909)
settings = data["settings"]
settings.update(textureQuant="8bit", textureAtlas=False, modelAo=False,
                terrainDetail=32, terrainViewDistance=90, terrainLodDistance=35,
                meshLodDistance=0, aoEnabled=False, aoStrength=.4, aoRadius=2.2, giEnabled=False,
                keyboardMouse=False, keyboardMousePs2Link=False,
                showFps=False, showMemory=False, showProfiler=False,
                skyColor=[.72, .80, .82], skyTopColor=[.24, .43, .64],
                lightDir=[-.45, .72, -.38], ambient=.55, diffuse=.45,
                lightColor=[1, .91, .72], brightness=1,
                fogEnabled=True, fogColor=[.65, .73, .72], fogStart=48, fogEnd=105,
                terrainMaterial="res/materials/forest-floor.mtl")
# Ambience presets override project settings, so keep the authored preset in sync.
data["ambience"] = [dict(name="Morning grove", **{k: settings[k] for k in
    ("skyColor", "skyTopColor", "skyDome", "zenithSize", "lightDir", "ambient",
     "diffuse", "lightColor", "brightness", "aoEnabled", "aoStrength", "aoRadius",
     "fogEnabled", "fogColor", "fogStart", "fogEnd")})]
data["defaultAmbience"] = 0
scene = data["scenes"][0]
scene["name"] = "main"
scene["terrain"] = dict(width=128, depth=128)
scene["objects"] = []
objects = root / "objects"
objects.mkdir(exist_ok=True)
# Only remove previously referenced scene records in this explicit fixture.
old_ids = {i for sc in json.loads(manifest.read_text(encoding="utf-8"))["scenes"]
           for i in sc["objects"]}
for ident in old_ids:
    path = objects / (ident + ".json")
    if path.parent.resolve() == objects.resolve() and path.exists():
        path.unlink()

def add(name, kind, pos, scale=(1, 1, 1), color=(1, 1, 1), **fields):
    ident = hashlib.sha256(name.encode()).hexdigest()[:16]
    obj = dict(id=ident, name=name, type=kind, position=list(pos),
               rotation=[0, 0, 0], scale=list(scale), color=list(color), physics=False)
    obj.update(fields)
    scene["objects"].append(ident)
    (objects / (ident + ".json")).write_text(json.dumps(obj, indent=2)+"\n", encoding="utf-8")

add("Grove entrance", "player", (0, 0, -22), rotation=[-2, 0, 0],
    player=dict(mode="walk", walkSpeed=.09, lookSpeed=.8, eyeHeight=1.8,
                jumpSpeed=4.5, canJump=True))
add("Entrance view", "camera", (0, 2.0, -22), rotation=[-2, 0, 0])
names = ["silver-birch", "old-oak", "young-oak"]
# Jittered woodland with a clear winding avenue and an open ruin court.
count = 0
for z in range(-42, 57, 9):
    for x in range(-48, 49, 9):
        xx, zz = x+rng.uniform(-2.5, 2.5), z+rng.uniform(-2.5, 2.5)
        pathx = 3*math.sin(zz*.085)
        if abs(xx-pathx) < 5 or (xx*xx+(zz-15)**2 < 105):
            continue
        name = names[rng.randrange(3)]
        size = rng.uniform(.82, 1.15)
        add(f"Canopy {count:03}", "model", (round(xx, 3), 0, round(zz, 3)),
            (size, size, size), (.94, .98, .88),
            rotation=[0, rng.randrange(360), 0],
            model=f"res/models/trees/{name}.obj",
            impostor=f"res/models/trees/{name}-impostor.obj",
            impostorBillboard=True, impostorViews=8, impostorDistance=30, drawDistance=105, collision="none",
            castShadow=False, bakedLighting=False)
        count += 1
for i in range(28):
    z = -28+i*2.5
    x = 3*math.sin(z*.085)
    add(f"Old paving {i:02}", "box", (x, .025, z),
        (2.8+rng.uniform(-.3, .3), .07, 2.25), (.58, .56, .43),
        rotation=[0, rng.uniform(-8,8), 0], material="res/materials/stone.mtl")
for i in range(30):
    z = rng.uniform(-28, 48)
    x = 3*math.sin(z*.085) + rng.choice([-1,1])*rng.uniform(3.6, 6.0)
    s = rng.uniform(.65, 1.25)
    add(f"Undergrowth {i:02}", "model", (x, 0, z), (s,s,s), (.82,.95,.76),
        rotation=[0,rng.randrange(360),0], model="res/models/trees/fern-bush.obj",
        impostor="res/models/trees/fern-bush-impostor.obj", impostorBillboard=True, impostorViews=4, impostorDistance=16,
        drawDistance=52, collision="none", castShadow=False, bakedLighting=False)
# Broken gate and low ruined walls frame the end of the avenue.
for i, x in enumerate([-3.6,3.6]):
    add(f"Gate plinth {i}", "box", (x,.3,15), (2,.6,2), (.64,.61,.49), material="res/materials/stone.mtl")
    add(f"Gate pillar {i}", "box", (x,2.2,15), (1.2,3.3,1.2), (.72,.69,.56), material="res/materials/stone.mtl")
    add(f"Gate capital {i}", "box", (x,3.9,15), (1.8,.35,1.6), (.66,.64,.51), material="res/materials/stone.mtl")
add("Broken lintel", "box", (0,4.3,15), (8.7,.55,1.5), (.69,.65,.51), material="res/materials/stone.mtl")
for i in range(9):
    x = -9 if i < 5 else 9
    z = 9+(i%5)*3
    height = rng.uniform(.5,1.7)
    add(f"Courtyard wall {i}", "box", (x,height/2,z), (1.1,height,2.7),
        (.56,.57,.43), material="res/materials/stone.mtl")
for i in range(18):
    x,z = rng.uniform(-11,11),rng.uniform(5,25)
    if abs(x) < 3: x += 5
    add(f"Fallen stone {i}", "box", (x,.2,z),
        (rng.uniform(.4,1.1),.4,rng.uniform(.5,1.2)), (.49,.53,.39),
        rotation=[0,rng.randrange(180),8], material="res/materials/stone.mtl")

materials = root/"res/materials"
materials.mkdir(parents=True, exist_ok=True)
for name, base, repeat in [("forest-floor", (67,78,39),.5), ("stone", (163,157,132),1)]:
    image = Image.new("RGB", (128,128))
    pix = image.load()
    for y in range(128):
        for x in range(128):
            n = rng.uniform(-12,12) + 6*math.sin(x*.31)*math.sin(y*.27)
            if name == "stone" and (y%32 < 2 or (x+(y//32%2)*32)%64 < 2): n -= 24
            pix[x,y] = tuple(max(0,min(255,int(c+n))) for c in base)
    image.save(materials/(name+".png"))
    (materials/(name+".mtl")).write_text(
        f"newmtl {name}\nKd 1 1 1\nmap_Kd -s {repeat} {repeat} 1 {name}.png\n", encoding="utf-8")

# A non-tree, two-material OBJ demonstrates the universal capture path.
model_dir = root / "res/models"
model_dir.mkdir(parents=True, exist_ok=True)
marker_vertices, marker_faces = [], []
def marker_box(cx, cy, cz, sx, sy, sz, material):
    base = len(marker_vertices) + 1
    corners = [(-1,-1,-1),(1,-1,-1),(1,1,-1),(-1,1,-1),
               (-1,-1,1),(1,-1,1),(1,1,1),(-1,1,1)]
    marker_vertices.extend((cx+x*sx/2, cy+y*sy/2, cz+z*sz/2) for x,y,z in corners)
    marker_faces.append("usemtl " + material)
    for face in [(0,3,2,1),(4,5,6,7),(0,4,7,3),(1,2,6,5),(3,7,6,2),(0,1,5,4)]:
        marker_faces.append("f " + " ".join(f"{base+k}/{uv+1}" for uv,k in enumerate(face)))
marker_box(0,.3,0,2,.6,1.6,"stone")
marker_box(0,1.9,0,.7,2.6,.7,"stone")
marker_box(.55,3.2,0,2.3,.35,.4,"copper")
(model_dir / "waystone.obj").write_text("mtllib waystone.mtl\n" +
    "\n".join("v %g %g %g" % v for v in marker_vertices) + "\n" +
    "vt 0 0\nvt 1 0\nvt 1 1\nvt 0 1\n" + "\n".join(marker_faces) + "\n", encoding="utf-8")
(model_dir / "waystone.mtl").write_text(
    "newmtl stone\nKd 0.8 0.85 0.75\nmap_Kd ../materials/stone.png\n"
    "newmtl copper\nKd 0.8 0.8 0.7\nmap_Kd trees/old-oak-bark.png\n", encoding="utf-8")
add("Waystone", "model", (-5,0,3), rotation=[0,25,0],
    model="res/models/waystone.obj", impostor="res/models/impostors/waystone.obj",
    impostorBillboard=True, impostorViews=16, impostorDistance=18, collision="none",
    castShadow=False, bakedLighting=False)

manifest.write_text(json.dumps(data,indent=2)+"\n",encoding="utf-8")
print(f"Grove: {count} canopy trees, 30 shrubs, {len(scene['objects'])} objects")
