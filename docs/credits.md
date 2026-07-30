# Credits rolls

A **credits roll** is an end-credits screen: headings, role/name rows, lines,
images and page breaks that scroll up over music, with a skip button and
somewhere to go when it is over. Rolls are project-wide data (like Ambience
presets or Loading Screens), authored in **Tools > Credits Editor**.

A roll **owns the screen and the pad** while it plays. Gameplay, scripts and
flow graphs are frozen, nothing is rendered behind it, and when it ends — or the
player skips it — it runs its own **finish action**: resume the game, switch
scene, open a menu, fire a flow event, or hold the last frame forever.

## Starting a roll

Three ways, all interchangeable:

- a **menu row** — *Menu Editor*, a row with the action **Play credits** and the
  roll as its target. The menu closes first, so a title screen's `CREDITS` row
  hands over a clean screen (and typically has the roll open that title screen
  again when it finishes).
- the **Play Credits** flow node (param = the roll's name).
- **Stop Credits** ends one early from a graph — exactly as a player's skip
  does, finish action included.

**On Credits Finished** fires the frame a roll stops, however it stopped, and
its bool output is "credits are rolling right now".

## Anatomy of a roll

A roll is a vertical **flow of blocks**. Everything is laid out on the host and
baked to pixels at build time (the PS2 engine has no font), so anything the
editor can draw, the console can show.

| Block | What it is |
|---|---|
| **Heading** | a section title (`CAST`), the roll's heading size + color |
| **Role / name** | the classic two-column credit: role right-aligned against the gutter, name(s) left-aligned after it. Several names for one role = one per line |
| **Line** | a line or paragraph, wrapped to the page width, left/centered/right |
| **Image** | a PNG, scaled to a fraction of the page width (a studio logo, a portrait) |
| **Gap** | vertical space |
| **Page break** | jumps to the next page: a screenful of nothing when scrolling, a new **card** in card mode |

Per block you can override the size, typeface and color; leaving those at their
defaults (size 0, no font, no own color) inherits the roll's, so restyling a
whole roll is one edit at the top. Text carries [inline icons](text-icons.md) —
`{{cross}}` draws the button glyph.

Roll-wide settings: background color, an optional **still backdrop** image
(which does not scroll), the default typeface, heading/body sizes and colors, a
drop shadow, page width, side margin, column gap and line spacing.

### Motion

- **Scroll up** — the strip walks up the screen at *Speed* px/s. This is the
  normal credits roll.
- **Cards** — the roll is split at every page break and each card is shown for
  *Card time* seconds, cross-fading. For title cards, dedications, a "six months
  later".

Plus a *start delay*, an *end hold* after the last block leaves, and fade
in/out (a fade is one quad of the background color over the whole frame — the
same trick the Cutscene Director's fade uses).

### Music

A roll can start one of the project's music tracks when it begins (`<keep
playing>` leaves whatever was playing alone), loop it, set its volume, and stop
it at the end — which is what you want for a roll that hands over to a menu.

### Skipping

*Player can skip* accepts the **menu confirm** action and Start by default, or
one named [input action](input-bindings.md) if you pick it. *Ignore for* is a
short deadline before a skip counts, so a button held from the moment before the
roll started does not skip it instantly. The **hint** (`PRESS {{cross}} TO
SKIP`) is a baked text sprite placed anywhere on screen; being baked, its glyph
is the binding at BUILD time — for one that follows an in-game rebind, use a
*Display Text* node instead.

A skip runs the finish action, never just a stop: a player who skips lands
exactly where a player who watched lands.

## Importing a text file

Long rolls belong in a text editor or a spreadsheet, so *Import text...* reads
one and replaces the roll's blocks. The format is plain text:

```
# CAST
Producer: Ada Lovelace
Programming: Grace Hopper
             Alan Turing

[image res/credits/studio.png 0.4]

> A centered line
< A left-aligned line
| A right-aligned line

---
# THE END
```

- `# TEXT` (or `## TEXT`) — a Heading
- `Role: Name` — a Role/name row (the colon must be followed by a space, and the
  role side has to be short — a URL or a timestamp stays a plain line)
- `> `, `< `, `| ` — a Line, centered / left / right
- `[image <path> [scale]]` — an Image block, scale 0..1 of the page width
- `---` (or `***`) — a Page break
- a blank line — a Gap (repeated blanks collapse into one)
- `//` — a comment, dropped
- anything else — a plain centered Line

The source path is remembered, so **Re-import** picks up an edited file. Import
replaces every block, so per-block styling done in the editor is lost — style
the roll (or use `#`/`>` markers) rather than hand-tuning blocks you plan to
re-import.

## How it reaches the game

The roll is baked into a vertical **strip of page textures**
(`res/credits/pages/<roll>-<k>.png`, 256 px tall, 256 or 512 wide) that the
runtime scrolls past the screen; the tables land in `inc/credits_data.gen.hpp`
and the player in `src/gen/credits.gen.cpp`. That folder belongs to the build:
every roll's pages are rewritten there on each build and anything no roll claims
is swept out, so **images an Image block points at live one level up**, in
`res/credits/` (which is also where the editor imports them), where the build
never touches them. Only the pages overlapping the screen are
submitted, so a roll of any length costs **two sprites a frame**.

Pages instead of one sprite per line is a VRAM decision, and it is the one
constraint worth knowing about:

> The GS pins every texture it draws in a ~1.33 MB budget with no eviction
> (see [gs-vram.md](gs-vram.md)). A page at 4-bit is ~64 KB (512 px wide) or
> ~32 KB (256 px), so **16 pages** is the cap — 4096 px of roll, over three
> minutes at 20 px/s. The editor prints the page count, the running time and the
> VRAM estimate under the preview, and says *content clipped* when a roll runs
> past the cap (anything beyond it is not baked and never shows). A longer roll
> wants a slower speed, 256 px pages, card mode, or a second roll.

The **page depth** (*4-bit / 8-bit / full color*) is the roll's own, like a font
atlas carries its own: with no backdrop the pages are baked as opaque plates of
the background color, so 16 colors comfortably hold the text's antialiasing.
Give the roll a backdrop and the pages have to stay transparent for it to show
through, which usually wants 8-bit — and costs twice the VRAM per page.

The preview in the Credits Editor draws **the same baked pages** the console
gets, positioned by the same arithmetic the generated player uses, so what
scrolls in the editor is what scrolls in the game.

## The window

Settings on top, the preview below, and a **splitter between them** — drag it to
trade one for the other (writing a roll wants the block list tall; judging its
timing wants the preview big). The split is remembered per machine, not per
project, like the Material Editor's.

Beside the preview is the **Jump to** list: every block with the moment it is
centred on screen, so clicking `1:04  Music: Wendy Carlos` scrubs straight to the
frame that answers "does that row look right". It selects the block too, and
anything past the page budget is listed in amber (it is not baked and never
shows).

Rolls are project-wide data: edits save immediately and are **not** part of
undo/redo.
