"""Builds the script-cookbook level into a fresh `--new script-cookbook ... fpp`
project: one station per area of the script API, each object configured only
through its scripts' public fields ("scriptProps"), plus the project data the
stations use (a sound, save values, a save text, a HUD text and bar, a World
Fact) and two small flow graphs that talk to the scripts.

    python authoring/build-scene.py <projectDir>

Re-running it replaces the cookbook objects and keeps the player.
"""
import json
import os
import re
import sys

root = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), "..")
tyra = [f for f in os.listdir(root) if f.endswith(".tyra")][0]
tpath = os.path.join(root, tyra)
objdir = os.path.join(root, "objects")

# Drop the cookbook objects of an earlier run (ids start with "c0b0").
for f in os.listdir(objdir):
    if f.startswith("c0b0"):
        os.remove(os.path.join(objdir, f))

objs = []


def add(name, typ, pos, scale=(1, 1, 1), color=(0.8, 0.8, 0.8), rot=(0, 0, 0), **extra):
    oid = "c0b0%012d" % (len(objs) + 1)
    o = {"id": oid, "name": name, "type": typ, "position": list(pos), "rotation": list(rot),
         "scale": list(scale), "color": list(color), "physics": False}
    o.update(extra)
    objs.append(o)
    return oid


def script(cls, **props):
    d = {"scripts": [cls]}
    if props:
        d["scriptProps"] = {cls: {k: str(v) for k, v in props.items()}}
    return d


def graph(nodes, links):
    return {"nextId": len(nodes) + 1,
            "nodes": [{"id": i + 1, "type": t, "pos": [i * 220, 0], "str": s, "num": [0, 0, 0, 0]}
                      for i, (t, s) in enumerate(nodes)],
            "links": [{"id": i + 1, "from": a, "to": b} for i, (a, b) in enumerate(links)]}


# --- Row A (z = 6): one sphere per easing curve ------------------------------
curves = ["Linear", "In", "Out", "Smooth", "InCubic", "OutCubic", "InOutCubic", "OutBack", "OutBounce"]
for i, c in enumerate(curves):
    add("a-ease-%s" % c.lower(), "sphere", (-8 + 2 * i, 0.4, 6), (0.6, 0.6, 0.6),
        (0.25 + 0.08 * i, 0.55, 1.0 - 0.07 * i), **script("EaseShowcase", curve=i, height="2", period="1.5"))

# --- Row B (z = 13): random, time, transforms, rays ------------------------------
add("b-dice", "box", (-9, 0.5, 13), color=(1, 1, 1), usable=True,
    **script("DiceRoller", luckyChance="0.25", rollCounter="rolls"))
add("b-blinker", "sphere", (-6, 1, 13), (0.8, 0.8, 0.8), (1, 0.8, 0.2), **script("Blinker", interval="0.6"))
add("b-cooldown-button", "cylinder", (-3, 0.25, 13), (1.2, 0.5, 1.2), (0.2, 0.9, 0.3), usable=True,
    **script("CooldownButton", cooldownSeconds="2"))
add("b-transform-tour", "box", (0, 0.5, 13), color=(0.4, 0.7, 1.0), **script("TransformTour", stepSeconds="1.2"))
dot = add("b-laser-dot", "sphere", (4, 0.5, 18), (0.25, 0.25, 0.25), (1, 0.2, 0.2), collision="none")
add("b-laser-turret", "cylinder", (4, 0.6, 13), (0.8, 1.2, 0.8), (0.5, 0.5, 0.55),
    **script("LaserTurret", laserDot=dot, range="30", sweep="50"))
for i, x in enumerate((2.5, 5.5)):
    add("b-target-%d" % (i + 1), "box", (x, 1, 19), (1.2, 2, 0.4), (0.6, 0.6, 0.7))
