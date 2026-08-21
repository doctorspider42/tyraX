# Input recorder — record a play session and perform it again

The input recorder writes down every frame of what a player did, so the same run
can be performed again on demand. It is how a bug that somebody hit once becomes
a bug you can walk into whenever you like, with the Live Debugger, the time
machine and a breakpoint already waiting for it.

Live Link changes the world while the game runs, Live Logic changes the program,
the Live Debugger reports what ran and the time machine puts the world back. This
is the fifth channel and the only one that reproduces a whole **session**.

**Only the input is recorded.** The world is not stored at all — it is
reproduced by running the same game against the same input. That is what keeps
ten minutes of play at about a megabyte instead of a gigabyte of world state, and
it is what makes a recording small enough to commit next to the bug report it
belongs to.

Like every other devkit channel it rides the `host:` filesystem the game already
loads its assets from, so it works in PCSX2 and on a real PlayStation 2 over
ps2link with no extra hardware and no extra transport.

## Turning it on

1. Set the project's build profile to **debug** (*Project > Preferences >
   Build*) — a release build carries none of this.
2. Tick **Input recorder** (*Project > Preferences > Build*).

It is the one devkit channel that is **off by default**. The other four cost a
small fixed file each; this one writes a file that *grows* for as long as the
game runs, and that is not something a rebuild should quietly start doing to
somebody's disk.

## Recording and replaying from the editor

*Tools > Debugger* (**F9**) has a **Replay** tab.

Pick what the **next run** does — *Live*, *Record*, or *Play back* a file — then
Build & Run (**F5**) or Run. The choice is staged into the launcher, which
prepares `bin/` before the emulator starts; it is not something you can switch
mid-run.

While the game runs the tab shows what it is doing, read straight out of the
status file the game writes:

- `Recording: frame 1234`
- `Replay: 812 / 3000` — and, if it stopped matching, `Diverged at frame 640`
- `Replay finished: 3000 frames, 0 divergence(s)` — `Reproduced exactly.`

**Save recording** is what turns the raw append log in `bin/replay.out` into a
file worth keeping: it asks the game to finish cleanly, waits for it, and writes
`recordings/<name>.tyrarep` with whole chunks, a frame count and a clean end.

A recording whose emulator was killed mid-run still saves. The unfinished tail is
dropped and everything before it is kept, which is the normal way a debugging
session ends.

## From the command line

```bash
tyrax-editor --record <projectDir> <out.tyrarep> --pad "wait 6; stick l 0 -127; wait 3; press cross" --seconds 12
```

Builds, launches, holds the controller with the [Remote Pad](remote-pad.md)
script language, stops the recording and writes the finished file. `--seconds` is
a floor, not a replacement: a shorter script keeps recording (idle frames are
part of a run) and a longer one is not cut short.

```bash
tyrax-editor --replay <projectDir> <file.tyrarep>
```

Builds, launches, performs the recording, and reports. **The exit code is the
point of this command:**

| Exit | Meaning |
|---|---|
| `0` | reproduced exactly |
| `3` | the run diverged |
| `1` | it could not be run at all (build failure, timeout, wrong frame rate) |

So a recording is a **regression test for a whole play session** — the thing
`--pad` could never be, because it could drive a game but nothing afterwards
could say whether it did the same thing as last time.

Both take `--clear-saves` (drop the host-side save files first) and `--replay`
also takes `--timeout <s>` and `--keep-running` (leave the emulator up so you can
poke at whatever the recording arrived at).

Every line the runtime logs is prefixed `Replay:`, which is the one anchor a
script or a `grep` over `bin/log.txt` needs.

## What is reproduced

