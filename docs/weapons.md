# Weapons and combat

Firearms and melee weapons with configurable damage, ammunition, recoil,
projectiles, particle effects and enemies that shoot back — authored in
**Tools > Weapon Editor**, carried by scene objects, driven by fourteen
**Combat** flow-graph nodes, and compiled into a generated PS2 runtime that a
project without combat never pays for.

Working demos: [examples/weapons](../examples/weapons) — a quick shooting
range — and [examples/weapons-arena](../examples/weapons-arena), which lays
out every axis of the feature as six stations along one walk.

## The shape of it

A **weapon** is a project-wide definition, like a font or a menu — it is not
placed anywhere. **Objects carry weapons by name**: the scene's *Player*
object is the player's inventory (the first entry is equipped at spawn), and
any other object's entry 0 is what it shoots with. That one decision is why
there is no separate "actor" concept anywhere in the system: an armed guard
is an ordinary object with a loadout, and the thing that makes it dangerous
is a number (*Auto-fire range*) on the same panel.

The **weapon in your hands is also an ordinary scene object**. A weapon
definition points at one by name; while that weapon is equipped the runtime
pins the object in front of the camera and hides it otherwise. Nothing else
in the engine had to learn what a viewmodel is — the object lights, materials,
LODs and previews exactly like anything else you place, and you can see it in
the viewport while you position it.

