/*
# _____        ____   ___
#   |     \/   ____| |___|
#   |     |   |   \  |   |
#-----------------------------------------------------------------------
# Copyright 2022, tyra - https://github.com/h4570/tyra
# Licensed under Apache License 2.0
# Sandro Sobczyński <sandro.sobczynski@gmail.com>
# Modified by TyraX: decode from a whole-file memory buffer
*/

#include "debug/debug.hpp"
#include <stdio.h>
#include <string.h>
#include <malloc.h>
#include <png.h>
#include <string>
#include <vector>
#include <draw_buffers.h>
#include "loaders/texture/png_loader.hpp"
#include "file/file_utils.hpp"

namespace Tyra {

PngLoader::PngLoader() {}

PngLoader::~PngLoader() {}

struct PngClut {
  u8 r, g, b, a;
};

// libpng pulls the file in many small reads; over ps2link's network host:
// each one is a full EE->IOP->TCP round trip and a 400 KB texture takes
// minutes. Read the whole file up front with large sequential freads (no
// fseek - it is unreliable over the PS2 host fs) and let libpng decode
// from memory.
struct PngMemorySource {
  const u8* data;
  size_t size;
  size_t offset;
};

static void pngReadFromMemory(png_structp pngPtr, png_bytep out,
                              png_size_t count) {
  auto* src = static_cast<PngMemorySource*>(png_get_io_ptr(pngPtr));
  if (src->offset + count > src->size)
    png_error(pngPtr, "PNG read past end of file");
  memcpy(out, src->data + src->offset, count);
  src->offset += count;
}

// Modified by TyraX: a visible stand-in texture returned when a PNG can
// not be loaded (missing / empty file), so a missing asset degrades to an
// obvious magenta-and-black checkerboard instead of crashing the game. 8x8,
// 32bpp RGBA (power-of-two, always valid for the GS uploader).
static TextureBuilderData* makePlaceholderTexture(const std::string& name) {
  const int dim = 8;
  auto* result = new TextureBuilderData();
  result->width = dim;
  result->height = dim;
  result->name = name;
  result->gsComponents = TEXTURE_COMPONENTS_RGBA;
  result->bpp = bpp32;
  // getTextureSize is a TextureLoader member (not reachable from this free
  // helper); bpp32 is exactly width*height*4 bytes (see TextureLoader).
  result->data = static_cast<unsigned char*>(memalign(128, dim * dim * 4));
  unsigned char* p = result->data;
  for (int y = 0; y < dim; y++) {
    for (int x = 0; x < dim; x++) {
      const bool on = (((x >> 1) ^ (y >> 1)) & 1) != 0;  // 2x2 checker
      *p++ = on ? 255 : 0;  // R
      *p++ = 0;             // G
      *p++ = on ? 255 : 0;  // B
      *p++ = 128;           // A (PS2 GS convention: 128 = fully opaque)
    }
  }
  return result;
}

/** Based on GsKit texture loading - thank you guys! */
TextureBuilderData* PngLoader::load(const char* fullPath) {
  std::string path = fullPath;
  TYRA_ASSERT(!path.empty(), "Provided path is empty!");

  auto filename = FileUtils::getFilenameFromPath(path);

  FILE* file = fopen(fullPath, "rb");
  // Modified by TyraX: a missing texture is no longer fatal - log it (the
  // editor surfaces it) and return a placeholder so the game keeps running.
  if (file == nullptr) {
    TYRA_SOFT_ERROR("Failed to load texture: ", fullPath,
                    " (using a placeholder so the game keeps running)");
    return makePlaceholderTexture(filename);
  }

  std::vector<u8> fileData;
  {
    const size_t chunk = 64 * 1024;
    size_t used = 0;
    for (;;) {
      fileData.resize(used + chunk);
      const size_t got = fread(fileData.data() + used, 1, chunk, file);
      used += got;
      if (got < chunk) break;
    }
    fileData.resize(used);
  }
  fclose(file);
  // Modified by TyraX: empty/truncated file -> placeholder, not a crash.
  if (fileData.empty()) {
    TYRA_SOFT_ERROR("Empty texture file: ", fullPath, " (using a placeholder)");
    return makePlaceholderTexture(filename);
  }
  PngMemorySource source{fileData.data(), fileData.size(), 0};

  png_structp pngPtr;
  png_infop infoPtr;
  png_uint_32 width, height;
  png_bytep* rowPointers = nullptr;

  u32 sigRead = 0;
  int bitDepth, colorType, interlaceType;

  pngPtr = png_create_read_struct(PNG_LIBPNG_VER_STRING, (png_voidp) nullptr,
                                  nullptr, nullptr);
  TYRA_ASSERT(pngPtr, "PNG read struct init failed for: ", filename);

  infoPtr = png_create_info_struct(pngPtr);
  TYRA_ASSERT(infoPtr, "PNG read struct init failed for: ", filename);

  TYRA_ASSERT(!setjmp(png_jmpbuf(pngPtr)), "PNG read error for: ", filename);

  png_set_read_fn(pngPtr, &source, pngReadFromMemory);
  png_set_sig_bytes(pngPtr, sigRead);
  png_read_info(pngPtr, infoPtr);
  png_get_IHDR(pngPtr, infoPtr, &width, &height, &bitDepth, &colorType,
               &interlaceType, nullptr, nullptr);

  if (bitDepth == 16) png_set_strip_16(pngPtr);
  if (colorType == PNG_COLOR_TYPE_GRAY && bitDepth < 4) png_set_expand(pngPtr);

  png_set_filler(pngPtr, 0xff, PNG_FILLER_AFTER);

  png_read_update_info(pngPtr, infoPtr);

  auto* result = new TextureBuilderData();
  result->width = width;
  result->height = height;
  result->name = filename;

  auto updatedColorType = png_get_color_type(pngPtr, infoPtr);

  if (updatedColorType == PNG_COLOR_TYPE_PALETTE) {
    handlePalletized(result, pngPtr, infoPtr, rowPointers, bitDepth);
  } else if (updatedColorType == PNG_COLOR_TYPE_RGB_ALPHA) {
    handle32bpp(result, pngPtr, infoPtr, rowPointers);
  } else if (updatedColorType == PNG_COLOR_TYPE_RGB) {
    handle24bpp(result, pngPtr, infoPtr, rowPointers);
  } else
    TYRA_TRAP("This texture depth is not supported!");

  png_read_end(pngPtr, nullptr);
  png_destroy_read_struct(&pngPtr, &infoPtr, nullptr);

  return result;
}

void PngLoader::handle32bpp(TextureBuilderData* result, png_structp pngPtr,
                            png_infop infoPtr, png_bytep* rowPointers) {
  int rowBytes = png_get_rowbytes(pngPtr, infoPtr);

  result->gsComponents = TEXTURE_COMPONENTS_RGBA;
  result->bpp = bpp32;
  result->data = static_cast<unsigned char*>(memalign(
      128, getTextureSize(result->width, result->height, result->bpp)));

  rowPointers =
      static_cast<png_bytep*>(calloc(result->height, sizeof(png_bytep)));

  for (int row = 0; row < result->height; row++)
    rowPointers[row] = static_cast<png_bytep>(malloc(rowBytes));

  png_read_image(pngPtr, rowPointers);

  struct PngPixel4* pixels = (struct PngPixel4*)result->data;

  int k = 0;
  for (int i = 0; i < result->height; i++) {
    for (int j = 0; j < result->width; j++) {
      pixels[k].r = rowPointers[i][4 * j];
      pixels[k].g = rowPointers[i][4 * j + 1];
      pixels[k].b = rowPointers[i][4 * j + 2];
      pixels[k++].a = ((int)rowPointers[i][4 * j + 3] * 128 / 255);
    }
  }

  for (int row = 0; row < result->height; row++) free(rowPointers[row]);

  free(rowPointers);
}

void PngLoader::handle24bpp(TextureBuilderData* result, png_structp pngPtr,
                            png_infop infoPtr, png_bytep* rowPointers) {
  int rowBytes = png_get_rowbytes(pngPtr, infoPtr);

  result->gsComponents = TEXTURE_COMPONENTS_RGB;
  result->bpp = bpp24;
  result->data = static_cast<unsigned char*>(memalign(
      128, getTextureSize(result->width, result->height, result->bpp)));

  rowPointers =
      static_cast<png_bytep*>(calloc(result->height, sizeof(png_bytep)));

  for (int row = 0; row < result->height; row++)
    rowPointers[row] = static_cast<png_bytep>(malloc(rowBytes));

  png_read_image(pngPtr, rowPointers);

  struct PngPixel3* pixels = (struct PngPixel3*)result->data;

  int k = 0;
  for (int i = 0; i < result->height; i++) {
    for (int j = 0; j < result->width; j++) {
      pixels[k].r = rowPointers[i][4 * j];
      pixels[k].g = rowPointers[i][4 * j + 1];
      pixels[k++].b = rowPointers[i][4 * j + 2];
    }
  }

  for (int row = 0; row < result->height; row++) free(rowPointers[row]);

  free(rowPointers);
}

void PngLoader::handlePalletized(TextureBuilderData* result, png_structp pngPtr,
                                 png_infop infoPtr, png_bytep* rowPointers,
                                 const int& bitDepth) {
  png_colorp palette = nullptr;
  png_bytep trans = nullptr;
  int numPallete = 0;
  int numTrans = 0;

  png_get_PLTE(pngPtr, infoPtr, &palette, &numPallete);
  png_get_tRNS(pngPtr, infoPtr, &trans, &numTrans, nullptr);
  result->clutBpp = bpp32;
  result->clutGsComponents = TEXTURE_COMPONENTS_RGBA;

  if (bitDepth == 4) {
    handle4bppPalletized(result, pngPtr, infoPtr, rowPointers, palette, trans,
                         numPallete, numTrans);
  } else if (bitDepth == 8) {
    handle8bppPalletized(result, pngPtr, infoPtr, rowPointers, palette, trans,
                         numPallete, numTrans);
  } else {
    TYRA_TRAP("Only 4 and 8 bits palettes are supported");
  }
}

void PngLoader::handle8bppPalletized(TextureBuilderData* result,
                                     png_structp pngPtr, png_infop infoPtr,
                                     png_bytep* rowPointers, png_colorp palette,
                                     png_bytep trans, const int& numPallete,
                                     const int& numTrans) {
  int rowBytes = png_get_rowbytes(pngPtr, infoPtr);
  result->bpp = bpp8;
  result->clutWidth = 16;
  result->clutHeight = 16;

  result->gsComponents = TEXTURE_COMPONENTS_RGBA;
  result->data = static_cast<unsigned char*>(memalign(
      128, getTextureSize(result->width, result->height, result->bpp)));

  rowPointers =
      static_cast<png_bytep*>(calloc(result->height, sizeof(png_bytep)));

  for (int row = 0; row < result->height; row++)
    rowPointers[row] = static_cast<png_bytep>(malloc(rowBytes));

  png_read_image(pngPtr, rowPointers);

  result->clut =
      static_cast<unsigned char*>(memalign(128, getTextureSize(16, 16, bpp32)));
  memset(result->clut, 0, getTextureSize(16, 16, bpp32));

  auto* pixel = static_cast<unsigned char*>(result->data);
  struct PngClut* clut = (struct PngClut*)result->clut;

  for (int i = 0; i < numPallete; i++) {
    clut[i].r = palette[i].red;
    clut[i].g = palette[i].green;
    clut[i].b = palette[i].blue;
    clut[i].a = 0x80;
  }

  for (int i = 0; i < numTrans; i++) clut[i].a = trans[i] >> 1;

  // rotate clut
  for (int i = 0; i < numPallete; i++) {
    if ((i & 0x18) == 8) {
      struct PngClut tmp = clut[i];
      clut[i] = clut[i + 8];
      clut[i + 8] = tmp;
    }
  }

  int k = 0;
  for (int i = 0; i < result->height; i++) {
    for (int j = 0; j < result->width; j++) {
      memcpy(&pixel[k++], &rowPointers[i][1 * j], 1);
    }
  }

  for (int row = 0; row < result->height; row++) free(rowPointers[row]);

  free(rowPointers);
}

void PngLoader::handle4bppPalletized(TextureBuilderData* result,
                                     png_structp pngPtr, png_infop infoPtr,
                                     png_bytep* rowPointers, png_colorp palette,
                                     png_bytep trans, const int& numPallete,
                                     const int& numTrans) {
  int rowBytes = png_get_rowbytes(pngPtr, infoPtr);
  result->bpp = bpp4;
  result->clutWidth = 8;
  result->clutHeight = 2;
  result->gsComponents = TEXTURE_COMPONENTS_RGBA;
  result->data = static_cast<unsigned char*>(memalign(
      128, getTextureSize(result->width, result->height, result->bpp)));

  rowPointers =
      static_cast<png_bytep*>(calloc(result->height, sizeof(png_bytep)));

  for (int row = 0; row < result->height; row++)
    rowPointers[row] = static_cast<png_bytep>(malloc(rowBytes));

  png_read_image(pngPtr, rowPointers);

  result->clut =
      static_cast<unsigned char*>(memalign(128, getTextureSize(8, 2, bpp32)));
  memset(result->clut, 0, getTextureSize(8, 2, bpp32));

  auto* pixel = static_cast<unsigned char*>(result->data);
  struct PngClut* clut = (struct PngClut*)result->clut;

  for (int i = 0; i < numPallete; i++) {
    clut[i].r = palette[i].red;
    clut[i].g = palette[i].green;
    clut[i].b = palette[i].blue;
    clut[i].a = 0x80;
  }

  for (int i = 0; i < numTrans; i++) clut[i].a = trans[i] >> 1;

  int k = 0;
  for (int i = 0; i < result->height; i++) {
    for (int j = 0; j < result->width / 2; j++)
      memcpy(&pixel[k++], &rowPointers[i][1 * j], 1);
  }
  unsigned char* tmpdst = (unsigned char*)result->data;
  unsigned char* tmpsrc = (unsigned char*)pixel;

  for (u32 byte = 0;
       byte < getTextureSize(result->width, result->height, result->bpp);
       byte++)
    tmpdst[byte] = (tmpsrc[byte] << 4) | (tmpsrc[byte] >> 4);

  for (int row = 0; row < result->height; row++) free(rowPointers[row]);

  free(rowPointers);
}

}  // namespace Tyra
