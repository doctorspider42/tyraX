#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "ambience.hpp"
#include "flowgraph.hpp"
#include "grading.hpp"
#include "screenfx.hpp"
#include "sequence.hpp"

struct TerrainConfig {
    int width = 64;   // world units, X axis
    int depth = 64;   // world units, Z axis
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
};

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
    float playerWalkSpeed = 0.4f;  // units per frame at full stick
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

    // Per-object logic. Object-referencing nodes default to this object
    // ("self"), so a copied object brings a working copy of its behavior.
    FlowGraph flowGraph;

    // Attached object scripts: class names registered in src/scripts/*.cpp
    // with TYRA_OBJECT_SCRIPT(Name). Each attachment becomes its own script
    // instance in the game (Unity-style components); the same class can be
    // attached to any number of objects across scenes.
    std::vector<std::string> scripts;
};

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
           a.modelPath == b.modelPath &&
           a.materialPath == b.materialPath && a.decalProject == b.decalProject &&
           a.playerMode == b.playerMode &&
           a.playerWalkSpeed == b.playerWalkSpeed &&
           a.playerLookSpeed == b.playerLookSpeed &&
           a.playerEyeHeight == b.playerEyeHeight &&
           a.playerJumpSpeed == b.playerJumpSpeed &&
           a.playerCanJump == b.playerCanJump &&
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
           a.cameraFov == b.cameraFov &&
           a.camFeed == b.camFeed && a.camFeedTerrain == b.camFeedTerrain &&
           a.camFeedObjects == b.camFeedObjects &&
           a.textureFeed == b.textureFeed &&
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
           a.flowGraph == b.flowGraph && a.scripts == b.scripts;
}

// General project preferences (Project > Preferences in the editor).
// Baked into the generated terrain_config.hpp on every build.
struct ProjectSettings {
    // Build target. videoSystem: "auto" follows the console region, "ntsc"
    // forces 60 Hz, "pal" forces 50 Hz (gameplay speed is wall-clock
    // normalized via g_frameScale in the generated game, so both play the
    // same). The "debug" profile unlocks the on-screen FPS / free-RAM
    // overlays; "release" strips them from the build.
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
    // Debug profile only: compile the Live Link poller into the game, so the
    // editor can stream scene edits into the running game (docs/live-link.md).
    // Off = the game never reads livelink.bin and the editor never writes it -
    // for anyone who does not want their debug builds patched from outside.
    bool liveLink = true;

    // USB keyboard & mouse controls: the game loads the usbd/ps2kbd/ps2mouse
    // drivers and maps keys/mouse onto a virtual pad (bindings live in the
    // generated controls.hpp). Works in PCSX2 (the editor configures its
    // emulated USB devices before launch) and with real USB devices on a
    // console. Skipped automatically on ps2link deploys - a second usbd on
    // an IOP that may already run one (ps2link booted from a USB stick)
    // wedges the USB stack.
    bool keyboardMouse = true;

    // Experimental (debug): force the keyboard/mouse drivers on even under a
    // ps2link deploy, where they are normally skipped. Meant for the network-
    // deploy dev loop: a network-booted ps2link (SMAP/dev9) has no usbd, so
    // the engine loads its own plus ps2kbd/ps2mouse and the EE console
    // (Output / ps2client) shows the driver-load logs live. On a USB-booted
    // ps2link (usbd already resident) it may wedge the USB stack - boot from
    // that USB instead. Off by default.
    bool keyboardMousePs2Link = false;

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
    float walkSpeed = 0.4f;
    float lookSpeed = 1.0f;  // multiplier

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

    // Baked ambient occlusion (docs/ambient-occlusion.md): terrain
    // self-shadowing, contact darkening between static geometry and raycast
    // self-AO for imported .obj models - all folded into the baked vertex
    // colors, zero PS2 per-frame cost. Authored per ambience preset; these
    // fields are the no-preset fallback like the lighting above.
    bool aoEnabled = false;
    float aoStrength = 0.55f;  // 0..1, how dark full occlusion gets
    float aoRadius = 2.5f;     // world units the contact darkening reaches

    // Terrain material (.mtl asset; empty = checker greens). The first
    // material's Kd tints the terrain; its map_Kd (when present) textures it,
    // tiled by the map's "-s" scale (repeats per world unit), otherwise the
    // terrain is a flat Kd-colored surface.
    std::string terrainMaterial;