add("b-sentry", "cone", (8, 0.75, 13), (0.8, 1.5, 0.8), (1, 0.2, 0.2), **script("Sentry", watchRadius="15"))
add("b-sentry-wall", "box", (8, 1.25, 10.5), (3, 2.5, 0.4), (0.45, 0.45, 0.5))

# --- Row C (z = 22): searches, physics, spawning ---------------------------------
add("c-magnet", "sphere", (-7, 1.2, 22), (1, 1, 1), (0.8, 0.1, 0.1), **script("Magnet", radius="6", strength="6"))
for i, (dx, dz) in enumerate(((-3, -2), (2.5, -1.5), (-2, 3), (3, 2.5))):
    add("c-crate-%d" % (i + 1), "box", (-7 + dx, 0.5, 22 + dz), (0.7, 0.7, 0.7), (0.7, 0.5, 0.3),
        physics=True, physMass=1, physBounce=0.3, physFriction=0.5, physTumble=True, physSleep=2)
add("c-launch-pad", "box", (0, 0.05, 22), (4, 0.1, 4), (0.2, 0.6, 1.0),
    **script("LaunchPad", launchAction="jump", power="9", radius="2.5"))
for i, dx in enumerate((-1, 0, 1)):
    add("c-ball-%d" % (i + 1), "sphere", (dx, 0.4, 23), (0.6, 0.6, 0.6), (1, 0.9, 0.2),
        physics=True, physMass=0.5, physBounce=0.6, physFriction=0.2, physTumble=True, physSleep=3)
seed = add("c-seed", "box", (7, 0.3, 25), (0.4, 0.4, 0.4), (0.5, 1.0, 0.5))
add("c-spawner", "empty", (7, 0.5, 22), **script("SpawnDemo", templateObject=seed, interval="1.5", maxAlive="4"))

# --- Row D (z = 30): input, sound, save data, events + graphs --------------------
add("d-tilt-plank", "box", (-8, 1, 30), (3, 0.2, 1.5), (0.8, 0.6, 0.3),
    **script("StickTilt", maxTilt="25", boostAction="sprint"))
add("d-sound-box", "box", (-4, 0.5, 30), color=(0.3, 0.9, 0.9), usable=True,
    **script("SoundBox", sound="res/sfx/knock.wav", volume="90", priority="5"))
add("d-save-counter", "cylinder", (0, 0.4, 30), (1, 0.8, 1), (0.9, 0.9, 0.9), usable=True,
    **script("SaveCounter", presses="presses", bar="presses", banner="pressed", message="pressed the counter"))
add("d-event-button", "cylinder", (4, 0.25, 30), (1, 0.5, 1), (1, 0.5, 0.1), usable=True,
    **script("EventButton", event="CookbookPing"))
# The lamp: hears the event in C++ (onEvent) AND in its own graph, and the
# graph calls back into the script with Call Script.
lamp_graph = graph([("OnEvent", "CookbookPing"), ("Log", "graph heard CookbookPing"),
                    ("OnButton", "Square"), ("CallScript", "EventLamp.flash")],
                   [(1, 2), (3, 4)])
add("d-event-lamp", "sphere", (7, 1.5, 30), (1, 1, 1), (0.4, 0.4, 0.5), flowGraph=lamp_graph,
    **script("EventLamp", listenTo="CookbookPing"))

# --- Row E (z = 38): areas, camera, scene, lifecycle, the gallery ---------------
add("e-mood-zone", "area", (-6, 2, 38), (6, 4, 6), (0.6, 0.3, 0.8), **script("MoodZone"))
add("e-camera-glance", "box", (0, 0.75, 38), (0.6, 1.5, 0.6), (0.9, 0.9, 0.3), usable=True,
    **script("CameraGlance", lookAt=seed, seconds="2.5"))
add("e-restart-button", "cylinder", (4, 0.25, 38), (1, 0.5, 1), (0.9, 0.2, 0.2), usable=True,
    **script("RestartButton"))
