#!/usr/bin/env python3
"""Rebuild the flagship three-act TyraX showcase.

The checked-in project remains the source of truth.  This authoring script is
kept beside it because a hundred objects, three cinematics and several graphs
are much easier to review as intent than as unrelated JSON blobs.
"""
from __future__ import annotations

import copy
import hashlib
import json
import math
from pathlib import Path

HERE = Path(__file__).resolve().parent
MANIFEST = HERE / "showcase.tyra"
OBJECTS = HERE / "objects"


def oid(key: str) -> str:
    return hashlib.sha1(("showcase-2026:" + key).encode()).hexdigest()[:16]


class Graph:
    def __init__(self):
        self.nodes, self.links, self.next_id = [], [], 1

    def node(self, kind, x, y, text="", text2=None, nums=None):
        node = {"id": self.next_id, "type": kind, "pos": [x, y],
                "str": text, "num": nums or [0, 0, 0, 0]}
        if text2 is not None:
            node["str2"] = text2
        self.nodes.append(node)
        self.next_id += 1
        return node["id"]

    def link(self, source, target, *, fpin=0, pin=0, plane=None):
        link = {"id": self.next_id, "from": source, "to": target}
        if fpin:
            link["fpin"] = fpin
        if pin:
            link["pin"] = pin
        if plane:
            link[plane] = True
        self.links.append(link)
        self.next_id += 1

    def out(self):
        return {"nextId": self.next_id, "nodes": self.nodes,
                "links": self.links}


def intro_graph(sequence: str, grading: str, title: str):
    g = Graph()
    start = g.node("OnStart", 0, 20)
    seq = g.node("Sequence", 220, 20)
    play = g.node("PlaySequence", 470, 0, sequence)
    grade = g.node("SetGrading", 470, 110, grading)
    text = g.node("DisplayText", 470, 220, "", title,
                  [0.5, 0.91, 18, 5])
    g.link(start, seq)
    g.link(seq, play)
    g.link(seq, grade, fpin=1)
    g.link(seq, text, fpin=2)
    return g.out()


def gateway_graph(sequence: str, target: str, delay: float):
    g = Graph()
    used = g.node("OnUsed", 0, 40)
    seq = g.node("Sequence", 220, 40)
    play = g.node("PlaySequence", 460, 0, sequence)
    fade = g.node("SetBloom", 460, 110, nums=[0.8, 0, 0, 0])
    wait = g.node("Delay", 700, 40, nums=[delay, 0, 0, 0])
    switch = g.node("SwitchScene", 930, 40, target)
    g.link(used, seq)
    g.link(seq, play)
    g.link(seq, fade, fpin=1)
    g.link(seq, wait, fpin=2)
    g.link(wait, switch)
    return g.out()


def layer_gate(load: str, unload: str, radius=24):
    g = Graph()
    near = g.node("NearObject", 0, 40, nums=[radius, 0, 0, 0])
    on = g.node("SetLayerLoaded", 260, 0, load)
    off = g.node("SetLayerLoaded", 260, 110, unload)
    g.link(near, on)
    g.link(near, off, pin=1)
    return g.out()


def spinner(speed=35):
    g = Graph()
    start = g.node("OnStart", 0, 20)
    spin = g.node("SpinObject", 230, 20, nums=[0, speed, 0, 0])
    g.link(start, spin)
    return g.out()


def drone_ai(waypoint_prefix, seen=16):
    g = Graph()
    start = g.node("OnStart", 0, 20)
    patrol = g.node("PatrolWaypoints", 240, 20, waypoint_prefix,
                    nums=[2.2, 0.5, 0, 0])
    spotted = g.node("OnPlayerSeen", 0, 180, nums=[seen, 120, 1, 0])
    chase = g.node("ChasePlayer", 240, 180, nums=[3.2, 1.4, 28, 0])
    g.link(start, patrol)
    g.link(spotted, chase)
    return g.out()


def physics_pulse():
    g = Graph()
    tick = g.node("EverySeconds", 0, 20, nums=[4, 0, 0, 0])
    kick = g.node("PushObject", 240, 20, nums=[1.5, 8, -2, 0])
    g.link(tick, kick)
    return g.out()


def obj(key, name, kind, pos, *, scale=(1, 1, 1), rot=(0, 0, 0),
        color=(1, 1, 1), layer=None, model=None, graph=None, usable=False,
        collision=None, extra=None):
    data = {"id": oid(key), "name": name, "type": kind,
            "position": [round(v, 4) for v in pos],
            "rotation": list(rot), "scale": list(scale),
            "color": list(color), "physics": False}
    if layer:
        data["layer"] = layer
    if model:
        data["model"] = model
    if graph:
        data["flowGraph"] = graph
    if usable:
        data["usable"] = True
    if collision:
        data["collision"] = collision
    if extra:
        data.update(extra)
    return data


