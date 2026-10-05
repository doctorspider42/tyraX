/*
# _____        ____   ___
#   |     \/   ____| |___|
#   |     |   |   \  |   |
#-----------------------------------------------------------------------
# Copyright 2022, tyra - https://github.com/h4570/tyra
# Licensed under Apache License 2.0
# Wellington Carvalho <wellcoj@gmail.com>
*/

#include "debug/debug.hpp"
#include <tamtypes.h>
#include <cstdio>
#include <kernel.h>

#include <unistd.h>
#include <cctype>
#include <cstring>
#include "file/file_utils.hpp"

namespace Tyra {

FileUtils::FileUtils() {
  getcwd(cwd, sizeof(cwd));
  setPathInfo(cwd);
}

FileUtils::~FileUtils() {}

void FileUtils::setPathInfo(const char* path) {
  char* ptr;

  strcpy(this->elfName, path);
  strcpy(this->elfPath, path);

  ptr = strrchr(this->elfPath, '/');
  if (ptr == nullptr) {
    ptr = strrchr(this->elfPath, '\\');
    if (ptr == nullptr) {
      ptr = strrchr(this->elfPath, ':');
      if (ptr == nullptr) {
        TYRA_TRAP("Did not find path! PATH: ", path);
      }
    }
  }

  ptr++;
  *ptr = '\0';
}

std::string FileUtils::getCwd() {
  std::string result;
  char _cwd[NAME_MAX];
  // On failure getcwd() leaves the buffer untouched, so reading it would hand
  // every caller a path made of stack garbage.
  if (getcwd(_cwd, sizeof(_cwd)) == nullptr) return result;
  result = _cwd;
  return result;
}

std::string FileUtils::fromCwd(const std::string& relativePath) {
  return fromCwd(relativePath.c_str());
}

std::string FileUtils::fromCwd(const char* file) {
  auto cwd = getCwd();
  // Whether getcwd() ends with a separator is not something to rely on: the
  // ps2sdk the stock Tyra image carries returns "host:/dir/bin/", a current one
  // returns "host:/dir/bin" - and this used to be a plain concatenation, so on
  // the newer SDK every path came out as ".../binlivepad.bin" and PCSX2 refused
  // it ("Denying access to path outside of ELF directory"), which reads as a
  // sandbox problem rather than a missing slash. A trailing ':' is left alone:
  // "host:" + "file.txt" is a valid path and must not gain one.
  if (!cwd.empty() && cwd.back() != '/' && cwd.back() != '\\' &&
      cwd.back() != ':')
    cwd += '/';
  auto path = cwd + file;

  // ISO9660 (cdrom0:) stores names upper-case with a ";1" version suffix and
  // the CD driver expects '\' separators. host:/mass:/mc0: take paths as-is.
  if (path.rfind("cdrom", 0) == 0) {
    auto rel = path.find(':');
    rel = (rel == std::string::npos) ? 0 : rel + 1;
    for (auto i = rel; i < path.size(); i++) {
      if (path[i] == '/') {
        path[i] = '\\';
      } else {
        path[i] = std::toupper(static_cast<unsigned char>(path[i]));
      }
    }
    if (path.find(';', rel) == std::string::npos) path += ";1";
  }

  return path;
}

std::string FileUtils::getFilenameFromPath(const std::string& path) {
  std::string filename = path.substr(path.find_last_of("/\\") + 1);
  if (filename.size() == path.size()) {
    filename = path.substr(path.find_last_of(":\\") + 1);
  }
  // Drop the ISO9660 file version ("USE.PNG;1" -> "USE.PNG")
  auto version = filename.find_last_of(';');
  if (version != std::string::npos) filename = filename.substr(0, version);
  return filename;
}

std::string FileUtils::getPathFromFilename(const std::string& path) {
  std::string basepath = path.substr(0, path.find_last_of("/\\"));
  if (basepath.size() == path.size()) {
    basepath = path.substr(0, path.find_last_of(":\\"));
  }
  return basepath;
}

std::string FileUtils::getFilenameWithoutExtension(
    const std::string& filename) {
  auto lastindex = filename.find_last_of(".");
  return filename.substr(0, lastindex);
}

std::string FileUtils::getExtensionOfFilename(const std::string& filename) {
  auto lastindex = filename.find_last_of(".");
  auto extension = filename.substr(lastindex + 1);
  // Drop the ISO9660 file version ("PNG;1" -> "PNG")
  auto version = extension.find_last_of(';');
  if (version != std::string::npos) extension = extension.substr(0, version);
  return extension;
}

}  // namespace Tyra
