#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "ambience.hpp"
#include "flowgraph.hpp"
#include "grading.hpp"
#include "input.hpp"
#include "procgraph.hpp"
#include "screenfx.hpp"
#include "sequence.hpp"

struct TerrainConfig {
    int width = 100;  // world units, X axis
    int depth = 100;  // world units, Z axis
    // Does this scene HAVE a terrain at all (New Project > Create terrain, or
    // the Terrain Editor's toggle)? False removes the ground completely: no
    // mesh, no textures, no heightmap bakes, and the game has no floor either -
    // the player and the physics stand on placed geometry and fall through the
    // void everywhere else (docs/terrain.md). Width/depth stay meaningful, they
    // are also the world bounds every walker is clamped to. Default true: a
    // project saved before this key existed had a terrain.
    bool enabled = true;
};

// One paintable terrain layer above the base (the scene's terrain material).
// `material` is a res-relative .mtl, resolved through project::resolveTerrainMaterial
// so a layer inherits the same texture / Kd tint / tiling as the base terrain.
// The painted per-texel weight of each layer lives in SceneData::splat; the base
// (index -1) gets the remaining weight. See docs/terrain-painting.md.
struct TerrainLayer {
    std::string name = "Layer";
    std::string material;  // res-relative .mtl ("" = flat, uses its own name only)
    // Texture "size": how large the layer's tiled pattern appears on the ground.
    // Multiplies the pattern size on top of the material's own tiling (2 = twice
    // as big / half the repeats), so you can tune it without editing the .mtl.
    // No effect on a flat (textureless) layer.
    float scale = 1.0f;
    // Stochastic tiling ("texture bombing", docs/terrain-painting.md): bake a
    // larger non-repeating supertile from this layer's texture at build so the
    // grid repetition leaves the visible range. Zero runtime cost. Best on
    // organic textures (grass/sand/rock); leave off for anything with fixed
    // seams (bricks, tiles). No effect on a flat layer.
    bool stochastic = false;
};

inline bool operator==(const TerrainLayer& a, const TerrainLayer& b) {
    return a.name == b.name && a.material == b.material && a.scale == b.scale &&
           a.stochastic == b.stochastic;
}

enum class PrimitiveType {
    Box = 0,
    Sphere = 1,
    Cylinder = 2,
    Cone = 3,
    // Marker, not geometry: where the player appears at game start
    // (used by the FPP template; the first one in the scene wins).
    SpawnPoint = 4,
    // Custom .obj mesh (modelPath, relative to the project dir)
    Model = 5,
    // Player entity: marker in the editor; in the game the camera becomes
    // this player - walk FPP, noclip, or third person (an orbit camera behind
    // a visible avatar = the object's own animated .glb model). Regardless of
    // the project template. See playerMode + the third-person fields below.
    Player = 6,
    // Particle emitter (fire/smoke/fog/sparks/rain): live animated preview in
    // the editor viewport; camera-facing quads (optionally textured via the
    // assigned material) simulated on a fixed pool in the game.
    Emitter = 7,
    // Sound emitter: sphere marker in the editor; in the game it plays an
    // imported sound effect with distance-attenuated (spatial) volume.
    SoundEmitter = 8,
    // Point light: a glowing bulb + radius ring in the editor; at build its
    // color/brightness are baked into nearby terrain and object vertex colors
    // with a linear distance falloff (static lighting - no runtime cost).
    PointLight = 9,
    // Save point: a solid box that is implicitly usable; pressing USE on it
    // in the game opens the memory card save menu (3 slots on mc0:).
    SavePoint = 10,
    // Empty: a pure transform with no geometry in the game (sphere marker in
    // the editor). Anchor for attached scripts, waypoints, flow-graph logic.
    Empty = 11,
    // Plane: a flat unit square in the XZ plane (a floor/wall tile). Rendered
    // double-sided so it is visible from both faces.
    Plane = 12,
    // Decal: a flat unit quad in the XY plane facing +Z, textured through the
    // assigned material's map_Kd with transparency (cutout + alpha blend). Sits
    // slightly in front of its origin along +Z to avoid z-fighting the surface
    // it is stuck on. Never collides. For signs, posters, text on walls.
    Decal = 13,
    // Camera: a shot marker for the Cutscene Director (body + FOV frustum
    // wireframe in the editor, looking down its +Z axis; invisible in the
    // game). A camera-track keyframe bound to it takes eye/look-at/FOV from
    // the entity - animate the entity itself for dolly/crane shots.
    Camera = 14,
    // Mirror: a rectangle (unit XY quad facing +Z, like a decal) that fakes a
    // real mirror the PS2 way: the game re-draws each listed object a second
    // time, reflected across the mirror plane, then blends the tinted glass
    // quad over the copies. No render-to-texture, no stencil - the "mirror
    // world" is real geometry behind the plane, so build it into a wall (the
    // wall hides the copies outside the frame). See mirrorObjects below.
    Mirror = 15,
    // Portal: a rectangle (unit XY quad facing +Z, like a mirror) linked to
    // another Portal in the same scene. The quad shows a live view "through"
    // to the target: each frame the game renders the target's surroundings
    // from a second camera (the player camera mapped through the portal pair)
    // into a small VRAM render target and projects it onto the quad. Walking
    // into the front face teleports the player to the target portal with
    // position, view angle and vertical velocity carried through the same
    // transform - a seamless corridor between two parts of the map. See
    // portalTarget / portalObjects below and docs/portals.md.
    Portal = 16,
    // Area: an invisible oriented box (the unit cube under the object's
    // position/rotation/scale) with no geometry in the game - the editor draws
    // its wireframe only. It replaces hand-typed distances: point a streaming
    // layer's zone, a mirror's reflected set, a portal's through-view set or a
    // camera feed's view set at an Area instead of a radius or a hand-built
    // list, and use the In Area flow trigger for volume triggers.
    // See docs/areas.md.
    Area = 17,
    // Scatter volume: an authoring-only region (wireframe box in the editor,
    // nothing at all in the game) carrying a procedural graph in
    // procGraph. The graph scatters instances inside this box; the build
    // bakes them into ordinary static chunk meshes, so the console never
    // learns a graph existed. See docs/procedural-generation.md.
    Scatter = 18,
};

// One past the last PrimitiveType value - loops over "every object type" (the
// multi-select tally) bound on this instead of a hardcoded member.
constexpr int kPrimitiveTypeCount = (int)PrimitiveType::Scatter + 1;

// Tessellation detail for the geometry primitives, stored per object in
// SceneObject::primDetail. Its meaning depends on the shape: for the curved
// shapes (Sphere, Cylinder, Cone) it is the number of radial segments; for a
// Box it is the number of subdivisions per edge (1 = the plain 12-triangle
// box). Higher = more triangles = smoother/finer baked lighting, at PS2 vertex
// cost. Box grows quadratically, so it has a tighter cap. The same formulas run
// in three places that must stay in sync: the shared host tessellation
// (primmesh.cpp unitBox/... - used by the viewport AND the decal projector),
// the generated PS2 runtime (templates.cpp addBox/addSphere/addCylinder/addCone)
// and primTriangleCount.
constexpr int kDefaultPrimDetail = 16;  // curved shapes (radial segments)
constexpr int kDefaultBoxDetail = 1;    // Box (subdivisions per edge)

// SavePoint tessellates exactly like a Box (the game's geometry builder and
// the viewport both draw it as one), so it shares the Box detail semantics.
// Without this it inherited the curved-shape default of 16 segments, which
// addBox read as 16 subdivisions per edge - every save shrine silently cost
// 3072 triangles (9.2k verts) of near-camera EE clipping in the game.
inline bool primDetailIsBoxLike(PrimitiveType t) {
    return t == PrimitiveType::Box || t == PrimitiveType::SavePoint;
}
inline int primDetailMin(PrimitiveType t) { return primDetailIsBoxLike(t) ? 1 : 3; }
inline int primDetailMax(PrimitiveType t) { return primDetailIsBoxLike(t) ? 16 : 64; }
inline int clampPrimDetail(PrimitiveType t, int d) {
    const int lo = primDetailMin(t), hi = primDetailMax(t);
    return d < lo ? lo : (d > hi ? hi : d);
}
inline int defaultPrimDetail(PrimitiveType t) {
    return primDetailIsBoxLike(t) ? kDefaultBoxDetail : kDefaultPrimDetail;
}
// Sphere vertical rings derived from the radial segment count (~5:7 ratio).
inline int primSphereStacks(int detail) {
    const int s = detail * 5 / 7;
    return s < 2 ? 2 : s;
}
// Triangles a primitive tessellates to at the given detail - for the UI
// readout. Marker/geometry-less types report 0.
inline int primTriangleCount(PrimitiveType type, int detail) {
    const int d = clampPrimDetail(type, detail);
    switch (type) {
        case PrimitiveType::Box:
        case PrimitiveType::SavePoint:
            return 12 * d * d;  // 6 faces * 2 * d^2 subquads
        case PrimitiveType::Sphere: return primSphereStacks(d) * d * 2;
        case PrimitiveType::Cylinder: return d * 4;  // side (2/seg) + 2 caps
        case PrimitiveType::Cone: return d * 2;      // side + base (1/seg each)
        default: return 0;
    }
}

// Unit primitives fit a 1x1x1 cube centered at origin and are transformed by
// scale -> rotation (X, then Y, then Z) -> translation.
struct SceneObject {
    // Stable, opaque identity - generated once (project::ensureObjectIds) and
    // never changed, even across renames. This is the merge/persistence key:
    // the multi-user file layout stores one object per file keyed on it, so
    // two people editing different objects touch different files. Object
    // *references* (flow graphs, sequences, layers) still resolve by name;
    // the id only exists to give the merge machinery something stable to
    // anchor on. Empty on objects authored before ids existed / freshly
    // pasted; filled in on the next load or commit.
    std::string id;
    std::string name;
    PrimitiveType type = PrimitiveType::Box;
    float position[3] = {0.0f, 0.5f, 0.0f};
    float rotation[3] = {0.0f, 0.0f, 0.0f};  // degrees
    float scale[3] = {1.0f, 1.0f, 1.0f};
    float color[3] = {0.6f, 0.6f, 0.6f};  // neutral gray (specialized types override)
    bool physics = false;     // simulated as a rigid body in the game: gravity,
                              // bounces off slopes/objects, tumbles, can be
                              // pushed by the player and Apply Impulse nodes
    // Physics material (read when physics == true). Mass is relative - it only
    // matters where bodies trade momentum (collisions, player pushes).
    float physMass = 1.0f;      // relative mass; heavier = harder to push
    float physBounce = 0.35f;   // restitution 0..1: 0 = thud, 1 = superball
    float physFriction = 0.5f;  // ground drag 0..1: 0 = ice, 1 = sticky
    bool physTumble = true;     // ground contact converts slide into roll/spin
    float physSleep = 3.0f;     // seconds of near-rest before the body sleeps
    bool usable = false;      // shows the USE prompt up close; BTN_USE fires On Used
    bool pickable = false;    // BTN_USE picks it up: carried in front of the
                              // camera (swept against the world so it cannot
                              // be pushed through walls), BTN_USE again drops
    bool pickThrow = false;   // carried object can be thrown with BTN_THROW
    bool saveState = false;   // position/color/visibility persisted in save slots
    // Player collision: 0 = box (models use their real mesh AABB), 1 = mesh
    // (models only: per-triangle - ramps/stairs are walkable), 2 = none
    int collisionMode = 0;
    // Streaming layer this object belongs to (SceneData::layers entry name).
    // Empty = no layer: always resident in the game, always shown in the
    // editor. Objects reference layers by name (renames remap references).
    std::string layer;
    // Tessellation detail for the geometry primitives: radial segments for
    // Sphere/Cylinder/Cone, subdivisions per edge for Box. More = smoother +
    // more triangles. Type-dependent range/default - see clampPrimDetail /
    // defaultPrimDetail / primTriangleCount.
    int primDetail = kDefaultPrimDetail;
    // Rendering cut-off: farther than this from the camera the object is not
    // drawn at all (collision, sounds and scripts still run). 0 = unlimited.
    // The cheapest LOD there is - era-correct for dense scenes.
    float drawDistance = 0.0f;
    // Show in reflections: this object is also rendered into the dynamic
    // ("@sky") environment map, so reflective materials mirror it - the GT3
    // trick's second half. Each marked object costs a second (128x128,
    // wide-FOV) render per frame; mark the few props that sell the effect.
    bool reflected = false;
    // Ambient occlusion: this object darkens nearby terrain and objects
    // (a baked contact shadow - docs/ambient-occlusion.md). Off = the object
    // casts nothing; it still receives shadows from others.
    bool castShadow = true;
    // Baked lighting (docs/global-illumination.md). On = this object may take
    // a per-texel lightmap, which is the best-looking route and also GLUES the
    // result to its surface: tip the object over at runtime and it carries a
    // shadow that no longer matches anything. Off = it stays on the probe
    // path, where the light is re-read from the grid every time the geometry
    // is rebuilt, so it relights as it moves.
    //
    // The bake already excludes everything it can PROVE moves
    // (project::objectRuntimeMovable - physics, pickable, usable, save-state,
    // streamed, owning a graph, or named by one). This switch is for the rest:
    // an object moved through a channel no build-time scan can see (Live Link,
    // a Raycast latch, a custom node's object output), or one you simply want
    // to keep relightable.
    bool bakedLighting = true;
    // Dynamic lighting (docs/global-illumination.md). Opt-in, and a different
    // deal from bakedLighting above rather than a stronger version of it: the
    // object moves to the LIT VU1 program - one base colour plus a light bag
    // whose colours are re-read from the probe grid every frame - which is
    // exactly how animated models are lit. So it relights with zero latency,
    // including while it spins.
    //
    // What it costs, and it is not nothing:
    //  - the engine refuses per-vertex colours on a lit bag ("Multicolor is
    //    not supported with lighting"), so the object gives up everything the
    //    bake put in them - contact AO, per-face variation - and gets VU1's
    //    N.L instead;
    //  - a lit bag takes no dynamic-light slot, so the flashlight and the live
    //    point lights have to be folded into its ambient term by hand (the
    //    animated models already do this - dynLightAt);
    //  - it needs a per-vertex normal array it does not otherwise keep.
    // For things that TUMBLE it is worth all of that; for things that merely
    // slide, leaving bakedLighting off is cheaper and looks better.
    bool dynamicLighting = false;
    // Projected silhouette shadow (runtime, NOT the baked AO above): the
    // game renders this object's silhouette from the sun into a small VRAM
    // target every frame and projects it onto the terrain under it - a
    // real-shape moving shadow. Manual opt-in per object; the 4 nearest
    // casters are active at a time, each costing a 64x64 silhouette render
    // plus a small terrain patch.
    bool projShadow = false;
    std::string modelPath;    // for PrimitiveType::Model, e.g. "res/models/tree.obj"
    // Material library (.mtl) assigned to the object, e.g.
    // "res/materials/walls.mtl". Primitives take the file's FIRST material
    // (Kd + map_Kd applied on their UVs); models use it as an override that
    // replaces their own mtl (usemtl names resolve against it). Empty =
    // plain color (primitives) / the model's own materials.
    std::string materialPath;

    // Decal projection (used when type == Decal). false = the flat quad; true =
    // project the texture onto the receiver geometry (terrain + every solid
    // object whose bounding box overlaps this decal's oriented unit-cube volume)
    // so it conforms to walls/models/floors instead of floating as a flat plane.
    // The transform is the projector: scale = footprint (X/Y) + depth (Z),
    // rotation aims +Z at the surface, position places it. Computed at build
    // time on the host (decalproj) and baked to static geometry - zero PS2 cost.
    // For graffiti/wall text on angled or curved surfaces and fake blob shadows.
    bool decalProject = false;

    // Player entity parameters (used when type == Player)
    int playerMode = 0;            // 0 = walk (FPP), 1 = noclip (fly), 2 = third person
    // Movement per 1/50 s at full stick (the generated game's step unit -
    // g_frameScale normalizes it to real time). 0.1 = 5 units/s. The editor
    // shows and edits this in units per SECOND; only the file and the game
    // see the step form.
    float playerWalkSpeed = 0.1f;
    float playerLookSpeed = 1.0f;  // multiplier
    float playerEyeHeight = 1.8f;
    float playerJumpSpeed = 4.5f;  // units/s (walk mode, X button)
    bool playerCanJump = true;     // walk mode: X jumps

    // Third-person parameters (playerMode == 2). The visible avatar is the
    // Player object's OWN model (modelPath must be an animated .glb; the same
    // .glb/anim pipeline that drives NPCs). Its clips are mapped to locomotion
    // states below and auto-selected from the player's actual planar speed at
    // runtime - drop in a model, name idle/walk/run, and the character
    // walks/runs/idles with cross-fades, no scripting. Scripts and the flow
    // graph can still force any clip (Play Animation on the Player object): a
    // non-locomotion one-shot plays to the end, then locomotion resumes.
    std::string playerIdleClip;       // clip name; "" = the model's first clip
    std::string playerWalkClip;       // "" = the model's first clip
    std::string playerRunClip;        // "" = never runs (walk covers all speeds)
    std::string playerJumpClip;       // "" = no airborne clip (holds walk/idle)
    // Directional locomotion (all optional; "" = the walk clip covers that
    // direction). Only visible with playerFaceCamera on: facing then stays on
    // the camera direction instead of turning into the movement, so sideways/
    // backward movement keeps the avatar oriented and these clips play.
    std::string playerBackClip;         // backpedaling
    std::string playerStrafeLeftClip;   // sidestep toward the avatar's left
    std::string playerStrafeRightClip;  // sidestep toward the avatar's right
    bool playerFaceCamera = false;      // strafe locomotion (see above)
    float playerRunThreshold = 0.55f; // planar-speed fraction where run kicks in
    // The camera rig, expressed in the camera's own frame: Dist is the offset
    // back, Height the offset up, Shoulder the offset sideways - together a
    // full camera offset. Shoulder slides the whole rig (eye AND look-at) along
    // the camera's right vector, so the avatar sits off-center: 0 = centered
    // behind, ~0.6 = over-the-shoulder, negative = the left shoulder.
    float playerCamDist = 6.0f;       // third-person boom length (world units)
    float playerCamHeight = 1.6f;     // camera/look-at height above the feet
    float playerCamShoulder = 0.0f;   // lateral rig offset; + = right shoulder
    float playerTurnRate = 0.25f;     // avatar turn-to-face lerp per 60fps frame
    // Camera style. 0 = Orbit (free look behind the avatar - the classic rig).
    // The fixed styles pin the camera angle for top-down / isometric games:
    // 1 = Top-down and 2 = Isometric are presets of 3 = Fixed angle (the UI
    // seeds their pitch/yaw on selection; the angles stay editable in all
    // three). The left stick keeps moving the avatar relative to the camera
    // heading, and the spring arm still keeps the boom out of geometry.
    int playerCamStyle = 0;           // 0 orbit, 1 top-down, 2 isometric, 3 fixed
    float playerCamPitch = 55.0f;     // fixed styles: elevation above horizon, deg (10..85)
    float playerCamYaw = 45.0f;       // fixed styles: world heading the camera looks along, deg
    bool playerCamYawRotate = false;  // fixed styles: right stick still orbits the yaw