def model(key, name, path, pos, **kwargs):
    return obj(key, name, "model", pos, model=path, **kwargs)


def player(key, name, pos, rot=(0, 0, 0)):
    return obj(key, name, "player", pos, rot=rot, color=(0.2, 0.9, 1),
               extra={"player": {"mode": "walk", "walkSpeed": 0.65,
                       "lookSpeed": 1, "eyeHeight": 1.8,
                       "jumpSpeed": 4.5, "canJump": True,
                       "flashlight": {"enabled": True,
                           "color": [0.7, 0.8, 1], "range": 28,
                           "angle": 18, "toggle": "Circle"}}})


def light(key, name, pos, color, radius, brightness=1, *, layer=None,
          flicker=0.0, dynamic=True):
    return obj(key, name, "point-light", pos, scale=(0.35, 0.35, 0.35),
               color=color, layer=layer,
               extra={"light": {"brightness": brightness, "radius": radius,
                       "dynamic": dynamic, "flicker": flicker, "beam": 2}})


def emitter(key, name, pos, kind, color, count, size, scale=(1, 1, 1),
            *, layer=None, follow=False):
    return obj(key, name, "emitter", pos, scale=scale, color=color, layer=layer,
               extra={"emitter": {"kind": kind, "count": count,
                       "size": size, "enabled": True,
                       "followPlayer": follow}})


def camera_key(t, eye, target, fov, ease=1, shake=0):
    key = {"t": t, "eye": eye, "target": target, "fov": fov,
           "ease": ease}
    if shake:
        key["shake"] = shake
    return key


def sequence(name, duration, cameras, tracks=None, bars=0.85):
    return {"name": name, "duration": duration, "loop": False,
            "cameraEnabled": True, "hidePlayer": False, "bars": bars,
            "skippable": True, "fadeIn": 0.55, "fadeOut": 0.7,
            "barsSlideIn": 0.45, "barsSlideOut": 0.65,
            "tracks": tracks or [], "cameraKeys": cameras}


def track(target, keys, *, pos=False, rot=False, scale=False, color=False,
          vis=False):
    return {"target": target, "animPos": pos, "animRot": rot,
            "animScale": scale, "animColor": color, "animVis": vis,
            "keys": keys}


def tkey(t, pos, rot=(0, 0, 0), scale=(1, 1, 1), color=(1, 1, 1),
         vis=True, ease=1):
    return {"t": t, "pos": list(pos), "rot": list(rot),
            "scale": list(scale), "color": list(color), "vis": vis,
            "ease": ease}


