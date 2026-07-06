#pragma once

#include <cstdint>
#include <vector>

#include "project.hpp"

// 3D preview of the project terrain and scene objects, rendered into an
// offscreen texture shown inside an ImGui window. Orbit camera (drag+scroll).
class Viewport {
public:
    bool init();  // requires a current GL context
    void shutdown();

    // (Re)builds the terrain mesh from project settings.
    void setTerrain(const TerrainConfig& terrain);

    // Renders terrain + objects at the given pixel size, returns GL texture id.
    // selectedIndex: index into objects highlighted with an outline (-1 = none).
    uint32_t render(int width, int height, const std::vector<SceneObject>& objects,
                    int selectedIndex);

    // Camera controls, driven by the UI layer.
    void orbit(float dxPixels, float dyPixels);
    void zoom(float wheel);

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

    // camera (orbit around terrain center)
    float yaw_ = 0.8f;
    float pitch_ = 0.6f;
    float distance_ = 90.0f;

    uint32_t program_ = 0;
    int uMvp_ = -1;
    int uTint_ = -1;

    Mesh terrain_mesh_;
    Mesh lines_;  // terrain grid + axes
    Mesh box_, sphere_, cylinder_, cone_;
    Mesh wireCube_;  // selection outline (unit cube edges)

    uint32_t fbo_ = 0, colorTex_ = 0, depthRbo_ = 0;
    int fbWidth_ = 0, fbHeight_ = 0;
};
