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
| A rule that **sends an event** | `PowerRestored` → `power-on` → the status lamp's graph |
| **On Fact Changed** | the lamp and the HUD both react without polling |
| **Get Fact As Text** | the plant's state prints as *Broken / Powered / Overloaded*, not as a number |
| **Scenarios** | *Endgame*, *Locked out*, *Overloaded* |

## Playing it

Walk into the three yellow spheres — each fits a generator part
(`Set Fact` on the `add` pin, so no read is needed). The third one makes
`GeneratorReady` true, the **PowerRestored** rule flips `world.power.state` to
*Powered* and sends `power-on`, and the status lamp turns green while the HUD
prints the new state by name.

Use the generator to push `world.power.load` up by 0.25 a time. Past 0.8 the
**OverloadTrips** rule flips the state to *Overloaded*, and
**AlarmWhileOverloaded** re-asserts the alarm every frame. The red switch beside
it clears both back to their catalog defaults.

Pick up the key in the yard, use Marta to rescue her (which is worth 5 trust —
from the **RescueEarnsTrust** rule, once per run, not from her graph). The
basement door opens exactly while `CanEnterBasement` holds: the key, plus either
working power or Marta vouching for you.

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