def village_objects():
    out = [
        player("v-player", "village-player", (0, 1.4, 18), (0, 180, 0)),
        obj("v-director", "village-director", "empty", (0, 0, 0),
            graph=intro_graph("Dawn of Worlds", "Elysian", "ACT I  /  ELYSIAN VILLAGE"),
            collision="none"),
        model("v-gate", "rift-gateway",
              "res/models/medieval/DoorFrame_Round_WoodDark.obj",
              (0, 0.2, -27.8), scale=(3.2, 3.2, 3.2)),
        obj("v-gate-core", "rift-core", "sphere", (0, 2.4, -27.5),
            scale=(1.35, 1.35, 1.35), color=(0.3, 0.85, 1),
            graph=spinner(55), collision="none"),
        obj("v-gate-console", "rift-console", "cylinder", (0, 0.45, -23.5),
            scale=(1.15, 0.9, 1.15), color=(0.07, 0.12, 0.2), usable=True,
            graph=gateway_graph("Rift Ignition", "rift-lab", 3.2)),
        emitter("v-rift-sparks", "rift-sparks", (0, 2.5, -27), "sparks",
                (0.2, 0.9, 1), 72, 0.12, (3, 4, 2)),
        light("v-rift-light", "rift-light", (0, 3, -26.5),
              (0.25, 0.8, 1), 14, 1.5, flicker=0.12),
        obj("v-layer-market", "market-stream-gate", "empty", (12, 0, 2),
            graph=layer_gate("market", "ridge"), collision="none"),
        obj("v-layer-ridge", "ridge-stream-gate", "empty", (-17, 0, -8),
            graph=layer_gate("ridge", "market"), collision="none"),
    ]
    # Fertile Soil's CC0 kit is assembled into complete, single-draw models.
    # The detailed hero buildings stream by district; the smaller cottages are
    # real geometry too, replacing the old box-and-cone skyline placeholders.
    out += [
        model("v-ridge-tower", "ridge-watchtower",
              "res/models/modular-village/village-watchtower.obj",
              (-18, 0.15, -8), rot=(0, 15, 0), scale=(1.7, 1.7, 1.7),
              layer="ridge"),
        light("v-ridge-lamp", "ridge-watchtower-lamp", (-16.8, 3.0, -5.8),
              (1, 0.55, 0.2), 8, 0.8, layer="ridge", flicker=0.25),
        model("v-market-tavern", "market-tavern",
              "res/models/modular-village/village-tavern.obj",
              (18, 0.15, 10), rot=(0, 215, 0), scale=(1.55, 1.55, 1.55),
              layer="market"),
        light("v-market-lamp", "market-tavern-lamp", (16.5, 2.8, 11.7),
              (1, 0.55, 0.2), 8, 0.8, layer="market", flicker=0.25),
    ]
    for i, (x, z, yaw, scale, layer_name) in enumerate([
        (-14, 12, 32, 1.30, "market"),
        (14, -13, 205, 1.42, "ridge"),
        (-22, -18, 58, 1.24, "ridge"),
        (22, 5, 242, 1.34, "market"),
        (-8, -24, 355, 1.18, "ridge"),
    ]):
        out.append(model(f"v-cottage-{i}", f"village-cottage-{i+1}",
                         "res/models/modular-village/village-cottage.obj",
                         (x, 0.12, z), rot=(0, yaw, 0),
                         scale=(scale, scale, scale), layer=layer_name))
    out += [
        model("v-wagon", "market-wagon", "res/models/medieval/Prop_Wagon.obj",
              (8, 0.3, 3), rot=(0, 72, 0), scale=(1.4, 1.4, 1.4), layer="market"),
        model("v-crate-1", "market-crate-a", "res/models/medieval/Prop_Crate.obj",
              (5, 0.7, 0), scale=(1.5, 1.5, 1.5), layer="market"),
        model("v-crate-2", "market-crate-b", "res/models/medieval/Prop_Crate.obj",
              (6.4, 0.7, -0.4), rot=(0, 25, 0), scale=(1.3, 1.3, 1.3), layer="market"),
        model("v-well", "market-well",
              "res/models/modular-village/village-well.obj",
              (-9, 0.2, 8), scale=(1.6, 1.6, 1.6), layer="market"),
        obj("v-fire-base", "festival-brazier", "cylinder", (-4, 0.5, 4),
            scale=(1.2, 0.7, 1.2), color=(0.18, 0.12, 0.08)),
        emitter("v-fire", "festival-fire", (-4, 1, 4), "fire",
                (1, 0.45, 0.1), 54, 0.55, (1.4, 1.5, 1.4)),
        emitter("v-smoke", "festival-smoke", (-4, 2.5, 4), "smoke",
                (0.35, 0.3, 0.28), 26, 0.75),
        light("v-fire-light", "festival-light", (-4, 2, 4),
              (1, 0.42, 0.12), 13, 1.35, flicker=0.5),
        obj("v-portal-a", "village-portal-a", "portal", (-8, 2.3, -20),
            scale=(2.2, 3.2, 1), color=(0.95, 0.45, 0.15), layer="ridge",
            extra={"portal": {"target": "village-portal-b", "showTerrain": False,
                               "teleportObjects": False, "viewAll": False,
                               "objects": ["market-tavern", "village-cottage-1",
                                           "market-wagon", "festival-fire"]}}),
        obj("v-portal-b", "village-portal-b", "portal", (15, 4.5, 15),
            rot=(0, 180, 0), scale=(2.2, 3.2, 1), color=(0.2, 0.75, 1),
            layer="ridge",
            extra={"portal": {"target": "village-portal-a", "showTerrain": False,
                               "teleportObjects": False, "viewAll": False,
                               "objects": ["market-tavern", "ridge-watchtower",
                                           "rift-gateway", "rift-sparks"]}}),
    ]
    return out


