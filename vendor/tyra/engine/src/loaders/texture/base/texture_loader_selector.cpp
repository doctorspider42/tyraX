/*
# _____        ____   ___
#   |     \/   ____| |___|
#   |     |   |   \  |   |
#-----------------------------------------------------------------------
# Copyright 2022, tyra - https://github.com/h4570/tyra
# Licensed under Apache License 2.0
# Sandro Sobczyński <sandro.sobczynski@gmail.com>
*/

#include <algorithm>
#include <string>
#include "debug/debug.hpp"
#include "file/file_utils.hpp"
#include "renderer/core/texture/models/texture.hpp"
#include "loaders/texture/base/texture_loader_selector.hpp"

namespace Tyra {

TextureLoaderSelector::TextureLoaderSelector() {}

TextureLoaderSelector::~TextureLoaderSelector() {}

// ----
// Methods
// ----

TextureLoader& TextureLoaderSelector::getLoaderByFileName(
    const char* fullpath) {
  // Handles ISO9660 versioned names too ("USE.PNG;1" -> "PNG")
  return getLoaderByExtension(FileUtils::getExtensionOfFilename(fullpath));
}

TextureLoader& TextureLoaderSelector::getLoaderByExtension(
    const std::string& extension) {
  std::string extensionLower = extension;
  std::transform(extensionLower.begin(), extensionLower.end(),
                 extensionLower.begin(), ::tolower);
  if (extensionLower == "png") {
    return pngLoader;
  } else {
    TYRA_TRAP("There is no texture loader for extension: ", extensionLower);
    return pngLoader;
  }
}

}  // namespace Tyra
