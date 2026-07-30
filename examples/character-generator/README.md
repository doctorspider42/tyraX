# character-generator example

Four people, all built by *Tools > Character Generator* — the
[character generator](../../docs/character-generator.md) feature end to end:
bodies morphed from MakeHuman's CC0 data, clothes and hair fitted through the
same barycentric binding, one rig, and locomotion that came out of the
generator rather than out of a motion library.

Open `character-generator.tyra` in the editor and Build & Run (`F5`), or build
headless: `tyrax-editor.exe --build <this folder> --run`.

## What to do

You **are** a generated character. The third-person camera sits behind `hero`:

- **Walk** (left stick) and the avatar walks; push it to full tilt and it
  **runs**; let go and it **idles**. Nothing here is scripted - `hero.glb`
  simply contains clips named `idle` / `walk` / `run` / `jump`, which is what
  the generated game's third-person locomotion looks for, so a generated
  character is a working avatar the moment you drop it in.
- The three bystanders (`clerk`, `dockhand`, `kid`) are ordinary **Model**
  objects playing their first clip. They idle - breathing, weight shifting -
  because the generator writes `idle` first, so an autoplaying model is never
  caught standing in its bind pose.

## The cast, and why these four

They are deliberately spread across the generator's axes, and they are all the
same 23-bone Mixamo-named rig with the same four clips:

| | Sliders | Wardrobe | Triangles |
|---|---|---|---|
| `hero` | male, 1.82 m, muscular | casual suit, shoes, short hair | 2428 |
| `clerk` | female, 1.66 m | elegant suit, shoes, bob | 3496 |
| `dockhand` | male, 1.74 m, older, heavy | work suit, shoes, no hair | 2062 |
| `kid` | female, 1.28 m, child | casual suit, ponytail | 1780 |

All four were generated at **Detail: Low** and a **128² texture** - this is a
crowd, not a hero close-up, and four characters share one ~1.33 MB GS VRAM
budget. The scene runs at a full **50 FPS** with 15 resident textures and no
eviction; raising Detail or the texture size is exactly the knob to turn when
you have one character on screen instead of four.

Note what is *not* in the triangle counts: a dressed character's body is
**750 triangles, not 1460**, because every garment lists the body it covers and
that geometry is dropped. Dressing a character costs less than it looks.

## Making your own

*Tools > Character Generator*, move the sliders, pick clothes and hair, then
**Add to scene**. That writes a plain `.glb` into `res/models/characters/` and
drops in a Model object - from there it is an ordinary
[animated model](../../docs/animated-models.md), so the Animation Editor, the
`.tskl` bake with its distance LODs, Live Link and the NPC AI all work on it
without knowing it was generated.

To make one the player, set a Player object's model to it and name its
locomotion clips (`idle` / `walk` / `run` / `jump`) in *Properties*, which is
all that was done for `hero` here.

The bodies, clothes, hair and skins are MakeHuman **CC0** assets - see
[Credits](../../README.md#credits). `setup.ps1` fetches them; without them the
generator window says so and does nothing else.
