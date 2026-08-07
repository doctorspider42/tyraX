#!/usr/bin/env python3
"""Authors the World Facts example (docs/world-facts.md).

A single small level whose entire logic is facts: three generator parts to
collect, an NPC to rescue, a locked basement, and a power state that other
things react to. Nothing here is a scratch variable - every piece of state is
a declared fact, and the parts that would normally be duplicated across five
graphs (is the basement enterable? is Marta an ally?) are named queries.

The point of the example is coverage, so between them the pieces exercise:

  * every fact TYPE          - yes/no, whole number, number, one-of-several,
                               position
  * every PERSISTENCE tier   - session, checkpoint, save game, profile
  * both SCOPES              - world, and one scene-scoped fact
  * a COMPUTED fact          - marta.isAlly, which nothing can write
  * nested QUERIES           - CanEnterBasement names MartaIsAlly
  * all three RULE policies  - becomes-true, every-frame, once-per-run
  * a rule that SENDS AN EVENT into a graph
  * the reactive trigger     - On Fact Changed
  * Get Fact As Text         - the one-of-several fact printed by NAME

Kept as a script for the reason the cube example is: fourteen graphs is not
something anyone should read as JSON. Re-run it after `tyrax-editor --new` to
rebuild the example from scratch.
"""
import json
import os

HERE = os.path.dirname(os.path.abspath(__file__))
TYRA = os.path.join(HERE, "world-facts.tyra")
OBJ = os.path.join(HERE, "objects")

# --- ids ---------------------------------------------------------------------
# Stable, hand-assigned so a re-run overwrites the same object files instead of
# leaving orphans behind (project::save prunes them, but a diff full of renamed
# files helps nobody).
IDS = {
    "part-a": "fac70000000000a1",
    "part-b": "fac70000000000a2",
    "part-c": "fac70000000000a3",
    "marta": "fac70000000000b1",
    "generator": "fac70000000000c1",
    "basement-door": "fac70000000000d1",
    "lamp": "fac70000000000e1",
    "key": "fac70000000000f1",
    "overload-switch": "fac700000000000f",
    "hud": "fac70000000000aa",
    "checkpoint": "fac70000000000bb",
}


def obj(key, name, kind, pos, color, scale=(1, 1, 1), graph=None,
        collision="box", usable=False, extra=None):
    o = {
        "id": IDS[key],
        "name": name,
        "type": kind,
        "position": [round(v, 3) for v in pos],
        "rotation": [0, 0, 0],
        "scale": [round(v, 3) for v in scale],
        "color": list(color),
        "physics": False,
        "collision": collision,
        "castShadow": True,
    }
    if usable:
        o["usable"] = True
    if extra:
        o.update(extra)
    if graph:
        o["flowGraph"] = graph
    return o


# --- graph helpers -----------------------------------------------------------
# A graph is nodes + links; the editor lays nothing out for us, so the helpers
# below place nodes on a grid as they are added. Node ids are local to a graph.


class G:
    def __init__(self):
        self.nodes = []
        self.links = []
        self.next = 1

    def n(self, type_, col=0, row=0, str_="", str2=None, num=None):
        node = {
            "id": self.next,
            "type": type_,
            "pos": [col * 280, row * 150],
            "str": str_,
            "num": (num or [0, 0, 0, 0]),
        }
        if str2 is not None:
            node["str2"] = str2
        self.nodes.append(node)
        self.next += 1
        return node["id"]

    def link(self, a, b, kind=None, fpin=0, pin=0):
        # A link's PLANE is a boolean key in the .tyra ("pos": true), not a
        # "kind" string - see project::flowGraphJson. Getting this wrong is
        # silent: the link loads as a plain exec link and the node quietly
        # falls back to its own params.
        l = {"id": self.next, "from": a, "to": b}
        if kind:
            l[kind] = True
        if fpin:
            l["fpin"] = fpin
        if pin:
            l["pin"] = pin
        self.links.append(l)
        self.next += 1

    def out(self):
        return {"nextId": self.next, "nodes": self.nodes, "links": self.links}


