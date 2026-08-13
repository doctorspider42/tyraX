# Save Editor: card identity, size, checkpoints

*Tools > Save Editor* is everything about memory card saves in one window: how
the save presents itself in the PS2 browser, exactly what one slot costs, the
named values and texts every slot carries, and the checkpoint system that saves
progress without touching the card at all.

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

## Motion

A memory card icon has no runtime transform: the browser blends between **morph
shapes**, and that is all. "Animation" here means shipping several displaced
copies of the model, which is why **Frames** (1–8) is both the smoothness knob
and most of the file size.

A `.glb` with a clip uses the clip. Everything else — the flat quad, an `.obj`,
a `.glb` with no animation — takes one of the built-in **Motion** presets, with
an **Amount** multiplier on the amplitude:

| Motion | What it does |
| --- | --- |
| **Sway** (default) | A gentle turn left and right. What every icon did before this setting existed, so an older project keeps its look. |
| **Bounce** | Hops and squashes as it lands — the classic PS2 save icon. The landing frame *is* the loop point, so it cannot stutter. |
| **Spin** | A full turn per loop. Give it most of the 8 frames: with few shapes the browser lerps *through* the model rather than around it. |
| **Pulse** | Breathes about its centre. The quietest, and the one that reads best on a flat image icon. |
| **Tilt** | Rocks about its feet like a metronome. Wants a clear base — a floating shape looks like it is falling over. |
| **None** | One shape, no motion. Also the smallest `list.icn` you can ship. |

Models are auto-fitted into icon space. Anything over **800 triangles** falls
back to the quad (the browser gets sluggish past that) and the Save Editor says
so.

The panel **previews the baked icon, animated**, before anything reaches a
card: it renders the actual triangles that go into `list.icn`, multiplying
texture by vertex colour the way the browser does, and cycles the animation
shapes — sway, clip or preset. That product matters: a model with no `map_Kd`
keeps its colours in its vertices against a near-white texture, so the texture
on its own tells you nothing about the icon. The picture and the stats line
under it come from the same bake. The browser's own lighting and camera differ
slightly, so treat it as a faithful preview of the *geometry and colour*, not a
pixel-exact mock of the BIOS screen. The editor bakes `icon.sys` + `list.icn`
into `res/save/` on every build and the generated save system copies them onto
the card the first time it writes.

`res/save/` is **derived output and gitignored** — the settings live in the
`.tyra`, these are only their bytes. (The *checking memory card* overlay in
`res/hud/` is the opposite: written only when missing, so it stays tracked and
you can replace it.)

`list.icn` is usually **far** the largest thing your save puts on the card —
tens of kilobytes against a slot payload measured in hundreds of bytes. It is
written once per card, not once per slot.

## Size preview: bytes vs card space

The table breaks down one slot byte for byte: the header (scene index, player
position and facing), four bytes per save value, a fixed 32 bytes per save
text, 32 bytes per save-flagged object state — sized by the largest per-scene
count of objects with *Save state* ticked, which the tree below the table lists
by name — and 16 bytes per **World Fact** that rides a slot (an id plus three
floats; see [world-facts.md](world-facts.md)). The slot file is that sum
rounded up to a 64-byte boundary.

A catalog with **profile**-lived facts adds one more row: those live in a file
of their own beside the slots, shared by every save, and it costs a whole 1 KB
cluster whatever is in it.

Two totals are shown, and they deliberately disagree:

- **All data (raw bytes)** — the arithmetic sum. Useful for "did adding that
  save value matter?".
- **Card space used (1 KB clusters)** — what the card actually loses, and the
  number to quote. A PS2 memory card allocates in whole **1 KB clusters** and
  no two files share one, so *every* slot costs at least a full kilobyte
  however small its payload, `icon.sys` costs one, `list.icn` costs as many as
  it needs, and the save's own directory costs one more.

A byte-count that ignores that rounding understates real usage — for a small
save it can be off by several times over, since a 128-byte slot and a
1000-byte slot cost exactly the same on the card.

## Save data: values and texts

Named values (numbers) and texts persist in every slot; the defaults you set
here are the fresh-game state. Flow-graph *Save* nodes address them by name:
**Set Save Value**, **Add To Save Value**, **Value At Least** (a pure bool
source), **Get Save Value**, **Set Save Text** and **Get Save Text**. Texts are
a fixed 32 bytes each — that cap keeps the payload a fixed size, which is what
makes the checkpoint buffer below possible.

**Open Save Menu** opens the in-game three-slot save/load menu, the same one a
Save point object opens on USE. Its buttons come from the remappable input map
(*confirm* saves, *alt* loads), so they follow whatever the player rebound.

## Checkpoints: saving without the card

Writing to a memory card is slow and has to be announced, which makes it wrong
for a death-and-respawn loop. Checkpoints are the RAM half:

- **Save Checkpoint** snapshots the whole save payload into a single in-RAM
  buffer. Instant; nothing touches the card.
- **Load Checkpoint** restores it. It does nothing until a checkpoint has been
  taken, so it is safe to wire unconditionally.
- **Has Checkpoint** is a pure bool source — gate a "Continue?" prompt on it.
- **Commit Checkpoint** writes the buffer to a real slot (0–2). The one
  checkpoint node that touches the card, so the one that can be slow: use it
  at a chapter break, not on every death.

