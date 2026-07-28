#pragma once

#include <array>
#include <cmath>
#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "aobake.hpp"
#include "gibake.hpp"
#include "glbparser.hpp"
#include "navmesh.hpp"
#include "procgen.hpp"
#include "project.hpp"

// 3D preview of the project terrain and scene objects, rendered into an
// offscreen texture shown inside an ImGui window. Orbit camera (drag+scroll).
class Viewport {
public:
    enum class ViewMode {
        Solid = 0,
        Wireframe = 1,       // colored wireframe only
        SolidWireframe = 2,  // solid shading with a dark wireframe overlay
    };

    void setViewMode(ViewMode m) { viewMode_ = m; }
    ViewMode viewMode() const { return viewMode_; }

    // Camera projection (docs/orthographic-views.md). Perspective is the
    // classic free orbit camera; the ortho modes render with a parallel
    // projection (no foreshortening, so equal sizes read equal anywhere on
    // screen) and the six axis modes additionally lock the camera onto that
    // world axis - the Top/Front/Side views of a CAD or level editor.
    // Orbiting out of an axis view keeps the parallel projection and falls
    // back to Ortho (free), so a drag never feels dead.
    enum class Projection {
        Perspective = 0,
        Ortho = 1,        // parallel, free orbit direction
        OrthoTop = 2,     // looks down -Y (+X right, +Z down)
        OrthoBottom = 3,  // looks up +Y
        OrthoFront = 4,   // looks along -Z (+X right, +Y up)
        OrthoBack = 5,    // looks along +Z
        OrthoRight = 6,   // looks along -X (from +X)
        OrthoLeft = 7,    // looks along +X (from -X)
    };
    static constexpr int kProjectionCount = 8;
    void setProjection(Projection p) { projection_ = p; }
    Projection projection() const { return projection_; }
    bool orthographic() const { return projection_ != Projection::Perspective; }
    // Display name of a projection mode ("Perspective", "Top", ...).
    static const char* projectionName(Projection p);

    bool init();  // requires a current GL context
    void shutdown();

    // (Re)builds the terrain mesh from project settings.
    // maxCells: terrain grid detail cap; heights: sculpted heightmap
    // (hmW x hmD vertex grid, empty = flat).
    void setTerrain(const TerrainConfig& terrain, int maxCells = 32,
                    const std::vector<float>& heights = {}, int hmW = 0, int hmD = 0);

    // Sculpt fast path: takes the freshly brushed heightmap (same dims as the
    // last setTerrain) and rebuilds only the terrain chunks under the brush
    // circle - a stroke on a large map stays interactive.
    void updateTerrainRegion(const std::vector<float>& heights, float worldX,
                             float worldZ, float radius);

    // Casts a ray through normalized image coords onto the terrain surface.
    // Returns false when the ray misses; used by the sculpting brush.
    bool terrainRaycast(float u, float v, float& outX, float& outZ) const;

    // Casts a ray through normalized image coords at the whole scene (object
    // boxes + the terrain heightfield) and returns the closest hit point in
    // world space. `skip` (optional, parallel to objects) excludes indices
    // from the test - the objects being placed must not catch their own ray.
    // Objects on hidden layers are excluded like they are for picking.
    // Returns false when the ray leaves the scene without hitting anything.
    bool placementRaycast(float u, float v, const std::vector<SceneObject>& objects,
                          const std::vector<char>& skip, float outPoint[3]) const;

    // Inverse of camRay: a world point -> normalized image coords (u, v in
    // [0,1], origin top-left) of the LAST rendered frame. False when the point
    // is behind a perspective camera. App-side ImDrawList overlays that have
    // to sit on world geometry (the measuring tape) place themselves with
    // this, so they agree with the image under every projection instead of
    // rebuilding a camera of their own.
    bool projectToImage(const float world[3], float& outU, float& outV) const;

    // Local-space AABB of what an object DRAWS as a model: static .obj bounds
    // (GL-free, cached) or an animated model's baked pose bounds. False for
    // non-model objects and unreadable files. The aobake::ModelAabbFn the app
    // hands to the placement snapping and the occluder collection.
    bool modelLocalBounds(const SceneObject& o, float mn[3], float mx[3]);

    float terrainHeight(float x, float z) const;  // bilinear, 0 when flat

    // The camera ray through normalized image coords - the same one pick() and
    // terrainRaycast() build. Exposed so the app can hit-test things the
    // viewport does not own (procedural scatter instances live in the graph,
    // not in the scene object list).
    void cameraRay(float u, float v, float outOrigin[3], float outDir[3]) const;

    // horizon + zenith colors; gradient=false renders a flat horizon color
    void setSky(const float* horizonRgb, const float* topRgb, bool gradient,
                float zenithSize = 0.5f);

    // directional light baked into mesh shading (matches the PS2 output)
    void setLighting(const float* dir, float ambient, float diffuse, const float* color,
                     float brightness);

    // Baked global illumination preview (docs/global-illumination.md). The
    // editor must show the same light the console will, or authoring a bake is
    // guesswork - so the scene's probe grid is uploaded as a 3D texture and the
    // fragment shader evaluates it per pixel, REPLACING the ambient +
    // directional shade exactly the way the generated game replaces it per
    // vertex. An empty grid (no bake, or a stale one) restores the classic
    // preview with one call.
    //
    // Where this preview is honest and where it is not: the game gives static
    // untextured geometry a per-TEXEL lightmap, whose contact shadows are
    // sharper than a 3-unit probe grid can be. Everything else - models,
    // textured surfaces, physics bodies, characters - is probe-lit in the game
    // too, and there this is exact.
    void setGiProbes(const gibake::ProbeGrid& g);
    void clearGiProbes() { setGiProbes(gibake::ProbeGrid()); }
    bool giProbesLoaded() const { return giDim_[0] > 0; }

    // The TERRAIN takes GI from the baked terrain lightmap, not from the probe
    // grid - the same split the generated game makes (buildTerrainChunk's
    // `terrainGi`). One probe sample every ~3 units over a broadly flat ground
    // is nearly constant, so the probe route paints the whole terrain one
    // colour; the map is per texel and is what the console actually reads.
    // A TEXTURED terrain has no lit map (a flat additive term would blow out
    // its dark texels) and stays on the probe route in both.
    void setGiTerrain(const aobake::AoImage& img);
    void clearGiTerrain() { setGiTerrain(aobake::AoImage()); }

