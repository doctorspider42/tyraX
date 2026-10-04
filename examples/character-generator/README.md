# character-generator example

Six people, all made by *Tools > Character Generator* - the
[character generator](../../docs/character-generator.md) end to end: bodies,
faces, skin and makeup from sliders, clothes and hair from the CC0 wardrobe,
motion-captured clips, and nothing hand-made.

Open `character-generator.tyra` in the editor and Build & Run (`F5`), or build
headless: `tyrax-editor --build <this folder> --run`.

## What to do

You **are** a generated character. The third-person camera sits behind `hero`:
walk (left stick) and he walks, push to full tilt and he runs, let go and he
idles. Nothing is scripted - `hero.glb` contains clips named `idle` / `walk` /
`run` / `jump`, which is what the generated game's third-person locomotion
looks for.

The five bystanders are ordinary **Model** objects, each autoplaying a
different clip from the library: the dockhand stands with his arms folded, the
kid dances, the punk talks with his hands, the clerk and the elder idle.
Walk up to any of them and they look at you, blinking as they do; the punk's
jaw moves with his talking clip. Nothing scripts that either: it is the
generated rig's face (docs/character-generator.md, "A living face").

## The cast

Every character has its **recipe** beside it: `res/models/characters/<name>.chargen.json`.
*Open recipe...* in the generator loads one for editing; the command line
rebuilds the model from it, byte for byte:

```
tyrax-editor --chargen res/models/characters/hero.chargen.json res/models/characters/hero.glb
```

| | Body | Wears | Atlas | Triangles |
|---|---|---|---|---|
| `hero` | man, 1.82 m, muscular | polo (recoloured navy), cargo pants, boots, short hair | 256 | 4245 |
| `clerk` | woman, 1.65 m, mostly East Asian | trouser suit (recoloured navy; the blouse and scarf keep theirs), T-bar shoes, square frames, a bun, lipstick | 128 | 4508 |
| `dockhand` | man, 1.76 m, older, heavy | work overalls, ankle boots, newsboy cap, buzz cut, stubble | 128 | 4551 |
| `kid` | child, 1.30 m | striped T-shirt (pattern), jean shorts, canvas shoes, ponytail | 128 | 4365 |
| `punk` | man, 1.78 m | casual outfit, black hero boots, 3D glasses, green messy hair | 128 | 4382 |
| `elder` | woman, 1.58 m, 90 | sweater, long skirt, flats, round glasses, grey bun | 128 | 4848 |

Shirts, trousers and suits are **shells** - the body itself, pushed out and
painted - so they add no triangles; the counts above are a 3348-triangle woman's body
or a 3320-triangle man's, plus hair, shoes, hats, glasses and skirts. Those
mesh items share ONE accessory texture, so each character is two draw parts:
the body with its atlas, and everything else. The hero carries a 256 atlas
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
