#pragma once

#include <string>

#include "project.hpp"

// Host-side top-down silhouette bake for the one-quad runtime blob shadow.
// The result is an ordinary project PNG; the console only samples it and never
// renders the caster a second time.
namespace blobshadowbake {

bool canBake(const SceneObject& object);

// Writes `projectRelativePng` (normally under res/textures/blob-shadows).
// Static OBJ models and primitives use their authored mesh; animated GLB/FBX
// models use frame zero of the same host import path as the preview.
bool bake(const Project& project, const SceneObject& object,
          const std::string& projectRelativePng, float footprint[2],
          std::string& error);

}  // namespace blobshadowbake