    // Baked ambient occlusion preview (docs/ambient-occlusion.md): terrain
    // self-occlusion is multiplied into the terrain vertex colors (the same
    // aobake::terrainAO grid the build ships), model self-AO into the model
    // vertex colors, and the analytic occluder + ground contact terms run
    // live in the fragment shader (the GL twin of the generated game's
    // per-vertex bake). Rebuilds terrain/models when the values change.
    void setAmbientOcclusion(bool enabled, float strength, float radius);

    // GS hardware distance fog preview (Preferences > Distance fog); geometry
    // blends toward rgb between the start/end view distances, sky excluded -
    // same as the generated game.
    void setFog(bool enabled, const float* rgb, float start, float end);

    // Camera flashlight preview (Preferences > Flashlight): additive cone
    // from the editor camera, the exact formula the PS2 runs on VU1.
    void setFlashlight(bool enabled, const float* rgb, float range,
                       float halfAngleDeg);

    // project root for resolving relative model paths (clears the model cache)
    void setProjectDir(const std::string& dir);

    // Terrain material, pre-resolved by the caller: the first material's
    // map_Kd texture (empty = flat color), its Kd tint, whether a material is
    // assigned (false = checker greens), and the map's "-s" tiling (texture
    // repeats per world unit, per axis u/v).
    void setTerrainMaterial(const std::string& texRelPath, const float kd[3],
                            bool hasMaterial, const float tile[2]);

    // Terrain splat painting preview (docs/terrain-painting.md): the same
    // two-pass blending the PS2 does - the base terrain draws as usual, then
    // every painted layer alpha-blends over it as a second pass of the same
    // grid (tiled layer texture, Gouraud vertex alpha = the painted weight).
    struct TerrainLayerDraw {
        std::string texture;  // res-relative map_Kd ("" = flat color)
        float kd[3] = {0.6f, 0.6f, 0.6f};
        float tile[2] = {1.0f, 1.0f};  // repeats per world unit (incl. Size)
    };
    // weights: hmW*hmD*layers bytes, layer-interleaved per vertex (the
    // project's SceneData::splat). Rebuilds the terrain meshes.
    void setTerrainLayers(const std::vector<TerrainLayerDraw>& layers,
                          const std::vector<uint8_t>& weights);
    // Cheap during-a-stroke update: new weights, rebuild only the chunks under
    // the brush (the paint twin of updateTerrainRegion).
    void updateSplatRegion(const std::vector<uint8_t>& weights, float worldX,
                           float worldZ, float radius);

    // Macro ground variation (docs/terrain-painting.md): world-noise tint
    // multiplied into the terrain vertex shade (base + layer passes). Rebuilds
    // the terrain when the values change. variation 0 = off.
    void setTerrainTint(float variation, float scaleWorld);

    // "Highlight usable objects" preference: marks usable objects with a wire
    // box in the highlight color (proximity is a game-runtime condition)
    void setUsableHighlight(bool enabled, const float* rgb);

    // Color grading preview: replicates the PS2 GS grading pass (the same
    // quantized integers, incl. the 0..255 clamp after every blend step) as
    // a full-screen post pass over the rendered frame.
    void setGrading(bool enabled, const CompiledGrading& g);

    // Editor-only layer visibility: indices flagged in the mask (parallel to
    // the objects vector) are skipped by render() and pick(). The app
    // rebuilds the mask each frame from the scene's layer eye toggles.
    void setHiddenMask(std::vector<char> mask) { hiddenMask_ = std::move(mask); }

    // Nav-mesh overlay (View > Nav Mesh Overlay): translucent green quads
    // over the walkable cells of the app-baked grid (navmesh::bake - the app
    // owns the Project). The GL mesh is rebuilt only when `version` changes,
    // so this can be called every frame cheaply; pass nullptr to hide.
    void setNavOverlay(const navmesh::NavGrid* grid, uint64_t version);

    // Procedural scatter preview (docs/procedural-generation.md). The app
    // evaluates every Scatter volume's graph (procgen, off the UI thread) and
    // pushes the result in once per frame; the viewport draws the instances
    // with the ordinary model path, so the preview is shaded exactly like the
    // baked chunks that ship. `version` gates the overlay-mesh rebuilds (mask
    // grid, curve polyline) - the instance list itself is cheap to re-walk.
    //
    // The baked chunk objects (SceneObject::procSource) are NOT drawn while a
    // preview exists: they are build output of the same deterministic
    // evaluation, and drawing both would double every tree.
    struct ScatterPreview {
        uint64_t version = 0;
        std::vector<std::string> assets;  // index = Instance::asset
        std::vector<procgen::Instance> instances;
        // An isolated node's own output, shown instead of instances: a mask
        // draped over the terrain, or a curve as a polyline (UX-01).
        std::shared_ptr<const procgen::Mask> mask;
        std::shared_ptr<const procgen::Curve> curve;
        // Control points of the curve node being edited (world XYZ triples),
        // drawn as grabbable handles; -1 = none highlighted.
        std::vector<float> handles;
        int activeHandle = -1;
        // Instances above this are drawn as plain points instead of meshes -
        // one GL draw per instance is fine for thousands, not for tens of
        // thousands, and the author still needs to see the layout.
        int proxyAbove = 4000;
    };
    void setScatterPreview(ScatterPreview p);

    // Projected-decal preview meshes, computed app-side (decalproj) because the
    // app owns the Project. Keyed by object id; each value is a world-space
    // triangle list, 5 floats/vertex (pos3 + uv2). The GL meshes are rebuilt
    // only when `version` changes (bumped whenever the app recomputes), so this
    // can be called every frame cheaply. A projecting decal with an entry here
    // draws it instead of the flat quad.
    void setProjectedDecals(const std::map<std::string, std::vector<float>>& meshes,
                            uint64_t version);

    // Renders terrain + objects at the given pixel size, returns GL texture id.
    // selection: indices outlined; primary (the anchor, usually selection.back())
    // is outlined brighter so it reads as the value source for the multi-editor.
    uint32_t render(int width, int height, const std::vector<SceneObject>& objects,
                    const std::vector<int>& selection, int primary);

    // Phone camera link (docs/phone-camera.md): reads the image the LAST
    // render() produced back into packed RGB for JPEG streaming to the
    // companion app - so the phone sees exactly the frame the editor viewport
    // shows, grading included, without a second scene pass. The image is
    // downscaled on the GPU (a blit into a small dedicated target, which is
    // also all that gets read back, so a 1600x900 viewport does not stall the
    // frame) and row-flipped into top-down order on the way out.
    // maxW/maxH cap the long edges; the source aspect is preserved. False when
    // nothing has been rendered yet.
    bool grabPreviewRgb(int maxW, int maxH, std::vector<unsigned char>& outRgb,
                        int& outW, int& outH);

