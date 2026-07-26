# weapons-arena example

Every axis of the [weapons and combat](../../docs/weapons.md) feature laid out
as **six stations along one walk** — six weapons to pick up, five surfaces
that react differently to the same shot, all four death actions side by side,
a blast-radius pit, enemies that shoot back (one of which chases you while it
does), and a melee alley. Where [examples/weapons](../weapons) is the quick
shooting range, this is the whole feature with nothing left out.

Open `weapons-arena.tyra` in the editor and Build & Run (`F5`), or build
headless: `tyrax-editor.exe --build <this folder> --run`.

## Controls

| Button | Action |
|---|---|
| **Square** | Pick up the weapon you are standing at |
| **R1** | Fire / swing |
| **Triangle** | Reload |
| **L1** | Next weapon |
| **Start** | Respawn after you die |

You start **unarmed** — the arsenal is the first thing in front of you.

## The six stations

**1. The arsenal** (straight ahead at spawn). Six glowing pedestals; USE takes
the weapon and the pedestal goes dark. Each is an `On Used → Give Weapon`
pair, which is the whole pickup system — there is no pickup object type.

| Weapon | What it is for |
|---|---|
| **Pistol** | 22 damage, 12 rounds, tracers. The baseline. |
| **Revolver** | 48 damage, 6 rounds, a 4° view kick. Slow and mean. |
| **SMG** | **Automatic** — hold R1. 11 damage, 32 rounds, wide spread. |
| **Shotgun** | 9 pellets × 13, 0.8 falloff over 30 units. A knife fight tool. |
| **Launcher** | A **projectile** that arcs under gravity with a 7-unit blast. |
| **Axe** | **Melee**: 70 damage in a 65° arc, and no ammunition at all. |

**2. The reaction wall** (left of the arsenal, on the long plinth). Five
targets, identical except for their *Hit effect*: steel **sparks**, flesh
**bleeds**, the sandbag puffs **dust**, the crate takes the weapon's own
impact effect, and the pale one is set to **None** and swallows the shot
without a mark. One weapon, five reactions — the target has the last word,
not the gun.

**3. The four deaths** (centre). Four dummies, 70 health each, one per
*On death* action: **Hide**, **Despawn**, **Stay** (it just stands there at
zero health — for a scripted death) and **Knock over**, which hands the
corpse to the rigid-body simulation and shoves it so it topples.

**4. The barrel pit** (far left). Nine barrels in a walled bowl. One Launcher
shell in the middle takes most of them: the blast damages everything within
7 units, weaker toward the edge. Try clearing it with the Pistol first to feel
the difference.

**5. The firing squad** (far end, behind the grey wall). Three turrets with
28–34 unit auto-fire ranges and 45–75° vision cones, plus a **guard** on the
right that carries an SMG *and* runs an `On Player Seen → Chase Player`
graph — auto-fire and the [AI nodes](../../docs/navigation-ai.md) compose with
no glue, so it closes in **and** shoots. Auto-fire needs terrain line of
sight, which is what makes the wall real cover: step out and your HP counter
starts dropping, duck back and it stops.

**6. The melee alley** (right). A narrow corridor of four dummies where the
Axe is the correct tool and the Launcher is a mistake.

## The HUD

**AMMO**, **HP** and **KILLS** are three `Display Text` nodes fed by pure
sources — `Get Ammo As Text`, `Get Health As Text` and `Get Save Value` over a
save value the `On Killed` triggers increment. There is no weapon HUD in the
engine; this is all of it.

Dying is scripted too: the Player object's own *On death* action is
deliberately ignored by the runtime, so an `On Killed` trigger draws the
game-over text and **Start** heals, respawns and re-arms you.

## Assets

Everything here is generated, so the example carries no third-party licence:

- the six weapon models come from **Tools > Weapon Editor > Generate a model**
  (48–184 triangles each, three flat-`Kd` materials, no textures);
- the nine sounds in `res/sfx/` were **synthesized** — a gunshot is a noise
  transient plus a low thump, and an explosion is integrated (brown) noise
  with a long decay.
