/*
# _____        ____   ___
#   |     \/   ____| |___|
#   |     |   |   \  |   |
#-----------------------------------------------------------------------
# Copyright 2022, tyra - https://github.com/h4570/tyra
# Licensed under Apache License 2.0
# Sandro Sobczyński <sandro.sobczynski@gmail.com>
# Modified by TyraX: link changes bump Texture::linkGeneration.
*/

#include <iomanip>
#include <draw_buffers.h>
#include <gs_psm.h>
#include <stdlib.h>
#include <malloc.h>
#include <string.h>
#include <fastmath.h>
#include <string>
#include <sstream>
#include "renderer/core/texture/models/texture.hpp"

namespace Tyra {

u32 Texture::linkGeneration = 1;

namespace TyraTexture {
u32 textureCounter = 1;
std::vector<u32> deletedIDs;
}  // namespace TyraTexture

Texture::Texture(TextureBuilderData* t_data) {
  if (TyraTexture::deletedIDs.empty() == false) {
    id = TyraTexture::deletedIDs.front();
    TyraTexture::deletedIDs.erase(TyraTexture::deletedIDs.begin());
  } else {
    id = TyraTexture::textureCounter++;
  }

  name = t_data->name;

  TYRA_ASSERT(t_data->width <= 512 && t_data->height <= 512,
              "Tyra supports only 512x512 textures max!");

  TYRA_ASSERT(t_data->width == 8 || t_data->width == 16 ||
                  t_data->width == 32 || t_data->width == 64 ||
                  t_data->width == 128 || t_data->width == 256 ||
                  t_data->width == 512,
              "Texture width/height should be 8/16/32/64/128/256/512!");

  TYRA_ASSERT(t_data->height == 8 || t_data->height == 16 ||
                  t_data->height == 32 || t_data->height == 64 ||
                  t_data->height == 128 || t_data->height == 256 ||
                  t_data->height == 512,
              "Texture width/height should be 8/16/32/64/128/256/512!");

  core = new TextureData(t_data->data, t_data->bpp, t_data->gsComponents,
                         t_data->width, t_data->height);

  clut =
      new TextureData(t_data->clut, t_data->clutBpp, t_data->clutGsComponents,
                      t_data->clutWidth, t_data->clutHeight);

  setDefaultWrapSettings();
}

Texture::Texture(const Texture* base, const unsigned char* rgba, int entries) {
  if (TyraTexture::deletedIDs.empty() == false) {
    id = TyraTexture::deletedIDs.front();
    TyraTexture::deletedIDs.erase(TyraTexture::deletedIDs.begin());
  } else {
    id = TyraTexture::textureCounter++;
  }
  TYRA_ASSERT(base != nullptr && base->clut != nullptr && base->clut->width > 0,
              "A palette variant needs a palettized base texture");
  name = base->name + "*";
  paletteBase = base;
  core = base->core;  // borrowed - see ~Texture
  const int w = base->clut->width, h = base->clut->height;
  const int count = w * h;
  auto* data = static_cast<unsigned char*>(memalign(128, count * 4));
  memset(data, 0, count * 4);
  for (int i = 0; i < count && i < entries; i++) {
    data[i * 4 + 0] = rgba[i * 4 + 0];
    data[i * 4 + 1] = rgba[i * 4 + 1];
    data[i * 4 + 2] = rgba[i * 4 + 2];
    // PngLoader's alpha: 0x80 opaque, trans >> 1 otherwise
    data[i * 4 + 3] = rgba[i * 4 + 3] == 255 ? 0x80 : rgba[i * 4 + 3] >> 1;
  }
  // the CSM1 order an 8-bit CLUT is stored in (PngLoader's "rotate clut")
  if (count == 256)
    for (int i = 0; i < 256; i++)
      if ((i & 0x18) == 8)
        for (int c = 0; c < 4; c++) {
          const unsigned char t = data[i * 4 + c];
          data[i * 4 + c] = data[(i + 8) * 4 + c];
          data[(i + 8) * 4 + c] = t;
        }
  clut = new TextureData(data, base->clut->bpp, base->clut->components, w, h);
  wrap = base->wrap;
}

Texture::~Texture() {
  TyraTexture::deletedIDs.push_back(id);
  if (links.size() > 0) links.clear();
  ++linkGeneration;  // a cached pointer to this texture must not survive it
  if (core && !paletteBase) delete core;
  if (clut) delete clut;
}

const s32 Texture::getIndexOfLink(const u32& t_id) const {
  for (u32 i = 0; i < links.size(); i++)
    if (links[i].id == t_id) return i;
  return -1;
}

const u8 Texture::isLinkedWith(const u32& t_id) const {
  s32 index = getIndexOfLink(t_id);
  if (index != -1)
    return true;
  else
    return false;
}

void Texture::removeLinkByIndex(const u32& t_index) {
  links.erase(links.begin() + t_index);
  ++linkGeneration;
}

void Texture::removeLinkById(const u32& t_id) {
  s32 index = getIndexOfLink(t_id);
  TYRA_ASSERT(index != -1, "Cant remove link, because it was not found!");
  removeLinkByIndex(index);
}

float Texture::getSizeInMB() const {
  return (core->width / 100.0F) * (core->height / 100.0F) *
         (core->bpp / 100.0F) / 8.0F;
}

/** Based on gsKit code, thank you guys! */
u32 Texture::getTextureSize() const {
  int widthBlocks, heightBlocks;
  int widthAlign, heightAlign;

  // Calculate the number of blocks width and height
  // A block is 256 bytes in size
  switch (core->bpp) {
    case bpp32:
    case bpp24:
      // 1 block = 8x8 pixels
      widthBlocks = (core->width + 7) / 8;
      heightBlocks = (core->height + 7) / 8;
      break;

    case bpp8:
      // 1 block = 16x16 pixels
      widthBlocks = (core->width + 15) / 16;
      heightBlocks = (core->height + 15) / 16;
      break;
    case bpp4:
      // 1 block = 32x16 pixels
      widthBlocks = (core->width + 31) / 32;
      heightBlocks = (core->height + 15) / 16;
      break;
    default:
      TYRA_TRAP("Unknown texture bpp");
      return -1;
  }

  // Calculate the minimum block alignment
  if (core->bpp == bpp32 || core->bpp == bpp24 || core->bpp == bpp8) {
    // 8x4 blocks in a page.
    // block traversing order:
    // 0145....
    // 2367....
    // ........
    // ........
    if (widthBlocks <= 2 && heightBlocks <= 1) {
      widthAlign = 1;
      heightAlign = 1;
    } else if (widthBlocks <= 4 && heightBlocks <= 2) {
      widthAlign = 2;
      heightAlign = 2;
    } else if (widthBlocks <= 8 && heightBlocks <= 4) {
      widthAlign = 4;
      heightAlign = 4;
    } else {
      widthAlign = 8;
      heightAlign = 4;
    }
  } else {
    if (widthBlocks <= 1 && heightBlocks <= 2) {
      widthAlign = 1;
      heightAlign = 1;
    } else if (widthBlocks <= 2 && heightBlocks <= 2) {
      widthAlign = 2;
      heightAlign = 2;
    } else if (widthBlocks <= 2 && heightBlocks <= 8) {
      widthAlign = 2;
      heightAlign = 8;
    } else {
      widthAlign = 4;
      heightAlign = 8;
    }
  }

  widthBlocks = (-widthAlign) & (widthBlocks + widthAlign - 1);
  heightBlocks = (-heightAlign) & (heightBlocks + heightAlign - 1);

  return widthBlocks * heightBlocks * 256;
}

void Texture::setDefaultWrapSettings() {
  wrap.horizontal = WRAP_REPEAT;
  wrap.vertical = WRAP_REPEAT;
  wrap.maxu = 0;
  wrap.maxv = 0;
  wrap.minu = 0;
  wrap.minv = 0;
}

void Texture::setWrapSettings(const TextureWrap t_horizontal,
                              const TextureWrap t_vertical, int minu, int minv,
                              int maxu, int maxv) {
  wrap.horizontal = t_horizontal;
  wrap.vertical = t_vertical;
  wrap.minu = minu;
  wrap.minv = minv;
  wrap.maxu = maxu;
  wrap.maxv = maxv;
}

void Texture::addLink(const u32& t_id) {
  TextureLink link;
  link.id = t_id;
  links.push_back(link);
  ++linkGeneration;
}

void Texture::print() const {
  auto text = getPrint(nullptr);
  printf("%s\n", text.c_str());
}

void Texture::print(const char* name) const {
  auto text = getPrint(name);
  printf("%s\n", text.c_str());
}

std::string Texture::getPrint(const char* objectName) const {
  std::stringstream res;
  if (objectName) {
    res << objectName << "(";
  } else {
    res << "Texture(";
  }

  std::string wrapString;
  if (wrap.horizontal == WRAP_REPEAT)
    wrapString = "WRAP_REPEAT";
  else if (wrap.horizontal == WRAP_CLAMP)
    wrapString = "WRAP_CLAMP";

  res << "id: " << id << ", " << std::endl;
  res << "name: " << name << ", " << std::endl;
  res << "core: " << core->getPrint() << ", " << std::endl;
  if (clut != nullptr) res << "clut: " << clut->getPrint() << ", " << std::endl;
  if (links.size()) {
    for (size_t i = 0; i < links.size(); i++) {
      res << "link " << i << " for id: " << links[i].id << ", " << std::endl;
    }
  }
  res << "wrap: " << wrapString << ")";
  return res.str();
}

}  // namespace Tyra