def lab_objects():
    out = [
        player("l-player", "lab-player", (0, 1.2, 22), (0, 180, 0)),
        obj("l-director", "lab-director", "empty", (0, 0, 0),
            graph=intro_graph("Portal Breach", "Rift Lab", "ACT II  /  RIFT LABORATORY"),
            collision="none"),
        obj("l-floor", "lab-floor", "box", (0, -0.5, 0), scale=(52, 1, 52),
            color=(0.06, 0.08, 0.12)),
        obj("l-back", "lab-back-wall", "box", (0, 6, -25), scale=(52, 13, 1),
            color=(0.08, 0.1, 0.16)),
        obj("l-left", "lab-left-wall", "box", (-25, 6, 0), scale=(1, 13, 52),
            color=(0.08, 0.1, 0.16)),
        obj("l-right", "lab-right-wall", "box", (25, 6, 0), scale=(1, 13, 52),
            color=(0.08, 0.1, 0.16)),
        obj("l-mirror", "quantum-mirror", "mirror", (0, 4.5, -24.3),
            rot=(0, 180, 0), scale=(12, 7, 1), color=(0.8, 0.9, 1),
            layer="reflection",
            extra={"mirror": {"opacity": 0.3, "reflectPlayer": True,
                               "raytraced": True, "rtSize": 32,
                               "objects": ["eye-drone", "physics-orb-a"]}}),
        obj("l-portal-floor", "gravity-portal-floor", "portal", (-12, 0.3, -2),
            rot=(-90, 0, 0), scale=(3.2, 3.2, 1), color=(0.7, 0.2, 1),
            layer="portal",
            extra={"portal": {"target": "gravity-portal-ceiling", "showTerrain": False,
                               "teleportObjects": True, "viewAll": False,
                               "objects": ["physics-orb-a", "physics-orb-b",
                                           "physics-orb-c", "reactor-haze"]}}),
        obj("l-portal-ceil", "gravity-portal-ceiling", "portal", (-12, 10, -2),
            rot=(90, 0, 0), scale=(3.2, 3.2, 1), color=(0.2, 1, 0.65),
            layer="portal",
            extra={"portal": {"target": "gravity-portal-floor", "showTerrain": False,
                               "teleportObjects": False, "viewAll": False,
                               "objects": ["physics-orb-a", "physics-orb-b",
                                           "physics-orb-c", "lab-floor"]}}),
        model("l-gateway", "city-uplink",
              "res/models/scifi/Prop_Locker.obj", (14.8, 1.2, -18),
              rot=(0, 90, 0), scale=(1.8, 1.8, 1.8), usable=True,
              graph=gateway_graph("City Uplink", "neon-city", 3.0)),
        model("l-uplink-right", "uplink-frame-right",
              "res/models/scifi/Prop_Locker.obj", (19.2, 1.2, -18),
              rot=(0, -90, 0), scale=(1.8, 1.8, 1.8)),
        emitter("l-gateway-fx", "uplink-plasma", (17, 2.8, -17.4), "sparks",
                (1, 0.15, 0.65), 48, 0.14, (3, 5, 2)),
        obj("l-bio-gate-a", "bio-wing-gate-a", "empty", (0, 0, 16),
            graph=layer_gate("bio", "reflection", 13), collision="none"),
        obj("l-bio-gate-b", "bio-wing-gate-b", "empty", (0, 0, 16),
            graph=layer_gate("bio", "portal", 13), collision="none"),
        obj("l-portal-gate-a", "portal-wing-gate-a", "empty", (-12, 0, -2),
            graph=layer_gate("portal", "bio", 8), collision="none"),
        obj("l-portal-gate-b", "portal-wing-gate-b", "empty", (-12, 0, -2),
            graph=layer_gate("portal", "reflection", 8), collision="none"),
        obj("l-reflect-gate-a", "reflection-wing-gate-a", "empty", (0, 0, -19),
            graph=layer_gate("reflection", "bio", 8), collision="none"),
        obj("l-reflect-gate-b", "reflection-wing-gate-b", "empty", (0, 0, -19),
            graph=layer_gate("reflection", "portal", 8), collision="none"),
    ]
    for i, (name, path, pos, scale) in enumerate([
        ("eye-drone", "Enemy_EyeDrone.fbx", (-7, 3.2, -8), 1.7),
        ("quad-shell", "Enemy_QuadShell.fbx", (5, 1.1, -9), 1.25),
        ("trilobite", "Enemy_Trilobite.fbx", (10, 1.0, 3), 1.15),
    ]):
        feature_layer = "reflection" if i == 0 else "bio"
        out.append(model(f"l-anim-{i}", name, f"res/models/scifi/{path}", pos,
                         scale=(scale, scale, scale), collision="none",
                         layer=feature_layer,
                         graph=drone_ai("lab-wp-1") if i == 1 else None,
                         extra={"anim": {"clip": "", "autoplay": True,
                                         "loop": True, "speed": 0.75 + i * 0.1}}))
        if i == 1:
            for w, wp in enumerate([(-8, -4), (-1, -13), (8, -5)]):
                out.append(obj(f"l-wp-{i}-{w}", f"lab-wp-{i}-{w+1}", "empty",
                               (wp[0] + i * 2, 1.0, wp[1] + i),
                               layer=feature_layer, collision="none"))
    for i, (x, z) in enumerate([(-16, 6), (-8, 8), (1, 7), (9, 9)]):
        feature_layer = ("bio", "bio", "portal", "reflection")[i]
        out.append(model(f"l-crate-{i}", f"lab-crate-{chr(97+i)}",
                         "res/models/scifi/Prop_Crate_Large.obj", (x, 1, z),
                         rot=(0, i * 27, 0), scale=(1.5, 1.5, 1.5),
                         layer=feature_layer))
    out += [
        model("l-dish", "tracking-dish", "res/models/scifi/Prop_SatelliteDish.obj",
              (15, 1.5, 8), scale=(2.2, 2.2, 2.2), layer="portal",
              graph=spinner(18)),
        model("l-rifle", "artifact-rifle", "res/models/scifi/Gun_Rifle.obj",
              (0, 2, 2), rot=(0, 35, 15), scale=(2, 2, 2), graph=spinner(24),
              usable=True, collision="none", layer="reflection"),
        emitter("l-haze", "reactor-haze", (0, 0.7, -6), "fog",
                (0.18, 0.4, 0.65), 38, 3.2, (18, 1, 18), layer="portal"),
    ]
    for i, (x, y, z, color) in enumerate([(-12, 7, -2, (1, 0.2, 0.7)),
                                           (-10, 9, -2, (0.2, 0.8, 1)),
                                           (-14, 11, -2, (0.4, 1, 0.4))]):
        out.append(obj(f"l-phys-{i}", f"physics-orb-{chr(97+i)}", "sphere",
                       (x, y, z), scale=(0.8, 0.8, 0.8), color=color,
                       layer="portal",
                       graph=physics_pulse() if i == 0 else None,
                       extra={"physics": True, "physMass": 0.8,
                              "physBounce": 0.72, "physFriction": 0.12,
                              "physTumble": True, "physSleep": 3}))
    return out


