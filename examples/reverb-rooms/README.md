# reverb-rooms example

Four rooms, four different acoustics, one sound. This is the demo for
[reverb](../../docs/reverb.md): the PlayStation 2's **hardware reverb**,
authored by drawing a box.

Open `reverb-rooms.tyra` in the editor and Build & Run (`F5`), or build
headless: `tyrax-editor --build <this folder> --run`.

**Wear headphones or use speakers.** Everything this example does is audible
and nothing is visible — the reverb zones are invisible in the game, exactly
like every other [Area](../../docs/areas.md).

## What to do

You stand outside, on the grass, facing a brown-floored opening. Press
**CROSS** anywhere to knock. The knock is one short sample and it never
changes — what changes is the room you are standing in.

| Where | Press CROSS and you hear |
|---|---|
| **Outside**, on the grass | the bare sample, ~200 ms and gone |
| The brown **cave mouth** | the same knock with a light hall around it |
| The blue **hall** (through the mouth, past the pillars) | a big room — the tail runs for over a second |
| The amber **pipe corridor** (right of the spawn, walk in from its near end) | narrow, metallic, resonant |
| The green **chamber** (left of the spawn) | a small, tight room |
| The **red closet** inside that chamber | dead silent acoustics — the reverb stops at its door |

Four buttons:

- **CROSS** — knock, heard through whatever room you are in.
- **CIRCLE** — the *same knock, forced dry*. Stand in the hall and alternate
  the two: CROSS rings, CIRCLE does not. That is the **Dry** parameter on the
  Play Sound node — for a UI beep or a voice line that has to sound the same
  everywhere.
- **TRIANGLE** — force *Space echo* on the whole game, ignoring every zone
  (the **Set Reverb** flow node). Now the grass outside sounds like a canyon.
- **SQUARE** — hand control back to the rooms.

Two things happen without you pressing anything:

- a **drip in the hall** every 3 s — a sound emitter with its *Reverb*
  checkbox **on**, so it is heard through whatever room you are in;
- a **tick in the green chamber** every 2 s — the same sample from an emitter
  with *Reverb* **off**. Stand in the chamber and compare it against your own
  CROSS: same sample, same spot, one wet and one dry.

## The transitions worth walking slowly

**Grass → cave mouth → hall** uses the *same* preset (Hall) at two strengths —
0.35 in the mouth, 0.90 in the hall — so it ramps on one reverb unit. Walk in
slowly and the room grows around you.

**Hall → pipe corridor** changes the algorithm entirely, and it still fades:
the PS2 has **two** reverb units, and the game hands the room you are entering
the free one, loads its preset there while it is silent, and ramps the two past
each other. No dry gap.

Two consequences you can hear if you look for them. A sound **finishes in the
room it started in** — knock as you step out of the hall and the tail follows
you, because a reverb unit is per SPU2 core and the voice is already on the
other one. And only **two** rooms can be live at once, so sprinting through
three differently-flavoured rooms inside half a second makes the third wait for
the first to finish leaving; it waits rather than glitching.

## How it is wired

- **`zone-mouth` / `zone-hall`** — Areas with *This area is a room for the
  sound effects* ticked: Hall at 0.35 and 0.90. Same preset on purpose.
- **`zone-pipe`** — Pipe 0.80. **`zone-chamber`** — Room 0.55.
- **`zone-closet`** — preset **Off**, **Priority 1**, sitting inside
  `zone-chamber`. Overlapping zones do not mix; the highest priority containing
  the listener wins, so the closet cuts a dry pocket out of the room around it.
- **`hall-drip`** / **`chamber-tick`** — sound emitters, identical but for the
  *Reverb* checkbox.
- The player's flow graph — two `On Button` → `Play Sound` pairs (the second
  with *Dry* = 1, both pinned to their own channel so a new press cuts the
  previous one), plus `On Button` → **Set Reverb** on its `set` and `clear`
  pins. One node, two triggers.

The geometry is only there so you can tell the rooms apart: coloured floor
slabs (collision off) and plain box walls. Nothing about the reverb comes from
the geometry — the volumes are what the game reads, and you could move them
somewhere else entirely.

## Measured

Recorded off the sound card in PCSX2, tail = time from the knock's peak until
the signal reaches the noise floor:

| | tail |
|---|---|
| CROSS outside the zones | 200 ms |
| CROSS in the hall | **1150 ms** |
| CIRCLE (dry) in the hall | 200 ms |
| the hall emitter, *Reverb* on | 700 ms |
| CROSS outside, after TRIANGLE forced Space echo | **1550 ms** |
| CROSS outside, after SQUARE cleared it | 200 ms |
| CROSS in the hall **0.4 s after** TRIANGLE swapped the preset | **1550 ms** — no dry gap |

So: the zone works, the *Dry* parameter works, the emitter checkbox works and
the override node works — all against one unchanging sample.

`res/sfx/knock.wav` is a synthesised percussive knock (22 kHz mono, 0.18 s),
generated for this example and in the public domain. A short transient is the
right test signal here: a reverb tail is only legible after a sound that
*stops*.
