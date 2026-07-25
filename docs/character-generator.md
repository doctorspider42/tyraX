# Character generator

*Tools > Character Generator* builds a **rigged, skinned, textured human** at
the PS2's budget - 1460 triangles, 23 bones, one 256² skin - from a handful of
sliders, and drops it into the scene as an ordinary animated model.

```
macro sliders (gender / age / muscle / weight / ethnicity)
   │  weighted sum of MakeHuman's CC0 morph targets
   ▼
19158-vertex reference body (deformed)
   │  proxy741 barycentric fit          │  joint cubes -> 23 Mixamo bones
   ▼                                     ▼
741-vertex body + UVs  ────skin weights transferred────>  rigged mesh
   │  gltfwrite
   ▼
res/models/characters/<name>.glb   ← an ordinary asset from here on
   │  the existing animated-model chain: import, preview, Animation Editor,
   ▼  .tskl bake with LODs, player avatars, NPC AI, Live Link
```

Nothing downstream knows a character was generated. That is the whole design:
the generator's output is a **plain glTF binary**, which is exactly where
[animated models](animated-models.md) already begin.

## Where the data comes from

The bodies are **MakeHuman's CC0 data set**, not MakeHuman the program. The
program is AGPL and none of it is used here; the base mesh, morph targets,
proxy meshes, rig, vertex weights and skins were explicitly released as CC0 in
2020, and the project [says outright](https://static.makehumancommunity.org/mpfb/faq/build_other_chargen.html)
that building your own character generator on them is fine. Credits are in
[README.md](../README.md#credits).

`setup.ps1` fetches about 45 MB into `vendor/mh-assets` (git-ignored, listed in
`deps.ps1`):

| File | What it is |
|---|---|
| `base.obj` | the hm08 reference mesh: 19158 vertices, 18486 quads, in decimetres |
| `targets/*.target` | 96 macro morph targets (sparse per-vertex offsets) |
| `proxy741.obj` / `.proxy` | the low-poly body and its binding to the base mesh |
| `default.mhskel` | the 163-bone reference rig |
| `default_weights.mhw` | per-bone vertex weights on the base mesh |
| `skins/*.png` | six 2048² CC0 diffuse maps |

The editor never downloads anything itself. With the directory missing the
window explains how to get it and does nothing else.

## The three tricks that make it fit on a PS2

**1. The macro sliders are corners, not targets.** MakeHuman does not have a
"muscle" morph - it has targets for every *combination* of the macro axes, and a
setting is the product of one factor per axis. Gender has two levels, age four
(baby 1 year / child 10 / young 25 / old 90), muscle and weight three each, so a
body is a blend of up to 16 `universal-*` targets plus 12 `<ethnicity>-*` ones.
`chargen.cpp` reimplements that composition; the targets are loaded lazily and
cached, so a slider drag re-blends about 28 sparse arrays and nothing else.

**2. The low-poly body rides the high-poly one.** `proxy741` is a 741-vertex
mesh whose every vertex is expressed as *three base-mesh vertices with
barycentric weights, plus an offset measured in units of the base mesh's own
proportions*. Evaluate it against the deformed reference body and it follows
every morph exactly - no decimation, no re-fitting, and the UVs (which the CC0
skins are painted for) come along untouched. This is why the generator can hit
a PS2 budget without a quadric collapse melting the face.

**3. The rig is derived from the morphed mesh, not fitted to it.** MakeHuman
defines each joint as a *cube of base-mesh vertices*; the joint's position is
their centroid. Deform the body and the skeleton moves with it for free - a
child and a heavy-set adult get correctly placed hips without anything
resembling an IK solve.

## The rig

23 bones, named the way Mixamo names them (`mixamorig:Hips`,
`mixamorig:LeftForeArm`, ...). Nothing in this pipeline needs the names - the
matrix palette and every animation channel address nodes by index - but they
are what free animation libraries and retarget tools match on, and renaming a
rig afterwards is far more annoying than naming it right once.

```
Hips ─ Spine ─ Spine1 ─ Spine2 ─┬─ Neck ─ Head ─ HeadTop_End
  │                             ├─ LeftShoulder  ─ LeftArm  ─ LeftForeArm  ─ LeftHand
  │                             └─ RightShoulder ─ RightArm ─ RightForeArm ─ RightHand
  ├─ LeftUpLeg  ─ LeftLeg  ─ LeftFoot  ─ LeftToeBase
  └─ RightUpLeg ─ RightLeg ─ RightFoot ─ RightToeBase
```

The reference rig has 163 bones (fingers, toes, eyes, jaw, twist bones); each
one's weights fold into the nearest ancestor that survived, so nothing is
dropped - a finger's influence simply becomes the hand's. The toe chains are
the one exception to the ancestor walk: MakeHuman parents all five straight to
the foot, so they are matched by name onto the toe bone instead, which keeps
the ball of the foot animated.

Every bone keeps **identity rotation** at bind, so a bone's local space is world
space and the inverse bind matrix is a pure translation. That makes the rig
trivial to author against and to reason about. It also means a clip authored
for MakeHuman's own bone axes would need converting - which is fine, because
clips do not come from there.

## What the sliders do

| Control | Effect |
|---|---|
| Gender | 0 female … 1 male |
| Age | MakeHuman's scale: 0 = baby, 0.19 = child, 0.5 = young adult, 1 = old |
| Muscle / Weight | 0 min … 0.5 average … 1 max |
| Ethnicity | a 3-way mix, normalized to 1 inside the generator |
| Height | scales the finished body; feet land on `y = 0` |
| Skin | one of the CC0 diffuse maps, box-filtered to 64 / 128 / 256 |

**Height is a scale, not a morph.** MakeHuman's height and body-proportion
targets are two further 144-file sets (~120 MB) whose effect at this polygon
budget is mostly overall size, and a game wants a metre value anyway. The cost
is honest: a 1.25 m "child" preset is a child-shaped body (the age targets do
that) scaled to 1.25 m, not a separately authored short adult.

The **skin texture is forced opaque**. StaPip draws with the GS alpha test set
to "pass only when alpha != 0", so a transparent texel in a body skin would
punch a hole straight through the character - see the lightmap trap in
[ambient-occlusion.md](ambient-occlusion.md) for the same rule biting elsewhere.

Everything is **deterministic in the parameters**: identical sliders produce
byte-identical files, which is what makes a rebuild-on-drag preview cheap to
reason about and a generated character reproducible from its numbers.

## Cost on the console

A generated character is ~380 KB of PS2 RAM (model data plus one instance's
skinned output buffers) and skins on VU0 like any other animated model. The
`.tskl` bake adds the usual two distance LODs on top. Budget guidance from
[animated models](animated-models.md) applies unchanged: a few instances are
comfortable, and the texture is the thing to watch in a crowd - GS VRAM is
~1.33 MB with no eviction, so drop the skin to 128 or 64 before dropping
triangles (see [gs-vram.md](gs-vram.md)).

## What is not here yet

- **Animation.** A generated character ships in bind pose with no clips. The
  rig is Mixamo-named precisely so that the next step - importing a `.fbx`
  animation library and retargeting onto it, which the vendored ufbx importer
  already gets most of the way - is a mapping table rather than a rewrite.
- **Clothing and hair.** MakeHuman's CC0 asset packs include both, fitted
  through the same proxy mechanism `proxy741` uses, so the machinery is
  already here; what is missing is the second material, the alpha-cutout hair
  card and the UI for it. Today a clothed character comes from picking one of
  the two "special suit" skins.
- **Face detail.** The 96 macro targets carry the face's overall character;
  the per-feature targets (nose, chin, ears, mouth - another ~1000 files) are
  not fetched, and at 1460 triangles most of them would not survive anyway.
- **Body proportions and height targets** - see above.

## Code map

| File | Role |
|---|---|
| `src/mhdata.cpp` | readers for the CC0 data (base mesh, targets, proxies, rig, weights). Host-only, no GL. |
| `src/chargen.cpp` | `Params` → `glbparser::Skel`: macro blend, proxy fit, rig, weight transfer, skin bake. Host-only, no GL, no `Project`. |
| `src/gltfwrite.cpp` | `Skel` → `.glb` bytes; the exact inverse of `glbparser::parseSkel`. |
| `src/app.cpp` | `drawCharacterGeneratorWindow` / `rebuildCharacterPreview` / `addCharacterToScene`. |
| `src/viewport.cpp` | `renderCharacterPreview` on its **own** framebuffer (`charFbo_`), sharing `drawToolPreview` with the Tree Generator. |

The first three link into a host harness without ImGui or GL - the
treegen/matbake pattern - which is how the generator was developed and is the
cheap way to test a change to it.
