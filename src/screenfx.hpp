// Custom screen effects: project-defined full-screen post effects, loaded from
// <project>/screen-effects/*.screenfx text files (parallel to custom flow
// nodes - see flowgraph.hpp / flownode.cpp / docs/custom-screen-effects.md).
//
// Like the built-in bloom / film grain, a custom effect is a low-level GS
// framebuffer blit that composites over the frame (no pixel shaders on the
// PS2). The file carries a small manifest (title + up to four numeric params)
// and the raw C++ body that draws the effect; the body is emitted into
// src/scripts/screen_fx.gen.cpp and run via RendererCore::applyCustomPostFx at
// the effect's slot in the UI Editor screen stack.
//
// A Project references an effect only by placement (ScreenFxPlacement, in
// project.hpp): the effect's identity is its file stem ("vignette.screenfx" ->
// "custom:vignette"), so the .screenfx file must travel with the project.

#pragma once

#include <memory>
#include <string>
#include <vector>

// One loaded .screenfx file: the manifest plus the raw effect body. The body
// is C++ emitted verbatim (with {p0}..{p3} substituted for the runtime params)
// into a customFx_<key> function; see screenFxSource() in templates.cpp.
struct CustomScreenFx {
    std::string key;         // "custom:<file-stem>" - the serialized placement key
    std::string title;       // display name in the UI Editor stack
    std::string sourceFile;  // absolute path of the .screenfx file (diagnostics)
    std::string code;        // raw C++ effect body (everything after the --- line)

    int paramCount = 0;                 // number of numeric params (0..4)
    std::string paramLabel[4];          // slider labels
    float paramDefault[4] = {0, 0, 0, 0};
    float paramMin[4] = {0, 0, 0, 0};
    float paramMax[4] = {1, 1, 1, 1};
};

// The global custom-effect registry, rebuilt on every project load
// (screenfx::loadForProject) and on the UI Editor's "Reload from folder".
// unique_ptr keeps each entry's address stable while the UI holds pointers.
inline std::vector<std::unique_ptr<CustomScreenFx>>& customScreenEffects() {
    static std::vector<std::unique_ptr<CustomScreenFx>> reg;
    return reg;
}

inline const CustomScreenFx* customScreenFx(const std::string& key) {
    for (const auto& c : customScreenEffects())
        if (key == c->key) return c.get();
    return nullptr;
}

namespace screenfx {

// Absolute path of a project's effect folder (<projectDir>/screen-effects).
std::string dirForProject(const std::string& projectDir);

// Rebuilds the global customScreenEffects() registry from every *.screenfx file
// in dirForProject(projectDir). Called by project::load BEFORE the placements
// are read (unknown keys are dropped), and again by the UI Editor's "Reload
// from folder". Returns a short human-readable summary / error list.
std::string loadForProject(const std::string& projectDir);

// Writes a commented starter template into screen-effects/example.screenfx
// (never overwriting an existing file). Returns the file path, or an error
// string prefixed with "error:".
std::string writeExample(const std::string& projectDir);

}  // namespace screenfx
