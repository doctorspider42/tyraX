# world-facts — the game's central memory, end to end

A small level whose entire logic is **World Facts**
([docs/world-facts.md](../../docs/world-facts.md)): three generator parts to
collect, an NPC to rescue, a locked basement, and a power state other things
react to. Nothing in it is a scratch variable — every piece of state is a
declared fact, and the conditions that would normally be copied into five
graphs are named queries.

It is a **chain**, not a pile. Three parts make the generator *startable*;
starting it — a second, deliberate act — powers the plant; the plant running
drives the pump that gets Marta out; and only once she is out does she vouch
for anyone. Every link is a named query, so the whole progression reads off
the **Catalog** tab without opening a single graph.

Open *Tools > World Facts* first. The **Catalog** tab is the design of the
level on one page.

## What it exercises

| | Where |
| --- | --- |
| Every fact **type** | `world.generator.parts` (whole number), `world.power.state` (one-of-several), `world.power.load` (number), `characters.marta.rescued` (yes/no), `player.lastCheckpoint` (position) |
| Every **persistence** tier | session (`world.alarm.ringing`, `world.bootCounted`), checkpoint (`world.power.load`), save game (most), profile (`profile.timesPlayed`) |
| Both **scopes** | world, plus one scene-scoped fact (`world.alarm.ringing`) |
| A **computed** fact | `characters.marta.isAlly` — derived, and nothing can write to it |
| **Nested queries** | `GeneratorRunning` names `GeneratorReady`; `CanEnterBasement` names `PowerOnline` **and** `MartaIsAlly` |
| One query, **several consumers** | `PowerOnline` is asked by the pump that frees Marta and by the basement door — the compare lives in one place, not in two graphs |
| State that must **outlive the scene** | node state *and* object state reset on every scene load, so "announce the door once", "count this boot once" and "this sphere is gone" are **facts**, not a *Do Once* and not a hidden object — see below |
| All three **rule policies** | becomes-true (`PowerRestored`), every-frame (`AlarmWhileOverloaded`), once-per-run (`RescueEarnsTrust`) |
| Rule **order** mattering | `PowerSettles` sits above the alarm rule on purpose, so the frame the plant recovers the alarm is not raised one more time |
| A rule that **sends an event** | `PowerRestored` → `power-on` → the status lamp's graph |
| **On Fact Changed**, all three outputs | the lamp's HUD line hangs off *changed*; the alarm's *became true* / *became false* are raised by a RULE and lowered by a graph's Clear Fact, and the lamp does not care which |
| **Get Fact As Text** | the plant's state prints as *Broken / Powered / Overloaded*, not as a number |
| **Scenarios** | *Endgame*, *Locked out*, *Parts but no spark*, *Overloaded* |
| Facts **across a scene switch** | the basement prints `world.power.state` (world-scoped, comes through the door with you) next to `world.alarm.ringing` (scene-scoped, back to false however loudly it was ringing upstairs) |
| **Per-slot vs per-card** | the basement's *ledger* reads `world.basementVisits` (save-lived, belongs to the slot) next to `profile.timesPlayed` (profile-lived, belongs to the card) |
| **Checkpoint vs card** | the *save-point* takes the RAM snapshot and commits it to slot 1 as two separate nodes |

## Playing it

Walk into the three yellow spheres — each fits a generator part (`Set Fact`
on the `add` pin, so no read is needed) and takes itself off the field. Each
one has its **own** `world.generator.partX` yes/no beside the count, because
"how many are in" cannot answer "is *this* one still lying in the yard", and
hiding an object is scene state that comes back on the next load. The
one-shot is that fact rather than a *Do Once* — `Near Object`'s edge latch
resets with the scene too, so without it the parts respawned and let
themselves be collected a second time on the way up from the basement.
`On Start` asks the same fact and hides the sphere again. The key does
exactly the same, and needs no new fact: `player.hasBasementKey` already *is*
"this thing is gone".

The third part makes `GeneratorReady` true — the generator can now be
**started**, not that it is running. Use it and it turns over: that writes
`world.generator.started`, `GeneratorRunning` becomes true, the
**PowerRestored** rule flips `world.power.state` to *Powered* and sends
`power-on`, and the status lamp turns green while the HUD prints the new
state by name. Use the generator before the parts are in and it just tells
you how many it has.

Two facts rather than one is the point: *"the last part went in"* and *"the
player started it"* are different events, and a level that conflates them
hands the player a milestone they never chose to reach.

Once it is running, use the generator to push `world.power.load` up by 0.25 a
time. Past 0.8 the **OverloadTrips** rule flips the state to *Overloaded*,
**AlarmWhileOverloaded** re-asserts the alarm every frame, and the lamp goes
red off *On Fact Changed*'s `became true`. The red switch beside the
generator clears the load and the alarm; the **PowerSettles** rule then
notices the load is off a tripped plant and brings it back to *Powered* —
which makes the whole loop replayable and lets the basement door's
`On Condition` fire more than once in a run.

