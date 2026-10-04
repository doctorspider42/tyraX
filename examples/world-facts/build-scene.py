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
  * rule ORDER inside a pass  - PowerSettles is listed BEFORE the alarm rule
                               on purpose (see its note)
  * a rule that SENDS AN EVENT into a graph
  * the reactive trigger     - On Fact Changed
  * Get Fact As Text         - the one-of-several fact printed by NAME
  * state that must OUTLIVE   - node state (Do Once, Near Object's edge latch,
    THE SCENE                  On Condition's edge flag) AND object state
                               (visibility, colour, position) both reset on
                               every scene load, so anything that must hold
                               once per SAVE or once per RUN is a fact instead.
                               Four things in here learned that the hard way by
                               firing again on the walk back up from the
                               basement: the door's announcement, the boot
                               counter, and the parts and the key, which
                               respawned and let themselves be collected twice

The level is also a chain rather than a pile: three parts make the generator
startable, STARTING it (a second, deliberate act) powers the plant, the plant
running is what lets Marta be pumped out, and only then does she vouch for
anyone. Each link in that chain is a named query, so the whole progression is
readable in the Catalog without opening a single graph.

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
    # --- the basement, scene 2
    "b-player": "fac71100000000a0",
    "b-floor": "fac71100000000a1",
    "b-status": "fac71100000000a2",
    "b-savepoint": "fac71100000000a3",
    "b-ledger": "fac71100000000a4",
    "b-exit": "fac71100000000a5",
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


# --- where the lines go ------------------------------------------------------
# Every Display Text node owns its OWN runtime slot and draws exactly where it
# is told, so two nodes sharing a Y overlap - which is what happened when
# Marta's reply landed on top of her own "please help me". The engine has no
# shared subtitle stream, so the level keeps one table of screen rows and every
# graph places its text on one of them. Rows are reused across the two scenes
# (only the running scene's texts exist at all), never within one.
LINE_PARTS = 0.02   # hud-logic: parts fitted
LINE_PLANT = 0.08   # status-lamp: the plant's state by name / basement arrival
LINE_GEN = 0.14     # generator: what it just did / basement: the alarm readout
LINE_LOAD = 0.20    # generator: the load figure / basement ledger, first line
LINE_DOOR = 0.26    # basement door: it unlocked / basement ledger, second line
LINE_USE = 0.32     # basement door: what using it did / "saved to slot 1"
LINE_ALARM = 0.40   # the alarm shout
LINE_SPEECH = 0.82  # the bottom row: anything a character SAYS

# ...and how much fits on one. Display Text draws ONE centred line with no
# wrapping and no clamping: too long and it runs off both edges of the screen,
# and past 63 characters the runtime's per-slot buffer truncates it silently.
# The default font is 9 px per glyph at size 16, the picture is 512 across, and
# text belongs inside the TITLE-SAFE 80% (docs/safe-areas.md) - so 410/9 = 45
# characters, counting whatever a wired Get Fact As Text will add.
LINE_CHARS = 45

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
        "id": "fac7100000000000b",
        "name": "world.basementVisits",
        "type": "int",
        "persist": "save",
        "desc": "How many times THIS SAVE has been down to the basement. The "
                "per-slot counterpart of profile.timesPlayed: load an older "
                "slot and this comes back with it, while timesPlayed keeps "
                "counting whatever you load.",
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
    {
        "id": "fac7100000000000c",
        "name": "world.generator.started",
        "type": "bool",
        "persist": "save",
        "desc": "The player pulled the generator over WITH all three parts "
                "fitted. Separate from the parts count on purpose: fitting "
                "the last part makes the plant startABLE, and this is the "
                "player's own act of starting it.",
    },
    {
        "id": "fac7100000000000d",
        "name": "world.basementDoorOpened",
        "type": "bool",
        "persist": "save",
        "desc": "The player has already been told the basement door unlocked. "
                "This is a FACT rather than a Do Once because a Do Once dies "
                "with the scene: coming back up from the basement rebuilds "
                "the door's graph, and the announcement would play again "
                "every single time.",
    },
    {
        "id": "fac71000000000010",
        "name": "world.generator.partA",
        "type": "bool",
        "persist": "save",
        "desc": "The first sphere is gone. One per part, because 'how many are "
                "in' cannot answer 'is THIS one still lying in the yard' - and "
                "an object's visibility is SCENE state that resets on every "
                "load, so without these the parts respawned (and re-counted "
                "themselves) every time the player came up from the basement. "
                "The count beside them is the progress figure everything else "
                "reads; these three are what the world remembers about the "
                "objects.",
    },
    {
        "id": "fac71000000000011",
        "name": "world.generator.partB",
        "type": "bool",
        "persist": "save",
        "desc": "The second sphere is gone. See world.generator.partA.",
    },
    {
        "id": "fac71000000000012",
        "name": "world.generator.partC",
        "type": "bool",
        "persist": "save",
        "desc": "The third sphere is gone. See world.generator.partA.",
    },
    {
        "id": "fac7100000000000e",
        "name": "world.bootCounted",
        "type": "bool",
        "persist": "session",
        "desc": "SESSION-lived, world-scoped: this run has already counted "
                "itself into profile.timesPlayed. On Start runs on every "
                "scene load, so without it a trip to the basement and back "
                "would count as two more boots - and 'session' is exactly "
                "the tier for 'has this RUN already done X'.",
    },
]