    // Camera-attached spot light carried by the player (used when type ==
    // Player). Additive cone + distance falloff computed per vertex on VU1, on
    // top of the baked shading (no N.L - the PS2 color pipelines carry no
    // normals). `flashlightEnabled` is the master switch: it is the initial
    // state and the flow graph can flip it at runtime (Set Flashlight node).
    // `flashlightToggleButton` (empty = none) is an optional pad button the
    // player presses to turn the beam on/off; that on/off state only shows
    // while the master is enabled (it respects flashlightEnabled).
    bool flashlightEnabled = false;
    float flashlightColor[3] = {0.75f, 0.75f, 0.62f};
    float flashlightRange = 30.0f;  // world units
    float flashlightAngle = 20.0f;  // cone half-angle, degrees
    std::string flashlightToggleButton;  // pad button name, e.g. "Circle"; "" = none
    // Texture of the beam's ground pool (res-relative PNG, e.g.
    // "res/hud/beam.png"). Empty = the built-in procedural corona. The
    // shape must live in the RGB channels: the pool draws additively and
    // additive bags ignore texture alpha.
    std::string flashlightTexture;

    // Particle emitter parameters (used when type == Emitter). The particle
    // texture comes from the shared materialPath (first material's map_Kd);
    // scale X/Z = spawn area, color = tint.
    int emitterKind = 0;      // 0 fire, 1 smoke, 2 fog, 3 sparks, 4 rain, 5 custom
    int emitterCount = 24;    // particle pool size = density (compiled in)
    float emitterSize = 0.5f; // base particle size in world units
    bool emitterEnabled = true;       // off = starts disabled; Show/Hide Object
                                      // flow nodes switch it at runtime
    bool emitterFollowPlayer = false; // position becomes an offset from the
                                      // player (rain that tracks the camera)
    // Custom kind (5) physics. Particles shoot along the object's +Y axis
    // rotated by the object rotation (tilt the emitter 90 deg = a horizontal
    // pipe leak), inside a cone of emitterSpread degrees.
    float emitterSpeed = 3.0f;    // emission speed, units/s (+-20% jitter)
    float emitterSpread = 20.0f;  // cone half-angle, degrees (0 = jet)
    float emitterGravity = 9.8f;  // units/s^2; negative = buoyant (steam)
    float emitterWeight = 1.0f;   // air drag ~ 1/weight: light particles brake
                                  // and drift, heavy ones keep their velocity
    float emitterLife = 1.5f;     // particle lifetime, seconds (+-25% jitter)
    float emitterGrow = 1.0f;     // size multiplier reached at end of life
    float emitterOpacity = 0.6f;  // base alpha 0..1 (fades out near death)
    bool emitterDieOnGround = false;  // particle dies when it hits the terrain
                                      // (water soaking in instead of clipping)

    // Sound emitter parameters (used when type == SoundEmitter)
    std::string soundPath;      // one of Project::sounds ("res/sfx/x.wav")
    bool soundAuto = true;      // play automatically while the player is in range
    float soundRange = 15.0f;   // world units; volume fades linearly to 0
    float soundInterval = 0.0f; // seconds between retriggers; 0 = loop seamlessly
    bool soundOnPlayer = false; // plays centered on the player (plain stereo,
                                // full volume, no distance/pan) - e.g. dialogs

    // Point light parameters (used when type == PointLight). The light color
    // is the shared `color` field above.
    float lightBright = 1.0f;   // intensity added on top of the scene ambient
    float lightRadius = 8.0f;   // world units; contribution fades linearly to 0
    // Dynamic (live) point light: instead of baking into vertex colors at
    // build, the game registers it every frame and the engine lights nearby
    // meshes through the VU1 spot-light slot - it can flicker and be driven
    // by the Set Light flow node. Max 8 dynamic lights per scene.
    bool lightDynamic = false;
    float lightFlicker = 0.0f;  // 0 = steady .. 1 = full torch-like flicker
    // Visible beam drawn at the light source (additive, follows the light's
    // runtime state incl. flicker/Set Light): 0 = none, 1 = glow corona
    // (camera-facing halo), 2 = corona + a cone shaft pointing down (street
    // lamp / stage light look). Works on baked lights too (steady glow).
    int lightBeam = 0;

    // Camera entity parameter (used when type == Camera): vertical field of
    // view in degrees. A Cutscene Director shot bound to this camera applies
    // it to the real PS2 projection for the duration of the shot.
    float cameraFov = 60.0f;
    // Camera texture feed (CCTV): the camera renders its view - sky (+
    // terrain) + an explicit object list, the Mirror philosophy - into the
    // engine's 128x128 camFeed VRAM target every frame; objects with
    // textureFeed == "camera:<name>" show it live. ONE active feed camera
    // per scene (the first enabled one). See docs/texture-feeds.md.
    bool camFeed = false;
    bool camFeedTerrain = true;
    std::vector<std::string> camFeedObjects;
    // Live texture feed shown on THIS object's surface (any renderable
    // primitive): "" = none, "camera:<name>" = a feed camera's view,
    // "mirror:<name>" = a raytraced mirror's traced image. Renames remap.
    std::string textureFeed;

    // Catch area (docs/areas.md): name of an Area object whose volume selects
    // this object's target list instead of (well, on top of) the hand-built
    // one. Read for the three types that carry such a list - Mirror
    // (mirrorObjects), Portal (portalObjects), Camera feed (camFeedObjects) -
    // which is why one field serves all three: an object is only ever one of
    // them. Empty = no area. Renames remap; a dangling name catches nothing.
    // Resolved at BUILD time (project::areaCaughtObjects, the same call the
    // Properties panel previews with), so the second-render cost stays visible
    // to the author instead of growing silently at runtime.
    std::string catchArea;
    // Live catch area: also re-test the volume EVERY FRAME, so an object that
    // walks/falls/spawns into it starts reflecting (showing, feeding) there
    // and then. Only objects that can actually move are re-tested - the
    // build-time list still covers the immovable rest, so a static room costs
    // nothing extra (project::areaLiveCandidates picks the movable set, which
    // is by construction the set static batching already refuses). Ignored by
    // raytraced mirrors: their proxy meshes are baked per mirror at build.
    bool catchAreaLive = false;

    // Mirror parameters (used when type == Mirror). An explicit list of scene
    // object names this mirror reflects (renames remap; a dangling name is
    // skipped) - a hard list instead of a radius, so the geometry cost is
    // always visible to the author. Reflected copies follow the live object
    // (movement, visibility, layer streaming). mirrorReflectPlayer also
    // reflects the player avatar (visible flesh only - a third-person model;
    // FPP players have no body to reflect). The shared `color` field tints
    // the glass quad; mirrorOpacity is its alpha (0 = invisible glass,
    // 1 = opaque - the reflection shows through low values).
    // mirrorRaytraced (experimental PoC): instead of re-submitting reflected
    // geometry, the game ray-traces the targets as SPHERE PROXIES on a VU0
    // microprogram into a small texture mapped onto the glass - true
    // per-pixel raytracing on PS2 hardware. See docs/raytraced-reflections.md.
    std::vector<std::string> mirrorObjects;
    bool mirrorReflectPlayer = false;
    float mirrorOpacity = 0.35f;
    bool mirrorRaytraced = false;
    // Traced image edge: 32/64/128/256/512. Cost scales with the square
    // (VU0 traces every texel) - 256/512 are photo modes, not frame rates.
    int mirrorRtSize = 64;

    // Portal parameters (used when type == Portal). portalTarget names the
    // destination Portal in the same scene (renames remap; empty or dangling =
    // inactive: the quad draws as a tinted surface and nothing teleports).
    // A one-way link - set both portals' targets at each other for a two-way
    // door. portalObjects is the explicit list of scene objects rendered in
    // the through-view (the Mirror philosophy: a hard list instead of a
    // radius, so the second-render cost is always visible to the author);
    // terrain + sky have their own switch. portalTeleportObjects also carries
    // physics-enabled objects that cross the plane through to the target.
    // The shared `color` field tints the surface of an inactive portal and of
    // the pair member whose view was not rendered this frame (one live view
    // per frame - the nearest portal facing the camera wins).
    std::string portalTarget;
    std::vector<std::string> portalObjects;
    bool portalShowTerrain = true;
    bool portalTeleportObjects = false;
    // Experimental: render EVERY scene object in the through-view instead
    // of the explicit portalObjects list (which is ignored while this is
    // on). The virtual camera's frustum culling and each object's draw
    // distance still trim the cost, but the whole scene is submitted a
    // second time when this portal's view is live - measure before
    // shipping. Mirrors' reflections and particles still don't show.
    bool portalViewAll = false;

    // Animated model parameters (Model objects whose modelPath ends in .glb;
    // the editor bakes the file's clips to morph frames - see glbparser.hpp).
    std::string animClip;       // starting clip name ("" = the file's first)
    bool animAutoplay = true;   // play the starting clip at scene start
    bool animLoop = true;       // starting clip loops
    float animSpeed = 1.0f;     // playback speed multiplier
    // Per-object LOD overrides (animated models, incl. player avatars - each
    // of the two Player objects of a two-player scene carries its own set).
    // -1 = use the project preference (Preferences > Rendering), 0 = LOD off
    // for this object, > 0 = custom distance in world units.
    float animLodOverride = -1.0f;  // pose-refresh (animation) LOD distance
    float meshLodOverride = -1.0f;  // decimated-mesh LOD distance
    // Content-forward correction, degrees around the model's own Y, applied
    // between scale and the (authored or runtime) rotation. For models
    // authored facing +-X instead of the avatar/AI convention's +Z: set
    // +-90 and the runtime facing (walker faceYaw, NPC turn-to-face) stays
    // pure logic while the mesh renders turned. Applies to animated models.
    float modelYawOffset = 0.0f;

    // --- Combat (docs/weapons.md) ----------------------------------------
    // `damageable` is the master switch: without it the object is scenery
    // that bullets pass their impact effect over and nothing else. With it
    // the object carries `health` hit points, fires the On Damaged / On
    // Killed triggers, and does `deathAction` when it runs out.
    bool damageable = false;
    float health = 100.0f;  // hit points at scene start (and after a respawn)
    // What happens the moment health reaches 0. 2 (stay) is for objects whose
    // death is entirely scripted - the trigger fires and the object is left
    // exactly as it was, still at 0 health (heal it to bring it back).
    int deathAction = 0;  // 0 hide, 1 despawn, 2 stay, 3 knock over (physics)
    // Which impact burst a hit on THIS object throws, overriding the
    // weapon's own impact effect. 0 = the weapon decides.
    int hitFx = 0;  // 0 weapon default, 1 sparks, 2 blood, 3 dust, 4 none
    // Weapons this object carries (Project::weapons names; renames remap).
    // On a Player object this is the starting inventory - the first entry is
    // equipped at spawn. On anything else, entry 0 is the weapon the Fire
    // Weapon node (and auto-fire below) shoots with.
    std::vector<std::string> weapons;
    // NPC auto-fire: while the player is within this distance, inside
    // `autoFireFov` degrees of the object's facing and in terrain
    // line-of-sight, the object fires weapons[0] at them on its own cadence.
    // 0 = never (the object only shoots when a flow graph tells it to).
    float autoFireRange = 0.0f;
    float autoFireFov = 60.0f;  // vision cone half-angle, degrees

    // Per-object logic. Object-referencing nodes default to this object
    // ("self"), so a copied object brings a working copy of its behavior.
    FlowGraph flowGraph;

    // Procedural graph (type == Scatter): what this volume generates. The
    // object's transform IS the region the graph works in, so the ordinary
    // gizmo moves and resizes it. Evaluated in the editor (procgen), baked to
    // static geometry at build (procbake); nothing of it reaches the PS2.
    ProcGraph procGraph;
    // Set on the chunk objects a Scatter bake produced: the id of the Scatter
    // object that owns them. They are real scene objects (so codegen,
    // culling, LOD and the disc layout need no special case) but the editor
    // treats them as build output: not drawn in the viewport (the live graph
    // preview stands in for them), grouped in the outliner, and replaced
    // wholesale by the next bake. Empty = hand-authored, the normal case.
    std::string procSource;
    // Name of the prefab this object was stamped from (empty = authored by
    // hand, the normal case). Editor bookkeeping ONLY - nothing downstream
    // reads it: an inserted prefab produces ordinary, fully independent
    // objects, and editing the prefab afterwards does not reach back. It exists
    // because the outliner otherwise cannot tell twenty hand-placed slabs from
    // twenty that arrived together, which is the one question you ask when a
    // scene has prefabs in it. Kept through a copy/paste on purpose (a copy of
    // a room is still a room); dropped by prefab::capture, which must not
    // record where its own members came from.
    std::string prefabSource;

    // Attached object scripts: class names registered in src/scripts/*.cpp
    // with TYRA_OBJECT_SCRIPT(Name). Each attachment becomes its own script
    // instance in the game (Unity-style components); the same class can be
    // attached to any number of objects across scenes.
    std::vector<std::string> scripts;
};

// A reusable group of scene objects - their flow graphs included - stamped
// into the world by hand, by a procedural graph, or by the Spawn Prefab flow
// node while the game runs (docs/prefabs.md). The verbs live in prefab.hpp;
// the struct is here because a prefab MEMBER is a SceneObject and nothing
// lighter: a prefab is a piece of scene, and everything the editor, codegen and
// the runtime already do with an object has to keep working after it comes out
// of one. The only difference is the frame - member transforms are LOCAL to the
// prefab origin, so instantiating is a yaw plus a translation.
struct Prefab {
    // Stable, opaque identity (16 hex chars) - the collaboration merge key,
    // like SceneObject::id. Every REFERENCE to a prefab is by name.
    std::string id;
    std::string name;
    // Free prose: what this thing is for, how it is meant to be placed, what
    // the caller has to provide. Multi-line - the field it is edited in wraps,
    // because a one-line box turned every note into its own first half.
    std::string notes;
    // Members. Their `id` is empty by construction - an instance gets fresh
    // ids, and two instances of one prefab must never share an identity.
    std::vector<SceneObject> objects;

    bool empty() const { return objects.empty(); }
};

inline bool operator==(const Prefab& a, const Prefab& b) {
    return a.id == b.id && a.name == b.name && a.notes == b.notes &&
           a.objects == b.objects;
}
inline bool operator!=(const Prefab& a, const Prefab& b) { return !(a == b); }

const char* primitiveTypeName(PrimitiveType t);

// Animated models are .glb or .fbx files (serialized to .tskl at build);
// static models are .obj. Decides which import/render/codegen path an object
// takes.
inline bool isAnimatedModelPath(const std::string& path) {
    if (path.size() <= 4) return false;
    const std::string ext = path.substr(path.size() - 4);
    return ext == ".glb" || ext == ".fbx";
}

inline bool operator==(const SceneObject& a, const SceneObject& b) {
    auto eq3 = [](const float* x, const float* y) {
        return x[0] == y[0] && x[1] == y[1] && x[2] == y[2];
    };
    return a.id == b.id && a.name == b.name && a.type == b.type && eq3(a.position, b.position) &&
           eq3(a.rotation, b.rotation) && eq3(a.scale, b.scale) && eq3(a.color, b.color) &&
           a.physics == b.physics && a.physMass == b.physMass &&
           a.physBounce == b.physBounce && a.physFriction == b.physFriction &&
           a.physTumble == b.physTumble && a.physSleep == b.physSleep &&
           a.usable == b.usable &&
           a.pickable == b.pickable && a.pickThrow == b.pickThrow &&
           a.saveState == b.saveState && a.collisionMode == b.collisionMode &&
           a.layer == b.layer &&
           a.primDetail == b.primDetail && a.drawDistance == b.drawDistance &&
           a.reflected == b.reflected && a.castShadow == b.castShadow &&
           a.projShadow == b.projShadow &&
           a.bakedLighting == b.bakedLighting &&
           a.dynamicLighting == b.dynamicLighting &&
           a.modelPath == b.modelPath &&
           a.materialPath == b.materialPath && a.decalProject == b.decalProject &&
           a.playerMode == b.playerMode &&
           a.playerWalkSpeed == b.playerWalkSpeed &&
           a.playerLookSpeed == b.playerLookSpeed &&
           a.playerEyeHeight == b.playerEyeHeight &&
           a.playerJumpSpeed == b.playerJumpSpeed &&
           a.playerCanJump == b.playerCanJump &&
           a.playerBackClip == b.playerBackClip &&
           a.playerStrafeLeftClip == b.playerStrafeLeftClip &&
           a.playerStrafeRightClip == b.playerStrafeRightClip &&
           a.playerFaceCamera == b.playerFaceCamera &&
           a.playerIdleClip == b.playerIdleClip &&
           a.playerWalkClip == b.playerWalkClip &&
           a.playerRunClip == b.playerRunClip &&
           a.playerJumpClip == b.playerJumpClip &&
           a.playerRunThreshold == b.playerRunThreshold &&
           a.playerCamDist == b.playerCamDist &&
           a.playerCamHeight == b.playerCamHeight &&
           a.playerCamShoulder == b.playerCamShoulder &&
           a.playerTurnRate == b.playerTurnRate &&
           a.playerCamStyle == b.playerCamStyle &&
           a.playerCamPitch == b.playerCamPitch &&
           a.playerCamYaw == b.playerCamYaw &&
           a.playerCamYawRotate == b.playerCamYawRotate &&
           a.flashlightEnabled == b.flashlightEnabled &&
           eq3(a.flashlightColor, b.flashlightColor) &&
           a.flashlightRange == b.flashlightRange &&
           a.flashlightAngle == b.flashlightAngle &&
           a.flashlightToggleButton == b.flashlightToggleButton &&
           a.flashlightTexture == b.flashlightTexture &&
           a.emitterKind == b.emitterKind &&
           a.emitterCount == b.emitterCount && a.emitterSize == b.emitterSize &&
           a.emitterEnabled == b.emitterEnabled &&
           a.emitterFollowPlayer == b.emitterFollowPlayer &&
           a.emitterSpeed == b.emitterSpeed && a.emitterSpread == b.emitterSpread &&
           a.emitterGravity == b.emitterGravity &&
           a.emitterWeight == b.emitterWeight && a.emitterLife == b.emitterLife &&
           a.emitterGrow == b.emitterGrow && a.emitterOpacity == b.emitterOpacity &&
           a.emitterDieOnGround == b.emitterDieOnGround &&
           a.soundPath == b.soundPath && a.soundAuto == b.soundAuto &&
           a.soundRange == b.soundRange && a.soundInterval == b.soundInterval &&
           a.soundOnPlayer == b.soundOnPlayer &&
           a.lightBright == b.lightBright && a.lightRadius == b.lightRadius &&
           a.lightDynamic == b.lightDynamic && a.lightFlicker == b.lightFlicker &&
           a.lightBeam == b.lightBeam &&
           a.cameraFov == b.cameraFov &&
           a.camFeed == b.camFeed && a.camFeedTerrain == b.camFeedTerrain &&
           a.camFeedObjects == b.camFeedObjects &&
           a.textureFeed == b.textureFeed &&
           a.catchArea == b.catchArea &&
           a.catchAreaLive == b.catchAreaLive &&
           a.mirrorObjects == b.mirrorObjects &&
           a.mirrorReflectPlayer == b.mirrorReflectPlayer &&
           a.mirrorOpacity == b.mirrorOpacity &&
           a.mirrorRaytraced == b.mirrorRaytraced &&
           a.mirrorRtSize == b.mirrorRtSize &&
           a.portalTarget == b.portalTarget &&
           a.portalObjects == b.portalObjects &&
           a.portalShowTerrain == b.portalShowTerrain &&
           a.portalTeleportObjects == b.portalTeleportObjects &&
           a.portalViewAll == b.portalViewAll &&
           a.animClip == b.animClip && a.animAutoplay == b.animAutoplay &&
           a.animLoop == b.animLoop && a.animSpeed == b.animSpeed &&
           a.animLodOverride == b.animLodOverride &&
           a.meshLodOverride == b.meshLodOverride &&
           a.modelYawOffset == b.modelYawOffset &&
           a.damageable == b.damageable && a.health == b.health &&
           a.deathAction == b.deathAction && a.hitFx == b.hitFx &&
           a.weapons == b.weapons && a.autoFireRange == b.autoFireRange &&
           a.autoFireFov == b.autoFireFov &&
           a.flowGraph == b.flowGraph && a.scripts == b.scripts &&
           a.procGraph == b.procGraph && a.procSource == b.procSource &&
           a.prefabSource == b.prefabSource;
}