    // Post effects (GS framebuffer blits at the end of every frame; no
    // pixel shaders on the PS2). 0 = off, 1 = maximum.
    float bloom = 0.0f;  // downsample + blur + additive re-add (glow)
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
           a.showProfiler == b.showProfiler &&
           a.liveLink == b.liveLink &&
           a.keyboardMouse == b.keyboardMouse &&
           a.keyboardMousePs2Link == b.keyboardMousePs2Link &&
           a.disableVsync == b.disableVsync &&
           a.clipping == b.clipping && a.animLodDistance == b.animLodDistance &&
           a.meshLodDistance == b.meshLodDistance &&
           a.staticBatching == b.staticBatching &&
           a.envProbeReflected == b.envProbeReflected &&
           a.navCellSize == b.navCellSize && a.navMaxSlope == b.navMaxSlope &&
           a.navAgentRadius == b.navAgentRadius &&
           a.terrainDetail == b.terrainDetail &&
           a.terrainViewDistance == b.terrainViewDistance &&
           eq3(a.skyColor, b.skyColor) && eq3(a.skyTopColor, b.skyTopColor) &&
           a.skyDome == b.skyDome && a.zenithSize == b.zenithSize &&
           a.eyeHeight == b.eyeHeight &&
           a.walkSpeed == b.walkSpeed && a.lookSpeed == b.lookSpeed &&
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
           a.aoRadius == b.aoRadius &&
           a.terrainMaterial == b.terrainMaterial && a.bloom == b.bloom &&
           a.grain == b.grain && a.dofAmount == b.dofAmount &&
           a.dofFocus == b.dofFocus && a.dofRange == b.dofRange &&
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
    bool postFx = false;      // bloom, grain, depth of field
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
enum class LayoutRecipe { None = -1, Default = 0, Director = 1, Material = 2 };

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
};

inline bool operator==(const SceneLayer& a, const SceneLayer& b) {
    return a.name == b.name && a.startLoaded == b.startLoaded &&
           a.editorVisible == b.editorVisible && a.autoStream == b.autoStream &&
           a.streamX == b.streamX && a.streamZ == b.streamZ &&
           a.streamRadius == b.streamRadius;
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
    };
    int settingBind = BindNone;
};

inline bool operator==(const MenuEntry& a, const MenuEntry& b) {
    return a.label == b.label && a.action == b.action && a.param == b.param &&
           a.amount == b.amount && a.options == b.options &&
           a.optionModes == b.optionModes && a.settingBind == b.settingBind;
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

struct Project {
    std::string name;
    std::string dir;  // absolute path to project root
    // Stable, opaque project identity (16 hex chars), persisted in the .tyra
    // manifest. Distinguishes projects independently of name/path - the remote
    // collaboration cache keys downloaded projects on it. Generated at create,
    // backfilled on load for older projects (see project::ensureProjectId).
    std::string projectId;
    std::string gameTemplate = "orbit";  // "orbit" | "fpp"
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

    // Typefaces the project draws with (Tools > Font Manager). Never empty:
    // fonts[0] is the default every text falls back to, and the Font Manager
    // refuses to delete the last entry - so an empty `font` reference always
    // resolves. Replace fonts[0] to restyle the whole project at once.
    std::vector<GameFont> fonts{GameFont{}};

    std::vector<HudImage> hud;
    // The USE prompt as an overridable HUD element (see defaultUsePrompt).
    // Always present - the UI Editor edits it but cannot delete it.
    HudImage usePrompt = defaultUsePrompt();
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
    // In-game menus (Project panel, Menus): panels baked at build, opened by
    // the Open Menu flow node, menu entries, or at boot (titleScreen).
    std::vector<GameMenu> menus;
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
    // Cutscene Director sequences (Tools > Cutscene Director): project-wide
    // keyframe timelines that pose scene objects + the camera over time. Like
    // the preset collections above they persist through save() but are not part
    // of undo/redo. The Play/Stop Sequence flow nodes drive them at runtime.
    std::vector<Sequence> sequences;

    // --- Editor-side state, persisted in the .tyra project file ------------
    // Not game data and not part of undo/redo (undo lives in the history
    // file). Restores the editing session on reopen.
    int selectedObject = -1;   // selected object in the active scene (-1 none)
    int gizmoOp = 0;           // transform gizmo: 0 move, 1 rotate, 2 scale
    int gizmoSpace = 0;        // gizmo axes: 0 absolute (world), 1 camera-relative
    int viewMode = 0;          // viewport shading: 0 solid, 1 wire, 2 wire+solid
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
    std::string elfPath() const { return dir + "\\bin\\" + elfName(); }
};

namespace project {

// Creates the project directory, generates all Tyra game sources / build files
// and the <name>.tyra project file. `preset` picks the starting content:
//   "empty" - orbit camera, no objects.
//   "fpp"   - FPP game template with a single Player entity in the center.
// Returns empty string on success, error message otherwise.
std::string create(Project& out, const std::string& name, const std::string& parentDir,
                   const TerrainConfig& terrain, const std::string& preset = "empty");

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
    SaveData,        // "saveValues", "saveTexts"
    Gradings,        // "gradings", "defaultGrading"
    Ambience,        // "ambience", "defaultAmbience"
    LoadingScreens,  // "loadingScreens", "defaultLoadingScreen"
    Splash,          // "splashScreens"
    Sequences,       // "sequences"
    Menus,           // "menus"
};
constexpr int kSectionCount = 11;

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
