# Live texture feeds (camera CCTV + raytraced-mirror streams)

Any renderable object's surface can show a **live texture feed** instead of its
material: *Properties > Texture feed*. Two sources:

- **`camera: <name>`** — a Camera entity with **Render to texture (CCTV
  feed)** checked renders its view into the engine's dedicated 128×128 VRAM
  target every frame, and the surface samples it live. Security monitors,
  big-screen billboards, rear-view mirrors-by-camera — the 2002 set piece.
- **`mirror: <name>`** — a raytraced Mirror's traced image
  ([docs/raytraced-reflections.md](raytraced-reflections.md)) re-used as a
  texture on another surface: a monitor showing the reflection stream.

The feed draws **emissive** — the surface's baked shading flattens to the
object tint at texture scale (screens glow, they don't shade) — through the
object's normal UVs, so a box face or plane shows the image 1:1.

## Camera feeds (CCTV)

The Camera object gains, next to its FOV:

- **Render to texture (CCTV feed)** — enables the feed. **One active feed
  camera per scene** (the first enabled one; extra ones warn at build and stay
  inactive) — the engine has a single camera-feed target.
- **Show terrain in feed** — sky dome + resident terrain chunks under the view
  list.
- **Objects in feed** — an explicit list, the Mirror philosophy: the
  second-render cost is always visible to the author. A **catch area** fills
  that list from a volume instead: everything inside an Area object's box is
  in the feed, resolved at build so the cost stays a number you can read —
  point one at whatever the camera watches ([areas.md](areas.md)). Tick
  *Update every frame* on it and whatever walks into the volume walks onto the
  monitor, at the price of a re-draw count that now varies.

The feed renders every frame from the camera's **live transform** (+Z lens, the
Cutscene Director convention) at its authored FOV — a flow graph or sequence
moving the camera pans the feed. Animated models in the list show their last
skinned pose (skinning runs later in the frame, like mirrors and portals).
Mirrors and portals never render inside a feed — their surfaces are main-pass
tricks.

## How it works

The engine carries a second instance of the env-map redirect bracket
(`RendererCore::camFeed`, 128×128 + its own z-buffer, ~128 KB of VRAM allocated
permanently below every texture — the FIFO-free rule). Per frame the generated
game runs `camFeed.begin() → pushEnvView(camera pose, fov) → sky/terrain/view
list → popEnvView → end()` before any main-frame 3D, and surfaces bind the
target as a VRAM-resident texture (no PATH3 upload — the pixels are rendered in
place).

Two raster details paid for in testing. The target is sampled through plain
surface UVs, and GS rows run top-down while texture V grows downward — the
image reads **upside down** unless the surface V flips (the feed binding flips
it; the env map and portals never showed this because they don't sample with
plain UVs). And the default Repeat wrap bleeds the opposite edge rows into the
screen border — the feed texture is Clamp.

That Clamp is real now, and it was not before TyraX 1.9.1. The GS wrap mode is
one global register that no 3D pipeline wrote per mesh, so a feed sampled
clamped only because the whole engine happened to be left clamped at boot —
the same accident that stopped the *terrain* from tiling
([terrain painting](terrain-painting.md)). 3D is REPEAT from the top of every
frame now, and a bag whose texture asks for clamping is bracketed by a pipeline
drain and a CLAMP register write. That drain is why the mechanism is reserved
for render targets: it is a barrier, and one per such mesh per frame is only
affordable because there are a handful of them.

## Costs and constraints

- A camera feed is a real second scene render (bounded by the view list) at
  128² — comparable to the `@sky` dynamic env map pass, every frame.
- +128 KB of GS VRAM, currently always reserved (engine-level; a future
  preference could gate it).
- One camera feed per scene; any number of surfaces may show it. Mirror feeds
  are free (the traced texture already exists) — any number, also across
  surfaces.
- Feed surfaces are excluded from static batching (their texture rebinds at
  runtime).
- PS2-only: the editor viewport shows the base material.

Serialized as `"camera": {"feed", "feedTerrain", "feedObjects"}` on the Camera
and `"textureFeed": "camera:<name>" | "mirror:<name>"` on the surface; codegen
bakes `CAM_FEEDS`/`CAM_FEED_VIEWS`/`OBJECT_FEEDS` into `scene_data.hpp`.
Renames remap all references.