// General project preferences (Project > Preferences in the editor).
// Baked into the generated terrain_config.hpp on every build.
struct ProjectSettings {
    // Build target. videoSystem: "auto" follows the console region, "ntsc"
    // forces 60 Hz, "pal" forces 50 Hz (gameplay speed is wall-clock
    // normalized via g_frameScale in the generated game, so both play the
    // same). The "debug" profile unlocks the on-screen FPS / free-RAM
    // overlays and the Live Link poller; "release" strips them from the build.
    // NOTE these are the values a project that predates the key loads as, NOT
    // the new-project defaults - project::create() starts a fresh project in
    // "debug" (you author with Live Link, then switch to release for the disc).
    std::string videoSystem = "auto";      // "auto" | "ntsc" | "pal"
    std::string buildProfile = "release";  // "release" | "debug"

    // Output scan mode. "interlaced" is the stock 480i/576i signal (follows
    // videoSystem). "interlaced-field" is the same signal with true field
    // rendering: half-height buffers, a fresh image every field (50/60
    // distinct pictures per second at full speed) for about half the fill
    // and VRAM cost. "progressive" outputs flicker-free 480p, "1080i" a
    // pillarboxed HD signal - both need component cables on a real console
    // (PCSX2 shows every mode) and always run at 60 Hz. "pal576" is the
    // full-height PAL frame (true 576i, 512 rendered lines, always 50 Hz
    // regardless of videoSystem) - the "full PAL" of European releases;
    // costs ~380 KB of GS VRAM over "interlaced".
    std::string displayMode =
        "interlaced";  // "interlaced" | "interlaced-field" | "progressive" |
                       // "1080i" | "pal576"

    // PAL handling of the region-following "interlaced" mode: false = the
    // letterboxed NTSC-size picture (stock), true = a PAL console (or a
    // forced-PAL videoSystem) boots the full-height 576i frame instead
    // (DisplayMode::Pal576i). Resolved in the generated main.cpp before
    // engine init; fixed display modes ignore it.
    bool palFullHeight = false;

    // 16:9 anamorphic output: widens the projection so proportions are
    // correct on a widescreen TV (the framebuffer stays the same; in 1080i
    // the GS display window widens instead). Also switchable at runtime
    // via the Set Widescreen flow node.
    bool widescreen = false;

    // Texture quantization at build (the PS2-native "compression": palettized
    // PSMT8/PSMT4 textures). Applied to res/models|materials|textures PNGs
    // when baking res/ -> .res-baked/; sources stay untouched. Per-asset
    // overrides live in Project::textureQuality. "none" = full color,
    // "8bit" = 256 colors, "4bit" = 16 colors (default - era-correct).
    std::string textureQuant = "4bit";
    // Texture atlasing at build (docs/texture-atlasing.md): small clamp-safe
    // map_Kd textures pack into shared 256x256 pages - one GS allocation
    // (+~8 KB overhead) per page instead of per texture. Conservative
    // eligibility, computed by texatlas::plan; off = classic per-file bake.
    bool textureAtlas = false;
    bool showFps = false;     // debug profile only: on-screen FPS counter
    bool showMemory = false;  // debug profile only: on-screen free-RAM readout
    bool showProfiler = false;  // debug profile only: per-phase EE-time HUD
                                // (scene / highlight / particles / whole frame)
    // Debug profile only: draw Area objects (docs/areas.md) in the game as
    // wireframe boxes. An area has no geometry on the console by design, which
    // is exactly why "why did the layer not unload / why is that not
    // reflecting" is hard to see - this puts the volume back on screen.
    bool showAreas = false;
    // Debug profile only: compile the Live Link poller into the game, so the
    // editor can stream scene edits into the running game (docs/live-link.md).
    // Off = the game never reads livelink.bin and the editor never writes it -
    // for anyone who does not want their debug builds patched from outside.
    bool liveLink = true;

    // Debug profile only: compile the Live Debugger runtime into the game -
    // the flow graphs report every node they run to the editor, and the editor
    // can set breakpoints, stop/step the game and force-fire a trigger
    // (docs/live-debugger.md). Off = no instrumentation, no reporting, and
    // livedbg.bin / livedbg.cmd are never touched by either side.
    bool liveDebug = true;

    // Debug profile only: compile the Live Logic interpreter into the game, so
    // the editor can hot-patch an edited flow graph into the RUNNING game with
    // no rebuild (docs/live-logic.md). Off = graphs only ever run as the C++
    // they were compiled to.
    bool liveLogic = true;

    // Debug profile only: compile the time machine into the game
    // (docs/time-machine.md). The game captures everything it mutates - object
    // transforms and physics, the walkers, flow variables, save values - into
    // bin/livetime.bin every few frames; the editor keeps a history of those
    // captures and can push one back through bin/livetime.rst, which puts the
    // running game where it was. Off = neither file is ever touched and the
    // generated runtime is an empty translation unit.
    bool timeMachine = true;

    // Debug profile only: compile the Remote Pad overlay into the game
    // (docs/remote-pad.md), so the editor's on-screen pad and the --pad CLI can
    // drive it through bin/livepad.bin - no window focus, no real controller,
    // and scriptable for unattended tests. Off = the game never looks for the
    // file and the generated runtime is an empty translation unit.
    bool remotePad = true;

    // Debug profile only, EXPERIMENTAL and off by default: install the engine's
    // EE crash handler, which turns a real CPU exception (bad pointer, address
    // error, reserved instruction) into a crash.txt report instead of a silent
    // freeze. Measured under PCSX2, ps2sdk's ee_dbg_install() wedges the game
    // the moment it goes in, so this needs a real console (or a hand-written
    // exception stub) before it can be the default - see docs/devkit.md.
    bool eeCrashHandler = false;

    // USB keyboard & mouse controls: the game loads the usbd/ps2kbd/ps2mouse
    // drivers and maps keys/mouse onto a virtual pad (bindings live in the
    // generated controls.hpp). Works in PCSX2 (the editor configures its
    // emulated USB devices before launch) and with real USB devices on a
    // console. Skipped automatically on ps2link deploys - a second usbd on
    // an IOP that may already run one (ps2link booted from a USB stick)
    // wedges the USB stack.
    //
    // true here is what a project that predates the key loads as (the feature
    // was retroactively on for everyone); project::create() starts a fresh
    // project with it OFF - a pad game pays nothing for drivers it never uses,
    // and the console only speaks the USB HID boot protocol anyway, so this is
    // a choice to make deliberately (docs/keyboard-mouse.md).
    bool keyboardMouse = true;

    // Keep keyboard/mouse working on a "Run on PS2" (ps2link) deploy. The
    // console side is always the TyraX ps2link (tools/ps2link, built by its
    // build.sh/.ps1 - see docs/ps2link-setup.md): it bakes usbd + ps2kbd +
    // ps2mouse into its own boot, so the engine reuses that resident stack and
    // loads none of its own (a second usbd would wedge it). On by default for
    // that reason; untick it only for a deliberately stock ps2link, which has
    // no USB stack to reuse - the drivers then report "not ready" and the
    // mouse is skipped rather than hanging. See docs/keyboard-mouse.md.
    bool keyboardMousePs2Link = true;

    // Experimental: skip the vsync wait before the buffer flip. Frame rate
    // becomes continuous instead of quantized to 50/25 (PAL), at the cost
    // of screen tearing. Gameplay speed is unaffected either way - the
    // generated game measures real frame time (see updateFrameClock).
    bool disableVsync = false;

    // "precise": real per-triangle clipping - no holes at screen edges, but
    // costs EE time. "fast": VU1 cull only - fastest, may drop triangles
    // that extend far beyond the screen.
    // Triangle handling: "vu1" (default - precise clipping in the VU1 clip
    // programs, no EE cost), "precise" (the legacy EE clipper) or "fast"
    // (cull-only). Projects saved before the vu1 default keep their value.
    std::string clipping = "vu1";

    // Animation frame rate. glTF and FBX store keyframe times in SECONDS,
    // never an fps, so a clip authored as N frames for F fps but exported
    // from a scene running at S fps arrives S/F times too long - the
    // classic "Blender scene is 24 fps, the animation was made for 30"
    // mismatch, which plays back visibly too slow on the console. These two
    // numbers say what the source fps was and what it should have been; the
    // build scales every clip's keyframe times by animSourceFps /
    // animPlayFps at bake time, so nothing is destroyed and the .glb is
    // never touched. Equal values (the default) = no scaling at all.
    float animSourceFps = 24.0f;  // fps the clips were exported at
    float animPlayFps = 24.0f;    // fps they should play at

    // Animation LOD: animated-model instances farther than this from the
    // camera refresh their pose/skinning every 2nd frame (every 4th beyond
    // twice the distance), staggered across objects so the cost spreads.
    // Playback time is unaffected - the pose catches up on the next
    // refresh. 0 = off (every instance skins every frame).
    float animLodDistance = 0.0f;

    // Mesh LOD: the build bakes decimated variants (~50% and ~25% vertices,
    // quadric-error collapse) of every animated model into the .tskl;
    // instances farther than this render the 50% mesh, beyond twice the
    // distance the 25% one. 0 = off (no LODs baked or kept in RAM).
    float meshLodDistance = 0.0f;

    // Static batching: the generated game merges non-moving primitive
    // objects that share a material into combined world-space bags at scene
    // load, paying the fixed per-bag submit cost (~1 ms/object on real
    // hardware) once per batch instead of once per object. Objects with
    // physics, scripts, flow-graph references, save-state or a streaming
    // layer stay individual; runtime edits (Live Link, Raycast-driven
    // actions) trigger a batch rebuild. Off = every object submits its own
    // bag (pre-batching behavior; the A/B lever for profiling).
    bool staticBatching = true;

    // Dynamic reflection probe aim (docs/reflective-materials.md). false =
    // the classic GT3 aim: the env camera looks level along the player
    // forward from the eye. true = "reflected ray": each frame a ray from
    // the camera is intersected with the dynamic-reflective objects
    // (analytic normals - OBB faces for boxes, spheres for curved shapes,
    // bounding spheres for models) and the probe renders from the hit
    // point along the REFLECTED ray (smoothed), so the map shows what the
    // surface the player looks at actually reflects. Off by default -
    // existing projects keep their look.
    bool envProbeReflected = false;

    // AI navigation (docs/navigation-ai.md). The nav grid is baked on the
    // host at build time (navmesh.cpp) from the terrain slope + blocking
    // objects; the game runs A* over the baked bitmap on the EE, only in
    // scenes whose flow graphs use the AI nodes - other scenes cost nothing.
    float navCellSize = 1.0f;     // world units per nav cell (grid capped 128x128)
    float navMaxSlope = 40.0f;    // degrees; steeper terrain is unwalkable
    float navAgentRadius = 0.4f;  // obstacle inflation around blockers, world units

    // World scale: how many world units one REAL-WORLD METER is. The engine
    // itself has no opinion about units - a unit is whatever the project
    // decides - but everything imported from reality does: a phone camera
    // take records meters, a Mixamo/Maya/Blender model carries meters (or a
    // unit the importer normalizes to meters). This number is the one place
    // that conversion is written down, so a project authored at, say, 5 units
    // per meter lands those imports at the size its own content already uses
    // instead of 5x too small (docs/world-scale.md).
    //
    // Host-side only - it never reaches the game, which keeps working in
    // plain units. 1.0 (the default, and what every pre-existing project
    // loads as) means "one unit is one meter" and changes nothing.
    float unitsPerMeter = 1.0f;

    int terrainDetail = 32;  // max terrain grid cells per axis (quality vs perf)

    // Terrain streaming: the generated game builds the terrain in 16x16-cell
    // chunks; with a view distance > 0 only the chunks within that range of
    // the camera stay in memory (the ring streams in as the player moves,
    // like the layer streaming). 0 = whole map resident. Large maps at high
    // detail NEED this - the full mesh would not fit in the PS2's 32 MB.
    float terrainViewDistance = 0.0f;  // world units, 0 = off
    float skyColor[3] = {0.25f, 0.55f, 0.78f};   // horizon / clear color
    float skyTopColor[3] = {0.08f, 0.3f, 0.65f};  // zenith (gradient dome)
    bool skyDome = true;  // render a gradient sky dome (vs flat clear color)
    // How much of the dome the zenith color fills. 0.5 = linear (color scales
    // linearly with elevation); higher = zenith reaches lower toward the
    // horizon (bigger zenith cap); lower = zenith stays near the top. Both the
    // viewport preview and the generated dome remap the gradient by
    // pow(t, (1-size)/size), t = 0 horizon .. 1 zenith.
    float zenithSize = 0.5f;  // 0.05 .. 0.95

    // FPP template
    float eyeHeight = 1.8f;
    // Movement per 1/50 s at full stick, like SceneObject::playerWalkSpeed -
    // NOT per second. 0.1 = 5 units/s, which in a metric project (the default
    // 1 unit = 1 m, and an eye height of 1.8) is a brisk 5 m/s run; the old
    // 0.4 default was 20 units/s, i.e. 72 km/h for a person-sized player, and
    // it is why projects tended to get built several times larger than metric
    // (docs/world-scale.md). Existing projects keep whatever they saved.
    float walkSpeed = 0.1f;
    float lookSpeed = 1.0f;  // multiplier

    // Sprint: while the "sprint" input action (Tools > Input Map) is held, the
    // walkers multiply their walk speed by this. 1.0 = sprinting does nothing
    // (the switch that turns the feature off without unbinding the button).
    float sprintMultiplier = 1.8f;  // 1..4

    // Analog sticks: offsets below this fraction of full deflection read as
    // zero (real DualShock sticks rest off-center); motion rescales smoothly
    // from the deadzone edge. Per stick - worn pads rarely drift equally.
    float stickDeadzoneL = 0.2f;  // 0..0.9, left stick (movement)
    float stickDeadzoneR = 0.2f;  // 0..0.9, right stick (camera)

    // Analog stick response curve, applied AFTER the deadzone rescales the
    // magnitude to 0..1: 0 = Linear (raw), 1 = Exponential (pow(mag, exp) -
    // finer control near center, snappier at the edge), 2 = S-Curve (eases in
    // and out - a soft center plus a firm cap). stickExp* tunes the shape of
    // curves 1/2 (>=1; higher = more pronounced). Per stick, and live-settable
    // from a flow graph via the Set Stick Curve node.
    int stickCurveL = 0;     // left stick (movement)
    int stickCurveR = 0;     // right stick (camera)
    float stickExpL = 2.0f;  // exponent for the L curve (>=1)
    float stickExpR = 2.0f;  // exponent for the R curve (>=1)

    // Two-player support (docs/multiplayer.md). The mode used while player 2
    // is active: "shared" = both avatars on one screen, the camera frames the
    // pair; "split" = horizontal split screen (P1 top, P2 bottom). "off"
    // compiles every 2P path out. Player 2 exists only in scenes that contain
    // a second Player object (the first one is P1, the second P2).
    std::string multiplayer = "off";  // "off" | "shared" | "split"
    // Player 2 can join mid-game by pressing Start on pad 2 (and a menu
    // Toggle bound to "Player count" can switch 1P/2P at any time).
    bool p2JoinOnStart = true;

    // Orbit template
    float orbitSpeed = 1.0f;  // multiplier

    // Physics
    float gravity = 9.8f;    // units/s^2
    float jumpSpeed = 4.5f;  // units/s (FPP jump)

    // Lighting (baked into vertex colors at build)
    float lightDir[3] = {0.37f, 0.82f, 0.44f};   // direction TO the light
    float ambient = 0.55f;                       // 0..1
    float diffuse = 0.45f;                       // 0..1
    float lightColor[3] = {1.0f, 1.0f, 1.0f};    // tints the diffuse term
    float brightness = 1.0f;                     // global multiplier (0..2)

    // Sun lens flare (0 = off, else brightness 0..1): additive sprites along
    // the sun->screen-center axis, drawn after the 3D scene under the HUD,
    // occluded by geometry/terrain via one ray cast per frame. The sun sits
    // infinitely far along lightDir; its tint follows lightColor.
    float flare = 0.0f;
    // God rays / light shafts from the sun (0 = off, else strength 0..1):
    // a bright-pass + radial blur toward the sun's screen position on the GS
    // (reuses the bloom blur chain), composited additively after the scene.
    float godRays = 0.0f;
    // Baked ambient occlusion (docs/ambient-occlusion.md): terrain
    // self-shadowing, contact darkening between static geometry and raycast
    // self-AO for imported .obj models - all folded into the baked vertex
    // colors, zero PS2 per-frame cost. Authored per ambience preset; these
    // fields are the no-preset fallback like the lighting above.
    bool aoEnabled = false;
    float aoStrength = 0.55f;  // 0..1, how dark full occlusion gets
    float aoRadius = 2.5f;     // world units the contact darkening reaches

    // Baked global illumination (docs/global-illumination.md). Project-wide on
    // purpose - unlike the sky/lighting above these are not part of the
    // ambience-preset overlay: a preset changes what the light LOOKS like, the
    // bake quality is a project decision. Nothing here reaches the game
    // directly; it drives the host bake in gibake, whose OUTPUT ships as the
    // scene lightmap's RGB channel plus inc/probe_data.gen.hpp.
    //
    // The bake is explicit (Tools > Bake Global Illumination) and cached in
    // .res-baked/gi/ - a build never silently re-bakes it. A stale or missing
    // cache simply falls the scene back to the pre-GI emissive-only lighting.
    bool giEnabled = false;
    int giRays = 128;    // hemisphere rays per lightmap texel / per probe
    int giBounces = 2;   // interreflection passes (0 = direct + sky only)
    float giSkyLight = 1.0f;  // the sky dome's strength as a light source
    float giSunLight = 1.0f;  // the directional sun's strength
    // A constant added to every gather. Real GI makes a sealed room with no
    // light source pitch black, which reads as "the bake is broken" rather
    // than "you forgot a lamp"; this is the floor under that.
    float giAmbientFloor = 0.03f;
    bool giProbes = true;        // bake the light-probe grid
    float giProbeSpacing = 3.0f; // world units between probes, horizontally
    float giProbeHeight = 2.0f;  // ...and between vertical levels
    int giProbeLevels = 4;       // vertical levels above the lowest ground