    // Collaboration presence: other participants' selections in the ACTIVE
    // scene, outlined in each peer's color under the local selection (local
    // amber always reads on top). The app resolves object ids to indices per
    // frame; empty vector = no session / nothing selected remotely.
    struct PeerSel {
        float color[3] = {1.0f, 1.0f, 1.0f};
        std::vector<int> indices;
    };
    void setPeerSelections(std::vector<PeerSel> sels) { peerSels_ = std::move(sels); }

    // Material Editor live preview: a lit primitive OR one of the project's
    // .obj models over a checker floor, rendered into its own framebuffer
    // (render() resizes the main one to the viewport every frame).
    // Optional lighting override for the tool-window previews (Material /
    // Animation Editor). A scene authored dark - low brightness, a night
    // ambience - makes its previews unreadable too, because the previews shade
    // with the scene's baked light on purpose (what you see is what ships).
    // This lets a preview bake with different values instead: the panels offer
    // the neutral studio default and every ambience preset in the project.
    // It is a real re-bake of the preview's own vertex colors, not a post-hoc
    // brightness scale, so the shading stays exactly what those light values
    // would produce in-game.
    struct PreviewLight {
        bool on = false;  // false = follow the scene's resolved ambience
        float dir[3] = {0.37f, 0.82f, 0.44f};
        float ambient = 0.55f;
        float diffuse = 0.45f;
        float color[3] = {1.0f, 1.0f, 1.0f};
        float brightness = 1.0f;
    };

    struct MatPreviewDesc {
        float kd[3] = {1.0f, 1.0f, 1.0f};  // staged tint of the selected entry
                                           // (channels may exceed 1 - brightness)
        float ke[3] = {0.0f, 0.0f, 0.0f};  // staged Ke emission floor (0 = matte)
        std::string texRel;   // staged map_Kd, project-relative ("" = none)
        std::string reflRel;  // staged refl sphere map, project-relative
        float reflStrength = 0.0f;  // staged reflection strength (0 = matte)
        bool reflSky = false;       // staged "@sky" dynamic mode
        bool reflRounded = false;   // staged "-rounded" env normals
        int shape = 1;        // 0 box, 1 sphere, 2 cylinder, 3 cone, 4 model
        std::string modelRel; // .obj shown when shape == 4 (project-relative)
        std::string mtlRel;   // the open .mtl: override library for the model
        std::string entryName;  // selected entry - its model parts are drawn
                                // with the staged kd/texRel (live edits)
        float angleDeg = 40.0f;   // turntable yaw
        float pitchDeg = 30.0f;   // camera elevation
        float zoom = 1.0f;        // dolly multiplier (1 = default framing)
        int displayMode = 0;      // 0 solid, 1 solid + wireframe overlay,
                                  // 2 UV checker (replaces every texture)
        PreviewLight light;       // off = the scene's ambience
    };
    uint32_t renderMaterialPreview(int width, int height, const MatPreviewDesc& d);

    // Tree Generator live preview (Tools > Tree Generator): generated
    // geometry + in-memory textures straight from treegen - nothing touches
    // disk or the shared asset caches, so slider drags stay instant. Renders
    // into its OWN framebuffer (only the gradient/checker backdrop meshes are
    // shared); meshes and textures re-upload only when `version` changes.
    struct TreePreviewDesc {
        uint64_t version = 0;
        const std::vector<float>* bark = nullptr;    // pos3+normal3+uv2 tris
        const std::vector<float>* leaves = nullptr;  // same layout, drawn
                                                     // with alpha cutout
        const unsigned char* barkRgba = nullptr;     // RGBA texture pixels
        int barkW = 0, barkH = 0;
        const unsigned char* leafRgba = nullptr;
        int leafW = 0, leafH = 0;
        float center[3] = {0, 0, 0};  // mesh AABB center (camera pivot)
        float minY = 0.0f;            // AABB bottom (floor placement)
        float radius = 1.0f;          // AABB half-diagonal (framing)
        float angleDeg = 40.0f;       // turntable yaw
        float pitchDeg = 18.0f;       // camera elevation
        float zoom = 1.0f;
        int displayMode = 0;  // 0 solid, 1 solid + wireframe overlay
    };
    uint32_t renderTreePreview(int width, int height, const TreePreviewDesc& d);
    // Non-destructive animation-clip edits (Tools > Animation Editor). The app
    // owns the Project, so it pushes the list plus the project's fps ratio in
    // once per frame - the same pattern the nav overlay and projected decals
    // use. Both the scene preview and the Animation Editor preview apply them.
    void setAnimEdits(std::vector<AnimClipEdit> edits, float projectScale) {
        animEdits_ = std::move(edits);
        animProjectScale_ = projectScale > 0.001f ? projectScale : 1.0f;
    }

    // Animation Editor live preview: one animated model on a checker floor,
    // posed at an explicit time so the panel owns play/pause/scrub. Times are
    // SOURCE seconds (see animedit.hpp); the trim window is applied here, so
    // dragging a trim handle moves the preview immediately - before anything
    // is committed to the project.
    struct AnimPreviewDesc {
        std::string modelRel;     // project-relative .glb/.fbx
        std::string materialRel;  // .mtl override ("" = the model's own)
        std::string clip;         // SOURCE clip name ("" = the first clip)
        float time = 0.0f;        // seconds into the TRIMMED clip
        float trimStart = 0.0f;   // source seconds
        float trimEnd = 0.0f;     // source seconds, 0 = to the end
        float angleDeg = 40.0f;   // turntable yaw
        float pitchDeg = 15.0f;   // camera elevation
        float zoom = 1.0f;        // dolly multiplier
        bool wireframe = false;   // overlay the triangles
        PreviewLight light;       // off = the scene's ambience
    };
    uint32_t renderAnimPreview(int width, int height, const AnimPreviewDesc& d);

    // Asset Browser thumbnails (docs/asset-browser.md): one square preview of
    // an asset file, rendered ONCE into a dedicated framebuffer and copied into
    // its own small GL texture - a grid of hundreds then costs nothing to draw.
    // Handles static .obj, animated .glb/.fbx (first pose) and .mtl libraries
    // (a sphere wearing the first material); an image file needs no render and
    // returns the shared texture. `render` false only reports what is already
    // baked (0 = nothing yet), which is how the browser budgets the number of
    // new thumbnails a single frame may pay for. Failed assets are remembered
    // as 0 so an unreadable file is not retried every frame.
    uint32_t assetThumb(const std::string& relPath, bool render);
    static constexpr int kAssetThumbSize = 128;  // baked size; ImGui scales it

