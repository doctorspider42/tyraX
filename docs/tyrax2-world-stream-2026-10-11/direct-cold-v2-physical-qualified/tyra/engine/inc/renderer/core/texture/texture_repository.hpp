/*
# _____        ____   ___
#   |     \/   ____| |___|
#   |     |   |   \  |   |
#-----------------------------------------------------------------------
# Copyright 2022, tyra - https://github.com/h4570/tyra
# Licensed under Apache License 2.0
# Sandro Sobczyński <sandro.sobczynski@gmail.com>
# Modified by TyraX: cached id -> texture lookup (findLinked).
*/

#pragma once

#include <tamtypes.h>
#include <draw_buffers.h>
#include <vector>
#include "./models/texture.hpp"
#include "renderer/3d/mesh/mesh.hpp"
#include "renderer/core/2d/sprite/sprite.hpp"
#include "./renderer_core_texture_buffers.hpp"
#include "loaders/texture/base/texture_loader_selector.hpp"
#include <string>

namespace Tyra {

class RendererCoreTexture;

class TextureRepository {
 public:
  TextureRepository();
  ~TextureRepository();

  // Modified by TyraX: the repository needs its owning core to
  // properly deallocate GS texture buffers on free()/removeById() -
  // removeBufferId() only tombstones the allocation entry (id = -1) and
  // leaks the VRAM plus the texbuffer structs, which games that stream
  // textures in and out at runtime (layer streaming) cannot afford.
  void init(std::vector<RendererCoreTextureBuffers>* textureBuffers,
            RendererCoreTexture* coreTexture = nullptr);

  /** Returns all repository textures. */
  // Modified by TyraX: the caller may reorder or edit the list through the
  // pointer, so handing it out drops the lookup cache (see findLinked).
  std::vector<Texture*>* getAll() {
    ++Texture::linkGeneration;
    return &textures;
  }

  u32 getTexturesCount() const { return static_cast<u32>(textures.size()); }

  /**
   * Returns single texture.
   * nullptr if not found.
   */
  Texture* getBySpriteId(const u32& t_id) const;

  /**
   * Returns single texture.
   * nullptr if not found.
   */
  Texture* getByMeshMaterialId(const u32& t_id) const;

  /**
   * Returns single texture.
   * nullptr if not found.
   */
  Texture* getByTextureId(const u32& t_id) const;

  /**
   * Returns index of link.
   * -1 if not found.
   */
  const s32 getIndexOf(const u32& t_texId) const;

  /**
   * Add unlinked texture.
   * @param fullpath Full path to texture file. Example: "host:texture.png"
   */
  Texture* add(const char* fullpath);

  /**
   * Add unlinked texture.
   * @param fullpath Full path to texture file. Example: "host:texture.png"
   */
  inline Texture* add(const std::string& fullpath) {
    return add(fullpath.c_str());
  }

  /**
   * Add unlinked texture.
   * @param texture Your own texture
   */
  Texture* add(Texture* texture);

  /**
   * Add linked textures in given path for mesh material names.
   */
  void addByMesh(const Mesh* mesh, const char* directory,
                 const char* extension);

  /**
   * Add linked textures in given path for mesh material names.
   */
  inline void addByMesh(const Mesh* mesh, const std::string& directory,
                        const char* extension) {
    addByMesh(mesh, directory.c_str(), extension);
  }

  /**
   * Remove texture from repository.
   * Texture IS destructed.
   */
  void free(const u32& texId);
  void free(const Texture* tex);
  void free(const Texture& tex);
  void freeBySprite(const Sprite& sprite);
  void freeByMesh(const Mesh& mesh);
  void freeByMesh(const Mesh* mesh);

  /**
   * remove texture buffer id if exist.
   * Texture buffer is NOT destructed.
   * easy way to create another texture buffer.
   */
  int removeBufferId(const u32& t_texId);

  /**
   * Remove texture from repository.
   * Texture is NOT destructed.
   * Not recommended.
   */
  void removeById(const u32& t_texId);

 private:
  void removeByIndex(const u32& t_index);

  std::vector<Texture*> textures;

  // Modified by TyraX: getBySpriteId / getByMeshMaterialId both mean "the
  // FIRST texture whose links contain this id", found by walking every
  // texture's link list. A sprite does that once per draw: on a physical PS2
  // it was 0.97 ms of a 1.73 ms, 85-sprite HUD in Motor District, whose
  // repository also holds every scene material. A direct-mapped cache of that
  // answer, valid for one Texture::linkGeneration, returns the same texture
  // the walk would - the walk runs again after any link or list change.
  struct LinkCacheEntry {
    u32 id;
    u32 generation;
    Texture* texture;
  };
  static constexpr u32 kLinkCacheBits = 8;
  mutable LinkCacheEntry linkCache[1U << kLinkCacheBits] = {};
  Texture* findLinked(const u32& t_id) const;
  std::vector<RendererCoreTextureBuffers>* textureBuffers;
  RendererCoreTexture* coreTexture = nullptr;  // Modified by TyraX
  TextureLoaderSelector texLoaderSelector;
};
}  // namespace Tyra