    // Terrain material (.mtl asset; empty = checker greens). The first
    // material's Kd tints the terrain; its map_Kd (when present) textures it,
    // tiled by the map's "-s" scale (repeats per world unit), otherwise the
    // terrain is a flat Kd-colored surface.
    std::string terrainMaterial;

    // Post effects (GS framebuffer blits at the end of every frame; no
    // pixel shaders on the PS2). 0 = off, 1 = maximum.
    float bloom = 0.0f;  // downsample + blur + additive re-add (glow)
    // Bright-pass cut for the bloom, 0..1 of full white. 0 = the whole frame
    // glows (soft focus); raise it and only what is brighter than this blooms,
    // which is what makes emissive materials (docs/emissive-materials.md) read
    // as glowing objects instead of veiling the picture.
    float bloomThreshold = 0.0f;
    // How far the glow reaches, 0..1 -> 1..4 soften iterations over the
    // quarter-res buffer (each doubles the tap offsets). 0 = the original
    // tight fringe; raise it for a real corona around emissive surfaces.
    float bloomSpread = 0.0f;
    float grain = 0.0f;  // animated film grain noise overlay
    // Depth of field: the image blurs progressively past dofFocus (world
    // units from the camera), reaching the full dofAmount blur at
    // dofFocus + dofRange. Composites right after the 3D scene (per-pixel
    // z-tested), so the HUD stack always stays crisp. The Set Depth Of Field
    // flow node can override or restore these at runtime.
    float dofAmount = 0.0f;  // far-blur strength, 0 = off
    float dofFocus = 20.0f;  // sharp up to this camera distance
    float dofRange = 15.0f;  // full blur reached at dofFocus + dofRange

    // GS hardware distance fog (atmospheric fade-out). Geometry blends
    // toward fogColor between fogStart and fogEnd view distances; free on the
    // GS (per-vertex coefficient computed on VU1). Match fogColor with the
    // sky/clear color and keep fogEnd at (or before) the far plane.
    bool fogEnabled = false;
    float fogColor[3] = {0.5f, 0.5f, 0.55f};
    float fogStart = 15.0f;   // world units from the camera
    float fogEnd = 120.0f;    // full fog at/after this distance

    // Scene switches show res/hud/loading.png centered on black for a
    // moment (a generated placeholder is written when the file is missing).
    bool loadingScreen = true;

    // Soft dark blob shadows under moving things (the third-person avatar,
    // animated models, physics objects): a terrain-conforming alpha-blended
    // quad through the same soft-glow sprite the flare uses, fading out as
    // the object rises. Project-wide; grounds objects visually for almost
    // nothing (one quad per object).
    bool blobShadows = false;

    // In-game outline around usable objects while the player is within
    // highlightDistance (fading silhouette shells drawn after the scene).
    bool highlightUsable = false;
    float highlightDistance = 6.0f;                  // world units
    float highlightColor[3] = {1.0f, 0.85f, 0.15f};  // outline color
    float highlightWidth = 0.35f;  // total rim width incl. blur, world units
    int highlightSteps = 4;        // blur shells; 1 = sharp outline
    // Opacity of the strongest (innermost) shell, 0..1; the outer shells fade
    // from it. 1 = the strongest shell is fully opaque.
    float highlightOpacity = 0.56f;
    // Experimental: draw the glow ON the object surface (a colored overlay
    // that fades outward into a rim) instead of only a rim BEHIND it. Off =
    // the classic silhouette outline (shells pushed behind object depth).
    bool highlightOverlay = false;
};

inline bool operator==(const ProjectSettings& a, const ProjectSettings& b) {
    auto eq3 = [](const float* x, const float* y) {
        return x[0] == y[0] && x[1] == y[1] && x[2] == y[2];
    };
    return a.videoSystem == b.videoSystem && a.buildProfile == b.buildProfile &&
           a.displayMode == b.displayMode &&
           a.palFullHeight == b.palFullHeight && a.widescreen == b.widescreen &&
           a.showFps == b.showFps && a.showMemory == b.showMemory &&
           a.showProfiler == b.showProfiler && a.showAreas == b.showAreas &&
           a.liveLink == b.liveLink && a.liveDebug == b.liveDebug &&
           a.liveLogic == b.liveLogic && a.timeMachine == b.timeMachine &&
           a.remotePad == b.remotePad &&
           a.eeCrashHandler == b.eeCrashHandler &&
           a.keyboardMouse == b.keyboardMouse &&
           a.keyboardMousePs2Link == b.keyboardMousePs2Link &&
           a.disableVsync == b.disableVsync &&
           a.clipping == b.clipping && a.animLodDistance == b.animLodDistance &&
           a.meshLodDistance == b.meshLodDistance &&
           a.animSourceFps == b.animSourceFps &&
           a.animPlayFps == b.animPlayFps &&
           a.staticBatching == b.staticBatching &&
           a.envProbeReflected == b.envProbeReflected &&
           a.navCellSize == b.navCellSize && a.navMaxSlope == b.navMaxSlope &&
           a.navAgentRadius == b.navAgentRadius &&
           a.unitsPerMeter == b.unitsPerMeter &&
           a.terrainDetail == b.terrainDetail &&
           a.terrainViewDistance == b.terrainViewDistance &&
           eq3(a.skyColor, b.skyColor) && eq3(a.skyTopColor, b.skyTopColor) &&
           a.skyDome == b.skyDome && a.zenithSize == b.zenithSize &&
           a.eyeHeight == b.eyeHeight &&
           a.walkSpeed == b.walkSpeed && a.lookSpeed == b.lookSpeed &&
           a.sprintMultiplier == b.sprintMultiplier &&
           a.stickDeadzoneL == b.stickDeadzoneL &&
           a.stickDeadzoneR == b.stickDeadzoneR &&
           a.stickCurveL == b.stickCurveL && a.stickCurveR == b.stickCurveR &&
           a.stickExpL == b.stickExpL && a.stickExpR == b.stickExpR &&
           a.multiplayer == b.multiplayer &&
           a.p2JoinOnStart == b.p2JoinOnStart &&
           a.orbitSpeed == b.orbitSpeed && a.gravity == b.gravity &&
           a.jumpSpeed == b.jumpSpeed && eq3(a.lightDir, b.lightDir) &&
           a.ambient == b.ambient && a.diffuse == b.diffuse &&
           eq3(a.lightColor, b.lightColor) && a.brightness == b.brightness &&
           a.aoEnabled == b.aoEnabled && a.aoStrength == b.aoStrength &&
           a.aoRadius == b.aoRadius && a.giEnabled == b.giEnabled &&
           a.giRays == b.giRays && a.giBounces == b.giBounces &&
           a.giSkyLight == b.giSkyLight && a.giSunLight == b.giSunLight &&
           a.giAmbientFloor == b.giAmbientFloor &&
           a.giProbes == b.giProbes &&
           a.giProbeSpacing == b.giProbeSpacing &&
           a.giProbeHeight == b.giProbeHeight &&
           a.giProbeLevels == b.giProbeLevels &&
           a.terrainMaterial == b.terrainMaterial && a.bloom == b.bloom &&
           a.bloomThreshold == b.bloomThreshold &&
           a.bloomSpread == b.bloomSpread &&
           a.grain == b.grain && a.dofAmount == b.dofAmount &&
           a.dofFocus == b.dofFocus && a.dofRange == b.dofRange &&
           a.flare == b.flare && a.godRays == b.godRays &&
           a.blobShadows == b.blobShadows &&
           a.fogEnabled == b.fogEnabled &&
           eq3(a.fogColor, b.fogColor) && a.fogStart == b.fogStart &&
           a.fogEnd == b.fogEnd &&
           a.loadingScreen == b.loadingScreen &&
           a.highlightUsable == b.highlightUsable &&
           a.highlightDistance == b.highlightDistance &&
           eq3(a.highlightColor, b.highlightColor) &&
           a.highlightWidth == b.highlightWidth &&
           a.highlightSteps == b.highlightSteps &&
           a.highlightOpacity == b.highlightOpacity &&
           a.highlightOverlay == b.highlightOverlay;
}

// Per-scene override switches (Scene > Preferences). Each "scene-visual"
// category can override the project defaults; when a flag is off, the scene
// inherits Project::settings for that category (see project::resolvedSettings).
// Camera, physics, terrain detail and the game template stay project-wide.
struct SceneOverrides {
    bool lighting = false;    // lightDir, ambient, diffuse, lightColor, brightness
    bool sky = false;         // skyColor, skyTopColor, skyDome
    bool clipping = false;    // clipping mode
    bool terrainMat = false;  // terrainMaterial
    bool postFx = false;      // bloom, grain, depth of field, flare, god rays
    bool fog = false;         // fogEnabled, fogColor, fogStart, fogEnd
    bool highlight = false;   // highlightUsable + distance/color/width/steps
};

inline bool operator==(const SceneOverrides& a, const SceneOverrides& b) {
    return a.lighting == b.lighting && a.sky == b.sky && a.clipping == b.clipping &&
           a.terrainMat == b.terrainMat && a.postFx == b.postFx &&
           a.fog == b.fog && a.highlight == b.highlight;
}

class History;

// A HUD image (PNG sprite) drawn on top of the 3D scene.
struct HudImage {
    std::string name;
    std::string imagePath;      // e.g. "res/hud/crosshair.png"
    float pos[2] = {0.5f, 0.5f};   // normalized screen position (center anchor)
    float size[2] = {64.0f, 64.0f};  // on-screen draw size in pixels (PS2 screen
                                     // is 512x448); independent of the texture
                                     // resolution below (the sprite is stretched)

    // Build-time bake: the PS2 rejects textures that are not 8/16/32/64/128/
    // 256/512 in each dimension (a runtime assert), so the editor resizes the
    // imported PNG into .res-baked before it reaches the game. 0 = auto (the
    // nearest valid power-of-two of the source); otherwise a chosen valid size.
    int texW = 0;
    int texH = 0;
    // Palette quantization, like the per-asset material texture quality:
    // "" = follow the project default (ProjectSettings::textureQuant),
    // "none" = full color (32-bit) override, "8bit" = 256-color, "4bit" =
    // 16-color. Lets an important HUD element keep full color while the rest
    // of the project runs quantized (or vice versa).
    std::string texQuant;
};

inline bool operator==(const HudImage& a, const HudImage& b) {
    return a.name == b.name && a.imagePath == b.imagePath &&
           a.pos[0] == b.pos[0] && a.pos[1] == b.pos[1] &&
           a.size[0] == b.size[0] && a.size[1] == b.size[1] &&
           a.texW == b.texW && a.texH == b.texH && a.texQuant == b.texQuant;
}

// The built-in "USE" prompt as a customizable HUD element (Tools > UI
// Editor, a non-deletable entry). imagePath "" = the embedded built-in
// use.png sprite; a custom PNG replaces it (baked like any HUD image). The
// defaults reproduce the classic hardcoded placement: centered, top edge at
// 72% of the 448px screen, 128x32 px.
inline HudImage defaultUsePrompt() {
    HudImage h;
    h.name = "USE prompt";
    h.pos[0] = 0.5f;
    h.pos[1] = 0.72f + 16.0f / 448.0f;  // center anchor of the old top-at-72%
    h.size[0] = 128.0f;
    h.size[1] = 32.0f;
    return h;
}

// A typeface the project can draw with (Tools > Font Manager). Everything that
// references a font does so by `name`, so swapping a project's look is one
// edit here rather than a hunt through every text and menu.
//
// Two very different costs hang off one entry:
//  - Static text (HUD texts, menus, loading screens) is rasterized straight
//    from the TTF into a sprite at BUILD time, so the font itself never
//    reaches the PS2 - only the pixels of that one string.
//  - A font referenced by a Display Text node also bakes a glyph atlas
//    (res/fonts/atlas-<name>.png) the runtime samples glyph by glyph, because
//    that node's string is only known while the game runs. That atlas is the
//    only case where font pixels ship, and it is loaded lazily - see
//    templates.cpp's drawFontText.
struct GameFont {
    std::string name = "Default";
    // Source TTF: "" = the built-in default (the Consolas Bold fallback chain
    // in menubake::resolveFontPath), "res/fonts/x.ttf" = a font imported into
    // the project (travels with it), bare "impact.ttf" = a Windows font (that
    // machine only). This is the one place the editor resolves a real file.
    std::string fontPath;
    // Glyph height the atlas is rasterized at. Display Text scales from this,
    // so it trades atlas sharpness against VRAM, and is NOT the on-screen size.
    // Only matters to fonts a Display Text node actually uses.
    int atlasSize = 16;
    // Applied to the glyphs at runtime (the atlas itself bakes white, so one
    // atlas serves every color). Static baked text keeps its own per-text color.
    float color[3] = {1.0f, 1.0f, 1.0f};
    bool shadow = true;  // 1px dark offset behind the glyphs
    // Atlas palette depth: "4bit" (16 colors - plenty for white glyphs the
    // runtime tints, and ~8x cheaper in VRAM), "8bit", "none" = full color.
    std::string quant = "4bit";
};

inline bool operator==(const GameFont& a, const GameFont& b) {
    return a.name == b.name && a.fontPath == b.fontPath &&
           a.atlasSize == b.atlasSize && a.color[0] == b.color[0] &&
           a.color[1] == b.color[1] && a.color[2] == b.color[2] &&
           a.shadow == b.shadow && a.quant == b.quant;
}

// One inline icon a text can splice in (Tools > UI Editor > Button icons,
// docs/text-icons.md). ANY text in the project - HUD texts, menu titles and
// entry labels, loading-screen texts, Display Text nodes - substitutes
// `{{name}}` with this image, sized to the text it sits in.
//
// The seeded set is named after the pad buttons ("cross", "l1", "start", ...),
// which is also what makes `{{action:jump}}` work: that form resolves the
// action's bound button and then looks up the icon of that name. Extra entries
// with any name are fine ({{coin}}) - they just have no action to resolve from.
struct TextIcon {
    std::string name;  // the placeholder token: {{cross}}
    // Project-relative PNG. The seeded pad-button entries point at
    // res/hud/icon-<name>.png, which the editor generates when the file is
    // missing - overriding an icon is just replacing that PNG (or pointing
    // this at your own).
    std::string path;
    // Height relative to the text's line height (1 = as tall as a capital).
    float scale = 1.0f;
};

inline bool operator==(const TextIcon& a, const TextIcon& b) {
    return a.name == b.name && a.path == b.path && a.scale == b.scale;
}

// One piece of a text carrying icon placeholders: either a run of plain
// characters or a resolved icon token. Every text renderer walks these instead
// of raw bytes, which is what makes `{{cross}}` work in every text at once.
struct TextRun {
    std::string text;  // non-empty on a plain run
    std::string icon;  // non-empty on an icon token (a TextIcon name)
    // On an icon token that resolved through an ACTION ({{action:jump}} or the
    // {{jump}} shorthand): the action's name. This is what lets a consumer draw
    // the glyph from the LIVE binding instead of the one baked in - the
    // interaction prompts do exactly that (docs/text-icons.md).
    std::string action;
};

// Lowercased pad-button name, i.e. the TextIcon that stands for that button
// ("Cross" -> "cross"). The naming convention the seeded icon set follows.
inline std::string textIconNameForPad(const std::string& padName) {
    std::string s;
    for (char c : padName) s += (char)tolower((unsigned char)c);
    return s;
}

// The icon a token names when read as an ACTION rather than an icon: the
// lowercased pad button that action is bound to. Empty when no action goes by
// that name or it has no pad button.
//
// This is the `{{use}}` shorthand: `{{action:use}}` spelled out is precise, but
// a bare `{{use}}` is what people actually type, so a token that matches no
// icon gets one more chance as an action name before falling back to literal
// text. Icon names win - `{{cross}}` stays the Cross glyph even if a project
// ever names an action "cross".
inline std::string textIconForAction(const std::string& token,
                                     const InputMap& input) {
    if (!input.findAction(token)) return std::string();
    const InputBinding b = input.resolve(token);
    return b.pad.empty() ? std::string() : textIconNameForPad(b.pad);
}

// Splits `s` into plain runs and icon tokens.
//   {{cross}}        -> the icon named "cross"
//   {{action:jump}}  -> the icon named after the pad button the "jump" action
//                       is bound to in `input`'s active preset
// A token whose action is unknown or has no pad button, and anything that is
// not a well-formed `{{...}}`, stays LITERAL text - a typo shows up on screen
// instead of silently vanishing.
//
// A BARE token comes out as-is in TextRun::icon; a caller that finds no icon of
// that name should try textIconForAction() before giving up, which is what makes
// the `{{use}}` shorthand work (see there).
inline std::vector<TextRun> parseTextIcons(const std::string& s,
                                           const InputMap& input) {
    std::vector<TextRun> out;
    auto pushText = [&out](const std::string& t) {
        if (t.empty()) return;
        if (!out.empty() && out.back().icon.empty())
            out.back().text += t;
        else
            out.push_back(TextRun{t, ""});
    };
    size_t i = 0;
    while (i < s.size()) {
        const size_t open = s.find("{{", i);
        if (open == std::string::npos) {
            pushText(s.substr(i));
            break;
        }
        const size_t close = s.find("}}", open + 2);
        if (close == std::string::npos) {
            pushText(s.substr(i));
            break;
        }
        pushText(s.substr(i, open - i));
        const std::string token = s.substr(open + 2, close - open - 2);
        std::string icon = token;
        std::string action;
        if (token.rfind("action:", 0) == 0) {
            action = token.substr(7);
            const InputBinding b = input.resolve(action);
            icon = b.pad.empty() ? std::string() : textIconNameForPad(b.pad);
        }
        if (icon.empty())
            pushText(s.substr(open, close + 2 - open));  // keep it visible
        else
            out.push_back(TextRun{"", icon, action});
        i = close + 2;
    }
    return out;
}

// The text with every icon token removed - what a plain-text consumer (a
// window title, a list row) should show.
inline std::string stripTextIcons(const std::string& s, const InputMap& input) {
    std::string out;
    for (const TextRun& r : parseTextIcons(s, input)) out += r.text;
    return out;
}

// An on-screen text (Tools > UI Editor > Texts): baked to a PNG sprite at
// build (res/hud/text-<name>.png - the engine has no font), shown/hidden at
// runtime by the Set Text Visible flow node. Multi-line on '\n'. The string is
// frozen at build; for a runtime-varying one use a Display Text node instead.
struct HudText {
    std::string name = "text";
    std::string text = "New text";
    float pos[2] = {0.5f, 0.8f};   // normalized screen position (center anchor)
    int size = 16;                 // font pixel height
    float color[3] = {1.0f, 1.0f, 1.0f};
    // Which Project::fonts entry to rasterize with ("" = the default entry).
    std::string font;
    bool shadow = true;           // 1px dark offset behind the glyphs
    bool visibleAtStart = false;  // shown when the scene starts
};

