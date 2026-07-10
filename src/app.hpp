#pragma once

#include <map>
#include <string>

#include "history.hpp"
#include "isoexport.hpp"
#include "project.hpp"
#include "runner.hpp"
#include "viewport.hpp"

struct GLFWwindow;

class App {
public:
    // initialProjectDir: optional project to open on startup (may be empty)
    int run(const std::string& initialProjectDir = "");

private:
    void drawUI();
    void drawMenuBar();
    void drawViewportWindow();
    void drawProjectWindow();
    void drawPropertiesWindow();
    void drawSceneSection();
    void drawScriptsSection();
    void drawNewScriptModal();
    void drawNewSceneModal();
    void drawDeleteSceneModal();
    void drawFlowGraphWindow();
    // Names used by same-type "Variables" nodes across every scene's graphs
    // (the int / bool / position namespaces are separate).
    std::vector<std::string> flowVarNames(const std::string& nodeType) const;
    void openInVSCode();
    void drawOutputWindow();
    void drawDebugWindow();
    void drawDiscLayoutWindow();
    void drawNewProjectModal();
    void drawPreferencesModal();
    void drawScenePreferencesModal();
    void openScenePreferences();  // stage the active scene into scenePref* + open
    void openProjectDialog();
    void applyProjectToViewport();
    void addObject(PrimitiveType type);
    void addEmitter(int kind);  // Effects menu presets (fire/smoke/fog/sparks)
    void addSoundEmitter();
    void addPointLight();
    void addSavePoint();
    void drawAddObjectMenu();
    // Copies a picked .obj (with its .mtl + textures, references rewritten to
    // the sanitized names) into res/models. Returns the project-relative path
    // of the model, or "" when cancelled/failed. Does NOT create an object.
    std::string importModelAsset();
    // Copies a picked PNG into res/textures (terrain tiling); "" on cancel.
    std::string importTextureAsset();
    // Copies a picked .mtl (with its map_Kd textures, references rewritten to
    // the sanitized names) into res/materials; "" when cancelled/failed.
    std::string importMaterialAsset();
    // All .mtl assets an object can use: res/materials + res/models, as
    // project-relative paths ("res/materials/walls.mtl")
    std::vector<std::string> listMaterialAssets();
    // Combo picking an .mtl for the object (primitives: surface; models:
    // override). Returns true when materialPath changed.
    bool drawMaterialCombo(SceneObject& o);
    // Creates a scene object for a model already in res/models (no copying)
    void addModelObject(const std::string& relPath);
    // Project-panel section listing res/models + res/textures with the
    // Import... buttons (the object pickers only offer what is listed here)
    void drawAssetsSection();
    // Files directly under res/<subdir> with the given extension (lowercase
    // compare), names only, sorted by the directory iteration order
    std::vector<std::string> listAssetFiles(const char* subdir, const char* ext);
    // "Pick..." button + popup listing res/textures; true when path changed
    bool pickProjectTexture(const char* popupId, std::string& path);
    // Cached objparser summary of a model (for the properties panel)
    struct ModelInfo {
        bool ok = false;
        int tris = 0;
        struct MaterialLine {
            std::string text;      // "name (texture.png)" / "name (color)"
            bool missing = false;  // the referenced texture file is absent
        };
        std::vector<MaterialLine> materials;
        bool anyMissing = false;
    };
    std::map<std::string, ModelInfo> modelInfoCache_;
    // materialRel: .mtl override applied to the model ("" = its own)
    const ModelInfo& modelInfo(const std::string& relPath,
                               const std::string& materialRel = "");
    // Cached summary of an animated .glb (clip names for the properties
    // panel; a fresh bake without keeping the geometry)
    struct GlbInfo {
        bool ok = false;
        std::string error;
        std::vector<std::string> clips;
        int vertexCount = 0, frameCount = 0;
        std::vector<std::string> warnings;
    };
    std::map<std::string, GlbInfo> glbInfoCache_;
    const GlbInfo& glbInfo(const std::string& relPath);
    // Summary of a standalone .mtl (material lines + missing-texture flags)
    const ModelInfo& materialInfo(const std::string& relPath);
    // Mirrors res/audio + res/sfx into the music/sounds lists (manual drops
    // are picked up, vanished files are dropped). announce: status even when
    // nothing changed.
    void rescanAssets(bool announce);
    // Cached format problem of a project WAV ("" = fine). sfx = adpenc rules
    // (16-bit PCM 22050 Hz); music = the song player rules.
    const std::string& wavIssue(const std::string& relPath, bool sfx);
    std::map<std::string, std::string> wavIssueCache_;
    void drawHudSection();
    void importHudImage();
    void drawMusicSection();
    void importMusicTrack();
    void drawSoundsSection();
    void importSoundEffect();
    void drawSaveDataSection();
    void drawMenusWindow();
    void handleFileDrop(int count, const char** paths);
    void saveProject();

    // Editing model: mutate project_ freely, then commitChange() once per
    // logical action - it pushes an undo snapshot and saves everything.
    void commitChange();
    void saveAll(const char* status);
    void applySnapshot(const SceneSnapshot& s);
    void undo();
    void redo();
    void copyObject();
    void pasteObject();
    void attachProject();  // post-open: history + solution state

