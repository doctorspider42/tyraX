# Text icons (button glyphs in text)

Write `{{cross}}` in any text and the game draws the ✕ button glyph there:

```
Press {{cross}} to jump
{{l1}}+{{r1}} Aim
{{action:use}} Open
```

It works in **every** text the project has — HUD texts, menu titles and entry
labels, Toggle/Choice option labels, loading-screen texts and *Display Text*
nodes — because both text renderers understand the placeholder, not just one of
them.

Managed in *Tools > UI Editor > **Button icons***. Every field a placeholder can
go into has a **`{{ }}`** button next to it that lists the tokens this project
understands, each with the glyph it draws - the legend for the syntax, and one
click inserts the token so there is nothing to remember.

## Two forms

| Placeholder | Resolves to |
| --- | --- |
| `{{cross}}`, `{{l1}}`, `{{start}}`, `{{coin}}` | the icon of that **name** |
| `{{use}}`, `{{jump}}`, `{{sprint}}` | shorthand: a token that is not an icon name but IS an **action** name resolves as that action |
| `{{action:jump}}` | the same thing spelled out (use it when a name could be either) |

Icon names win, so `{{cross}}` stays the ✕ glyph even in a project that also has
an action called "cross".

`{{action:...}}` is the one to reach for in a prompt: it names an action from
*Tools > Input Map* ([docs/input-bindings.md](input-bindings.md)) and follows the
binding, so "Press {{action:jump}} to jump" stays correct after a preset switch
or a player's rebind. One caveat by construction: in **baked** text (see below)
it is resolved at *build* time from the default preset — a text that must track a
runtime rebind has to be a *Display Text* node, or one of the two interaction
prompts, which bake their glyph as a live slot for exactly that reason.

A placeholder that names nothing stays on screen as literal text (`{{nope}}`),
so a typo is visible instead of silently vanishing.

## The built-in set

Every project is seeded with one icon per pad button — `cross`, `circle`,
`square`, `triangle`, `l1`, `l2`, `l3`, `r1`, `r2`, `r3`, `start`, `select`,
`dpadup`, `dpaddown`, `dpadleft`, `dpadright` — so `{{cross}}` works in a fresh
project with no setup. Those names are also what makes `{{action:x}}` work: it
resolves the action's pad button and then looks up the icon named after it
(lowercased).

The images are **drawn by the editor**, not shipped as blobs: the face buttons
are geometry in the DualShock colors (blue ✕, red ○, pink □, green △), the d-pad
four an arrow, and the shoulder/Start/Select ones their **label alone** - a
border around "R2" ate so much of the icon at text size that the letters were
unreadable on a TV, so the label gets the whole box instead. They are written
into `res/hud/icon-<name>.png` on the first build and never overwritten after
that.

Icons scale with the text they sit in, so a menu's *Row size* (Menu Editor) or a
text's own size grows the glyphs with the letters. A per-icon *Scale* nudges one
that needs to sit bigger or smaller than the rest.

**Overriding an icon is just replacing that PNG** — or pointing the entry at your
own file with *PNG...* in the icon manager. *Regenerate built-in PNGs* deletes
the generated files so the next build redraws them, which is the way back after
a hand-edit.

Unlike font glyphs, icons keep **their own colors** — a colored button glyph
should look like that button, and a custom PNG should look like what you drew.
They are not tinted with the color of the text they sit in.

## The interaction prompts

*Tools > UI Editor > USE prompt* holds both of them - the **USE prompt** and the
**PICK UP** variant shown instead when the looked-at object is pickable - and
each is explicitly either **Text** or **Image** (a radio pair, not "text when the
field is not empty": flipping to the image to compare should not throw away what
you typed).

A fresh project starts both on text, at `{{use}} USE` and `{{use}} PICK UP`, so a
prompt says which button to press and follows a rebind of the `use` action.

Either way the build produces one sprite per prompt and the game draws it the
same: text is rasterized to `res/hud/use-text.png` / `res/hud/pick-text.png` and
the prompt just points there. The two share the USE prompt's screen position; in
text mode each is sized by its own bake, in image mode PICK UP rides in the USE
prompt's box.

Existing projects stay on their images until you flip the switch - turning them
into text would have restyled every project's HUD behind the user's back.

A prompt in text mode is the one baked text that is **not** a build-time snapshot
of its icon. The bake writes the letters only and leaves the first `{{action}}`
glyph out, reporting where it would have sat (`USE_PROMPT_ICON_X/Y/SIZE` in
`hud_data.gen.hpp`); the game blits the **current** binding's icon into that hole
from the icon sheet every frame. So a player who rebinds `use` from □ to △ sees
the prompt change with it, with no rebuild — the text sprite is unchanged and only
one small quad follows the binding.

A prompt without an action token bakes whole (`ICON_ACTION` = -1) and no glyph is
drawn over it. Every *other* baked text still resolves `{{action:...}}` at build
time from the default preset; a runtime-varying one has to be a *Display Text*
node.

## Adding your own

*+ Add icon* in the manager, give it a name and a PNG. Nothing ties an icon to a
pad button: `{{coin}}`, `{{skull}}` and `{{warning}}` are perfectly good icons —
they simply have no action to resolve from.

*Scale* is the drawn height relative to the text's size (1 = as tall as a
capital), for a glyph that needs to sit a little bigger or smaller than the
letters around it.

## How it reaches the PS2

Two paths, because the engine has no font and the two kinds of text are made
very differently:

- **Baked text** (HUD texts, menus, loading screens) rasterizes to a PNG sprite
  at build time, so the icon is **composited straight into the sprite**
  (`menubake.cpp`: `textWidth`/`drawText` walk icon runs). Costs the game
  nothing at all — it is the same single quad it always was.
- **Runtime text** (*Display Text* nodes, a menu *Rebind key* row's binding)
  only knows its string while running, so its icons ship as one sheet,
  `res/hud/icons.png`, with the rects in `inc/icon_data.gen.hpp`. The generated
  game blits a sub-rect per icon (`resolveIconToken`/`drawFontText`), and the
  sheet reaches GS VRAM **only the first time a text actually draws an icon** —
  a project that uses no placeholders never pays for it.

Both sides derive their geometry from the same `iconAtlasLayout`, and the two
advance formulas are twins (`iconAdvance` in menubake.cpp,
`iconAdvanceFor` in the generated game) — change one and the other must follow,
or a baked and a runtime copy of the same string come out different widths.

## Related

- [docs/input-bindings.md](input-bindings.md) — the Input Map that
  `{{action:...}}` resolves against, and the in-game rebind rows (whose value is
  drawn as the bound button's icon).
- [docs/keyboard-mouse.md](keyboard-mouse.md) — there are no keyboard-key icons;
  that support is experimental and the placeholders cover pad buttons.