- **Both pads** — buttons held, click edges, and both analog sticks as raw axis
  bytes. The recording **overwrites** the pad rather than merging with it, so a
  hand resting on a real controller cannot change the run being reproduced. (That
  is the difference from the Remote Pad, which is an overlay on purpose. If both
  are active the recording wins; it is the last stage of the frame's input.)
- **The USB keyboard and mouse** — held keys, click edges, mouse deltas, wheel
  and buttons. A recording made on a machine with a keyboard replays on one
  without.
- **The frame time.** `dt` is recorded per frame, so a loading hitch that shifted
  a jump reproduces too. This matters more than it sounds: without it, a replay
  on a warmer disk cache silently walks further per frame than the original.
- **Procedural seeds.** A runtime [procedural volume](procedural-runtime.md) set
  to *a new world every run* asks the console's clock for a seed — the one
  genuinely non-deterministic decision the game makes. It is recorded, so the
  same world regenerates.

## What is not

- **Memory-card saves.** PCSX2 keeps its card between runs, so a game that reads
  one starts from wherever the last session left it. `--clear-saves` covers the
  host-side `save<N>.sav` / `profile.sav` fallback files; the emulated card has
  no cheap equivalent. If a run depends on save state, start it from a card you
  control.
- **Anything you change while it plays.** A time-machine rewind, a Live Logic
  patch or a Live Link edit all move the world out from under the recording — on
  purpose. That is often exactly what you want (rewind, patch a graph, watch the
  fix play out); just do not read the divergence count as a bug afterwards.
- **A different frame rate.** A 50 Hz recording performed at 60 Hz is a different
  run: every `dt`, every menu repeat and every animation step moves. The game
  refuses it at boot and says so rather than reporting thousands of divergences.
- **A build without the keyboard/mouse option** replaying a recording that used
  one. The keys are in the file, and the engine accepts them, but nothing the
  project generated will be reading for them.

## How a divergence is reported

Recording the input alone is only useful if you find out when the reproduction
stops being one. So each frame also carries a light **fingerprint** — the
player's position and the yaw/pitch of the view, twenty bytes.

During a replay the game compares its own fingerprint against the recorded one
every frame. The first mismatch is logged with both values:

```text
Replay: diverged at frame 400: pos (0 1.8 10.301) yaw 0 pitch 0, expected (0 1.8 10.5) yaw 0 pitch 0
```

and after that only the count is kept, so a run that went wrong early does not
fill the log with the consequences. The final line names both numbers:

```text
Replay: finished 705 frames, 304 divergences (first at frame 400)
```

The fingerprint script does not run while a menu owns the frame, so those frames
carry none — the frame record says which ones do.

A recording also carries a hash of the **world it was made against** (scene
shapes, the input map, the multiplayer and keyboard settings). A mismatch is a
**warning**, not a refusal: editing the scene between recording a bug and
replaying it is the normal case, and the fingerprint reports what actually went
different far better than a hash could. The editor shows the same warning before
you start the run.

## Where the files live

| File | Written by | What it is |
|---|---|---|
| `bin/replay.arm` | the editor | "record this run" |
| `bin/replay.out` | the game | the raw recording, appended chunk by chunk |
| `bin/replay.in` | the editor | a recording to perform |
| `bin/replay.stop` | the editor | finish the recording cleanly |
| `bin/replay.st` | the game | 32 bytes of status for the Replay tab |
| `recordings/*.tyrarep` | the editor | saved recordings — **committed on purpose** |

Everything in `bin/` is gitignored; `recordings/` is not. A recording next to the
bug it reproduces is the whole reason the format is small and stable.

The mode is picked at boot from those files, so a run needs no launch flags: the
game looks for `replay.in`, then `replay.arm`, and if neither is there it never
touches the disk again — one failed pair of `fopen` on the first frame and
nothing after it.

## The file format

Little-endian throughout. A 64-byte header (magic `TXRP`, version, frame count,
frame rate, flags, chunk size, the world hash, the start scene and the project
name) followed by **chunks**:

```text
u32 firstFrame, u16 records, u16 payloadBytes, payload, u32 crc32(payload) ^ firstFrame
```

A chunk with zero records and zero payload is the **terminal chunk** — the
writer's way of saying "this file ends here on purpose", which is what lets a
reader tell a finished recording from a killed one.

Two things fall out of that framing and both matter:

- **The recording is buffered in RAM and appended a chunk at a time** (64 frames
  under PCSX2, 256 over ps2link, where every write is a network round trip). A
  per-frame write would be a `host:` round trip per frame and would itself change
  the frame times the recording exists to capture.
- **A file killed mid-write still parses.** Each chunk carries its own CRC, so
  the reader keeps every chunk that checks out and stops at the first that does
  not. Saving canonicalizes what survived.

The read path **streams**: one chunk in memory at a time, never the whole file.
Half an hour of input is ~3.6 MB and the EE's 32 MB is already spoken for by a
large scene.

Records inside a chunk are one byte of kind plus a body — a `FRAME` (flags, `dt`,
pad 1, then pad 2, the keyboard and the fingerprint only when present) or a
`SEED` (a volume index and the number it drew). About 34 bytes a frame in
practice, so ten minutes at 50 Hz is roughly a megabyte.

The editor side is `src/livereplay.hpp` / `.cpp`; the game side is generated into
`src/gen/input_replay.gen.cpp`. **The two are twins — change them together.**

## Cost

Nothing at all in a release build: the generated runtime becomes an empty
translation unit, the header's two entry points become inline no-ops and every
call site folds away. `tyrax-editor --audit-release` proves it, and every release
build runs that audit.

In a debug build with the recorder off, the same. With it on, recording costs one
`host:` write per 64 frames and about 34 bytes of RAM per frame; replaying costs
one read per 64 frames. Both are far below what the polling channels
(docs/devkit.md) cost, because the cadence is a chunk and not a frame.

## Related

- [Devkit overview](devkit.md) — every channel, the release-clean rule, and why
  they start on different frames.
- [Remote Pad](remote-pad.md) — the script language `--record --pad` takes, and
  the channel a recording overrides.
- [The time machine](time-machine.md) — the other way to get back to a moment.
- [Live Debugger](live-debugger.md) — what to have open while a replay runs.