Exactly **one** checkpoint exists at a time, on purpose. The buffer is the same
fixed-size payload a slot holds, so checkpoints cost a couple of kilobytes of
RAM that never grows, instead of a stack of snapshots nobody budgeted for on a
32 MB console.

## What the save menu writes

By default the in-game menu records **a live snapshot** — where the player is
standing right now. *Save menu writes* can switch it to **the last checkpoint**
instead — the "you resume from the shrine, not from here" model: the menu then
writes exactly what *Commit Checkpoint* would. Before the first checkpoint of a
run it falls back to a live snapshot, so the menu is never dead at the start of
a game.

This changes only the **menu**. *Commit Checkpoint* always writes the
checkpoint buffer, as it was captured — not the state at the moment you fire
the node.

## How many slots, and the save menu itself

**Slots** (1–100) is how many the game offers; **Rows per page** is how many
the menu shows at once. With more slots than rows the menu **pages**: the
cursor walking off the bottom row turns to the next page, left/right jump a
whole page, and a `2/5` counter sits above the rows. Rows per page is capped at
8 — the panel bake's row limit.

Every slot is its own file and costs a whole 1 KB cluster however small it is,
so a hundred slots is about 100 KB of card. The size table below the setting
does that sum for the count you picked.

**The save menu is a normal menu.** It appears in *Tools > Menu Editor* like
any other, so its title, colours, font, sizes, panel width, placement and
images are all yours — a project made before this gets one seeded to look
exactly like the panel that used to ship, so nothing changes until you touch
it. Its **entry list is not used**: the rows *are* the save slots. The panel is
baked with *Rows per page* blank rows and the game writes `SLOT n` into them at
runtime — the only way a slot count in the dozens can work, since a label baked
per slot could never page.

## Which slot a commit goes to

*Commit Checkpoint* has a **Writes to** mode:

| Mode | Slot |
| --- | --- |
| **This slot** (default) | The node's own Slot number — what the node always did, so existing graphs are unchanged. |
| **Autosave slot** | The slot named below. With none set the node does nothing at all, rather than guessing at slot 1. |
| **Next free slot** | The first empty slot, so a run leaves a trail of saves instead of one. When they are all full it cycles, and it never picks the autosave slot. |

The **Autosave slot** (Save Editor) sets one slot aside for the game's own
saves: *Autosave slot* writes it and *Next free slot* leaves it alone, so a
rotating autosave cannot eat the one the game relies on.

It is a **designation, not a lock** — the in-game menu can still save over that
slot and load from it like any other. Nothing stops a player from doing what
they like with their own memory card, which is usually the right default; if a
game needs the slot protected, that is a menu rule to add, not a card one.

## Writing in the background

Every libmc call is asynchronous already; the blocking path just answers each
one immediately. Tick **Write in the background** and a save is instead stepped
one call per frame while the game keeps running — no pause, and no *checking
memory card* overlay. That is what makes *Commit Checkpoint* usable at a
chapter break without stopping the music.

**Loading always blocks.** The world is being replaced, so there is nothing to
keep playing while it happens.

What you give up: the overlay exists to tell the player not to pull the card
out. Without it, a card yanked mid-write corrupts the slot with no warning
shown. That is the trade, and why this is off by default.

The **spinner** is the replacement signal — a small activity ring in a corner
you choose, with its own margin and size. It holds for a minimum time even when
the write finishes instantly, because a one-frame flash reads as a glitch
rather than as "your game was saved".

### Using your own spinner

*Spinner sheet* points at **any PNG in the project**; *Cells* says how many
animation frames the strip is cut into. The sheet is one horizontal row, so a
cell is `width / cells` by `height` — the editor works the cell size out for
you, previews the animation at the speed the console will play it, and prints
what it decided. `res/hud/save-spinner.png` (8 cells of 32×32) is the built-in
default, written only when missing, so overwriting that file works too.

The one hard rule is the console's: **both sides of the sheet must be
8/16/32/64/128/256/512**. A 256×64 strip of 4 cells is fine; a 100×20 one is
not. You don't have to remember this — a sheet the console would reject is
**not shipped**: the Save Editor says why in amber and the build falls back to
the built-in. That matters because the failure it replaces is unhelpful: the
engine asserts while loading the texture and the game never leaves the TyraX
splash, which looks like a hang rather than like a bad image.

## The "checking memory card" screen

Every card access — saving, loading, committing a checkpoint — goes through one
path that puts the *do not remove the memory card* overlay on screen **before**
the blocking transfer starts, holds it for a minimum time so it cannot flicker
past unread, and owns the pad while it is up (no input reaches the game or the
save menu, and a second card operation cannot start on top of the first). The
overlay sprite bakes to `res/hud/save-busy.png` and is user-replaceable like
the save-menu sprites.

## Hands-on checks this feature needs

The card path cannot be fully verified from the host: a real (or virtual) card
that is **full**, **absent** or **unformatted** exercises the failure feedback,
and the icon only proves itself in the **PS2 BIOS browser**, which is where the
title's line break and the animated icon's motion actually show.
