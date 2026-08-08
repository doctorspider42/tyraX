// The BLSS training corpus, sourced from the USER'S OWN PROJECT
// (docs/neural-upscaler.md, "Training on your project").
//
// WHY THIS EXISTS. `blsscorpus.cpp` manufactures thirteen procedural shots that
// imitate what a PS2 scene looks like, and 6a4cbead measured what the console
// actually hands the network on a real generated game: not the same thing. The
// engine side of that gap is fixed; this file closes the other half by walking
// the project the user is going to ship and rendering IT.
//
// The split with blsscorpus.cpp is deliberate and it is the one that file was
// structured for: this module knows about `Project`, `SceneObject`, .obj files
// and heightmaps and knows NOTHING about rasterising, jitter, ground truth or
// bag proxies. It answers one question - "what triangles, what materials, and
// which cameras" - and hands back a form the corpus can draw. Everything
// downstream (the rasteriser, bagOf(), accumulate(), buildFeatures()) is the
// code the procedural corpus already runs, unchanged, so a project-trained net
// and a procedurally-trained net are measured by exactly the same instrument.
//
// Host-only: no GL, no ImGui (the aobake/gibake pattern), so --blss-train
// <projectDir> runs headless. Deterministic: no RNG at all here, and the object
// walk is in authored order.
//
// TESSELLATION IS BORROWED, NOT REINVENTED. Primitives come from primmesh (the
// same source the viewport draws and gibake bounces), models from objparser,
// the terrain from the scene heightmap - the same three sources gibake::build()
// uses, in the same transform order. What this adds over gibake's Scene is the
// two things a rasteriser needs and a light bake does not: UVs and per-mesh
// material identity.

#pragma once

#include <string>
#include <vector>