    // Source clip names of an animated model, in file order. Empty when the
    // file is missing or unusable. (The Animation Editor's clip list.)
    std::vector<std::string> animClipNames(const std::string& modelRel,
                                           const std::string& materialRel);
    // Duration in SOURCE seconds of one clip; 0 when unknown. Derived from
    // the preview bake, so it matches the frames the preview steps through.
    float animClipDuration(const std::string& modelRel,
                           const std::string& materialRel,
                           const std::string& clip);

    // Raycast of the LAST renderMaterialPreview frame: image coords (u, v in
    // [0,1], origin top-left) -> the hit surface's texture UV. paintable is
    // true when the hit face is drawn with the staged texture (the selected
    // entry's parts on a model; always for primitive shapes). outMaterial,
    // when given, receives the hit part's material name ("" for primitive
    // shapes) - the editor's click-a-part-to-select-its-entry hook.
    bool materialPreviewPick(float u, float v, float& outU, float& outV,
                             bool& paintable,
                             std::string* outMaterial = nullptr) const;

    // Inverse of the pick: model-space point -> image coords of the LAST
    // renderMaterialPreview frame (u, v in [0,1], origin top-left). False
    // when the point is behind the camera. The UV-panel hover sync draws
    // triangle outlines over the preview image with this.
    bool materialPreviewProject(const float world[3], float& outU,
                                float& outV) const;

    // The shared GL texture of a project-relative path (loads on first use;
    // live paint updates included - it is the same cache updateTexturePixels
    // writes). 0 when unreadable. The UV-layout panel underlay.
    uint32_t sharedTexture(const std::string& relPath) { return glTexture(relPath); }

    // Replaces the pixels of the cached GL texture for a project-relative
    // path (creating the cache entry when absent) - live texture painting.
    // Every mesh sampling that path updates immediately (the GL texture is
    // shared); the file on disk is NOT touched.
    void updateTexturePixels(const std::string& relPath, int w, int h,
                             const unsigned char* rgba);

    // Drops every disk-derived cache (models, materials, GL textures). Call
    // after an asset file changed on disk (e.g. the Material Editor saved a
    // .mtl) so the next frame re-reads it.
    void invalidateAssets();

    // Camera controls, driven by the UI layer. The camera orbits a movable
    // target point: pan slides it in the view plane (middle mouse drag),
    // fly moves it on the horizontal plane along the view direction (WASD).
    void orbit(float dxPixels, float dyPixels);
    void zoom(float wheel);
    void pan(float dxPixels, float dyPixels);
    void fly(float forward, float strafe, float dt);

    // Snap the orbit pivot to a world-space point (e.g. the selected object),
    // keeping the current yaw/pitch/distance. Used by "orbit around selection".
    void setTarget(const float target[3]);

    // Cutscene Director camera-track preview: override the orbit camera with an
    // explicit eye + look-at target + vertical FOV (degrees) for the next
    // render() calls; clearCameraOverride() restores the orbit camera.
    // `rollDeg` rotates the view about its own axis (the Dutch angle) - the
    // basis comes from seqCameraUp, the same function the generated PS2 player
    // and the camera-take bake use, so the preview cannot tilt differently from
    // the console.
    void setCameraOverride(const float eye[3], const float target[3], float fovDeg,
                           float rollDeg = 0.0f) {
        camOverride_ = true;
        for (int i = 0; i < 3; ++i) camEye_[i] = eye[i], camTarget_[i] = target[i];
        camFov_ = fovDeg;
        camRoll_ = rollDeg;
    }
    void clearCameraOverride() { camOverride_ = false; }

    // Camera entities to skip rendering (body + FOV frustum): the camera(s)
    // the viewport is currently previewing THROUGH would otherwise sit on the
    // near plane and cover the whole view. Names, matched against SceneObject.
    void setHiddenCameras(std::vector<std::string> names) {
        hiddenCams_ = std::move(names);
    }

    // Current orbit-camera eye + look-at target (world space). Snapshotted into
    // a Cutscene Director camera keyframe ("Set camera key from view").
    void currentCamera(float eye[3], float target[3]) const {
        eye[0] = target_[0] + distance_ * std::cos(pitch_) * std::cos(yaw_);
        eye[1] = target_[1] + distance_ * std::sin(pitch_);
        eye[2] = target_[2] + distance_ * std::cos(pitch_) * std::sin(yaw_);
        target[0] = target_[0];
        target[1] = target_[1];
        target[2] = target_[2];
    }

    // Recenter the camera on the terrain center (world origin) and restore the
    // default orientation and a distance framing the whole terrain.
    void resetView();

    // View/projection of the last render() call (column-major, OpenGL style) -
    // used by the transform gizmo. The projection is in PANEL space: in the
    // PS2 output mode it carries the letterbox scale, so the gizmo lands on
    // the picture and not on the bars beside it.
    const float* viewMatrix() const { return viewM_; }
    const float* projMatrix() const { return projM_; }

    // PS2 output emulation (docs/ps2-viewport.md): render the scene at the GS
    // framebuffer size of the project's display mode and present it - nearest,
    // letterboxed into the TV's display window - instead of drawing the scene
    // at whatever shape the viewport happens to be docked to. Geometry comes
    // from the app (the twin of Tyra's RendererSettings::updateGeometry); the
    // viewport only consumes it, so a new engine display mode is one table
    // entry there and nothing here.
    struct Ps2Output {
        bool on = false;
        int bufW = 512;   // physical GS framebuffer, in GS pixels
        int bufH = 448;   // half the logical height when field rendering
        float projAspect = 512.0f / 448.0f;  // RendererSettings::aspectRatio
        float tvAspect = 4.0f / 3.0f;        // shape of the display window
        // The GS flicker filter (two read circuits, the second offset by one
        // line): on for the Interlaced and Pal576i scan-outs, off for the DTV
        // modes and field rendering - RendererCoreGS::presentFrameBuffer.
        bool flicker = true;
    };
    void setPs2Output(const Ps2Output& o) { ps2_ = o; }
    const Ps2Output& ps2Output() const { return ps2_; }

    // Returns the index of the frontmost object under the given normalized
    // image coordinates (u, v in [0,1], origin top-left), or -1.
    int pick(float u, float v, const std::vector<SceneObject>& objects) const;

private:
    struct Mesh {
        uint32_t vao = 0, vbo = 0;
        int vertexCount = 0;
    };

    bool hiddenAt(size_t i) const {
        return i < hiddenMask_.size() && hiddenMask_[i] != 0;
    }
    std::vector<char> hiddenMask_;