Everything that can be used **reports**, because a fact nobody can see is a
fact nobody can debug. The generator prints its state by name and its load as
a figure. The door says why it is shut, and tells *"it needs the key"* apart
from *"the plant is dead and Marta does not vouch for you"* by asking one
more fact.

Marta asks for help as you come near. Getting her out needs the plant
**running** — her room is flooded and the pump is on the mains — so using her
asks the `PowerOnline` query and says so when the answer is no. Once she is
out, using her thanks you instead. Two `Branch` nodes on two conditions are
that entire state machine: the graph holds no state of its own, which is the
argument for facts in one picture. Rescuing her is worth 5 trust, and that
comes from the **RescueEarnsTrust** rule rather than from her graph.

Every line she has sits on the same screen row, and the first thing *use*
does is fire the **hide** pin of the two that may still be up. A *Display
Text* node draws until its seconds run out and nothing hides one for you —
two strings on one row at one moment is how her reply used to land on top of
*"Please — the water is rising"*. The level keeps one table of rows (`LINE_*`
in `build-scene.py`) and every graph places its text on one of them.

There is a width budget too, and nothing enforces it at runtime: *Display
Text* centres **one** unwrapped line and lets it run off both edges, then the
slot buffer truncates it at 63 characters without a word. The default font is
9 px a glyph at size 16 and text belongs inside the title-safe 80% of a
512-wide picture ([safe-areas.md](../../docs/safe-areas.md)), so **45
characters** is the line — counting whatever a wired *Get Fact As Text* adds.
`build-scene.py` refuses to author a longer one.

Pick up the key in the yard. The basement door unlocks exactly while
`CanEnterBasement` holds: the key, plus either working power or Marta
vouching for you. That second half is not decoration — she can only be got
out while the plant runs, so an ally is standing proof that the power worked
*once*, which is what keeps the door openable after the plant trips.

The door turns **green** rather than moving. It is the object you have to
*use* to get downstairs, so an "open" animation that slides it out of the
world takes the basement with it — state a player has to interact with
reports itself with colour, like the status lamp, and stays where it is.

Walk back up from the basement and the door is green again but says nothing.
`On Condition`'s edge flag, like every piece of node state, resets on scene
load, so it fires a second time — and it *has* to, because the colour resets
too. What must not repeat is the announcement, and the save-lived
`world.basementDoorOpened` is what tells those two apart. That is the whole
lesson: **node state dies with the scene, a fact does not.** The same shape
gates the boot counter (`world.bootCounted`, session tier), which otherwise
counted every trip downstairs as another launch of the game.

## The basement, and what survives the trip

Using the open basement door takes you to a **second scene**, which exists
for one reason: to show which facts cross a scene boundary and which do not.
On arrival it prints the plant's state — `world.power.state` is world-scoped,
so it is whatever you left it at — beside the alarm, which is **scene-scoped**
and reads *false* however loudly it was ringing upstairs.

Two objects down there make the persistence tiers concrete:

- the **ledger** reads `world.basementVisits` (save-lived, so it belongs to
  the slot) next to `profile.timesPlayed` (profile-lived, so it belongs to
  the card);
- the **save-point** takes the RAM snapshot and writes it to slot 1 — as two
  nodes, *Save Checkpoint* then *Commit Checkpoint*, because that split is
  what lets a game checkpoint constantly and only pay for the card at a
  chapter break.

The demonstration worth doing by hand: visit the basement a few times, save,
visit a few more, then load slot 1. `basementVisits` comes back to what the
save held; `timesPlayed` does not, because it never belonged to the save.
Quit and relaunch and `timesPlayed` goes up again while nothing else does.

The basement also has **no terrain at all** (`enabled: false` — see
[terrain.md](../../docs/terrain.md)), so its floor is an ordinary box.
Incidental to the facts, but it is why the void is under you rather than
ground.

## Reading it while it runs

Build and run, then open *Tools > World Facts > Blackboard*: every fact live,
with **who changed it last** — the graph node or the rule, by name. Tick
**Override** on a fact to hold it at a value, or apply a scenario from the
**Scenarios** tab to jump straight to a situation.

The **Why?** panel under any query or rule shows the condition tree with the
value each leaf actually read and the child that decided the answer. It works
with the game off too, against the catalog's defaults — try it on
`CanEnterBasement` after applying the *Locked out* scenario.

## Rebuilding it

`build-scene.py` authors the whole thing — the catalog, the queries, the
rules, the scenarios and all fifteen object graphs — and is the readable form
of the design:

```bash
tyrax-editor --new world-facts <parentDir> 64 32 fpp
python3 examples/world-facts/build-scene.py
```

The project ships with the **debug** profile and the *Live Debugger*
preference on, because the blackboard reads the running game through that
channel.
