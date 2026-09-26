/*
# _____        ____   ___
#   |     \/   ____| |___|
#   |     |   |   \  |   |
#-----------------------------------------------------------------------
# Copyright 2022, tyra - https://github.com/h4570/tyra
# Licensed under Apache License 2.0
# Sandro Sobczyński <sandro.sobczynski@gmail.com>
# Modified by TyraX: Texture::linkGeneration (the repository lookup cache).
*/

#pragma once

#include <tamtypes.h>
#include <vector>
#include <draw_buffers.h>
#include <draw_sampling.h>
#include "./texture_link.hpp"
#include "./texture_wrap.hpp"
#include "../texture_data.hpp"
#include "loaders/texture/builder/texture_builder_data.hpp"
#include "debug/debug.hpp"

namespace Tyra {

/**
 * Class which contains texture data.
 * Textures are paired with meshes/sprites via addLink() and
 * removeLink() functions which use meshId/spriteId and materialId (for mesh).
 */
class Texture {
 public:
  Texture(TextureBuilderData* data);
  ~Texture();

  u32 id;
  std::string name;
  TextureData* core;
  TextureData* clut;

  /**
   * Modified by TyraX: VRAM-resident texture (the dynamic env map). The
   * pixels live only in GS memory - rendered there every frame - so
   * useTexture() binds this texbuffer directly: no PATH3 upload, no
   * repository allocation, never evicted. nullptr = a normal texture.
   */
  texbuffer_t* vramResident = nullptr;

  /**
   * Modified by TyraX: the full path the texture was loaded from (as passed
   * to TextureRepository::add; "" for procedurally built textures). `name`
   * keeps only the basename, which is ambiguous across directories - the
   * texture hot-reload poller (docs/live-link.md) matches on this instead.
   */
  std::string sourcePath;

  /** Array of texture links with sprites/meshes.
   * Modified by TyraX: change it through addLink()/removeLink*() only - they
   * bump linkGeneration, which is what keeps TextureRepository's lookup cache
   * honest. A direct push_back here would leave that cache stale. */
  std::vector<TextureLink> links;

  /** Modified by TyraX: bumped by every change to any texture's links and to
   * the repository's texture list; TextureRepository's id -> texture cache is
   * valid only for the generation it was filled in. Starts at 1, so a
   * zero-initialised cache entry never matches. */
  static u32 linkGeneration;

  /** Modified by TyraX: where this texture's entry sat in
   * RendererCoreTexture's resident list the last time it was looked up - a
   * HINT, checked against the entry's id before it is trusted, so a list that
   * moved since only costs the scan it replaced. */
  mutable u32 residentHint = 0;

  inline const int& getWidth() const { return core->width; }

  inline const int& getHeight() const { return core->height; }

  float getSizeInMB() const;

  inline const texwrap_t* getWrapSettings() const { return &wrap; }

  inline const std::vector<TextureLink>& getTextureLinks() const {
    return links;
  }

  /**
   * Returns index of link.
   * -1 if not found.
   * @param t_id
   * For 3D: MeshMaterial id.
   * For 2D: Sprite id.
   */
  const s32 getIndexOfLink(const u32& t_id) const;

  /** Set texture wrapping */
  void setWrapSettings(const TextureWrap t_horizontal,
                       const TextureWrap t_vertical, int minu = 0, int minv = 0,
                       int maxu = 0, int maxv = 0);

  // ----
  //  Other
  // ----

  /** Assign texture to Sprite.Id or MeshMaterial.Id. */
  void addLink(const u32& t_id);

  u32 getTextureSize() const;

  /**
   * Check if texture is linked with MeshMaterial/Sprite.
   * @param t_id
   * For 3D: MeshMaterial material id.
   * For 2D: Sprite id.
   */
  const u8 isLinkedWith(const u32& t_id) const;

  void removeLinkByIndex(const u32& t_index);

  /**
   * Remove texture link with given MeshMaterial/Sprite.
   * @param t_id
   * For 3D: MeshMaterial material id.
   * For 2D: Sprite id.
   */
  void removeLinkById(const u32& t_id);

  void print() const;
  void print(const char* name) const;
  void print(const std::string& name) const { print(name.c_str()); }
  std::string getPrint(const char* name = nullptr) const;

  void setDefaultWrapSettings();

 private:
  void setPsm();

  texwrap_t wrap;
};
}  // namespace Tyra
