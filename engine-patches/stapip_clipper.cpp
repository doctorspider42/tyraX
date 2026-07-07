/*
# Patched by tyra-editor (engine patch v2) - no heap allocations per clip call.
# Based on the original by Sandro Sobczynski (h4570/tyra), Apache License 2.0.
*/

#include "renderer/3d/pipeline/static/core/stapip_clipper.hpp"

namespace Tyra {

namespace {
// Rendering is single-threaded on the EE - one static pool is enough.
constexpr u32 kMaxClippedVerts = 1024;
PlanesClipVertex clippedPool[kMaxClippedVerts];
}  // namespace

StaPipClipper::StaPipClipper() {}
StaPipClipper::~StaPipClipper() {}

void StaPipClipper::setMVP(M4x4* t_mvp) { mvp = t_mvp; }

void StaPipClipper::init(const RendererSettings& settings) {
  algorithm.init(settings);
}

void StaPipClipper::setMaxVertCount(const u32& count) { maxVertCount = count; }

void StaPipClipper::clip(StaPipQBuffer* buffer) {
  TYRA_ASSERT(buffer->size <= maxVertCount / 3, "Buffer should have max ",
              maxVertCount / 3, " verts if we want to clip it.");

  EEClipAlgorithmSettings algoSettings = {buffer->bag->lighting != nullptr,
                                          buffer->bag->texture != nullptr,
                                          buffer->bag->color->many != nullptr};

  u32 outCount = 0;

  for (u32 i = 0; i < buffer->size / 3; i++) {
    for (u8 j = 0; j < 3; j++) {
      inputVerts[j] = *mvp * buffer->vertices[i * 3 + j];

      inputTriangle[j] = {
          &inputVerts[j],
          buffer->bag->lighting ? &buffer->normals[i * 3 + j] : nullptr,
          buffer->bag->texture ? &buffer->sts[i * 3 + j] : nullptr,
          buffer->bag->color->many ? &buffer->colors[i * 3 + j] : nullptr};
    }

    const u8 clippedSize =
        algorithm.clip(clippedTriangle, inputTriangle, algoSettings);
    if (clippedSize < 3) continue;

    for (u8 j = 1; j <= (u8)(clippedSize - 2); j++) {
      if (outCount + 3 > kMaxClippedVerts) break;
      clippedPool[outCount++] = clippedTriangle[0];
      clippedPool[outCount++] = clippedTriangle[j];
      clippedPool[outCount++] = clippedTriangle[(j + 1) % clippedSize];
    }
  }

  for (u32 i = 0; i < outCount; i++)
    clippedPool[i].position /= clippedPool[i].position.w;

  buffer->reallocateManually(outCount);
  for (u32 i = 0; i < outCount; i++) {
    buffer->vertices[i] = clippedPool[i].position;
    if (buffer->bag->texture) buffer->sts[i] = clippedPool[i].st;
    if (buffer->bag->color->many) buffer->colors[i] = clippedPool[i].color;
    if (buffer->bag->lighting) buffer->normals[i] = clippedPool[i].normal;
  }
}

// Kept only to satisfy the class declaration (unused after the patch).
void StaPipClipper::perspectiveDivide(std::vector<PlanesClipVertex>* vertices) {
  for (u32 i = 0; i < vertices->size(); i++) {
    (*vertices)[i].position /= (*vertices)[i].position.w;
  }
}

void StaPipClipper::moveDataToBuffer(
    const std::vector<PlanesClipVertex>& vertices, StaPipQBuffer* buffer) {
  buffer->reallocateManually(vertices.size());

  for (u32 i = 0; i < vertices.size(); i++) {
    auto& vertex = vertices.at(i);
    buffer->vertices[i] = vertex.position;

    if (buffer->bag->texture) buffer->sts[i] = vertex.st;
    if (buffer->bag->color->many) buffer->colors[i] = vertex.color;
    if (buffer->bag->lighting) buffer->normals[i] = vertex.normal;
  }
}

}  // namespace Tyra