# --- the facts catalog -------------------------------------------------------
# Read this list first: it IS the design of the level. Every graph below only
# reads and writes what is declared here, which is the whole argument for a
# catalog - the state of the world is one page long and lives in one place.
FACTS = [
    {
        "id": "fac71000000000001",
        "name": "world.generator.parts",
        "type": "int",
        "persist": "save",
        "desc": "How many of the three generator parts the player has fitted.",
    },
    {
        "id": "fac71000000000002",
        "name": "world.power.state",
        "type": "enum",
        "persist": "save",
        "options": ["Broken", "Powered", "Overloaded"],
        "desc": "What the plant is doing. Printed on the HUD by name, not by "
                "number - which is the reason it is a one-of-several fact and "
                "not an int.",
    },
    {
        "id": "fac71000000000003",
        "name": "world.power.load",
        "type": "float",
        "persist": "checkpoint",
        "default": 0.0,
        "desc": "How hard the plant is being pushed, 0..1. Checkpoint-lived: "
                "it belongs to the attempt, not to the save.",
    },
    {
        "id": "fac71000000000004",
        "name": "world.alarm.ringing",
        "type": "bool",
        "persist": "session",
        "scope": "scene",
        "desc": "Scene-scoped on purpose: an alarm is a property of the room "
                "you are in, and reloading the level should silence it.",
    },
    {
        "id": "fac71000000000005",
        "name": "characters.marta.trust",
        "type": "int",
        "persist": "save",
        "desc": "0..10. Rescuing her is worth 5 of it.",
    },
    {
        "id": "fac71000000000006",
        "name": "characters.marta.rescued",
        "type": "bool",
        "persist": "save",
        "desc": "The player got Marta out of the flooded room.",
    },
    {
        "id": "fac71000000000007",
        "name": "characters.marta.isAlly",
        "type": "bool",
        "persist": "session",
        "computed": "MartaIsAlly",
        "desc": "COMPUTED - derived from her trust and whether she was "
                "rescued. Nothing can write to it, which is the point: an "
                "alliance should never be settable behind the back of the "
                "things that decide it.",
    },
    {
        "id": "fac71000000000008",
        "name": "player.hasBasementKey",
        "type": "bool",
        "persist": "save",
        "desc": "Picked up in the yard.",
    },
    {
        "id": "fac71000000000009",
        "name": "player.lastCheckpoint",
        "type": "position",
        "persist": "save",
        "pos": [0.0, 0.0, 0.0],
        "desc": "Where to put the player back. A position fact - three floats "
                "in their own runtime array.",
    },
    {
        "id": "fac7100000000000a",
        "name": "profile.timesPlayed",
        "type": "int",
        "persist": "profile",
        "desc": "PROFILE-lived: one file per memory card, outside the save "
                "slots and shared by all of them. Survives starting a new "
                "game, which is exactly what an unlock or a play counter "
                "wants.",
    },
]

# --- the queries -------------------------------------------------------------
# Named conditions. CanEnterBasement names MartaIsAlly, so the nesting the
# codegen and the "Why?" explanation both have to handle is exercised.
QUERIES = [
    {
        "name": "GeneratorReady",
        "desc": "All three parts are in.",
        "when": {
            "kind": "compare",
            "fact": "world.generator.parts",
            "cmp": "ge",
            "value": 3,
        },
    },
    {
        "name": "MartaIsAlly",
        "desc": "She was rescued AND trusts the player enough.",
        "when": {
            "kind": "all",
            "children": [
                {"kind": "compare", "fact": "characters.marta.rescued",
                 "cmp": "eq", "value": 1},
                {"kind": "compare", "fact": "characters.marta.trust",
                 "cmp": "ge", "value": 5},
            ],
        },
    },
    {
        "name": "CanEnterBasement",
        "desc": "The key, plus either working power or Marta vouching for you.",
        "when": {
            "kind": "all",
            "children": [
                {"kind": "compare", "fact": "player.hasBasementKey",
                 "cmp": "eq", "value": 1},
                {
                    "kind": "any",
                    "children": [
                        {"kind": "compare", "fact": "world.power.state",
                         "cmp": "eq", "value": 1},
                        {"kind": "query", "query": "MartaIsAlly"},
                    ],
                },
            ],
        },
    },
]

