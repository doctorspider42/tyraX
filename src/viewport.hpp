#pragma once

#include <cstdint>
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
    // maxCells: terrain grid detail cap (Project > Preferences).
    void setTerrain(const TerrainConfig& terrain, int maxCells = 32);

    void setSkyColor(const float* rgb) {
        sky_[0] = rgb[0], sky_[1] = rgb[1], sky_[2] = rgb[2];
    }

    // Renders terrain + objects at the given pixel size, returns GL texture id.
    // selectedIndex: index into objects highlighted with an outline (-1 = none).
    uint32_t render(int width, int height, const std::vector<SceneObject>& objects,
                    int selectedIndex);

    // Camera controls, driven by the UI layer.
    void orbit(float dxPixels, float dyPixels);
    void zoom(float wheel);

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
    float sky_[3] = {0.25f, 0.55f, 0.78f};
    ViewMode viewMode_ = ViewMode::Solid;

    // camera (orbit around terrain center)
    float yaw_ = 0.8f;
    float pitch_ = 0.6f;
    float distance_ = 90.0f;

    uint32_t program_ = 0;
    int uMvp_ = -1;
    int uTint_ = -1;

    Mesh terrain_mesh_;
    Mesh lines_;  // terrain grid + axes
    Mesh box_, sphere_, cylinder_, cone_, spawnMarker_;
    Mesh wireCube_;  // selection outline (unit cube edges)

    uint32_t fbo_ = 0, colorTex_ = 0, depthRbo_ = 0;
    int fbWidth_ = 0, fbHeight_ = 0;

    float viewM_[16] = {};
    float projM_[16] = {};
};
