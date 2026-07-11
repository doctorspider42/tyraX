#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "glbparser.hpp"
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

    bool init();  // requires a current GL context
    void shutdown();

    // (Re)builds the terrain mesh from project settings.
    // maxCells: terrain grid detail cap; heights: sculpted heightmap
    // (hmW x hmD vertex grid, empty = flat).
    void setTerrain(const TerrainConfig& terrain, int maxCells = 32,
                    const std::vector<float>& heights = {}, int hmW = 0, int hmD = 0);

    // Casts a ray through normalized image coords onto the terrain surface.
    // Returns false when the ray misses; used by the sculpting brush.
    bool terrainRaycast(float u, float v, float& outX, float& outZ) const;

    float terrainHeight(float x, float z) const;  // bilinear, 0 when flat

    // horizon + zenith colors; gradient=false renders a flat horizon color
    void setSky(const float* horizonRgb, const float* topRgb, bool gradient);

    // directional light baked into mesh shading (matches the PS2 output)
    void setLighting(const float* dir, float ambient, float diffuse, const float* color,
                     float brightness);

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

    // Renders terrain + objects at the given pixel size, returns GL texture id.
    // selection: indices outlined; primary (the anchor, usually selection.back())
    // is outlined brighter so it reads as the value source for the multi-editor.
    uint32_t render(int width, int height, const std::vector<SceneObject>& objects,
                    const std::vector<int>& selection, int primary);

    // Material Editor live preview: a lit turntable primitive over a checker
    // floor, rendered into its own framebuffer (render() resizes the main one
    // to the viewport every frame). kd = diffuse tint (channels may exceed 1 -
    // material brightness), texRel = map_Kd as a project-relative path
    // ("" = untextured), shape: 0 box, 1 sphere, 2 cylinder, 3 cone,
    // angleDeg = turntable rotation. Returns the GL texture id.
    uint32_t renderMaterialPreview(int width, int height, const float* kd,
                                   const std::string& texRel, int shape,
                                   float angleDeg);

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

    // Recenter the camera on the terrain center (world origin) and restore the
    // default orientation and a distance framing the whole terrain.
    void resetView();

    // View/projection of the last render() call (column-major, OpenGL style) -
    // used by the transform gizmo.
    const float* viewMatrix() const { return viewM_; }
    const float* projMatrix() const { return projM_; }

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
    void destroyMesh(Mesh& m);

    TerrainConfig terrain_;
    int maxCells_ = 32;
    std::vector<float> heights_;
    int hmW_ = 0, hmD_ = 0;
    float sky_[3] = {0.25f, 0.55f, 0.78f};
    float skyTop_[3] = {0.08f, 0.3f, 0.65f};
    bool skyGradient_ = true;
    Mesh skyQuad_;
    bool skyQuadDirty_ = true;
    ViewMode viewMode_ = ViewMode::Solid;

    // camera (orbit around a movable target, initially the terrain center)
    float yaw_ = 0.8f;
    float pitch_ = 0.6f;
    float distance_ = 90.0f;
    float target_[3] = {0.0f, 0.0f, 0.0f};

    uint32_t program_ = 0;
    int uMvp_ = -1;
    int uTint_ = -1;
    int uUseTex_ = -1;
    int uAlpha_ = -1;  // decal cutout/blend toggle
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

    Mesh terrain_mesh_;
    Mesh lines_;  // terrain grid + axes
    Mesh box_, sphere_, cylinder_, cone_, plane_, decal_, spawnMarker_, playerMarker_;
    Mesh lightGizmo_;  // small unshaded bulb marking a point light
    Mesh wireSphere_;  // unit-radius ring sphere, scaled to a light's radius
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
    };
    struct ModelDraw {
        std::vector<ModelPart> parts;  // empty = missing/unparseable model
    };
    // keyed by "<modelPath>|<materialPath>" - an .mtl override changes the draw
    std::map<std::string, ModelDraw> modelCache_;
    const ModelDraw* modelDraw(const std::string& relPath,
                               const std::string& materialRel);
    void clearModelCache();

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
    std::map<std::string, AnimModelDraw> animModelCache_;
    AnimModelDraw* animModelDraw(const std::string& relPath);
    // Uploads the object's current pose (clip + preview clock) into the VBOs.
    void updateAnimPose(AnimModelDraw& draw, const SceneObject& o);
    double animClock_ = 0.0;  // preview time in seconds (advanced per render)

    // Primitive materials: first entry of an assigned .mtl (Kd tint + map_Kd)
    struct MaterialDraw {
        uint32_t tex = 0;
        float kd[3] = {1.0f, 1.0f, 1.0f};
    };
    std::map<std::string, MaterialDraw> materialCache_;  // by relative path
    const MaterialDraw* materialDraw(const std::string& relPath);

    std::map<std::string, uint32_t> texCache_;  // GL textures by relative path
    uint32_t glTexture(const std::string& relPath);
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
    bool usableHighlight_ = false;  // wire box on usable objects
    float usableHighlightCol_[3] = {1.0f, 0.85f, 0.15f};

    uint32_t fbo_ = 0, colorTex_ = 0, depthRbo_ = 0;
    int fbWidth_ = 0, fbHeight_ = 0;

    // Material Editor preview target + fixed backdrop meshes
    void ensurePreviewFramebuffer(int width, int height);
    uint32_t prevFbo_ = 0, prevTex_ = 0, prevDepth_ = 0;
    int prevW_ = 0, prevH_ = 0;
    Mesh prevBg_, prevFloor_;  // vertical gradient + checker floor

    // Color grading post pass (grading preview): colorTex_ -> gradeTex_
    bool gradingOn_ = false;
    CompiledGrading grading_;
    uint32_t gradeProgram_ = 0, gradeFbo_ = 0, gradeTex_ = 0, gradeVao_ = 0;
    int uGradeSrc_ = -1, uGradeGain_ = -1, uGradeLiftPos_ = -1,
        uGradeLiftNeg_ = -1, uGradeMixCol_ = -1, uGradeMixAmt_ = -1;

    float viewM_[16] = {};
    float projM_[16] = {};
};