# --- the rules ---------------------------------------------------------------
# One of each policy, and one that sends an event into a graph.
RULES = [
    {
        "name": "PowerRestored",
        "desc": "Fitting the last part powers the plant and tells the lamp.",
        "policy": "rising",
        "when": {"kind": "query", "query": "GeneratorReady"},
        "then": [
            {"do": "set", "target": "world.power.state", "value": 1},
            {"do": "event", "target": "power-on", "value": 0},
        ],
    },
    {
        "name": "OverloadTrips",
        "desc": "Pushed past 80% with the plant running, it trips.",
        "policy": "rising",
        "when": {
            "kind": "all",
            "children": [
                {"kind": "compare", "fact": "world.power.state",
                 "cmp": "eq", "value": 1},
                {"kind": "compare", "fact": "world.power.load",
                 "cmp": "gt", "value": 0.8},
            ],
        },
        "then": [
            {"do": "set", "target": "world.power.state", "value": 2},
        ],
    },
    {
        "name": "AlarmWhileOverloaded",
        "desc": "Every frame while overloaded - the alarm has to be "
                "re-asserted, which is the one shape 'every frame' is right "
                "for.",
        "policy": "while",
        "when": {"kind": "compare", "fact": "world.power.state",
                 "cmp": "eq", "value": 2},
        "then": [
            {"do": "set", "target": "world.alarm.ringing", "value": 1},
        ],
    },
    {
        "name": "RescueEarnsTrust",
        "desc": "Once per run: getting her out is worth 5 trust, and only the "
                "first time.",
        "policy": "once",
        "when": {"kind": "compare", "fact": "characters.marta.rescued",
                 "cmp": "eq", "value": 1},
        "then": [
            {"do": "add", "target": "characters.marta.trust", "value": 5},
        ],
    },
]

# --- test scenarios ----------------------------------------------------------
SCENARIOS = [
    {
        "name": "Endgame",
        "desc": "Everything done: plant powered, Marta rescued and trusting, "
                "key in hand.",
        "values": [
            {"fact": "world.generator.parts", "value": 3},
            {"fact": "world.power.state", "value": 1},
            {"fact": "characters.marta.rescued", "value": 1},
            {"fact": "characters.marta.trust", "value": 8},
            {"fact": "player.hasBasementKey", "value": 1},
        ],
    },
    {
        "name": "Locked out",
        "desc": "The awkward one: the key but nothing else, so the basement "
                "query is false and you can see WHY.",
        "values": [
            {"fact": "world.generator.parts", "value": 1},
            {"fact": "world.power.state", "value": 0},
            {"fact": "characters.marta.rescued", "value": 0},
            {"fact": "characters.marta.trust", "value": 2},
            {"fact": "player.hasBasementKey", "value": 1},
        ],
    },
    {
        "name": "Overloaded",
        "desc": "Powered and pushed past the trip point - the alarm rule and "
                "the scene-scoped fact in action.",
        "values": [
            {"fact": "world.generator.parts", "value": 3},
            {"fact": "world.power.state", "value": 2},
            {"fact": "world.power.load", "value": 0.95},
        ],
    },
]


# --- the graphs --------------------------------------------------------------

def part_graph():
    """A generator part: walk into it, it counts itself in and disappears.

    Set Fact's `add` pin is the whole read-modify-write - no Get, no Math.
    """
    g = G()
    near = g.n("NearObject", 0, 0, num=[2.5, 0, 0, 0])
    once = g.n("DoOnce", 1, 0)
    seq = g.n("Sequence", 2, 0)
    add = g.n("SetFact", 3, 0, str_="world.generator.parts", num=[1, 0, 0, 0])
    hide = g.n("SetObjectVisible", 3, 1)
    g.link(near, once)
    g.link(once, seq)
    g.link(seq, add, pin=1)          # exec input 1 = "add"
    g.link(seq, hide, fpin=1, pin=1)  # Sequence output 1 -> "hide"
    return g.out()


def marta_graph():
    """Use her once to rescue her. The trust that follows is a RULE's job, not
    this graph's - which is the division the whole system is for.

    Note the Sequence: a plain ACTION has no "then" output at all (only the
    Flow-category nodes and execThrough ones do - flowExecOutCount), so two
    actions are SIBLINGS of a Sequence rather than a chain. Wiring Set Fact
    straight into Display Text produces a link to a pin that does not exist:
    the editor draws nothing and codegen emits nothing, which reads exactly
    like "the node is never triggered".
    """
    g = G()
    used = g.n("OnUsed", 0, 0)
    once = g.n("DoOnce", 1, 0)
    seq = g.n("Sequence", 2, 0)
    setf = g.n("SetFact", 3, 0, str_="characters.marta.rescued",
               num=[1, 0, 0, 0])
    say = g.n("DisplayText", 3, 1, str_="", str2="Marta: I owe you one.",
              num=[0.5, 0.18, 16, 3])
    g.link(used, once)
    g.link(once, seq)
    g.link(seq, setf)
    g.link(seq, say, fpin=1)
    return g.out()


