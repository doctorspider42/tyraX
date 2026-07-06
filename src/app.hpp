#pragma once

#include <string>

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
    void drawOutputWindow();
    void drawNewProjectModal();
    void openProjectDialog();
    void addObject(PrimitiveType type);
    void saveProject();

    GLFWwindow* window_ = nullptr;

    Project project_;
    bool hasProject_ = false;
    int selectedObject_ = -1;

    Viewport viewport_;
    Runner runner_;

    // "New project" modal state
    bool openNewProjectPopup_ = false;
    char newName_[128] = "my-game";
    char newLocation_[512] = "";
    int newWidth_ = 64;
    int newDepth_ = 64;
    std::string newProjectError_;

    std::string statusMessage_;
};