inline bool operator==(const HudText& a, const HudText& b) {
    return a.name == b.name && a.text == b.text && a.pos[0] == b.pos[0] &&
           a.pos[1] == b.pos[1] && a.size == b.size &&
           a.color[0] == b.color[0] && a.color[1] == b.color[1] &&
           a.color[2] == b.color[2] && a.font == b.font &&
           a.shadow == b.shadow && a.visibleAtStart == b.visibleAtStart;
}

// A prompt text's starting state. HudText's own default is "New text" (right
// for the HUD-texts list, nonsense in a prompt field), so the prompts carry the
// classic word plus the button glyph - "{{use}} USE" reads as the old sprite did
// and follows a rebind (docs/text-icons.md).
inline HudText defaultPromptText(const char* name, const char* text) {
    HudText t;
    t.name = name;
    t.text = text;
    t.size = 20;
    return t;
}

// A progress bar on a loading screen (Tools > Loading Screens). Continuous =
// a track quad with a fill quad growing left-to-right with load progress.
// Quantized = `segments` cells lighting up one per completed 1/segments step;
// cells are colored rects, or copies of segImage when it names a PNG (lit
// cells tinted fillColor, unlit bgColor - color modulation, one texture).
struct LoadingBar {
    std::string name = "bar";
    int kind = 0;                    // 0 = continuous, 1 = quantized
    float pos[2] = {0.5f, 0.75f};    // normalized screen position (center anchor)
    float size[2] = {256.0f, 16.0f}; // total on-screen size in px (512x448 screen)
    float bgColor[3] = {0.15f, 0.15f, 0.15f};  // track / unlit segment tint
    float fillColor[3] = {0.9f, 0.9f, 0.9f};   // fill / lit segment tint
    int segments = 5;                // quantized only (2..16)
    float spacing = 6.0f;            // quantized: gap between segments, px
    // Optional segment sprite (quantized): imagePath "" = colored rects.
    // Reuses HudImage so the pow2/quant bake controls and texbake apply; its
    // pos/size are ignored (segments are laid out from the bar's pos/size).
    HudImage segImage;
};

inline bool operator==(const LoadingBar& a, const LoadingBar& b) {
    auto eq3 = [](const float* x, const float* y) {
        return x[0] == y[0] && x[1] == y[1] && x[2] == y[2];
    };
    return a.name == b.name && a.kind == b.kind && a.pos[0] == b.pos[0] &&
           a.pos[1] == b.pos[1] && a.size[0] == b.size[0] &&
           a.size[1] == b.size[1] && eq3(a.bgColor, b.bgColor) &&
           eq3(a.fillColor, b.fillColor) && a.segments == b.segments &&
           a.spacing == b.spacing && a.segImage == b.segImage;
}

// A named loading screen (Tools > Loading Screens): what the game shows while
// a scene loads. Scenes pick one by name (SceneData::loadingScreen); unnamed
// scenes use Project::defaultLoadingScreen; with neither, the game falls back
// to the classic built-in screen (hud/loading.png centered on black).
struct LoadingScreenDef {
    std::string name = "loading";
    float bgColor[3] = {0.0f, 0.0f, 0.0f};  // clear color behind everything
    std::vector<HudImage> images;  // PNG sprites, drawn in order
    std::vector<HudText> texts;    // baked text sprites (visibleAtStart unused)
    std::vector<LoadingBar> bars;  // progress bars, drawn on top
};

inline bool operator==(const LoadingScreenDef& a, const LoadingScreenDef& b) {
    return a.name == b.name && a.bgColor[0] == b.bgColor[0] &&
           a.bgColor[1] == b.bgColor[1] && a.bgColor[2] == b.bgColor[2] &&
           a.images == b.images && a.texts == b.texts && a.bars == b.bars;
}

// A boot splash screen (Tools > Loading Screens > Boot splash): a single image
// shown for `duration` seconds during startup, after the engine's Tyra logo
// and before the loading screen. Splashes play in list order. Images only for
// now. Independent of the loading-screen master toggle - splashes always show
// when defined. Reuses HudImage so import + pow2/quant bake apply.
struct SplashScreen {
    std::string name = "splash";
    HudImage image;                         // the graphic (fullscreen by default)
    float bgColor[3] = {0.0f, 0.0f, 0.0f};  // behind the image (letterbox)
    float duration = 2.0f;                  // seconds shown (0.1 .. 10)
};

inline bool operator==(const SplashScreen& a, const SplashScreen& b) {
    return a.name == b.name && a.image == b.image &&
           a.bgColor[0] == b.bgColor[0] && a.bgColor[1] == b.bgColor[1] &&
           a.bgColor[2] == b.bgColor[2] && a.duration == b.duration;
}

// One block of a credits roll (Tools > Credits Editor, docs/credits.md). A
// roll is a vertical FLOW of blocks laid out on the host and baked into page
// textures at build - the engine has no font, so nothing here reaches the PS2
// as text.
//
// Presentation fields default to "inherit the roll's" (size 0, empty font,
// ownColor false) so restyling a whole roll stays one edit, exactly like a
// text's font reference.
struct CreditsBlock {
    enum Kind {
        Heading = 0,  // a section title ("CAST"), the roll's heading size
        Line = 1,     // a line of text, wrapped to the content width
        Pair = 2,     // role on the left, name(s) on the right ("Music | Ana")
        Image = 3,    // a PNG, scaled to `scale` of the content width
        Gap = 4,      // `space` pixels of nothing
        Break = 5,    // skip to the next page: a screenful gap when scrolling,
                      // a new card in card mode (CreditsRoll::mode)
    };
    int kind = Line;
    std::string text;   // Heading/Line: the string; Pair: the left column
    std::string text2;  // Pair: the right column ('\n' = several names)
    std::string imagePath;  // Image: project-relative PNG ("res/credits/x.png")
    int size = 0;           // font pixel height; 0 = the roll's default
    std::string font;       // Project::fonts entry; "" = the roll's font
    bool ownColor = false;  // false = the roll's text color
    float color[3] = {1.0f, 1.0f, 1.0f};
    int align = 1;        // 0 left, 1 center, 2 right (Pair ignores it)
    float space = 0.0f;   // extra px below the block; Gap: the gap itself
    float scale = 1.0f;   // Image: width as a fraction of the content width
};

inline bool operator==(const CreditsBlock& a, const CreditsBlock& b) {
    return a.kind == b.kind && a.text == b.text && a.text2 == b.text2 &&
           a.imagePath == b.imagePath && a.size == b.size && a.font == b.font &&
           a.ownColor == b.ownColor && a.color[0] == b.color[0] &&
           a.color[1] == b.color[1] && a.color[2] == b.color[2] &&
           a.align == b.align && a.space == b.space && a.scale == b.scale;
}

// A credits roll (Tools > Credits Editor, docs/credits.md): the end-credits
// screen as project-wide data, started by the Play Credits flow node or a menu
// row, and free to end by going somewhere (a scene, a menu, a flow event).
//
// The roll owns the screen while it plays. Its blocks are laid out and baked
// into a strip of pow2 PAGE textures (see menubake::creditsLayout) rather than
// one sprite per line: a long roll would otherwise be dozens of textures on a
// ~1.33 MB VRAM budget, and pages keep the runtime at two sprite draws a frame.
struct CreditsRoll {
    std::string name = "credits";

    // --- look ---------------------------------------------------------------
    float bgColor[3] = {0.0f, 0.0f, 0.0f};  // cleared behind everything
    // Optional still backdrop (does NOT scroll), baked like any HUD image.
    HudImage bgImage;
    std::string font;   // default typeface for every block ("" = fonts[0])
    int headingSize = 22;
    int lineSize = 16;
    float color[3] = {1.0f, 1.0f, 1.0f};          // body text
    float headingColor[3] = {1.0f, 0.85f, 0.4f};  // Heading blocks
    bool shadow = true;                           // 1px dark offset
    int pageW = 512;      // page texture width: 256 or 512 (PS2 pow2 limit)
    float margin = 40.0f;   // side margin inside the page, px
    float columnGap = 24.0f;  // Pair: gap between the two columns
    float lineSpacing = 1.25f;  // line pitch as a multiple of the font size

    // --- motion -------------------------------------------------------------
    int mode = 0;              // 0 = scroll up, 1 = cards (one page at a time)
    float speed = 34.0f;       // scroll: pixels per second
    float cardSeconds = 4.0f;  // cards: seconds per card
    float startDelay = 0.8f;   // black/backdrop before anything moves
    float endHold = 2.0f;      // held after the last block has left
    float fadeIn = 0.6f;       // seconds of fade from the background color
    float fadeOut = 1.2f;      // seconds of fade out at the very end

    // --- music --------------------------------------------------------------
    // A Project::music track played when the roll starts ("" = leave whatever
    // is playing alone). Stopping at the end is the usual choice for a roll
    // that hands over to a menu.
    std::string music;
    bool musicLoop = true;
    bool musicStopAtEnd = true;
    int musicVolume = 100;

    // --- the player's way out ----------------------------------------------
    bool skippable = true;
    // Input Map action the skip listens on ("" = the menu confirm action, i.e.
    // whatever Cross is bound to). A skip runs the finish action, so it never
    // strands the player on a black screen.
    std::string skipAction;
    float skipAfter = 1.0f;  // seconds before a skip is accepted
    // Baked hint sprite ("PRESS {{action:...}} TO SKIP"): static text, so its
    // button glyph is the binding at BUILD time (a Display Text node is the
    // tool for one that must follow a runtime rebind).
    bool showSkipHint = true;
    std::string skipHint = "PRESS {{confirm}} TO SKIP";
    float hintPos[2] = {0.5f, 0.93f};  // normalized screen position, centered
    int hintSize = 14;

    // --- where it goes afterwards ------------------------------------------
    enum Finish {
        Resume = 0,       // back to the game exactly where it was
        SwitchScene = 1,  // param = scene name
        OpenMenu = 2,     // param = menu name (a title screen, typically)
        FlowEvent = 3,    // param = event name (On Menu Event triggers)
        Hold = 4,         // stay on the last frame (an ending that ends)
    };
    int finish = Resume;
    std::string finishParam;

    // Page texture depth, like a font atlas carries its own (GameFont::quant):
    // "4bit" (16 colors) is plenty for text on a flat background and ~8x
    // cheaper in VRAM than full color. "none" / "8bit" / "4bit".
    std::string quant = "4bit";
    // Text file the blocks were last imported from (docs/credits.md), kept so
    // "Re-import" can pick up an edited file. Editor-side only.
    std::string source;

    std::vector<CreditsBlock> blocks;
};

inline bool operator==(const CreditsRoll& a, const CreditsRoll& b) {
    auto eq3 = [](const float* x, const float* y) {
        return x[0] == y[0] && x[1] == y[1] && x[2] == y[2];
    };
    return a.name == b.name && eq3(a.bgColor, b.bgColor) &&
           a.bgImage == b.bgImage && a.font == b.font &&
           a.headingSize == b.headingSize && a.lineSize == b.lineSize &&
           eq3(a.color, b.color) && eq3(a.headingColor, b.headingColor) &&
           a.shadow == b.shadow && a.pageW == b.pageW &&
           a.margin == b.margin && a.columnGap == b.columnGap &&
           a.lineSpacing == b.lineSpacing && a.mode == b.mode &&
           a.speed == b.speed && a.cardSeconds == b.cardSeconds &&
           a.startDelay == b.startDelay && a.endHold == b.endHold &&
           a.fadeIn == b.fadeIn && a.fadeOut == b.fadeOut &&
           a.music == b.music && a.musicLoop == b.musicLoop &&
           a.musicStopAtEnd == b.musicStopAtEnd &&
           a.musicVolume == b.musicVolume && a.skippable == b.skippable &&
           a.skipAction == b.skipAction && a.skipAfter == b.skipAfter &&
           a.showSkipHint == b.showSkipHint && a.skipHint == b.skipHint &&
           a.hintPos[0] == b.hintPos[0] && a.hintPos[1] == b.hintPos[1] &&
           a.hintSize == b.hintSize && a.finish == b.finish &&
           a.finishParam == b.finishParam && a.quant == b.quant &&
           a.source == b.source && a.blocks == b.blocks;
}

// A named editor window layout (docking arrangement), stored per project and
// switchable from the Layout menu. `ini` is an ImGui docking dump
// (SaveIniSettingsToMemory); when it is empty and `recipe` >= 0 the layout is
// (re)built programmatically from a built-in DockBuilder recipe the first time
// it is shown - this is how the seeded built-ins (Default/Director/Material)
// start life without needing an ImGui context at project-create time.
// `openWindows` lists the optional editor windows (Cutscene Director, Material
// Editor, ...) that must be open for the layout to make sense; switching to the
// layout opens them, and saving the layout captures whichever are currently
// open. Layouts are editor state, not game data or undo history.
struct WindowLayout {
    std::string name;
    std::string ini;                       // ImGui docking dump; empty = use recipe
    int recipe = -1;                       // -1 none, 0 default, 1 director, 2 material
    std::vector<std::string> openWindows;  // optional-window keys (see App::layoutWindowKeys)
};

// Built-in DockBuilder recipe ids (WindowLayout::recipe). The arrangement lives
// in App::buildLayoutRecipe; only the id travels in the .tyra file.
enum class LayoutRecipe {
    None = -1,
    Default = 0,
    Director = 1,
    Material = 2,
    Debugger = 3,
    Procedural = 4
};

// One custom screen effect placed in the screen stack. The effect body lives
// in a <project>/screen-effects/<stem>.screenfx file (loaded into
// customScreenEffects()); this is the project-side record: which effect, where
// in the stack, and its param values. See CustomScreenFx in screenfx.hpp.
struct ScreenFxPlacement {
    std::string key;             // "custom:<file-stem>"
    int layer = -1;              // stack slot, like Project::hudBloomLayer
    bool enabled = true;         // unchecked = kept but not composited/generated
    float params[4] = {0, 0, 0, 0};
};

inline bool operator==(const ScreenFxPlacement& a, const ScreenFxPlacement& b) {
    return a.key == b.key && a.layer == b.layer && a.enabled == b.enabled &&
           a.params[0] == b.params[0] && a.params[1] == b.params[1] &&
           a.params[2] == b.params[2] && a.params[3] == b.params[3];
}

// A streaming layer: a named group of scene objects that the game can load
// into / evict from memory at runtime (Load Layer / Unload Layer flow nodes)
// - GTA3-style interior streaming. Also doubles as an editor visibility
// group (the eye toggle hides the layer's objects while editing).
struct SceneLayer {
    std::string name = "Layer";
    bool startLoaded = true;    // game: resident when the scene starts
    bool editorVisible = true;  // editor-only: draw/pick the objects

    // Auto-streaming (opt-in): the game loads this layer while the player is
    // within streamRadius of (streamX, streamZ) and unloads it once they
    // leave radius + hysteresis - GTA-style zone streaming without wiring
    // the flow graph. Requests are edge-triggered (issued only when the
    // player crosses the boundary), so Load/Unload Layer nodes can still
    // override until the next crossing. When enabled, the initial residency
    // comes from the spawn distance, not startLoaded.
    bool autoStream = false;
    float streamX = 0.0f, streamZ = 0.0f;  // zone center, world units
    float streamRadius = 60.0f;            // load within this range
    // Zone shape: the name of an Area object in this scene (docs/areas.md)
    // whose oriented box IS the zone, replacing the (streamX, streamZ) +
    // streamRadius circle. Empty / dangling = the circle. Unlike the circle
    // the box also bounds Y, so a zone can cover one floor of a building; the
    // area is read live, so a moved area moves the zone with it.
    std::string streamArea;
};

inline bool operator==(const SceneLayer& a, const SceneLayer& b) {
    return a.name == b.name && a.startLoaded == b.startLoaded &&
           a.editorVisible == b.editorVisible && a.autoStream == b.autoStream &&
           a.streamX == b.streamX && a.streamZ == b.streamZ &&
           a.streamRadius == b.streamRadius && a.streamArea == b.streamArea;
}

// A scene: its own objects (each with its flow graph), its own terrain
// (size, heightmap, texture) and its own lighting. Sky, physics prefs, HUD
// and audio assets are shared; the game starts in the first scene and
// switches via the Switch Scene flow node.
struct SceneData {
    std::string name = "main";
    std::vector<SceneObject> objects;
    std::vector<SceneLayer> layers;

    TerrainConfig terrain;
    // Heightmap: vertex heights on the render grid (row-major, hmW x hmD).
    // Empty = flat. Persisted in <project>/terrain-<scene>.heights.
    std::vector<float> heights;
    int hmW = 0, hmD = 0;

    // Terrain layer painting (docs/terrain-painting.md). `terrainLayers` are the
    // paintable layers ABOVE the base terrain material (index 0 = first extra
    // layer). `splat` holds their per-VERTEX weight on the terrain render grid
    // (splatW x splatD == hmW x hmD - the blend is drawn as Gouraud vertex
    // alpha, so vertex resolution IS the blend resolution),
    // terrainLayers.size() bytes per vertex (row-major:
    // [z*splatW + x]*N + layer), 0..255; the base gets the remaining weight.
    // Empty layers => all base = the single-material terrain. Persisted in
    // <project>/terrain-<scene>.splat.
    std::vector<TerrainLayer> terrainLayers;
    std::vector<uint8_t> splat;
    int splatW = 0, splatD = 0;
    // Stochastic tiling for the BASE terrain material (the layers carry their
    // own flag). See TerrainLayer::stochastic.
    bool terrainBaseStochastic = false;
    // Macro ground variation (docs/terrain-painting.md): large soft patches of
    // lighter/darker ground, world-position value noise multiplied into the
    // terrain vertex shade while chunks bake - base AND layer passes together,
    // so it reads as ground lighting, not an overlay. Zero runtime cost;
    // infinite period (breaks even the supertile's second-order repetition).
    // variation = amplitude 0..1 (0 = off), scale = patch size in world units.
    float terrainTintVariation = 0.0f;
    float terrainTintScale = 24.0f;

    // Per-scene overrides of the project's scene-visual settings. `settings`
    // holds this scene's values; `overrides` says which categories are active.
    // Inactive categories inherit Project::settings - resolve with
    // project::resolvedSettings(); never read these fields directly for the
    // final value (an inactive category's stored values are stale).
    ProjectSettings settings;
    SceneOverrides overrides;

    // Ambience preset this scene uses (name into Project::ambiencePresets).
    // Empty = the project default preset (Project::defaultAmbience). When a
    // preset resolves, project::resolvedSettings overlays its sky/lighting/fog
    // over this scene's settings.
    std::string ambiencePreset;

    // Loading screen shown while this scene loads (name into
    // Project::loadingScreens). Empty = the project default
    // (Project::defaultLoadingScreen); a dangling name also falls back there.
    std::string loadingScreen;
};

inline bool operator==(const SceneData& a, const SceneData& b) {
    return a.name == b.name && a.objects == b.objects && a.layers == b.layers &&
           a.terrain.width == b.terrain.width && a.terrain.depth == b.terrain.depth &&
           a.terrain.enabled == b.terrain.enabled &&
           a.heights == b.heights && a.hmW == b.hmW && a.hmD == b.hmD &&
           a.terrainLayers == b.terrainLayers && a.splat == b.splat &&
           a.splatW == b.splatW && a.splatD == b.splatD &&
           a.terrainBaseStochastic == b.terrainBaseStochastic &&
           a.terrainTintVariation == b.terrainTintVariation &&
           a.terrainTintScale == b.terrainTintScale &&
           a.overrides == b.overrides && a.settings == b.settings &&
           a.ambiencePreset == b.ambiencePreset &&
           a.loadingScreen == b.loadingScreen;
}