def key_graph():
    g = G()
    near = g.n("NearObject", 0, 0, num=[2.0, 0, 0, 0])
    once = g.n("DoOnce", 1, 0)
    seq = g.n("Sequence", 2, 0)
    setf = g.n("SetFact", 3, 0, str_="player.hasBasementKey", num=[1, 0, 0, 0])
    hide = g.n("SetObjectVisible", 3, 1)
    g.link(near, once)
    g.link(once, seq)
    g.link(seq, setf)
    g.link(seq, hide, fpin=1, pin=1)
    return g.out()


def lamp_graph():
    """Every reactive door into a graph, on one object.

    On Event is what a RULE can reach directly. On Fact Changed needs no rule
    at all, and its three outputs are the point: "changed" fires on any move
    (the HUD line), while "became true" / "became false" are the crossings of
    zero - which is what "the alarm started" and "the alarm stopped" actually
    are. The alarm is the honest demonstration because the two edges have
    different authors: the AlarmWhileOverloaded RULE raises it, the reset
    switch's Clear Fact lowers it, and this graph does not care which.

    Note that two actions can hang off ONE trigger output directly - a trigger
    fans out, unlike an action, which is why only the action-to-action cases
    below need a Sequence.
    """
    g = G()
    on = g.n("OnEvent", 0, 0, str_="power-on")
    green = g.n("SetObjectColor", 1, 0, num=[0.3, 1.0, 0.4, 0])
    changed = g.n("OnFactChanged", 0, 1, str_="world.power.state")
    show = g.n("DisplayText", 1, 1, str_="", str2="Plant: ",
               num=[0.5, 0.10, 16, 3])
    text = g.n("GetFactText", 0, 2, str_="world.power.state")
    alarm = g.n("OnFactChanged", 0, 3, str_="world.alarm.ringing")
    red = g.n("SetObjectColor", 1, 3, num=[1.0, 0.15, 0.15, 0])
    shout = g.n("DisplayText", 2, 3, str_="", str2="ALARM - plant overloaded",
                num=[0.5, 0.34, 18, 4])
    calm = g.n("SetObjectColor", 1, 4, num=[0.3, 1.0, 0.4, 0])
    g.link(on, green)
    g.link(changed, show)
    g.link(text, show, kind="text")
    # exec output 1 = "became true", 2 = "became false"
    g.link(alarm, red, fpin=1)
    g.link(alarm, shout, fpin=1)
    g.link(alarm, calm, fpin=2)
    return g.out()


def door_graph():
    """The basement door: opens exactly while the named query says so.

    Nothing here restates the condition - CanEnterBasement is authored once in
    the Queries tab and this graph asks it. That is the difference the queries
    exist to make.
    """
    g = G()
    q = g.n("FactQuery", 0, 0, str_="CanEnterBasement")
    cond = g.n("OnCondition", 1, 0)
    seq = g.n("Sequence", 2, 0)
    move = g.n("MoveObjectTo", 3, 0, num=[0, -4, 0, 3])
    say = g.n("DisplayText", 3, 1, str_="", str2="The basement door unlocks.",
              num=[0.5, 0.26, 16, 3])
    g.link(q, cond, kind="bool")
    g.link(cond, seq)
    g.link(seq, move)
    g.link(seq, say, fpin=1)
    return g.out()


def generator_graph():
    """Drives the load fact so the overload rule has something to react to,
    and shows the number plane feeding a fact."""
    g = G()
    used = g.n("OnUsed", 0, 0)
    add = g.n("SetFact", 1, 0, str_="world.power.load", num=[0.25, 0, 0, 0])
    g.link(used, add, pin=1)  # "add"
    return g.out()


def overload_switch_graph():
    """A reset: Clear Fact puts the load and the state back to the values the
    CATALOG declares, rather than to numbers retyped here."""
    g = G()
    used = g.n("OnUsed", 0, 0)
    seq = g.n("Sequence", 1, 0)
    c1 = g.n("ClearFact", 2, 0, str_="world.power.load")
    c2 = g.n("ClearFact", 2, 1, str_="world.alarm.ringing")
    g.link(used, seq)
    g.link(seq, c1)
    g.link(seq, c2, fpin=1)
    return g.out()