    void ensureFramebuffer(int width, int height);
    void buildTerrainMesh();
    void buildPrimitiveMeshes();
    Mesh uploadMesh(const std::vector<float>& interleaved);  // pos3 + color3
    // pos3 + color4 + uv2 (the particle-shader layout) - terrain layer passes
    Mesh uploadMesh9(const std::vector<float>& interleaved);
    void destroyMesh(Mesh& m);

    TerrainConfig terrain_;
    int maxCells_ = 32;
    std::vector<float> heights_;
    int hmW_ = 0, hmD_ = 0;

    // Nav-mesh overlay mesh (see setNavOverlay)
    bool navOverlayOn_ = false;
    uint64_t navOverlayVersion_ = 0;
    bool navOverlayHasVersion_ = false;
    Mesh navOverlayMesh_;

    // Scatter preview (see setScatterPreview): the pushed result plus the GL
    // meshes for its mask / curve overlays, rebuilt only when version changes.
    ScatterPreview scatter_;
    uint64_t scatterVersion_ = 0;
    bool scatterHasVersion_ = false;
    Mesh scatterMaskMesh_;
    Mesh scatterCurveMesh_;
    Mesh scatterPointsMesh_;  // proxy dots for very large instance counts

    // Projected-decal GL meshes (see setProjectedDecals), keyed by object id;
    // rebuilt only when projectedDecalVersion_ changes.
    std::map<std::string, Mesh> projectedDecalMeshes_;
    uint64_t projectedDecalVersion_ = 0;
    bool projectedDecalHasVersion_ = false;
    float sky_[3] = {0.25f, 0.55f, 0.78f};
    float skyTop_[3] = {0.08f, 0.3f, 0.65f};
    bool skyGradient_ = true;
    float skyZenithSize_ = 0.5f;  // gradient bias, see setSky / the dome build
    Mesh skyQuad_;
    bool skyQuadDirty_ = true;
    ViewMode viewMode_ = ViewMode::Solid;

    // camera (orbit around a movable target, initially the terrain center)
    float yaw_ = 0.8f;
    float pitch_ = 0.6f;
    float distance_ = 90.0f;
    float target_[3] = {0.0f, 0.0f, 0.0f};
    Projection projection_ = Projection::Perspective;
    // The camera a render() draws with: eye + orthonormal basis plus the
    // projection extents. ONE source for render(), pick(), the terrain
    // raycast and the placement raycast - those used to rebuild the same
    // hardcoded 50-degree perspective ray by hand, so they disagreed with the
    // image as soon as the projection was ortho or a Camera entity's FOV.
    struct CamView {
        float eye[3] = {0.0f, 0.0f, 0.0f};
        float fwd[3] = {0.0f, 0.0f, -1.0f};
        float right[3] = {1.0f, 0.0f, 0.0f};
        float up[3] = {0.0f, 1.0f, 0.0f};
        bool ortho = false;
        float tanHalf = 0.4663f;  // perspective: tan(vertical fov / 2)
        float halfH = 1.0f;       // ortho: half the visible height (world units)
        float aspect = 1.0f;
        // Letterbox: the fraction of the panel the picture covers, per axis
        // (1,1 outside the PS2 output mode). Image coords passed to camRay and
        // returned by projectToImage are PANEL coords, so both go through it -
        // that is what keeps picking, the placement raycast and the measuring
        // tape on the picture when it no longer fills the viewport.
        float boxSx = 1.0f, boxSy = 1.0f;
    };
    CamView camView(int width, int height) const;
    // Ray through normalized image coords (u, v in [0,1], origin top-left).
    // The ortho ray starts a full scene depth behind the eye plane so nothing
    // visible can sit behind the ray origin (a parallel projection draws what
    // is behind the camera too - see the symmetric depth range).
    void camRay(const CamView& c, float u, float v, float o[3], float d[3]) const;
    // Half the depth range the projection spans - far plane / ortho z extent.
    float sceneDepth() const;

    // Cutscene camera-track preview override (see setCameraOverride)
    bool camOverride_ = false;
    float camEye_[3] = {0.0f, 0.0f, 0.0f};
    float camTarget_[3] = {0.0f, 0.0f, 0.0f};
    float camFov_ = 50.0f;
    float camRoll_ = 0.0f;  // Dutch angle of the override camera, degrees
    // Camera entities not to draw (previewing through them) - see setHiddenCameras
    std::vector<std::string> hiddenCams_;
    bool camHidden(const std::string& name) const {
        for (const std::string& n : hiddenCams_)
            if (n == name) return true;
        return false;
    }