def city_objects():
    out = [
        player("c-player", "city-player", (0, 1.2, 26), (0, 180, 0)),
        obj("c-director", "city-director", "empty", (0, 0, 0),
            graph=intro_graph("Neon Overdrive", "Neon Rain", "ACT III  /  NEON OVERDRIVE"),
            collision="none"),
        emitter("c-rain", "neon-rain", (0, 13, 0), "rain",
                (0.45, 0.65, 1), 112, 0.12, (22, 1, 22), follow=True),
        emitter("c-steam", "street-steam", (-8, 0.6, -2), "smoke",
                (0.5, 0.55, 0.65), 34, 0.9, (2, 1, 2)),
    ]
    # Road deck.
    for z in range(-24, 25, 8):
        out.append(model(f"c-road-{z}", f"road-{z+24:02d}",
                         "res/models/urban/road-asphalt-straight.obj", (0, 0.05, z),
                         scale=(4, 4, 4)))
    # Five kit-bashed towers, streamed by city block.
    towers = [(-18, -16, "west", "a"), (-17, 5, "west", "b"),
              (18, -18, "east", "b"), (17, 3, "east", "a"),
              (14, 20, "east", "b")]
    for i, (x, z, layer_name, style) in enumerate(towers):
        for floor in range(3 + (i % 2)):
            y = 1.8 + floor * 3.8
            out.append(model(f"c-tower-{i}-{floor}", f"tower-{i+1}-floor-{floor+1}",
                             f"res/models/urban/wall-{style}-window.obj", (x, y, z),
                             rot=(0, 90 if x > 0 else -90, 0), scale=(3.2, 3.2, 3.2),
                             layer=layer_name))
        out.append(model(f"c-tower-roof-{i}", f"tower-{i+1}-roof",
                         f"res/models/urban/wall-{style}-roof-detailed.obj",
                         (x, 13.2 + (i % 2) * 3.8, z),
                         rot=(0, 90 if x > 0 else -90, 0), scale=(3.2, 3.2, 3.2),
                         layer=layer_name))
    out += [
        obj("c-west-gate", "west-block-gate", "empty", (-12, 0, 0),
            graph=layer_gate("west", "east", 30), collision="none"),
        obj("c-east-gate", "east-block-gate", "empty", (12, 0, 0),
            graph=layer_gate("east", "west", 30), collision="none"),
        model("c-police", "police-cruiser", "res/models/urban/police-car.obj",
              (-4, 0.8, -8), rot=(0, 180, 0), scale=(1.15, 1.15, 1.15)),
        model("c-truck", "cargo-truck", "res/models/urban/truck-green.obj",
              (5, 0.7, 7), rot=(0, 0, 0), scale=(1.7, 1.7, 1.7)),
        model("c-scaffold", "hero-scaffolding", "res/models/urban/scaffolding-structure.obj",
              (-10, 0.2, -20), rot=(0, 90, 0), scale=(3, 3, 3), layer="west"),
        model("c-dumpster", "neon-dumpster", "res/models/urban/detail-dumpster-open.obj",
              (8, 0.5, -4), rot=(0, -30, 0), scale=(2, 2, 2), layer="east"),
        obj("c-roof-portal-a", "rooftop-portal-a", "portal", (-14, 3.5, -12),
            rot=(0, 90, 0), scale=(2.4, 3.6, 1), color=(1, 0.15, 0.65),
            extra={"portal": {"target": "rooftop-portal-b", "showTerrain": False,
                               "teleportObjects": True, "viewAll": False,
                               "objects": ["tower-5-floor-3", "tower-5-roof",
                                           "city-eye-drone", "neon-rain"]}}),
        obj("c-roof-portal-b", "rooftop-portal-b", "portal", (14, 8.5, 15),
            rot=(0, -90, 0), scale=(2.4, 3.6, 1), color=(0.15, 0.8, 1),
            extra={"portal": {"target": "rooftop-portal-a", "showTerrain": False,
                               "teleportObjects": False, "viewAll": False,
                               "objects": ["tower-1-floor-3", "tower-1-roof",
                                           "police-cruiser", "street-steam"]}}),
    ]
    neon = [(-10, 4, 12, (1, 0.1, 0.55)), (10, 5, 10, (0.1, 0.8, 1)),
            (0, 4, -22, (1, 0.35, 0.08))]
    for i, (x, y, z, color) in enumerate(neon):
        # Keep the west-side arrival at one dynamic light for the 50 FPS
        # gameplay budget. The east wing deliberately stacks two lights as a
        # heavier showcase beat where a short performance dip is acceptable.
        feature_layer = ("west", "east", "east")[i]
        out.append(light(f"c-neon-{i}", f"neon-light-{i+1}", (x, y, z),
                         color, 9, 1.45, flicker=0.12, layer=feature_layer))
    out.append(model("c-drone", "city-eye-drone",
                     "res/models/scifi/Enemy_EyeDrone.fbx", (0, 6, -12),
                     scale=(1.5, 1.5, 1.5), collision="none", layer="east",
                     graph=spinner(24),
                     extra={"anim": {"clip": "", "autoplay": True,
                                     "loop": True, "speed": 0.8}}))
    return out


