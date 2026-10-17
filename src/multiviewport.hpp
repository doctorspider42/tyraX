// Windows that leave the main window - Dear ImGui multi-viewports
// (docs/editor-theme.md "Windows outside the main window").
//
// With ImGuiConfigFlags_ViewportsEnable a docked panel dragged out of the
// editor becomes its own OS window (a GLFW window sharing the main GL
// context, created by ImGui's GLFW backend) and docks back when dragged onto
// a dock target again. Everything the editor adds on top of the stock
// backend lives here:
//
//  - install(): wraps the backend's Platform_CreateWindow (each new OS window
//    is owned by the main window - platform::ownToolWindow - and gets the
//    editor's file-drop callback) and Renderer_RenderWindow (a script's `shot`
//    reads every OS window back, not only the main framebuffer).
//  - setEnabled(): the editor.ini multiViewports preference, applied between
//    frames - ImGui supports toggling the flag at runtime.
//  - renderPlatformWindows(): UpdatePlatformWindows + RenderPlatformWindows-
//    Default, then the main context made current again (the editor's own GL
//    work - viewport renders, previews, bakes - all happens on it).
//  - bringWindowsBack(): Layout > "Bring all windows back".
//  - fitLayoutIni(): a saved layout clamped to the screens that exist now,
//    before ImGui loads it.
//  - --ui-script support: the injected cursor is routed to the OS window under
//    it, and `undock` / `dock` steps move a named window out of and back into
//    a dock node without a real mouse drag.
//
// Coordinates: with viewports on, every ImGui position is in DESKTOP space
// (the main window's top-left is GetMainViewport()->Pos, not 0,0); with them
// off, positions are relative to the main window as before. Code that mixes
// ImGui positions with GLFW window coordinates (the file-drop callback) adds
// the window position when viewports are on.
#pragma once

#include <string>

struct GLFWwindow;
typedef void (*GLFWdropfun)(GLFWwindow* window, int path_count, const char* paths[]);

namespace uiscript {
struct Step;
}

namespace mvp {

/** Call once, after ImGui_ImplGlfw_InitForOpenGL / ImGui_ImplOpenGL3_Init. */
void install(GLFWwindow* mainWindow, GLFWdropfun drop);
/** True when the platform can do it at all (ImGui's GLFW backend says no on
 * Wayland, where the preference is then shown disabled). */
bool supported();
/** Turns ImGuiConfigFlags_ViewportsEnable on or off. Call between frames
 * (before ImGui::NewFrame); a no-op where !supported(). */
void setEnabled(bool on);
/** Whether panels can leave the main window this frame. */
bool enabled();

/** After the main window's ImGui_ImplOpenGL3_RenderDrawData, before its swap:
 * draws and swaps every other OS window, then restores the main context. */
void renderPlatformWindows(GLFWwindow* mainWindow);
/** The next renderPlatformWindows() also writes each OS window other than the
 * main one to `<stem>-<window name>.png` beside `mainShotPath`. */
void requestPlatformShots(const std::string& mainShotPath);

/** Once per frame right after ImGui::NewFrame(): finishes pending script
 * undocks. */
void newFrame();

/** Layout > "Bring all windows back": every floating window and floating dock
 * node that lives in its own OS window or reaches outside the main window is
 * moved (and if needed shrunk) into the main window, where ImGui merges it
 * back. Returns how many were moved. */
int bringWindowsBack();

/** A saved ImGui layout (SaveIniSettingsToMemory text) with every floating
 * window / floating dock node that would open off-screen moved back into the
 * main window: off every monitor while viewports are on, outside the main
 * window while they are off (a layout saved with panels in their own OS
 * windows, opened with the preference off). `mainX/mainY` is the main
 * window's desktop position (glfwGetWindowPos), `mainW/mainH` its size. */
std::string fitLayoutIni(const std::string& ini, float mainX, float mainY, float mainW, float mainH);

/** --ui-script: the cursor the script just queued (if any) is reported to
 * ImGui as being over the OS window under it, so a click on a panel in its
 * own window lands there even though the real cursor is elsewhere. Call
 * right after App::uiScriptTick(). */
void routeInjectedMouse();
/** --ui-script `undock` / `dock` steps. "" = started; else why not. */
std::string scriptStep(const uiscript::Step& s);

}  // namespace mvp
