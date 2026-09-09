# save-points example

A crystal run across a hazard slab, wired to show the two halves of
[the save system](../../docs/save-editor.md): **checkpoints**, which live in RAM
and cost nothing, and the **memory card**, which is slow and has to be
announced.

Open `save-points.tyra` in the editor and Build & Run (`F5`), or build
headless: `tyrax-editor.exe --build <this folder> --run`.

## What to do

You start at the west end. The HUD shows three things: crystals collected,
deaths, and which checkpoint you are on.

1. Walk east past the **green post** — that is *Flag A*. The checkpoint text
   changes; nothing touched the card.
2. Collect the two crystals past it. **Crystals** climbs.
3. Walk into the **dark red slab**. You die: you are put straight back at Flag A
   — and the crystals you picked up after it are **back where they were**.
   **Deaths** keeps climbing.
4. Go around the slab (north or south) to reach *Flag B*, collect the last two
   crystals, and die on purpose again. Now you respawn at Flag B, and only the
   crystals collected after B are lost.
5. At the east end, the blue **save shrine** opens the real 3-slot card menu on
   USE — the one place in this level that writes to a memory card.
6. The orange **pedestal** next to it is USE → *Commit Checkpoint* to the next
   free slot: it takes the RAM checkpoint you have been building and writes it
   to the card.
   This project has **background writing** on, so the game does not stop for it
   — you get a spinner in the bottom-right corner and keep walking.

## The point of it

**A checkpoint is the whole save payload, in RAM.** That is why step 3 restores
the crystals and not just your position: the snapshot carries player position
and facing, the save values, the save texts, and the state of every object with
*Save state* ticked (that is the flag the crystals carry). Restoring it is
instant and nothing touches the card, which is what makes it usable in a
death loop.

**Deaths is deliberately NOT a save value.** It is a flow variable (`Set Int`),
and flow variables are game-global and are not part of the save payload — so a
checkpoint load does not roll it back. Crystals *is* a save value, so it does.
Standing at the two counters after a death is the fastest way to see where the
boundary of a save actually is.

**Only two things here touch the card.** The save shrine and the pedestal.
Everything else — every flag, every death — is a memcpy. And because this
project writes in the background, even those two do not stop the game: the
spinner in the corner is the whole interruption.

## How it is wired

| Object | Graph |
| --- | --- |
| `flag-a`, `flag-b` | `Near Object` (radius 3) → `Do Once` → `Sequence`: *Set Save Text* `checkpoint`, then **Save Checkpoint**. The Sequence is there because the order matters — the text has to be written *before* the snapshot, or the checkpoint records the previous flag's name. |
| `hazard` | `Near Object` (radius 4) → `Cooldown` 2 s → `Sequence`: *Set Int* `deaths` **+1**, then `Branch` on **Has Checkpoint** → **Load Checkpoint** / `Restart Scene`. The Branch is what makes the very first death (before any flag) restart the scene instead of doing nothing. |
| `coin-1..4` | `Near Object` (radius 1.5) → `Branch` on **Is Visible** → *Add To Save Value* `coins` +1 and *hide self*. Testing its own visibility is what makes a crystal re-collectable: a checkpoint load turns it visible again, and the same graph then works a second time. A `Do Once` here would have collected it once and never again. |
| `commit-pedestal` | `On Used` → `Sequence`: **Commit Checkpoint** set to *Next free slot*, then a *Display Text* that names the checkpoint (`Get Save Text`) behind the fixed prefix `Written to slot 1: ` — that prefix is a literal typed into the node, not the slot the commit resolved, so it keeps reading *slot 1* while the writes walk on. Using it repeatedly fills slot 1, then 2, then 3, and then starts cycling — so the pedestal builds a trail rather than overwriting one save. |
| `hud` | `On Start` → three *Display Text* nodes fed by `Get Save Value`, `Get Int` → *Number To Text*, and `Get Save Text`. |

`Near Object` fires on the frame you come **inside** the radius, not every frame
you are within it, and it measures in **XZ only** — which is why the crystals
float at head height and still collect.

## The card side

*Tools > Save Editor* is where the rest of it lives. This project sets:

- a two-line browser title, `Save Points|crystal run` (the `|` is the break),
- a **3D icon** from `res/models/crystal.obj` — 12 triangles, so nowhere near
  the 800-triangle cap, set to **Bounce** over 8 frames so the crystal hops and
  squashes on landing the way retail PS2 saves did,
- one save value (`coins`) and one save text (`checkpoint`).

The Save Editor's size table is worth reading with this project open: the slot
payload is tiny, and the card space is dominated by `list.icn`.

Note that `res/save/` (the baked `icon.sys` + `list.icn`) is **not** checked in —
it is regenerated from those settings on every build.

## What a real card would still prove

Everything above runs against the `save<n>.sav` fallback next to the ELF under
PCSX2, with no virtual card configured. A **full**, **absent** or
**unformatted** card exercises failure paths this level cannot reach, and the
icon's motion and title break only really show in the **PS2 BIOS browser**.
