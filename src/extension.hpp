#pragma once

#include <string>
#include <vector>

// Script extensions (docs/script-extensions.md): a folder of reusable game
// code - object scripts, global scripts, helpers and custom flow nodes - that
// moves between projects as a copy. An extension lives in
//
//   <project>/src/extensions/<name>/
//       extension.json       {"name", "version", "description", "author"}
//       *.cpp / *.hpp        compiled with the game (the Makefile builds all of src/)
//       flow-nodes/*.flownode  custom nodes, loaded like the project's own
//
// Inside src/ on purpose: the engine Makefile already compiles every .cpp under
// it, so an installed extension needs no build change at all, and the editor's
// script scans (attach list, [[tyra::prop]] fields, events, sounds) read the
// extensions folder beside src/scripts.
namespace extension {

struct Info {
    std::string folder;       // the directory name under src/extensions
    std::string name;         // manifest "name" (falls back to the folder)
    std::string version;
    std::string description;
    std::string author;
    int sources = 0;          // .cpp files
    int flowNodes = 0;        // .flownode files
    std::vector<std::string> objectScripts;  // TYRA_OBJECT_SCRIPT classes
    std::string problem;      // "" = fine; else why it is not usable as is
};

// <project>/src/extensions
std::string rootDir(const std::string& projectDir);

// The directories the editor scans for scripts: src/scripts and every
// installed extension.
std::vector<std::string> scriptDirs(const std::string& projectDir);

// Every installed extension, sorted by folder.
std::vector<Info> list(const std::string& projectDir);

// Copies `sourceDir` (a folder holding extension.json) into the project.
// Returns "" on success, else why not - an extension of the same folder name is
// never overwritten, and the source must not be inside the project already.
std::string install(const std::string& projectDir, const std::string& sourceDir,
                    std::string* installedFolder = nullptr);

// Writes a new extension skeleton (manifest, one object script showing the
// API and a public field, one custom node). "" on success.
std::string scaffold(const std::string& projectDir, const std::string& name);

// Deletes an installed extension's folder. "" on success.
std::string remove(const std::string& projectDir, const std::string& folder);

// A folder name for a display name: lowercase, '-' for anything else.
std::string folderFor(const std::string& name);

}  // namespace extension
