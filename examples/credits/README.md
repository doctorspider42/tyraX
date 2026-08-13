# credits example

Two [credits rolls](../../docs/credits.md) in one small project: a **scrolling
end roll** started from the title screen, and a **card-mode dedication** started
by a flow node mid-gameplay. Between them: both start paths, both motion modes,
the skip, and every way a roll can finish.

Open `credits.tyra` in the editor and Build & Run (`F5`), or build headless:
`tyrax-editor.exe --build <this folder> --run`.

## What to do

1. The game boots on the **CREDITS DEMO** title screen. Pick **CREDITS**.
   - The end roll scrolls up: a heading, a logo image, four **role / name**
     rows, a wrapped SPECIAL THANKS paragraph, then a page break and THE END.
   - `PRESS (X) TO SKIP` sits at the bottom — the glyph is baked from the
     *confirm* binding. Press Cross and the roll ends **back on the title
     screen**, its finish action; sitting through it lands in the same place.
2. Pick **START** to play (an empty checkerboard — this demo is about the
   credits, not the level), then press **L1** anywhere.
   - The **dedication** plays as three **cards**, one screenful each, cross-fading:
     `SIX MONTHS LATER`, a line about PlayStation 2s, and `MADE WITH TYRAX`.
   - It finishes with **Resume**, so the game comes back exactly where it was —
     and `THANKS FOR WATCHING` appears, because the same graph wires **On Credits
     Finished** to a *Set Text Visible*.

Unattended, with no controller (see the [testing skill](../../.claude/skills/tyra-testing/SKILL.md)):

```powershell
tyrax-editor.exe --pad <this folder> "press down; wait 0.5; press cross"  # roll the credits
tyrax-editor.exe --pad <this folder> "press cross"                        # skip -> title screen
tyrax-editor.exe --pad <this folder> "press l1"                           # the dedication cards
```

## How it is wired

- **Tools > Credits Editor** holds both rolls:
  - `end-credits` — *Scroll up* at 34 px/s, 512 px pages, skippable after 1 s
    with a hint, finish **Open menu > title**. Its blocks were **imported from
    [`credits.txt`](credits.txt)** (the plain-text credits format), so editing
    that file and hitting *Re-import* rewrites the roll.
  - `dedication` — *Cards* at 3 s each, 256 px pages, no hint, finish
    **Resume the game**.
- **Menu Editor** — the `title` menu (opens at boot) has `START` (close) and
  `CREDITS`, whose action is **Play credits > end-credits**.
- The Player's flow graph (Flow Graph window with `player-1` selected):

  ```
  On Button L1        -> Play Credits "dedication"
  On Credits Finished -> Set Text Visible "watched"
  ```

  The trigger fires for **either** roll and however it ended — ran out,
  skipped, or stopped by a node.
- `res/credits/logo.png` is the Image block's asset — an ordinary checked-in
  file. The baked page strips next to it in `res/credits/pages/` are build
  output (regenerated every build, git-ignored).

## Things worth noticing

- A roll **owns the screen and the pad**: nothing is simulated behind it, so
  pressing L1 mid-walk parks the player exactly where they were.
- The editor's preview draws **the same baked pages** the console does — the
  scroll you scrub in the Credits Editor is the scroll the PS2 shows.
- The report line under the preview reads e.g. `3/16 pages | 33 s | ~216 KB
  VRAM` — pages are textures, and the GS pins every one it draws, which is why
  a roll has a page budget at all (see [docs/credits.md](../../docs/credits.md)).
