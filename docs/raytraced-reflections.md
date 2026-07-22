# Raytraced reflections (VU0, experimental PoC)

Yes, actual ray tracing on a PlayStation 2 — as a proof of concept. A Mirror
object with **Properties > Mirror > Raytraced (VU0, experimental)** checked
stops re-submitting reflected geometry and instead **ray-traces its
reflection on a VU0 microprogram** into a small texture (`RT_MIRROR_SIZE` =
64×64) that re-uploads to the GS every frame and is sampled by the glass
quad. Per-pixel reflections, honestly traced — with deliberately simplified
scene proxies to make it fit the machine.

## What the rays hit

The traced scene is a stylized stand-in for the real one:

- **Sphere proxies** — every object in the mirror's *Reflected objects* list
  becomes a sphere at the object's live position, radius = half its largest
  scale axis, colored with the object's tint and shaded with a single-bounce
  lambert (`0.30 + 0.70·max(N·L, 0)`). Up to 8 spheres
  (`Vu0Raytracer::MaxSpheres`); *Reflect player* adds one more at the
  player's chest height. Moving objects move in the reflection — positions
  are read per frame.
- **Checkerboard ground plane** — at the terrain height under the glass,
  classic two-tone checker (2-unit cells) fading into the horizon color with
  distance. Nothing says "ray tracing" like a checkerboard.
- **Sky gradient** — misses shade from the scene's horizon→zenith colors
  (`SKY_*` / `SKY_TOP_*`), so the reflection matches the real sky dome.

The glass quad draws opaque full-bright white (colors modulate the texture),
so in RT mode the *Glass opacity* slider is ignored — the traced image IS
the reflection. Everything else about the Mirror object (transform, target
list, Reflect player) keeps its meaning.

## How it works

```
EE (per frame, renderRtMirror in the generated game)
  ├─ mirror the camera across the glass plane (Householder on the point —
  │  then every texel's reflected ray is just normalize(P − eyeMirrored):
  │  no per-texel reflection math anywhere)
  ├─ collect sphere proxies from the live target objects
  ├─ per image row: write ray params into VU0 data memory (0x11004000),
  │  `vcallms 0`, spin on VPU STAT, pack the returned integer colors
  │  into RGBA32 texels
  └─ re-upload the texture over PATH3 into its existing GS allocation
     (RendererCoreTexture::updateTextureInfo), draw the textured quad

VU0 microprogram (vendor/tyra/engine/src/renderer/rt/vu0_rt_kernel.vclpp)
  └─ per texel: normalize ray → nearest-sphere intersection (BRANCHLESS:
     VU floats saturate instead of inf/nan, so clamp(x·1e38, 0, 1) is an
     exact step(0,x) — masks select the nearest hit; the only branches are
     the two loop back-edges) → sphere lambert / checker plane / sky
     gradient → clamp, ftoi0, store
```

The kernel is written in VCL like the VU1 programs (same
vclpp→vcl→dvp-as pipeline, see `Makefile.base`) but is **VU0-honest**: no
XGKICK, no EFU (`esum`/`ersqrt` are VU1-only), no `xtop` — fixed data-memory
addresses only. The EE uploads it once to VU0 micro memory (0x11000000) via
`Vu0Raytracer::init()` and kicks it per row with `vcallms`.

Engine side: `Tyra::Vu0Raytracer`
(`vendor/tyra/engine/inc/renderer/rt/vu0_raytracer.hpp`) owns the upload,
the data-memory protocol and the texel packing. The generated game
(`renderRtMirror` / `buildRtMirrors` in `terrain_game.cpp`) owns the scene:
proxies, plane basis, the per-mirror `Texture` (created at scene load,
GS-freed and deleted on scene switch) and the draw.

## Cost and constraints

- **VU0 runs the trace synchronously** — macro-mode COP2 code (the engine's
  `Vec4`/`M4x4` math) shares VU0's register file with the microprogram, so
  the EE waits per row instead of racing it. A 64×64 trace with 3–4 spheres
  is a low-single-digit-ms slice of the frame; each extra sphere adds
  measurably (the sphere loop dominates). Budget ONE raytraced mirror per
  scene and keep the target list short.
- The reflection is **view-dependent and traced per frame** — in split-screen
  it traces once per half (each camera gets a correct reflection); that
  doubles the cost.
- Texel→world mapping matches the glass quad's STs exactly (see `addDecal`),
  so the image lines up with the plane by construction — the mirror can be
  any size, anywhere, at any rotation.
- Proxies are spheres: a reflected crate is a ball. That is the PoC
  trade — the rays, the plane and the shading are real; the shapes are not.
  (GT3 faked the rays and kept the shapes; we do the opposite.)
- 16 KB of GS VRAM for the 64×64 RGBA32 target + a 16 KB EE-side pixel
  buffer per raytraced mirror. If the all-or-nothing texture eviction ever
  flushes it, the next frame re-allocates and re-uploads automatically.

## Authoring

1. Insert a Mirror (`+ Add object > 3D Object > Mirror`), size and place it.
2. *Properties > Mirror*: add the objects that should reflect, check
   **Raytraced (VU0, experimental)**.
3. Build & run. The editor viewport previews the mirror as a regular planar
   mirror (the VU0 trace is PS2-only); judge the traced look in PCSX2 —
   software renderer, as always.

Serialized as `"raytraced": true` inside the object's `"mirror"` block;
codegen carries it as `MirrorData::raytraced` in `scene_data.hpp`.
