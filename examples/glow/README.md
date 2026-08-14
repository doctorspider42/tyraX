# glow — a midnight tour of emissive materials

First-person walk through a pitch-black scene built entirely out of
[emissive materials](../../docs/emissive-materials.md): four stations, one per
axis of the effect, along the +Z walk from the spawn — plus a bloom pulser so
you can watch the halo breathe without touching the pad.

The scene's **one** directional light is almost off (`ambient 0.06`,
`diffuse 0.04`, cold blue). Every warm pixel is either a material lighting
itself or a material lighting something else.

## The tour (walk forward from the spawn)

### Station 1 — the floor (z ≈ −18)

Two signs on identical pedestals, with **identical `Kd`** (`0.1 0.9 1.0`):

| Side | Material | `Ke` |
| --- | --- | --- |
| Right, `s1-sign-MATTE` | `sign-matte.mtl` | *none* |
| Left, `s1-sign-GLOW` | `sign-glow.mtl` | `0.1 0.9 1.0` |

The right one is a barely visible dark silhouette; the left one is full cyan.
Same color, same scene, one line of `.mtl`. Neither lights its pedestal —
that's station 3's job, kept separate on purpose.

### Station 2 — the white-hot ladder (z ≈ −8)

Three boxes, all at **glow 1** in the same deep red, differing only in the
**White-hot core**:

| Box | White-hot | Resolved `Ke` | What you see |
| --- | --- | --- | --- |
| `s2-white-hot-0` | 0 | `1.00 0.15 0.10` | deep red, **red** halo (only R clears the threshold) |
| `s2-white-hot-1` | 0.35 | `1.35 0.50 0.45` | salmon, warmer and wider halo |
| `s2-white-hot-2` | 0.7 | `1.70 0.85 0.80` | blown out to warm white, **white** halo |

This answers "it's already at maximum, how do I make it brighter?". An
untextured surface at glow 1 is at the framebuffer ceiling in its own hue; the
only direction left is *whiter*. The halo color changes too — the bright pass
runs per channel.

### Station 3 — the lamp (z ≈ 2)

`s3-lava-pit` is a plane with `# tyra-glow-light 13 1.9`: it lights its
surroundings. Four `concrete.mtl` pillars stand around it and a **fifth
identical pillar** (`s3-pillar-OUT-OF-REACH`) sits at x = 19, past the 13-unit
reach. Same material, same everything — one is orange, one is black.

The pool on the ground has a quadratic falloff measured from the **plane's
surface**, not its center — which is why the whole plate lights evenly instead
of blooming from a point.

`s3-shadow-wall` is the shadow demo — a slab between the pit and the far left
pillar. The light doesn't pass through it: the pillar behind is dark on the
side facing the pit, and the ground carries the wall's shadow. Untick the
wall's *Cast shadow* in Properties and the shadow disappears (the flag is
shared with the ambient occlusion). Shadows come from the analytic box/sphere
each object reduces to, so the *shape* is rectangular — a wall throws a
rectangle, not its silhouette — but the **edge is soft**: the plate is an area
source, so the pillar catches the part of it that reaches around the wall, and
the band widens the further the surface is from the caster.

All of it is baked at build. There is no runtime light here at all.

**Everything here is Detail 1.** Baked light normally lands on *vertices*, and
a plain box face is two triangles, so a gradient this strong would show the
diagonal split between them as a hard seam. It doesn't, because primitives
take the light through the **scene lightmap atlas** — per texel, in the same
image the ambient occlusion uses (`A` = occlusion, `RGB` = light). The pillars
keep their 12 triangles and still shade smoothly.

The **ground** takes the same route through the terrain lightmap, and it's the
one that shows: this map is 64 units across on a 33×33 vertex grid, so per
vertex the pool and the wall's shadow were quantised into ~2-metre squares.
Per texel the map is 256² over the whole terrain — 0.25 units per texel, ~8×
finer per axis — which is why the falloff and the shadow edge read as curves
and straight lines rather than facets.

### Station 4 — the alley (z ≈ 12–28)

Two `concrete.mtl` walls and four neon strips (magenta, cyan, green, amber),
each with a 7-unit reach. Each strip paints its own colored pool on the
opposite wall and the floor, and where two reaches overlap the colors add.
This is what the feature is actually *for*.

## The bloom pulser

The `bloom-pulse` Empty carries a four-node flow graph:

*Every N Seconds (14)* → *Set Bloom (0.45)*, and in parallel
→ *Delay (7)* → *Set Bloom (1.5)*.

Every 14 s the halo collapses to a tight fringe; 7 s later it blows back out.
Only the post-effect moves — the emissive floors and the baked light are
untouched. It's a live demonstration that **Bloom now goes to 2**: 1.5
over-adds the blur (the GS blend factor is a whole byte), which is what makes
the strips read as *hot* rather than merely bright.

(Note: built-in action nodes don't chain exec onward — wire the *Delay*
directly to the trigger, not to the first *Set Bloom*.)

## The project's glow settings

*Tools > UI Editor* → the screen stack → **[ Bloom + color grading ]**:

| Setting | Value | Why |
| --- | --- | --- |
| Bloom | 1.5 | over-added blur — a hot glow, not a soft one |
| Threshold | 0.5 | only the emitters clear it; walls and ground stay out of the blur |
| Spread | 0.6 | 3 extra blur rounds with doubled offsets — a corona, not a fringe |

Plus distance fog fading to near-black at 70 units — that's what makes the far
end of the alley recede instead of floating.

## How it's authored

All plain *Tools > Material Editor* work in the **Glow (emissive)** section —
a strength, a color, a white-hot core, and optionally *Lights up surroundings*
with a reach and a strength. The `.mtl` files store the standard Wavefront
`Ke` statement plus TyraX hint comments:

```
# tyra-glow 1 1 0.42 0.06 0.25     # strength, color, white-hot core
# tyra-glow-light 13 1.9           # reach (world units), strength
Ke 1.2500 0.6700 0.3100            # the resolved emission every renderer reads
```

The editor viewport previews all of it live — the emissive floors, the pools
of light, the falloff. The bloom halo is GS-only and shows up in the game.

## Cost

Almost nothing here costs anything per frame: the emissive floors are folded
into the vertex colors once at scene load, the five emitters' light is a
build-time bake read back as one extra additive pass per terrain chunk and per
lit primitive, and the bloom is a fixed handful of GS sprites over two 1/8-res
scratch buffers. A full 50 FPS in the PCSX2 software renderer — the frame is
dominated by ordinary geometry, not the glow.

## Things to try

- Open `sign-matte.mtl` in the Material Editor and drag **Glow** up: the matte
  sign lights up in the viewport as you drag, and the panel tells you what the
  project's bloom is set to.
- Give `concrete.mtl` a tiny glow (0.15, no light) — the whole alley turns
  into a self-lit corridor, the cheap way to keep a dark level readable.
- Take the reach off `lava.mtl` (untick *Lights up surroundings*): the pit
  stays just as bright and the pillars go black. That's the difference between
  the floor and the light, in one click.
- Raise **Spread** to 1.0 in the UI Editor: at some point the corona stops
  reading as glow and starts reading as haze. That's the knob's honest limit.