// One selectable row of a generated in-game menu.
struct MenuEntry {
    std::string label = "New entry";
    // What Cross does on this row. Close/scene/save-menu also dismiss the
    // menu; set/add value, events and toggles/choices keep it open (add a
    // Close entry).
    enum Action {
        Close = 0,       // dismiss the menu (a title screen's "Start")
        SwitchScene = 1, // param = scene name
        OpenSaveMenu = 2,
        OpenMenu = 3,    // param = menu name (submenu; Triangle goes back)
        SetValue = 4,    // param = save value name, amount = new value
        AddValue = 5,    // param = save value name, amount = delta
        FlowEvent = 6,   // param = event name (fires On Menu Event triggers)
        // Stateful rows. The state lives in a save value (param = its name):
        // the value holds the option index, the save value's default is the
        // initial state, and flow graphs react through the pure bool sources
        // (Value At Least -> On Condition). Cross / dpad right cycle forward,
        // dpad left backward. The current option label renders right-aligned
        // on the row from the baked value strip (menubake).
        Toggle = 7,      // two options, "Off"/"On" unless customized
        Choice = 8,      // one of `options`, cycled in order
        // Commits the display-mode selection staged by a BindDisplayMode
        // row. While any menu in the project has such a row, the display
        // row only cycles its save value (the player browses freely); this
        // row fires the actual scan-mode switch (+ the keep-or-revert
        // confirm). Without one, display rows keep the classic
        // switch-on-change behavior.
        ApplyVideo = 9,
        // Rebinds one input action (docs/input-bindings.md). `bindAction` names
        // the Tools > Input Map action, `param` the save value holding the
        // player's override as an inputCodes() index (0 = the project's preset
        // binding). Selecting the row arms capture mode: the next button or
        // key the player presses becomes the binding. The row draws its
        // current binding as runtime text from the menu's font atlas, so it is
        // not limited to a baked option strip.
        RebindKey = 10,
        // Rolls the credits (param = a Project::credits roll name). Closes the
        // menu first, so a title screen's CREDITS row hands the screen over and
        // the roll's own finish action decides what comes back.
        PlayCredits = 11,
    };
    int action = Close;
    std::string param;
    float amount = 0.0f;
    // Toggle/Choice option labels (value = index into this list). Toggle
    // treats an empty list as {"Off", "On"}.
    std::vector<std::string> options;
    // BindDisplayMode rows only: the Tyra::DisplayMode each option drives
    // (parallel to `options`, values 0..4; -1 = the project-default mode,
    // resolved at boot on the player's console - region + the PAL-picture
    // preference). Empty = the option index itself (the legacy positional
    // mapping), so old projects behave unchanged.
    std::vector<int> optionModes;
    // Ready-made "option block" binding (Menu Editor > Insert option block).
    // On a Toggle/Choice row this makes the generated game map the row's
    // option index (held in the bound save value) straight onto a built-in
    // engine setting every frame - no flow graph needed. None = a plain
    // stateful row (the classic behavior). The option index -> value mapping
    // is spread evenly across the row's options (see applyMenuBindings in the
    // generated game): e.g. 5 volume options -> 0/25/50/75/100 %.
    enum Setting {
        BindNone = 0,
        BindMusicVolume = 1,  // engine music volume (0..100)
        BindSfxVolume = 2,    // master sound-effect volume (0..100)
        BindDeadzone = 3,     // analog stick deadzone, both sticks (0..0.4)
        BindStickCurve = 4,   // stick response curve exponent (1..3)
        BindDisplayMode = 5,  // scan mode: interlaced / 480p / 1080i / field
                              // / PAL 576i (see MenuEntry::optionModes)
        BindWidescreen = 6,   // aspect ratio: 4:3 / 16:9
        BindPlayerCount = 7,  // 1 / 2 players (two-player modes; runtime join)
        BindInputPreset = 8,  // Tools > Input Map preset (options = presets)
    };
    int settingBind = BindNone;
    // RebindKey rows only: which InputAction the row rebinds (by name). Last
    // field on purpose - the positional MenuEntry{...} initializers in app.cpp
    // predate it and must keep meaning what they say.
    std::string bindAction;
};

inline bool operator==(const MenuEntry& a, const MenuEntry& b) {
    return a.label == b.label && a.action == b.action && a.param == b.param &&
           a.bindAction == b.bindAction && a.amount == b.amount &&
           a.options == b.options && a.optionModes == b.optionModes &&
           a.settingBind == b.settingBind;
}

// One image composited into a menu's baked panel (see GameMenu::images).
struct MenuImage {
    std::string path;  // PNG, e.g. "res/hud/logo.png"
    enum Slot {
        AboveTitle = 0,   // block in the vertical flow, above the title
        AboveEntries = 1, // between the title and the entry rows
        BelowEntries = 2, // under the entry rows, above the button hints
        Background = 3,   // stretched under everything (dark wash on top)
        Overlay = 4,      // in front of the text, freeform offset position
    };
    int slot = AboveTitle;
    float scale = 1.0f;              // on top of the fit-to-panel size
    float offset[2] = {0.0f, 0.0f};  // px nudge; Overlay: top-left position
};

inline bool operator==(const MenuImage& a, const MenuImage& b) {
    return a.path == b.path && a.slot == b.slot && a.scale == b.scale &&
           a.offset[0] == b.offset[0] && a.offset[1] == b.offset[1];
}

// A generated in-game menu. The editor bakes the whole panel (title, entry
// labels, button hints - the engine has no font) into res/menus/<name>.png
// on every build; the game pauses while one is open. Navigation: dpad,
// Cross = select, Triangle = back/close.
struct GameMenu {
    std::string name = "menu";
    std::string title = "MENU";
    bool titleScreen = false;  // opens automatically at game start
    // Gameplay freezes while the menu is open (plus a dim overlay). false =
    // the menu floats over the running game; pad presses reach both.
    bool pauseGame = true;
    // The Start button opens this menu in-game and closes it again (the
    // classic pause menu; one per project).
    bool pauseMenu = false;
    float accent[3] = {0.47f, 0.82f, 1.0f};  // border/title tint
    // Images composited into the baked panel. Flow slots (AboveTitle /
    // AboveEntries / BelowEntries) are blocks in the panel's vertical flow -
    // list order = stacking order within a slot; Background stretches under
    // everything (with a dark wash); Overlay draws in front of the text at
    // a freeform offset.
    std::vector<MenuImage> images;
    // Panel placement: texture width (pow2 - PS2 requirement) and the
    // normalized screen position of the panel center (like HUD images).
    int panelW = 256;  // 128 / 256 / 512
    float screenPos[2] = {0.5f, 0.45f};
    bool showTitle = true;  // off = logo-only menus (skips title + separator)
    // Which Project::fonts entry the panel is baked with ("" = the default
    // entry). Only the typeface is taken from it - the panel's colors come
    // from `accent` and the bake itself.
    std::string font;
    int titleSize = 18;  // px; entrySize also drives the row pitch (and the
    int entrySize = 15;  // cursor geometry) through menubake::panelLayout.
    std::vector<MenuEntry> entries;
};

inline bool operator==(const GameMenu& a, const GameMenu& b) {
    return a.name == b.name && a.title == b.title &&
           a.titleScreen == b.titleScreen && a.pauseGame == b.pauseGame &&
           a.pauseMenu == b.pauseMenu && a.accent[0] == b.accent[0] &&
           a.accent[1] == b.accent[1] && a.accent[2] == b.accent[2] &&
           a.images == b.images && a.panelW == b.panelW &&
           a.screenPos[0] == b.screenPos[0] && a.screenPos[1] == b.screenPos[1] &&
           a.showTitle == b.showTitle && a.font == b.font &&
           a.titleSize == b.titleSize && a.entrySize == b.entrySize &&
           a.entries == b.entries;
}

// A named value persisted on the memory card (project-wide, not per scene).
// Flow graph Save nodes read/write these; every save slot stores a snapshot.
struct SaveValue {
    std::string name;
    float value = 0.0f;  // starting value on a fresh game
};

inline bool operator==(const SaveValue& a, const SaveValue& b) {
    return a.name == b.name && a.value == b.value;
}

// A named text persisted on the memory card (same lifecycle as SaveValue;
// stored in fixed 32-byte slots in the save payload, so keep them short).
struct SaveTextValue {
    std::string name;
    std::string value;  // starting text on a fresh game
};

inline bool operator==(const SaveTextValue& a, const SaveTextValue& b) {
    return a.name == b.name && a.value == b.value;
}

// One non-destructive edit of one animation clip of one model asset
// (Tools > Animation Editor). The source file is never rewritten: these
// numbers are folded into the clip on its way into the .tskl at build time,
// and the editor's preview applies the identical math (animedit.hpp).
//
// Order of operations, both here and in the preview: trim in SOURCE seconds,
// then rebase the trimmed range to start at 0, then scale time. The clip the
// game sees is `rename` (when set) with duration
// (trimEnd - trimStart) / (timeScale * project fps ratio).
struct AnimClipEdit {
    std::string model;   // project-relative asset, e.g. "res/models/hero.glb"
    std::string clip;    // source clip name, as authored in the file
    std::string rename;  // "" = keep the source name
    float timeScale = 1.0f;   // >1 plays faster; on top of the project fps ratio
    float trimStart = 0.0f;   // seconds into the source clip; 0 = from the start
    float trimEnd = 0.0f;     // seconds into the source clip; 0 = to the end
    bool loop = true;         // default Loop for objects that pick this clip

    // True when this entry changes nothing - the Animation Editor drops such
    // entries on save so an untouched project keeps an empty list.
    bool isDefault() const {
        return rename.empty() && timeScale == 1.0f && trimStart == 0.0f &&
               trimEnd == 0.0f && loop;
    }
};

inline bool operator==(const AnimClipEdit& a, const AnimClipEdit& b) {
    return a.model == b.model && a.clip == b.clip && a.rename == b.rename &&
           a.timeScale == b.timeScale && a.trimStart == b.trimStart &&
           a.trimEnd == b.trimEnd && a.loop == b.loop;
}

// One particle BURST thrown by a weapon: the muzzle flash, the impact, the
// spray off a body. Distinct from a scene Emitter object, which owns a
// permanent pool and runs forever - a burst is a one-shot fired at an
// arbitrary world point from a small shared runtime pool, so a firefight
// costs a fixed amount of memory no matter how many shots are in the air.
// The simulation lives in the generated weapons.gen.cpp; the editor's Weapon
// Editor previews it with the same numbers. See docs/weapons.md.
struct WeaponFx {
    // 0 none, 1 flash (a short bright puff that does not travel),
    // 2 sparks (radial, pulled down), 3 smoke (slow lazy rise),
    // 4 blood (wet spray, heavy), 5 debris (chunks that fall and stay put).
    int kind = 0;
    float color[3] = {1.0f, 0.82f, 0.35f};
    // Base particle size in world units - the quad's HALF extent, like
    // SceneObject::emitterSize. Small numbers here: a muzzle flash lives one
    // unit from the eye, where 0.1 already fills a quarter of the screen.
    float size = 0.05f;
    int count = 8;       // particles in one burst (1..32)
    float life = 0.25f;  // seconds until the burst is gone
    float speed = 4.0f;  // initial speed, units/s (0 = the burst hangs)
};

// Starting effects for a NEW weapon. A weapon whose three effect slots are
// all "none" is a weapon that shoots invisible bullets, which is nobody's
// intent - so a fresh definition arrives already looking like a gun.
inline WeaponFx defaultMuzzleFx() {
    WeaponFx f;
    f.kind = 1;  // flash
    f.color[0] = 1.0f, f.color[1] = 0.85f, f.color[2] = 0.40f;
    f.size = 0.045f, f.count = 5, f.life = 0.07f, f.speed = 1.5f;
    return f;
}
inline WeaponFx defaultImpactFx() {
    WeaponFx f;
    f.kind = 2;  // sparks
    f.color[0] = 1.0f, f.color[1] = 0.80f, f.color[2] = 0.45f;
    f.size = 0.035f, f.count = 8, f.life = 0.35f, f.speed = 5.0f;
    return f;
}
inline WeaponFx defaultBloodFx() {
    WeaponFx f;
    f.kind = 4;  // blood
    f.color[0] = 0.55f, f.color[1] = 0.05f, f.color[2] = 0.05f;
    f.size = 0.055f, f.count = 8, f.life = 0.5f, f.speed = 3.5f;
    return f;
}

inline bool operator==(const WeaponFx& a, const WeaponFx& b) {
    return a.kind == b.kind && a.color[0] == b.color[0] &&
           a.color[1] == b.color[1] && a.color[2] == b.color[2] &&
           a.size == b.size && a.count == b.count && a.life == b.life &&
           a.speed == b.speed;
}

// A weapon (Tools > Weapon Editor). Project-wide like fonts and menus: scenes
// don't own weapons, objects REFERENCE them by name through
// SceneObject::weapons (a Player's starting inventory, an NPC's armament).
//
// Three kinds, and the kind decides which half of the struct matters:
//  - Hitscan firearm: the shot arrives the frame it is fired (a ray against
//    object bounding spheres + the terrain). What every PS2-era gun was.
//  - Projectile firearm: a visible body of matter flies out, drops under
//    gravity and damages what it touches - grenade launchers, plasma, arrows.
//  - Melee: an arc swept in front of the attacker; everything damageable
//    inside the cone and within reach is hit once per swing.
//
// The viewmodel is deliberately a SCENE OBJECT (by name, renames remap) and
// not a separate asset path: the weapon in your hands is then an ordinary
// object you place, light, material and see in the viewport, and every
// existing pipeline (models, LODs, materials, streaming) applies to it for
// free. While equipped the game pins it in front of the camera; the rest of
// the time it is hidden.
struct WeaponDef {
    std::string name = "Weapon";
    int kind = 0;  // 0 hitscan firearm, 1 projectile firearm, 2 melee

    // --- Damage
    float damage = 25.0f;   // hit points per hit (per PELLET on a shotgun)
    float range = 60.0f;    // hitscan/melee reach; projectile flight range
    float falloff = 0.0f;   // 0..1 of the damage lost at maximum range
    float impulse = 0.0f;   // physics impulse (units/s) pushed into a body hit

    // --- Firing
    float fireRate = 4.0f;  // shots per second (the cooldown between shots)
    bool automatic = false; // hold the fire button, vs one shot per press
    float spread = 1.0f;    // cone half-angle of the shot, degrees
    int pellets = 1;        // rays per shot: 1 = a bullet, 8 = buckshot
    float recoil = 1.0f;    // view kick per shot, degrees (decays back)
    float rumble = 0.3f;    // pad vibration per shot, 0..1 (0 = none)

    // --- Ammo. magSize 0 = the weapon has no magazine at all (melee, and
    // guns that should never reload); reserve -1 = bottomless spare ammo.
    int magSize = 12;
    int reserve = 60;
    float reloadTime = 1.2f;

    // --- Projectile (kind 1)
    float projSpeed = 40.0f;
    float projGravity = 9.8f;  // units/s^2; 0 = flies dead straight
    float projSize = 0.15f;
    float projColor[3] = {1.0f, 0.7f, 0.2f};
    float blastRadius = 0.0f;  // > 0 = the impact damages everything in range

    // --- Melee (kind 2)
    float meleeArc = 70.0f;   // swing half-angle around the aim, degrees
    float swingTime = 0.35f;  // seconds of swing (also the viewmodel arc)

    // --- Viewmodel (a scene object; "" = an invisible weapon)
    std::string viewModel;
    // Camera-space offset: X right, Y up, Z forward. The default sits the
    // weapon low-right like every FPS since Wolfenstein.
    float viewOffset[3] = {0.22f, -0.18f, 0.55f};
    float viewScale = 1.0f;
    float viewRot[3] = {0.0f, 0.0f, 0.0f};  // degrees, applied in the camera frame
    // Where the shot leaves the weapon, in the same camera-space frame -
    // the muzzle flash and the tracer start here.
    float muzzleOffset[3] = {0.22f, -0.1f, 1.0f};

    // --- Viewmodel animation (docs/weapons.md) ---------------------------
    // Two ways to make the weapon move, and the choice follows the ASSET:
    //  0 PROCEDURAL - the runtime animates the viewmodel's transform from the
    //    numbers below. Needs no animated model, which is exactly what a
    //    generated weapon is: a static .obj cannot carry clips.
    //  1 CLIPS - the viewmodel is an animated .glb/.fbx and the runtime plays
    //    the clips named below on fire / reload / equip through the ordinary
    //    animation system. The procedural KICK and SWING then stand aside
    //    (the clip owns them), but the idle sway and the walk bob stay on -
    //    a baked clip cannot know how fast the player is moving.
    // Clip mode on a static model is a harmless no-op (nothing resolves), so
    // switching an asset never breaks the build.
    int animMode = 0;
    // Recoil, per shot. The kick is a spring the runtime decays; these are
    // its amplitude and rate, decoupled from the VIEW kick (`recoil`) so a
    // weapon can jolt in the hands without moving the aim, or vice versa.
    float animKickBack = 0.10f;    // units the weapon drives into the screen
    float animKickPitch = 7.0f;    // degrees the muzzle rises
    float animRecover = 7.0f;      // decay rate, 1/s (higher = snappier)
    // Life while nothing is happening. Sway is the hands never being still;
    // bob rides the player's actual planar speed, so it stops when they do.
    float animSway = 0.012f;       // idle sway amplitude, units (0 = dead still)
    float animSwaySpeed = 1.6f;    // idle sway rate, cycles/s
    float animBob = 0.030f;        // walk bob amplitude at full speed, units
    // Reload: the weapon drops out of the aim and rolls while it happens, over
    // the weapon's own reloadTime.
    float animReloadDip = 0.22f;   // units it drops
    float animReloadRoll = 35.0f;  // degrees it rolls
    // Melee: the lunge and the chop, over swingTime.
    float animSwingReach = 0.35f;  // forward lunge, units
    float animSwingPitch = 55.0f;  // chop angle, degrees
    // Clip mode only. Empty = that state does not change the clip (an empty
    // Idle leaves whatever the model was already playing).
    std::string clipIdle;
    std::string clipFire;
    std::string clipReload;
    std::string clipEquip;

    // --- Effects
    WeaponFx muzzleFx = defaultMuzzleFx();  // at the muzzle as the shot leaves
    WeaponFx impactFx = defaultImpactFx();  // where the shot lands (scenery)
    WeaponFx bloodFx = defaultBloodFx();    // where it lands on something alive
    bool tracer = false;                          // a streak muzzle -> hit
    float tracerColor[3] = {1.0f, 0.9f, 0.5f};

    // --- Sounds (Project::sounds entries; "" = silent)
    std::string fireSound;
    std::string reloadSound;
    std::string emptySound;   // played when the trigger is pulled dry
    std::string impactSound;  // played at the hit point
};