    uint32_t program_ = 0;
    int uMvp_ = -1;
    int uTint_ = -1;
    int uUseTex_ = -1;
    int uAlpha_ = -1;  // decal cutout/blend toggle
    int uOpacity_ = -1;  // constant alpha multiplier (mirror glass)
    // Live point-light preview (fragment shader, world-space)
    int uModel_ = -1;
    int uLit_ = -1;
    int uLightCount_ = -1;
    int uLightPos_ = -1;
    int uLightCol_ = -1;
    // GS hardware fog preview
    int uFogOn_ = -1, uFogColor_ = -1, uFogStart_ = -1, uFogEnd_ = -1;
    int uFogEye_ = -1, uFogFwd_ = -1;
    bool fogOn_ = false;
    float fogColor_[3] = {0.5f, 0.5f, 0.55f};
    float fogStart_ = 15.0f, fogEnd_ = 120.0f;
    // Camera flashlight preview
    int uFlashOn_ = -1, uFlashCol_ = -1, uFlashInvR2_ = -1, uFlashCut2_ = -1;
    int uFlashSoft_ = -1;
    bool flashOn_ = false;
    float flashColor_[3] = {0.75f, 0.75f, 0.62f};
    float flashRange_ = 30.0f, flashAngle_ = 20.0f;
    // Spherical environment map (refl) preview - matcap on texture unit 1;
    // "@sky" dynamic mode approximated by the analytic sky gradient
    int uReflOn_ = -1, uRefl_ = -1, uReflStrength_ = -1;
    int uReflSkyHorizon_ = -1, uReflSkyTop_ = -1;
    int uReflRounded_ = -1, uReflCenter_ = -1;
    int uEmissive_ = -1;  // Ke floor, premultiplied by the object tint
    // Emissive lights (materials that light their surroundings)
    int uEmisCount_ = -1, uEmisPos_ = -1, uEmisAx_ = -1, uEmisAy_ = -1,
        uEmisAz_ = -1, uEmisCol_ = -1, uEmisRange_ = -1, uEmisObj_ = -1;
    // Ambient occlusion preview (see setAmbientOcclusion)
    int uAoOn_ = -1, uAoStrength_ = -1, uAoRadius_ = -1, uAoCount_ = -1;
    int uAoSelfObj_ = -1, uAoGround_ = -1, uAoReceive_ = -1;
    int uAoPos_ = -1, uAoAx_ = -1, uAoAy_ = -1, uAoAz_ = -1, uAoObj_ = -1;
    int uAoHeight_ = -1, uAoHmRect_ = -1, uAoHmOn_ = -1;
    // Baked GI probe grid (see setGiProbes)
    int uGiOn_ = -1, uGiProbes_ = -1, uGiOrigin_ = -1, uGiStep_ = -1,
        uGiDim_ = -1, uGiScale_ = -1;
    uint32_t giTex_ = 0;
    float giOrigin_[3] = {0, 0, 0};
    float giStep_[3] = {1, 1, 1};
    int giDim_[3] = {0, 0, 0};
    float giScale_ = 1.0f;
    std::vector<uint8_t> giPixels_;  // staged until the GL context exists
    // Baked terrain lightmap (see setGiTerrain). Sampled on the CPU while the
    // terrain chunks are built, exactly where the generated game samples it -
    // its shadeAt is this one's twin - so the ground's own tint survives (the
    // terrain carries it in the vertex colour, which the probe route replaced
    // wholesale). uGiSkipProbe_ then keeps the fragment shader off the probes
    // for those draws.
    int uGiSkipProbe_ = -1;
    std::vector<uint8_t> giTerrLight_;  // size*size*3, empty = no lit map
    int giTerrSize_ = 0;
    bool giUploadPending_ = false;
    void uploadGiProbes();
    bool aoOn_ = false;
    float aoStrength_ = 0.55f;
    float aoRadius_ = 2.5f;
    std::vector<uint8_t> aoGrid_;  // terrain self-AO (aobake::terrainAO)
    uint32_t aoHmTex_ = 0;         // R32F heightmap for the ground term
    int aoHmW_ = 0, aoHmD_ = 0;    // dimensions of the uploaded heightmap

    // Terrain in chunks of kTerrainChunkCells^2 cells (mesh + grid lines per
    // chunk) so sculpting rebuilds only the chunks under the brush. Grid
    // lines drop to chunk borders only above kTerrainFullGridCells total
    // cells - a full per-cell grid on a 512x512 map is solid noise (and tens
    // of MB of line vertices).
    static constexpr int kTerrainChunkCells = 64;
    static constexpr int kTerrainFullGridCells = 128 * 128;
    std::vector<Mesh> terrainChunkMeshes_;  // tcChunksX_ * tcChunksZ_, row-major
    std::vector<Mesh> terrainLineMeshes_;
    // Painted-layer passes: [layer * chunkCount + chunk]; an empty Mesh where a
    // layer has no weight on that chunk. Drawn blended after the base chunks.
    std::vector<Mesh> terrainLayerMeshes_;
    std::vector<TerrainLayerDraw> terrainLayers_;
    std::vector<uint8_t> splat_;  // hmW_*hmD_*layers, layer-interleaved
    float tintVariation_ = 0.0f;  // macro ground variation amplitude (0 = off)
    float tintScale_ = 24.0f;     // patch size, world units
    int tcChunksX_ = 0, tcChunksZ_ = 0;
    int tcCellsX_ = 0, tcCellsZ_ = 0;
    void buildTerrainChunkMesh(int cx, int cz);
    Mesh axes_;  // world axes
    Mesh box_, sphere_, cylinder_, cone_, plane_, decal_, spawnMarker_, playerMarker_;
    Mesh lightGizmo_;  // small unshaded bulb marking a point light
    Mesh wireSphere_;  // unit-radius ring sphere, scaled to a light's radius
    Mesh cameraBody_;     // Camera entity marker (film camera, lens = +Z)
    Mesh cameraFrustum_;  // FOV wedge lines, scaled to the entity's FOV
    // Per-detail primitive meshes (Box/Sphere/Cylinder/Cone), built lazily and
    // shared across objects with the same detail. The fixed box_ / sphere_ /
    // cylinder_ / cone_ above stay at the default detail (markers, previews).
    std::map<int, Mesh> boxMeshes_, sphereMeshes_, cylinderMeshes_, coneMeshes_;
    const Mesh& primMesh(PrimitiveType type, int detail);
    void clearPrimMeshCache();
    std::string projectDir_;
    // .obj models split per material (MTL): each part carries its own GL mesh
    // (Kd baked into the vertex colors) and map_Kd texture.
    struct ModelPart {
        Mesh mesh;
        uint32_t tex = 0;  // GL texture from map_Kd (0 = untextured)
        // map_Kd carries transparency: draw this part cutout + blended, the
        // way the console's static pipeline alpha-tests every texture. Leaf
        // cards are the everyday case (Tree Generator); without it their
        // transparent margin renders as the PNG's black RGB.
        bool alpha = false;
        float ke[3] = {0.0f, 0.0f, 0.0f};  // Ke emission floor (0 = matte)
        uint32_t reflTex = 0;      // refl sphere map (0 = not reflective)
        float reflStrength = 0.0f;
        bool reflSky = false;      // refl "@sky" - live sky gradient
        bool reflRounded = false;  // refl "-rounded" env normals
        float centroid[3] = {0, 0, 0};  // model-space, for the rounded mode
    };
    struct ModelDraw {
        std::vector<ModelPart> parts;  // empty = missing/unparseable model
        float mn[3] = {0, 0, 0};       // model-space AABB (AO occluder shape)
        float mx[3] = {0, 0, 0};
    };
    // keyed by "<modelPath>|<materialPath>" - an .mtl override changes the draw
    std::map<std::string, ModelDraw> modelCache_;
    const ModelDraw* modelDraw(const std::string& relPath,
                               const std::string& materialRel);
    void clearModelCache();
    // GL-free model AABB lookup (objparser only), cached; the AO occluder
    // collection uses this so reading bounds never triggers a GL upload.
    // Value: (loaded?, {minXYZ, maxXYZ}).
    std::map<std::string, std::pair<bool, std::array<float, 6>>> modelBoundsCache_;
    bool modelBounds(const std::string& relPath, const std::string& materialRel,
                     float mn[3], float mx[3]);

