# The time machine — put the running game back

Live Link streams object state **into** the running game. The Live Debugger
streams back **what ran**. Live Logic streams **the program itself**. This
channel streams **the world**: the game captures everything it mutates a few
times a second, the editor keeps a history of those captures, and pushing one
back puts the PlayStation 2 where it was.

Together with [Live Logic](live-logic.md) that closes a loop nothing else on
this hardware has: **rewind a few seconds, fix the graph, and watch the fix play
out on the situation that just broke** — no rebuild, no reboot, no walking back
across the map.

## Using it

1. Build profile **debug** and **Time machine** on (*Project > Preferences >
   Build*; on by default).
2. Build & run once — **F5** (PCSX2) or **F6** (a console over ps2link).
3. Play. The game starts capturing immediately; *Tools > Debugger* > **Rewind**
   shows how much history you have.
4. Drag the slider back and press **Rewind to here**. The game snaps into that
   capture and keeps running from it.

The Rewind tab sits next to the Debugger's **Timeline** on purpose: the Timeline
says *what ran* on a frame, Rewind puts the world back *into* that frame. Same
axis, two halves of the same question.

## What a capture holds

Everything the running game mutates that the channel can reach through
`ScriptContext`:

- **Every runtime object** (authored ones and live-spawned clones): position,
  rotation, scale, colour; the physics state (velocities, spin, the
  settle-flatten targets, the sleep counter); visibility and layer residency;
  and the animation state (clip, playing/looping, speed, crossfade).
- **Where the player stands** and which way they face.
- **Every flow variable** — the Set/Get Int, Bool and Position nodes' storage.
- **Every save value.**

### What it does not hold — yet

Named here rather than discovered later:

- **The walker's fall speed and camera boom.** They live in the game class,
  which this channel deliberately does not reach into (that is what keeps it out
  of the two duplicated game templates). A rewind lands you standing where you
  were.
- **Flow-graph timers and edge latches.** Each generated graph class keeps its
  own state — an armed `Delay`, "has this trigger fired". A rewind restores the
  world those graphs act on, not their own counters, so a `Delay` armed before
  the rewind still lands after it.
- **Sequences mid-play, open menus, audio playback position, particles.** Each
  is a separate "does rewinding this even mean anything" question.

The Rewind tab lists these under the controls, so the panel never implies more
than it does.

## Why the history is in RAM

The only things on disk are two files next to the ELF, both **fixed size**:

| File | Written by | Size |
|---|---|---|
| `bin/livetime.bin` | the game, every 6 frames (25 under ps2link) | one capture, overwritten in place |
| `bin/livetime.rst` | the editor, when you rewind | one capture |

The history itself lives in the editor's memory, bounded by *Edit > Preferences*
(**128 MB** by default — roughly seven minutes of a mid-sized scene). Oldest
captures fall out as new ones arrive; **Clear history** empties it, closing the
project drops it, and the Runner deletes both files at the start of every build.

This is a deliberate trade. Writing the history to disk would buy hours of it,
but a debugging session would silently grow a file nobody asked for. Here the
disk footprint is bounded *by construction* rather than by remembering to clean
up.

## How it works

The game side is generated into `src/gen/live_time.gen.cpp` — an ordinary global
`Script`, like the Live Logic pump, so it needs no game-loop hook at all.
Everything it touches is on `ScriptContext`, including moving the player: a
restore raises `ctx.teleport`, the same request the **Spawn Player At** node
makes, and the game's own loop consumes it. That is why a restored player is
still subject to the playable bounds and to collision — the rewind goes through
the game, not around it.

The editor side is [`src/livetime.hpp`](../src/livetime.hpp) (formats, the
history ring and its budget — no GL, no ImGui, harness-testable) plus
`App::livetimeTick` / `App::timeMachineRewind`.

**The editor never looks inside a capture.** What is in one is a codegen detail;
the editor stores the bytes and hands the right ones back. What keeps that safe
is the **layout hash** in the header: it mixes the object, variable and save
counts, and the game refuses a capture whose hash is not its own — so a capture
taken before a rebuild that changed the scene is rejected instead of written
over a differently shaped world. A capture from another scene is refused the same
way.

Torn writes are caught on both ends by an exact-size + footer-echo check (the
footer echoes the sequence number, so a file caught half-rewritten fails instead
of restoring a mixed state). A missed capture costs nothing — the next one is
along in a few frames.

The frame counter deliberately keeps counting **forward** across a rewind: it is
the history's ordering key, and the editor uses "the frame went backwards" to
detect a restarted game and drop a history that is no longer a continuation.

### Cost

In a debug build with the preference on: one `fopen` + a few KB written every 6
frames (25 under ps2link), one `fopen` to check for a restore on the same beat,
and a buffer sized for the largest scene plus the spawn pool (~3.4 KB for a
33-object scene). Nothing on the GS. In a release build — or with the preference
off — the generated runtime is an empty translation unit and neither file is ever
touched.

## Related

- [live-logic.md](live-logic.md) — edit a graph with no rebuild. The other half
  of the loop.
- [live-debugger.md](live-debugger.md) — what the graphs are doing, breakpoints,
  step, and the execution timeline this tab sits next to.
- [live-link.md](live-link.md) — object state into the running game.
- [devkit.md](devkit.md) — what a debug build costs, and the release audit that
  proves a shipped ELF carries none of it.
