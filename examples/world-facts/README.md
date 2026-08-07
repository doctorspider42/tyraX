# world-facts — the game's central memory, end to end

A small level whose entire logic is **World Facts**
([docs/world-facts.md](../../docs/world-facts.md)): three generator parts to
collect, an NPC to rescue, a locked basement, and a power state that other
things react to. Nothing in it is a scratch variable — every piece of state is a
declared fact, and the conditions that would normally be copied into five graphs
are named queries.

Open *Tools > World Facts* first. The **Catalog** tab is the design of the level
on one page.

## What it exercises

| | Where |
| --- | --- |
| Every fact **type** | `world.generator.parts` (whole number), `world.power.state` (one-of-several), `world.power.load` (number), `characters.marta.rescued` (yes/no), `player.lastCheckpoint` (position) |
| Every **persistence** tier | session (`world.alarm.ringing`), checkpoint (`world.power.load`), save game (most), profile (`profile.timesPlayed`) |
| Both **scopes** | world, plus one scene-scoped fact (`world.alarm.ringing`) |
| A **computed** fact | `characters.marta.isAlly` — derived, and nothing can write to it |
| **Nested queries** | `CanEnterBasement` names `MartaIsAlly` |
| All three **rule policies** | becomes-true (`PowerRestored`), every-frame (`AlarmWhileOverloaded`), once-per-run (`RescueEarnsTrust`) |
| Rule **order** mattering | `PowerSettles` sits above the alarm rule on purpose, so the frame the plant recovers the alarm is not raised one more time |
| A rule that **sends an event** | `PowerRestored` → `power-on` → the status lamp's graph |
| **On Fact Changed**, all three outputs | the lamp's HUD line hangs off *changed*; the alarm's *became true* / *became false* are raised by a RULE and lowered by a graph's Clear Fact, and the lamp does not care which |
| **Get Fact As Text** | the plant's state prints as *Broken / Powered / Overloaded*, not as a number |
| **Scenarios** | *Endgame*, *Locked out*, *Overloaded* |
| Facts **across a scene switch** | the basement prints `world.power.state` (world-scoped, comes through the door with you) next to `world.alarm.ringing` (scene-scoped, back to false however loudly it was ringing upstairs) |
| **Per-slot vs per-card** | the basement's *ledger* reads `world.basementVisits` (save-lived, belongs to the slot) next to `profile.timesPlayed` (profile-lived, belongs to the card) |
| **Checkpoint vs card** | the *save-point* takes the RAM snapshot and commits it to slot 1 as two separate nodes |

## Playing it

Walk into the three yellow spheres — each fits a generator part
(`Set Fact` on the `add` pin, so no read is needed). The third one makes
`GeneratorReady` true, the **PowerRestored** rule flips `world.power.state` to
*Powered* and sends `power-on`, and the status lamp turns green while the HUD
prints the new state by name.

Use the generator to push `world.power.load` up by 0.25 a time. Past 0.8 the
**OverloadTrips** rule flips the state to *Overloaded*, **AlarmWhileOverloaded**
re-asserts the alarm every frame, and the lamp goes red off *On Fact Changed*'s
`became true`. The red switch beside the generator clears the load and the
alarm; the **PowerSettles** rule then notices the load is off a tripped plant
and brings it back to *Powered* — which is what makes the whole loop replayable
and lets the basement door's `On Condition` fire more than once in a run.

Everything that can be used now **reports**, because a fact nobody can see is a
fact nobody can debug. The generator prints its state by name and its load as a
figure. The door says why it is shut, and tells *"it needs the key"* apart from
*"the plant is dead and Marta does not vouch for you"* by asking one more fact.

Marta asks for help as you come near — and only once she is out does using her
thank you instead. One `Branch` on `characters.marta.rescued` is that entire
state machine: the graph holds no state of its own, which is the argument for
facts in one picture. Rescuing her is worth 5 trust, and that comes from the
**RescueEarnsTrust** rule rather than from her graph.

Pick up the key in the yard. The basement door opens exactly while
`CanEnterBasement` holds: the key, plus either working power or Marta vouching
for you.

## The basement, and what survives the trip

Using the basement door once it is open takes you to a **second scene**, which
exists for one reason: to show which facts cross a scene boundary and which do
not. On arrival it prints the plant's state — `world.power.state` is
world-scoped, so it is whatever you left it at — beside the alarm, which is
**scene-scoped** and reads *false* however loudly it was ringing upstairs.

Two objects down there make the persistence tiers concrete:

- the **ledger** reads `world.basementVisits` (save-lived, so it belongs to the
  slot) next to `profile.timesPlayed` (profile-lived, so it belongs to the
  card);
- the **save-point** takes the RAM snapshot and writes it to slot 1 — as two
  nodes, *Save Checkpoint* then *Commit Checkpoint*, because that split is what
  lets a game checkpoint constantly and only pay for the card at a chapter
  break.

The demonstration worth doing by hand: visit the basement a few times, save,
visit a few more, then load slot 1. `basementVisits` comes back to what the save
held; `timesPlayed` does not, because it never belonged to the save. Quit and
relaunch and `timesPlayed` goes up again while nothing else does.

The basement also has **no terrain at all** (`enabled: false` — see
[terrain.md](../../docs/terrain.md)), so its floor is an ordinary box. That is
incidental to the facts, but it is why the void is under you rather than ground.

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

`build-scene.py` authors the whole thing — the catalog, the queries, the rules,
the scenarios and all eleven object graphs — and is the readable form of the
design:

```bash
tyrax-editor --new world-facts <parentDir> 64 32 fpp
python3 examples/world-facts/build-scene.py
```

The project ships with the **debug** profile and the *Live Debugger* preference
on, because the blackboard reads the running game through that channel.