namespace blss {

// One vertex of a project mesh, in WORLD space with the scene's directional
// light already baked into `c` - which is what the PS2 pipeline does too
// (templates.cpp bakes the directional term per vertex), so the corpus'
// rasteriser only ever modulates a texel by an interpolated vertex colour.
struct SceneVert {
    float p[3] = {0, 0, 0};
    float u = 0, v = 0;
    float c[3] = {1, 1, 1};
};

// One submitted draw of the project's scene: the unit a StaPipBag would be.
struct SceneMesh {
    std::vector<SceneVert> vert;
    std::vector<int> idx;      // 3 per triangle
    std::string texture;       // ABSOLUTE path to a PNG, "" = untextured
    // ...or an index into ProjectScene::embedded when the texture lives INSIDE
    // a .glb and has no path on the host (>= 0 wins over `texture`). The build
    // writes those images out next to the ELF, so the console is textured; a
    // corpus that called the same part untextured would report texDetail = 0
    // for a bag whose console twin reports a real minification ratio.
    int embeddedTex = -1;
    float tint[3] = {1, 1, 1}; // object colour x material Kd, already in `c`
    bool cutout = false;       // alpha-tested (decals, foliage sheets)
    bool bilinear = true;
    // False for a shell centred on the camera - the sky dome and its company.
    // The twin of PipelineInfoBag::blssProxy: such a bag is drawn (it is most
    // of what the frame looks like) but it describes nothing, so it must not
    // become a BagProxy. Keeping the two rules in one concept is what stops
    // the corpus from training on a frame the console describes differently.
    bool proxy = true;
    // Not submitted at all past this distance from the camera - the object's
    // `drawDistance`, or the terrain's streaming view distance. 0 = always.
    // A bag the console never submits is neither drawn nor described here, so
    // the ground truth and the proxy list agree with each other and with the
    // frame the game would produce.
    //
    // ONE APPROXIMATION, and it is the only one in this file: the generated
    // terrain streams by an XZ RECTANGLE of chunks around the focus while an
    // object cuts off on a SPHERE (templates.cpp). Both are applied here as
    // the sphere, so a corner chunk of the streaming rect may be described one
    // frame later than the console would describe it.
    float viewDist = 0.0f;
    float centre[3] = {0, 0, 0};  // AABB centre, filled by loadProject()
};

// One camera move over the scene, as a polyline of (eye, look-at) keys. Every
// kind of move this module produces - an orbit, a walk from the player start, a
// whip pan, an authored cutscene shot - reduces to this, because the corpus
// only ever asks for "the camera at t" and a polyline answers that for all of
// them without a second parameterisation to keep in step.
struct SceneShot {
    std::string name;   // "vale walk", "cavern orbit", ...
    std::string move;   // "dolly", "pan", "orbit", "whip", "static", "take"
    std::vector<float> eye;   // 3 per key, >= 1 key
    std::vector<float> look;  // 3 per key, same count
    // 0 = constant speed in t; 1 = smoothstep, so the angular velocity PEAKS
    // mid-shot. The whip wants the second: a linear sweep gives every frame the
    // same hopeless reprojection offset, while the ease produces frames where
    // history is fine, frames where it is useless, and the two transitions.
    float ease = 0.0f;
    float fovDeg = 60.0f;
    int keys() const { return static_cast<int>(eye.size() / 3); }
};

// ONE ANIMATED MODEL PART, POSED PER CONSOLE FRAME - and it is here because the
// corpus was fitting a distribution the console does not run.
//
// This file used to SKIP animated models outright, on the stated grounds that
// "animated .glb goes down the dynamic pipeline, which does not feed BLSS at
// all". That was true of the engine this fork started from and is not true of
// this one: `updateAndRenderAnimObjects` skins on the EE and then submits the
// skinned arrays through `stapip.core.render()` like any other static bag
// (templates.cpp), `PipelineInfoBag::blssProxy` defaults to true, and
// `StaPipCore` therefore hands RendererCoreBlss one box per VU1 package for
// every animated part on screen. So on the console a spider is drawn AND
// described; in the corpus it was neither. Same class of bug as the whole-bag
// proxy, and the same instrument found it: compare what the frame contains.
//
// `pose[f]` is the mesh as it stands at console frame f of the shot, with the
// object's clip, its `animSpeed`, its loop flag and its autoplay flag already
// folded in, so the consumer only ever indexes by frame number. Triangles,
// material and vertex count are identical across poses - only positions,
// normals (hence the baked vertex colour) and the bounding boxes move.
struct AnimMesh {
    std::vector<SceneMesh> pose;  // >= 1
};

// How many console frames of pose are baked. The corpus splits its frame budget
// over the shots, so a shot is a dozen-odd frames; 48 is generous enough that
// the pose never has to be held past the end and small enough that a 2 000-vertex
// character costs ~3 MB. Frames past it clamp to the last pose.
constexpr int kAnimPoses = 48;

// One scene of the project, drawn and shot.
struct ProjectScene {
    std::string name;
    std::vector<SceneMesh> mesh;
    // One entry per animated model PART on screen (a two-material character is
    // two, exactly as it is two bags on the console).
    std::vector<AnimMesh> anim;
    // PNG blobs lifted out of .glb files, referenced by SceneMesh::embeddedTex.
    // Stored once per scene rather than per pose: a pose list is 48 copies of
    // the same mesh and would have been 48 copies of the texture with it.
    std::vector<std::vector<unsigned char>> embedded;
    std::vector<SceneShot> shot;
    float bmin[3] = {0, 0, 0}, bmax[3] = {0, 0, 0};
    size_t triangles() const {
        size_t n = 0;
        for (const SceneMesh& m : mesh) n += m.idx.size() / 3;
        for (const AnimMesh& a : anim)
            if (!a.pose.empty()) n += a.pose[0].idx.size() / 3;
        return n;
    }
};

// How many camera moves each scene contributes at most. The corpus splits its
// frame budget evenly over shots and leave-one-shot-out cross-validation runs
// one fold per shot, so this is a cost knob in both directions: too many shots
// and every fold is three frames deep, too few and the net sees one viewpoint.
constexpr int kShotsPerScene = 6;

// Walks `projectDir` and returns one entry per scene that produced geometry.
// Empty (with `err` set) when the directory is not a project; empty with no
// `err` when the project loads but has nothing to draw - which is the case the
// caller must fall back to the procedural bestiary for.
// WHAT THE PROJECT SAYS ABOUT BLSS, so the corpus can fit the build that will
// actually ship rather than a default. Only the settings that change what a
// FRAME looks like belong here - a net fitted against a sampler the generated
// game does not use is fitted out of distribution, which is the mistake this
// whole file exists to stop making.
struct ProjectBlss {
    bool found = false;   // false: not a loadable project, so nothing was read
    bool jitter = true;   // ProjectSettings::blssJitter (format v5)
};

// `animated` false leaves the animated models out entirely - no pose table, no
// pixels, no proxies - which is how this corpus behaved for its whole life
// before AnimMesh existed. It is `--no-package-split`'s sibling and exists for
// the same one reason: to reproduce a fold table measured before the change,
// not to ship.
std::vector<ProjectScene> loadProject(const std::string& projectDir,
                                      std::string* err, bool verbose,
                                      bool animated = true,
                                      ProjectBlss* blss = nullptr);

}  // namespace blss
