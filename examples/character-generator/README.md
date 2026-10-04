# character-generator example

Six people, all made by *Tools > Character Generator* - the
[character generator](../../docs/character-generator.md) end to end: bodies,
faces, skin and makeup from sliders, clothes and hair from the CC0 wardrobe,
motion-captured clips, and nothing hand-made.

Open `character-generator.tyra` in the editor and Build & Run (`F5`), or build
headless: `tyrax-editor --build <this folder> --run`.

## What to do

The game opens in the **Character Creator**: dress `hero` before you play.
Up/Down picks a row, Left/Right changes it - four colour looks, five
hairstyles, three hats, three sets of glasses, or none - L1/R1 turn him round,
Cross keeps it and Circle puts back what he wore. **Select** opens it again
any time. That is the player's own flow graph (On Start and On Button Select
into a Character Creator node), and `hero.chargen.json` lists the choices in
its `"options"` (docs/character-generator.md, "In-game character creator").

The screen itself is the `character` **menu**, picked in the node's *Menu*:
restyle it in *Tools > Menu Editor* like any other menu - stylesheet, labels,
row order, position (docs/character-generator.md, "The creator as a menu").
Clear the node's *Menu* and you get the built-in screen instead.

![The Character Creator in the example](../../docs/img/chargen-creator-menu.png)

You **are** a generated character. The third-person camera sits behind `hero`:
walk (left stick) and he walks, push to full tilt and he runs, let go and he
idles. Nothing is scripted - `hero.glb` contains clips named `idle` / `walk` /
`run` / `jump`, which is what the generated game's third-person locomotion
looks for.

The five bystanders are ordinary **Model** objects, each autoplaying a
different clip from the library: the dockhand stands with his arms folded, the
kid dances, the punk talks with his hands, the clerk and the elder idle.
Walk up to any of them and they look at you, blinking as they do; the punk's
jaw moves with his talking clip, the kid's ponytail and the elder's long skirt
swing. Nothing scripts that either: it is the generated rig's face and spring
bones (docs/character-generator.md, "A living face", "Spring bones").

Behind them stands a **crowd of eight commuters**: one character
(`commuter.glb`) in six colour schemes, made with the generator's *Crowd...*
button. They share one mesh, one atlas and one skin per frame; each colour
scheme is a 1 KB palette (`commuter_skin.v<k>.png` beside the model, fitted at
build time - docs/character-generator.md, "Crowds"). Pick one and look at
*Palette variant* in Properties. They do not stand still: each one has
*Wander* set, so they walk between random spots near where they were placed,
stop a while and pass each other on the right
(docs/navigation-ai.md, "Wandering").

## The cast

Every character has its **recipe** beside it: `res/models/characters/<name>.chargen.json`.
*Open recipe...* in the generator loads one for editing; the command line
rebuilds the model from it, byte for byte:

```
tyrax-editor --chargen res/models/characters/hero.chargen.json res/models/characters/hero.glb
```

| | Body | Wears | Atlas | Triangles |
|---|---|---|---|---|
| `hero` | man, 1.82 m, muscular | polo (recoloured navy), cargo pants, boots, short hair - plus 11 creator options and 3 colour looks | 256 | 4245 as worn (8901 in the file) |
| `clerk` | woman, 1.65 m, mostly East Asian | trouser suit (recoloured navy; the blouse and scarf keep theirs), T-bar shoes, square frames, a bun, lipstick | 128 | 4508 |
| `dockhand` | man, 1.76 m, older, heavy | work overalls, ankle boots, newsboy cap, buzz cut, stubble | 128 | 4551 |
| `kid` | child, 1.30 m | striped T-shirt (pattern), jean shorts, canvas shoes, ponytail | 128 | 4365 |
| `punk` | man, 1.78 m | casual outfit, black hero boots, 3D glasses, green messy hair | 128 | 4382 |
| `elder` | woman, 1.58 m, 90 | sweater, long skirt, flats, round glasses, grey bun | 128 | 4848 |
| `commuter` x 8 | man, 1.78 m | T-shirt, trousers, sneakers, short hair - five palette variants plus the original | 128 | 4211 |

Shirts, trousers and suits are **shells** - the body itself, pushed out and
painted - so they add no triangles; the counts above are a 3348-triangle woman's body
or a 3320-triangle man's, plus hair, shoes, hats, glasses and skirts. Those
mesh items share ONE accessory texture, so each character is two draw parts:
the body with its atlas, and everything else. The hero's creator options are
the exception - each is its own part and texture, so the game can show one per
slot; a hidden one is neither drawn nor skinned. The hero carries a 256 atlas
because he is on screen up close; the rest are a crowd at 128, which is what
keeps six characters well inside the GS VRAM budget. All six are pinned to
8-bit textures in the project (`textureQuality`) - skin bands badly at the
project's default 16 colours.

## Making your own

*Tools > Character Generator*: move sliders, or press **Randomize** until
someone interesting turns up, then **Add to scene**. That writes a plain `.glb`
and its recipe into `res/models/characters/` and drops in a Model object -
from there it is an ordinary [animated model](../../docs/animated-models.md),
so the Animation Editor, the `.tskl` bake with its distance LODs, Live Link and
the NPC AI all work on it without knowing it was generated.

Everything the generator uses is CC0 (MakeHuman's data and asset packs,
Quaternius' Universal Animation Library) and embedded in the editor - see
[Credits](../../README.md#credits).
