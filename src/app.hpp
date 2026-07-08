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
    void drawSceneSection();
    void drawScriptsSection();
    void drawNewScriptModal();
    void drawNewSceneModal();
    void drawDeleteSceneModal();
    void drawFlowGraphWindow();
    void openInVSCode();
    void drawOutputWindow();
    void drawDiscLayoutWindow();
    void drawNewProjectModal();
    void drawPreferencesModal();
    void openProjectDialog();
    void applyProjectToViewport();
    void stageSceneIntoPrefs();  // active-scene terrain/light -> pref staging
    void addObject(PrimitiveType type);
    void addEmitter(int kind);  // Effects menu presets (fire/smoke/fog/sparks)
    void addSoundEmitter();
    void addPointLight();
    void drawAddObjectMenu();
    void importModel();
    void drawHudSection();
    void importHudImage();
    void drawMusicSection();
    void importMusicTrack();
    void drawSoundsSection();
    void importSoundEffect();
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
    int selectedObject_ = -1;

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
    int newTemplate_ = 0;  // 0 = orbit, 1 = fpp
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

    // "Preferences" modal staging (applied on OK)
    bool openPreferencesPopup_ = false;
    TerrainConfig prefTerrain_;
    int prefTemplate_ = 0;
    ProjectSettings prefSettings_;

    std::string statusMessage_;
};