inline bool operator==(const WeaponDef& a, const WeaponDef& b) {
    auto eq3 = [](const float* x, const float* y) {
        return x[0] == y[0] && x[1] == y[1] && x[2] == y[2];
    };
    return a.name == b.name && a.kind == b.kind && a.damage == b.damage &&
           a.range == b.range && a.falloff == b.falloff &&
           a.impulse == b.impulse && a.fireRate == b.fireRate &&
           a.automatic == b.automatic && a.spread == b.spread &&
           a.pellets == b.pellets && a.recoil == b.recoil &&
           a.rumble == b.rumble && a.magSize == b.magSize &&
           a.reserve == b.reserve && a.reloadTime == b.reloadTime &&
           a.projSpeed == b.projSpeed && a.projGravity == b.projGravity &&
           a.projSize == b.projSize && eq3(a.projColor, b.projColor) &&
           a.blastRadius == b.blastRadius && a.meleeArc == b.meleeArc &&
           a.swingTime == b.swingTime && a.viewModel == b.viewModel &&
           eq3(a.viewOffset, b.viewOffset) && a.viewScale == b.viewScale &&
           eq3(a.viewRot, b.viewRot) && eq3(a.muzzleOffset, b.muzzleOffset) &&
           a.animMode == b.animMode && a.animKickBack == b.animKickBack &&
           a.animKickPitch == b.animKickPitch &&
           a.animRecover == b.animRecover && a.animSway == b.animSway &&
           a.animSwaySpeed == b.animSwaySpeed && a.animBob == b.animBob &&
           a.animReloadDip == b.animReloadDip &&
           a.animReloadRoll == b.animReloadRoll &&
           a.animSwingReach == b.animSwingReach &&
           a.animSwingPitch == b.animSwingPitch &&
           a.clipIdle == b.clipIdle && a.clipFire == b.clipFire &&
           a.clipReload == b.clipReload && a.clipEquip == b.clipEquip &&
           a.muzzleFx == b.muzzleFx && a.impactFx == b.impactFx &&
           a.bloodFx == b.bloodFx && a.tracer == b.tracer &&
           eq3(a.tracerColor, b.tracerColor) && a.fireSound == b.fireSound &&
           a.reloadSound == b.reloadSound && a.emptySound == b.emptySound &&
           a.impactSound == b.impactSound;
}

// Ready-made procedural viewmodel motions (Weapon Editor > Animation >
// "Motion preset"). A generated weapon is a static .obj and can carry no
// clips, so procedural motion is the only animation it will ever have - and
// hand-tuning ten numbers to find out what "a pistol" feels like is the wrong
// first experience. These are the starting points; every number stays
// editable afterwards. "Create viewmodel" picks the one matching the
// generated kind, so a fresh weapon arrives already moving.
enum WeaponAnimPreset {
    WeaponAnimSnap = 0,     // a light pistol: quick, small, fast recovery
    WeaponAnimHeavy = 1,    // revolver / shotgun: a real shove, slow settle
    WeaponAnimChatter = 2,  // SMG / rifle: tiny kick, very fast recovery
    WeaponAnimShove = 3,    // launcher: the biggest kick, slowest settle
    WeaponAnimBlade = 4,    // melee: barely any kick, a wide swing
    WeaponAnimLocked = 5,   // nothing moves (a mounted gun, a debug rig)
    WeaponAnimPresetCount = 6,
};

inline const char* weaponAnimPresetName(int p) {
    switch (p) {
        case WeaponAnimSnap: return "Pistol snap";
        case WeaponAnimHeavy: return "Heavy recoil";
        case WeaponAnimChatter: return "Automatic chatter";
        case WeaponAnimShove: return "Launcher shove";
        case WeaponAnimBlade: return "Blade swing";
        case WeaponAnimLocked: return "Locked down";
        default: return "Custom";
    }
}

// Overwrites only the procedural motion fields - the clip names, the offsets
// and everything about damage are left alone, so applying a preset to a tuned
// weapon changes how it MOVES and nothing else.
inline void applyWeaponAnimPreset(WeaponDef& w, int preset) {
    // kickBack, kickPitch, recover, sway, swaySpeed, bob, dip, roll, reach, chop
    struct P {
        float kb, kp, rec, sw, sws, bob, dip, roll, reach, chop;
    };
    static const P kP[] = {
        {0.08f, 6.0f, 9.0f, 0.010f, 1.6f, 0.028f, 0.20f, 30.0f, 0.35f, 55.0f},
        {0.18f, 12.0f, 5.0f, 0.014f, 1.2f, 0.034f, 0.28f, 45.0f, 0.35f, 55.0f},
        {0.05f, 3.5f, 14.0f, 0.008f, 2.0f, 0.026f, 0.22f, 25.0f, 0.35f, 55.0f},
        {0.22f, 10.0f, 4.0f, 0.016f, 1.0f, 0.036f, 0.30f, 20.0f, 0.35f, 55.0f},
        {0.04f, 2.0f, 10.0f, 0.018f, 1.4f, 0.040f, 0.10f, 10.0f, 0.45f, 70.0f},
        {0.0f, 0.0f, 8.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f},
    };
    if (preset < 0 || preset >= WeaponAnimPresetCount) return;
    const P& p = kP[preset];
    w.animKickBack = p.kb, w.animKickPitch = p.kp, w.animRecover = p.rec;
    w.animSway = p.sw, w.animSwaySpeed = p.sws, w.animBob = p.bob;
    w.animReloadDip = p.dip, w.animReloadRoll = p.roll;
    w.animSwingReach = p.reach, w.animSwingPitch = p.chop;
}

struct Project {
    std::string name;
    std::string dir;  // absolute path to project root
    // Stable, opaque project identity (16 hex chars), persisted in the .tyra
    // manifest. Distinguishes projects independently of name/path - the remote
    // collaboration cache keys downloaded projects on it. Generated at create,
    // backfilled on load for older projects (see project::ensureProjectId).
    std::string projectId;
    // The starting preset, chosen ONCE in the New Project dialog and fixed for
    // the project's life (Project > Preferences shows it read-only): it decides
    // which game-template sources are generated, and those sources are
    // user-ownable - flipping it later would either overwrite work or, on an
    // owned file, silently stop matching what the project actually builds.
    // "orbit" = Empty (no player entity); "fpp" and "thirdperson" both generate
    // the player-entity template and differ in the seeded Player's mode.
    std::string gameTemplate = "orbit";  // "orbit" | "fpp" | "thirdperson"

    // Does this project's template carry a player entity (i.e. is it anything
    // but the Empty/orbit preset)? The one place the two player presets are
    // treated as one thing.
    bool hasPlayerTemplate() const { return gameTemplate != "orbit"; }
    ProjectSettings settings;
    std::vector<SceneData> scenes{SceneData{}};
    int activeScene = 0;  // scene edited in the editor (not persisted in json)

    // The active scene - what the editor UI operates on.
    SceneData& active() {
        if (activeScene < 0 || activeScene >= (int)scenes.size()) activeScene = 0;
        return scenes[activeScene];
    }
    const SceneData& active() const {
        const int i =
            (activeScene < 0 || activeScene >= (int)scenes.size()) ? 0 : activeScene;
        return scenes[i];
    }
    std::vector<SceneObject>& objects() { return active().objects; }
    const std::vector<SceneObject>& objects() const { return active().objects; }

    // Name of the fallback font (what an empty `font` reference means).
    std::string defaultFontName() const {
        return fonts.empty() ? std::string() : fonts.front().name;
    }
    // Indices into `fonts` that some Display Text node draws with - the only
    // fonts that need a glyph atlas baked and shipped (static text rasterizes
    // straight from the TTF at build). Sorted, no duplicates.
    std::vector<int> atlasFontIndices() const;
    // Font by name; an empty or stale name resolves to the default entry, so
    // deleting a font never breaks the texts still pointing at it.
    const GameFont* findFont(const std::string& name) const {
        if (fonts.empty()) return nullptr;
        for (const GameFont& f : fonts)
            if (f.name == name) return &f;
        return &fonts.front();
    }

    // Uniform scale a new object gets when this model is dropped into a
    // scene: the model's real-world size (meters per file unit) expressed in
    // the project's own units. 1.0 for a model whose real size was never
    // recorded, and for every project that left the world scale at 1 unit =
    // 1 meter - so this only ever moves content that asked for it.
    float modelInsertScale(const std::string& modelPath) const {
        auto it = modelUnitMeters.find(modelPath);
        if (it == modelUnitMeters.end()) return 1.0f;
        const float s = it->second * settings.unitsPerMeter;
        return s > 0.0001f ? s : 1.0f;
    }

    // Typefaces the project draws with (Tools > Font Manager). Never empty:
    // fonts[0] is the default every text falls back to, and the Font Manager
    // refuses to delete the last entry - so an empty `font` reference always
    // resolves. Replace fonts[0] to restyle the whole project at once.
    std::vector<GameFont> fonts{GameFont{}};

    std::vector<HudImage> hud;
    // Inline text icons (Tools > UI Editor > Button icons,
    // docs/text-icons.md): any text in the project can splice one in with a
    // {{name}} placeholder. Seeded with one entry per pad button by
    // project::ensureTextIcons, so {{cross}} works in a fresh project.
    std::vector<TextIcon> textIcons;
    // The USE prompt as an overridable HUD element (see defaultUsePrompt).
    // Always present - the UI Editor edits it but cannot delete it.
    HudImage usePrompt = defaultUsePrompt();
    // The two interaction prompts (Tools > UI Editor > USE prompt) are each
    // either TEXT or an IMAGE - an explicit mode, not "text wins when non-empty":
    // switching to the image to compare should not mean losing the text you
    // typed. Text is the interesting mode because it can carry a button glyph
    // that follows the binding, which is why a fresh project starts on it
    // ("{{use}} Use" / "{{use}} Pick up", docs/text-icons.md).
    //
    // Either way the build produces ONE sprite per prompt and the game draws it
    // the same: text is rasterized to res/hud/use-text.png / pick-text.png and
    // the prompt simply points there. The texts' `pos` is unused (the USE
    // prompt's own position places both); size/color/font/shadow are the text's.
    bool usePromptIsText = false;
    HudText usePromptText = defaultPromptText("use-prompt", "{{use}} USE");
    // The "PICK UP" prompt, shown instead of USE while the looked-at object is
    // pickable. It shares the USE prompt's screen position; `pickPromptImage`
    // empty = the built-in res/hud/pickup.png.
    bool pickPromptIsText = false;
    HudText pickPromptText =
        defaultPromptText("pick-prompt", "{{use}} PICK UP");
    std::string pickPromptImage;
    // On-screen texts baked to sprites at build, triggered by the Show Text /
    // Hide Text flow nodes (Tools > UI Editor > Texts).
    std::vector<HudText> hudTexts;
    // Where the full-screen post effects sit in the screen stack (Tools > UI
    // Editor). Bloom (with color grading) and film grain are placed
    // independently: the effect applies right before the HUD sprite at that
    // index, so sprites with a lower index get the effect and higher ones draw
    // crisp on top. -1 = apply at the very end of the frame, over everything
    // including menus (the classic behavior, and the default). Typical split:
    // bloom under the HUD so it does not blur the crosshair, grain at -1 as a
    // filmic overlay over the whole screen. Grading rides with bloom.
    int hudBloomLayer = -1;
    int hudGrainLayer = -1;
    // Custom screen effects placed in the screen stack (Tools > UI Editor).
    // Each placement references a <project>/screen-effects/*.screenfx file by
    // its key ("custom:<stem>") and carries the effect's per-placement param
    // values; `layer` has the same meaning as hudBloomLayer (index into `hud`,
    // -1 = topmost). Project-wide like bloom/grain (not per-scene), and not on
    // the undo stack (edited via saveAll, like the rest of the UI Editor).
    // Placements whose .screenfx file is missing on load are dropped.
    std::vector<ScreenFxPlacement> screenFx;
    // Music tracks (16-bit 22kHz stereo WAV in res/audio/), played via the
    // flow graph (Play Music / Stop Music / Set Music Volume actions).
    std::vector<std::string> music;
    // Per-track build-time conversion (Music panel, "PS2 build" controls):
    // the res/audio source stays untouched; after every build the Runner
    // re-converts the bin/audio copy the game actually streams. Lower rates
    // and mono are the knobs when music snags on a real console over the
    // network deploy (each halves the byte rate). Keyed by the music relPath;
    // absent entry = ship as-is. rate 0 = keep the source rate.
    struct MusicBuildOpt {
        int rate = 0;
        bool mono = false;
    };
    std::map<std::string, MusicBuildOpt> musicBuild;
    // Sound effects (16-bit 22kHz WAV in res/sfx/, converted to ADPCM by the
    // toolchain at build). One-shots via the flow graph Play Sound action.
    std::vector<std::string> sounds;
    // Custom values persisted in memory card saves (Project panel, Save data).
    std::vector<SaveValue> saveValues;
    // Custom text values persisted in memory card saves (same panel).
    std::vector<SaveTextValue> saveTexts;
    // Per-asset texture-quality overrides of ProjectSettings::textureQuant,
    // keyed by asset path (a res/models .obj or a .mtl library): "none" /
    // "8bit" / "4bit". Textures referenced by several assets take the
    // HIGHEST requested quality - e.g. everything 4-bit, but the hero model
    // pinned to "none" keeps its textures full color.
    std::map<std::string, std::string> textureQuality;
    // Artist-authored mesh LOD variants of a static model, keyed by the
    // model's asset path ("res/models/tree.obj") -> the tier files in order,
    // nearest first ("res/models/tree_lod1.obj", ...). No entry (the default)
    // = the build decimates automatically. A tier must keep the model's
    // material set (same count, same usemtl names) and be smaller than the
    // one before it, or the build warns and falls back to decimation.
    // Only used when a mesh LOD distance is in play (Preferences > Rendering
    // or a per-object override) - see docs/model-pipeline.md.
    std::map<std::string, std::vector<std::string>> modelLods;
    // Real-world size of an imported model, keyed by its asset path:
    // how many METERS one unit of the file measures. An entry exists only
    // for models whose real size is known - written when a model is imported
    // (docs/world-scale.md) - so procedurally generated assets that are
    // authored in world units already (the Tree Generator) stay untouched.
    // Combined with ProjectSettings::unitsPerMeter it gives the scale a new
    // object gets when the model is dropped into a scene; see
    // Project::modelInsertScale.
    std::map<std::string, float> modelUnitMeters;
    // In-game menus (Project panel, Menus): panels baked at build, opened by
    // the Open Menu flow node, menu entries, or at boot (titleScreen).
    std::vector<GameMenu> menus;
    // Configurable buttons/keys (Tools > Input Map, docs/input-bindings.md).
    // Named actions + per-project binding presets; the generated game reads
    // every gameplay button through them. Never empty after a load -
    // project::ensureInputActions() seeds the built-in roles.
    InputMap input;
    // Color grading presets (Tools > Color Grading): project-wide looks
    // applied as GS full-screen passes. defaultGrading is the index applied
    // at game boot (-1 = none); the Set Color Grading flow node switches
    // presets at runtime (the switch persists across scene changes).
    std::vector<ColorGradingPreset> gradings;
    int defaultGrading = -1;
    // Ambience presets (Tools > Ambience Editor): project-wide sky/lighting/fog
    // "mood" bundles. defaultAmbience is the preset a scene uses when it names
    // none (-1 = fall back to the raw project/scene settings). A scene picks
    // its preset by name (SceneData::ambiencePreset); the Set Ambience flow
    // node repaints the sky at runtime.
    std::vector<AmbiencePreset> ambiencePresets;
    int defaultAmbience = -1;
    // Loading screens (Tools > Loading Screens): project-wide definitions of
    // what shows while a scene loads. defaultLoadingScreen is the one used by
    // scenes that name none (-1 = the classic built-in hud/loading.png on
    // black). A scene picks its screen by name (SceneData::loadingScreen).
    // The ProjectSettings::loadingScreen bool stays the master enable.
    std::vector<LoadingScreenDef> loadingScreens;
    int defaultLoadingScreen = -1;
    // Boot splash screens (Tools > Loading Screens > Boot splash): images shown
    // in order at startup, after the Tyra logo, before the loading screen.
    std::vector<SplashScreen> splashScreens;
    // Credits rolls (Tools > Credits Editor, docs/credits.md): project-wide
    // end-credits screens, started by the Play Credits flow node or a menu row.
    // Persist through save() but are not part of undo/redo, like the preset
    // collections above.
    std::vector<CreditsRoll> credits;
    // Cutscene Director sequences (Tools > Cutscene Director): project-wide
    // keyframe timelines that pose scene objects + the camera over time. Like
    // the preset collections above they persist through save() but are not part
    // of undo/redo. The Play/Stop Sequence flow nodes drive them at runtime.
    std::vector<Sequence> sequences;

    // Weapons (Tools > Weapon Editor): project-wide definitions objects
    // reference by name through SceneObject::weapons. Like the preset
    // collections above they persist through save() but are not part of
    // undo/redo. Empty = the project has no combat and every weapon table,
    // runtime file and flow node compiles out of the game entirely.
    std::vector<WeaponDef> weapons;

    // Weapon by name; nullptr when the name is empty or dangling (unlike
    // fonts there is no fallback entry - a dangling weapon reference is a
    // real authoring mistake and the codegen reports it).
    const WeaponDef* findWeapon(const std::string& n) const {
        if (n.empty()) return nullptr;
        for (const WeaponDef& w : weapons)
            if (w.name == n) return &w;
        return nullptr;
    }
    int weaponIndex(const std::string& n) const {
        for (size_t i = 0; i < weapons.size(); ++i)
            if (weapons[i].name == n) return (int)i;
        return -1;
    }

    // Non-destructive animation-clip edits (Tools > Animation Editor). One
    // entry per (model, source clip) the user has touched; clips with no
    // entry bake exactly as authored. The source .glb/.fbx is never
    // modified - the edits are applied to the parsed skeleton on the way
    // into the .tskl at build time (animedit::applyClipEdits), and the
    // viewport preview applies the same numbers, so what you scrub is what
    // ships. Like the preset collections above these persist through save()
    // but are not part of undo/redo.
    std::vector<AnimClipEdit> animClipEdits;

    // Prefabs (Tools > Prefabs, docs/prefabs.md): reusable groups of scene
    // objects - their flow graphs included - stamped into the world by hand,
    // by a procedural graph, or by the Spawn Prefab node while the game runs.
    // Project-wide like the preset collections above (a prefab built in one
    // scene is available in all of them) and persisted through save(), but not
    // part of undo/redo. Members carry transforms LOCAL to the prefab origin.
    std::vector<Prefab> prefabs;