The *Viewmodel* tab previews it **from the player's eye** — see
[The viewmodel preview](#the-viewmodel-preview) below.

## Weapon kinds

| Kind | What it does |
|---|---|
| **Firearm (hitscan)** | The shot arrives the frame it is fired: a ray against object bounding spheres and the terrain heightmap. What every PS2-era gun was. |
| **Firearm (projectile)** | A visible body of matter flies out, drops under gravity and damages what it touches. Grenades, plasma, arrows. Optional blast radius. |
| **Melee** | An arc swept in front of the attacker: everything damageable inside the cone and within reach is hit once per swing. |

### The knobs

**Damage** is per hit — and per *pellet*, so a shotgun's real output is
Damage × Pellets at point blank. **Falloff** is the fraction of it lost at
maximum range (0 = a hit is a hit at any distance), measured over the
weapon's own *Range* — so a rifle that reaches 120 units does not lose half
its damage at 40 the way a pistol would. For a **projectile** the distance
that counts is the ground the projectile actually covered, not the range it
was capable of: a grenade that goes off at your feet does full damage.
**Impulse** is a physics shove into a rigid body that is hit.

**Firing**: rate in shots per second, *Automatic* (hold the button) vs one
shot per press, *Spread* (the shot's cone half-angle), *Pellets*, *Recoil*
in degrees of view kick, *Rumble*.

**Ammunition**: *Magazine* (0 = no magazine at all, never reloads, capped at
999), *Reserve* spare rounds (-1 = bottomless, otherwise up to 9999) and
*Reload time*. Firing dry plays the empty sound and starts a reload by
itself. Both counts reach the console as 16-bit values, which is where those
caps come from; the project loader clamps them to the same ranges the Weapon
Editor offers, so a hand-edited `project.json` cannot wrap a magazine
negative.

**Projectile** settings (kind 1) and **Swing** settings (kind 2) appear only
for the kind that reads them.

## Effects

Every weapon carries three particle bursts — **Muzzle**, **Impact** and
**Blood** — plus an optional **tracer** streak. A burst is a kind (flash,
sparks, smoke, blood, debris), a color, a size, a particle count, a lifetime
and a speed.

Which burst a hit throws is decided by the **target**, not only the weapon: a
damageable object bleeds where a wall sparks. An object can override that
outright with *Properties > Combat > Hit effect* (sparks / blood / dust /
none), which is how you make a metal barrel spark and a sandbag puff dust
with the same rifle.

All of this comes out of **one shared 128-particle pool** (`FX_MAX` in the
generated game): a burst is fired at a world point, lives out its seconds and
frees its slots, so a firefight costs exactly as much memory as a quiet room.
A burst fired into a full pool steals the stalest slots rather than being
dropped — a muzzle flash that silently does not appear reads as a bug.

The pool is not weapon-only. Any script can throw a burst:

```cpp
const float pos[3] = {x, y, z}, up[3] = {0, 1, 0};
const float col[3] = {1.0F, 0.6F, 0.2F};
if (ctx.spawnFx) ctx.spawnFx(2 /*sparks*/, pos, up, col, 0.05F, 10, 0.4F, 6.0F);
```

Projectiles reuse the same pool for their body and trail: the runtime
re-seeds a one-frame burst at the projectile's position every frame, so a
grenade in the air needs no extra render path at all.

## The viewmodel preview

*Weapon Editor > Viewmodel* renders the viewmodel object live, and its default
camera is **not** a turntable: it sits exactly where the player's does — at the
origin, looking down +Z, at the engine's 60° vertical field of view, at the
project's own aspect ratio (*Preferences > Display > Widescreen*). That is the
only camera the numbers on that tab mean anything against. An *Offset* of
(0.2, −0.15, 0.6) is not a place in the world; it is a place on the **screen**,
and eyeballing it there beats a build-and-look round trip.

- **Offset X is a screen direction.** +X is right *on screen*, which is world
  −X when the player faces +Z. The preview folds that for you.
- **The crosshair** marks the screen centre; the **orange dot** is the *Muzzle*
  offset, drawn through the geometry so a muzzle correctly buried in the barrel
  is still visible. Toggle it with *Muzzle*.
- **Turntable** swaps the eye for an orbit around the weapon (drag to rotate,
  wheel to dolly) when you want to look at the model rather than at its framing.
- **Fire / Reload** run one event through the same springs and curves the game
  uses; **Walk** drives the bob, **Auto** holds the trigger at the weapon's own
  *Rate*. In clip mode the buttons switch clips exactly as the runtime does, and
  the status line under the image names the clip that is playing.
- A viewmodel object that is a **placeholder primitive** rather than a model is
  drawn as its unit shape, so the offsets can be dialled in before the real
  asset exists.

The preview is a **twin of the generated runtime**: `weaponPreviewPose` in
`src/weaponedit.cpp` reproduces `wpnPinViewModels` and the sway / bob / kick /
reload / swing block that `src/templates.cpp` emits. Changing either without the
other makes the preview lie, which is the only way it can stop being useful.

Shading comes from the **scene's** own ambience, like every other preview here —
what you see is what will ship. A scene authored for night previews dark on
purpose.

## Viewmodel animation

Two ways to make the weapon in your hands move, and the choice follows the
**asset** rather than being a matter of taste (*Weapon Editor > Animation*).

### Procedural (`animMode` 0) — the default

The runtime animates the viewmodel's **transform** from ten numbers. No
animated model is needed, which matters because a **generated weapon is a
static `.obj` and can never carry clips** — procedural motion is the only
animation it will ever have.

| Knob | What moves |
|---|---|
| **Sway** / **Sway speed** | The hands never being quite still. 0 welds the weapon to the camera. |
| **Walk bob** | A figure-eight that scales with the player's *actual* planar speed, so it stops when they do. |
| **Kick back** / **Kick pitch** / **Recovery** | The weapon driving into the screen and rising per shot, on a spring. |
| **Reload dip** / **Reload roll** | The weapon dropping out of the aim and rolling, spread over the weapon's own *Reload time*. |
| **Swing reach** / **Swing chop** | Melee only: the lunge and the chop, over *Swing time*. |

**Kick is not Recoil.** *Recoil* (on the Firing tab) moves the **aim** — the
player has to fight it. *Kick back / Kick pitch* move the **weapon** in the
hands and never touch where the shot goes. Keeping them separate is what lets
a heavy revolver jolt hard without becoming unaimable, and a mounted gun
recoil without visibly moving.

Rather than tune ten numbers to discover what "a pistol" feels like, hit a
**motion preset**: *Pistol snap*, *Heavy recoil*, *Automatic chatter*,
*Launcher shove*, *Blade swing* or *Locked down*. A preset overwrites only the
motion fields, so applying one to a finished weapon changes how it moves and
nothing else. **Create viewmodel + add to scene** picks the preset matching the
generated kind, so a fresh weapon arrives already moving.

### Clips (`animMode` 1) — your own animation

Point the weapon's viewmodel at an **animated `.glb`/`.fbx`** (the ordinary
[animated model](animated-models.md) pipeline — the viewmodel is just a scene
object, so nothing special is needed) and name up to four of its clips:

| Clip | When it plays |
|---|---|
| **Idle** | Loops whenever nothing else is. |
| **Fire** | One shot per shot. A nine-pellet blast restarts it **once**, not nine times. |
| **Reload** | When a reload starts. Author it to the weapon's *Reload time* or the animation and the mechanic will disagree. |
| **Equip** | Once when the weapon is drawn; *Idle* takes over at the end. |

In clip mode the procedural **kick, reload dip and swing stand aside** — the
clip owns them. **Sway and walk bob stay on**, because a baked clip has no way
to know how fast the player is walking, and that is precisely the part a clip
cannot do. An empty clip name means "this state does not change the clip", and
clip mode on a *static* model is a harmless no-op: nothing resolves, so the
build never breaks over it.

## Controls

Bound in the generated `inc/controls.hpp` (a user-owned file — change them
there):

| Button | Action |
|---|---|
| **R1** (`BTN_FIRE`) | Fire / swing |
| **Triangle** (`BTN_RELOAD`) | Reload |
| **L1** (`BTN_WEAPON_NEXT`) | Next carried weapon |

With *Keyboard & mouse controls* on, the mouse's **left button** also pulls
the trigger (it keeps driving USE as well — USE needs the player close to and
looking at a usable object, so the two do not collide in practice).

**Recoil** is a spring the weapon runtime owns end to end: firing gives it a
velocity impulse and the spring settles it back down, with the per-frame
*delta* handed to the walker (`ScriptContext::viewKickPitch`). Modelling it
as a delta rather than a remembered angle is what lets the player fight the
recoil with the stick instead of having their aim snapped back.

## Per-object combat (Properties > Combat)

- **Damageable** — the master switch. Off, the object is scenery: a shot
  leaves an impact effect and nothing else. On, it has **Health**, fires the
  **On Damaged** / **On Killed** triggers, and can die.
- **On death** — *Hide*, *Despawn*, *Stay* (nothing happens, script it from
  On Killed) or *Knock over*, which hands the corpse to the rigid-body
  simulation with a shove so it topples. Ignored on a *Player* object: the
  player's death is entirely yours to script.
- **Hit effect** — the impact override described above.
- **Weapons carried** — the loadout.
- **Auto-fire range / Vision cone** — above 0 the object fires at the player
  on its own while they are within range, inside the cone around its facing,
  and in terrain line of sight. NPCs deliberately fire slower than their
  weapon allows (a 0.35–0.85 s pause between shots): a hostile that empties a
  magazine at the weapon's true rate is not a fight, it is an execution.

Auto-fire composes with the AI nodes ([navigation-ai.md](navigation-ai.md)):
*On Player Seen → Chase Player* on an object that also has an auto-fire range
gives you a guard that closes in **and** shoots, with no scripting beyond the
two nodes.

## The Combat nodes

Every node's target is an **object** — from an object link, or the graph's
owner ("self") — and `str` names the **weapon**, exactly like the Animation
node where `str` is the clip. So a Give Weapon node on the Player object's
graph arms the player.

| Node | What it does |
|---|---|
| **Give Weapon** | Adds a weapon (and spare ammo) to the target's inventory; equips it if the target had nothing. |
| **Equip Weapon** | `equip` draws the named weapon, `next` cycles — the same thing the pad button does. |
| **Fire Weapon** | One shot right now, ignoring the fire rate and the magazine, so a scripted gunshot goes off on the frame the graph says so. |
| **Reload Weapon** | Starts a reload. |
| **Set Ammo** | Sets magazine / reserve (each < 0 = leave alone; reserve -1 = bottomless). |
| **Apply Damage** | Four exec pins: `damage`, `heal` (which revives), `kill`, `set`. |
| **On Damaged** / **On Killed** | Fire the frame after the target takes damage / dies. Also bool sources. |
| **On Weapon Fired** | Fires when the named weapon is fired by anyone (empty = any). |
| **Has Weapon**, **Ammo At Least**, **Health At Least** | Pure bools for the logic gates. |
| **Get Ammo As Text**, **Get Health As Text** | Pure text — wire into a **Display Text** node for an ammo counter or a health readout. |

The ammo counter is the whole HUD story: there is no bespoke weapon HUD,
because *Get Ammo As Text → Display Text* already draws a live value with any
Font Manager font, at any screen position, in any color.

### Trigger timing

**On Damaged**, **On Killed** and **On Weapon Fired** are one-frame flags
published by the weapon runtime, and the publication *is* the edge — two hits
in consecutive frames fire the trigger twice. A flag raised anywhere during
frame N is published for the whole of frame N+1, which is what makes them
independent of the link order of the generated scripts. (One frame of latency,
invisible in play; the alternative was a trigger that fired for some graphs
and not others depending on which `.cpp` the linker put first.)

## Two rules the runtime enforces for you

**The combat state syncs on first TOUCH, not first tick.** Generated scripts
run in link order, so a graph's *On Start* can hand the player a weapon before
the weapon script has ever ticked. If the per-scene sync ran lazily from that
tick it would wipe the inventory it was handed a moment earlier — which is
exactly the bug this cost, once. Every `wpn*` entry point calls the sync
first, so whoever touches combat first triggers it and the ordering stops
mattering.

**A generated viewmodel is auto-framed.** The game has one field of view for
the world and the weapon (a PS2 has no separate viewmodel FOV), so a life-size
rifle half a unit from the eye covers a quarter of the screen. Every FPS
solves this by rendering a miniature; *Create viewmodel + add to scene* sets
`viewScale` so each weapon lands at the same ~0.38-unit apparent length and
moves the muzzle offset to the end of the scaled barrel. A pistol is already
small enough and clamps to 1. Hand-authored viewmodels are on you — if yours
fills the screen, that is the knob.

## Generating weapon models

**Tools > Weapon Editor > Viewmodel > Generate a model** builds a low-poly
weapon from numbers (`src/weapongen.cpp`): pistol, revolver, SMG, rifle,
shotgun, knife, sword, axe, crowbar. *Create viewmodel + add to scene* writes
the `.obj`/`.mtl` into `res/models/weapons/`, drops a Model object into the
scene and points the weapon's viewmodel at it — one click from a weapon
definition to something you can see in your hands.

They are 48–190 triangles, flat-shaded, three flat-`Kd` materials and no
textures at all: a weapon is small on screen, the baked vertex lighting
already reads the facets, and a texture per gun is GS VRAM the effects want
more ([gs-vram.md](gs-vram.md)).

Everything is deterministic in the parameters, so dragging *Length* scales the
weapon rather than reshuffling it. **The orientation convention is
load-bearing**: the model points down **+Z**, +Y is up, and the **origin is
where the hand grips it** — that is what makes the viewmodel offsets work
without per-model fiddling, and it is what the runtime assumes when it aims
the model down the view ray.

These exist because a weapon system needs weapons to point at and every free
gun model on the internet arrives with a licence the project would then have
to carry. Generated geometry is the project's own content, with no strings
attached.

## What it costs

**A project with no weapons, no damageable objects and no Combat nodes
generates no combat code at all** — `weapon_data.gen.hpp`,
`inc/scripts/weapons.gen.hpp` and `src/gen/weapons.gen.cpp` collapse to a
one-line comment, the same pay-for-what-you-use rule as the AI navigation.
(Health and damage are usable *without* defining a single weapon; that counts
as combat and generates the runtime with an empty weapon table.)

When it is on, the runtime is 2002-shaped: fixed arrays sized at compile time
(64 combat actors, 24 projectiles in the air, 128 burst particles), one cheap
LCG, no allocation anywhere in the per-frame path, and hitscan against
bounding spheres plus the heightmap rather than anything resembling a physics
broadphase. Scenes with no armed or damageable objects fall straight through
every loop.

Two structural consequences worth knowing:

- **Damageable and armed objects never join a static batch**, and neither does
  a viewmodel. A dying object is hidden, despawned or handed to the physics
  simulation, and a viewmodel is re-posed every frame — none of which a merged
  static bag can represent.
- **A viewmodel's collision is forced off** by the runtime at scene load. The
  weapon sits half a unit from the eye, and a player who walks into their own
  gun is a bug nobody should have to find in the editor.

## Limits

- At most **32 weapons** per project (the carried set is a 32-bit mask; the
  generated code static-asserts on it).
- Combat only involves the first **64 runtime objects** of a scene.
- Hit detection is a **bounding sphere** — half the object's largest scale
  axis, the same approximation the USE picker and the Raycast node use.
- **Player 2 does not carry weapons.** Combat is single-aim: one recoil
  spring, one viewmodel, one trigger.
- The Player object's *On death* action is ignored; script it from **On
  Killed**.

## Files

| File | Role |
|---|---|
| `src/project.hpp` | `WeaponDef`, `WeaponFx`, `Project::weapons`, the per-object combat fields |
| `src/weaponedit.cpp` | Tools > Weapon Editor, the weapon picker, rename retargeting, the viewmodel preview + its runtime pose twin |
| `src/weapongen.cpp/.hpp` | The procedural model generator (host-only, no GL) |
| `src/viewport.cpp` | `renderWeaponPreview` — the preview's own framebuffer and eye camera |
| `src/templates.cpp` | `weaponDataHeader` / `weaponsHeader` / `weaponsSource`, the FX burst pool in the game template |
| `src/flowgraph.hpp` | The fourteen Combat node types |
| `inc/weapon_data.gen.hpp` | Generated: `WEAPON_DEFS`, viewmodel/loadout/auto-fire side tables |
| `src/gen/weapons.gen.cpp` | Generated: the whole combat runtime, one global `Script` |
