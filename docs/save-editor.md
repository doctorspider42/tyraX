# Save Editor: card identity, size, checkpoints

*Tools > Save Editor* is everything about memory card saves in one window: how
the save presents itself in the PS2 browser, exactly what one slot costs, the
named values and texts every slot carries, and the checkpoint system that lets
you save progress without touching the card at all.

The game keeps **three slots** in its own directory on card 1
(`/TYRA-<PROJECT>`). When the BIOS memory-card modules cannot be loaded, or no
formatted card answers, the slots fall back to `save<n>.sav` next to the ELF —
which is what makes saves work under PCSX2 without a virtual card set up.

## Card appearance: title and icon

The **title** is what the PS2 browser prints under the icon. It is two lines;
write `|` where the break goes. The browser renders only full-width Shift-JIS,
so ASCII is mapped to the full-width forms automatically — you type normally.

The **icon** is a real PS2 3D icon (`list.icn`), not a picture:

| Icon source | What ships |
| --- | --- |
| *Image quad* (default) | A flat two-sided quad carrying any project picture, resampled to the PS2's 128×128 icon texture, with a gentle idle sway. |
| `res/models/*.obj` | The model's own triangles plus its `map_Kd` texture (Kd baked into vertex colours), with the same idle sway. |
| `res/models/*.glb` | The chosen **clip**, sampled into up to 8 morph shapes — the icon actually plays your animation in the browser, like retail saves. |

Models are auto-fitted into icon space. Anything over **800 triangles** falls
back to the quad (the browser gets sluggish past that) and the Save Editor says
so. The editor bakes `icon.sys` + `list.icn` into `res/save/` on every build and
the generated save system copies them onto the card the first time it writes.

Note that `list.icn` is usually **far** the largest thing your save puts on the
card — tens of kilobytes against a slot payload measured in hundreds of bytes.
It is written once per card, not once per slot.

## Size preview: bytes vs card space

The table breaks down one slot byte for byte: the header (scene index, player
position and facing), four bytes per save value, a fixed 32 bytes per save text,
and 32 bytes per save-flagged object state — sized by the largest per-scene
count of objects with *Save state* ticked, which the tree below the table lists
by name. The slot file itself is that sum rounded up to a 64-byte boundary.

Two totals are shown, and they deliberately disagree:

- **All data (raw bytes)** — the arithmetic sum. Useful for "did adding that
  save value matter?".
- **Card space used (1 KB clusters)** — what the card actually loses, and the
  number to quote. A PS2 memory card allocates in whole **1 KB clusters** and no
  two files share one, so *every* slot costs at least a full kilobyte however
  small its payload is, `icon.sys` costs one, `list.icn` costs as many as it
  needs, and the save's own directory costs one more.

A byte-count that ignores that rounding understates real usage — for a small
save it can be off by several times over, since a 128-byte slot and a 1000-byte
slot cost exactly the same on the card.

## Save data: values and texts

Named values (numbers) and texts persist in every slot; the defaults you set
here are the fresh-game state. Flow-graph *Save* nodes address them by name:
**Set Save Value**, **Add To Save Value**, **Value At Least** (a pure bool
source), **Get Save Value**, **Set Save Text** and **Get Save Text**. Texts are
a fixed 32 bytes each — that cap is what keeps the payload a fixed size, which
is what makes the checkpoint buffer below possible.

**Open Save Menu** opens the in-game three-slot save/load menu, the same one a
Save point object opens on USE. Its buttons come from the remappable input map
(*confirm* saves, *alt* loads), so they follow whatever the player rebound.

## Checkpoints: saving without the card

Writing to a memory card is slow and has to be announced, which makes it wrong
for a death-and-respawn loop. Checkpoints are the RAM half:

- **Save Checkpoint** snapshots the whole save payload into a single in-RAM
  buffer. Instant; nothing touches the card.
- **Load Checkpoint** restores it. It does nothing at all until a checkpoint has
  been taken, so it is safe to wire unconditionally.
- **Has Checkpoint** is a pure bool source — gate a "Continue?" prompt on it.
- **Commit Checkpoint** writes the buffer to a real slot (0–2). This is the one
  checkpoint node that touches the card, so it is the one that can be slow: use
  it at a chapter break, not on every death.

Exactly **one** checkpoint exists at a time, on purpose. The buffer is the same
fixed-size payload a slot holds, so checkpoints cost a couple of kilobytes of RAM
that never grows, instead of a stack of snapshots nobody budgeted for on a 32 MB
console.

## The "checking memory card" screen

Every card access — saving, loading, committing a checkpoint — goes through one
path that puts the *do not remove the memory card* overlay on screen **before**
the blocking transfer starts, holds it for a minimum time so it cannot flicker
past unread, and owns the pad while it is up (no input reaches the game or the
save menu, and a second card operation cannot be started on top of the first).
The overlay sprite is baked to `res/hud/save-busy.png` and is user-replaceable
like the save-menu sprites.

## Hands-on checks this feature needs

The card path cannot be fully verified from the host: a real (or virtual) card
that is **full**, **absent** or **unformatted** exercises the failure feedback,
and the icon only proves itself in the **PS2 BIOS browser**, which is where the
title's line break and the animated icon's motion actually show.