    // Animated .glb models: the baked clips stay CPU-side and each part owns
    // a dynamic VBO that is re-lerped per frame for the playback preview
    // (same morph-frame interpolation the PS2 does on VU1).
    struct AnimModelDraw {
        bool ok = false;
        glbparser::Baked baked;
        struct Part {
            Mesh mesh;
            uint32_t tex = 0;
        };
        std::vector<Part> parts;  // parallel to baked.parts
    };
    // keyed by "modelPath|materialOverride" (an assigned .mtl overrides the
    // model's own materials, resolved into the bake - same as the game)
    std::map<std::string, AnimModelDraw> animModelCache_;
    AnimModelDraw* animModelDraw(const std::string& relPath,
                                 const std::string& materialRel);
    // Uploads the object's current pose (clip + preview clock) into the VBOs.
    void updateAnimPose(AnimModelDraw& draw, const SceneObject& o);
    // Uploads one explicit pose: `frame` is a fractional index into the whole
    // baked frame list, wrapped inside [first, first + count). The shared
    // worker behind updateAnimPose and the Animation Editor preview.
    void uploadAnimPose(AnimModelDraw& draw, int firstFrame, int frameCount,
                        float frame);
    double animClock_ = 0.0;  // preview time in seconds (advanced per render)
    // Non-destructive clip edits pushed in by the app (it owns the Project).
    // The preview applies the same trim/retime the build bakes, so a placed
    // object plays exactly what will ship.
    std::vector<AnimClipEdit> animEdits_;
    float animProjectScale_ = 1.0f;  // project fps ratio (animedit.hpp)
    const AnimClipEdit* animEditFor(const std::string& modelRel,
                                    const std::string& sourceClip) const;

    // Primitive materials: first entry of an assigned .mtl (Kd tint + map_Kd)
    struct MaterialDraw {
        uint32_t tex = 0;
        float kd[3] = {1.0f, 1.0f, 1.0f};
        float ke[3] = {0.0f, 0.0f, 0.0f};  // Ke emission floor (0 = matte)
        uint32_t reflTex = 0;      // refl sphere map (0 = not reflective)
        float reflStrength = 0.0f;
        bool reflSky = false;      // refl "@sky" - live sky gradient
        bool reflRounded = false;  // refl "-rounded" env normals
    };
    std::map<std::string, MaterialDraw> materialCache_;  // by relative path
    const MaterialDraw* materialDraw(const std::string& relPath);
    // Emissive-light material lookups (collectEmitters reads .mtl files, and
    // the emitter set is rebuilt every frame). Cleared by invalidateAssets().
    aobake::GlowCache emisGlowCache_;

    std::map<std::string, uint32_t> texCache_;  // GL textures by relative path
    // Which of those images actually carry transparency (any texel below
    // fully opaque), filled as they load - the cutout/blend decision, so a
    // draw site never has to re-read the file.
    std::map<std::string, bool> texAlpha_;
    uint32_t glTexture(const std::string& relPath);
    bool texHasAlpha(const std::string& relPath) const;
    void clearTexCache();

    // Live particle-emitter preview. The simulation mirrors the generated
    // game's updateParticles() per-kind formulas (templates.cpp) - keep them
    // in sync. Alpha-blended camera-facing quads on a shared dynamic buffer,
    // one pool per emitter keyed by object index (reset when the emitter's
    // kind or count changes; a removed/retyped object drops its pool).
    struct PreviewParticle {
        float pos[3] = {0, 0, 0};
        float vel[3] = {0, 0, 0};
        float life = 0.0f, maxLife = 1.0f;
    };
    struct EmitterPreview {
        unsigned rng = 1;
        int kind = -1;
        int count = 0;
        std::vector<PreviewParticle> parts;
    };
    std::map<int, EmitterPreview> emitterPreviews_;
    double particleClock_ = 0.0;  // last sim time (advances with animClock_)
    uint32_t particleProgram_ = 0;
    int uPartMvp_ = -1, uPartUseTex_ = -1;
    uint32_t particleVao_ = 0, particleVbo_ = 0;  // pos3 + rgba4 + uv2, dynamic
    // Simulates and draws every enabled emitter (called once per render).
    void drawEmitterPreviews(const std::vector<SceneObject>& objects,
                             const float* viewProj, const float* eye,
                             const float* fwd);
    std::string terrainTexture_;  // resolved map_Kd of the terrain material
    float terrainTile_[2] = {1.0f, 1.0f};  // map_Kd -s: repeats per world unit
    float terrainKd_[3] = {1.0f, 1.0f, 1.0f};  // terrain material Kd tint
    bool terrainHasMaterial_ = false;  // false = checker greens fallback
    Mesh wireCube_;  // selection outline (unit cube edges)
    std::vector<PeerSel> peerSels_;  // session peers' selections (see above)
    Mesh segment_;   // unit +Z line segment (portal link line)
    Mesh portalArrow_;  // +Z line arrow: the portal's entry-side marker
    bool usableHighlight_ = false;  // wire box on usable objects
    float usableHighlightCol_[3] = {1.0f, 0.85f, 0.15f};

    // The RENDER target: the panel size normally, the GS framebuffer size in
    // the PS2 output mode. Everything inside render() sizes itself from
    // fbWidth_/fbHeight_, so the whole scene pass moves to GS resolution
    // without knowing about it.
    uint32_t fbo_ = 0, colorTex_ = 0, depthRbo_ = 0;
    int fbWidth_ = 0, fbHeight_ = 0;
    // Which framebuffer holds the image the last render() returned (fbo_,
    // gradeFbo_ when the grading pass ran, or outFbo_ in the PS2 output mode)
    // and its size - the source grabPreviewRgb blits from, so the phone sees
    // the graded picture too. 0 = nothing rendered yet.
    uint32_t lastImageFbo_ = 0;
    int lastImageW_ = 0, lastImageH_ = 0;

    // PS2 output mode: the panel-sized presentation target the GS image is
    // scaled into (nearest, letterboxed into the display window), and the
    // panel size itself - camView() needs it to work out the letterbox even
    // though every render/pick call now passes the GS size around.
    Ps2Output ps2_;
    void ensureOutputFramebuffer(int width, int height);
    uint32_t outFbo_ = 0, outTex_ = 0;
    int outW_ = 0, outH_ = 0;
    // How much of the panel the picture covers, per axis (1 = edge to edge).
    // The letterbox is centred by construction, so it is a pure scale: the
    // projection handed to the gizmo is multiplied by it, camRay divides by
    // it, projectToImage multiplies again.
    void ps2LetterBox(float& sx, float& sy) const;
    uint32_t ps2Program_ = 0;
    int uPs2Src_ = -1, uPs2Box_ = -1, uPs2Texel_ = -1, uPs2Flicker_ = -1;