# --- the queries -------------------------------------------------------------
# Named conditions. CanEnterBasement names MartaIsAlly, so the nesting the
# codegen and the "Why?" explanation both have to handle is exercised.
QUERIES = [
    {
        "name": "GeneratorReady",
        "desc": "All three parts are in, so the thing can be STARTED. Not the "
                "same as running.",
        "when": {
            "kind": "compare",
            "fact": "world.generator.parts",
            "cmp": "ge",
            "value": 3,
        },
    },
    {
        "name": "GeneratorRunning",
        "desc": "All three parts in AND the player pulled it over. The gate "
                "the whole level hangs off.",
        "when": {
            "kind": "all",
            "children": [
                {"kind": "query", "query": "GeneratorReady"},
                {"kind": "compare", "fact": "world.generator.started",
                 "cmp": "eq", "value": 1},
            ],
        },
    },
    {
        "name": "PowerOnline",
        "desc": "The plant is actually delivering - not broken, not tripped. "
                "Named once and asked by three different things (the pump "
                "that frees Marta, the basement door, the door's refusal "
                "message), which is the whole argument for a query over a "
                "compare copied into three graphs.",
        "when": {
            "kind": "compare",
            "fact": "world.power.state",
            "cmp": "eq",
            "value": 1,
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
        "desc": "The key, plus either working power or Marta vouching for "
                "you. The second half is not decoration: Marta can only be "
                "got out while the plant runs, so an ally is proof the power "
                "worked ONCE - which is what keeps the door openable after "
                "the plant trips.",
        "when": {
            "kind": "all",
            "children": [
                {"kind": "compare", "fact": "player.hasBasementKey",
                 "cmp": "eq", "value": 1},
                {
                    "kind": "any",
                    "children": [
                        {"kind": "query", "query": "PowerOnline"},
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
        "desc": "Starting a fully assembled generator powers the plant and "
                "tells the lamp. It watches GeneratorRunning rather than "
                "GeneratorReady, so the last part makes the thing startable "
                "and the PLAYER makes it run.",
        "policy": "rising",
        "when": {"kind": "query", "query": "GeneratorRunning"},
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
        "name": "PowerSettles",
        "desc": "The load came off a tripped plant, so it comes back. Without "
                "this the world has a DEAD END: nothing else can leave the "
                "Overloaded state, so CanEnterBasement - which wants Powered - "
                "could never become true a second time.",
        "policy": "rising",
        "when": {
            "kind": "all",
            "children": [
                {"kind": "compare", "fact": "world.power.state",
                 "cmp": "eq", "value": 2},
                {"kind": "compare", "fact": "world.power.load",
                 "cmp": "le", "value": 0.1},
            ],
        },
        "then": [
            {"do": "set", "target": "world.power.state", "value": 1},
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
            {"fact": "world.generator.partA", "value": 1},
            {"fact": "world.generator.partB", "value": 1},
            {"fact": "world.generator.partC", "value": 1},
            {"fact": "world.generator.started", "value": 1},
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
            {"fact": "world.generator.started", "value": 0},
            {"fact": "world.power.state", "value": 0},
            {"fact": "characters.marta.rescued", "value": 0},
            {"fact": "characters.marta.trust", "value": 2},
            {"fact": "player.hasBasementKey", "value": 1},
        ],
    },
    {
        "name": "Parts but no spark",
        "desc": "All three parts fitted and the generator never pulled over: "
                "GeneratorReady is true, GeneratorRunning is false, and every "
                "gate downstream - the pump that frees Marta, the basement "
                "door - is shut. The scenario for reading the difference "
                "between 'can be started' and 'running' in the Why? panel.",
        "values": [
            {"fact": "world.generator.parts", "value": 3},
            {"fact": "world.generator.partA", "value": 1},
            {"fact": "world.generator.partB", "value": 1},
            {"fact": "world.generator.partC", "value": 1},
            {"fact": "world.generator.started", "value": 0},
            {"fact": "world.power.state", "value": 0},
            {"fact": "player.hasBasementKey", "value": 1},
        ],
    },
    {
        "name": "Overloaded",
        "desc": "Powered and pushed past the trip point - the alarm rule and "
                "the scene-scoped fact in action.",
        "values": [
            {"fact": "world.generator.parts", "value": 3},
            {"fact": "world.generator.partA", "value": 1},
            {"fact": "world.generator.partB", "value": 1},
            {"fact": "world.generator.partC", "value": 1},
            {"fact": "world.generator.started", "value": 1},
            {"fact": "world.power.state", "value": 2},
            {"fact": "world.power.load", "value": 0.95},
        ],
    },
]


# --- the graphs --------------------------------------------------------------

def part_graph(fact):
    """A generator part: walk into it, it counts itself in and disappears.

    Set Fact's `add` pin is the whole read-modify-write - no Get, no Math.

    The one-shot is the FACT, not a Do Once. Near Object is edge-triggered and
    its latch, like a Do Once's, resets on scene load - so the first version of
    this graph gave the player the part again (and counted it again) every time
    they walked back up from the basement, because a hidden object comes back
    visible and the latches came back armed. Asking the fact costs the same and
    is true for the life of the save.

    On Start is the other half: the sphere has to take itself off the field
    again on a scene it was already collected in. Both branches fire the SAME
    Set Object Visible node, which is what an exec input is for.
    """
    g = G()
    near = g.n("NearObject", 0, 0, num=[2.5, 0, 0, 0])
    fresh = g.n("Branch", 1, 0)
    taken = g.n("GetFactBool", 0, 1, str_=fact)
    notTaken = g.n("Not", 1, 1)
    seq = g.n("Sequence", 2, 0)
    mark = g.n("SetFact", 3, 0, str_=fact, num=[1, 0, 0, 0])
    add = g.n("SetFact", 3, 1, str_="world.generator.parts", num=[1, 0, 0, 0])
    hide = g.n("SetObjectVisible", 3, 2)
    start = g.n("OnStart", 0, 3)
    already = g.n("Branch", 1, 3)
    taken2 = g.n("GetFactBool", 0, 4, str_=fact)
    g.link(taken, notTaken, kind="bool")
    g.link(notTaken, fresh, kind="bool")
    g.link(near, fresh)
    g.link(fresh, seq)
    g.link(seq, mark)
    g.link(seq, add, fpin=1, pin=1)   # exec input 1 = "add"
    g.link(seq, hide, fpin=2, pin=1)  # exec input 1 = "hide"
    g.link(taken2, already, kind="bool")
    g.link(start, already)
    g.link(already, hide, pin=1)      # collected in an earlier visit
    return g.out()


def marta_graph():
    """Three different things to say, chosen by two facts.

    Before she is out she asks for help - on PROXIMITY, so it lands without the
    player having to guess she is interactive. Using her frees her, but only
    while the plant is delivering: the room is flooded and the pump that drains
    it runs off the mains, so the rescue asks PowerOnline - the same named
    query the basement door asks - and says so when the answer is no. After she
    is out, using her thanks the player instead.

    Two Branches on two facts are the whole state machine: the graph holds no
    state of its own, which is the argument for facts in one picture.

    The trust that follows the rescue is still a RULE's job, not this graph's.

    Every line she has is on LINE_SPEECH, and the first thing 'use' does is
    HIDE the two that may still be up - a Display Text node draws until its
    seconds run out or its 'hide' pin fires, and nothing hides one for you. Two
    strings on the same row at the same moment is how her reply used to land on
    top of "Please - the water is rising".
    """
    g = G()
    # "please help" while she is still trapped - throttled, or it would re-arm
    # every frame the player stands next to her.
    near = g.n("NearObject", 0, 0, num=[4.0, 0, 0, 0])
    cool = g.n("Cooldown", 1, 0, num=[6.0, 0, 0, 0])
    plead = g.n("Branch", 2, 0)
    notYet = g.n("Not", 1, 1)
    isOut = g.n("GetFactBool", 0, 1, str_="characters.marta.rescued")
    beg = g.n("DisplayText", 3, 0, str_="",
              str2="Marta: Please - the water is rising. Help me!",
              num=[0.5, LINE_SPEECH, 16, 4])
    g.link(near, cool)
    g.link(cool, plead)
    g.link(isOut, notYet, kind="bool")
    g.link(notYet, plead, kind="bool")
    g.link(plead, beg)  # exec output 0 = "true"

    # Using her. The row is cleared first, THEN one of the three lines is
    # shown - a hide and a show of the same slot in one frame ends up shown.
    used = g.n("OnUsed", 0, 3)
    hush = g.n("Sequence", 1, 3)
    which = g.n("Branch", 2, 3)
    already = g.n("GetFactBool", 1, 4, str_="characters.marta.rescued")
    thanks = g.n("DisplayText", 3, 3, str_="",
                 str2="Marta: I owe you one. The key is in the yard.",
                 num=[0.5, LINE_SPEECH, 16, 4])
    canFree = g.n("Branch", 3, 5)
    powered = g.n("FactQuery", 2, 6, str_="PowerOnline")
    seq = g.n("Sequence", 4, 5)
    rescue = g.n("SetFact", 5, 5, str_="characters.marta.rescued",
                 num=[1, 0, 0, 0])
    relief = g.n("DisplayText", 5, 6, str_="",
                 str2="Marta: Thank you. I thought that was it.",
                 num=[0.5, LINE_SPEECH, 16, 4])
    jammed = g.n("DisplayText", 4, 7, str_="",
                 str2="Marta: The pump is dead. Get the power on.",
                 num=[0.5, LINE_SPEECH, 16, 5])
    g.link(used, hush)
    g.link(hush, beg, pin=1)         # 1 -> hide the plea
    g.link(hush, jammed, fpin=1, pin=1)  # 2 -> hide the refusal
    g.link(hush, which, fpin=2)      # 3 -> now decide what she says
    g.link(already, which, kind="bool")
    g.link(which, thanks)            # true  -> she is already out
    g.link(which, canFree, fpin=1)   # false -> can she be got out yet?
    g.link(powered, canFree, kind="bool")
    g.link(canFree, seq)             # true  -> the pump runs
    g.link(canFree, jammed, fpin=1)  # false -> and it does not
    g.link(seq, rescue)
    g.link(seq, relief, fpin=1)
    return g.out()


def key_graph():
    """The same shape as a generator part, and it needs no fact of its own:
    player.hasBasementKey already IS "this thing is gone", so the pickup asks
    it and On Start asks it again on the way back into the scene."""
    g = G()
    near = g.n("NearObject", 0, 0, num=[2.0, 0, 0, 0])
    fresh = g.n("Branch", 1, 0)
    has = g.n("GetFactBool", 0, 1, str_="player.hasBasementKey")
    notHas = g.n("Not", 1, 1)
    seq = g.n("Sequence", 2, 0)
    setf = g.n("SetFact", 3, 0, str_="player.hasBasementKey", num=[1, 0, 0, 0])
    hide = g.n("SetObjectVisible", 3, 1)
    start = g.n("OnStart", 0, 3)
    already = g.n("Branch", 1, 3)
    has2 = g.n("GetFactBool", 0, 4, str_="player.hasBasementKey")
    g.link(has, notHas, kind="bool")
    g.link(notHas, fresh, kind="bool")
    g.link(near, fresh)
    g.link(fresh, seq)
    g.link(seq, setf)
    g.link(seq, hide, fpin=1, pin=1)
    g.link(has2, already, kind="bool")
    g.link(start, already)
    g.link(already, hide, pin=1)
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
               num=[0.5, LINE_PLANT, 16, 3])
    text = g.n("GetFactText", 0, 2, str_="world.power.state")
    alarm = g.n("OnFactChanged", 0, 3, str_="world.alarm.ringing")
    red = g.n("SetObjectColor", 1, 3, num=[1.0, 0.15, 0.15, 0])
    shout = g.n("DisplayText", 2, 3, str_="", str2="ALARM - plant overloaded",
                num=[0.5, LINE_ALARM, 18, 4])
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
    """Opens exactly while the named query says so - and when it does NOT, says
    which half is missing.

    Nothing here restates the condition: CanEnterBasement is authored once in
    the Queries tab and this graph asks it. The 'locked' branch then asks ONE
    more fact to tell "no key" apart from "no power and no ally", which is the
    cheap way to make a refusal informative instead of mysterious.

    The announcement is on its own row (LINE_DOOR) and the answers to USING it
    are on the next one (LINE_USE), so the frame the door unlocks under the
    player's hand does not print two strings over each other.
    """
    g = G()
    q = g.n("FactQuery", 0, 0, str_="CanEnterBasement")
    cond = g.n("OnCondition", 1, 0)
    # On Condition fires on the RISING edge, and its edge flag - like every
    # piece of node state - resets with the scene. Coming back up from the
    # basement therefore fires it again, which is right for the door (the
    # object is back at its authored position and has to be re-opened) and
    # wrong for the announcement, which used to play on every single return.
    # A save-lived FACT is what tells the two apart, because it is the one kind
    # of state that outlives the scene.
    firstTime = g.n("Branch", 2, 0)
    told = g.n("GetFactBool", 1, 2, str_="world.basementDoorOpened")
    notTold = g.n("Not", 2, 2)
    seq = g.n("Sequence", 3, 0)
    # It goes GREEN - it does not move. An earlier version slid the door into
    # the ground on unlock, which read as "open" and meant the object the
    # player has to USE was no longer there: the basement became unreachable
    # the moment it unlocked. A door that IS the switch has to stay put and
    # say so with its colour, the same way the status lamp does.
    lit = g.n("SetObjectColor", 4, 0, num=[0.4, 0.85, 0.45, 0])
    say = g.n("DisplayText", 4, 1, str_="", str2="The basement door unlocks.",
              num=[0.5, LINE_DOOR, 16, 3])
    remember = g.n("SetFact", 4, 2, str_="world.basementDoorOpened",
                   num=[1, 0, 0, 0])
    # Already unlocked when the player left. Colour is object state and resets
    # with the scene, so it has to be re-applied on the way back up - just
    # without the announcement, which is what the fact is for.
    relit = g.n("SetObjectColor", 3, 3, num=[0.4, 0.85, 0.45, 0])
    g.link(q, cond, kind="bool")
    g.link(told, notTold, kind="bool")
    g.link(notTold, firstTime, kind="bool")
    g.link(cond, firstTime)
    g.link(firstTime, seq)
    g.link(firstTime, relit, fpin=1)
    g.link(seq, lit)
    g.link(seq, say, fpin=1)
    g.link(seq, remember, fpin=2)

    # Using it: through it when open, an explanation when not.
    used = g.n("OnUsed", 0, 3)
    open_ = g.n("Branch", 1, 3)
    q2 = g.n("FactQuery", 0, 4, str_="CanEnterBasement")
    goSeq = g.n("Sequence", 2, 3)
    goSay = g.n("DisplayText", 3, 3, str_="", str2="Down into the basement.",
                num=[0.5, LINE_USE, 16, 2])
    go = g.n("SwitchScene", 3, 4, str_="basement")
    hasKey = g.n("Branch", 2, 5)
    keyFact = g.n("GetFactBool", 1, 6, str_="player.hasBasementKey")
    noPower = g.n("DisplayText", 3, 5, str_="",
                  str2="Locked: no power, and Marta will not vouch.",
                  num=[0.5, LINE_USE, 16, 4])
    noKey = g.n("DisplayText", 3, 6, str_="",
                str2="Locked: it needs the basement key.",
                num=[0.5, LINE_USE, 16, 4])
    g.link(q2, open_, kind="bool")
    g.link(used, open_)
    g.link(open_, goSeq)
    g.link(goSeq, goSay)
    g.link(goSeq, go, fpin=1)
    g.link(open_, hasKey, fpin=1)
    g.link(keyFact, hasKey, kind="bool")
    g.link(hasKey, noPower)          # has the key, so it is the other half
    g.link(hasKey, noKey, fpin=1)
    return g.out()


def generator_graph():
    """Three parts, then a pull, then it can be leaned on - in that order.

    Using it asks two questions before it does anything. GeneratorReady says
    whether the thing is even assembled; world.generator.started says whether
    the player has pulled it over. Only the third use onwards pushes the load
    up, which is what makes the trip/reset loop something you have to EARN
    rather than something the level hands you the moment the last sphere is
    picked up.

    Nothing here sets world.power.state: the PowerRestored rule watches
    GeneratorRunning and does that, which is why starting the plant needs no
    wire between this graph and the lamp.

    It also REPORTS, because a number nobody can see is a number nobody can
    debug. Two readouts once it runs: the plant's state by NAME (a
    one-of-several fact through Get Fact As Text) and the load as a figure
    (Get Fact -> Number To Text).
    """
    g = G()
    used = g.n("OnUsed", 0, 0)
    assembled = g.n("Branch", 1, 0)
    ready = g.n("FactQuery", 0, 1, str_="GeneratorReady")
    running = g.n("Branch", 2, 0)
    isOn = g.n("GetFactBool", 1, 2, str_="world.generator.started")

    # not assembled: say how far off it is, and touch nothing
    missing = g.n("DisplayText", 3, 7, str_="",
                  str2="Generator: dead. Parts fitted: ",
                  num=[0.5, LINE_GEN, 16, 4])
    partsText = g.n("GetFactText", 2, 8, str_="world.generator.parts")

    # assembled but cold: the pull. One Set Fact, and the RULE does the rest.
    startSeq = g.n("Sequence", 3, 4)
    start = g.n("SetFact", 4, 4, str_="world.generator.started",
                num=[1, 0, 0, 0])
    # A fixed string, not Get Fact As Text: the rules run in their own pass
    # AFTER the graphs, so world.power.state is still "Broken" on this line.
    # The lamp's On Fact Changed prints the new state a moment later, which is
    # the honest order and worth seeing.
    startSay = g.n("DisplayText", 4, 5, str_="",
                   str2="Generator: it turns over and catches.",
                   num=[0.5, LINE_GEN, 16, 4])

    # running: lean on it
    seq = g.n("Sequence", 3, 0)
    add = g.n("SetFact", 4, 0, str_="world.power.load", num=[0.25, 0, 0, 0])
    sayState = g.n("DisplayText", 4, 1, str_="", str2="Generator: ",
                   num=[0.5, LINE_GEN, 16, 3])
    stateText = g.n("GetFactText", 3, 2, str_="world.power.state")
    sayLoad = g.n("DisplayText", 4, 3, str_="", str2="Load: ",
                  num=[0.5, LINE_LOAD, 16, 3])
    loadNum = g.n("GetFact", 2, 3, str_="world.power.load")
    loadText = g.n("NumToTextFmt", 3, 3, num=[2, 1, 0, 0])

    g.link(ready, assembled, kind="bool")
    g.link(used, assembled)
    g.link(assembled, running)          # true  -> all three parts are in
    g.link(assembled, missing, fpin=1)  # false -> and how many are not
    g.link(partsText, missing, kind="text")
    g.link(isOn, running, kind="bool")
    g.link(running, seq)                # true  -> it is already turning
    g.link(running, startSeq, fpin=1)   # false -> start it
    g.link(startSeq, start)
    g.link(startSeq, startSay, fpin=1)
    g.link(seq, add, pin=1)          # "add"
    g.link(seq, sayState, fpin=1)
    g.link(seq, sayLoad, fpin=2)
    g.link(stateText, sayState, kind="text")
    g.link(loadNum, loadText, kind="number")
    g.link(loadText, sayLoad, kind="text")
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
    """Counts the boot in (profile tier) and prints the parts count.

    On Start runs on every SCENE load, not once per run, so the bump is gated
    on a session-lived fact - otherwise a trip down to the basement and back
    counted as two more boots and the ledger downstairs told a lie. Which tier
    a fact belongs to is the whole answer here: 'session' means exactly "this
    run has already done it", and it is the only tier that both survives the
    scene switch and forgets on relaunch.
    """
    g = G()
    start = g.n("OnStart", 0, 0)
    first = g.n("Branch", 1, 0)
    counted = g.n("GetFactBool", 0, 1, str_="world.bootCounted")
    notYet = g.n("Not", 1, 1)
    seq = g.n("Sequence", 2, 0)
    bump = g.n("SetFact", 3, 0, str_="profile.timesPlayed", num=[1, 0, 0, 0])
    mark = g.n("SetFact", 3, 1, str_="world.bootCounted", num=[1, 0, 0, 0])
    changed = g.n("OnFactChanged", 0, 3, str_="world.generator.parts")
    show = g.n("DisplayText", 1, 3, str_="", str2="Parts fitted: ",
               num=[0.5, LINE_PARTS, 16, 3])
    text = g.n("GetFactText", 0, 4, str_="world.generator.parts")
    g.link(counted, notYet, kind="bool")
    g.link(notYet, first, kind="bool")
    g.link(start, first)
    g.link(first, seq)
    g.link(seq, bump, pin=1)  # "add"
    g.link(seq, mark, fpin=1)
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


# --- the basement, scene 2 ---------------------------------------------------
# The reason there are two scenes at all: a WORLD-scoped fact has to be shown
# surviving the switch, and a SCENE-scoped one has to be shown not surviving it.


def basement_status_graph():
    """On arrival, print what came through the door with you.

    world.power.state is world-scoped, so it reads whatever the plant was left
    at. world.alarm.ringing is SCENE-scoped, so it is back to false however
    loudly it was ringing upstairs - that pair is the whole demonstration.
    """
    g = G()
    start = g.n("OnStart", 0, 0)
    seq = g.n("Sequence", 1, 0)
    visit = g.n("SetFact", 2, 0, str_="world.basementVisits", num=[1, 0, 0, 0])
    sayPlant = g.n("DisplayText", 2, 1, str_="",
                   str2="Basement. The plant is: ",
                   num=[0.5, LINE_PLANT, 16, 5])
    plantText = g.n("GetFactText", 1, 2, str_="world.power.state")
    sayAlarm = g.n("DisplayText", 2, 3, str_="",
                   str2="Alarm down here (scene-scoped): ",
                   num=[0.5, LINE_GEN, 16, 5])
    alarmBool = g.n("GetFactBool", 0, 4, str_="world.alarm.ringing")
    alarmText = g.n("BoolToText", 1, 4)
    g.link(start, seq)
    g.link(seq, visit, pin=1)        # "add"
    g.link(seq, sayPlant, fpin=1)
    g.link(seq, sayAlarm, fpin=2)
    g.link(plantText, sayPlant, kind="text")
    g.link(alarmBool, alarmText, kind="bool")
    g.link(alarmText, sayAlarm, kind="text")
    return g.out()


def basement_ledger_graph():
    """The per-slot / per-card difference, side by side.

    world.basementVisits is SAVE-lived, so it belongs to the slot: load an
    older save and it comes back with that save. profile.timesPlayed is
    PROFILE-lived, so it is the card's and keeps counting whatever you load.
    Reading them next to each other is the fastest way to feel the difference.
    """
    g = G()
    used = g.n("OnUsed", 0, 0)
    seq = g.n("Sequence", 1, 0)
    sayVisits = g.n("DisplayText", 2, 0, str_="",
                    str2="This save has been here: ",
                    num=[0.5, LINE_LOAD, 16, 5])
    visits = g.n("GetFactText", 1, 1, str_="world.basementVisits")
    sayPlays = g.n("DisplayText", 2, 2, str_="",
                   str2="This card has booted the game: ",
                   num=[0.5, LINE_DOOR, 16, 5])
    plays = g.n("GetFactText", 1, 3, str_="profile.timesPlayed")
    g.link(used, seq)
    g.link(seq, sayVisits)
    g.link(seq, sayPlays, fpin=1)
    g.link(visits, sayVisits, kind="text")
    g.link(plays, sayPlays, kind="text")
    return g.out()


def basement_save_graph():
    """Write the slot, and say so.

    Save Checkpoint takes the RAM snapshot (every checkpoint- and save-lived
    fact goes into it); Commit Checkpoint is the one node that touches the
    card. Splitting them is what lets a game checkpoint constantly and only
    pay for the card at a chapter break - see docs/save-editor.md.
    """
    g = G()
    used = g.n("OnUsed", 0, 0)
    seq = g.n("Sequence", 1, 0)
    snap = g.n("SaveCheckpoint", 2, 0)
    commit = g.n("CommitCheckpoint", 2, 1, str_="fixed", num=[0, 0, 0, 0])
    say = g.n("DisplayText", 2, 2, str_="",
              str2="Saved to slot 1 - facts included.",
              num=[0.5, LINE_USE, 16, 4])
    g.link(used, seq)
    g.link(seq, snap)
    g.link(seq, commit, fpin=1)
    g.link(seq, say, fpin=2)
    return g.out()


def basement_exit_graph():
    g = G()
    used = g.n("OnUsed", 0, 0)
    back = g.n("SwitchScene", 1, 0, str_="main")
    g.link(used, back)
    return g.out()


# --- assembly ----------------------------------------------------------------

def main():
    objects = [
        obj("part-a", "part-a", "sphere", (-8, 1, -6), (0.9, 0.7, 0.2),
            (0.6, 0.6, 0.6), part_graph("world.generator.partA"),
            collision="none"),
        obj("part-b", "part-b", "sphere", (6, 1, -9), (0.9, 0.7, 0.2),
            (0.6, 0.6, 0.6), part_graph("world.generator.partB"),
            collision="none"),
        obj("part-c", "part-c", "sphere", (10, 1, 7), (0.9, 0.7, 0.2),
            (0.6, 0.6, 0.6), part_graph("world.generator.partC"),
            collision="none"),
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
            (0.35, 0.3, 0.28), (0.6, 4, 5), door_graph(), usable=True),
        obj("checkpoint", "checkpoint-flag", "cone", (0, 1, 4),
            (0.4, 0.8, 0.9), (0.6, 1.4, 0.6), checkpoint_graph(),
            collision="none"),
        obj("hud", "hud-logic", "empty", (0, 0, 12), (0.5, 0.5, 0.5),
            (1, 1, 1), hud_graph(), collision="none"),
    ]

    # --- the basement. No terrain at all (docs/terrain.md): a scene can be
    # built without one, so the floor is an ordinary box and the void below it
    # is what the player would fall into without one.
    player2 = {
        "id": IDS["b-player"],
        "name": "player-2",
        "type": "player",
        "position": [0, 1, 8],
        "rotation": [0, 0, 0],
        "scale": [1, 1, 1],
        "color": [0.15, 0.9, 0.9],
        "physics": False,
        "player": {"mode": "walk", "walkSpeed": 0.1, "lookSpeed": 1,
                   "eyeHeight": 1.8, "jumpSpeed": 4.5, "canJump": True},
    }
    basement = [
        player2,
        obj("b-floor", "basement-floor", "box", (0, -0.25, 0),
            (0.22, 0.2, 0.24), (26, 0.5, 26)),
        obj("b-status", "arrival-logic", "empty", (0, 0, 0), (0.5, 0.5, 0.5),
            (1, 1, 1), basement_status_graph(), collision="none"),
        obj("b-ledger", "ledger", "cylinder", (-5, 1, 0), (0.4, 0.7, 0.9),
            (0.8, 2, 0.8), basement_ledger_graph(), usable=True),
        obj("b-savepoint", "save-point", "cone", (5, 1, 0), (0.95, 0.85, 0.3),
            (0.9, 2, 0.9), basement_save_graph(), usable=True),
        obj("b-exit", "stairs-up", "box", (0, 1.5, -11), (0.35, 0.3, 0.28),
            (5, 3, 0.6), basement_exit_graph(), usable=True),
    ]

    # Nothing warns about a line that does not fit - drawFontText centres one
    # unwrapped string and lets it run off both edges of the picture, and the
    # runtime buffer truncates it at 63 characters without a word. So the
    # authoring script checks, counting the widest value a wired reader can
    # add (an enum option name, or a couple of digits).
    widest = {"world.power.state": len("Overloaded"), "world.alarm.ringing": 5}
    for o in objects + basement:
        graph = o.get("flowGraph") or {}
        byId = {n["id"]: n for n in graph.get("nodes", [])}
        for n in graph.get("nodes", []):
            if n["type"] != "DisplayText":
                continue
            wired = [byId[l["from"]] for l in graph.get("links", [])
                     if l.get("text") and l["to"] == n["id"]]
            room = len(n.get("str2", "")) + sum(
                widest.get(w.get("str", ""), 3) for w in wired)
            if room > LINE_CHARS:
                raise SystemExit(
                    "%s: \"%s\" is %d characters and only %d fit on one "
                    "title-safe line" % (o["name"], n.get("str2", ""), room,
                                         LINE_CHARS))
    for o in objects + basement:
        with open(os.path.join(OBJ, o["id"] + ".json"), "w",
                  encoding="utf-8") as f:
            json.dump(o, f)
            f.write("\n")

    manifest = json.load(open(TYRA, encoding="utf-8"))
    # Keep whatever the scaffold seeded (the Player object) and append ours.
    seeded = [i for i in manifest["scenes"][0].get("objects", [])
              if i not in IDS.values()]
    manifest["scenes"][0]["objects"] = seeded + [o["id"] for o in objects]
    # Scene 2. It borrows scene 1's settings block wholesale - what this
    # example is about is the facts, not the lighting - but darkens the sky,
    # because it is a basement. `enabled: false` on the terrain is the
    # terrain-less path (docs/terrain.md); the floor object is why the player
    # does not fall through it.
    import copy
    base = copy.deepcopy(manifest["scenes"][0])
    base["name"] = "basement"
    base["terrain"] = {"width": 32, "depth": 32, "enabled": False}
    base["objects"] = [o["id"] for o in basement]
    base["settings"]["sky"] = {"color": [0.05, 0.05, 0.07],
                               "topColor": [0.02, 0.02, 0.04],
                               "dome": False, "zenithSize": 0.5}
    base["settings"]["lighting"]["ambient"] = 0.42
    manifest["scenes"] = [manifest["scenes"][0], base]
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
    print("written: %d + %d objects over 2 scenes, %d facts, %d queries, "
          "%d rules" % (len(objects), len(basement), len(FACTS), len(QUERIES),
                        len(RULES)))


if __name__ == "__main__":
    main()