    // --- Editor-side state, persisted in the .tyra project file ------------
    // Not game data and not part of undo/redo (undo lives in the history
    // file). Restores the editing session on reopen.
    int selectedObject = -1;   // selected object in the active scene (-1 none)
    int gizmoOp = 0;           // transform gizmo: 0 move, 1 rotate, 2 scale
    int gizmoSpace = 0;        // gizmo axes: 0 absolute (world), 1 camera-relative
    int viewMode = 0;          // viewport shading: 0 solid, 1 wire, 2 wire+solid
    // Viewport camera projection (Viewport::Projection): 0 perspective,
    // 1 ortho (free), 2..7 the locked Top/Bottom/Front/Back/Right/Left views.
    int viewProjection = 0;
    // Live Debugger breakpoints (docs/live-debugger.md), as
    // "<objectId>:<nodeId>" - the owning object's stable id and the flow-graph
    // node id, so they survive renames, reorders and rebuilds. Personal
    // editing state: kept in the .tyra like the selection, but deliberately
    // NOT a collaboration section (a peer's breakpoints are their own).
    std::vector<std::string> debugBreakpoints;
    // Named window layouts (docking arrangements), switchable from the Layout
    // menu and edited by simply rearranging windows. Every project keeps at
    // least one; seedBuiltinLayouts() fills a fresh/legacy project with the
    // Default/Director/Material built-ins. activeLayout indexes into this list.
    std::vector<WindowLayout> windowLayouts;
    int activeLayout = 0;
    // Emulator path + dev-PS2 IP. These are machine-global editor settings
    // (stored in editor.ini, edited in Edit > Preferences), NOT persisted in the
    // .tyra file. They live on Project only as the runtime transport the Runner
    // reads: App::attachProject copies the global values in on every open. The
    // headless --build path (main.cpp) also sets ps2LinkIp here directly.
    std::string emulatorPath;  // PCSX2 exe; empty = auto-detect under Program Files
    std::string ps2LinkIp;     // ps2link IP for "Run on PS2"; empty = disabled

    bool valid() const { return !name.empty() && !dir.empty(); }
    std::string elfName() const { return name + ".elf"; }
    // Handed to PCSX2 (and ps2client) on a command line, so it must come out
    // natively separated - see filePath().
    std::string elfPath() const { return filePath("bin/" + elfName()); }

    // A project-relative path (always stored forward-slashed: "res/models/x.obj")
    // as a real filesystem path in the platform's OWN separators. ALWAYS use
    // this instead of `dir + "\\" + rel` - outside Windows a backslash is an
    // ordinary FILENAME character, so the hand-join silently names a file that
    // does not exist and the asset just fails to load.
    //
    // The make_preferred() is not cosmetic. Plain `path(dir) / relative` leaves
    // a MIXED path on Windows ("C:\proj\bin/proj.elf"): the CRT and
    // std::filesystem accept it, so every load and every fs::exists() check
    // passes, but an external program need not - PCSX2 v2.6.3 answers
    // "Requested boot ELF ... does not exist" for exactly the ELF it boots
    // fine when the same path is spelled "C:\proj\bin\proj.elf" - which is
    // what broke Build & Run on Windows entirely. Anything handed to another
    // process must be natively separated, and normalizing here is what makes
    // that true for every caller.
    std::string filePath(const std::string& relative) const {
        return (std::filesystem::path(dir) / relative).make_preferred().string();
    }
};

namespace project {

// Creates the project directory, generates all Tyra game sources / build files
// and the <name>.tyra project file. `preset` picks the starting content, and it
// is the project's permanent game template (Project::gameTemplate):
//   "empty"       - orbit camera, no objects.
//   "fpp"         - player template, one Player entity in walk (FPP) mode.
//   "thirdperson" - player template, one Player entity in third-person mode
//                   (the avatar is that object's own animated model, assigned
//                   later - the camera rig works without one).
// `unitsPerMeter` is the project's world scale (ProjectSettings::unitsPerMeter,
// docs/world-scale.md); the metric-by-definition FPP/physics defaults (eye
// height, walk speed, gravity, jump) are multiplied by it so the preset player
// is person-sized whatever scale the project chose. Decided here and not later
// because changing the scale afterwards deliberately rescales nothing.
// Returns empty string on success, error message otherwise.
std::string create(Project& out, const std::string& name, const std::string& parentDir,
                   const TerrainConfig& terrain, const std::string& preset = "empty",
                   float unitsPerMeter = 1.0f);

// Fills p.windowLayouts with the three built-in layouts (Default, Director,
// Material Designer) as recipe-backed entries with empty ini, and resets
// activeLayout to 0. Used for fresh projects and to migrate older projects that
// predate named layouts. A pre-existing single "layout" dump can be preserved
// by the caller by assigning it into windowLayouts[0].ini after seeding.
void seedBuiltinLayouts(Project& p);

// A fresh opaque object id (16 hex chars from a 64-bit random value). Unique
// within a project with negligible collision odds; the merge/file-split layout
// keys on it. See SceneObject::id.
std::string newObjectId();

// Assigns a stable id to every scene object that lacks one (empty id, e.g. an
// object from a pre-id project or a fresh paste), and repairs any accidental
// duplicate so ids stay unique project-wide. Idempotent - a no-op once every
// object already has a distinct id. Called on load, on create, and from the
// editor's commitChange() so no object is ever persisted without an id.
void ensureObjectIds(Project& p);

// Assigns Project::projectId when it is empty (fresh create or a project from
// before project ids existed). Idempotent; persisted on the next save.
void ensureProjectId(Project& p);

// Fills in the built-in input actions and the "Default" preset (Tools > Input
// Map) with the bindings that were hardcoded before the Input Map existed, so
// a project from an older TyraX plays identically. Only ADDS what is missing:
// an action the user renamed/rebound/deleted stays as it is, and re-running is
// a no-op. Called from create() and at the end of load().
void ensureInputActions(Project& p);

// The built-in action name for a role (InputAction::Role), e.g. "jump" - what
// ensureInputActions seeds and what the codegen role slots look for. Empty for
// RoleNone / out-of-range values.
const char* inputRoleName(int role);

// Fills in the pad-button text icons (Tools > UI Editor > Button icons) so
// {{cross}} and {{action:jump}} resolve in a fresh or older project. Only ADDS
// missing entries - a renamed/repointed/deleted icon stays as the user left it.
// Their PNGs are generated into res/hud/ when absent (saveAssets).
void ensureTextIcons(Project& p);

// --- Per-object / per-section (de)serialization ------------------------------
// The building blocks of both the on-disk format and the collaboration wire
// format: an object body is the exact JSON written to objects/<id>.json, a
// section is a group of project-wide manifest keys serialized as one JSON
// object. Both directions work purely in memory.

// One scene object as a standalone JSON object string (the objects/<id>.json
// body, without the trailing newline).
std::string objectJson(const SceneObject& o);

// Parses a standalone object body produced by objectJson. Returns false when
// the string is not a JSON object; unknown/missing keys take their defaults
// (same reader the project load uses).
bool parseObject(const std::string& body, SceneObject& out);

// Project-wide manifest sections (everything in the .tyra except the scene
// table, the per-object bodies and the editor-side state). Each serializes
// independently so the collaboration layer can diff and ship them one at a
// time; save()/load() are recomposed from the same writers/readers.
enum class Section {
    Settings = 0,    // "settings" (project preferences)
    Hud,             // "hud", "usePrompt", "hudTexts", bloom/grain layers, "screenFx"
    Audio,           // "music", "musicBuild", "sounds"
    TexQuality,      // "textureQuality" (per-asset overrides)
    ModelLods,       // "modelLods" (per-model custom LOD meshes)
    SaveData,        // "saveValues", "saveTexts"
    Gradings,        // "gradings", "defaultGrading"
    Ambience,        // "ambience", "defaultAmbience"
    LoadingScreens,  // "loadingScreens", "defaultLoadingScreen"
    Splash,          // "splashScreens"
    Credits,         // "credits" (Tools > Credits Editor)
    Sequences,       // "sequences"
    Menus,           // "menus"
    AnimEdits,       // "animClipEdits"
    ModelUnits,      // "modelUnits" (per-model real-world size)
    Input,           // "input" (actions + binding presets)
    Prefabs,         // "prefabs" (reusable object groups)
    Weapons,         // "weapons" (Tools > Weapon Editor)
};
// KEEP THIS EQUAL TO THE ENUM SIZE. save() loops sections by index, so a count
// one short silently stops writing the LAST section to the .tyra - and parallel
// branches keep adding sections (ModelLods, ModelUnits, Input, Prefabs and
// Weapons all arrived while this one was open), which is exactly how it drifts.
// It HAS drifted: Input+Prefabs made 17 sections while this still said 16, so
// Prefabs was silently never written; Weapons makes 18.
constexpr int kSectionCount = 18;

// Stable lowercase identifier for a section (wire format / diagnostics).
const char* sectionName(Section s);

// The section as one standalone JSON object string (its manifest keys wrapped
// in braces). Deterministic: equal project state = equal string.
std::string sectionJson(const Project& p, Section s);

// Replaces the section's fields in `p` from a sectionJson() string. Fields the
// blob does not carry reset to their defaults (a section blob is total, not a
// patch). Returns false when the string is not a JSON object.
bool applySectionJson(Project& p, Section s, const std::string& body);

// An in-memory image of one project-model file. relativePath uses forward
// slashes (a wire path, not an OS path).
struct VirtualFile {
    std::string relativePath;
    std::string content;
};

// Byte images of every model file exactly as save()/saveHeights() would write
// them: the <name>.tyra manifest, one objects/<id>.json per live object and
// one terrain-<scene>.heights per scene - WITHOUT touching disk. The
// collaboration host ships these so a joining client sees the live (possibly
// unsaved) model, not the last saved state.
std::vector<VirtualFile> manifestFiles(const Project& p);

// The scene table WITHOUT the per-object bodies: each scene's name, terrain
// size, scene-visual settings/overrides, ambience/loading refs, layers, and
// its ordered list of object ids. This is the collaboration "scene-layout"
// message - the structural skeleton; object bodies travel separately as
// objectJson. Deterministic (equal state = equal string).
std::string scenesLayoutJson(const Project& p);

// Rebuilds p.scenes from a scenesLayoutJson() string: scene count / names /
// meta / ordered membership. Objects are pulled BY ID out of p's current
// scenes into the new arrangement (so a move/reorder keeps the object's body);
// an id with no current object gets a default placeholder (a matching
// objectJson upsert is expected to have arrived first). Per-scene heightmaps
// are preserved by scene index. Returns false when the string is malformed.
bool applyScenesLayout(Project& p, const std::string& body);

// The effective settings for a scene: the project defaults with each scene
// category (lighting, sky, clipping, terrain material, post-FX, highlight)
// replaced by the scene's own values where its override flag is set. All
// codegen and viewport code reads scene-visual settings through this.
ProjectSettings resolvedSettings(const Project& p, const SceneData& s);

// Index into Project::ambiencePresets of the preset a scene resolves to:
// its named preset if it exists, otherwise the project default. -1 = none
// (no presets, or a dangling/empty name with no default).
int ambienceIndexFor(const Project& p, const SceneData& s);

// Index into Project::loadingScreens of the screen a scene resolves to: its
// named screen if it exists, otherwise the project default. -1 = the built-in
// fallback screen (hud/loading.png centered on black).
int loadingScreenIndexFor(const Project& p, const SceneData& s);

// A terrain material resolved to what the terrain actually needs.
struct TerrainMaterial {
    bool present = false;            // false = no material -> checker greens
    std::string texture;             // res-relative map_Kd ("" = flat color)
    float kd[3] = {1.0f, 1.0f, 1.0f};  // tint (defaults to white)
    float tile[2] = {1.0f, 1.0f};    // texture repeats per world unit (u, v)
};

// Resolves a terrain material (.mtl asset, res-relative, e.g. from
// resolvedSettings(...).terrainMaterial) to its first material's map_Kd
// texture, Kd tint and "-s" tiling. `present` is false when unassigned or the
// .mtl is unreadable. Codegen, the editor viewport and the ISO planner resolve
// through this so they agree on the terrain's texture, color and tiling.
TerrainMaterial resolveTerrainMaterial(const Project& p, const std::string& matRel);

// Loads the single <name>.tyra project file from an existing project
// directory (game data + editor-side state + window layout).
std::string load(Project& out, const std::string& projectDir);

// Writes the single <name>.tyra project file. Editor-side state (selection,
// gizmo, view mode) and the window layout are taken from the Project fields.
std::string save(const Project& p);

// A flow graph as the project-file JSON ("nodes"/"links"/"nextId" - the same
// shape stored inside objects/<id>.json). Used by the headless --dump-graph
// CLI so AI agents can read a graph without parsing the whole object file.
std::string flowGraphToJson(const FlowGraph& fg);

// --- Terrain heightmap -------------------------------------------------------

// Vertex grid dimensions for the ACTIVE scene terrain size + detail cap.
void terrainGridDims(const Project& p, int& vertsW, int& vertsD);

// Makes every scene heightmap match its grid (zero-fill or resample).
void ensureHeightmap(Project& p);

// Bilinear height at world coordinates in the ACTIVE scene (0 outside).
float heightAtWorld(const Project& p, float x, float z);

// Raise/lower with a smooth (cosine) falloff brush.
void sculptHeightmap(Project& p, float worldX, float worldZ, float radius, float delta);

// Level the terrain toward targetH under the brush (same falloff; strength
// is the per-stroke lerp rate, 0..1).
void flattenHeightmap(Project& p, float worldX, float worldZ, float radius, float targetH,
                      float strength);

std::string saveHeights(const Project& p);
void loadHeights(Project& p);  // silent no-op when the file is absent

// --- Terrain splatmap --------------------------------------------------------

// Makes every scene splatmap match its layer count and the terrain render grid
// (the splat stores per-VERTEX weights - splatW/splatD track hmW/hmD):
// zero-fills a fresh map, resamples an existing one when the grid changed, and
// grows/shrinks the per-vertex layer stride when layers are added/removed.
// A scene with no terrainLayers keeps an empty splat.
void ensureSplatmap(Project& p);

// Paints the active layer under a world-space brush (cosine falloff). `delta`
// > 0 adds the layer's weight, < 0 erases it (toward the base). Clamped 0..255.
void paintSplat(Project& p, int layer, float worldX, float worldZ, float radius,
                float delta);

// Active-scene terrain layer edits that keep the interleaved splat columns in
// sync (the splat stores terrainLayers.size() bytes per texel, in layer order).
void addTerrainLayer(Project& p, const std::string& name, const std::string& material);
void removeTerrainLayer(Project& p, int idx);       // drops its splat column
void moveTerrainLayer(Project& p, int idx, int dir);  // dir = -1 up / +1 down; swaps columns

std::string saveSplat(const Project& p);
void loadSplat(Project& p);  // silent no-op when the file is absent

// --- Areas (PrimitiveType::Area, docs/areas.md) ------------------------------
// An Area is an invisible oriented box: the unit cube under the object's
// position/rotation/scale, exactly what the editor draws as a wireframe. These
// three helpers are the ONE implementation every consumer shares - the
// Properties/Layers previews, codegen (mirror/portal/camera-feed target lists,
// the streaming-layer zone) and the generated game's point test (emitted from
// areaContainsPointSource() so the runtime cannot drift from the host).

// The Area object named `name` in an object list (nullptr = none/not an Area).
// Takes the raw vector, not a SceneData, so the viewport - which only ever
// sees the active scene's objects - shares the same resolution.
const SceneObject* findArea(const std::vector<SceneObject>& objs,
                            const std::string& name);

// Is the world point inside the area's box?
bool areaContainsPoint(const SceneObject& area, float x, float y, float z);

// Types an area may catch: everything the game draws as static geometry (the
// same set the mirror/portal pickers offer). Markers have nothing to render or
// reflect, so an area never picks them up.
bool areaCatchable(PrimitiveType t);

// Scene-object indices the named area catches, in scene-table order: catchable
// objects whose bounding sphere (half the largest scale axis, the USE picker's
// approximation) touches the volume. `exclude` drops the referencing object
// itself (-1 = none). Empty for a missing/dangling area name.
std::vector<int> areaCaughtObjects(const std::vector<SceneObject>& objs,
                                   const std::string& areaName, int exclude);

// Object names reachable by something that can move, hide or re-target them at
// runtime: same-scene flow nodes with an object-name param (writers and readers
// alike - over-including is cheap), mirror/portal target lists, and the
// project's cutscene tracks / camera-shot bindings (they apply to whatever
// scene is active). Shared by static-batching eligibility and the live-catch
// candidate set, which is why over-including stays safe in both: the first
// costs one solo bag, the second one point test per frame.
std::set<std::string> runtimeRefNames(const Project& p,
                                      const std::vector<SceneObject>& objs);

// Can this object's transform change (or can the object appear) after load?
// The exact complement of the immovability static batching relies on, so a
// live-catch candidate is by construction never a batch member - it always has
// the solo bag a second submission needs.
bool objectRuntimeMovable(const SceneObject& o,
                          const std::set<std::string>& refs);

// Scene-object indices a LIVE catch area (SceneObject::catchAreaLive) has to
// re-test every frame: every catchable object that can move, in scene-table
// order. Deliberately not filtered by the area - a candidate's whole point is
// that it may be outside now and inside next frame. `exclude` drops the
// referencing object itself (-1 = none).
std::vector<int> areaLiveCandidates(const std::vector<SceneObject>& objs,
                                    int exclude,
                                    const std::set<std::string>& refs);

// --- History file (<name>.history) ------------------------------------------
// The undo history (up to History::kMaxEntries scene snapshots), kept next to
// the project file. Churny disposable editor state - gitignored in generated
// projects; the .tyra project file is the tracked source of truth.

std::string saveHistory(const Project& p, const History& h);

// Restores the undo history. Returns an error string when the file is
// missing/malformed/stale - the caller should then start a fresh history.
std::string loadHistory(const Project& p, History& h);

// Rewrites editor-owned files from the current templates and project data:
// docker infra (Dockerfile, docker-compose.yml) and generated headers
// (terrain_config.hpp, scene_data.hpp) are always rewritten; game sources
// are rewritten only while they still carry the "Generated by TyraX"
// marker in the first line (delete it to take ownership of a file).
// Called before every build.
std::string refreshGenerated(const Project& p);

// --- Live Link structure hashing (docs/live-link.md) ------------------------
// Objects are addressed by a stable 64-bit id hash, so a live session
// survives renames/reorders and can even spawn NEWLY ADDED objects through
// the game's runtime spawn pool (cloning an authored template with an equal
// "recipe"). The Runner writes liveLinkSigFile() to bin/livelink.sig at build
// start; the editor compares the live project against it every tick and
// streams livelink.bin only while the session is representable - anything
// else flips the toolbar to "LIVE (rebuild)". See templates::liveLinkScript.

// Stable per-object identity: FNV-1a 64 of SceneObject::id (name fallback).
// Also baked into scene_data.hpp (SCENE_*_OBJECT_ID_HASHES) for the game.
uint64_t liveLinkIdHash(const SceneObject& o);
// Everything a live patch can't change and a spawn clone copies from its
// template - equal recipes = interchangeable as templates. Transform + color
// (the live-patched fields) are excluded, except for objects whose transform
// is baked at build (point lights, projecting decals).
uint64_t liveLinkRecipeHash(const SceneObject& o);
// False for objects the spawn pool can't faithfully instantiate (baked
// lights/decals/mirrors, objects carrying flow graphs or attached scripts).
bool liveLinkCanSpawnLive(const SceneObject& o);
// Cross-object structure (scene count, streaming-layer tables).
uint64_t liveLinkContextHash(const Project& p);
// The whole as-built record written to bin/livelink.sig.
std::string liveLinkSigFile(const Project& p);

}  // namespace project
