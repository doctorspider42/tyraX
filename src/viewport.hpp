#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

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

    // project root for resolving relative model paths (clears the model cache)
    void setProjectDir(const std::string& dir);

    // terrain texture (PNG, tiled; empty = checker colors) + world units per tile
    void setTerrainTexture(const std::string& relPath, float scale);

    // "Highlight usable objects" preference: marks usable objects with a wire
    // box in the highlight color (proximity is a game-runtime condition)
    void setUsableHighlight(bool enabled, const float* rgb);

    // Renders terrain + objects at the given pixel size, returns GL texture id.
    // selectedIndex: index into objects highlighted with an outline (-1 = none).
    uint32_t render(int width, int height, const std::vector<SceneObject>& objects,
                    int selectedIndex);

    // Camera controls, driven by the UI layer. The camera orbits a movable
    // target point: pan slides it in the view plane (middle mouse drag),
    // fly moves it on the horizontal plane along the view direction (WASD).
    void orbit(float dxPixels, float dyPixels);
    void zoom(float wheel);
    void pan(float dxPixels, float dyPixels);
    void fly(float forward, float strafe, float dt);

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
    // Live point-light preview (fragment shader, world-space)
    int uModel_ = -1;
    int uLit_ = -1;
    int uLightCount_ = -1;
    int uLightPos_ = -1;
    int uLightCol_ = -1;

    Mesh terrain_mesh_;
    Mesh lines_;  // terrain grid + axes
    Mesh box_, sphere_, cylinder_, cone_, spawnMarker_, playerMarker_;
    Mesh lightGizmo_;  // small unshaded bulb marking a point light
    Mesh wireSphere_;  // unit-radius ring sphere, scaled to a light's radius
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
    std::map<std::string, ModelDraw> modelCache_;  // by relative path
    const ModelDraw* modelDraw(const std::string& relPath);
    void clearModelCache();

    std::map<std::string, uint32_t> texCache_;  // GL textures by relative path
    uint32_t glTexture(const std::string& relPath);
    void clearTexCache();
    std::string terrainTexture_;
    float terrainTexScale_ = 4.0f;
    Mesh wireCube_;  // selection outline (unit cube edges)
    bool usableHighlight_ = false;  // wire box on usable objects
    float usableHighlightCol_[3] = {1.0f, 0.85f, 0.15f};

    uint32_t fbo_ = 0, colorTex_ = 0, depthRbo_ = 0;
    int fbWidth_ = 0, fbHeight_ = 0;

    float viewM_[16] = {};
    float projM_[16] = {};
};