def scene_settings(light_dir, ambient, diffuse, light_color, sky, top, fog,
                   fog_start, fog_end, bloom, grain, terrain_material):
    return {"lighting": {"dir": light_dir, "ambient": ambient,
                          "diffuse": diffuse, "color": light_color,
                          "brightness": 1},
            "sky": {"color": sky, "topColor": top, "dome": True,
                    "zenithSize": 0.5}, "clipping": "precise",
            "terrainMaterial": terrain_material,
            "postfx": {"bloom": bloom, "bloomThreshold": 0.35,
                       "bloomSpread": 1, "grain": grain, "dofAmount": 0,
                       "dofFocus": 20, "dofRange": 15, "flare": 0.18,
                       "godRays": 0.28},
            "fog": {"enabled": True, "color": fog,
                    "start": fog_start, "end": fog_end},
            "highlight": {"usable": True, "distance": 7,
                          "color": [0.2, 0.9, 1], "width": 0.28,
                          "steps": 4, "opacity": 0.5, "overlay": False}}


def write_terrain(name, width, depth, kind):
    n = 33
    rows = []
    for z in range(n):
        row = []
        for x in range(n):
            wx = (x / (n - 1) - 0.5) * width
            wz = (z / (n - 1) - 0.5) * depth
            if kind == "village":
                h = 0.35 * math.sin(wx * 0.13) + 0.28 * math.cos(wz * 0.16)
                h += 1.9 * math.exp(-((wx + 22) ** 2 + (wz + 8) ** 2) / 170)
            elif kind == "city":
                h = 0.05 * math.sin(wx * 0.2) * math.cos(wz * 0.15)
            else:
                h = 0
            row.append(f"{h:.4f}")
        rows.append(" ".join(row))
    (HERE / f"terrain-{name}.heights").write_text(
        f"{n} {n}\n" + "\n".join(rows) + "\n", encoding="utf-8")