    GLFWwindow* window_ = nullptr;

    Project project_;
    bool hasProject_ = false;
    // Set by attachProject(): apply project_.windowLayout at the next frame
    // boundary (ImGui cannot reload settings between NewFrame and EndFrame).
    bool layoutLoadPending_ = false;
    int selectedObject_ = -1;

    // Layouts saved before the Properties window existed lack a slot for it;
    // when set, the next frame docks it under the Project panel.
    bool dockPropertiesPending_ = false;

    // Transform gizmo: 0 = move, 1 = rotate, 2 = scale
    int gizmoOp_ = 0;
    bool gizmoWasUsing_ = false;

    // Terrain sculpting brush
    bool sculptMode_ = false;
    float brushRadius_ = 5.0f;
    float brushStrength_ = 0.08f;  // units per frame at the brush center
    bool sculptStroke_ = false;    // an LMB stroke is in progress
    bool sculptFlatten_ = false;   // level toward flattenHeight_ instead of raise
    float flattenHeight_ = 0.0f;   // flatten target height (world units)

    History history_;
    SceneObject clipboard_;
    bool hasClipboard_ = false;

    // Flow graph editor state
    int flowGraphObject_ = -1;           // object whose graph is open in the editor
    bool flowPositionsApplied_ = false;  // node positions pushed to imnodes per graph
    float flowZoom_ = 1.0f;              // canvas zoom (imnodes emulation, 0.4-1.8)

    // Viewport overlays: TV frames (PAL 4:3 and NTSC, which shows a
    // slightly wider slice of the same 512x448 buffer)
    bool showPal_ = false;
    bool showNtsc_ = false;
    bool showHudInEditor_ = false;  // HUD preview overlay (default hidden)

    // HUD editing
    int selectedHud_ = -1;

    // Menus editing (Menu Editor window): selected menu + a live preview of
    // the baked panel (re-baked whenever the menu's content changes)
    bool showMenusEditor_ = false;
    int selectedMenu_ = -1;
    unsigned menuPreviewTex_ = 0;
    int menuPreviewW_ = 0, menuPreviewH_ = 0;
    int menuPreviewContentH_ = 0;  // drawn part (layout cached at bake time)
    bool menuPreviewClipped_ = false;  // content hit the 512px texture cap
    int menuPreviewMode_ = 0;      // 0 = panel 1:1, 1 = TV PAL, 2 = TV NTSC
    std::string menuPreviewKey_;  // serialized menu the texture was baked from
    struct HudTexture {
        unsigned tex = 0;
        int w = 0, h = 0;
    };
    std::map<std::string, HudTexture> hudTexCache_;
    const HudTexture* hudTexture(const std::string& relPath);


    Viewport viewport_;
    Runner runner_;

    // "New project" modal state
    bool openNewProjectPopup_ = false;
    char newName_[128] = "my-game";
    char newLocation_[512] = "";
    int newWidth_ = 64;
    int newDepth_ = 64;
    int newTemplate_ = 0;  // 0 = empty, 1 = fpp
    std::string newProjectError_;

    // "New script" modal state
    bool openNewScriptPopup_ = false;
    char newScriptName_[64] = "my_script";
    std::string newScriptError_;

    // "New scene" modal state
    int deleteScenePending_ = -1;  // scene index awaiting delete confirmation
    bool openNewScenePopup_ = false;
    char newSceneName_[64] = "scene-2";
    int newSceneWidth_ = 64, newSceneDepth_ = 64;
    std::string newSceneError_;

    // Disc Layout window (Project > Disc Layout...): plan preview + reorder
    bool showDiscLayout_ = false;
    bool discPlanDirty_ = true;        // replan on next draw
    bool discRunnerWasBusy_ = false;   // replan after a build/export finishes
    isoexport::Plan discPlan_;
    std::string discPlanError_;
    std::string discPlanWarnings_;
    int discSelected_ = -1;   // index into discPlan_.items (list <-> disc sync)
    int discCapacity_ = 2;    // 0 = fit to data, 1 = CD-R 700 MB, 2 = DVD-5

    // "Project Preferences" modal staging (applied on OK). Edits project-wide
    // defaults only (project_.settings + terrain + game template).
    bool openPreferencesPopup_ = false;
    TerrainConfig prefTerrain_;
    int prefTemplate_ = 0;
    ProjectSettings prefSettings_;
    char prefEmulatorPath_[512] = "";  // PCSX2 exe path (editor-side, not baked)

    // "Debug" window: tails a log from disk (reloaded, throttled). Source 0 is
    // the game's own log (bin/log.txt, written by TYRA_LOG); source 1 is the
    // emulator's console log (PCSX2 emulog.txt, boot progress + asserts).
    std::string debugLog_;
    int debugLogSource_ = 0;
    bool debugAutoReload_ = true;
    double debugNextReload_ = 0.0;  // ImGui::GetTime() gate for the next read

    // "Scene Preferences" modal staging (applied on OK): the active scene's
    // per-category overrides of the project defaults.
    bool openScenePrefsPopup_ = false;
    int scenePrefScene_ = -1;  // scene index the staging belongs to
    ProjectSettings scenePrefSettings_;
    SceneOverrides scenePrefOverrides_;

    std::string statusMessage_;
};