def hud_graph():
    """Counts the boot in (profile tier) and prints the parts count."""
    g = G()
    start = g.n("OnStart", 0, 0)
    bump = g.n("SetFact", 1, 0, str_="profile.timesPlayed", num=[1, 0, 0, 0])
    changed = g.n("OnFactChanged", 0, 1, str_="world.generator.parts")
    show = g.n("DisplayText", 1, 1, str_="", str2="Parts fitted: ",
               num=[0.5, 0.02, 16, 3])
    text = g.n("GetFactText", 0, 2, str_="world.generator.parts")
    g.link(start, bump, pin=1)  # "add"
    g.link(changed, show)
    g.link(text, show, kind="text")
    return g.out()


def checkpoint_graph():
    """A position fact: remember where the player was standing."""
    g = G()
    near = g.n("NearObject", 0, 0, num=[2.0, 0, 0, 0])
    cool = g.n("Cooldown", 1, 0, num=[5, 0, 0, 0])
    setp = g.n("SetFactPos", 2, 0, str_="player.lastCheckpoint")
    ppos = g.n("PlayerPos", 1, 1)
    g.link(near, cool)
    g.link(cool, setp)
    g.link(ppos, setp, kind="pos")
    return g.out()


# --- assembly ----------------------------------------------------------------

def main():
    objects = [
        obj("part-a", "part-a", "sphere", (-8, 1, -6), (0.9, 0.7, 0.2),
            (0.6, 0.6, 0.6), part_graph(), collision="none"),
        obj("part-b", "part-b", "sphere", (6, 1, -9), (0.9, 0.7, 0.2),
            (0.6, 0.6, 0.6), part_graph(), collision="none"),
        obj("part-c", "part-c", "sphere", (10, 1, 7), (0.9, 0.7, 0.2),
            (0.6, 0.6, 0.6), part_graph(), collision="none"),
        obj("key", "basement-key", "box", (-12, 1, 8), (0.85, 0.85, 0.3),
            (0.4, 0.4, 0.4), key_graph(), collision="none"),
        obj("marta", "marta", "cylinder", (-4, 1, 10), (0.8, 0.4, 0.6),
            (0.8, 2.0, 0.8), marta_graph(), usable=True),
        obj("generator", "generator", "box", (0, 1.2, -12), (0.5, 0.5, 0.55),
            (3, 2.4, 2), generator_graph(), usable=True),
        obj("overload-switch", "reset-switch", "box", (3.5, 1, -12),
            (0.8, 0.25, 0.25), (0.5, 0.8, 0.5), overload_switch_graph(),
            usable=True),
        obj("lamp", "status-lamp", "sphere", (0, 3.4, -12), (0.7, 0.2, 0.2),
            (0.7, 0.7, 0.7), lamp_graph(), collision="none"),
        obj("basement-door", "basement-door", "box", (14, 2, 0),
            (0.35, 0.3, 0.28), (0.6, 4, 5), door_graph()),
        obj("checkpoint", "checkpoint-flag", "cone", (0, 1, 4),
            (0.4, 0.8, 0.9), (0.6, 1.4, 0.6), checkpoint_graph(),
            collision="none"),
        obj("hud", "hud-logic", "empty", (0, 0, 12), (0.5, 0.5, 0.5),
            (1, 1, 1), hud_graph(), collision="none"),
    ]

    for o in objects:
        with open(os.path.join(OBJ, o["id"] + ".json"), "w",
                  encoding="utf-8") as f:
            json.dump(o, f)
            f.write("\n")

    manifest = json.load(open(TYRA, encoding="utf-8"))
    # Keep whatever the scaffold seeded (the Player object) and append ours.
    seeded = [i for i in manifest["scenes"][0].get("objects", [])
              if i not in IDS.values()]
    manifest["scenes"][0]["objects"] = seeded + [o["id"] for o in objects]
    manifest["facts"] = FACTS
    manifest["factQueries"] = QUERIES
    manifest["factRules"] = RULES
    manifest["factScenarios"] = SCENARIOS
    # The blackboard reads the running game through the Live Debugger channel,
    # so an example whose point is the blackboard ships with it on.
    manifest["settings"]["buildProfile"] = "debug"
    manifest["settings"]["liveDebug"] = True
    with open(TYRA, "w", encoding="utf-8") as f:
        json.dump(manifest, f, indent=2)
        f.write("\n")
    print("scene written: %d objects, %d facts, %d queries, %d rules" %
          (len(objects), len(FACTS), len(QUERIES), len(RULES)))


if __name__ == "__main__":
    main()