def main():
    manifest = json.loads(MANIFEST.read_text(encoding="utf-8"))
    village, lab, city = village_objects(), lab_objects(), city_objects()
    all_objects = village + lab + city

    # Regeneration owns the object directory; stable IDs keep diffs legible.
    OBJECTS.mkdir(exist_ok=True)
    for old in OBJECTS.glob("*.json"):
        old.unlink()
    for item in all_objects:
        (OBJECTS / f"{item['id']}.json").write_text(
            json.dumps(item, separators=(",", ":")) + "\n", encoding="utf-8")

    overrides = {"lighting": True, "sky": True, "clipping": False,
                 "terrainMat": True, "postFx": True, "fog": True,
                 "highlight": True}
    manifest["scenes"] = [
        {"name": "elysian-village", "terrain": {"width": 80, "depth": 80},
         "settings": scene_settings([0.5, 0.72, 0.32], 0.42, 0.7,
                                    [1, 0.82, 0.62], [0.46, 0.58, 0.76],
                                    [0.055, 0.09, 0.23], [0.34, 0.42, 0.54],
                                    24, 72, 0.0, 0.0,
                                    "res/materials/village-ground.mtl"),
         "overrides": overrides, "ambiencePreset": "Elysian Dawn",
         "layers": [{"name": "market"}, {"name": "ridge", "startLoaded": False}],
         "objects": [o["id"] for o in village]},
        {"name": "rift-lab", "terrain": {"width": 56, "depth": 56},
         "settings": scene_settings([0.1, 0.9, 0.35], 0.58, 0.74,
                                    [0.72, 0.85, 1], [0.06, 0.09, 0.18],
                                    [0.02, 0.035, 0.08], [0.08, 0.14, 0.22],
                                    10, 48, 0.0, 0.0,
                                    "res/materials/ground.mtl"),
         "overrides": overrides, "ambiencePreset": "Rift Lab",
         "layers": [{"name": "bio"},
                    {"name": "portal", "startLoaded": False},
                    {"name": "reflection", "startLoaded": False}],
         "objects": [o["id"] for o in lab]},
        {"name": "neon-city", "terrain": {"width": 96, "depth": 96},
         "settings": scene_settings([0.15, 0.82, -0.2], 0.16, 0.48,
                                    [0.45, 0.55, 1], [0.025, 0.02, 0.09],
                                    [0.002, 0.004, 0.02], [0.08, 0.04, 0.16],
                                    14, 70, 0.0, 0.0,
                                    "res/materials/city-asphalt.mtl"),
         "overrides": overrides, "ambiencePreset": "Neon Rain",
         "layers": [{"name": "west"}, {"name": "east", "startLoaded": False}],
         "objects": [o["id"] for o in city]},
    ]
    settings = manifest["settings"]
    settings.update({"buildProfile": "debug", "showFps": True,
                     "showMemory": True, "showProfiler": False,
                     "liveLink": True, "liveDebug": True, "liveLogic": True,
                     "timeMachine": True, "keyboardMouse": True,
                     "clipping": "precise", "textureQuant": "4bit",
                     "textureAtlas": False, "staticBatching": True,
                     "terrainDetail": 32, "terrainViewDistance": 62,
                     "animLodDistance": 24, "meshLodDistance": 32,
                     "aoEnabled": True, "aoStrength": 0.52, "aoRadius": 2.4,
                     "giEnabled": True, "giRays": 64, "giBounces": 2,
                     "giSkyLight": 0.8, "giSunLight": 1,
                     "giAmbientFloor": 0.03, "giProbes": True,
                     "giProbeSpacing": 4, "giProbeHeight": 2,
                     "giProbeLevels": 3, "blobShadows": True,
                     "highlightUsable": True, "loadingScreen": True})
    manifest["gradings"] = [
        {"name": "Elysian", "brightness": 0.96, "contrast": 1.06,
         "saturation": 1.06, "temperature": 0.08,
         "tint": [1, 0.78, 0.58], "tintAmount": 0.04,
         "lift": [0.005, 0, 0.005], "gain": [1.01, 1, 0.99]},
        {"name": "Rift Lab", "brightness": 1.18, "contrast": 1.02,
         "saturation": 1.04, "temperature": -0.12,
         "tint": [0.35, 0.58, 1], "tintAmount": 0.07,
         "lift": [0.035, 0.05, 0.085], "gain": [1.06, 1.12, 1.2]},
        {"name": "Neon Rain", "brightness": 0.9, "contrast": 1.28,
         "saturation": 1.24, "temperature": -0.12,
         "tint": [0.82, 0.2, 1], "tintAmount": 0.18,
         "lift": [-0.02, -0.01, 0.035], "gain": [1.05, 0.9, 1.16]},
    ]
    manifest["defaultGrading"] = 0
    manifest["ambience"] = [
        {"name": "Elysian Dawn", "skyColor": [0.46, 0.58, 0.76],
         "skyTopColor": [0.055, 0.09, 0.23], "skyDome": True,
         "zenithSize": 0.5, "lightDir": [0.5, 0.72, 0.32], "ambient": 0.42,
         "diffuse": 0.7, "lightColor": [1, 0.82, 0.62], "brightness": 1,
         "aoEnabled": True, "aoStrength": 0.52, "aoRadius": 2.4,
         "fogEnabled": True, "fogColor": [0.34, 0.42, 0.54],
         "fogStart": 24, "fogEnd": 72},
        {"name": "Rift Lab", "skyColor": [0.06, 0.09, 0.18],
         "skyTopColor": [0.02, 0.035, 0.08], "skyDome": False,
         "zenithSize": 0.5, "lightDir": [0.1, 0.9, 0.35], "ambient": 0.58,
         "diffuse": 0.74, "lightColor": [0.72, 0.85, 1], "brightness": 1.18,
         "aoEnabled": True, "aoStrength": 0.65, "aoRadius": 2.2,
         "fogEnabled": True, "fogColor": [0.08, 0.14, 0.22],
         "fogStart": 10, "fogEnd": 48},
        {"name": "Neon Rain", "skyColor": [0.025, 0.02, 0.09],
         "skyTopColor": [0.002, 0.004, 0.02], "skyDome": True,
         "zenithSize": 0.38, "lightDir": [0.15, 0.82, -0.2], "ambient": 0.16,
         "diffuse": 0.48, "lightColor": [0.45, 0.55, 1], "brightness": 1,
         "aoEnabled": True, "aoStrength": 0.6, "aoRadius": 2.6,
         "fogEnabled": True, "fogColor": [0.08, 0.04, 0.16],
         "fogStart": 14, "fogEnd": 70},
    ]
    manifest["defaultAmbience"] = 0
    manifest["hudTexts"] = [
        {"name": "showcase-help", "text": "{{square}} INTERACT   {{circle}} FLASHLIGHT   START OPTIONS",
         "pos": [0.5, 0.94], "size": 13, "color": [0.82, 0.94, 1],
         "shadow": True, "visibleAtStart": True}
    ]
    manifest["usePromptIsText"] = True
    manifest["pickPromptIsText"] = True
    manifest["sequences"] = [
        sequence("Dawn of Worlds", 8.0, [
            camera_key(0, [-28, 13, 30], [0, 2, -4], 58, 2),
            camera_key(3, [24, 8, 18], [0, 2, -10], 65, 1),
            camera_key(6, [8, 4, -18], [0, 2.5, -28], 52, 2),
            camera_key(8, [0, 3, 15], [0, 2, -10], 70, 1)]),
        sequence("Rift Ignition", 3.2, [
            camera_key(0, [7, 3, -18], [0, 2.4, -28], 55, 2),
            camera_key(2.1, [0, 2.8, -21], [0, 2.4, -28], 88, 1, 0.08),
            camera_key(3.2, [0, 2.4, -25], [0, 2.4, -28], 100, 1, 0.16)],
            [track("rift-core", [tkey(0, (0, 2.4, -27.5), scale=(1, 1, 1), color=(0.2, .7, 1)),
                                  tkey(3.2, (0, 2.4, -27.5), rot=(0, 720, 0), scale=(2.5, 2.5, 2.5), color=(1, .35, .8))],
                   rot=True, scale=True, color=True)]),
        sequence("Portal Breach", 7.0, [
            camera_key(0, [-20, 8, 20], [0, 2, -6], 58, 2),
            camera_key(2.4, [-10, 3, 3], [-12, 4, -2], 75, 1),
            camera_key(4.7, [13, 4, 6], [0, 4, -24], 50, 1),
            camera_key(7, [0, 3, 20], [0, 2, -8], 68, 2)]),
        sequence("City Uplink", 3.0, [
            camera_key(0, [10, 4, -10], [17, 2.8, -18], 62, 2),
            camera_key(2, [17, 3, -11], [17, 3, -18], 95, 1, 0.1),
            camera_key(3, [17, 3, -16], [17, 3, -18], 110, 1, 0.18)]),
        sequence("Neon Overdrive", 10.0, [
            camera_key(0, [-30, 17, 28], [0, 3, -5], 58, 2),
            camera_key(3, [8, 5, 22], [0, 2, -10], 74, 1),
            camera_key(6, [-7, 3, -6], [0, 6, -15], 48, 2),
            camera_key(8.5, [25, 13, -18], [0, 5, 0], 65, 1, 0.04),
            camera_key(10, [0, 3, 24], [0, 2, -4], 72, 2)],
            [track("police-cruiser", [tkey(0, (-4, .8, -8), rot=(0, 180, 0)),
                                       tkey(10, (-4, .8, 18), rot=(0, 180, 0))], pos=True)]),
    ]
    # Keep the useful settings menu, but rename it as the showcase control deck.
    if manifest.get("menus"):
        manifest["menus"][0]["title"] = "TYRAX // PAUSED"

    manifest["editor"] = {"selectedObject": -1, "gizmo": 0,
                          "gizmoSpace": 0, "viewMode": 0,
                          "viewProjection": 0, "breakpoints": []}
    MANIFEST.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    write_terrain("elysian-village", 80, 80, "village")
    write_terrain("rift-lab", 56, 56, "lab")
    write_terrain("neon-city", 96, 96, "city")
    for legacy in (HERE / "terrain-vale.heights", HERE / "terrain-cavern.heights"):
        if legacy.exists():
            legacy.unlink()
    print(f"showcase: {len(all_objects)} objects, 3 scenes, 5 cinematics")


if __name__ == "__main__":
    main()
