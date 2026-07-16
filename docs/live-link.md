# Live Link — edit the running game

Live Link mirrors scene edits into the **running** game without a rebuild:
drag an object with the gizmo, spin it, scale it, recolor it — and watch it
move on the PS2 (or in PCSX2) as you drag. In 2002 this kind of live tuning
loop was devkit-studio magic; here it rides entirely on infrastructure the
project already has.

## Using it

1. Set the project's build profile to **debug** (*Project > Preferences >
   Build > Build profile*). Release builds carry no Live Link code at all.
2. Build & run as usual — **F5** (PCSX2) or **F6** (real PS2 over ps2link).
3. Edit the scene: move / rotate / scale / recolor objects. The changes appear
   in the running game within a fraction of a second; a gizmo drag streams
   continuously.

The toolbar shows the session state next to the run buttons:

- **● LIVE** (green) — edits are streaming into the game.
- **● LIVE (rebuild)** (amber) — the scene changed *structurally* (see limits
  below); streaming is paused so the wrong object can never be patched. Build
  & Run again and it resumes automatically — you don't even have to restart
  the game if the structure went back to what was built (e.g. after an Undo).

The master switch is *Build > Live Link* (on by default, stored in
`editor.ini`, machine-global). With Live Link off, or a release profile, the
editor writes nothing.

## What updates live vs what needs a build

Live: **position, rotation, scale, color** of every scene object — the same
mutations the *Move/Show/Set Color* flow nodes perform at runtime, so geometry
rebuild, physics and collision all follow the patched values. Applying a
snapshot is idempotent; objects the game moved itself (physics, flow graphs)
are only stomped when you actually change something in the editor.

Everything else needs a normal build, exactly like before. The editor detects
the cases that would *corrupt* a live session (they change object indices or
build-time-baked geometry) and flips the indicator to amber:

- adding / deleting / reordering objects, or changing an object's type
- assigning a different model / material, changing primitive detail
- streaming-layer membership or layer definitions
- moving/editing a **point light** (its light is baked into vertex colors)
- moving a **projected decal** (its transform is the projector, baked on the
  host)

Other non-live properties (physics flag, emitter parameters, player tunables,
terrain sculpting, sky…) don't endanger the session — they simply don't show
up in the game until the next build.

## How it works

There is no socket and no extra protocol stack — the transport is the **host
filesystem the game already loads its assets from**: PCSX2's *Host Filesystem*
on the emulator, the ps2link/ps2client file server on a real console. One
mechanism, both targets.

- The editor (`App::liveLinkTick`, ~10 Hz) snapshots the active scene's
  live-patchable state. When it differs from the last written snapshot it
  writes `bin/livelink.bin` — a tiny little-endian blob (`TXLL` magic,
  sequence number, scene index, 12 floats per object, a footer echoing the
  sequence) — atomically via a sibling tmp file + rename.
- The game side is a generated global script, `src/scripts/live_link.gen.cpp`
  (debug profile only; the release variant is an empty translation unit). It
  re-reads the file every 6 frames (every 25 under ps2link — each `fopen`
  there is a network round-trip), validates magic/size/footer so a torn write
  is ignored, skips already-applied sequence numbers, and patches
  `RuntimeObject.data` + `dirty` for objects whose values actually changed.
- The index mapping between the two sides is guarded by a **structure
  signature** (`project::liveLinkSignature`): the Runner stamps it into
  `bin/livelink.sig` at the start of every build, and the editor streams only
  while the live project still hashes to the same value. Structural drift →
  amber indicator, no writes.
- The Runner also deletes `bin/livelink.bin` at build start: the fresh build
  bakes the current scene state, so a stale snapshot must not be re-applied at
  boot. Conversely, a *Run (no build)* keeps the snapshot — edits made while
  the game was down are applied as soon as it boots.

Cost: zero in release builds (the poller doesn't exist), an `fopen` + a few
hundred bytes read every 6/25 frames in debug builds, and nothing on the GS —
patched objects go through the exact same dirty-rebuild path the flow-graph
object actions already use.

## Limits & notes

- Only **authored** objects are patched — never the runtime spawn pool
  (*Spawn Object* clones live past the authored table and are left alone).
- The snapshot targets the editor's **active scene**; if the game is in a
  different scene it ignores it (switch scenes in the editor to tune there).
- Sequence numbers make application idempotent, but an object that physics is
  actively moving will be snapped back once per edit — that's the same
  behavior a *Set Position* flow node has.
- On a real PS2 the poll cadence is ~0.5 s to keep the ps2link file server
  happy; PCSX2 polls ~10x per second.