    // Phone-camera readback target: a small RGBA8 framebuffer the viewport
    // image is blitted (and thus downscaled) into before glReadPixels.
    void ensureGrabFramebuffer(int width, int height);
    uint32_t grabFbo_ = 0, grabTex_ = 0;
    int grabW_ = 0, grabH_ = 0;

    // Material Editor preview target + fixed backdrop meshes
    void ensurePreviewFramebuffer(int width, int height);
    void ensurePreviewBackdrop();  // lazily builds prevBg_ / prevFloor_
    uint32_t prevFbo_ = 0, prevTex_ = 0, prevDepth_ = 0;
    int prevW_ = 0, prevH_ = 0;
    // Animation Editor preview target. Its own FBO on purpose: both tool
    // windows can be open at once and size themselves independently, so
    // sharing one target would resize it twice per frame and make each window
    // show the other's last draw. Only the backdrop meshes are shared.
    void ensureAnimFramebuffer(int width, int height);
    uint32_t animFbo_ = 0, animTex_ = 0, animDepth_ = 0;
    int animFbW_ = 0, animFbH_ = 0;
    Mesh prevBg_, prevFloor_;  // vertical gradient + checker floor (y = 0 local)
    uint32_t uvCheckerTex_ = 0;  // generated UV-checker (displayMode 2)

    // Tree Generator preview target. Its OWN framebuffer, not the Material
    // Editor's: both tools can be open at once, they size their previews
    // independently, and both draw within a single UI frame - sharing one
    // target would thrash its size and make each show the other's image.
    void ensureTreeFramebuffer(int width, int height);
    uint32_t treeFbo_ = 0, treeTex_ = 0, treeDepth_ = 0;
    int treeFbW_ = 0, treeFbH_ = 0;

    // Asset Browser thumbnail target (see assetThumb). Its own framebuffer for
    // the same reason the tree preview has one - the browser bakes thumbnails
    // in the middle of a frame in which the Material Editor may be drawing its
    // own preview. Baked images live in thumbCache_ (one texture per asset, an
    // entry with value 0 = tried and unusable), dropped by invalidateAssets.
    void ensureThumbFramebuffer();
    uint32_t thumbFbo_ = 0, thumbColor_ = 0, thumbDepth_ = 0;
    std::map<std::string, uint32_t> thumbCache_;
    void clearThumbCache();

    // Tree Generator preview geometry + textures (see renderTreePreview);
    // rebuilt only when the desc version changes.
    Mesh treePrevBark_, treePrevLeaves_;
    uint32_t treePrevBarkTex_ = 0, treePrevLeafTex_ = 0;
    uint64_t treePrevVersion_ = 0;
    bool treePrevHasVersion_ = false;

    // Model shown in the material preview. Unlike modelCache_ the part Kd is
    // NOT baked into the vertex colors (it rides the tint uniform instead) so
    // staged, uncommitted edits of the selected entry preview live; the CPU
    // triangles (pos3 + uv2 per vertex) feed the paint raycast.
    struct MatPrevPart {
        Mesh mesh;
        std::string material;  // usemtl name
        float kd[3] = {1.0f, 1.0f, 1.0f};
        float ke[3] = {0.0f, 0.0f, 0.0f};  // Ke emission floor (0 = matte)
        std::string texRel;    // project-relative map_Kd ("" = none)
        std::string reflRel;   // project-relative refl sphere map ("" = none)
        float reflStrength = 0.0f;
        bool reflSky = false;  // refl "@sky" - live sky gradient
        bool reflRounded = false;   // refl "-rounded" env normals
        float centroid[3] = {0, 0, 0};  // model-space, for the rounded mode
        std::vector<float> tris;  // pos3 + uv2, flat triangle list
    };
    struct MatPrevModel {
        // "<modelRel>|<mtlRel>|<light>", "" = nothing loaded. The light rides
        // along because the shade is baked into these vertex colors.
        std::string key;
        bool ok = false;
        std::vector<MatPrevPart> parts;
        float center[3] = {0.0f, 0.0f, 0.0f};
        float minY = -0.5f;
        float radius = 1.0f;  // AABB half-diagonal (camera framing)
    };
    MatPrevModel matPrevModel_;
    const MatPrevModel* matPrevModelDraw(const std::string& modelRel,
                                         const std::string& mtlRel,
                                         const PreviewLight& light);
    // Bind-pose preview parts for an animated (.glb/.fbx) model.
    void buildMatPrevAnimated(const std::string& modelRel,
                              const std::string& mtlRel);
    void clearMatPrevModel();

    // CPU triangles of the four preview primitives (pos3 + uv2), built on
    // first use from the same unit-mesh generators as box_/sphere_/... so the
    // paint raycast sees exactly the drawn geometry.
    std::vector<float> prevShapeTris_[4];

    // Unit shapes re-baked for a PreviewLight override. box_/sphere_/... carry
    // the scene's baked shade and are shared with the viewport, so an
    // overridden material preview draws from these private copies instead.
    // Rebuilt only when the override values change (`prevLightKey_`), not per
    // frame - baking is the same cost as buildPrimitiveMeshes.
    Mesh prevLitShape_[4];
    std::string prevLightKey_;  // "" = no override baked yet
    // Serialized PreviewLight - the shape cache key and part of the
    // matPrevModel_ key (an override re-bakes the model's vertex colors too).
    static std::string previewLightKey(const PreviewLight& l);
    // Points the drawn mesh at the override copy when `l` is on, building it
    // (and dropping a stale bake) on demand.
    const Mesh& litShape(int shape, const PreviewLight& l);

    // Camera + geometry of the last renderMaterialPreview, consumed by
    // materialPreviewPick. Basis vectors are world-space, fov is vertical.
    struct MatPrevPick {
        bool valid = false;
        int shape = 1;  // 4 = model (raycast matPrevModel_)
        std::string entryName;
        float eye[3], fwd[3], right[3], up[3];
        float tanHalf = 0.4142f, aspect = 1.0f;
    };
    MatPrevPick matPrevPick_;

    // Color grading post pass (grading preview): colorTex_ -> gradeTex_
    bool gradingOn_ = false;
    CompiledGrading grading_;
    uint32_t gradeProgram_ = 0, gradeFbo_ = 0, gradeTex_ = 0, gradeVao_ = 0;
    int uGradeSrc_ = -1, uGradeGain_ = -1, uGradeLiftPos_ = -1,
        uGradeLiftNeg_ = -1, uGradeMixCol_ = -1, uGradeMixAmt_ = -1;

    float viewM_[16] = {};
    float projM_[16] = {};
};