add("e-lifecycle-zone", "area", (8, 2, 38), (4, 4, 4), (0.3, 0.8, 0.5), **script("LifecycleLogger"))
buddy = add("f-gallery-buddy", "cone", (-3, 0.75, 44), (0.6, 1.5, 0.6), (0.5, 0.8, 1.0))
add("f-props-gallery", "box", (0, 1, 44), (1.2, 1.2, 1.2), (1, 1, 1), usable=True,
    **script("PropsGallery", sliderFloat="3", dragFloat="-2.5", spinCount="2", spinning="true", mode="3",
             offset="0 0.5 0", tint="0.95 0.45 0.85", label="Hello from Properties", buddy=buddy,
             sound="res/sfx/knock.wav", event="CookbookPing", scene="main", saveValue="presses",
             action="alt", hudText="pressed", hudBar="presses", menu="save"))
add("manager", "empty", (0, 0.5, 2), **script("GameManager", reportEvery="10"))

for o in objs:
    with open(os.path.join(objdir, o["id"] + ".json"), "w", newline="\n") as f:
        f.write(json.dumps(o) + "\n")

# --- project data -------------------------------------------------------------
t = open(tpath, encoding="utf-8").read()
m = re.search(r'"objects": \[([^\]]*)\]', t)
keep = [i.strip() for i in m.group(1).split(",") if i.strip() and not i.strip().startswith('"c0b0')]
ids = keep + ['"%s"' % o["id"] for o in objs]
t = t[:m.start()] + '"objects": [' + ", ".join(ids) + "]" + t[m.end():]


def set_array(text, key, body):
    """Replaces a top-level array, or adds it before "sounds" when a fresh
    project has no such key yet."""
    if '"%s": [' % key not in text:
        return text.replace('  "sounds": [', '  "%s": [\n%s\n  ],\n  "sounds": [' % (key, body), 1)
    # Arrays of objects nest brackets only inside strings/objects here: match
    # up to the first "\n  ]" or an empty [].
    pat = r'"%s": \[(?:\]|.*?\n  \])' % key
    return re.sub(pat, lambda _m: '"%s": [\n%s\n  ]' % (key, body), text, count=1, flags=re.S)


t = set_array(t, "sounds", '    "res/sfx/knock.wav"')
t = set_array(t, "saveValues", '    { "name": "rolls", "default": 0 },\n    { "name": "presses", "default": 0 }')
t = set_array(t, "saveTexts", '    { "name": "last-press", "default": "" }')
t = set_array(t, "hudTexts", '    { "name": "pressed", "text": "COUNTER PRESSED", "pos": [0.5, 0.15], '
              '"size": 18, "color": [1, 0.85, 0.4], "shadow": true, "visibleAtStart": false }')
t = set_array(t, "hudBars", '    { "name": "presses", "kind": 1, "pos": [0.5, 0.9], "size": [200, 14], '
              '"bgColor": [0.12, 0.12, 0.16], "fillColor": [0.3, 0.8, 1.0], "ghostColor": [0.6, 0.9, 1], '
              '"ghost": false, "rightToLeft": false, "smoothing": 0.2, "lowFraction": 0, "segments": 10, '
              '"spacing": 4, "source": "presses", "min": 0, "max": 10, "start": 0 }')
if '"facts": [' in t:
    t = set_array(t, "facts", '    { "id": "fac0c00b000000001", "name": "cookbook.presses", "type": "int", '
                  '"persist": "save", "desc": "How often the save-counter station was pressed." }')
else:
    t = t.replace('"saveValues": [', '"facts": [\n    { "id": "fac0c00b000000001", "name": "cookbook.presses", '
                  '"type": "int", "persist": "save", "desc": "How often the save-counter station was pressed." }\n'
                  '  ],\n  "saveValues": [', 1)
open(tpath, "w", encoding="utf-8", newline="\n").write(t)
print("cookbook objects:", len(objs))
