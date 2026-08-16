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
// Materials, atlas UV rects, crease-smoothed normals and bin-relative texture
// paths are resolved at bake time, so the PS2 side is a sequential read plus a
// memcpy.
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

// Texture hot reload (docs/live-link.md): the bin/-relative paths the GAME
// knows a res/-relative texture as when an ANIMATED model carries it.
// bakeAnimAssets extracts every texture an override .mtl names into a RENAMED
// copy next to the model's .tskl ("models/<stem>_<basename>.png"), and that
// baked path - not the texture's own location - is what the .tskl stores and
// the game loads. So a repaint has to be announced under it, and under one
// per model when several animated models share the texture. Empty for the
// ordinary case (nothing animated uses the file: it ships where it lives).
std::vector<std::string> animTextureAliases(const Project& p,
                                            const std::string& texResRel);

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

// True when any scene can show the camera flashlight (a Player object with it
// enabled, or a Set Flashlight node that could switch one on at runtime).
// Gates the res/hud/flashlight-gobo.png bake and FLASHLIGHT_USED in
// scene_data.hpp - keep them equal, like projectUsesFlare and FLARE_USED.
bool projectUsesFlashlight(const Project& p);

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

// ...and the one whose starfield the single shared STARS table is generated
// from. Null when no scene has stars on.
const DayCycle* projectStarCycle(const Project& p);

// The C++ namespace the generated sources and every user script live in,
// derived from the project name. Exposed so a build can check a user-owned
// script against it (renaming a project does not rewrite user-owned files).
std::string projectNamespace(const Project& p);

// Content of a new user script created from the "New script..." action.
std::string scriptStub(const Project& p, const std::string& className,
                       const std::string& fileName);

// The Docker volume holding the COMPILED engine, named after a hash of the
// engine source path so projects built from one checkout share it and parallel
// checkouts do not. Declared `external` in the generated docker-compose.yml -
// it outlives any one project - so somebody has to create it: the runner does,
// before `compose up`.
std::string engineVolumeName();

// Content of a new VU program (src/vu/*.cpp) - C++ compiled and RUN on the host
// at build time, leaving a VU1 microprogram behind (docs/vu-authoring.md).
std::string vuScriptStub(const std::string& className);

// ... and of a new VU0 kernel (src/vu0/*.cpp): the same C++, on the other
// vector unit, leaving a microprogram and the EE driver that calls it.
std::string vuKernelStub(const std::string& className);

// How many save slots the generated save system exposes. The twin of the
// emitted `SAVE_SLOTS` in saveSystemHeader - change both together.
// The slot count a project ships, clamped to what the model allows. Was a
// fixed 3 until the count became configurable; kSaveSlots survives only as
// the default a fresh Project starts at.
constexpr int kSaveSlots = 3;
int saveSlotCount(const Project& p);
// Rows the save menu shows at once, and how many pages that makes.
int saveSlotsPerPage(const Project& p);
int saveSlotPages(const Project& p);

// What one memory card save slot holds and costs, mirroring the generated
// SaveGameData layout byte for byte (the Save Editor's size estimate; the
// same buffer is the in-RAM checkpoint). Keep in sync with saveSystemHeader
// and the SAVE_* tables in sceneDataContent.
struct SaveSizeInfo {
    int values = 0;       // Project::saveValues entries
    int texts = 0;        // Project::saveTexts entries
    int objectSlots = 0;  // SAVE_OBJECT_MAX (max save-flagged count, min 1)
    int headerBytes = 0;  // magic/version/scene/player + the three counters
    int valuesBytes = 0;
    int textsBytes = 0;
    int objectsBytes = 0;
    // World Facts (docs/world-facts.md). `facts` counts the catalog entries
    // that ride a slot - checkpoint- and save-lived, computed and
    // scene-scoped ones excluded because they have nothing to store - and
    // each row is an id plus three floats.
    int facts = 0;
    int factsBytes = 0;
    int payloadBytes = 0;  // the slot file: sum above, 64-byte aligned
    int iconSysBytes = 0;  // icon.sys, written once per card
    int iconIcnBytes = 0;  // list.icn, written once per card
    int iconBytes = 0;     // iconSysBytes + iconIcnBytes (raw sum)
    // What the card actually loses. A PS2 memory card allocates in 1 KB
    // clusters and no two files share one, so every file costs at least a
    // full cluster and the save's own directory costs another. Summing the
    // raw byte sizes understates real usage several times over for a save
    // this small (a 128-byte slot still eats 1 KB), which is the whole point
    // of reporting it separately from the byte breakdown.
    int cardClusterBytes = 0;    // the cluster size the rounding used
    // The PROFILE is a file of its own beside the slots, so it costs its own
    // cluster - 0 when the catalog declares no profile-lived fact.
    int profileFacts = 0;
    int profileBytes = 0;
    int cardFootprintBytes = 0;  // directory + slots + icons + profile
};
SaveSizeInfo saveSizeInfo(const Project& p);

// The game's save directory on the memory card ("/TYRA-<NAME>").
std::string saveDirName(const Project& p);

// A File::relativePath as a filesystem path. The generator writes '\\'
// separators (and hundreds of call sites compare against literals spelled that
// way), but a backslash is an ordinary FILENAME CHARACTER on POSIX - writing
// one straight out produces a single file literally called "src\\gen\\x.cpp"
// instead of the directory tree. Every place a relativePath meets the file
// system goes through here.
std::filesystem::path nativePath(const std::string& relativePath);

// `.vscode/extensions.json` with the ids the editor knows about ensured
// present, given whatever the project already has. "" when nothing needs
// adding, so an unchanged file is never rewritten. See the definition on why
// this one file is merged rather than written once.
std::string vscodeExtensionsMerged(const std::string& existing);

}  // namespace templates
