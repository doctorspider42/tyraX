#pragma once

#include <string>

#include "history.hpp"
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
    void drawFlowGraphWindow();
    void openInVSCode();
    void drawOutputWindow();
    void drawNewProjectModal();
    void drawPreferencesModal();
    void openProjectDialog();
    void applyProjectToViewport();
    void addObject(PrimitiveType type);
    void importModel();
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

    History history_;
    SceneObject clipboard_;
    bool hasClipboard_ = false;

    // Flow graph editor state
    bool flowPositionsApplied_ = false;  // node positions pushed to imnodes once


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

    // "Preferences" modal staging (applied on OK)
    bool openPreferencesPopup_ = false;
    TerrainConfig prefTerrain_;
    int prefTemplate_ = 0;
    ProjectSettings prefSettings_;

    std::string statusMessage_;
};
