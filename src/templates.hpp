#pragma once

#include <filesystem>
#include <string>
#include <vector>

// Forward-declared rather than #included: this header is pulled in widely and
// project.hpp is not cheap. Callers that actually dereference these already
// include it.
struct Project;
struct SceneData;
struct DayCycle;

namespace templates {

struct File {
    std::string relativePath;  // path inside project dir, '\\' separated
    std::string content;
};

// All files generated for a new Tyra game project (sources, Makefile,
// docker-compose, run scripts...) with project values substituted.
std::vector<File> generate(const Project& p);

// Compiles the project flow graph into a C++ script (flow_graph.gen.cpp).
std::string flowGraphScript(const Project& p);

// The build artifact a model asset actually ships as: the baked .tmdl for a
// static .obj, the baked .tskl for an animated .glb/.fbx, or the path itself
// when it is neither. `materialPath` is the object's .mtl override ("" = none)
// - it is resolved into the artifact, so it changes the file name. Callers
// that reason about shipped files (ISO load ordering) need this rather than
// the source path.
std::string bakedModelPath(const std::string& modelPath,
                           const std::string& materialPath);

// Bakes every static model (.obj) referenced by the project into the binary
// .tmdl the game loads (res/models/<stem>.tmdl, or <stem>__ovr<hash>.tmdl for
// a per-object .mtl override) - see src/tmdl.hpp and docs/model-pipeline.md.
// Materials, atlas UV rects, flat normals and bin-relative texture paths are
// resolved at bake time, so the PS2 side is a sequential read plus a memcpy.
// A model that cannot be parsed is reported in `warnings` and skipped (the
// game warns and renders nothing for it - the same soft-fail as .tskl).
std::vector<File> bakeStaticModels(const Project& p,
                                   std::vector<std::string>* warnings = nullptr);

// Bakes every animated model (.glb) referenced by the project into the
// .tanm morph-frame binaries + extracted PNG textures the game loads
// (res/models/<stem>.tanm, res/models/<stem>_<image>.png). Bake failures
// are reported in `warnings` and the model is skipped (the game warns and
// renders nothing for it - same soft-fail as missing textures).
std::vector<File> bakeAnimAssets(const Project& p,
                                 std::vector<std::string>* warnings = nullptr);

// Built-in assets for the "FPP showcase" template.
const char* houseObjText();
const unsigned char* crosshairPng(size_t& size);

// Built-in "USE" prompt sprite, shipped into every project (res/hud/use.png).
const unsigned char* usePromptPng(size_t& size);

// Built-in "PICK UP" prompt sprite (res/hud/pickup.png), shown instead of
// the USE prompt while the looked-at object is pickable.
const unsigned char* pickPromptPng(size_t& size);

// Built-in "LOADING..." sprite (res/hud/loading.png), shown centered on
// black during scene switches when the loading screen is enabled.
const unsigned char* loadingPng(size_t& size);

// 8x8 glyph strip for in-game HUD text (res/hud/debugfont.png): the
// debug-profile overlays and the video-mode confirm prompt. Written on
// every generated-file refresh. 42 glyphs (digits, letters, symbols) in
// two rows of 32 cells of 16px each (right half of a cell transparent);
// the order must match drawHudText's atlas in the game template.
const std::vector<unsigned char>& debugFontPng();

// Built-in save-menu sprites (res/hud/save-*.png), written when missing.
// The engine has no text rendering - the menu text is baked into these.
struct BuiltinAsset {
    const char* fileName;  // name inside res/hud/
    const unsigned char* data;
    size_t size;
};
const std::vector<BuiltinAsset>& saveMenuAssets();

// True when any scene can show the sun lens flare (an authored per-scene
// amount > 0, or a Set Flare flow node that could raise it at runtime).
// Gates both the res/hud/flare-*.png bake (refreshGenerated) and the
// game-side texture load (FLARE_USED in scene_data.hpp) - keep them equal.
bool projectUsesFlare(const Project& p);

// True when any scene has a Point Light with a visible beam (corona/cone).
// Gates the res/hud/flare-corona.png bake and BEAMS_USED in scene_data.hpp.
bool projectUsesBeams(const Project& p);

// The enabled day/night cycle a scene resolves to through its ambience preset,
// or null (docs/day-night-cycle.md). The single answer codegen, the sky-disc
// bake and the editor all ask - never walk the presets by hand.
const DayCycle* sceneDayCycle(const Project& p, const SceneData& sc);

// True when ANY scene resolves to an enabled cycle. Gates the
// res/hud/{sun,moon}-disc.png bake (refreshGenerated) and the game-side texture
// load (DAYCYCLE_USED in scene_data.hpp) - keep the two equal, the way
// projectUsesFlare and FLARE_USED are.
bool projectUsesDayCycle(const Project& p);

// The cycle whose moon phase/texture the single shared moon disc is baked from
// (the first scene that resolves to one), or null.
const DayCycle* projectMoonCycle(const Project& p);

// Content of a new user script created from the "New script..." action.
std::string scriptStub(const Project& p, const std::string& className,
                       const std::string& fileName);

// True when `content` is byte-identical to what an older editor version
// generated for this file - i.e. the user never edited it and it is safe
// to regenerate even though it predates the ownership marker.
// A File::relativePath as a filesystem path. The generator writes '\\'
// separators (and hundreds of call sites compare against literals spelled that
// way), but a backslash is an ordinary FILENAME CHARACTER on POSIX - writing
// one straight out produces a single file literally called "src\\gen\\x.cpp"
// instead of the directory tree. Every place a relativePath meets the file
// system goes through here.
std::filesystem::path nativePath(const std::string& relativePath);

bool matchesLegacy(const Project& p, const std::string& relativePath,
                   const std::string& content);

}  // namespace templates
