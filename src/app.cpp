#include "app.hpp"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>

#include "decalproj.hpp"
#include "gl_loader.h"
#include "glbparser.hpp"
#include "json.hpp"
#include "menubake.hpp"
#include "objparser.hpp"
#include "templates.hpp"
#include "wavconvert.hpp"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
// JPEG too: .glb files often embed JPEG textures - the importer transcodes
// them to PNG for the PS2 (see glbparser.cpp). Viewport/game stay PNG-only.
#define STBI_ONLY_JPEG
#include <stb_image.h>
#include <stb_image_write.h>  // implementation lives in menubake.cpp

#include <GLFW/glfw3.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <ImGuizmo.h>
#include <imnodes.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commdlg.h>
#include <shobjidl.h>

// ---------------------------------------------------------------------------
// Native pickers (IFileOpenDialog). pickFolder: FOS_PICKFOLDERS;
// pickSolutionFile: file dialog filtered to *.tyra project files.
// ---------------------------------------------------------------------------
enum class PickKind { Folder, Solution, ObjModel, Mtl, Png, Wav, Ttf, Executable, CamTake };

// Owner window for the native dialogs. An unowned modal (Show(nullptr))
// leaves the frozen GLFW window active behind it - Windows then wedges the
// dialog when it interacts with the non-pumping app (grayed Open button).
static HWND g_dialogOwner = nullptr;

// Defined lower down (next to the game-error catcher) but used early in
// attachProject() to baseline the log size.
static size_t fileSizeOr0(const std::string& path);

// ---------------------------------------------------------------------------
// Global editor config. These are machine/muscle-memory properties (a 4K laptop
// wants a different UI scale than a 1080p desktop; navigation is personal
// preference; the emulator lives at a fixed path on this PC and the dev PS2 has
// a fixed LAN address), so they live outside the per-project .tyra - in
// %LOCALAPPDATA%\tyra-editor\editor.ini. Trivial key=value lines; the whole
// file is rewritten on any change, so load once and save the full struct.
// ---------------------------------------------------------------------------
struct EditorConfig {
    float uiScale = 0.0f;  // 0 == auto (follow the display DPI)
    NavConfig nav;
    std::string emulatorPath;  // pcsx2-qt.exe; empty = auto-detect
    std::string ps2LinkIp;     // LAN IP of a PS2 running ps2link; empty = disabled
    // When true (default), a TYRA assertion from the running game pops up a
    // copyable error dialog. When false, errors go only to the console / Debug
    // window (the game already logs them there either way).
    bool errorPopup = true;
    // Parent folder proposed as the location for new projects. Empty = fall
    // back to ~/TyraProjects.
    std::string defaultProjectsDir;
};

static std::filesystem::path editorConfigPath() {
    const char* base = getenv("LOCALAPPDATA");
    if (!base || !*base) base = getenv("USERPROFILE");
    if (!base || !*base) return {};
    return std::filesystem::path(base) / "tyra-editor" / "editor.ini";
}

static EditorConfig loadEditorConfig() {
    EditorConfig cfg;
    const auto path = editorConfigPath();
    if (path.empty()) return cfg;
    std::ifstream f(path);
    std::string line;
    auto match = [&line](const char* key, std::string& out) {
        const std::string k = std::string(key) + "=";
        if (line.rfind(k, 0) != 0) return false;
        out = line.substr(k.size());
        return true;
    };
    std::string v;
    auto toF = [](const std::string& s, float d) { try { return std::stof(s); } catch (...) { return d; } };
    auto toI = [](const std::string& s, int d) { try { return std::stoi(s); } catch (...) { return d; } };
    auto clampScheme = [](int i) { return (i < 0 || i > 3) ? NavScheme::Default : (NavScheme)i; };
    while (std::getline(f, line)) {
        if (match("uiScale", v)) cfg.uiScale = toF(v, cfg.uiScale);
        else if (match("navScheme", v)) cfg.nav.scheme = clampScheme(toI(v, 0));
        else if (match("navMoveKeys", v)) cfg.nav.moveKeys = toI(v, 0) == 1 ? NavMoveKeys::Arrows : NavMoveKeys::WASD;
        else if (match("navOrbitSens", v)) cfg.nav.orbitSensitivity = toF(v, 1.0f);
        else if (match("navPanSens", v)) cfg.nav.panSensitivity = toF(v, 1.0f);
        else if (match("navZoomSens", v)) cfg.nav.zoomSensitivity = toF(v, 1.0f);
        else if (match("navInvertX", v)) cfg.nav.invertX = toI(v, 0) != 0;
        else if (match("navInvertY", v)) cfg.nav.invertY = toI(v, 0) != 0;
        else if (match("navOrbitSelection", v)) cfg.nav.orbitAroundSelection = toI(v, 1) != 0;
        else if (match("emulatorPath", v)) cfg.emulatorPath = v;
        else if (match("ps2LinkIp", v)) cfg.ps2LinkIp = v;
        else if (match("errorPopup", v)) cfg.errorPopup = toI(v, 1) != 0;
        else if (match("defaultProjectsDir", v)) cfg.defaultProjectsDir = v;
    }
    return cfg;
}

static void saveEditorConfig(const EditorConfig& cfg) {
    const auto path = editorConfigPath();
    if (path.empty()) return;
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    std::ofstream f(path, std::ios::trunc);
    if (!f) return;
    const NavConfig& n = cfg.nav;
    f << "uiScale=" << cfg.uiScale << "\n"
      << "navScheme=" << (int)n.scheme << "\n"
      << "navMoveKeys=" << (int)n.moveKeys << "\n"
      << "navOrbitSens=" << n.orbitSensitivity << "\n"
      << "navPanSens=" << n.panSensitivity << "\n"
      << "navZoomSens=" << n.zoomSensitivity << "\n"
      << "navInvertX=" << (n.invertX ? 1 : 0) << "\n"
      << "navInvertY=" << (n.invertY ? 1 : 0) << "\n"
      << "navOrbitSelection=" << (n.orbitAroundSelection ? 1 : 0) << "\n"
      << "emulatorPath=" << cfg.emulatorPath << "\n"
      << "ps2LinkIp=" << cfg.ps2LinkIp << "\n"
      << "errorPopup=" << (cfg.errorPopup ? 1 : 0) << "\n"
      << "defaultProjectsDir=" << cfg.defaultProjectsDir << "\n";
}

// Default parent directory proposed for new projects: the configured global
// default (Edit > Preferences) if set, else ~/TyraProjects.
static std::string defaultNewProjectLocation(const std::string& configured) {
    if (!configured.empty()) return configured;
    if (const char* home = getenv("USERPROFILE"))
        return std::string(home) + "\\TyraProjects";
    return "";
}

static std::string wideToUtf8(const wchar_t* path) {
    std::string result;
    const int len = WideCharToMultiByte(CP_UTF8, 0, path, -1, nullptr, 0, nullptr, nullptr);
    if (len > 1) {
        result.resize(len - 1);
        WideCharToMultiByte(CP_UTF8, 0, path, -1, result.data(), len, nullptr, nullptr);
    }
    return result;
}

// Classic comdlg32 file dialog. The shell-based IFileOpenDialog wedges on
// this setup (grayed Open button, dialog never returns) - the legacy dialog
// carries far less shell machinery. filter is the double-NUL comdlg format.
static std::string pickFileLegacy(const wchar_t* filter, const wchar_t* title) {
    wchar_t buf[MAX_PATH] = L"";
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g_dialogOwner;
    ofn.lpstrFilter = filter;
    ofn.nFilterIndex = 1;
    ofn.lpstrFile = buf;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrTitle = title;
    ofn.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST |
                OFN_NOCHANGEDIR | OFN_HIDEREADONLY;
    if (!GetOpenFileNameW(&ofn)) return "";
    return wideToUtf8(buf);
}

static std::string pickPath(PickKind kind) {
    switch (kind) {
        case PickKind::Solution:
            return pickFileLegacy(
                L"Tyra project (*.tyra)\0*.tyra\0All files (*.*)\0*.*\0",
                L"Open Tyra project");
        case PickKind::ObjModel:
            return pickFileLegacy(
                L"3D model (*.obj, *.glb)\0*.obj;*.glb\0"
                L"Wavefront model (*.obj)\0*.obj\0"
                L"Animated glTF binary (*.glb)\0*.glb\0All files (*.*)\0*.*\0",
                L"Import 3D model (.glb = animated)");
        case PickKind::Mtl:
            return pickFileLegacy(
                L"Material library (*.mtl)\0*.mtl\0All files (*.*)\0*.*\0",
                L"Import material library");
        case PickKind::Png:
            return pickFileLegacy(
                L"PNG image (*.png)\0*.png\0All files (*.*)\0*.*\0",
                L"Import PNG image");
        case PickKind::Wav:
            return pickFileLegacy(
                L"WAV audio (*.wav)\0*.wav\0All files (*.*)\0*.*\0",
                L"Import WAV (16-bit 22kHz recommended)");
        case PickKind::Ttf:
            return pickFileLegacy(
                L"TrueType font (*.ttf, *.otf)\0*.ttf;*.otf\0All files (*.*)\0*.*\0",
                L"Import menu font");
        case PickKind::Executable:
            return pickFileLegacy(
                L"Executable (*.exe)\0*.exe\0All files (*.*)\0*.*\0",
                L"Select PCSX2 executable");
        case PickKind::CamTake:
            return pickFileLegacy(
                L"Camera take (*.hfcs, *.csv)\0*.hfcs;*.csv\0"
                L"CamTrackAR composite shot (*.hfcs)\0*.hfcs\0"
                L"Camera take CSV (*.csv)\0*.csv\0All files (*.*)\0*.*\0",
                L"Import camera take (phone AR recording)");
        case PickKind::Folder:
            break;  // folders need the shell dialog below
    }

    const bool folder = true;
    std::string result;
    HRESULT hrInit = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    IFileOpenDialog* dialog = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_ALL,
                                   IID_PPV_ARGS(&dialog)))) {
        DWORD options = 0;
        dialog->GetOptions(&options);
        dialog->SetOptions(options | (folder ? FOS_PICKFOLDERS : 0) | FOS_FORCEFILESYSTEM);
        if (SUCCEEDED(dialog->Show(g_dialogOwner))) {
            IShellItem* item = nullptr;
            if (SUCCEEDED(dialog->GetResult(&item))) {
                PWSTR path = nullptr;
                if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
                    result = wideToUtf8(path);
                    CoTaskMemFree(path);
                }
                item->Release();
            }
        }
        dialog->Release();
    }
    if (SUCCEEDED(hrInit)) CoUninitialize();
    return result;
}

static std::string pickFolder() { return pickPath(PickKind::Folder); }
static std::string pickSolutionFile() { return pickPath(PickKind::Solution); }
static std::string pickModelFile() { return pickPath(PickKind::ObjModel); }
static std::string pickMtlFile() { return pickPath(PickKind::Mtl); }
static std::string pickPngFile() { return pickPath(PickKind::Png); }
static std::string pickWavFile() { return pickPath(PickKind::Wav); }
static std::string pickTtfFile() { return pickPath(PickKind::Ttf); }
static std::string pickExeFile() { return pickPath(PickKind::Executable); }

// Asset filenames flow into shell command lines (e.g. the adpenc wav->adpcm
// loop in runner.cpp), Makefiles and ISO9660 paths - none of which reliably
// tolerate spaces or shell-special characters. Fold anything outside
// [A-Za-z0-9._-] to '_' at import time so the file we copy into res/ and the
// relative path we store always match and stay pipeline-safe.
static std::string sanitizeAssetName(const std::string& fileName) {
    std::string out = fileName;
    for (char& c : out) {
        const bool safe = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                          (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-';
        if (!safe) c = '_';
    }
    return out;
}

// WAV format inspection + in-place 16-bit conversion live in wavconvert.*.
static bool readWavFormat(const std::string& path, int& audioFormat, int& channels,
                          int& sampleRate, int& bitsPerSample) {
    return wavconvert::readFormat(path, audioFormat, channels, sampleRate,
                                  bitsPerSample);
}

// PS2 SPU2 has ~2 MB of sample RAM. Sound emitters load every sfx into it at
// scene start (audsrv ADPCM one-shots), so the whole set must fit - a sample
// too big just fails to load and plays nothing. adpenc packs 28 samples into
// 16 bytes, so ADPCM is ~2/7 of the 16-bit PCM data; that lets us estimate the
// SPU2 footprint from the WAV file size and warn before a build silently drops
// it. Budget left a little under 2 MB for audsrv's own reverb/stream buffers.
static constexpr uintmax_t kSpu2SampleBudgetBytes = 2u * 1024 * 1024;
static uintmax_t estimateAdpcmBytes(uintmax_t wavFileBytes) {
    return wavFileBytes * 2 / 7;
}

// ---------------------------------------------------------------------------

int App::run(const std::string& initialProjectDir) {
    glfwSetErrorCallback([](int code, const char* msg) {
        std::fprintf(stderr, "GLFW error %d: %s\n", code, msg);
    });
    if (!glfwInit()) return 1;

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_SAMPLES, 4);
    glfwWindowHint(GLFW_MAXIMIZED, GLFW_TRUE);

    // Size is the restore-size when the user un-maximizes.
    window_ = glfwCreateWindow(1600, 900, "TyraX", nullptr, nullptr);
    if (window_) g_dialogOwner = glfwGetWin32Window(window_);
    if (!window_) {
        glfwTerminate();
        return 1;
    }
    glfwMakeContextCurrent(window_);
    glfwSwapInterval(1);

    if (!glInit()) {
        std::fprintf(stderr, "Failed to load OpenGL functions\n");
        return 1;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    // The window layout lives inside the project's .tyra file, not a global
    // imgui.ini - we load/save it via ImGui's in-memory settings API.
    io.IniFilename = nullptr;
    ImGui::StyleColorsDark();
    // Capture the unscaled style before any DPI scaling is applied - every
    // scale change resets to this reference so repeated changes don't compound.
    baseStyle_ = ImGui::GetStyle();

    ImGui_ImplGlfw_InitForOpenGL(window_, true);
    ImGui_ImplOpenGL3_Init("#version 330 core");
    ImNodes::CreateContext();

    // Scale the UI for the display: the saved override if any, else auto-match
    // the monitor's content scale (a 4K laptop reports e.g. 2.0). Fonts are
    // rasterized dynamically in this ImGui, so scaling stays crisp.
    {
        const EditorConfig cfg = loadEditorConfig();
        uiScaleUser_ = cfg.uiScale;
        nav_ = cfg.nav;
        globalEmulatorPath_ = cfg.emulatorPath;
        globalPs2Ip_ = cfg.ps2LinkIp;
        errorPopupEnabled_ = cfg.errorPopup;
        globalDefaultProjectsDir_ = cfg.defaultProjectsDir;
    }
    applyUiScale();

    // Drag & drop from Explorer: the dialog-free import path (PNGs land in
    // res/hud and attach to the selected menu when the Menu Editor is open).
    glfwSetWindowUserPointer(window_, this);
    glfwSetDropCallback(window_, [](GLFWwindow* w, int count, const char** paths) {
        static_cast<App*>(glfwGetWindowUserPointer(w))->handleFileDrop(count, paths);
    });

    if (!viewport_.init()) {
        std::fprintf(stderr, "Failed to init viewport renderer\n");
        return 1;
    }

    // Default location for new projects: the configured global default (Edit >
    // Preferences) or ~/TyraProjects. Re-proposed each time the New Project
    // modal opens, so a mid-session preference change takes effect immediately.
    std::snprintf(newLocation_, sizeof(newLocation_), "%s",
                  defaultNewProjectLocation(globalDefaultProjectsDir_).c_str());

    if (!initialProjectDir.empty()) {
        // Accept both a project directory and a <name>.tyra project file
        std::string dir = initialProjectDir;
        if (std::filesystem::path(dir).extension() == ".tyra")
            dir = std::filesystem::path(dir).parent_path().string();
        Project p;
        if (project::load(p, dir).empty()) {
            project_ = p;
            hasProject_ = true;
            applyProjectToViewport();
            attachProject();  // resets dirty + window title
        }
    }

    while (true) {
        glfwPollEvents();

        // Close request (window X or File > Exit). Guard against losing unsaved
        // edits: prompt once, and only really close after the user resolves it
        // (exitConfirmed_) or when there is nothing to lose.
        if (glfwWindowShouldClose(window_)) {
            if (exitConfirmed_ || !hasProject_ || !dirty_) break;
            glfwSetWindowShouldClose(window_, GLFW_FALSE);
            pendingAction_ = PendingAction::Exit;
            openDiscardPopup_ = true;
        }

        // Deferred from attachProject()/switchLayout(): a saved layout dump can
        // only be (re)applied between frames - existing windows get re-docked
        // here. Recipe-built layouts take the drawUI path instead.
        if (layoutLoadPending_) {
            layoutLoadPending_ = false;
            const std::string& ini =
                (hasProject_ && !project_.windowLayouts.empty())
                    ? project_.windowLayouts[project_.activeLayout].ini
                    : std::string();
            ImGui::LoadIniSettingsFromMemory(ini.c_str(), ini.size());
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        drawUI();

        // No autosave: layout/docking changes fold into the .tyra only when
        // the user saves the project (Save / Ctrl+S). io.WantSaveIniSettings
        // is left for the next explicit saveProject() to pick up.

        ImGui::Render();
        int w, h;
        glfwGetFramebufferSize(window_, &w, &h);
        glViewport(0, 0, w, h);
        glClearColor(0.08f, 0.08f, 0.10f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(window_);
    }

    // No save on exit: the user chose to discard (or had nothing unsaved).

    viewport_.shutdown();
    ImNodes::DestroyContext();
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window_);
    glfwTerminate();
    return 0;
}

void App::drawUI() {
    ImGuizmo::BeginFrame();
    ImGuiID dockspace = ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport());

    // Apply a pending layout rebuild now: the dockspace id exists, but no panel
    // window has been submitted yet this frame (DockBuilder must run before the
    // windows it docks are drawn). Saved-ini loads take the frame-boundary path
    // in run() instead - LoadIniSettingsFromMemory can't run mid-frame.
    if (recipeRebuildPending_) {
        recipeRebuildPending_ = false;
        buildLayoutRecipe(recipeRebuildId_, dockspace);
    }

    // First-run fallback before any project is open (no imgui.ini yet): the
    // default arrangement. Once a project attaches, applyActiveLayout() drives
    // the docking from the project's saved layouts instead.
    static bool bootLayoutDone = false;
    if (!bootLayoutDone && !hasProject_) {
        bootLayoutDone = true;
        if (ImGui::DockBuilderGetNode(dockspace) == nullptr ||
            ImGui::DockBuilderGetNode(dockspace)->IsLeafNode())
            buildLayoutRecipe((int)LayoutRecipe::Default, dockspace);
    }

    // Layouts saved before the Properties window existed: carve a slot for it
    // on the right side of the main dockspace once the Project panel has
    // settled (a signal that the loaded layout has been applied).
    if (dockPropertiesPending_) {
        if (ImGuiWindow* proj = ImGui::FindWindowByName("Project")) {
            if (proj->DockId != 0 && ImGui::DockBuilderGetNode(proj->DockId)) {
                ImGuiID center = dockspace;
                ImGuiID slot = ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.26f,
                                                           nullptr, &center);
                ImGui::DockBuilderDockWindow("Properties", slot);
                ImGui::DockBuilderFinish(dockspace);
            }
            dockPropertiesPending_ = false;  // Project floating -> Properties floats too
        }
    }

    // Watch the running game's log for a fresh assertion dump (throttled).
    pollGameError();

    // Stream scene edits to the running debug game (throttled; no-op unless
    // the live-patchable state changed since the last write).
    liveLinkTick();

    drawMenuBar();
    drawViewportWindow();
    drawProjectWindow();
    drawPropertiesWindow();
    drawFlowGraphWindow();
    drawOutputWindow();
    drawDebugWindow();
    drawDiscLayoutWindow();
    drawMenusWindow();
    drawGradingWindow();
    drawAmbienceWindow();
    drawCutsceneWindow();
    drawMaterialEditorWindow();
    drawUiEditorWindow();
    drawLoadingScreenWindow();
    drawNewProjectModal();
    drawPreferencesModal();
    drawEditorPreferencesModal();
    drawErrorModal();
    drawNavigationModal();
    drawScenePreferencesModal();
    drawNewScriptModal();
    drawNewSceneModal();
    drawDeleteSceneModal();
    drawDeleteAssetModal();
    drawDiscardModal();
    drawLayoutModals();

    // Bring the freshly switched layout's headline panel to the front, now that
    // its window has been submitted this frame (docked as a background tab by
    // the recipe, or loaded from the saved ini).
    if (!pendingFocusWindow_.empty() && !layoutLoadPending_ && !recipeRebuildPending_) {
        ImGui::SetWindowFocus(pendingFocusWindow_.c_str());
        pendingFocusWindow_.clear();
    }

    // Keyboard shortcuts
    ImGuiIO& io = ImGui::GetIO();
    if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_N)) requestNewProject();
    if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_O)) requestOpenProject();
    // UI scaling (works with no project open; skip while typing in a field)
    if (!io.WantTextInput) {
        if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Equal))
            setUiScale(ImClamp(uiScaleApplied_ + 0.1f, 0.5f, 4.0f));
        if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Minus))
            setUiScale(ImClamp(uiScaleApplied_ - 0.1f, 0.5f, 4.0f));
        if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_0))
            setUiScale(0.0f);
    }
    // Run shortcuts. IsKeyChordPressed matches the modifier state exactly, so
    // plain F5 and Ctrl+F5 stay distinct: F5/F6 build && run, Ctrl+F5/Ctrl+F6
    // run the existing ELF without building.
    if (hasProject_ && !runner_.busy()) {
        const bool ps2Ready = !project_.ps2LinkIp.empty();
        if (ImGui::IsKeyChordPressed(ImGuiKey_F5))
            runner_.buildAndRun(project_, true);
        if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_F5))
            runner_.runEmulatorOnly(project_);
        if (ps2Ready && ImGui::IsKeyChordPressed(ImGuiKey_F6))
            runner_.buildAndRunPs2(project_, true);
        if (ps2Ready && ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_F6))
            runner_.buildAndRunPs2(project_, false);
        if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_B))
            runner_.buildAndRun(project_, false);
    }
    if (hasProject_) {
        if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_S)) saveAll("Saved");
        if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Comma)) {
            prefTerrain_ = project_.active().terrain;
            prefTemplate_ = project_.gameTemplate == "fpp" ? 1 : 0;
            prefSettings_ = project_.settings;
            openPreferencesPopup_ = true;
        }
        if (!io.WantTextInput) {
            // While the Material Editor has focus, Ctrl+Z drives ITS undo
            // stack (paint strokes + material edits, saved straight to disk) -
            // undoing scene changes from under an open material would be
            // surprising. Same focus-scoping as the Flow Graph's Ctrl+C/V.
            if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Z)) {
                if (matEdFocused_) matEdUndoLast();
                else undo();
            }
            if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Y) &&
                !matEdFocused_)
                redo();
            // While the Flow Graph window has focus, Ctrl+C/V act on its nodes
            // (handled in drawFlowGraphWindow); otherwise they copy scene objects.
            if (!flowGraphFocused_) {
                if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_C)) copyObject();
                if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_V)) pasteObject();
            }
        }
    }
}

void App::applyUiScale() {
    float monitor = window_ ? ImGui_ImplGlfw_GetContentScaleForWindow(window_) : 1.0f;
    if (monitor <= 0.0f) monitor = 1.0f;
    float scale = uiScaleUser_ > 0.0f ? uiScaleUser_ : monitor;
    scale = ImClamp(scale, 0.5f, 4.0f);  // keep the UI usable whatever we read

    ImGuiStyle& style = ImGui::GetStyle();
    style = baseStyle_;           // reset to the unscaled reference...
    style.ScaleAllSizes(scale);   // ...then scale spacing/padding/borders
    style.FontScaleMain = scale;  // dynamic fonts re-rasterize at the new size
    uiScaleApplied_ = scale;
}

// Writes the whole global editor config (editor.ini) from the App members.
// All save sites funnel through here so no field is dropped - a bare
// {uiScaleUser_, nav_} would wipe the emulator path / PS2 IP on the next
// UI-scale or navigation change.
void App::saveGlobalConfig() {
    saveEditorConfig({uiScaleUser_, nav_, globalEmulatorPath_, globalPs2Ip_,
                      errorPopupEnabled_, globalDefaultProjectsDir_});
}

void App::setUiScale(float userScale) {
    uiScaleUser_ = userScale;  // 0 == auto (follow the display DPI)
    applyUiScale();
    saveGlobalConfig();
}

void App::drawMenuBar() {
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("New Project...", "Ctrl+N")) requestNewProject();
            if (ImGui::MenuItem("Open Project...", "Ctrl+O")) requestOpenProject();
            if (ImGui::MenuItem("Save", "Ctrl+S", false, hasProject_))
                saveAll("Saved");
            ImGui::Separator();
            if (ImGui::MenuItem("Exit")) requestExit();
            ImGui::EndMenu();
        }
        // Edit stays enabled without a project so the machine-global editor
        // settings (emulator path, PS2 IP) can be set before creating one; the
        // project-scoped items below disable themselves on their own state.
        if (ImGui::BeginMenu("Edit")) {
            const bool objectSelected =
                hasProject_ && selectedObject_ >= 0 &&
                selectedObject_ < (int)project_.objects().size();
            if (ImGui::MenuItem("Undo", "Ctrl+Z", false, history_.canUndo())) undo();
            if (ImGui::MenuItem("Redo", "Ctrl+Y", false, history_.canRedo())) redo();
            ImGui::Separator();
            const char* copyLabel =
                selection_.size() > 1 ? "Copy objects" : "Copy object";
            const char* pasteLabel =
                clipboard_.size() > 1 ? "Paste objects" : "Paste object";
            if (ImGui::MenuItem(copyLabel, "Ctrl+C", false, objectSelected)) copyObject();
            if (ImGui::MenuItem(pasteLabel, "Ctrl+V", false, hasProject_ && !clipboard_.empty()))
                pasteObject();
            ImGui::Separator();
            if (ImGui::MenuItem("Preferences...")) {
                snprintf(prefEmulatorPath_, sizeof(prefEmulatorPath_), "%s",
                         globalEmulatorPath_.c_str());
                snprintf(prefPs2Ip_, sizeof(prefPs2Ip_), "%s", globalPs2Ip_.c_str());
                snprintf(prefDefaultProjectsDir_, sizeof(prefDefaultProjectsDir_), "%s",
                         globalDefaultProjectsDir_.c_str());
                openEditorPrefsPopup_ = true;
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("View")) {
            ImGui::TextDisabled("Interface scale");
            if (ImGui::MenuItem("Zoom in", "Ctrl+="))
                setUiScale(ImClamp(uiScaleApplied_ + 0.1f, 0.5f, 4.0f));
            if (ImGui::MenuItem("Zoom out", "Ctrl+-"))
                setUiScale(ImClamp(uiScaleApplied_ - 0.1f, 0.5f, 4.0f));
            if (ImGui::MenuItem("Auto (match display DPI)", "Ctrl+0", uiScaleUser_ == 0.0f))
                setUiScale(0.0f);
            ImGui::Separator();
            const float presets[] = {1.0f, 1.25f, 1.5f, 1.75f, 2.0f, 2.5f, 3.0f};
            for (float v : presets) {
                char label[16];
                std::snprintf(label, sizeof(label), "%d%%", (int)std::lround(v * 100.0f));
                if (ImGui::MenuItem(label, nullptr, std::abs(uiScaleApplied_ - v) < 0.001f))
                    setUiScale(v);
            }
            ImGui::Separator();
            ImGui::TextDisabled("Current: %d%%%s", (int)std::lround(uiScaleApplied_ * 100.0f),
                                uiScaleUser_ == 0.0f ? " (auto)" : "");
            ImGui::Separator();
            if (ImGui::MenuItem("Navigation controls...")) openNavigationPopup_ = true;

            ImGui::Separator();
            ImGui::TextDisabled("Render mode");
            const char* modeNames[] = {"Solid", "Wireframe", "Wire + Solid"};
            for (int i = 0; i < 3; ++i) {
                const bool active = (int)viewport_.viewMode() == i;
                if (ImGui::MenuItem(modeNames[i], nullptr, active, hasProject_) && !active) {
                    viewport_.setViewMode((Viewport::ViewMode)i);
                    saveAll("Saved");  // persist the view mode in the project file
                }
            }

            ImGui::Separator();
            ImGui::TextDisabled("Preview");
            if (ImGui::MenuItem("Distance fog", nullptr, showFog_, hasProject_)) {
                showFog_ = !showFog_;
                applyProjectToViewport();  // suppress/restore fog in the viewport now
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                ImGui::SetTooltip("Preview the scene's distance fog in the editor. "
                                  "Turn off to see distant geometry - does not "
                                  "affect the generated game.");
            if (ImGui::MenuItem("Nav mesh overlay", nullptr, showNavOverlay_,
                                hasProject_))
                showNavOverlay_ = !showNavOverlay_;
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                ImGui::SetTooltip(
                    "Show the baked navigation grid the AI flow nodes walk on "
                    "(green = walkable). Tune it in Project > Preferences > "
                    "AI navigation.");

            ImGui::Separator();
            ImGui::TextDisabled("TV safe frame");
            if (ImGui::MenuItem("PAL 4:3 frame", nullptr, showPal_)) showPal_ = !showPal_;
            if (ImGui::MenuItem("NTSC frame", nullptr, showNtsc_)) showNtsc_ = !showNtsc_;

            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Layout", hasProject_)) {
            drawLayoutMenu();
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Scene", hasProject_)) {
            if (ImGui::BeginMenu("Add")) {
                drawAddObjectMenu();
                ImGui::EndMenu();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Scene Preferences...")) openScenePreferences();
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Project", hasProject_)) {
            const bool busy = runner_.busy();
            if (ImGui::MenuItem("Preferences...", "Ctrl+,")) {
                prefTerrain_ = project_.active().terrain;
                prefTemplate_ = project_.gameTemplate == "fpp" ? 1 : 0;
                prefSettings_ = project_.settings;
                openPreferencesPopup_ = true;
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Export PS2 ISO", nullptr, false, !busy))
                runner_.exportIso(project_);
            if (ImGui::MenuItem("Disc Layout...")) {
                showDiscLayout_ = true;
                discPlanDirty_ = true;
            }
            ImGui::EndMenu();
        }
        // VS-style top-level Build menu (the Project panel keeps its buttons)
        if (ImGui::BeginMenu("Build", hasProject_)) {
            const bool busy = runner_.busy();
            if (ImGui::MenuItem("Build", "Ctrl+Shift+B", false, !busy))
                runner_.buildAndRun(project_, false);
            if (ImGui::MenuItem("Build && Run in PCSX2", "F5", false, !busy))
                runner_.buildAndRun(project_, true);
            if (ImGui::MenuItem("Run in PCSX2 (no build)", "Ctrl+F5", false, !busy))
                runner_.runEmulatorOnly(project_);
            ImGui::Separator();
            const bool ps2Ready = !project_.ps2LinkIp.empty();
            if (ImGui::MenuItem("Build && Run on PS2", "F6", false, !busy && ps2Ready))
                runner_.buildAndRunPs2(project_, true);
            if (ImGui::MenuItem("Run on PS2 (no build)", "Ctrl+F6", false, !busy && ps2Ready))
                runner_.buildAndRunPs2(project_, false);
            if (!ps2Ready && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                ImGui::SetTooltip("Set 'PS2 (ps2link) IP' in Edit > Preferences first.");
            if (ImGui::MenuItem("Stop on PS2", nullptr, false, !busy && ps2Ready))
                runner_.stopPs2(project_);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                ImGui::SetTooltip("Kills the file server and resets ps2link - the "
                                  "console reboots back to its listening state.");
            ImGui::Separator();
            if (ImGui::MenuItem("Live Link", nullptr,
                                project_.settings.liveLink)) {
                project_.settings.liveLink = !project_.settings.liveLink;
                commitChange();
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                ImGui::SetTooltip(
                    "Mirror scene edits (move/rotate/scale/recolor objects, "
                    "add/delete them)\ninto the running game without a rebuild "
                    "- PCSX2 and real-PS2 deploys alike.\nA project setting: "
                    "off = the game is built without the poller and the "
                    "editor\nnever writes snapshots. Needs the \"debug\" build "
                    "profile (Project >\nPreferences > Build). Also toggled by "
                    "clicking the LIVE chip in the toolbar.");
            ImGui::Separator();
            if (ImGui::MenuItem("Cancel Build", nullptr, false, busy)) runner_.cancel();
            if (ImGui::MenuItem("Clean", nullptr, false, !busy))
                runner_.clean(project_);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                ImGui::SetTooltip("Deletes bin\\ and the container build cache "
                                  "(obj) - the next build starts from scratch.");
            ImGui::EndMenu();
        }

        if (hasProject_ && ImGui::BeginMenu("Tools")) {
            if (ImGui::MenuItem("Material Editor...")) showMaterialEditor_ = true;
            if (ImGui::MenuItem("Menu Editor...")) showMenusEditor_ = true;
            if (ImGui::MenuItem("Color Grading...")) showGradingEditor_ = true;
            if (ImGui::MenuItem("Ambience Editor...")) showAmbienceEditor_ = true;
            if (ImGui::MenuItem("Cutscene Director...")) showCutsceneEditor_ = true;
            if (ImGui::MenuItem("UI Editor...")) showUiEditor_ = true;
            if (ImGui::MenuItem("Loading Screens...")) showLoadingEditor_ = true;
            ImGui::EndMenu();
        }

        drawToolbar();

        if (!statusMessage_.empty()) {
            const float w = ImGui::CalcTextSize(statusMessage_.c_str()).x;
            ImGui::SameLine(ImGui::GetWindowWidth() - w - 16.0f);
            ImGui::TextDisabled("%s", statusMessage_.c_str());
        }
        ImGui::EndMainMenuBar();
    }
}

// Icon toolbar drawn inline in the main menu bar, after the menus. Layout:
// Save, Build, then two run/stop pairs - [green Play=PCSX2, dropdown, Stop] and
// [blue Play=PS2, dropdown, Stop]. Each Play has a Visual-Studio-style caret
// dropdown for the "run without build" variant. Icons are vector-drawn on the
// menu-bar draw list (the editor loads no icon font) so they stay crisp at any
// UI scale. Spacing is explicit: a pair sits tight, groups get a wider gap.
void App::drawToolbar() {
    if (!hasProject_) return;

    const bool busy = runner_.busy();
    const bool ps2Ready = !project_.ps2LinkIp.empty();
    const ImU32 colDim = ImGui::GetColorU32(ImGuiCol_TextDisabled);
    const ImU32 colText = ImGui::GetColorU32(ImGuiCol_Text);
    const ImU32 colStop = IM_COL32(225, 95, 85, 255);
    const float h = ImGui::GetFrameHeight();
    const float round = ImGui::GetStyle().FrameRounding;
    const float gapPair = ImMax(2.0f, h * 0.08f);   // within a play/stop pair
    const float gapGroup = h * 0.55f;               // between button groups

    // An icon button on the menu-bar line, `lead` px left of the previous item
    // and `bw` wide. `paint(dl, a, b, enabled)` draws the glyph into the padded
    // inner rect [a,b]. Returns true on click (ignored while disabled - the
    // icon dims instead).
    auto button = [&](const char* id, float lead, float bw, bool enabled,
                      const char* tip, auto&& paint) -> bool {
        ImGui::SameLine(0.0f, lead);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 p = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton(id, ImVec2(bw, h));
        const bool hovered =
            ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled);
        const bool held = enabled && ImGui::IsItemActive();
        const bool clicked = enabled && ImGui::IsItemClicked();
        if (enabled && hovered)
            dl->AddRectFilled(p, ImVec2(p.x + bw, p.y + h),
                              ImGui::GetColorU32(held ? ImGuiCol_ButtonActive
                                                      : ImGuiCol_ButtonHovered),
                              round);
        const float pad = h * 0.28f;
        // Square glyph rect regardless of button width (carets are narrower).
        const float cx = p.x + bw * 0.5f;
        const ImVec2 a(cx - (h * 0.5f - pad), p.y + pad);
        const ImVec2 b(cx + (h * 0.5f - pad), p.y + h - pad);
        paint(dl, a, b, enabled);
        if (tip && hovered) ImGui::SetTooltip("%s", tip);
        return clicked;
    };
    auto iconButton = [&](const char* id, float lead, bool enabled,
                          const char* tip, auto&& paint) -> bool {
        return button(id, lead, h, enabled, tip, paint);
    };

    // Reusable glyph painters.
    auto paintPlay = [](ImU32 c) {
        return [c](ImDrawList* dl, ImVec2 a, ImVec2 b, bool) {
            const float w = b.x - a.x, hh = b.y - a.y;
            dl->AddTriangleFilled(ImVec2(a.x + w * 0.08f, a.y),
                                  ImVec2(a.x + w * 0.08f, b.y),
                                  ImVec2(b.x, a.y + hh * 0.5f), c);
        };
    };
    auto paintStop = [](ImU32 c) {
        return [c](ImDrawList* dl, ImVec2 a, ImVec2 b, bool) {
            dl->AddRectFilled(a, b, c, (b.x - a.x) * 0.14f);
        };
    };
    // Small downward caret centered in the (narrow) button rect.
    auto paintCaret = [h](ImU32 c) {
        return [c, h](ImDrawList* dl, ImVec2 a, ImVec2 b, bool) {
            const float cx = (a.x + b.x) * 0.5f, cy = (a.y + b.y) * 0.5f;
            const float s = h * 0.13f;
            dl->AddTriangleFilled(ImVec2(cx - s, cy - s * 0.5f),
                                  ImVec2(cx + s, cy - s * 0.5f),
                                  ImVec2(cx, cy + s * 0.7f), c);
        };
    };

    // A Play button immediately followed by a narrow caret dropdown. `run` is
    // the default (build + run) action; the caret opens `popupId`, whose menu
    // items the caller renders after all buttons (BeginPopup below). Anchors
    // the popup just under the caret via `anchor`.
    const float caretW = h * 0.55f;
    auto playWithMenu = [&](const char* playId, const char* caretId,
                            const char* popupId, ImVec2& anchor, bool enabled,
                            ImU32 color, const char* tip, auto&& onRun) {
        if (button(playId, gapGroup, h, enabled, tip,
                   paintPlay(enabled ? color : colDim)))
            onRun();
        ImGui::SameLine(0.0f, 1.0f);
        const ImVec2 cp = ImGui::GetCursorScreenPos();
        anchor = ImVec2(cp.x, cp.y + h);
        if (button(caretId, 1.0f, caretW, enabled, "More run options...",
                   paintCaret(enabled ? colText : colDim)))
            ImGui::OpenPopup(popupId);
    };

    // Save (floppy): normal when clean, amber when there are unsaved edits.
    // Always enabled - saving also folds in layout/docking changes.
    if (iconButton(
            "##tb_save", gapGroup, true,
            dirty_ ? "Save - unsaved changes (Ctrl+S)" : "Save (Ctrl+S)",
            [&](ImDrawList* dl, ImVec2 a, ImVec2 b, bool) {
                const ImU32 c = dirty_ ? IM_COL32(240, 175, 70, 255) : colText;
                const float w = b.x - a.x, hh = b.y - a.y;
                dl->AddRect(a, b, c, w * 0.12f, 0, 1.6f);            // body
                dl->AddRectFilled(ImVec2(a.x + w * 0.22f, a.y),       // shutter
                                  ImVec2(a.x + w * 0.68f, a.y + hh * 0.32f), c);
                dl->AddRect(ImVec2(a.x + w * 0.26f, a.y + hh * 0.50f), // label
                            ImVec2(a.x + w * 0.74f, b.y - hh * 0.06f), c, 0, 0,
                            1.6f);
            }))
        saveAll("Saved");

    // Build only (no run) - a hammer, so it reads apart from the Play triangles.
    if (iconButton("##tb_build", gapGroup, !busy,
                   "Build (no run) (Ctrl+Shift+B)",
                   [&](ImDrawList* dl, ImVec2 a, ImVec2 b, bool en) {
                       const ImU32 c = en ? IM_COL32(210, 180, 120, 255) : colDim;
                       const float w = b.x - a.x, hh = b.y - a.y;
                       // handle: lower-left up to the head
                       dl->AddLine(ImVec2(a.x + 0.30f * w, b.y),
                                   ImVec2(a.x + 0.66f * w, a.y + 0.34f * hh), c,
                                   ImMax(2.0f, w * 0.13f));
                       // head: a thick short bar across the top of the handle
                       dl->AddLine(ImVec2(a.x + 0.40f * w, a.y + 0.12f * hh),
                                   ImVec2(b.x, a.y + 0.42f * hh), c,
                                   ImMax(3.0f, w * 0.26f));
                   }))
        runner_.buildAndRun(project_, false);

    // --- Emulator group: green Play (+dropdown) + Stop PCSX2 -------------
    ImVec2 emuMenuAnchor, ps2MenuAnchor;
    playWithMenu("##tb_run_emu", "##tb_run_emu_more", "emu_run_menu",
                 emuMenuAnchor, !busy, IM_COL32(95, 200, 115, 255),
                 "Build && Run in PCSX2 (F5)",
                 [&] { runner_.buildAndRun(project_, true); });
    // Stop PCSX2: cancels a running build, else closes the emulator. Always
    // available (can't detect a stray PCSX2; taskkill is a no-op if none runs).
    if (iconButton("##tb_stop_emu", gapPair, true,
                   busy ? "Cancel build" : "Stop PCSX2", paintStop(colStop))) {
        if (busy) runner_.cancel();
        else runner_.stopEmulator();
    }

    // --- Console group: blue Play (+dropdown) + Stop PS2 ----------------
    // Both disabled until a ps2link IP is configured (Edit > Preferences).
    playWithMenu("##tb_run_ps2", "##tb_run_ps2_more", "ps2_run_menu",
                 ps2MenuAnchor, !busy && ps2Ready, IM_COL32(80, 160, 245, 255),
                 ps2Ready ? "Build && Run on PS2 (F6)"
                          : "Set 'PS2 (ps2link) IP' in Edit > Preferences first.",
                 [&] { runner_.buildAndRunPs2(project_, true); });
    // Stop PS2: cancels a running build, else stops the game on the console
    // (kills the file server + resets ps2link + silences the SPU).
    const bool stopPs2Enabled = busy || ps2Ready;
    if (iconButton("##tb_stop_ps2", gapPair, stopPs2Enabled,
                   busy ? "Cancel build"
                        : ps2Ready ? "Stop the game on the PS2"
                                   : "Set 'PS2 (ps2link) IP' in Edit > Preferences first.",
                   paintStop(stopPs2Enabled ? colStop : colDim))) {
        if (busy) runner_.cancel();
        else runner_.stopPs2(project_);
    }

    // Live Link chip: a dot + label after the run groups, ALSO the on/off
    // switch (clicking toggles the project's Live Link preference, same as
    // Build > Live Link). Shown whenever the project builds in the debug
    // profile: gray "LIVE off" = disabled, dim "LIVE (build)" = enabled but
    // no Live-Link-capable build yet (F5), green "LIVE" = edits stream into
    // the running game, amber "LIVE (rebuild)" = an edit the session can't
    // absorb - rebuild to resync. Hidden in the release profile (the poller
    // only exists in debug builds).
    if (project_.settings.buildProfile == "debug") {
        const bool on = project_.settings.liveLink;
        ImU32 c = colDim;
        const char* label = "LIVE off";
        const char* tip =
            "Live Link is off (project setting) - the game is built without "
            "the poller\nand the editor writes no snapshots. Click to enable "
            "(then Build & Run).";
        if (on) {
            switch (liveLinkState_) {
                case LiveLinkState::Live:
                    c = IM_COL32(95, 200, 115, 255);
                    label = "LIVE";
                    tip = "Live Link: object edits (move/rotate/scale/recolor,"
                          " add/delete) stream\ninto the running game. Click "
                          "to turn off (project setting).";
                    break;
                case LiveLinkState::RebuildNeeded:
                    c = IM_COL32(240, 175, 70, 255);
                    label = "LIVE (rebuild)";
                    tip = "Live Link: the scene changed in a way the session "
                          "can't absorb (model/\nmaterial swaps, lights, "
                          "projected decals, mirrors, layers, objects with\n"
                          "logic...) - Build & Run (F5) to resync. Click to "
                          "turn off.";
                    break;
                default:  // Off (transient) / NoBuild
                    label = "LIVE (build)";
                    tip = "Live Link is on, but the last build has no poller "
                          "yet - Build & Run\n(F5) once and edits start "
                          "streaming. Click to turn off.";
                    break;
            }
        }
        ImGui::SameLine(0.0f, gapGroup);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float dotR = h * 0.14f;
        const float textW = ImGui::CalcTextSize(label).x;
        if (ImGui::InvisibleButton("##tb_livelink",
                                   ImVec2(dotR * 2.0f + 4.0f + textW, h))) {
            project_.settings.liveLink = !on;
            commitChange();
        }
        if (ImGui::IsItemHovered())
            dl->AddRectFilled(p, ImVec2(p.x + dotR * 2.0f + 4.0f + textW,
                                        p.y + h),
                              ImGui::GetColorU32(ImGuiCol_ButtonHovered),
                              round);
        dl->AddCircleFilled(ImVec2(p.x + dotR, p.y + h * 0.5f), dotR, c);
        dl->AddText(ImVec2(p.x + dotR * 2.0f + 4.0f,
                           p.y + (h - ImGui::GetTextLineHeight()) * 0.5f),
                    c, label);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tip);
    }

    // Dropdown menus for the two Play carets (anchored just under each caret).
    ImGui::SetNextWindowPos(emuMenuAnchor);
    if (ImGui::BeginPopup("emu_run_menu")) {
        if (ImGui::MenuItem("Run in PCSX2 (no build)", "Ctrl+F5", false, !busy))
            runner_.runEmulatorOnly(project_);
        if (ImGui::MenuItem("Build (no run)", "Ctrl+Shift+B", false, !busy))
            runner_.buildAndRun(project_, false);
        ImGui::EndPopup();
    }
    ImGui::SetNextWindowPos(ps2MenuAnchor);
    if (ImGui::BeginPopup("ps2_run_menu")) {
        if (ImGui::MenuItem("Run on PS2 (no build)", "Ctrl+F6", false,
                            !busy && ps2Ready))
            runner_.buildAndRunPs2(project_, false);
        if (ImGui::MenuItem("Build (no run)", "Ctrl+Shift+B", false, !busy))
            runner_.buildAndRun(project_, false);
        ImGui::EndPopup();
    }
}

void App::updateProjectedDecals() {
    // Cheap signature of everything a projection depends on: the projecting
    // decals AND every potential receiver's transform/type. Recompute only when
    // it changes (edits, gizmo drags), so the projection - which walks receiver
    // geometry - doesn't run every frame. Pure host work; nothing here reaches
    // the PS2 (see decalproj).
    const SceneData& sc = project_.active();
    bool anyProjecting = false;
    for (const SceneObject& o : sc.objects)
        if (o.type == PrimitiveType::Decal && o.decalProject) { anyProjecting = true; break; }

    uint64_t sig = 1469598103934665603ull;  // FNV-1a seed
    auto mix = [&](uint64_t v) { sig = (sig ^ v) * 1099511628211ull; };
    auto mixf = [&](float f) {
        uint32_t b;
        std::memcpy(&b, &f, sizeof(b));
        mix(b);
    };
    if (anyProjecting) {
        mix(sc.objects.size());
        for (const SceneObject& o : sc.objects) {
            mix((uint64_t)o.type);
            for (int k = 0; k < 3; ++k) { mixf(o.position[k]); mixf(o.rotation[k]); mixf(o.scale[k]); }
            mix((uint64_t)o.primDetail);
            for (char c : o.id) mix((uint8_t)c);
            for (char c : o.modelPath) mix((uint8_t)c);
            if (o.type == PrimitiveType::Decal) {
                mix(o.decalProject ? 2u : 1u);
                for (char c : o.materialPath) mix((uint8_t)c);
            }
        }
        for (float h : sc.heights) mixf(h);  // terrain receiver
    }

    if (sig == projectedDecalsSig_) {
        viewport_.setProjectedDecals(projectedDecals_, projectedDecalsVersion_);
        return;
    }
    projectedDecalsSig_ = sig;
    projectedDecals_.clear();
    if (anyProjecting) {
        for (const SceneObject& o : sc.objects) {
            if (o.type != PrimitiveType::Decal || !o.decalProject || o.materialPath.empty())
                continue;
            decalproj::DecalMesh m = decalproj::project(project_, sc, o);
            if (!m.verts.empty()) projectedDecals_[o.id] = std::move(m.verts);
        }
    }
    ++projectedDecalsVersion_;
    viewport_.setProjectedDecals(projectedDecals_, projectedDecalsVersion_);
}

void App::updateNavOverlay() {
    // Nav-mesh preview (View > Nav Mesh Overlay). Signature of everything the
    // bake depends on - blocking objects, terrain, the nav preferences - so
    // navmesh::bake only reruns on actual edits (same trick as the projected
    // decals). Pure host work; the game bakes its own copy at build time.
    if (!showNavOverlay_) {
        viewport_.setNavOverlay(nullptr, 0);
        return;
    }
    const SceneData& sc = project_.active();
    uint64_t sig = 1469598103934665603ull;  // FNV-1a seed
    auto mix = [&](uint64_t v) { sig = (sig ^ v) * 1099511628211ull; };
    auto mixf = [&](float f) {
        uint32_t b;
        std::memcpy(&b, &f, sizeof(b));
        mix(b);
    };
    mixf(project_.settings.navCellSize);
    mixf(project_.settings.navMaxSlope);
    mixf(project_.settings.navAgentRadius);
    mix((uint64_t)sc.terrain.width);
    mix((uint64_t)sc.terrain.depth);
    mix(sc.objects.size());
    for (const SceneObject& o : sc.objects) {
        mix((uint64_t)o.type);
        mix((uint64_t)o.collisionMode);
        for (int k = 0; k < 3; ++k) { mixf(o.position[k]); mixf(o.scale[k]); }
        for (char c : o.modelPath) mix((uint8_t)c);
    }
    for (float h : sc.heights) mixf(h);

    if (sig != navOverlaySig_) {
        navOverlaySig_ = sig;
        navGrid_ = navmesh::bake(project_, sc);
        ++navOverlayVersion_;
    }
    viewport_.setNavOverlay(&navGrid_, navOverlayVersion_);
}

void App::drawViewportWindow() {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    // NoNav: keep ImGui keyboard navigation out of the viewport. Otherwise the
    // arrow keys cycle focus through the overlay tool buttons (Move/Rotate/...)
    // instead of - or on top of - flying the camera when arrow-key movement is
    // selected. The buttons all have 1/2/3/5 hotkeys, so nothing is lost.
    ImGui::Begin("Viewport", nullptr, ImGuiWindowFlags_NoNav);
    ImGui::PopStyleVar();

    if (!hasProject_) {
        ImGui::Dummy(ImVec2(0, 40));
        ImGui::Indent(30);
        ImGui::TextDisabled("No project open.");
        ImGui::TextDisabled("File > New Project (Ctrl+N) to create one.");
        ImGui::Unindent(30);
        ImGui::End();
        return;
    }

    ImVec2 avail = ImGui::GetContentRegionAvail();
    if (avail.x >= 8 && avail.y >= 8) {
        // Color grading preview: the preset selected in the Color Grading
        // window wins over the project default while that window is open.
        {
            int gi = project_.defaultGrading;
            if (showGradingEditor_ && selectedGrading_ >= 0) gi = selectedGrading_;
            const bool on =
                gradingPreview_ && gi >= 0 && gi < (int)project_.gradings.size();
            viewport_.setGrading(
                on, on ? compileGrading(project_.gradings[gi]) : CompiledGrading{});
        }
        // Ambience preview: the preset selected in the Ambience Editor wins
        // over the scene's sky/lighting/fog while that window is open, then
        // the scene's own values are restored once.
        {
            const bool preview = showAmbienceEditor_ && ambiencePreview_ &&
                                 selectedAmbience_ >= 0 &&
                                 selectedAmbience_ < (int)project_.ambiencePresets.size();
            if (preview) {
                const AmbiencePreset& a = project_.ambiencePresets[selectedAmbience_];
                viewport_.setSky(a.skyColor, a.skyTopColor, a.skyDome, a.zenithSize);
                viewport_.setLighting(a.lightDir, a.ambient, a.diffuse, a.lightColor,
                                      a.brightness);
                viewport_.setFog(a.fogEnabled && showFog_, a.fogColor, a.fogStart, a.fogEnd);
                ambiencePreviewPushed_ = true;
            } else if (ambiencePreviewPushed_) {
                ambiencePreviewPushed_ = false;
                applyProjectToViewport();  // restore the scene's own ambience
            }
        }
        // Layer eye toggles: objects on hidden layers vanish from the render
        // and the click picking (mask indices parallel project_.objects()).
        {
            std::vector<char> hidden(project_.objects().size(), 0);
            for (size_t i = 0; i < project_.objects().size(); ++i)
                hidden[i] = isObjectHiddenInEditor(project_.objects()[i]) ? 1 : 0;
            viewport_.setHiddenMask(std::move(hidden));
        }
        // Cutscene Director preview: pose the objects (and maybe fly the
        // camera) at the playhead. Returns the raw objects when not previewing.
        const std::vector<SceneObject>& renderObjects = cutscenePosedObjects();
        // Look-through camera ("View:" overlay / camera Properties): render
        // from the chosen Camera entity's pose + FOV. The cutscene camera
        // track wins while it previews; reading the POSED objects means a
        // dollied camera entity is followed live. A stale name (deleted
        // entity) falls back to the free orbit camera.
        if (!seqCameraPushed_) {
            const SceneObject* cam = nullptr;
            if (!lookThroughCam_.empty())
                for (const SceneObject& o : renderObjects)
                    if (o.name == lookThroughCam_ &&
                        o.type == PrimitiveType::Camera) {
                        cam = &o;
                        break;
                    }
            if (cam) {
                float fwd[3], at[3];
                seqCameraForward(cam->rotation, fwd);
                for (int c = 0; c < 3; ++c) at[c] = cam->position[c] + fwd[c];
                viewport_.setCameraOverride(cam->position, at, cam->cameraFov);
            } else {
                viewport_.clearCameraOverride();
            }
        }
        // Hide the camera(s) we are previewing through so their model doesn't
        // fill the frame: during a cutscene camera preview, every camera the
        // sequence films from; otherwise the single looked-through camera.
        {
            std::vector<std::string> hideCams;
            if (seqCameraPushed_ && selectedSequence_ >= 0 &&
                selectedSequence_ < (int)project_.sequences.size()) {
                for (const SeqCameraKey& k :
                     project_.sequences[selectedSequence_].cameraKeys)
                    if (!k.camera.empty()) hideCams.push_back(k.camera);
            } else if (!seqCameraPushed_ && !lookThroughCam_.empty()) {
                hideCams.push_back(lookThroughCam_);
            }
            viewport_.setHiddenCameras(std::move(hideCams));
        }
        updateProjectedDecals();
        updateNavOverlay();
        uint32_t tex = viewport_.render((int)avail.x, (int)avail.y, renderObjects,
                                        selection_, selectedObject_);
        // Flip vertically: GL texture origin is bottom-left
        ImGui::Image((ImTextureID)(intptr_t)tex, avail, ImVec2(0, 1), ImVec2(1, 0));

        const ImVec2 imgPos = ImGui::GetItemRectMin();
        const bool imageHovered = ImGui::IsItemHovered();
        ImGuiIO& io = ImGui::GetIO();

        // Cutscene Director: widescreen bars + fade-to-black overlay, drawn
        // over the viewport image with the same coverage the PS2 composites.
        if (seqBarsNow_ > 0.0f || seqFadeNow_ > 0.0f) {
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const ImVec2 br(imgPos.x + avail.x, imgPos.y + avail.y);
            const ImU32 black = IM_COL32(0, 0, 0, 255);
            if (seqBarsNow_ > 0.0f) {
                float ft, fb, fl, fr;
                seqBarsFractions(seqBarsStyleNow_, ft, fb, fl, fr);
                const float t = ft * seqBarsNow_ * avail.y;
                const float b = fb * seqBarsNow_ * avail.y;
                const float l = fl * seqBarsNow_ * avail.x;
                const float r = fr * seqBarsNow_ * avail.x;
                if (t > 0) dl->AddRectFilled(imgPos, ImVec2(br.x, imgPos.y + t), black);
                if (b > 0) dl->AddRectFilled(ImVec2(imgPos.x, br.y - b), br, black);
                if (l > 0) dl->AddRectFilled(imgPos, ImVec2(imgPos.x + l, br.y), black);
                if (r > 0) dl->AddRectFilled(ImVec2(br.x - r, imgPos.y), br, black);
            }
            if (seqFadeNow_ > 0.0f)
                dl->AddRectFilled(imgPos, br,
                                  IM_COL32(0, 0, 0, (int)(seqFadeNow_ * 255.0f)));
        }

        // --- Terrain sculpting brush ---
        bool brushHit = false;
        float brushX = 0.0f, brushZ = 0.0f;
        if (sculptMode_ && imageHovered) {
            const float u = (io.MousePos.x - imgPos.x) / avail.x;
            const float v = (io.MousePos.y - imgPos.y) / avail.y;
            brushHit = viewport_.terrainRaycast(u, v, brushX, brushZ);

            if (brushHit && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                if (sculptFlatten_) {
                    // level toward the target height; strength = lerp rate
                    project::flattenHeightmap(project_, brushX, brushZ, brushRadius_,
                                              flattenHeight_, brushStrength_);
                } else {
                    const float delta = io.KeyShift ? -brushStrength_ : brushStrength_;
                    project::sculptHeightmap(project_, brushX, brushZ, brushRadius_,
                                             delta);
                }
                // Live rebuild of just the chunks under the brush - a full
                // applyProjectToViewport would rebuild the whole map per frame.
                viewport_.updateTerrainRegion(project_.active().heights, brushX, brushZ,
                                              brushRadius_);
                sculptStroke_ = true;
            }
        }
        if (sculptStroke_ && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            sculptStroke_ = false;
            commitChange();  // one undo step per finished brush stroke
            statusMessage_ = "Terrain sculpted";
        }

        // brush ring projected onto the terrain
        if (sculptMode_ && brushHit) {
            auto worldToImage = [&](float wx, float wy, float wz, ImVec2& out) {
                const float* V = viewport_.viewMatrix();
                const float* P = viewport_.projMatrix();
                const float vx = V[0] * wx + V[4] * wy + V[8] * wz + V[12];
                const float vy = V[1] * wx + V[5] * wy + V[9] * wz + V[13];
                const float vz = V[2] * wx + V[6] * wy + V[10] * wz + V[14];
                const float cx = P[0] * vx + P[4] * vy + P[8] * vz + P[12];
                const float cy = P[1] * vx + P[5] * vy + P[9] * vz + P[13];
                const float cw = P[3] * vx + P[7] * vy + P[11] * vz + P[15];
                if (cw <= 0.001f) return false;
                out = ImVec2(imgPos.x + (cx / cw * 0.5f + 0.5f) * avail.x,
                             imgPos.y + (1.0f - (cy / cw * 0.5f + 0.5f)) * avail.y);
                return true;
            };
            ImDrawList* dl = ImGui::GetWindowDrawList();
            ImVec2 prev;
            bool prevOk = false;
            for (int s = 0; s <= 32; ++s) {
                const float a = (float)s / 32.0f * 6.2831853f;
                const float px = brushX + std::cos(a) * brushRadius_;
                const float pz = brushZ + std::sin(a) * brushRadius_;
                ImVec2 pt;
                const bool ok =
                    worldToImage(px, viewport_.terrainHeight(px, pz) + 0.1f, pz, pt);
                if (ok && prevOk)
                    dl->AddLine(prev, pt, IM_COL32(255, 200, 40, 220), 2.0f);
                prev = pt;
                prevOk = ok;
            }
        }

        // --- Transform gizmo on the selection (disabled while sculpting;
        // objects on a hidden layer can't be grabbed either) ---
        bool objectSelected = !sculptMode_ && selectedObject_ >= 0 &&
                              selectedObject_ < (int)project_.objects().size() &&
                              !isObjectHiddenInEditor(project_.objects()[selectedObject_]);
        if (objectSelected) {
            SceneObject& o = project_.objects()[selectedObject_];

            ImGuizmo::SetOrthographic(false);
            ImGuizmo::SetDrawlist();
            ImGuizmo::SetRect(imgPos.x, imgPos.y, avail.x, avail.y);

            const ImGuizmo::OPERATION ops[] = {ImGuizmo::TRANSLATE, ImGuizmo::ROTATE,
                                               ImGuizmo::SCALE};
            const ImGuizmo::OPERATION op = ops[gizmoOp_];

            // Hold Ctrl to snap: 0.5 units / 15 degrees / 0.25 scale
            const float snapValues[3] = {op == ImGuizmo::ROTATE ? 15.0f
                                         : op == ImGuizmo::SCALE ? 0.25f
                                                                 : 0.5f,
                                         op == ImGuizmo::TRANSLATE ? 0.5f : 0.0f,
                                         op == ImGuizmo::TRANSLATE ? 0.5f : 0.0f};
            const float* snap = io.KeyCtrl ? snapValues : nullptr;

            if (selection_.size() > 1) {
                // Multiple objects: manipulate a proxy at the group's centroid,
                // then apply the resulting world-space delta to every selected
                // object's TRS - so the group translates / rotates / scales
                // about the pivot as one rigid arrangement. (float[16] are
                // column-major; element (row r, col c) is at [c*4 + r].)
                auto mulMat4 = [](const float* A, const float* B, float* C) {
                    for (int c = 0; c < 4; ++c)
                        for (int r = 0; r < 4; ++r) {
                            float s = 0.0f;
                            for (int k = 0; k < 4; ++k) s += A[k * 4 + r] * B[c * 4 + k];
                            C[c * 4 + r] = s;
                        }
                };
                float pivot[3] = {0.0f, 0.0f, 0.0f};
                for (int idx : selection_)
                    for (int k = 0; k < 3; ++k)
                        pivot[k] += project_.objects()[idx].position[k];
                for (int k = 0; k < 3; ++k) pivot[k] /= (float)selection_.size();

                const float unitRot[3] = {0.0f, 0.0f, 0.0f};
                const float unitScale[3] = {1.0f, 1.0f, 1.0f};
                float proxy[16], delta[16];
                ImGuizmo::RecomposeMatrixFromComponents(pivot, unitRot, unitScale, proxy);
                if (ImGuizmo::Manipulate(viewport_.viewMatrix(), viewport_.projMatrix(),
                                         op, ImGuizmo::WORLD, proxy, delta, snap)) {
                    for (int idx : selection_) {
                        SceneObject& so = project_.objects()[idx];
                        float model[16], out[16];
                        ImGuizmo::RecomposeMatrixFromComponents(so.position, so.rotation,
                                                                so.scale, model);
                        mulMat4(delta, model, out);  // out = delta * model
                        ImGuizmo::DecomposeMatrixToComponents(out, so.position,
                                                              so.rotation, so.scale);
                        for (float& s : so.scale)
                            if (s < 0.01f) s = 0.01f;
                    }
                }
            } else if (gizmoSpace_ == 0) {
                // Absolute: move and rotate along the world axes. ImGuizmo
                // forces SCALE onto the object's own axes regardless of the
                // mode (world-axis scale would skew the TRS matrix).
                // Same TRS composition as the viewport / PS2 code.
                float model[16];
                ImGuizmo::RecomposeMatrixFromComponents(o.position, o.rotation, o.scale,
                                                        model);
                if (ImGuizmo::Manipulate(viewport_.viewMatrix(), viewport_.projMatrix(),
                                         op, ImGuizmo::WORLD, model, nullptr, snap)) {
                    ImGuizmo::DecomposeMatrixToComponents(model, o.position, o.rotation,
                                                          o.scale);
                }
            } else {
                // Camera-relative: ImGuizmo only knows LOCAL/WORLD frames, so
                // manipulate a unit-scale proxy whose local frame is the
                // camera frame, then map the result back onto the object.
                // (float[16] here are OpenGL-style column-major, so the proxy
                // rotation is the transpose of the view rotation.)
                const float* view = viewport_.viewMatrix();
                float proxy[16] = {};
                for (int r = 0; r < 3; ++r)
                    for (int c = 0; c < 3; ++c) proxy[c * 4 + r] = view[r * 4 + c];
                for (int i = 0; i < 3; ++i) proxy[12 + i] = o.position[i];
                proxy[15] = 1.0f;

                // Scale deltas are cumulative over the whole drag - remember
                // the object's scale from before the drag started.
                if (!ImGuizmo::IsUsing())
                    for (int i = 0; i < 3; ++i) gizmoDragScale0_[i] = o.scale[i];

                float delta[16];
                if (ImGuizmo::Manipulate(view, viewport_.projMatrix(), op,
                                         ImGuizmo::LOCAL, proxy, delta, snap)) {
                    if (op == ImGuizmo::TRANSLATE) {
                        // Translation lands directly in the proxy position
                        for (int i = 0; i < 3; ++i) o.position[i] = proxy[12 + i];
                    } else if (op == ImGuizmo::ROTATE) {
                        // World-space delta W = proxyOut * proxyIn^-1; proxyIn
                        // is the camera rotation, whose inverse is the view
                        // rotation itself. Rotating about the object's own
                        // position keeps it in place, so only the linear part
                        // of the model changes: model' = W * model.
                        float W[3][3];
                        for (int r = 0; r < 3; ++r)
                            for (int c = 0; c < 3; ++c) {
                                float s = 0.0f;
                                for (int k = 0; k < 3; ++k)
                                    s += proxy[k * 4 + r] * view[c * 4 + k];
                                W[r][c] = s;
                            }
                        float model[16];
                        ImGuizmo::RecomposeMatrixFromComponents(o.position, o.rotation,
                                                                o.scale, model);
                        float rotated[16];
                        std::memcpy(rotated, model, sizeof(rotated));
                        for (int r = 0; r < 3; ++r)
                            for (int c = 0; c < 3; ++c) {
                                float s = 0.0f;
                                for (int k = 0; k < 3; ++k)
                                    s += W[r][k] * model[c * 4 + k];
                                rotated[c * 4 + r] = s;
                            }
                        // W is orthogonal, so position and scale are unchanged
                        // by construction - only take the rotation to avoid
                        // accumulating float drift in the other components.
                        float pos[3], scl[3];
                        ImGuizmo::DecomposeMatrixToComponents(rotated, pos, o.rotation,
                                                              scl);
                    } else {
                        // Per-camera-axis scale cannot be stored in a TRS
                        // (it shears rotated objects), so apply the dominant
                        // drag factor uniformly to all object axes.
                        float f = 1.0f, best = 0.0f;
                        for (int i = 0; i < 3; ++i) {
                            const float s = delta[i * 5];  // diagonal: 0, 5, 10
                            if (std::fabs(s - 1.0f) > best) {
                                best = std::fabs(s - 1.0f);
                                f = s;
                            }
                        }
                        for (int i = 0; i < 3; ++i)
                            o.scale[i] = gizmoDragScale0_[i] * f;
                    }
                }
            }
            for (float& s : o.scale)
                if (s < 0.01f) s = 0.01f;
        }

        // Commit once per completed gizmo drag (not every frame). Auto-key
        // first: the dropped cutscene keys share the drag's undo snapshot.
        const bool usingGizmo = ImGuizmo::IsUsing();
        if (gizmoWasUsing_ && !usingGizmo) {
            cutsceneAutoKey();
            commitChange();
        }
        gizmoWasUsing_ = usingGizmo;

        const bool gizmoBusy = usingGizmo || (objectSelected && ImGuizmo::IsOver());

        // --- Camera + selection input ---
        if (imageHovered && !gizmoBusy) {
            // Which drag orbits vs pans depends on the chosen navigation scheme
            // (see NavScheme). Sculpt mode paints with LMB, so any scheme that
            // orbits on plain LMB yields it to the brush. The wheel always
            // zooms; some schemes add a drag-dolly.
            const bool lmb = ImGui::IsMouseDragging(ImGuiMouseButton_Left);
            const bool rmb = ImGui::IsMouseDragging(ImGuiMouseButton_Right);
            const bool mmb = ImGui::IsMouseDragging(ImGuiMouseButton_Middle);
            const bool alt = io.KeyAlt, shift = io.KeyShift;
            bool doOrbit = false, doPan = false;
            float dolly = 0.0f;  // extra wheel-equivalent zoom from a drag
            switch (nav_.scheme) {
                case NavScheme::Blender:
                    doOrbit = mmb && !shift;
                    doPan = mmb && shift;
                    break;
                case NavScheme::Maya:
                    doOrbit = alt && lmb;
                    doPan = alt && mmb;
                    if (alt && rmb) dolly = io.MouseDelta.x * 0.02f;  // drag right = in
                    break;
                case NavScheme::Unity:
                    doOrbit = rmb;
                    doPan = mmb;
                    break;
                case NavScheme::Default:
                default:
                    // Left-drag is reserved for rubber-band selection; orbit on
                    // right-drag (as this scheme already supported).
                    doOrbit = rmb;
                    doPan = mmb;
                    break;
            }
            if (doOrbit) {
                const float sx = nav_.orbitSensitivity * (nav_.invertX ? -1.0f : 1.0f);
                const float sy = nav_.orbitSensitivity * (nav_.invertY ? -1.0f : 1.0f);
                viewport_.orbit(io.MouseDelta.x * sx, io.MouseDelta.y * sy);
            }
            if (doPan)
                viewport_.pan(io.MouseDelta.x * nav_.panSensitivity,
                              io.MouseDelta.y * nav_.panSensitivity);
            if (io.MouseWheel != 0.0f || dolly != 0.0f)
                viewport_.zoom(io.MouseWheel * nav_.zoomSensitivity + dolly);

            // Begin a rubber-band box when a left-drag starts and the left
            // button is not driving the camera in this scheme (only Maya's
            // Alt+LMB does) and we're not sculpting.
            const bool lmbCamera = (nav_.scheme == NavScheme::Maya) && alt;
            if (!sculptMode_ && !lmbCamera &&
                ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                boxSelecting_ = true;
        }

        // Rubber-band box select: tracked until the button is released, even if
        // the cursor leaves the image. A left-drag past the click threshold
        // draws a marquee and selects the overlapped objects on release; a plain
        // click (no drag) falls through to single-object picking below.
        if (boxSelecting_) {
            const ImVec2 a = io.MouseClickedPos[ImGuiMouseButton_Left];
            const ImVec2 b = io.MousePos;
            const bool dragged =
                io.MouseDragMaxDistanceSqr[ImGuiMouseButton_Left] >= 9.0f;
            if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                if (dragged) {
                    ImDrawList* dl = ImGui::GetWindowDrawList();
                    const ImVec2 mn(std::min(a.x, b.x), std::min(a.y, b.y));
                    const ImVec2 mx(std::max(a.x, b.x), std::max(a.y, b.y));
                    dl->AddRectFilled(mn, mx, IM_COL32(120, 170, 255, 40));
                    dl->AddRect(mn, mx, IM_COL32(120, 170, 255, 200));
                }
            } else {
                if (dragged) selectObjectsInBox(a, b, imgPos, avail, io.KeyCtrl);
                boxSelecting_ = false;
            }
        }

        // Click (no drag) = pick object under cursor. Ctrl toggles it in the
        // current selection; a plain click replaces (empty click clears).
        if (imageHovered && !gizmoBusy && !sculptMode_ &&
            ImGui::IsMouseReleased(ImGuiMouseButton_Left) &&
            io.MouseDragMaxDistanceSqr[ImGuiMouseButton_Left] < 9.0f) {
            const float u = (io.MousePos.x - imgPos.x) / avail.x;
            const float v = (io.MousePos.y - imgPos.y) / avail.y;
            const int hit = viewport_.pick(u, v, project_.objects());
            if (io.KeyCtrl) {
                if (hit >= 0) toggleSelect(hit);
            } else {
                selectOnly(hit);
            }
        }

        // Orbit around the selected object: snap the pivot to it whenever the
        // selection changes (independent of the transform gizmo mode). Panning
        // or flying afterward still moves freely until the next selection.
        {
            const bool objSel =
                selectedObject_ >= 0 && selectedObject_ < (int)project_.objects().size();
            if (nav_.orbitAroundSelection && objSel && selectedObject_ != navFocusedIndex_) {
                viewport_.setTarget(project_.objects()[selectedObject_].position);
                navFocusedIndex_ = selectedObject_;
            }
            if (!objSel) navFocusedIndex_ = -1;
        }

        // --- TV frames: what a PAL / NTSC set shows of the 512x448 buffer ---
        // PAL fills a 4:3 screen exactly; NTSC has fewer active lines, so the
        // same buffer looks a touch wider (~10:7). Rough approximations for
        // composition. The HUD preview maps into the PAL frame when shown.
        ImVec2 frameMin = imgPos, frameSize = avail;
        if (showPal_ || showNtsc_) {
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const ImVec2 imgMax(imgPos.x + avail.x, imgPos.y + avail.y);
            auto fitFrame = [&](float aspect, ImVec2& fMin, ImVec2& fSize) {
                float fw = avail.x, fh = avail.y;
                if (fw / fh > aspect)
                    fw = fh * aspect;
                else
                    fh = fw / aspect;
                fMin = ImVec2(imgPos.x + (avail.x - fw) * 0.5f,
                              imgPos.y + (avail.y - fh) * 0.5f);
                fSize = ImVec2(fw, fh);
            };
            ImVec2 palMin, palSize, ntscMin, ntscSize;
            fitFrame(4.0f / 3.0f, palMin, palSize);
            fitFrame(480.0f / 448.0f * 4.0f / 3.0f, ntscMin, ntscSize);

            // dim outside the union of the active frames
            ImVec2 uMin = showPal_ ? palMin : ntscMin;
            ImVec2 uSize = showPal_ ? palSize : ntscSize;
            if (showPal_ && showNtsc_) {
                uMin = ImVec2(std::min(palMin.x, ntscMin.x), std::min(palMin.y, ntscMin.y));
                const float ux2 = std::max(palMin.x + palSize.x, ntscMin.x + ntscSize.x);
                const float uy2 = std::max(palMin.y + palSize.y, ntscMin.y + ntscSize.y);
                uSize = ImVec2(ux2 - uMin.x, uy2 - uMin.y);
            }
            const ImVec2 uMax(uMin.x + uSize.x, uMin.y + uSize.y);
            const ImU32 dim = IM_COL32(0, 0, 0, 110);
            dl->AddRectFilled(imgPos, ImVec2(imgMax.x, uMin.y), dim);
            dl->AddRectFilled(ImVec2(imgPos.x, uMax.y), imgMax, dim);
            dl->AddRectFilled(ImVec2(imgPos.x, uMin.y), ImVec2(uMin.x, uMax.y), dim);
            dl->AddRectFilled(ImVec2(uMax.x, uMin.y), ImVec2(imgMax.x, uMax.y), dim);

            auto outline = [&](const ImVec2& fMin, const ImVec2& fSize, ImU32 col,
                               const char* label) {
                const ImVec2 fMax(fMin.x + fSize.x, fMin.y + fSize.y);
                dl->AddRect(fMin, fMax, col);
                dl->AddText(ImVec2(fMin.x + 4, fMax.y - 18), col, label);
            };
            if (showPal_)
                outline(palMin, palSize, IM_COL32(255, 255, 255, 150), "PAL");
            if (showNtsc_)
                outline(ntscMin, ntscSize, IM_COL32(255, 220, 90, 170), "NTSC");

            // HUD/screen mapping frame: PAL when active, NTSC otherwise
            frameMin = showPal_ ? palMin : ntscMin;
            frameSize = showPal_ ? palSize : ntscSize;
        }

        // --- HUD preview overlay (matches the PS2 512x448 screen mapping;
        // hidden by default - toggle in the UI Editor, which also shows it
        // while open) ---
        if (showHudInEditor_ || showUiEditor_) {
            ImDrawList* dl = ImGui::GetWindowDrawList();
            auto screenRect = [&](const float* pos, const float* size,
                                  ImVec2& pMin, ImVec2& pMax) {
                const float w = size[0] / 512.0f * frameSize.x;
                const float h = size[1] / 448.0f * frameSize.y;
                const ImVec2 c(frameMin.x + pos[0] * frameSize.x,
                               frameMin.y + pos[1] * frameSize.y);
                pMin = ImVec2(c.x - w * 0.5f, c.y - h * 0.5f);
                pMax = ImVec2(c.x + w * 0.5f, c.y + h * 0.5f);
            };
            for (int i = 0; i < (int)project_.hud.size(); ++i) {
                const HudImage& hi = project_.hud[i];
                ImVec2 pMin, pMax;
                screenRect(hi.pos, hi.size, pMin, pMax);
                if (const HudTexture* t = hudTexture(hi.imagePath))
                    dl->AddImage((ImTextureID)(intptr_t)t->tex, pMin, pMax);
                else
                    dl->AddRect(pMin, pMax, IM_COL32(255, 100, 100, 200));
                if (showUiEditor_ && uiFxSel_ == 0 && i == selectedHud_)
                    dl->AddRect(pMin, pMax, IM_COL32(255, 160, 30, 255), 0.0f, 0, 2.0f);
            }
            // The USE prompt (custom image or the embedded built-in sprite);
            // in the game it only shows near usable objects - here it is a
            // layout aid, drawn whenever the overlay is on.
            {
                const HudImage& up = project_.usePrompt;
                ImVec2 pMin, pMax;
                screenRect(up.pos, up.size, pMin, pMax);
                const HudTexture* t = up.imagePath.empty()
                                          ? builtinUseTexture()
                                          : hudTexture(up.imagePath);
                if (t)
                    dl->AddImage((ImTextureID)(intptr_t)t->tex, pMin, pMax);
                else
                    dl->AddRect(pMin, pMax, IM_COL32(255, 100, 100, 200));
                if (showUiEditor_ && uiFxSel_ == 3)
                    dl->AddRect(pMin, pMax, IM_COL32(255, 160, 30, 255), 0.0f, 0,
                                2.0f);
            }
            // On-screen texts: the ones visible at game start, plus the one
            // being edited (so hidden subtitles can still be placed).
            for (int i = 0; i < (int)project_.hudTexts.size(); ++i) {
                const HudText& ht = project_.hudTexts[i];
                const bool isSel =
                    showUiEditor_ && uiFxSel_ == 4 && i == selectedText_;
                if (!ht.visibleAtStart && !isSel) continue;
                if (const HudTexture* t = hudTextTexture(ht)) {
                    const float size[2] = {(float)t->w, (float)t->h};
                    ImVec2 pMin, pMax;
                    screenRect(ht.pos, size, pMin, pMax);
                    dl->AddImage((ImTextureID)(intptr_t)t->tex, pMin, pMax);
                    if (isSel)
                        dl->AddRect(pMin, pMax, IM_COL32(255, 160, 30, 255),
                                    0.0f, 0, 2.0f);
                }
            }
        }

        // --- Tools overlay (top-left corner of the viewport) ---
        // Transform gizmo modes + the sculpt toggle share this row; they map to
        // the 1-4 shortcuts. Render mode and the TV-safe frames now live in the
        // menu bar (View menu); the gizmo axis space and the camera-recenter
        // buttons sit in the bottom corners below.
        ImGui::SetCursorScreenPos(ImVec2(imgPos.x + 8, imgPos.y + 8));
        const char* toolNames[] = {"Move (1)", "Rotate (2)", "Scale (3)"};
        for (int i = 0; i < 3; ++i) {
            if (i) ImGui::SameLine();
            const bool active = gizmoOp_ == i;
            if (active)
                ImGui::PushStyleColor(ImGuiCol_Button,
                                      ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
            if (ImGui::SmallButton(toolNames[i])) gizmoOp_ = i;
            if (active) ImGui::PopStyleColor();
        }

        // Terrain sculpting toggle stays with the tools (shortcut 4).
        ImGui::SameLine(0.0f, 24.0f);
        if (sculptMode_)
            ImGui::PushStyleColor(ImGuiCol_Button,
                                  ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
        if (ImGui::SmallButton("Sculpt (4)")) sculptMode_ = !sculptMode_;
        if (sculptMode_) ImGui::PopStyleColor();

        // Geometry for the bottom-corner overlays. SmallButton keeps
        // FramePadding.x, so its width is the label plus twice that padding.
        auto smallBtnW = [](const char* s) {
            return ImGui::CalcTextSize(s).x + ImGui::GetStyle().FramePadding.x * 2.0f;
        };
        const float bottomY = imgPos.y + avail.y - ImGui::GetFrameHeight() - 8.0f;

        // --- Camera recenter (bottom-left) ---
        ImGui::SetCursorScreenPos(ImVec2(imgPos.x + 8, bottomY));
        if (ImGui::SmallButton("Center view")) viewport_.resetView();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Reset the camera to the terrain center\n"
                              "with the default orientation and zoom.");
        {
            const bool objSel = selectedObject_ >= 0 &&
                                selectedObject_ < (int)project_.objects().size();
            ImGui::SameLine();
            ImGui::BeginDisabled(!objSel);
            if (ImGui::SmallButton("Center selection") && objSel) {
                viewport_.setTarget(project_.objects()[selectedObject_].position);
                navFocusedIndex_ = selectedObject_;  // keep orbit-around-selection in sync
            }
            ImGui::EndDisabled();
            if (objSel && ImGui::IsItemHovered())
                ImGui::SetTooltip("Move the camera pivot to the selected object.");
        }

        // --- Look-through camera (next to the recenter buttons) ---
        // Shown as soon as the scene has a Camera entity: render the preview
        // from a chosen camera (its pose + FOV, live), "Free" returns to the
        // orbit camera. The Cutscene Director camera preview overrides it.
        {
            bool anyCam = !lookThroughCam_.empty();
            for (const SceneObject& o : project_.objects())
                if (o.type == PrimitiveType::Camera) {
                    anyCam = true;
                    break;
                }
            if (anyCam) {
                ImGui::SameLine(0.0f, 16.0f);
                const std::string viewLbl =
                    "View: " + (lookThroughCam_.empty() ? std::string("Free")
                                                        : lookThroughCam_);
                if (ImGui::SmallButton(viewLbl.c_str()))
                    ImGui::OpenPopup("##lookthrough");
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip(
                        "Render the viewport through a Camera entity (its\n"
                        "position, rotation and FOV, followed live). Pick\n"
                        "\"Free camera\" to return to the orbit camera.");
                if (ImGui::BeginPopup("##lookthrough")) {
                    if (ImGui::MenuItem("Free camera", nullptr,
                                        lookThroughCam_.empty()))
                        lookThroughCam_.clear();
                    ImGui::Separator();
                    for (const SceneObject& o : project_.objects())
                        if (o.type == PrimitiveType::Camera)
                            if (ImGui::MenuItem(o.name.c_str(), nullptr,
                                                o.name == lookThroughCam_))
                                lookThroughCam_ = o.name;
                    ImGui::EndPopup();
                }
            }
        }

        // --- Gizmo axis space (bottom-right) ---
        const char* spaceNames[] = {"World", "Camera"};
        const char* spaceTips[] = {
            "Absolute axes: move and rotate along the world X/Y/Z.\n"
            "Scale always works on the object's own axes. Toggle with 5.",
            "Camera-relative axes: move along the view right/up/forward\n"
            "and rotate around them; scale is uniform. Toggle with 5."};
        const float spaceW = smallBtnW(spaceNames[0]) + ImGui::GetStyle().ItemSpacing.x +
                             smallBtnW(spaceNames[1]);
        ImGui::SetCursorScreenPos(ImVec2(imgPos.x + avail.x - spaceW - 8.0f, bottomY));
        for (int i = 0; i < 2; ++i) {
            if (i) ImGui::SameLine();
            const bool active = gizmoSpace_ == i;
            if (active)
                ImGui::PushStyleColor(ImGuiCol_Button,
                                      ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
            if (ImGui::SmallButton(spaceNames[i])) gizmoSpace_ = i;
            if (active) ImGui::PopStyleColor();
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", spaceTips[i]);
        }

        if (sculptMode_) {
            ImGui::SetCursorScreenPos(ImVec2(imgPos.x + 8, imgPos.y + 32));
            ImGui::SetNextItemWidth(140.0f);
            ImGui::SliderFloat("Radius", &brushRadius_, 1.0f, 30.0f, "%.1f");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(140.0f);
            ImGui::SliderFloat("Strength", &brushStrength_, 0.01f, 0.5f, "%.2f");
            ImGui::SameLine();
            ImGui::Checkbox("Flatten", &sculptFlatten_);
            if (sculptFlatten_) {
                ImGui::SameLine();
                ImGui::SetNextItemWidth(90.0f);
                ImGui::DragFloat("Level", &flattenHeight_, 0.1f, -100.0f, 100.0f,
                                 "%.1f");
                ImGui::SameLine();
                ImGui::TextDisabled("LMB level to height, RMB orbit");
            } else {
                ImGui::SameLine();
                ImGui::TextDisabled("LMB raise, Shift+LMB lower, RMB orbit");
            }
        }

        // --- Keyboard shortcuts (viewport hovered, not typing) ---
        if (ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) && !io.WantTextInput) {
            if (ImGui::IsKeyPressed(ImGuiKey_1)) gizmoOp_ = 0;
            if (ImGui::IsKeyPressed(ImGuiKey_2)) gizmoOp_ = 1;
            if (ImGui::IsKeyPressed(ImGuiKey_3)) gizmoOp_ = 2;
            if (ImGui::IsKeyPressed(ImGuiKey_4)) sculptMode_ = !sculptMode_;
            if (ImGui::IsKeyPressed(ImGuiKey_5)) gizmoSpace_ = 1 - gizmoSpace_;

            // Fly the camera over the terrain. WASD or the arrow keys per the
            // navigation preference (tools live on 1-5, so WASD stays free).
            const bool arrows = nav_.moveKeys == NavMoveKeys::Arrows;
            const ImGuiKey kF = arrows ? ImGuiKey_UpArrow : ImGuiKey_W;
            const ImGuiKey kB = arrows ? ImGuiKey_DownArrow : ImGuiKey_S;
            const ImGuiKey kR = arrows ? ImGuiKey_RightArrow : ImGuiKey_D;
            const ImGuiKey kL = arrows ? ImGuiKey_LeftArrow : ImGuiKey_A;
            const float fwd = (ImGui::IsKeyDown(kF) ? 1.0f : 0.0f) -
                              (ImGui::IsKeyDown(kB) ? 1.0f : 0.0f);
            const float strafe = (ImGui::IsKeyDown(kR) ? 1.0f : 0.0f) -
                                 (ImGui::IsKeyDown(kL) ? 1.0f : 0.0f);
            viewport_.fly(fwd, strafe, io.DeltaTime);
            if (objectSelected && ImGui::IsKeyPressed(ImGuiKey_Delete))
                deleteSelectedObjects();
        }
    }
    ImGui::End();
}

void App::drawProjectWindow() {
    ImGui::Begin("Project");

    if (!hasProject_) {
        ImGui::TextDisabled("No project open.");
        ImGui::End();
        return;
    }

    ImGui::Text("Name:");
    ImGui::SameLine(110);
    ImGui::TextUnformatted(project_.name.c_str());

    ImGui::Text("Location:");
    ImGui::SameLine(110);
    ImGui::TextWrapped("%s", project_.dir.c_str());

    ImGui::Text("Terrain:");
    ImGui::SameLine(110);
    ImGui::Text("%d x %d units (scene %s)", project_.active().terrain.width,
                project_.active().terrain.depth, project_.active().name.c_str());

    ImGui::Text("Target:");
    ImGui::SameLine(110);
    ImGui::TextUnformatted(project_.elfName().c_str());

    if (ImGui::CollapsingHeader("Scenes", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (ImGui::SmallButton("+ Scene")) {
            openNewScenePopup_ = true;
            newSceneError_.clear();
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Preferences...")) openScenePreferences();
        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Each scene has its own objects and flow graphs.\n"
                              "The game starts in the first scene; switch with the\n"
                              "Switch Scene flow node. Terrain, HUD and audio are shared.");
        for (int i = 0; i < (int)project_.scenes.size(); ++i) {
            ImGui::PushID(i + 3000);
            const std::string label =
                project_.scenes[i].name + (i == 0 ? "  (start)" : "") + "##scene";
            if (ImGui::Selectable(label.c_str(), project_.activeScene == i,
                                  ImGuiSelectableFlags_AllowOverlap) &&
                project_.activeScene != i) {
                project_.activeScene = i;
                clearSelection();
                flowGraphObject_ = -1;
                flowPositionsApplied_ = false;
                applyProjectToViewport();  // terrain/lighting are per scene
            }
            if (project_.scenes.size() > 1) {
                ImGui::SameLine(ImGui::GetContentRegionMax().x - 22.0f);
                // deletion is confirmed in a modal (objects + graphs go with it)
                if (ImGui::SmallButton("x")) deleteScenePending_ = i;
            }
            ImGui::PopID();
        }
    }

    drawSceneSection();
    drawLayersSection();
    drawAssetsSection();
    drawMusicSection();
    drawSoundsSection();
    drawSaveDataSection();
    drawScriptsSection();

    // Building lives in the top-level Build menu (F5 / F6 / Ctrl+Shift+B);
    // the panel only mirrors the runner state so a build's progress is
    // visible without the Output window.
    if (runner_.busy()) {
        ImGui::Separator();
        ImGui::Text("Building... %c", "|/-\\"[(int)(ImGui::GetTime() * 8) & 3]);
        ImGui::SameLine();
        if (ImGui::SmallButton("Cancel")) runner_.cancel();
    } else if (runner_.state() == Runner::State::Failed) {
        ImGui::Separator();
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Last build failed - see Output.");
    }

    ImGui::End();
}

void App::saveProject() {
    // The .tyra file carries the editor-side state and window layout too.
    project_.selectedObject = selectedObject_;
    project_.gizmoOp = gizmoOp_;
    project_.gizmoSpace = gizmoSpace_;
    project_.viewMode = (int)viewport_.viewMode();
    // Fold the live docking arrangement + open windows into the active layout.
    // While a switch is still settling (load or rebuild pending) the on-screen
    // layout doesn't yet belong to the active layout - keep the stored one
    // instead of clobbering it.
    captureActiveLayout();
    if (auto err = project::save(project_); !err.empty())
        MessageBoxA(nullptr, err.c_str(), "Save Project", MB_ICONERROR | MB_OK);
    // Terrain heightmaps live in separate <scene>.heights files (not the .tyra)
    // and, like the rest of the model, are persisted only on demand. They are
    // kept in memory (and in undo snapshots) during editing.
    if (auto err = project::saveHeights(project_); !err.empty())
        MessageBoxA(nullptr, err.c_str(), "Save Terrain", MB_ICONERROR | MB_OK);
}

void App::saveAll(const char* status) {
    saveProject();
    if (auto err = project::saveHistory(project_, history_); !err.empty())
        MessageBoxA(nullptr, err.c_str(), "Save History", MB_ICONERROR | MB_OK);
    setDirty(false);
    statusMessage_ = status;
}

// --- Window layouts ---------------------------------------------------------
// Named docking arrangements stored per project (project_.windowLayouts) and
// switched from the Layout menu. A layout is either a saved ImGui ini dump or,
// while its ini is empty, a built-in DockBuilder recipe rebuilt on demand.

bool* App::showFlagForKey(const std::string& key) {
    if (key == "cutscene") return &showCutsceneEditor_;
    if (key == "material") return &showMaterialEditor_;
    if (key == "ui") return &showUiEditor_;
    if (key == "menus") return &showMenusEditor_;
    if (key == "grading") return &showGradingEditor_;
    if (key == "ambience") return &showAmbienceEditor_;
    if (key == "loading") return &showLoadingEditor_;
    if (key == "disc") return &showDiscLayout_;
    return nullptr;
}

// The optional windows a layout can carry, in a stable order (also the capture
// order). Core windows (Viewport/Project/Properties/Flow Graph/Output/Debug)
// are always drawn and never listed here.
static const char* const kLayoutWindowKeys[] = {
    "cutscene", "material", "ui", "menus", "grading", "ambience", "loading", "disc"};

void App::applyOpenWindows(const std::vector<std::string>& keys) {
    // Deterministic layouts: every optional window's open flag is set to whether
    // the layout requests it, so switching closes ones the previous layout left
    // open and opens the ones this layout needs.
    for (const char* k : kLayoutWindowKeys) {
        bool* flag = showFlagForKey(k);
        if (flag)
            *flag = std::find(keys.begin(), keys.end(), k) != keys.end();
    }
}

std::vector<std::string> App::captureOpenWindows() const {
    std::vector<std::string> keys;
    for (const char* k : kLayoutWindowKeys) {
        bool* flag = const_cast<App*>(this)->showFlagForKey(k);
        if (flag && *flag) keys.emplace_back(k);
    }
    return keys;
}

void App::buildLayoutRecipe(int recipe, unsigned int dockspace) {
    ImGui::DockBuilderRemoveNode(dockspace);
    ImGui::DockBuilderAddNode(dockspace, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dockspace, ImGui::GetMainViewport()->Size);

    ImGuiID center = dockspace;
    switch ((LayoutRecipe)recipe) {
    case LayoutRecipe::Director: {
        // Viewport in the centre, the Cutscene Director dopesheet along the
        // bottom, scene tree + properties stacked on a thin left column. Flow
        // Graph tabs behind the Viewport; Output/Debug behind the dopesheet.
        ImGuiID left =
            ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, 0.20f, nullptr, &center);
        ImGuiID bottom =
            ImGui::DockBuilderSplitNode(center, ImGuiDir_Down, 0.34f, nullptr, &center);
        ImGui::DockBuilderDockWindow("Project", left);
        ImGui::DockBuilderDockWindow("Properties", left);
        ImGui::DockBuilderDockWindow("Cutscene Director", bottom);
        ImGui::DockBuilderDockWindow("Output", bottom);
        ImGui::DockBuilderDockWindow("Debug", bottom);
        ImGui::DockBuilderDockWindow("Flow Graph", center);
        ImGui::DockBuilderDockWindow("Viewport", center);
        pendingFocusWindow_ = "Cutscene Director";
        break;
    }
    case LayoutRecipe::Material: {
        // Material Editor fills the window; the always-present core panels dock
        // as background tabs behind it so nothing floats loose.
        ImGui::DockBuilderDockWindow("Viewport", center);
        ImGui::DockBuilderDockWindow("Project", center);
        ImGui::DockBuilderDockWindow("Properties", center);
        ImGui::DockBuilderDockWindow("Flow Graph", center);
        ImGui::DockBuilderDockWindow("Output", center);
        ImGui::DockBuilderDockWindow("Debug", center);
        ImGui::DockBuilderDockWindow("Material Editor", center);
        pendingFocusWindow_ = "Material Editor";
        break;
    }
    case LayoutRecipe::Default:
    default: {
        ImGuiID left =
            ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, 0.24f, nullptr, &center);
        ImGuiID right =
            ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.26f, nullptr, &center);
        ImGuiID bottom =
            ImGui::DockBuilderSplitNode(center, ImGuiDir_Down, 0.26f, nullptr, &center);
        ImGui::DockBuilderDockWindow("Project", left);
        ImGui::DockBuilderDockWindow("Properties", right);
        ImGui::DockBuilderDockWindow("Output", bottom);
        ImGui::DockBuilderDockWindow("Debug", bottom);
        ImGui::DockBuilderDockWindow("Flow Graph", center);
        ImGui::DockBuilderDockWindow("Viewport", center);
        pendingFocusWindow_ = "Viewport";
        break;
    }
    }
    ImGui::DockBuilderFinish(dockspace);
}

void App::applyActiveLayout() {
    if (project_.windowLayouts.empty()) return;
    const WindowLayout& L = project_.windowLayouts[project_.activeLayout];
    applyOpenWindows(L.openWindows);
    if (!L.ini.empty()) {
        // Load the saved dump at the run() frame boundary.
        layoutLoadPending_ = true;
        recipeRebuildPending_ = false;
        // Legacy dumps predating the Properties window lack a slot for it; carve
        // one once the load settles (drawUI waits for the Project dock node).
        dockPropertiesPending_ = L.ini.find("[Window][Properties]") == std::string::npos;
    } else {
        // Empty ini: (re)build from the built-in recipe in drawUI.
        recipeRebuildPending_ = true;
        recipeRebuildId_ = L.recipe;
        layoutLoadPending_ = false;
        dockPropertiesPending_ = false;
    }
}

void App::captureActiveLayout() {
    // Skip while a switch is still settling: the on-screen arrangement doesn't
    // yet belong to the active layout, so capturing would clobber it.
    if (layoutLoadPending_ || recipeRebuildPending_) return;
    if (project_.windowLayouts.empty()) return;
    WindowLayout& L = project_.windowLayouts[project_.activeLayout];
    L.ini = ImGui::SaveIniSettingsToMemory();  // clears io.WantSaveIniSettings
    L.openWindows = captureOpenWindows();
}

void App::switchLayout(int index) {
    if (!hasProject_ || index < 0 || index >= (int)project_.windowLayouts.size())
        return;
    if (index == project_.activeLayout) return;
    captureActiveLayout();  // don't lose manual edits to the layout we're leaving
    project_.activeLayout = index;
    applyActiveLayout();
    setDirty(true);
    statusMessage_ = "Layout: " + project_.windowLayouts[index].name;
}

void App::resetActiveLayoutToRecipe() {
    if (project_.windowLayouts.empty()) return;
    WindowLayout& L = project_.windowLayouts[project_.activeLayout];
    if (L.recipe < 0) return;  // user layout with no built-in arrangement
    L.ini.clear();             // empty ini -> applyActiveLayout rebuilds the recipe
    applyActiveLayout();
    setDirty(true);
    statusMessage_ = "Reset layout: " + L.name;
}

void App::drawLayoutMenu() {
    // Switch: one item per layout, radio-checked on the active one.
    for (int i = 0; i < (int)project_.windowLayouts.size(); ++i) {
        const bool active = (i == project_.activeLayout);
        if (ImGui::MenuItem(project_.windowLayouts[i].name.c_str(), nullptr, active))
            switchLayout(i);
    }
    ImGui::Separator();

    WindowLayout& L = project_.windowLayouts[project_.activeLayout];
    if (ImGui::MenuItem("Save current arrangement")) {
        captureActiveLayout();
        saveAll("Layout saved");
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Store the current window arrangement into '%s'\n"
                          "(it is also saved whenever you save the project).",
                          L.name.c_str());
    const bool hasRecipe = L.recipe >= 0;
    if (ImGui::MenuItem("Reset to built-in arrangement", nullptr, false, hasRecipe))
        resetActiveLayoutToRecipe();
    if (hasRecipe && ImGui::IsItemHovered())
        ImGui::SetTooltip("Discard manual changes and rebuild the built-in "
                          "arrangement for '%s'.", L.name.c_str());

    ImGui::Separator();
    if (ImGui::MenuItem("New layout...")) {
        std::snprintf(layoutNameBuf_, sizeof(layoutNameBuf_), "Layout %d",
                      (int)project_.windowLayouts.size() + 1);
        layoutNameError_.clear();
        openNewLayoutPopup_ = true;
    }
    if (ImGui::MenuItem("Rename layout...")) {
        std::snprintf(layoutNameBuf_, sizeof(layoutNameBuf_), "%s", L.name.c_str());
        layoutNameError_.clear();
        openRenameLayoutPopup_ = true;
    }
    const bool canDelete = project_.windowLayouts.size() > 1;
    if (ImGui::MenuItem("Delete layout", nullptr, false, canDelete)) {
        project_.windowLayouts.erase(project_.windowLayouts.begin() +
                                     project_.activeLayout);
        if (project_.activeLayout >= (int)project_.windowLayouts.size())
            project_.activeLayout = (int)project_.windowLayouts.size() - 1;
        applyActiveLayout();  // show the layout that took the deleted one's place
        setDirty(true);
        statusMessage_ = "Layout deleted";
    }
    if (!canDelete && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("A project must keep at least one layout.");
}

void App::drawLayoutModals() {
    // Shared name check: non-empty and unique among the other layouts.
    auto nameTaken = [&](const std::string& n, int except) {
        for (int i = 0; i < (int)project_.windowLayouts.size(); ++i)
            if (i != except && project_.windowLayouts[i].name == n) return true;
        return false;
    };

    if (openNewLayoutPopup_) {
        ImGui::OpenPopup("New Layout");
        openNewLayoutPopup_ = false;
    }
    if (openRenameLayoutPopup_) {
        ImGui::OpenPopup("Rename Layout");
        openRenameLayoutPopup_ = false;
    }

    const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("New Layout", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextDisabled("Saves the current window arrangement as a new layout.");
        ImGui::InputText("Name", layoutNameBuf_, sizeof(layoutNameBuf_));
        if (!layoutNameError_.empty())
            ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "%s", layoutNameError_.c_str());
        ImGui::Separator();
        if (ImGui::Button("Create", ImVec2(scaled(120), 0))) {
            std::string name = layoutNameBuf_;
            if (name.empty())
                layoutNameError_ = "Name can't be empty";
            else if (nameTaken(name, -1))
                layoutNameError_ = "A layout with that name already exists";
            else {
                // Fold the live arrangement into the current layout, then clone
                // it under the new name and make the clone active.
                captureActiveLayout();
                WindowLayout nl;
                nl.name = name;
                nl.ini = ImGui::SaveIniSettingsToMemory();
                nl.recipe = -1;  // user layout: no built-in recipe to reset to
                nl.openWindows = captureOpenWindows();
                project_.windowLayouts.push_back(std::move(nl));
                project_.activeLayout = (int)project_.windowLayouts.size() - 1;
                setDirty(true);
                statusMessage_ = "Layout created: " + name;
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(scaled(120), 0))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Rename Layout", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::InputText("Name", layoutNameBuf_, sizeof(layoutNameBuf_));
        if (!layoutNameError_.empty())
            ImGui::TextColored(ImVec4(1, 0.4f, 0.4f, 1), "%s", layoutNameError_.c_str());
        ImGui::Separator();
        if (ImGui::Button("Rename", ImVec2(scaled(120), 0))) {
            std::string name = layoutNameBuf_;
            if (name.empty())
                layoutNameError_ = "Name can't be empty";
            else if (nameTaken(name, project_.activeLayout))
                layoutNameError_ = "A layout with that name already exists";
            else {
                project_.windowLayouts[project_.activeLayout].name = name;
                setDirty(true);
                statusMessage_ = "Layout renamed";
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(scaled(120), 0))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

void App::commitChange() {
    // Push an undo snapshot; mark the project dirty only when the edit actually
    // changed something. No disk write - saving is on demand (see saveAll).
    layerRamCache_.clear();  // objects/layers may have changed - re-estimate
    // Stamp ids on any freshly inserted / pasted object before it enters an
    // undo snapshot or hits disk, so every persisted object has a stable id.
    project::ensureObjectIds(project_);
    if (history_.push({project_.scenes})) setDirty(true);
}

void App::setDirty(bool dirty) {
    if (dirty_ == dirty) return;
    dirty_ = dirty;
    updateWindowTitle();
}

void App::updateWindowTitle() {
    if (!window_) return;
    // Skip redundant GLFW calls: only push when the shown state changed.
    if (titleShowsDirty_ == dirty_ && titleName_ == project_.name) return;
    titleShowsDirty_ = dirty_;
    titleName_ = project_.name;
    std::string title = "TyraX";
    if (hasProject_) title += " - " + project_.name + (dirty_ ? " *" : "");
    glfwSetWindowTitle(window_, title.c_str());
}

// --- Discard guard (Exit / Open / New with unsaved edits) ----------------
// Each request runs immediately when nothing would be lost; otherwise it
// stages the action and opens the confirm modal, which resolves it.
void App::requestExit() {
    if (hasProject_ && dirty_) {
        pendingAction_ = PendingAction::Exit;
        openDiscardPopup_ = true;
    } else {
        exitConfirmed_ = true;
        glfwSetWindowShouldClose(window_, GLFW_TRUE);
    }
}

void App::requestOpenProject() {
    if (hasProject_ && dirty_) {
        pendingAction_ = PendingAction::Open;
        openDiscardPopup_ = true;
    } else {
        openProjectDialog();
    }
}

void App::requestNewProject() {
    if (hasProject_ && dirty_) {
        pendingAction_ = PendingAction::New;
        openDiscardPopup_ = true;
    } else {
        openNewProjectPopup_ = true;
    }
}

void App::performPendingAction() {
    const PendingAction action = pendingAction_;
    pendingAction_ = PendingAction::None;
    switch (action) {
        case PendingAction::Exit:
            exitConfirmed_ = true;
            glfwSetWindowShouldClose(window_, GLFW_TRUE);
            break;
        case PendingAction::Open:
            openProjectDialog();
            break;
        case PendingAction::New:
            openNewProjectPopup_ = true;
            break;
        case PendingAction::None:
            break;
    }
}

void App::drawDiscardModal() {
    if (openDiscardPopup_) {
        ImGui::OpenPopup("Unsaved Changes");
        openDiscardPopup_ = false;
    }
    // Center the modal over the main viewport.
    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Unsaved Changes", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted(
            ("\"" + project_.name + "\" has unsaved changes.").c_str());
        ImGui::Spacing();
        // Auto-sized (with a sensible floor) so the labels never clip at high
        // UI scales, where a fixed pixel width is too narrow for the text.
        const float bw = ImGui::CalcTextSize("Don't Save").x +
                         ImGui::GetStyle().FramePadding.x * 2.0f;
        if (ImGui::Button("Save", ImVec2(bw, 0))) {
            saveAll("Saved");
            ImGui::CloseCurrentPopup();
            performPendingAction();
        }
        ImGui::SameLine();
        if (ImGui::Button("Don't Save", ImVec2(bw, 0))) {
            ImGui::CloseCurrentPopup();
            performPendingAction();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(bw, 0)) ||
            ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            pendingAction_ = PendingAction::None;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void App::applySnapshot(const SceneSnapshot& s) {
    project_.scenes = s.scenes;
    if (project_.activeScene >= (int)project_.scenes.size()) project_.activeScene = 0;
    // A snapshot may have fewer objects (undo of a paste/add) - drop selection
    // indices that no longer exist.
    pruneSelection();
    flowPositionsApplied_ = false;  // graphs live in objects - re-pin node positions
    // Heightmaps ride along in the snapshot (in memory); they hit disk only on
    // an explicit save (see saveProject). applyProjectToViewport pushes the
    // restored heights straight to the viewport.
    applyProjectToViewport();  // terrain/lighting/heights may differ per snapshot
}

void App::undo() {
    if (!history_.canUndo()) return;
    applySnapshot(history_.undo());
    setDirty(true);
    statusMessage_ = "Undo";
}

void App::redo() {
    if (!history_.canRedo()) return;
    applySnapshot(history_.redo());
    setDirty(true);
    statusMessage_ = "Redo";
}

// --- Selection set -------------------------------------------------------
// selectedObject_ is kept in sync as the primary (anchor) of the set: the
// last-clicked object, which drives the orbit pivot, the single-object gizmo
// path and the anchor value shown in the multi-edit panel.
void App::selectOnly(int i) {
    selection_.clear();
    if (i >= 0 && i < (int)project_.objects().size()) selection_.push_back(i);
    selectedObject_ = selection_.empty() ? -1 : selection_.back();
}

void App::toggleSelect(int i) {
    if (i < 0 || i >= (int)project_.objects().size()) return;
    auto it = std::find(selection_.begin(), selection_.end(), i);
    if (it != selection_.end()) selection_.erase(it);
    else selection_.push_back(i);
    selectedObject_ = selection_.empty() ? -1 : selection_.back();
}

void App::clearSelection() {
    selection_.clear();
    selectedObject_ = -1;
}

bool App::isSelected(int i) const {
    return std::find(selection_.begin(), selection_.end(), i) != selection_.end();
}

void App::pruneSelection() {
    const int n = (int)project_.objects().size();
    selection_.erase(
        std::remove_if(selection_.begin(), selection_.end(),
                       [n](int i) { return i < 0 || i >= n; }),
        selection_.end());
    selectedObject_ = selection_.empty() ? -1 : selection_.back();
}

void App::selectObjectsInBox(ImVec2 a, ImVec2 b, ImVec2 imgPos, ImVec2 avail, bool add) {
    const float rMinX = std::min(a.x, b.x), rMaxX = std::max(a.x, b.x);
    const float rMinY = std::min(a.y, b.y), rMaxY = std::max(a.y, b.y);

    // World -> image projection, matching the viewport's render camera (same
    // math as the sculpt brush overlay in run()).
    const float* V = viewport_.viewMatrix();
    const float* P = viewport_.projMatrix();
    auto project = [&](float wx, float wy, float wz, ImVec2& out) -> bool {
        const float vx = V[0] * wx + V[4] * wy + V[8] * wz + V[12];
        const float vy = V[1] * wx + V[5] * wy + V[9] * wz + V[13];
        const float vz = V[2] * wx + V[6] * wy + V[10] * wz + V[14];
        const float cx = P[0] * vx + P[4] * vy + P[8] * vz + P[12];
        const float cy = P[1] * vx + P[5] * vy + P[9] * vz + P[13];
        const float cw = P[3] * vx + P[7] * vy + P[11] * vz + P[15];
        if (cw <= 0.001f) return false;  // behind the camera
        out = ImVec2(imgPos.x + (cx / cw * 0.5f + 0.5f) * avail.x,
                     imgPos.y + (1.0f - (cy / cw * 0.5f + 0.5f)) * avail.y);
        return true;
    };

    if (!add) selection_.clear();
    const float d2r = 3.14159265358979f / 180.0f;
    for (int i = 0; i < (int)project_.objects().size(); ++i) {
        const SceneObject& o = project_.objects()[i];
        if (isObjectHiddenInEditor(o)) continue;  // hidden layers aren't selectable
        // Rotation applied to each unit-box corner: Rz*Ry*Rx (see modelMatrix).
        const float cx = std::cos(o.rotation[0] * d2r), sx = std::sin(o.rotation[0] * d2r);
        const float cy = std::cos(o.rotation[1] * d2r), sy = std::sin(o.rotation[1] * d2r);
        const float cz = std::cos(o.rotation[2] * d2r), sz = std::sin(o.rotation[2] * d2r);
        float oMinX = 1e30f, oMinY = 1e30f, oMaxX = -1e30f, oMaxY = -1e30f;
        bool anyFront = false;
        for (int s = 0; s < 8; ++s) {
            const float lx = ((s & 1) ? 0.5f : -0.5f) * o.scale[0];
            const float ly = ((s & 2) ? 0.5f : -0.5f) * o.scale[1];
            const float lz = ((s & 4) ? 0.5f : -0.5f) * o.scale[2];
            const float y1 = ly * cx - lz * sx, z1 = ly * sx + lz * cx, x1 = lx;  // Rx
            const float x2 = x1 * cy + z1 * sy, z2 = -x1 * sy + z1 * cy, y2 = y1;  // Ry
            const float x3 = x2 * cz - y2 * sz, y3 = x2 * sz + y2 * cz, z3 = z2;  // Rz
            ImVec2 sp;
            if (!project(o.position[0] + x3, o.position[1] + y3, o.position[2] + z3, sp))
                continue;
            anyFront = true;
            oMinX = std::min(oMinX, sp.x), oMinY = std::min(oMinY, sp.y);
            oMaxX = std::max(oMaxX, sp.x), oMaxY = std::max(oMaxY, sp.y);
        }
        if (!anyFront) continue;
        const bool overlap =
            oMinX <= rMaxX && oMaxX >= rMinX && oMinY <= rMaxY && oMaxY >= rMinY;
        if (overlap && !isSelected(i)) selection_.push_back(i);
    }
    selectedObject_ = selection_.empty() ? -1 : selection_.back();
}

void App::deleteSelectedObjects() {
    if (selection_.empty()) return;
    // Erase high indices first so earlier ones stay valid.
    std::vector<int> idx = selection_;
    std::sort(idx.begin(), idx.end(), [](int a, int b) { return a > b; });
    for (int i : idx)
        if (i >= 0 && i < (int)project_.objects().size())
            project_.objects().erase(project_.objects().begin() + i);
    clearSelection();
    commitChange();
}

void App::copyObject() {
    if (selection_.empty()) return;
    clipboard_.clear();
    for (int i : selection_)
        if (i >= 0 && i < (int)project_.objects().size())
            clipboard_.push_back(project_.objects()[i]);
    statusMessage_ = clipboard_.size() == 1
                         ? "Copied " + clipboard_.front().name
                         : "Copied " + std::to_string(clipboard_.size()) + " objects";
}

void App::pasteObject() {
    if (clipboard_.empty()) return;

    // Paste the whole group offset by the same amount so it keeps its shape,
    // then select the pasted objects (primary = the last one).
    selection_.clear();
    for (const SceneObject& src : clipboard_) {
        SceneObject o = src;
        o.id.clear();  // a paste is a new object - it must get its own id
        std::string name = o.name + "-copy";
        for (int n = 2;; ++n) {
            bool taken = false;
            for (const auto& other : project_.objects()) taken |= (other.name == name);
            if (!taken) break;
            name = o.name + "-copy" + std::to_string(n);
        }
        o.name = name;
        o.position[0] += 1.0f;  // offset so the copy is visible next to the original
        o.position[2] += 1.0f;
        project_.objects().push_back(std::move(o));
        selection_.push_back((int)project_.objects().size() - 1);
    }
    selectedObject_ = selection_.back();
    commitChange();
    statusMessage_ = clipboard_.size() == 1
                         ? "Pasted " + project_.objects().back().name
                         : "Pasted " + std::to_string(clipboard_.size()) + " objects";
}

void App::attachProject() {
    // Emulator path and PS2 IP are machine-global editor settings (editor.ini),
    // not project data. Migrate any value carried by an older .tyra file into
    // the global config the first time such a project is opened, then feed the
    // global values into this project - project_ is the Runner's runtime
    // transport for them (see Project::emulatorPath / ps2LinkIp).
    bool migrated = false;
    if (globalEmulatorPath_.empty() && !project_.emulatorPath.empty()) {
        globalEmulatorPath_ = project_.emulatorPath;
        migrated = true;
    }
    if (globalPs2Ip_.empty() && !project_.ps2LinkIp.empty()) {
        globalPs2Ip_ = project_.ps2LinkIp;
        migrated = true;
    }
    if (migrated) saveGlobalConfig();
    project_.emulatorPath = globalEmulatorPath_;
    project_.ps2LinkIp = globalPs2Ip_;

    flowGraphObject_ = -1;
    flowPositionsApplied_ = false;
    layerRamCache_.clear();
    // Live Link is per project: forget the previous project's cached as-built
    // record and written payload, re-read/re-write for this one. The sequence
    // counter is seeded from the clock: a game left running remembers the last
    // seq it applied, so a restarted editor beginning again at 1 would collide
    // with its predecessor's first write and the (different) snapshot would be
    // deduped away - time makes consecutive sessions never reuse a seq.
    liveLinkState_ = LiveLinkState::Off;
    liveLinkBuilt_ = LiveLinkBuilt();
    liveLinkLastPayload_.clear();
    liveLinkSeq_ = (uint32_t)std::time(nullptr);
    liveLinkSigNextRead_ = 0.0;
    liveLinkNextTick_ = 0.0;
    history_.reset({project_.scenes});
    // The history file restores the undo stack when it is in sync with the
    // project file; otherwise we start fresh (and write a new one).
    if (auto err = project::loadHistory(project_, history_); !err.empty()) {
        history_.reset({project_.scenes});
        project::saveHistory(project_, history_);
    }
    // Editor-side state + window layout came in with the .tyra project file.
    // Only the primary selection persists; seed the (transient) set from it.
    selectOnly(project_.selectedObject);
    gizmoOp_ = (project_.gizmoOp >= 0 && project_.gizmoOp <= 2) ? project_.gizmoOp : 0;
    gizmoSpace_ = project_.gizmoSpace == 1 ? 1 : 0;
    const int viewMode =
        (project_.viewMode >= 0 && project_.viewMode <= 2) ? project_.viewMode : 0;
    viewport_.setViewMode((Viewport::ViewMode)viewMode);
    // Window layouts arrived with the .tyra. Guard against an empty/out-of-range
    // set (hand-edited or very old file), then apply the active one. Applying is
    // deferred to a frame boundary: loading ImGui settings mid-frame is
    // unsupported, and a recipe rebuild needs the dockspace id from drawUI.
    if (project_.windowLayouts.empty()) project::seedBuiltinLayouts(project_);
    if (project_.activeLayout < 0 ||
        project_.activeLayout >= (int)project_.windowLayouts.size())
        project_.activeLayout = 0;
    applyActiveLayout();
    flowPositionsApplied_ = false;
    statusMessage_.clear();
    wavIssueCache_.clear();
    modelInfoCache_.clear();
    // Pick up assets dropped into res/ by hand while the project was closed.
    rescanAssets(false);
    // A freshly opened/created project starts clean. rescanAssets may have
    // pushed an undo snapshot for assets found on disk, but those are
    // rediscovered on every open, so treat the project as saved.
    dirty_ = false;
    updateWindowTitle();

    // Baseline the error catcher: any assertion already in this project's logs
    // is from a previous session, so treat it as seen (don't pop it on open),
    // and seed the sizes so the first poll neither re-pops it nor mistakes the
    // switch from another project's larger log for a fresh-run shrink.
    errorSeenSig_ = latestGameAssert();
    errorGameLogSize_ =
        fileSizeOr0((std::filesystem::path(project_.dir) / "bin" / "log.txt").string());
    errorRunnerLogSize_ = runner_.log().size();
    openErrorPopup_ = false;
}

void App::addEmitter(int kind) {
    addObject(PrimitiveType::Emitter);
    SceneObject& o = project_.objects().back();
    o.emitterKind = kind;
    // preset tints (the color tints the particles / their texture)
    const float presets[6][3] = {{1.0f, 0.6f, 0.2f},     // fire
                                 {0.5f, 0.5f, 0.5f},     // smoke
                                 {0.8f, 0.85f, 0.9f},    // fog
                                 {1.0f, 0.9f, 0.4f},     // sparks
                                 {0.65f, 0.75f, 0.95f},  // rain
                                 {0.55f, 0.7f, 0.95f}};  // custom (water-ish)
    for (int i = 0; i < 3; ++i) o.color[i] = presets[kind][i];
    if (kind == 2) {  // fog: wide footprint, big lazy puffs
        o.scale[0] = o.scale[2] = 8.0f;
        o.emitterCount = 16;
        o.emitterSize = 1.2f;
    }
    if (kind == 4) {  // rain: wide area, falls from the emitter height
        o.position[1] = 12.0f;
        o.scale[0] = o.scale[2] = 20.0f;
        o.emitterCount = 96;
        o.emitterSize = 1.0f;
    }
    if (kind == 5) {  // custom: a small water-like jet as the starting point
        o.position[1] = 2.0f;
        o.scale[0] = o.scale[2] = 0.5f;
        o.emitterCount = 64;
        o.emitterSize = 0.3f;
    }
    saveAll("Saved");
}
void App::addSoundEmitter() {
    addObject(PrimitiveType::SoundEmitter);
    SceneObject& o = project_.objects().back();
    o.position[1] = 1.0f;
    o.color[0] = 0.65f, o.color[1] = 0.3f, o.color[2] = 0.9f;  // violet marker
    o.scale[0] = o.scale[1] = o.scale[2] = 0.5f;
    if (!project_.sounds.empty()) o.soundPath = project_.sounds.front();
    saveAll("Saved");
}
void App::addPointLight() {
    addObject(PrimitiveType::PointLight);
    SceneObject& o = project_.objects().back();
    o.position[1] = 3.0f;  // hovers above the ground by default
    o.color[0] = 1.0f, o.color[1] = 0.95f, o.color[2] = 0.8f;  // warm white
    o.scale[0] = o.scale[1] = o.scale[2] = 0.4f;  // small bulb gizmo
    o.lightBright = 1.0f;
    o.lightRadius = 8.0f;
    saveAll("Saved");
}
void App::addEmpty() {
    addObject(PrimitiveType::Empty);
    SceneObject& o = project_.objects().back();
    // small neutral sphere marker, floats where scripts expect an anchor
    o.position[1] = 1.0f;
    o.scale[0] = o.scale[1] = o.scale[2] = 0.5f;
    o.color[0] = o.color[1] = o.color[2] = 0.75f;
    o.collisionMode = 2;  // pure transform - never blocks the player
    saveAll("Saved");
}
void App::addDecal() {
    addObject(PrimitiveType::Decal);
    SceneObject& o = project_.objects().back();
    o.position[1] = 1.5f;  // eye height on a wall
    // white so the texture shows untinted (color modulates the map_Kd)
    o.color[0] = o.color[1] = o.color[2] = 1.0f;
    o.collisionMode = 2;  // visual overlay - never blocks the player
    saveAll("Saved");
}
void App::addMirror() {
    addObject(PrimitiveType::Mirror);
    SceneObject& o = project_.objects().back();
    // an upright dressing-mirror rectangle at standing height, cool glass tint
    o.position[1] = 1.2f;
    o.scale[0] = 1.4f, o.scale[1] = 2.2f, o.scale[2] = 1.0f;
    o.color[0] = 0.62f, o.color[1] = 0.78f, o.color[2] = 0.88f;
    saveAll("Saved");
}
void App::addSavePoint() {
    addObject(PrimitiveType::SavePoint);
    SceneObject& o = project_.objects().back();
    // a slim cyan pillar - reads as a save terminal, box collision in game
    o.position[1] = 0.75f;
    o.scale[0] = 0.8f, o.scale[1] = 1.5f, o.scale[2] = 0.8f;
    o.color[0] = 0.25f, o.color[1] = 0.85f, o.color[2] = 0.95f;
    o.usable = true;  // implicit in the game; mirrored here for the viewport
    saveAll("Saved");
}
void App::addObject(PrimitiveType type) {
    // Unique default name: box-1, box-2, ...
    int counter = 0;
    std::string name;
    for (;;) {
        name = std::string(primitiveTypeName(type)) + "-" + std::to_string(++counter);
        bool taken = false;
        for (const auto& o : project_.objects()) taken |= (o.name == name);
        if (!taken) break;
    }

    SceneObject o;
    o.name = name;
    o.type = type;
    o.primDetail = defaultPrimDetail(type);  // box-like baseline 1, curved 16
    if (type == PrimitiveType::SpawnPoint) {
        o.position[1] = 0.0f;  // marker sits on the ground
        o.color[0] = 0.15f, o.color[1] = 0.9f, o.color[2] = 0.9f;
    }
    if (type == PrimitiveType::Player) {
        o.position[1] = 0.0f;  // marker stands on the ground
        o.color[0] = 0.95f, o.color[1] = 0.75f, o.color[2] = 0.2f;
    }
    if (type == PrimitiveType::Camera) {
        o.position[1] = 2.0f;  // eye height-ish, above the ground
        o.color[0] = 0.35f, o.color[1] = 0.75f, o.color[2] = 1.0f;
    }
    project_.objects().push_back(o);
    selectOnly((int)project_.objects().size() - 1);
    commitChange();
}

std::string App::importModelAsset() {
    const std::string src = pickModelFile();
    if (src.empty()) return "";

    const std::filesystem::path srcPath(src);
    const std::filesystem::path srcDir = srcPath.parent_path();
    const std::string fileName = sanitizeAssetName(srcPath.filename().string());
    const std::filesystem::path destDir = std::filesystem::path(project_.dir) / "res" / "models";
    std::error_code ec;
    std::filesystem::create_directories(destDir, ec);

    // Animated models (.glb) are self-contained (geometry, clips, textures in
    // one file): plain copy, then a validation bake for early feedback. The
    // .tanm the game loads is baked from it on every build.
    if (isAnimatedModelPath(fileName)) {
        std::filesystem::copy_file(srcPath, destDir / fileName,
                                   std::filesystem::copy_options::overwrite_existing, ec);
        if (ec) {
            statusMessage_ = "Model import failed: " + ec.message();
            return "";
        }
        glbparser::Baked baked;
        std::string error;
        if (!glbparser::bake((destDir / fileName).string(), 12.0f, baked, error)) {
            statusMessage_ = "Imported " + fileName + " - UNUSABLE: " + error;
            return "res/models/" + fileName;
        }
        statusMessage_ = "Imported " + fileName + " (" +
                         std::to_string(baked.clips.size()) + " clip(s), " +
                         std::to_string(baked.totalVertexCount()) + " verts, " +
                         std::to_string(baked.frameCount) + " baked frames)";
        // Rough PS2 memory estimate: the skeletal runtime keeps one bind-pose
        // mesh + keyframe tracks (+ per-instance skinned buffers), not baked
        // frames - parseSkel knows the actual footprint.
        glbparser::Skel skel;
        std::string skelError;
        const size_t bytes = glbparser::parseSkel((destDir / fileName).string(),
                                                  skel, skelError)
                                 ? skel.ps2Bytes()
                                 : 0;
        if (bytes > 8u * 1024 * 1024)
            statusMessage_ += " - WARNING: ~" + std::to_string(bytes >> 20) +
                              " MB on the PS2 (32 MB total) - reduce the mesh";
        if (!baked.warnings.empty())
            statusMessage_ += " - " + baked.warnings.front() +
                              (baked.warnings.size() > 1
                                   ? " (+" + std::to_string(baked.warnings.size() - 1) +
                                         " more warning(s), see build log)"
                                   : "");
        glbInfoCache_.erase("res/models/" + fileName);
        return "res/models/" + fileName;
    }

    // Parse the source model to find its material libraries and textures -
    // they get flattened next to the .obj in res/models/. Sanitized names may
    // differ from the originals, so the mtllib/map_Kd references are rewritten
    // while copying instead of copying the files verbatim.
    objparser::Model parsed;
    const bool parseOk = objparser::load(src, parsed);

    // texture file (relative to the .obj) -> sanitized basename in res/models
    std::map<std::string, std::string> textureNames;
    for (const objparser::Submesh& s : parsed.submeshes)
        if (!s.texture.empty())
            textureNames.emplace(
                s.texture,
                sanitizeAssetName(std::filesystem::path(s.texture).filename().string()));
    std::map<std::string, std::string> mtlNames;
    for (const std::string& m : parsed.mtlLibs)
        mtlNames.emplace(
            m, sanitizeAssetName(std::filesystem::path(m).filename().string()));

    int missing = 0;

    // .obj: rewrite mtllib lines to the sanitized library names
    {
        std::ifstream in(srcPath);
        std::ofstream out(destDir / fileName, std::ios::trunc);
        if (!in || !out) {
            statusMessage_ = "Model import failed: cannot copy " + fileName;
            return "";
        }
        std::string line;
        while (std::getline(in, line)) {
            std::istringstream ss(line);
            std::string tag;
            ss >> tag;
            if (tag == "mtllib") {
                out << "mtllib";
                std::string name;
                while (ss >> name) out << " " << mtlNames[name];
                out << "\n";
            } else {
                out << line << "\n";
            }
        }
    }

    // .mtl libraries: rewrite map_Kd to the sanitized (flattened) texture names
    for (const auto& [mtlRef, mtlDest] : mtlNames) {
        std::ifstream in(srcDir / mtlRef);
        if (!in) {
            ++missing;
            continue;
        }
        std::ofstream out(destDir / mtlDest, std::ios::trunc);
        if (!out) continue;
        std::string line;
        while (std::getline(in, line)) {
            std::istringstream ss(line);
            std::string tag;
            ss >> tag;
            if (tag == "map_Kd") {
                std::string tok, last;
                while (ss >> tok) last = tok;
                for (char& c : last)
                    if (c == '\\') c = '/';
                auto it = textureNames.find(last);
                out << "map_Kd " << (it != textureNames.end() ? it->second : last)
                    << "\n";
            } else {
                out << line << "\n";
            }
        }
    }

    // referenced textures, flattened into res/models/
    for (const auto& [texRef, texDest] : textureNames) {
        std::filesystem::copy_file(srcDir / texRef, destDir / texDest,
                                   std::filesystem::copy_options::overwrite_existing, ec);
        if (ec) {
            ++missing;
            ec.clear();
        }
    }

    modelInfoCache_.erase("res/models/" + fileName);
    statusMessage_ = "Imported " + fileName;
    if (!parseOk)
        statusMessage_ += " (unparseable - it will render as a placeholder box)";
    else if (!mtlNames.empty())
        statusMessage_ += " + " + std::to_string(mtlNames.size()) + " mtl, " +
                          std::to_string(textureNames.size()) + " texture(s)";
    if (missing > 0)
        statusMessage_ += " - " + std::to_string(missing) +
                          " referenced file(s) missing next to the .obj";
    return "res/models/" + fileName;
}

std::string App::importTextureAsset() {
    const std::string src = pickPngFile();
    if (src.empty()) return "";
    const std::filesystem::path srcPath(src);
    const std::string fileName = sanitizeAssetName(srcPath.filename().string());
    const std::filesystem::path destDir =
        std::filesystem::path(project_.dir) / "res" / "textures";
    std::error_code ec;
    std::filesystem::create_directories(destDir, ec);
    std::filesystem::copy_file(srcPath, destDir / fileName,
                               std::filesystem::copy_options::overwrite_existing, ec);
    if (ec) {
        statusMessage_ = "Texture import failed: " + ec.message();
        return "";
    }
    statusMessage_ = "Imported " + fileName;
    return "res/textures/" + fileName;
}

// Imports a universal .mtl into res/materials: copies the library and every
// map_Kd texture next to it, rewriting the references to the sanitized
// (flattened) names - mirrors the model import.
std::string App::importMaterialAsset() {
    const std::string src = pickMtlFile();
    if (src.empty()) return "";

    const std::filesystem::path srcPath(src);
    const std::filesystem::path srcDir = srcPath.parent_path();
    const std::string fileName = sanitizeAssetName(srcPath.filename().string());
    const std::filesystem::path destDir =
        std::filesystem::path(project_.dir) / "res" / "materials";
    std::error_code ec;
    std::filesystem::create_directories(destDir, ec);

    std::vector<objparser::MtlMaterial> parsed;
    objparser::loadMtl(src, parsed);
    std::map<std::string, std::string> textureNames;
    for (const objparser::MtlMaterial& m : parsed)
        if (!m.texture.empty())
            textureNames.emplace(
                m.texture,
                sanitizeAssetName(std::filesystem::path(m.texture).filename().string()));

    int missing = 0;
    {
        std::ifstream in(srcPath);
        std::ofstream out(destDir / fileName, std::ios::trunc);
        if (!in || !out) {
            statusMessage_ = "Material import failed: cannot copy " + fileName;
            return "";
        }
        std::string line;
        while (std::getline(in, line)) {
            std::istringstream ss(line);
            std::string tag;
            ss >> tag;
            if (tag == "map_Kd") {
                std::string tok, last;
                while (ss >> tok) last = tok;
                for (char& c : last)
                    if (c == '\\') c = '/';
                auto it = textureNames.find(last);
                out << "map_Kd " << (it != textureNames.end() ? it->second : last)
                    << "\n";
            } else {
                out << line << "\n";
            }
        }
    }
    for (const auto& [texRef, texDest] : textureNames) {
        std::filesystem::copy_file(srcDir / texRef, destDir / texDest,
                                   std::filesystem::copy_options::overwrite_existing, ec);
        if (ec) {
            ++missing;
            ec.clear();
        }
    }

    modelInfoCache_.clear();  // material summaries may change
    statusMessage_ = "Imported " + fileName + " (" +
                     std::to_string(parsed.size()) + " material(s), " +
                     std::to_string(textureNames.size()) + " texture(s))";
    if (missing > 0)
        statusMessage_ += " - " + std::to_string(missing) +
                          " texture(s) missing next to the .mtl";
    return "res/materials/" + fileName;
}

std::vector<std::string> App::listAssetFiles(const char* subdir, const char* ext) {
    std::vector<std::string> files;
    std::error_code ec;
    const std::filesystem::path dir = std::filesystem::path(project_.dir) / "res" / subdir;
    if (!std::filesystem::exists(dir, ec)) return files;
    // recursive: assets may be organized into subfolders (res/models/props/...)
    for (const auto& e : std::filesystem::recursive_directory_iterator(dir, ec)) {
        if (!e.is_regular_file()) continue;
        std::string fileExt = e.path().extension().string();
        for (char& c : fileExt) c = (char)tolower((unsigned char)c);
        if (fileExt == ext)
            files.push_back(std::filesystem::relative(e.path(), dir, ec).generic_string());
    }
    return files;
}

// "Pick..." + popup over the PNGs in res/textures (terrain tiling textures -
// objects are textured through materials instead). The Import item at the
// bottom is the only file dialog for these.
bool App::pickProjectTexture(const char* popupId, std::string& path) {
    bool changed = false;
    if (ImGui::SmallButton((std::string("Pick...##") + popupId).c_str()))
        ImGui::OpenPopup(popupId);
    if (ImGui::BeginPopup(popupId)) {
        const std::vector<std::string> textures = listAssetFiles("textures", ".png");
        for (const std::string& name : textures)
            if (ImGui::MenuItem(name.c_str())) {
                path = "res/textures/" + name;
                changed = true;
            }
        if (textures.empty()) ImGui::TextDisabled("No textures in res/textures yet.");
        ImGui::Separator();
        if (ImGui::MenuItem("Import PNG...")) {
            const std::string imported = importTextureAsset();
            if (!imported.empty()) {
                path = imported;
                changed = true;
            }
        }
        ImGui::EndPopup();
    }
    return changed;
}

const App::ModelInfo& App::modelInfo(const std::string& relPath,
                                     const std::string& materialRel) {
    const std::string key = relPath + "|" + materialRel;
    auto it = modelInfoCache_.find(key);
    if (it != modelInfoCache_.end()) return it->second;

    ModelInfo info;
    objparser::Model model;
    const std::filesystem::path full = std::filesystem::path(project_.dir) / relPath;
    const std::string overrideMtl =
        materialRel.empty()
            ? ""
            : (std::filesystem::path(project_.dir) / materialRel).string();
    if (objparser::load(full.string(), model, overrideMtl)) {
        info.ok = true;
        info.tris = model.vertexCount() / 3;
        // texture paths resolve relative to the file that defined them: the
        // override .mtl when one is assigned, the model otherwise
        const std::filesystem::path texBase =
            materialRel.empty()
                ? full.parent_path()
                : (std::filesystem::path(project_.dir) / materialRel).parent_path();
        for (const objparser::Submesh& s : model.submeshes) {
            const std::string name = s.material.empty() ? "(default)" : s.material;
            ModelInfo::MaterialLine line;
            if (s.texture.empty()) {
                line.text = name + " (color)";
            } else {
                // flag textures that are not actually inside the project (the
                // game would draw those parts untextured and warn)
                std::error_code ec;
                line.missing = !std::filesystem::exists(texBase / s.texture, ec);
                line.text = name + " (" + s.texture + ")";
                info.anyMissing |= line.missing;
            }
            info.materials.push_back(std::move(line));
        }
    }
    return modelInfoCache_.emplace(key, std::move(info)).first->second;
}

// Summary of an animated .glb (clip names + stats for the properties panel).
const App::GlbInfo& App::glbInfo(const std::string& relPath) {
    auto it = glbInfoCache_.find(relPath);
    if (it != glbInfoCache_.end()) return it->second;

    GlbInfo info;
    glbparser::Baked baked;
    const std::filesystem::path full = std::filesystem::path(project_.dir) / relPath;
    if (glbparser::bake(full.string(), 12.0f, baked, info.error)) {
        info.ok = true;
        for (const auto& c : baked.clips) info.clips.push_back(c.name);
        info.vertexCount = baked.totalVertexCount();
        info.frameCount = baked.frameCount;
        info.warnings = baked.warnings;
        for (const glbparser::Part& p : baked.parts) {
            GlbInfo::Material mat;
            mat.name = p.material.empty() ? "material" : p.material;
            mat.color[0] = p.baseColor[0];
            mat.color[1] = p.baseColor[1];
            mat.color[2] = p.baseColor[2];
            mat.textured = p.image >= 0;
            info.materials.push_back(std::move(mat));
        }
    }
    return glbInfoCache_.emplace(relPath, std::move(info)).first->second;
}

// Summary of a standalone .mtl asset (Assets section / material combos).
const App::ModelInfo& App::materialInfo(const std::string& relPath) {
    const std::string key = "mtl:" + relPath;
    auto it = modelInfoCache_.find(key);
    if (it != modelInfoCache_.end()) return it->second;

    ModelInfo info;
    std::vector<objparser::MtlMaterial> materials;
    const std::filesystem::path full = std::filesystem::path(project_.dir) / relPath;
    if (objparser::loadMtl(full.string(), materials)) {
        info.ok = true;
        for (const objparser::MtlMaterial& m : materials) {
            ModelInfo::MaterialLine line;
            if (m.texture.empty()) {
                line.text = m.name + " (color)";
            } else {
                std::error_code ec;
                line.missing =
                    !std::filesystem::exists(full.parent_path() / m.texture, ec);
                line.text = m.name + " (" + m.texture + ")";
                info.anyMissing |= line.missing;
            }
            info.materials.push_back(std::move(line));
        }
    }
    return modelInfoCache_.emplace(key, std::move(info)).first->second;
}

std::vector<std::string> App::listMaterialAssets() {
    std::vector<std::string> result;
    for (const std::string& m : listAssetFiles("materials", ".mtl"))
        result.push_back("res/materials/" + m);
    for (const std::string& m : listAssetFiles("models", ".mtl"))
        result.push_back("res/models/" + m);
    return result;
}

// Material combo shared by every solid object. Lists the project's .mtl
// assets (res/materials + the models' own libraries); primitives take the
// file's first material, models use it as an override.
bool App::drawMaterialCombo(SceneObject& o) {
    const bool isModel = o.type == PrimitiveType::Model;
    const char* noneLabel = isModel ? "(model's own)" : "<none - plain color>";
    std::string current = o.materialPath.empty() ? noneLabel : o.materialPath;
    if (current.rfind("res/", 0) == 0) current = current.substr(4);

    bool changed = false;
    if (ImGui::BeginCombo("Material", current.c_str())) {
        if (ImGui::Selectable(noneLabel, o.materialPath.empty()) &&
            !o.materialPath.empty()) {
            o.materialPath.clear();
            changed = true;
        }
        for (const std::string& rel : listMaterialAssets()) {
            const std::string label = rel.substr(4);  // drop "res/"
            if (ImGui::Selectable(label.c_str(), rel == o.materialPath) &&
                rel != o.materialPath) {
                o.materialPath = rel;
                changed = true;
            }
        }
        ImGui::EndCombo();
    }
    return changed;
}

// Terrain material picker. Lists the project's .mtl assets (res/materials +
// the models' own libraries); "<none>" clears it back to the checker greens.
bool App::drawTerrainMaterialCombo(const char* label, std::string& matPath) {
    const char* noneLabel = "<none - checker greens>";
    std::string current = matPath.empty() ? noneLabel : matPath;
    if (current.rfind("res/", 0) == 0) current = current.substr(4);

    bool changed = false;
    if (ImGui::BeginCombo(label, current.c_str())) {
        if (ImGui::Selectable(noneLabel, matPath.empty()) && !matPath.empty()) {
            matPath.clear();
            changed = true;
        }
        for (const std::string& rel : listMaterialAssets()) {
            const std::string item = rel.substr(4);  // drop "res/"
            if (ImGui::Selectable(item.c_str(), rel == matPath) && rel != matPath) {
                matPath = rel;
                changed = true;
            }
        }
        ImGui::EndCombo();
    }
    return changed;
}

// Project-wide asset lists (models + textures), mirrored straight from res/
// on every draw - hand-dropped files show up without any bookkeeping. The
// Import... buttons are the only file dialogs; everything else picks from
// these lists.
void App::drawAssetsSection() {
    if (!ImGui::CollapsingHeader("Assets")) return;

    // Per-asset texture-quality override of Preferences > Textures. Textures
    // shared by several assets take the highest requested quality.
    auto qualityCombo = [&](const std::string& assetRel) {
        auto it = project_.textureQuality.find(assetRel);
        int cur = it == project_.textureQuality.end() ? 0
                  : it->second == "none"              ? 1
                  : it->second == "8bit"              ? 2
                                                      : 3;
        const char* labels[] = {"(project)", "Full", "8-bit", "4-bit"};
        ImGui::SameLine();
        ImGui::SetNextItemWidth(90.0f);
        if (ImGui::Combo(("##tq" + assetRel).c_str(), &cur, labels, 4)) {
            if (cur == 0)
                project_.textureQuality.erase(assetRel);
            else
                project_.textureQuality[assetRel] =
                    cur == 1 ? "none" : cur == 2 ? "8bit" : "4bit";
            saveAll("Saved");
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Texture quality of this asset's textures\n"
                              "(overrides Preferences > Textures).");
    };

    ImGui::TextDisabled("Models (res/models)");
    ImGui::SameLine();
    if (ImGui::SmallButton("Import model...")) importModelAsset();
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip(".obj = static geometry (+ .mtl/textures)\n"
                          ".glb = animated model (Blender glTF Binary export;\n"
                          "clips are baked to PS2 morph frames at build)");
    const std::vector<std::string> models = listAssetFiles("models", ".obj");
    for (const std::string& m : models) {
        ImGui::Bullet();
        ImGui::SameLine();
        ImGui::TextUnformatted(m.c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton(("x##delmodel" + m).c_str()))
            requestAssetDelete(PendingAssetDelete::Model, "res/models/" + m, m);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Delete this model from the project");
        qualityCombo("res/models/" + m);
        const ModelInfo& info = modelInfo("res/models/" + m);
        if (info.ok) {
            ImGui::SameLine();
            ImGui::TextDisabled("(%d tris, %d mat)", info.tris,
                                (int)info.materials.size());
            if (info.anyMissing) {
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.3f, 1.0f), "missing textures!");
                if (ImGui::IsItemHovered()) {
                    ImGui::BeginTooltip();
                    for (const ModelInfo::MaterialLine& line : info.materials)
                        if (line.missing)
                            ImGui::TextUnformatted((line.text + " - not found next to the .obj").c_str());
                    ImGui::EndTooltip();
                }
            }
        }
    }
    const std::vector<std::string> animModels = listAssetFiles("models", ".glb");
    for (const std::string& m : animModels) {
        ImGui::Bullet();
        ImGui::SameLine();
        ImGui::TextUnformatted(m.c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton(("x##delglb" + m).c_str()))
            requestAssetDelete(PendingAssetDelete::Model, "res/models/" + m, m);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Delete this model from the project");
        const GlbInfo& info = glbInfo("res/models/" + m);
        if (info.ok) {
            ImGui::SameLine();
            ImGui::TextDisabled("(animated: %d clip(s), %d verts)",
                                (int)info.clips.size(), info.vertexCount);
            if (ImGui::IsItemHovered()) {
                ImGui::BeginTooltip();
                for (const std::string& c : info.clips)
                    ImGui::TextUnformatted(c.c_str());
                for (const std::string& w : info.warnings)
                    ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.3f, 1.0f), "%s", w.c_str());
                ImGui::EndTooltip();
            }
            if (!info.warnings.empty()) {
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.3f, 1.0f), "warnings");
            }
        } else {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.3f, 1.0f), "unusable: %s",
                               info.error.c_str());
        }
    }
    if (models.empty() && animModels.empty())
        ImGui::TextDisabled("  none - Import or drop .obj/.glb files there.");

    ImGui::TextDisabled("Materials (res/materials)");
    ImGui::SameLine();
    if (ImGui::SmallButton("New...")) {
        showMaterialEditor_ = true;
        openNewMaterialPopup_ = true;
        matEdNewError_.clear();
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Create a material in the Material Editor\n"
                          "(color, brightness, texture - live preview).");
    ImGui::SameLine();
    if (ImGui::SmallButton("Import .mtl...")) importMaterialAsset();
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Universal material libraries: assign one to many\n"
                          "objects (primitives use the first material, models\n"
                          "override their own mtl). Textures are copied along.");
    const std::vector<std::string> materials = listAssetFiles("materials", ".mtl");
    for (const std::string& m : materials) {
        ImGui::Bullet();
        ImGui::SameLine();
        ImGui::TextUnformatted(m.c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton(("Edit##mat" + m).c_str()))
            openMaterialEditor("res/materials/" + m);
        ImGui::SameLine();
        if (ImGui::SmallButton(("x##delmat" + m).c_str()))
            requestAssetDelete(PendingAssetDelete::Material, "res/materials/" + m, m);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Delete this material from the project");
        qualityCombo("res/materials/" + m);
        const ModelInfo& info = materialInfo("res/materials/" + m);
        if (info.ok) {
            ImGui::SameLine();
            ImGui::TextDisabled("(%d material(s))", (int)info.materials.size());
            if (ImGui::IsItemHovered()) {
                ImGui::BeginTooltip();
                for (const ModelInfo::MaterialLine& line : info.materials)
                    ImGui::TextUnformatted(
                        (line.text + (line.missing ? " - texture MISSING" : ""))
                            .c_str());
                ImGui::EndTooltip();
            }
            if (info.anyMissing) {
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.3f, 1.0f), "missing textures!");
            }
        }
    }
    if (materials.empty())
        ImGui::TextDisabled("  none - Import or drop .mtl files there.");
}

// Creates a scene object for a model that is already inside the project
// (res/models/...) - the "no-copy" path used by the From-project menu and
// after an import has placed the files.
void App::addModelObject(const std::string& relPath) {
    SceneObject o;
    o.type = PrimitiveType::Model;
    o.modelPath = relPath;
    o.color[0] = o.color[1] = o.color[2] = 0.85f;
    o.position[1] = 0.0f;

    // unique name from the file name
    std::string base = std::filesystem::path(relPath).stem().string();
    std::string name = base;
    for (int n = 2;; ++n) {
        bool taken = false;
        for (const auto& other : project_.objects()) taken |= (other.name == name);
        if (!taken) break;
        name = base + "-" + std::to_string(n);
    }
    o.name = name;

    project_.objects().push_back(std::move(o));
    selectOnly((int)project_.objects().size() - 1);
    commitChange();
}

// Categorized object palette, shared by the Scene menu and the "+ Add"
// button in the Project panel.
void App::drawAddObjectMenu() {
    // Pure transform without game geometry - a scene anchor for attached
    // scripts, waypoints and flow-graph logic (sphere marker in the editor).
    if (ImGui::MenuItem("Empty")) addEmpty();
    if (ImGui::BeginMenu("Object")) {
        if (ImGui::BeginMenu("Simple")) {
            if (ImGui::MenuItem("Box")) addObject(PrimitiveType::Box);
            if (ImGui::MenuItem("Sphere")) addObject(PrimitiveType::Sphere);
            if (ImGui::MenuItem("Cylinder")) addObject(PrimitiveType::Cylinder);
            if (ImGui::MenuItem("Cone")) addObject(PrimitiveType::Cone);
            if (ImGui::MenuItem("Plane")) addObject(PrimitiveType::Plane);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Model")) {
            // Only models already inside the project (res/models) - importing
            // new ones lives in Project > Assets.
            const std::vector<std::string> models = listAssetFiles("models", ".obj");
            for (const std::string& m : models)
                if (ImGui::MenuItem(m.c_str())) addModelObject("res/models/" + m);
            const std::vector<std::string> anim = listAssetFiles("models", ".glb");
            for (const std::string& m : anim)
                if (ImGui::MenuItem((m + " (animated)").c_str()))
                    addModelObject("res/models/" + m);
            if (models.empty() && anim.empty())
                ImGui::TextDisabled("No models - Import one in Project > Assets.");
            ImGui::EndMenu();
        }
        // Textured quad with transparency (sign/poster/text on a wall). Assign
        // a material whose map_Kd PNG has an alpha channel in the Properties.
        if (ImGui::MenuItem("Decal")) addDecal();
        // Rectangle that re-draws its listed objects mirrored across its
        // plane (real geometry behind the glass - build it into a wall).
        if (ImGui::MenuItem("Mirror")) addMirror();
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Gameplay")) {
        if (ImGui::MenuItem("Player")) addObject(PrimitiveType::Player);
        if (ImGui::MenuItem("Spawn point")) addObject(PrimitiveType::SpawnPoint);
        if (ImGui::MenuItem("Save point")) addSavePoint();
        // Cutscene Director shot marker (bind camera-track keys to it)
        if (ImGui::MenuItem("Camera")) addObject(PrimitiveType::Camera);
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Effects")) {
        if (ImGui::MenuItem("Fire")) addEmitter(0);
        if (ImGui::MenuItem("Smoke")) addEmitter(1);
        if (ImGui::MenuItem("Fog")) addEmitter(2);
        if (ImGui::MenuItem("Sparks")) addEmitter(3);
        if (ImGui::MenuItem("Rain")) addEmitter(4);
        if (ImGui::MenuItem("Custom")) addEmitter(5);
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Audio")) {
        if (ImGui::MenuItem("Sound emitter")) addSoundEmitter();
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Lighting")) {
        if (ImGui::MenuItem("Point light")) addPointLight();
        ImGui::EndMenu();
    }
}

void App::drawSceneSection() {
    if (!ImGui::CollapsingHeader("Scene objects", ImGuiTreeNodeFlags_DefaultOpen)) return;

    if (ImGui::Button("+ Add object"))
        ImGui::OpenPopup("##add_object");
    if (ImGui::BeginPopup("##add_object")) {
        drawAddObjectMenu();
        ImGui::EndPopup();
    }

    if (project_.objects().empty()) {
        ImGui::TextDisabled("No objects - add a primitive above.");
    } else {
        SceneData& sc = project_.active();
        // With layers, the list groups objects under a collapsible node per
        // layer (plus an Unassigned group); without layers it stays flat.
        const bool grouped = !sc.layers.empty();

        auto layerExists = [&](const std::string& name) {
            for (const SceneLayer& l : sc.layers)
                if (l.name == name) return true;
            return false;
        };

        // A dropped object (or the whole selection, if the dragged one is part
        // of it) is reassigned after the child so the render loop stays stable.
        int dropObj = -1;
        std::string dropLayer;
        bool dropHit = false;

        // One object row: Selectable with the existing multi-select clicks and
        // hidden dimming, doubling as a drag source for layer reassignment.
        // Ctrl/Shift+click extends the selection; plain click replaces it.
        auto objectRow = [&](int i) {
            const SceneObject& o = project_.objects()[i];
            const bool hidden = isObjectHiddenInEditor(o);
            std::string label = o.name + "  (" + primitiveTypeName(o.type) + ")" +
                                (hidden ? "  [hidden]" : "") + "##obj" +
                                std::to_string(i);
            if (hidden)
                ImGui::PushStyleColor(ImGuiCol_Text,
                                      ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
            if (ImGui::Selectable(label.c_str(), isSelected(i))) {
                if (ImGui::GetIO().KeyCtrl || ImGui::GetIO().KeyShift) toggleSelect(i);
                else selectOnly(i);
            }
            if (hidden) ImGui::PopStyleColor();
            if (grouped && ImGui::BeginDragDropSource()) {
                ImGui::SetDragDropPayload("SCENE_OBJECT", &i, sizeof(int));
                const int n =
                    (isSelected(i) && selection_.size() > 1) ? (int)selection_.size() : 1;
                if (n > 1) ImGui::Text("Move %d objects", n);
                else ImGui::TextUnformatted(o.name.c_str());
                ImGui::EndDragDropSource();
            }
        };

        // Turn the just-submitted item into a drop target for object rows.
        auto dropTarget = [&](const std::string& layerName) {
            if (!ImGui::BeginDragDropTarget()) return;
            if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("SCENE_OBJECT")) {
                dropObj = *(const int*)p->Data;
                dropLayer = layerName;
                dropHit = true;
            }
            ImGui::EndDragDropTarget();
        };

        ImGui::BeginChild("##objects", ImVec2(0, grouped ? 220 : 130),
                          ImGuiChildFlags_Borders);
        if (!grouped) {
            for (int i = 0; i < (int)project_.objects().size(); ++i) objectRow(i);
        } else {
            const ImGuiTreeNodeFlags gflags = ImGuiTreeNodeFlags_DefaultOpen |
                                              ImGuiTreeNodeFlags_SpanAvailWidth;
            for (int li = 0; li < (int)sc.layers.size(); ++li) {
                const SceneLayer& l = sc.layers[li];
                int count = 0;
                for (const SceneObject& o : sc.objects)
                    if (o.layer == l.name) ++count;
                std::string header = l.name + "  (" + std::to_string(count) + ")" +
                                     (l.editorVisible ? "" : "  [hidden]") +
                                     "##layergrp" + std::to_string(li);
                if (!l.editorVisible)
                    ImGui::PushStyleColor(ImGuiCol_Text,
                                          ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
                const bool open = ImGui::TreeNodeEx(header.c_str(), gflags);
                if (!l.editorVisible) ImGui::PopStyleColor();
                dropTarget(l.name);
                if (open) {
                    for (int i = 0; i < (int)project_.objects().size(); ++i)
                        if (project_.objects()[i].layer == l.name) objectRow(i);
                    ImGui::TreePop();
                }
            }

            // Unassigned: no layer, or a stale name left by a deleted layer.
            int count = 0;
            for (const SceneObject& o : sc.objects)
                if (o.layer.empty() || !layerExists(o.layer)) ++count;
            std::string header =
                "Unassigned  (" + std::to_string(count) + ")##layergrp_none";
            const bool open = ImGui::TreeNodeEx(header.c_str(), gflags);
            dropTarget("");
            if (open) {
                for (int i = 0; i < (int)project_.objects().size(); ++i) {
                    const SceneObject& o = project_.objects()[i];
                    if (o.layer.empty() || !layerExists(o.layer)) objectRow(i);
                }
                ImGui::TreePop();
            }
        }
        ImGui::EndChild();

        if (grouped)
            ImGui::TextDisabled("Drag objects onto a layer to assign them.");

        if (dropHit) {
            // Move the whole selection when the dragged object is part of it.
            std::vector<int> targets;
            if (isSelected(dropObj) && selection_.size() > 1) targets = selection_;
            else targets.push_back(dropObj);
            bool changed = false;
            for (int t : targets) {
                if (t < 0 || t >= (int)project_.objects().size()) continue;
                if (project_.objects()[t].layer != dropLayer) {
                    project_.objects()[t].layer = dropLayer;
                    changed = true;
                }
            }
            if (changed) commitChange();
        }
    }

    if (selection_.size() > 1)
        ImGui::TextDisabled("%d objects selected - edit shared fields in Properties.",
                            (int)selection_.size());
    else if (selectedObject_ >= 0 && selectedObject_ < (int)project_.objects().size())
        ImGui::TextDisabled("Edit the selection in the Properties window.");
}

// True when the object sits on a layer whose editor eye is off - the
// viewport skips it (render and picking) and the object list dims it.
// Unknown layer names count as visible.
bool App::isObjectHiddenInEditor(const SceneObject& o) const {
    if (o.layer.empty()) return false;
    for (const SceneLayer& l : project_.active().layers)
        if (l.name == o.layer) return !l.editorVisible;
    return false;
}

// Estimated RAM the layer's assets hold while resident: the unique files its
// objects reference - models, material libraries and the textures those point
// at - summed by file size under the project dir. An approximation on
// purpose: PNGs decode/quantize to different sizes in RAM and an asset shared
// between layers is counted in each layer that uses it, but it reliably
// points at the heavy layer before the PS2's 32 MB does. Cached per layer
// name until the next commitChange() (model/material parsing is not
// per-frame material).
double App::layerAssetMB(const std::string& layerName) {
    namespace fs = std::filesystem;
    if (auto it = layerRamCache_.find(layerName); it != layerRamCache_.end())
        return it->second;

    const SceneData& sc = project_.active();
    auto layerExists = [&](const std::string& name) {
        for (const SceneLayer& l : sc.layers)
            if (l.name == name) return true;
        return false;
    };
    std::set<std::string> files;  // project-relative or resolved paths
    auto addFile = [&](const fs::path& p) {
        std::error_code ec;
        const fs::path canon = fs::weakly_canonical(p, ec);
        files.insert((ec ? p : canon).string());
    };
    for (const SceneObject& o : sc.objects) {
        // "" collects the always-resident group: unassigned or stale names
        const bool match = layerName.empty()
                               ? (o.layer.empty() || !layerExists(o.layer))
                               : o.layer == layerName;
        if (!match) continue;
        const fs::path root = project_.dir;
        if (!o.materialPath.empty()) {
            const fs::path mtl = root / o.materialPath;
            addFile(mtl);
            std::vector<objparser::MtlMaterial> mats;
            if (objparser::loadMtl(mtl.string(), mats))
                for (const auto& m : mats)
                    if (!m.texture.empty()) addFile(mtl.parent_path() / m.texture);
        }
        if (o.modelPath.empty()) continue;
        const fs::path model = root / o.modelPath;
        addFile(model);
        // .glb bakes to a .tskl of a similar size; the textures it embeds are
        // extracted next to it at import time and already live under res/,
        // referenced by the baked materials - the file itself is the proxy.
        if (model.extension() == ".obj" && o.materialPath.empty()) {
            objparser::Model m;
            if (objparser::load(model.string(), m)) {
                for (const auto& sub : m.submeshes)
                    if (!sub.texture.empty())
                        addFile(model.parent_path() / sub.texture);
            }
        }
    }
    double bytes = 0;
    for (const std::string& f : files) {
        std::error_code ec;
        const auto sz = fs::file_size(f, ec);
        if (!ec) bytes += (double)sz;
    }
    const double mb = bytes / (1024.0 * 1024.0);
    layerRamCache_[layerName] = mb;
    return mb;
}

// Streaming layers of the ACTIVE scene: named object groups the game can
// evict from / pull into memory at runtime (Load/Unload Layer flow nodes).
// The eye checkbox hides a layer in the editor only; "start" marks it
// resident when the scene starts.
void App::drawLayersSection() {
    // starts open once the scene actually uses layers
    const ImGuiTreeNodeFlags flags =
        project_.active().layers.empty() ? 0 : ImGuiTreeNodeFlags_DefaultOpen;
    if (!ImGui::CollapsingHeader("Layers", flags)) return;

    SceneData& sc = project_.active();
    if (ImGui::SmallButton("+ Layer")) {
        auto exists = [&](const std::string& name) {
            for (const SceneLayer& e : sc.layers)
                if (e.name == name) return true;
            return false;
        };
        SceneLayer l;
        int n = (int)sc.layers.size() + 1;
        do {
            l.name = "Layer " + std::to_string(n++);
        } while (exists(l.name));
        sc.layers.push_back(l);
        commitChange();
    }
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip(
            "Streaming layers (per scene). Assign objects to a layer in\n"
            "Properties; the game can then drop the whole layer from memory\n"
            "and stream it back with the Load / Unload Layer flow nodes -\n"
            "GTA3-style interior streaming. The eye hides the layer in the\n"
            "editor only; \"start\" = in memory when the scene starts.\n"
            "Deleting a layer keeps its objects (they become unassigned).");

    if (sc.layers.empty()) {
        ImGui::TextDisabled("No layers - every object is always in memory.");
        return;
    }

    bool committed = false;
    int deleteIdx = -1;
    for (int i = 0; i < (int)sc.layers.size(); ++i) {
        SceneLayer& l = sc.layers[i];
        ImGui::PushID(i + 4000);

        bool vis = l.editorVisible;
        if (ImGui::Checkbox("##vis", &vis)) {
            l.editorVisible = vis;
            committed = true;
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Show in editor");

        // Reserve exactly enough room on the right for the "start" checkbox,
        // the "N | X.X MB" readout and the "x" button so the name field shrinks
        // to fit instead of letting the readout run under the delete button.
        int count = 0;
        for (const SceneObject& o : sc.objects)
            if (o.layer == l.name) ++count;
        char countBuf[32];
        std::snprintf(countBuf, sizeof(countBuf), "%d | %.1f MB", count,
                      layerAssetMB(l.name));
        const ImGuiStyle& st = ImGui::GetStyle();
        const float xBtnW = ImGui::CalcTextSize("x").x + st.FramePadding.x * 2.0f;
        const float startW = ImGui::GetFrameHeight() + st.ItemInnerSpacing.x +
                             ImGui::CalcTextSize("start").x;
        const float countW = ImGui::CalcTextSize(countBuf).x;
        const float rightW = startW + countW + xBtnW + st.ItemSpacing.x * 3.0f;

        // rename in place; object/flow references remap when the edit ends
        ImGui::SameLine();
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%s", l.name.c_str());
        const std::string before = l.name;
        ImGui::SetNextItemWidth(-rightW);
        if (ImGui::InputText("##name", buf, sizeof(buf))) l.name = buf;
        if (ImGui::IsItemActivated()) {
            layerRenameFrom_ = before;
            layerRenameIdx_ = i;
        }
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            const std::string from =
                layerRenameIdx_ == i ? layerRenameFrom_ : before;
            layerRenameIdx_ = -1;
            const std::string to = l.name;
            bool dup = false;
            for (int k = 0; k < (int)sc.layers.size(); ++k)
                dup |= (k != i && sc.layers[k].name == to);
            if (to.empty() || dup) {
                l.name = from;  // keep names unique and non-empty
            } else if (to != from) {
                for (SceneObject& o : sc.objects) {
                    if (o.layer == from) o.layer = to;
                    for (FlowNode& fn : o.flowGraph.nodes) {
                        const FlowNodeType* t = flowNodeType(fn.type);
                        if (t && t->strKind == FlowParamKind::LayerName &&
                            fn.str == from)
                            fn.str = to;
                    }
                }
            }
            committed = true;
        }

        ImGui::SameLine();
        bool start = l.startLoaded;
        ImGui::BeginDisabled(l.autoStream);  // auto zones decide their own start
        if (ImGui::Checkbox("start", &start)) {
            l.startLoaded = start;
            committed = true;
        }
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(l.autoStream
                                  ? "Auto-streamed: starts loaded only when the\n"
                                    "spawn point is inside the zone"
                                  : "In memory when the scene starts");

        ImGui::SameLine();
        ImGui::TextDisabled("%s", countBuf);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(
                "Objects on this layer | estimated asset RAM while resident\n"
                "(unique model/material/texture files, by file size - an\n"
                "approximation; shared assets count in every layer using them)");

        ImGui::SameLine(ImGui::GetContentRegionMax().x - xBtnW);
        if (ImGui::SmallButton("x")) deleteIdx = i;

        // Auto-streaming zone: load inside the radius, unload past it (the
        // game adds a hysteresis band); Load/Unload Layer nodes can still
        // override until the player next crosses the boundary.
        bool autoStream = l.autoStream;
        if (ImGui::Checkbox("auto-stream", &autoStream)) {
            l.autoStream = autoStream;
            committed = true;
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(
                "Load this layer automatically while the player is within\n"
                "the zone below, unload it when they leave - GTA-style zone\n"
                "streaming, no flow graph needed.");
        if (l.autoStream) {
            ImGui::SameLine();
            float center[2] = {l.streamX, l.streamZ};
            ImGui::SetNextItemWidth(110.0f);
            if (ImGui::DragFloat2("##zonexz", center, 0.5f, 0.0f, 0.0f, "%.0f")) {
                l.streamX = center[0];
                l.streamZ = center[1];
            }
            if (ImGui::IsItemDeactivatedAfterEdit()) committed = true;
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Zone center (world X, Z)");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(70.0f);
            ImGui::DragFloat("##zoner", &l.streamRadius, 0.5f, 1.0f, 4096.0f,
                             "r %.0f");
            if (ImGui::IsItemDeactivatedAfterEdit()) committed = true;
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Zone radius (world units)");
            ImGui::SameLine();
            if (ImGui::SmallButton("Center on sel.") && selectedObject_ >= 0 &&
                selectedObject_ < (int)sc.objects.size()) {
                l.streamX = sc.objects[selectedObject_].position[0];
                l.streamZ = sc.objects[selectedObject_].position[2];
                committed = true;
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Move the zone center to the selected object");
        }
        ImGui::PopID();
    }

    // The unassigned group is the floor every scene pays regardless of
    // streaming - show it so the budget math has all the numbers.
    ImGui::TextDisabled("Always resident (no layer): %.1f MB + terrain",
                        layerAssetMB(""));

    if (deleteIdx >= 0) {
        const std::string name = sc.layers[deleteIdx].name;
        for (SceneObject& o : sc.objects) {
            if (o.layer == name) o.layer.clear();
            for (FlowNode& fn : o.flowGraph.nodes) {
                const FlowNodeType* t = flowNodeType(fn.type);
                if (t && t->strKind == FlowParamKind::LayerName && fn.str == name)
                    fn.str.clear();
            }
        }
        sc.layers.erase(sc.layers.begin() + deleteIdx);
        committed = true;
    }

    if (committed) commitChange();
}

// Display names for the Type field (primitiveTypeName() is the serialized
// lowercase form, e.g. "spawn-point").
static const char* typeLabel(PrimitiveType t) {
    switch (t) {
        case PrimitiveType::Box: return "Box";
        case PrimitiveType::Sphere: return "Sphere";
        case PrimitiveType::Cylinder: return "Cylinder";
        case PrimitiveType::Cone: return "Cone";
        case PrimitiveType::Plane: return "Plane";
        case PrimitiveType::Decal: return "Decal";
        case PrimitiveType::SpawnPoint: return "Spawn point";
        case PrimitiveType::Model: return "3D model";
        case PrimitiveType::Player: return "Player";
        case PrimitiveType::Emitter: return "Particle emitter";
        case PrimitiveType::SoundEmitter: return "Sound emitter";
        case PrimitiveType::PointLight: return "Point light";
        case PrimitiveType::SavePoint: return "Save point";
        case PrimitiveType::Empty: return "Empty";
        case PrimitiveType::Camera: return "Camera";
        case PrimitiveType::Mirror: return "Mirror";
    }
    return "Object";
}

// Edits the object selected in the Project panel / viewport. Only fields the
// game actually reads for the object's type are shown: markers (spawn point,
// player, emitters, lights) have no geometry in the game, so texture,
// rotation, physics and "usable" would be dead settings on them.
void App::drawPropertiesWindow() {
    ImGui::Begin("Properties");
    if (!hasProject_) {
        ImGui::TextDisabled("No project open.");
        ImGui::End();
        return;
    }
    if (selectedObject_ < 0 || selectedObject_ >= (int)project_.objects().size()) {
        ImGui::TextDisabled("No object selected.\nPick one in the Project panel or in "
                            "the viewport.");
        ImGui::End();
        return;
    }
    if (selection_.size() > 1) {
        drawMultiProperties();
        ImGui::End();
        return;
    }
    SceneObject& o = project_.objects()[selectedObject_];

    // Edits apply live; a history snapshot is committed once per finished
    // interaction (slider released, text field defocused...).
    bool committed = false;

    // Real geometry in the game: rendered, collidable, texturable.
    const bool isShape =
        o.type == PrimitiveType::Box || o.type == PrimitiveType::Sphere ||
        o.type == PrimitiveType::Cylinder || o.type == PrimitiveType::Cone ||
        o.type == PrimitiveType::Plane;
    const bool isSolid =
        isShape || o.type == PrimitiveType::Model || o.type == PrimitiveType::SavePoint;

    char nameBuf[128];
    std::snprintf(nameBuf, sizeof(nameBuf), "%s", o.name.c_str());
    if (ImGui::InputText("Name", nameBuf, sizeof(nameBuf))) o.name = nameBuf;
    // Cutscene Director tracks and camera-shot bindings reference objects by
    // name - remap them when the rename edit ends so cutscenes don't go stale.
    if (ImGui::IsItemActivated()) {
        objRenameFrom_ = o.name;
        objRenameIdx_ = selectedObject_;
    }
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        committed = true;
        const std::string from =
            objRenameIdx_ == selectedObject_ ? objRenameFrom_ : std::string();
        objRenameIdx_ = -1;
        if (!from.empty() && from != o.name) {
            for (Sequence& s : project_.sequences) {
                for (SeqTrack& tr : s.tracks)
                    if (tr.target == from) tr.target = o.name;
                for (SeqCameraKey& k : s.cameraKeys)
                    if (k.camera == from) k.camera = o.name;
            }
            if (lookThroughCam_ == from) lookThroughCam_ = o.name;
            // Mirror target lists reference objects by name too.
            for (SceneObject& m : project_.objects())
                if (m.type == PrimitiveType::Mirror)
                    for (std::string& t : m.mirrorObjects)
                        if (t == from) t = o.name;
        }
    }

    // Streaming layer (Project panel > Layers). Shown as soon as the scene
    // has layers - or when the object still references a deleted-scene name.
    if (!project_.active().layers.empty() || !o.layer.empty()) {
        const std::string current = o.layer.empty() ? "<none>" : o.layer;
        if (ImGui::BeginCombo("Layer", current.c_str())) {
            if (ImGui::Selectable("<none>", o.layer.empty()) && !o.layer.empty()) {
                o.layer.clear();
                committed = true;
            }
            for (const SceneLayer& l : project_.active().layers) {
                if (ImGui::Selectable(l.name.c_str(), l.name == o.layer) &&
                    o.layer != l.name) {
                    o.layer = l.name;
                    committed = true;
                }
            }
            ImGui::EndCombo();
        }
    }

    if (isShape) {
        // Plane's enum value isn't contiguous with the other shapes, so map
        // combo indices through an explicit list instead of casting directly.
        static const PrimitiveType kShapeTypes[] = {
            PrimitiveType::Box, PrimitiveType::Sphere, PrimitiveType::Cylinder,
            PrimitiveType::Cone, PrimitiveType::Plane};
        const char* typeNames[] = {"Box", "Sphere", "Cylinder", "Cone", "Plane"};
        int typeIdx = 0;
        for (int i = 0; i < IM_ARRAYSIZE(kShapeTypes); ++i)
            if (kShapeTypes[i] == o.type) typeIdx = i;
        if (ImGui::Combo("Type", &typeIdx, typeNames, IM_ARRAYSIZE(typeNames))) {
            o.type = kShapeTypes[typeIdx];
            // Detail means different things (segments vs box subdivisions) and
            // has different ranges per shape - re-fit the value to the new one.
            o.primDetail = clampPrimDetail(o.type, o.primDetail);
            committed = true;
        }
    } else {
        ImGui::Text("Type:");
        ImGui::SameLine();
        ImGui::TextUnformatted(typeLabel(o.type));
    }
    // Geometry primitives: how many segments (curved) or edge subdivisions
    // (box-like) the mesh is built from. Editable any time, updates live.
    if (o.type == PrimitiveType::Box || o.type == PrimitiveType::Sphere ||
        o.type == PrimitiveType::Cylinder || o.type == PrimitiveType::Cone ||
        o.type == PrimitiveType::SavePoint) {
        const bool box = primDetailIsBoxLike(o.type);
        int detail = o.primDetail;
        if (ImGui::DragInt("Detail", &detail, 0.2f, primDetailMin(o.type),
                           primDetailMax(o.type), box ? "%d subdivisions"
                                                      : "%d segments"))
            o.primDetail = clampPrimDetail(o.type, detail);
        committed |= ImGui::IsItemDeactivatedAfterEdit();
        ImGui::SameLine();
        ImGui::TextDisabled("(%d tris)", primTriangleCount(o.type, o.primDetail));
    }
    if (o.type == PrimitiveType::Model) {
        // model file: pick among the project's res/models assets
        const std::string current = o.modelPath.empty()
                                        ? "<none>"
                                        : std::filesystem::path(o.modelPath)
                                              .filename()
                                              .string();
        if (ImGui::BeginCombo("Model", current.c_str())) {
            const std::vector<std::string> models = listAssetFiles("models", ".obj");
            for (const std::string& m : models) {
                const std::string rel = "res/models/" + m;
                if (ImGui::Selectable(m.c_str(), rel == o.modelPath) &&
                    rel != o.modelPath) {
                    o.modelPath = rel;
                    committed = true;
                }
            }
            const std::vector<std::string> anim = listAssetFiles("models", ".glb");
            for (const std::string& m : anim) {
                const std::string rel = "res/models/" + m;
                if (ImGui::Selectable((m + " (animated)").c_str(),
                                      rel == o.modelPath) &&
                    rel != o.modelPath) {
                    o.modelPath = rel;
                    o.animClip.clear();  // clip names belong to the old file
                    committed = true;
                }
            }
            if (models.empty() && anim.empty())
                ImGui::TextDisabled("No models - Import one in Project > Assets.");
            ImGui::EndCombo();
        }
        if (isAnimatedModelPath(o.modelPath)) {
            const GlbInfo& info = glbInfo(o.modelPath);
            if (info.ok) {
                ImGui::TextDisabled("%d verts, %d baked frames, %d clip(s)",
                                    info.vertexCount, info.frameCount,
                                    (int)info.clips.size());
                for (const std::string& w : info.warnings)
                    ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.3f, 1.0f), "%s",
                                       w.c_str());
                // Baked materials (from the .glb): the colors the game renders
                // the mesh with. Authored in the modelling tool (Blender:
                // material Base Color) and baked into the .tskl at build.
                if (!info.materials.empty()) {
                    ImGui::SeparatorText("Materials");
                    for (const GlbInfo::Material& mat : info.materials) {
                        ImGui::ColorButton(
                            ("##animmat" + mat.name).c_str(),
                            ImVec4(mat.color[0], mat.color[1], mat.color[2],
                                   1.0f),
                            ImGuiColorEditFlags_NoTooltip |
                                ImGuiColorEditFlags_NoDragDrop,
                            ImVec2(14, 14));
                        ImGui::SameLine();
                        if (mat.textured)
                            ImGui::Text("%s (textured)", mat.name.c_str());
                        else
                            ImGui::TextUnformatted(mat.name.c_str());
                    }
                    ImGui::TextDisabled(
                        "Colors come from the model; edit them in the\n"
                        "modelling tool and re-export the .glb.");
                }
                ImGui::SeparatorText("Animation");
                const std::string clipLabel =
                    o.animClip.empty()
                        ? (info.clips.empty() ? "<none>"
                                              : info.clips.front() + " (first)")
                        : o.animClip;
                if (ImGui::BeginCombo("Start clip", clipLabel.c_str())) {
                    for (const std::string& c : info.clips) {
                        const bool selected =
                            c == o.animClip ||
                            (o.animClip.empty() && c == info.clips.front());
                        if (ImGui::Selectable(c.c_str(), selected) &&
                            o.animClip != c) {
                            o.animClip = c;
                            committed = true;
                        }
                    }
                    ImGui::EndCombo();
                }
                if (ImGui::Checkbox("Autoplay at scene start", &o.animAutoplay))
                    committed = true;
                if (ImGui::Checkbox("Loop", &o.animLoop)) committed = true;
                ImGui::DragFloat("Speed", &o.animSpeed, 0.02f, 0.05f, 10.0f,
                                 "%.2fx");
                committed |= ImGui::IsItemDeactivatedAfterEdit();
                ImGui::TextDisabled(
                    "Scripts/flow graph: Play Animation, Stop Animation,\n"
                    "On Animation Finished.");
            } else if (!o.modelPath.empty()) {
                ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.3f, 1.0f),
                                   "Unusable .glb: %s", info.error.c_str());
            }
        } else {
        // materials come from the .obj's MTL file (or the assigned override)
        // - read-only summary
        const ModelInfo& info = modelInfo(o.modelPath, o.materialPath);
        if (info.ok) {
            ImGui::TextDisabled("%d triangles, materials (from .mtl):", info.tris);
            for (const ModelInfo::MaterialLine& m : info.materials) {
                if (m.missing)
                    ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.3f, 1.0f),
                                       "  - %s - MISSING", m.text.c_str());
                else
                    ImGui::TextDisabled("  - %s", m.text.c_str());
            }
            if (info.anyMissing)
                ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.3f, 1.0f),
                                   "Missing textures render as plain color - put the\n"
                                   "files next to the .obj (paths are relative to it).");
        } else if (!o.modelPath.empty()) {
            ImGui::TextDisabled("Model file missing/unparseable - renders as a box.");
        }
        }
    }

    // Empties are pure transforms - scripts read the whole transform (and the
    // color, as a free per-object parameter), so every field stays editable.
    const bool isEmpty = o.type == PrimitiveType::Empty;
    // Decal: a textured quad. Transform + color + material stay editable, but
    // it carries no physics/collision/usable game state (pure visual overlay).
    const bool isDecal = o.type == PrimitiveType::Decal;
    // Camera entity: position + rotation aim the shot, color tints the marker.
    const bool isCamera = o.type == PrimitiveType::Camera;
    // Mirror: transform places the glass rectangle (+Z = the reflective
    // face), color tints it; the mirror-specific block sits further down.
    const bool isMirror = o.type == PrimitiveType::Mirror;

    ImGui::DragFloat3("Position", o.position, 0.1f);
    committed |= ImGui::IsItemDeactivatedAfterEdit();
    // custom emitters rotate too - the rotation aims the emission direction
    if (isSolid || isEmpty || isDecal || isCamera || isMirror ||
        (o.type == PrimitiveType::Emitter && o.emitterKind == 5)) {
        ImGui::DragFloat3("Rotation", o.rotation, 1.0f, -360.0f, 360.0f, "%.0f deg");
        committed |= ImGui::IsItemDeactivatedAfterEdit();
    }
    if (isSolid || isEmpty || isDecal || isMirror ||
        o.type == PrimitiveType::Emitter) {
        ImGui::DragFloat3("Scale", o.scale, 0.05f, 0.01f, 1000.0f);
        committed |= ImGui::IsItemDeactivatedAfterEdit();
    }
    // Color: mesh tint for solids, particle tint for emitters, light color
    // for point lights, marker tint + free per-object parameter for empties,
    // texture tint for decals, marker/frustum tint for camera entities, glass
    // tint for mirrors. The remaining markers draw in fixed colors.
    if (isSolid || isEmpty || isDecal || isCamera || isMirror ||
        o.type == PrimitiveType::Emitter || o.type == PrimitiveType::PointLight) {
        ImGui::ColorEdit3("Color", o.color);
        committed |= ImGui::IsItemDeactivatedAfterEdit();
    }

    const bool animatedModel =
        o.type == PrimitiveType::Model && isAnimatedModelPath(o.modelPath);
    // Material picker: solids texture their surface with it; a decal uses its
    // map_Kd (with alpha) as the decal image.
    if (isSolid || isDecal) {
        // Material (.mtl asset): primitives take the file's first material
        // (Kd + map_Kd on their UVs, modulated by the object color), models
        // use it as an override replacing their own libraries. Animated .glb
        // models carry their own materials - no override.
        if (!animatedModel && drawMaterialCombo(o)) committed = true;
        if (!animatedModel && !o.materialPath.empty()) {
            ImGui::SameLine();
            if (ImGui::SmallButton("Edit..."))
                openMaterialEditor(o.materialPath,
                                   o.type == PrimitiveType::Model ? o.modelPath
                                                                  : "");
        }
        if (!o.materialPath.empty() && o.type != PrimitiveType::Model) {
            const ModelInfo& mat = materialInfo(o.materialPath);
            if (mat.ok && !mat.materials.empty()) {
                const ModelInfo::MaterialLine& line = mat.materials.front();
                if (line.missing)
                    ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.3f, 1.0f),
                                       "%s - texture MISSING", line.text.c_str());
                else
                    ImGui::TextDisabled("%s", line.text.c_str());
            } else {
                ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.3f, 1.0f),
                                   "Material file missing/empty - plain color.");
            }
        }
        if (isDecal) {
            ImGui::TextDisabled(
                "Assign a material whose map_Kd PNG has transparency.\n"
                "Sits just in front of its origin; place it on a surface.");
            if (ImGui::Checkbox("Project onto surfaces", &o.decalProject))
                committed = true;
            if (o.decalProject)
                ImGui::TextDisabled(
                    "Wraps onto terrain + overlapping objects instead of a flat\n"
                    "quad. Scale = projection box: X/Y footprint, Z depth into the\n"
                    "surface; aim +Z at the wall/floor. Baked at build (no PS2 cost).");
        }
    }
    if (isSolid) {
        if (ImGui::Checkbox("Physics (falls with gravity)", &o.physics)) committed = true;
        if (o.type == PrimitiveType::SavePoint) {
            ImGui::TextDisabled("Always usable - USE opens the save menu.");
        } else if (ImGui::Checkbox("Usable (USE prompt + On Used trigger)", &o.usable)) {
            committed = true;
        }
    }
    // Show/Hide Object nodes toggle emitters/sounds too - their on/off state
    // is worth saving; empties can be moved around by scripts. Lights are
    // baked at build, the remaining markers have no game state.
    if (isSolid || isEmpty || o.type == PrimitiveType::Emitter ||
        o.type == PrimitiveType::SoundEmitter) {
        if (ImGui::Checkbox("Save state (position/color/visibility in saves)",
                            &o.saveState))
            committed = true;
    }

    // Player collision. Solid geometry only - markers/emitters never collide.
    if (isSolid) {
        if (animatedModel) {
            // mesh collision is a static-model feature; animated models
            // collide as their baked all-clips AABB or not at all
            bool solid = o.collisionMode != 2;
            if (ImGui::Checkbox("Collision (blocks the player, animation AABB)",
                                &solid)) {
                o.collisionMode = solid ? 0 : 2;
                committed = true;
            }
        } else if (o.type == PrimitiveType::Model) {
            const char* modes[] = {"Box (mesh AABB)", "Mesh (walkable triangles)",
                                   "None"};
            if (ImGui::Combo("Collision", &o.collisionMode, modes, 3)) committed = true;
            if (o.collisionMode == 1)
                ImGui::TextDisabled("Player walks the model's surface (ramps, stairs).");
        } else {
            // primitives collide as their scale box or not at all
            bool solid = o.collisionMode != 2;
            if (ImGui::Checkbox("Collision (blocks the player)", &solid)) {
                o.collisionMode = solid ? 0 : 2;
                committed = true;
            }
        }
    }

    // Rendering cut-off - the cheapest LOD. Only drawing stops beyond the
    // distance; collision, sounds and scripts keep running. For a mirror it
    // gates the glass AND every reflected copy - the whole illusion.
    if (isSolid || isMirror) {
        ImGui::DragFloat("Draw distance", &o.drawDistance, 0.5f, 0.0f, 2000.0f,
                         o.drawDistance > 0.0f ? "%.0f units" : "unlimited");
        committed |= ImGui::IsItemDeactivatedAfterEdit();
        if (o.drawDistance > 0.0f)
            ImGui::TextDisabled(
                "Skipped at draw time when the camera is farther than this;\n"
                "collision and logic still run. 0 = always drawn.");
    }

    if (isMirror) {
        ImGui::SeparatorText("Mirror");
        ImGui::TextDisabled(
            "Re-draws the listed objects mirrored across this rectangle\n"
            "(+Z face). The copies are real geometry behind the plane -\n"
            "build the mirror into a wall so only the glass shows them.");
        ImGui::SliderFloat("Glass opacity", &o.mirrorOpacity, 0.0f, 1.0f, "%.2f");
        committed |= ImGui::IsItemDeactivatedAfterEdit();
        if (ImGui::Checkbox("Reflect player", &o.mirrorReflectPlayer)) committed = true;
        if (o.mirrorReflectPlayer)
            ImGui::TextDisabled(
                "Reflects the third-person avatar. An FPP player has no\n"
                "body to reflect (vampire rules).");
        bool solid = o.collisionMode != 2;
        if (ImGui::Checkbox("Collision (blocks the player)", &solid)) {
            o.collisionMode = solid ? 0 : 2;
            committed = true;
        }
        ImGui::TextUnformatted("Reflected objects:");
        int removeAt = -1;
        for (size_t i = 0; i < o.mirrorObjects.size(); ++i) {
            ImGui::PushID((int)i);
            if (ImGui::SmallButton("x")) removeAt = (int)i;
            ImGui::SameLine();
            bool exists = false;
            for (const SceneObject& t : project_.objects())
                if (t.name == o.mirrorObjects[i]) { exists = true; break; }
            if (exists)
                ImGui::TextUnformatted(o.mirrorObjects[i].c_str());
            else
                ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.3f, 1.0f),
                                   "%s (missing)", o.mirrorObjects[i].c_str());
            ImGui::PopID();
        }
        if (removeAt >= 0) {
            o.mirrorObjects.erase(o.mirrorObjects.begin() + removeAt);
            committed = true;
        }
        if (ImGui::BeginCombo("##mirrorAdd", "+ Add object...")) {
            for (const SceneObject& t : project_.objects()) {
                // only types the game draws as static/animated geometry can
                // show up in the glass; the player has its own checkbox
                const bool reflectable =
                    t.type == PrimitiveType::Box || t.type == PrimitiveType::Sphere ||
                    t.type == PrimitiveType::Cylinder ||
                    t.type == PrimitiveType::Cone || t.type == PrimitiveType::Plane ||
                    t.type == PrimitiveType::SavePoint ||
                    t.type == PrimitiveType::Model || t.type == PrimitiveType::Decal;
                if (!reflectable || t.name == o.name) continue;
                bool listed = false;
                for (const std::string& n : o.mirrorObjects)
                    if (n == t.name) { listed = true; break; }
                if (listed) continue;
                if (ImGui::Selectable(t.name.c_str())) {
                    o.mirrorObjects.push_back(t.name);
                    committed = true;
                }
            }
            ImGui::EndCombo();
        }
        if (o.mirrorObjects.empty() && !o.mirrorReflectPlayer)
            ImGui::TextDisabled("Nothing listed - the mirror shows only glass.");
    }

    if (o.type == PrimitiveType::Emitter) {
        ImGui::SeparatorText("Particle emitter");
        const char* kinds[] = {"Fire", "Smoke", "Fog", "Sparks", "Rain", "Custom"};
        if (ImGui::Combo("Effect", &o.emitterKind, kinds, 6)) committed = true;
        if (ImGui::DragInt("Density (count)", &o.emitterCount, 1.0f, 1, 256)) {}
        committed |= ImGui::IsItemDeactivatedAfterEdit();
        ImGui::DragFloat("Particle size", &o.emitterSize, 0.02f, 0.05f, 8.0f, "%.2f");
        committed |= ImGui::IsItemDeactivatedAfterEdit();
        // optional texture: the material's map_Kd, tinted by the color
        if (drawMaterialCombo(o)) committed = true;
        if (!o.materialPath.empty()) {
            ImGui::SameLine();
            if (ImGui::SmallButton("Edit...")) openMaterialEditor(o.materialPath);
        }
        if (o.emitterKind == 2) {  // fog density
            ImGui::DragFloat("Opacity", &o.emitterOpacity, 0.01f, 0.0f, 1.0f,
                             "%.2f");
            committed |= ImGui::IsItemDeactivatedAfterEdit();
            ImGui::TextDisabled(
                "Slowly swirling puffs. For a thick rolling fog: big spawn\n"
                "area, Follow player on, a soft-alpha texture, and match the\n"
                "color to the distance fog color (Preferences > Distance fog).");
        }
        if (o.emitterKind == 5) {  // custom physics knobs
            ImGui::DragFloat("Speed", &o.emitterSpeed, 0.05f, 0.0f, 50.0f,
                             "%.2f u/s");
            committed |= ImGui::IsItemDeactivatedAfterEdit();
            ImGui::DragFloat("Spread", &o.emitterSpread, 0.5f, 0.0f, 90.0f,
                             "%.0f deg");
            committed |= ImGui::IsItemDeactivatedAfterEdit();
            ImGui::DragFloat("Gravity", &o.emitterGravity, 0.1f, -30.0f, 50.0f,
                             "%.1f u/s2");
            committed |= ImGui::IsItemDeactivatedAfterEdit();
            ImGui::DragFloat("Weight", &o.emitterWeight, 0.02f, 0.05f, 10.0f,
                             "%.2f");
            committed |= ImGui::IsItemDeactivatedAfterEdit();
            ImGui::DragFloat("Lifetime", &o.emitterLife, 0.05f, 0.1f, 10.0f,
                             "%.2f s");
            committed |= ImGui::IsItemDeactivatedAfterEdit();
            ImGui::DragFloat("Grow", &o.emitterGrow, 0.02f, 0.1f, 4.0f, "%.2f x");
            committed |= ImGui::IsItemDeactivatedAfterEdit();
            ImGui::DragFloat("Opacity", &o.emitterOpacity, 0.01f, 0.0f, 1.0f,
                             "%.2f");
            committed |= ImGui::IsItemDeactivatedAfterEdit();
            if (ImGui::Checkbox("Die on terrain", &o.emitterDieOnGround))
                committed = true;
            ImGui::TextDisabled(
                "Particles shoot along the emitter's +Y axis - use\n"
                "Rotation to aim (90 deg X = a horizontal pipe leak).\n"
                "Negative gravity rises (steam); low weight = air drag.");
        }
        if (ImGui::Checkbox("Enabled", &o.emitterEnabled)) committed = true;
        if (ImGui::Checkbox("Follow player", &o.emitterFollowPlayer)) committed = true;
        if (o.emitterFollowPlayer)
            ImGui::TextDisabled("Position is an offset from the player - keep\n"
                                "X/Z near 0 and Y = height above the player.");
        ImGui::TextDisabled("Color tints the particles; scale X/Z = spawn area.\n"
                            "Rain falls from the emitter down to the terrain.\n"
                            "Show/Hide Object nodes switch the emitter on/off.");
    }

    if (o.type == PrimitiveType::SoundEmitter) {
        ImGui::SeparatorText("Sound emitter");
        if (project_.sounds.empty()) {
            ImGui::TextDisabled("No sounds - import WAVs in the Sounds section first.");
        } else {
            int current = -1;
            for (int i = 0; i < (int)project_.sounds.size(); ++i)
                if (project_.sounds[i] == o.soundPath) current = i;
            const std::string preview =
                current >= 0 ? project_.sounds[current] : "<pick a sound>";
            if (ImGui::BeginCombo("Sound", preview.c_str())) {
                for (int i = 0; i < (int)project_.sounds.size(); ++i) {
                    if (ImGui::Selectable(project_.sounds[i].c_str(), i == current)) {
                        o.soundPath = project_.sounds[i];
                        committed = true;
                    }
                }
                ImGui::EndCombo();
            }
        }
        if (ImGui::Checkbox("Autoplay (while the player is in range)", &o.soundAuto))
            committed = true;
        if (ImGui::Checkbox("Play on player (plain stereo, ignores position)",
                            &o.soundOnPlayer))
            committed = true;
        if (!o.soundOnPlayer) {
            ImGui::DragFloat("Range", &o.soundRange, 0.5f, 0.5f, 200.0f, "%.1f units");
            committed |= ImGui::IsItemDeactivatedAfterEdit();
        }
        ImGui::DragFloat("Interval", &o.soundInterval, 0.1f, 0.0f, 60.0f, "%.1f s");
        committed |= ImGui::IsItemDeactivatedAfterEdit();
        if (o.soundOnPlayer) {
            ImGui::TextDisabled("Plays centered at full volume everywhere -\n"
                                "no distance falloff, no panning (dialogs,\n"
                                "narration). Hide Object mutes.");
        } else {
            ImGui::TextDisabled("Volume fades with distance to the player.\n"
                                "Interval 0 loops the sample seamlessly; > 0\n"
                                "retriggers it every N seconds. Hide Object mutes.");
        }
    }

    if (o.type == PrimitiveType::PointLight) {
        ImGui::SeparatorText("Point light");
        ImGui::TextDisabled("The \"Color\" field above sets the light color.");
        ImGui::DragFloat("Brightness", &o.lightBright, 0.02f, 0.0f, 4.0f, "%.2f");
        committed |= ImGui::IsItemDeactivatedAfterEdit();
        ImGui::DragFloat("Radius", &o.lightRadius, 0.1f, 0.1f, 100.0f, "%.1f units");
        committed |= ImGui::IsItemDeactivatedAfterEdit();
        ImGui::TextDisabled("Previewed live in the viewport; in the game it is\n"
                            "baked into nearby terrain & object vertex colors\n"
                            "at build (static light, zero runtime cost).");
    }

    if (o.type == PrimitiveType::SavePoint) {
        ImGui::SeparatorText("Save point");
        ImGui::TextDisabled("Renders as a solid box in the game. Pressing USE\n"
                            "on it opens the save menu: 3 slots on the memory\n"
                            "card (mc0:), saving flagged objects, custom save\n"
                            "values, the player position and the scene.");
    }

    if (o.type == PrimitiveType::Camera) {
        ImGui::SeparatorText("Camera");
        ImGui::DragFloat("FOV (deg)", &o.cameraFov, 0.5f, 20.0f, 110.0f, "%.0f");
        committed |= ImGui::IsItemDeactivatedAfterEdit();
        if (o.cameraFov < 20.0f) o.cameraFov = 20.0f;
        if (o.cameraFov > 110.0f) o.cameraFov = 110.0f;
        const bool looking = lookThroughCam_ == o.name;
        if (ImGui::Button(looking ? "Stop looking through" : "Look through")) {
            if (looking)
                lookThroughCam_.clear();
            else
                lookThroughCam_ = o.name;  // editor view state - no undo entry
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Render the viewport from this camera (also in the\n"
                              "\"View:\" control in the viewport corner).");
        ImGui::TextDisabled("A Cutscene Director shot marker - invisible in the\n"
                            "game. Bind a camera-track keyframe to it (Tools >\n"
                            "Cutscene Director) and the shot films from here,\n"
                            "looking down the +Z wedge, with this FOV. Animate\n"
                            "this object in the same sequence for dolly shots.");
    }

    if (isEmpty) {
        ImGui::SeparatorText("Empty");
        ImGui::TextDisabled("Pure transform - invisible in the game, no collision.\n"
                            "An anchor for attached scripts, a waypoint for flow\n"
                            "graphs; scripts read position/rotation/scale/color.");
    }

    if (o.type == PrimitiveType::Player) {
        ImGui::SeparatorText("Player");
        const char* modes[] = {"Walk (FPP)", "Noclip (fly)", "Third person"};
        if (ImGui::Combo("Mode", &o.playerMode, modes, 3)) committed = true;
        ImGui::DragFloat("Walk speed", &o.playerWalkSpeed, 0.02f, 0.05f, 10.0f, "%.2f");
        committed |= ImGui::IsItemDeactivatedAfterEdit();
        ImGui::DragFloat("Look speed", &o.playerLookSpeed, 0.05f, 0.1f, 5.0f, "%.2f");
        committed |= ImGui::IsItemDeactivatedAfterEdit();
        ImGui::DragFloat(o.playerMode == 2 ? "Body height" : "Eye height",
                         &o.playerEyeHeight, 0.05f, 0.2f, 50.0f, "%.2f");
        committed |= ImGui::IsItemDeactivatedAfterEdit();
        committed |= ImGui::Checkbox("Can jump (X)", &o.playerCanJump);
        if (o.playerCanJump) {
            ImGui::DragFloat("Jump speed", &o.playerJumpSpeed, 0.1f, 0.0f, 50.0f, "%.1f");
            committed |= ImGui::IsItemDeactivatedAfterEdit();
        }
        ImGui::TextDisabled("First player in the scene drives the camera in the game.");
        if (o.playerMode == 2)
            ImGui::TextDisabled("Third person: X jumps. The avatar faces where it walks.");
        else
            ImGui::TextDisabled("Noclip: X up, Square down. Walk: X jumps.");

        // Third-person avatar: the Player's OWN animated .glb model, with its
        // clips mapped to locomotion states. The same .glb/anim pipeline as
        // regular animated models - the runtime just drives the transform and
        // auto-picks the clip from the player's real speed (walk/run/idle),
        // cross-faded. Scripts/flow-graph can still force any clip.
        if (o.playerMode == 2) {
            ImGui::SeparatorText("Avatar model");
            const std::string current =
                o.modelPath.empty()
                    ? "<none>"
                    : std::filesystem::path(o.modelPath).filename().string();
            if (ImGui::BeginCombo("Model", current.c_str())) {
                const std::vector<std::string> anim = listAssetFiles("models", ".glb");
                for (const std::string& m : anim) {
                    const std::string rel = "res/models/" + m;
                    if (ImGui::Selectable((m + " (animated)").c_str(),
                                          rel == o.modelPath) &&
                        rel != o.modelPath) {
                        o.modelPath = rel;
                        o.playerIdleClip.clear();
                        o.playerWalkClip.clear();
                        o.playerRunClip.clear();
                        o.playerJumpClip.clear();
                        committed = true;
                    }
                }
                if (anim.empty())
                    ImGui::TextDisabled(
                        "No animated .glb models - Import one in Project > Assets.");
                ImGui::EndCombo();
            }
            if (o.modelPath.empty()) {
                ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.3f, 1.0f),
                                   "Pick an animated .glb - the avatar is invisible\n"
                                   "without one (only the camera moves).");
            } else if (!isAnimatedModelPath(o.modelPath)) {
                ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.3f, 1.0f),
                                   "Third-person bodies must be an animated .glb.");
            } else {
                const GlbInfo& info = glbInfo(o.modelPath);
                if (!info.ok) {
                    ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.3f, 1.0f),
                                       "Unusable .glb: %s", info.error.c_str());
                } else {
                    ImGui::TextDisabled("%d verts, %d clip(s)", info.vertexCount,
                                        (int)info.clips.size());
                    // Locomotion clip mapping. Idle/Walk are required (fall back
                    // to the first clip); Run/Jump are optional (<none>).
                    auto clipCombo = [&](const char* label, std::string& clip,
                                         bool optional) {
                        const std::string cur =
                            clip.empty() ? (optional ? "<none>"
                                                     : (info.clips.empty()
                                                            ? "<none>"
                                                            : info.clips.front() +
                                                                  " (first)"))
                                         : clip;
                        if (ImGui::BeginCombo(label, cur.c_str())) {
                            if (optional && ImGui::Selectable("<none>", clip.empty())) {
                                if (!clip.empty()) {
                                    clip.clear();
                                    committed = true;
                                }
                            }
                            for (const std::string& c : info.clips) {
                                if (ImGui::Selectable(c.c_str(), c == clip) &&
                                    clip != c) {
                                    clip = c;
                                    committed = true;
                                }
                            }
                            ImGui::EndCombo();
                        }
                    };
                    clipCombo("Idle clip", o.playerIdleClip, false);
                    clipCombo("Walk clip", o.playerWalkClip, false);
                    clipCombo("Run clip", o.playerRunClip, true);
                    clipCombo("Jump clip", o.playerJumpClip, true);
                    ImGui::DragFloat("Run at", &o.playerRunThreshold, 0.01f, 0.1f,
                                     1.0f, "%.2f of walk speed");
                    committed |= ImGui::IsItemDeactivatedAfterEdit();
                    ImGui::TextDisabled(
                        "Clip auto-selected from real speed; a script/flow\n"
                        "\"Play Animation\" one-shot plays to the end first.");
                }
            }

            ImGui::SeparatorText("Third-person camera");
            // Distance / Height / Shoulder are the rig offset in the camera's
            // own frame: back, up, sideways.
            ImGui::DragFloat("Distance", &o.playerCamDist, 0.1f, 1.0f, 40.0f, "%.1f");
            committed |= ImGui::IsItemDeactivatedAfterEdit();
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("How far back the camera sits. The spring arm may\n"
                                  "shorten it, so this is the maximum.");
            ImGui::DragFloat("Height", &o.playerCamHeight, 0.05f, 0.0f, 20.0f, "%.2f");
            committed |= ImGui::IsItemDeactivatedAfterEdit();
            ImGui::DragFloat("Shoulder", &o.playerCamShoulder, 0.02f, -3.0f, 3.0f, "%.2f");
            committed |= ImGui::IsItemDeactivatedAfterEdit();
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Slides the camera sideways for an over-the-shoulder\n"
                                  "shot: 0 = centered behind, ~0.6 = right shoulder,\n"
                                  "negative = left. The avatar moves off-center in\n"
                                  "frame (eye and aim point shift together).");
            ImGui::DragFloat("Turn rate", &o.playerTurnRate, 0.01f, 0.02f, 1.0f, "%.2f");
            committed |= ImGui::IsItemDeactivatedAfterEdit();
        }

        ImGui::SeparatorText("Flashlight");
        committed |= ImGui::Checkbox("Enabled", &o.flashlightEnabled);
        if (o.flashlightEnabled) {
            ImGui::ColorEdit3("Light color", o.flashlightColor);
            committed |= ImGui::IsItemDeactivatedAfterEdit();
            ImGui::DragFloat("Reach (units)", &o.flashlightRange, 0.5f, 1.0f, 200.0f,
                             "%.1f");
            committed |= ImGui::IsItemDeactivatedAfterEdit();
            ImGui::DragFloat("Cone half-angle (deg)", &o.flashlightAngle, 0.5f, 2.0f,
                             80.0f, "%.1f");
            committed |= ImGui::IsItemDeactivatedAfterEdit();
        }
        // Optional pad button the player presses to turn the beam on/off. The
        // on/off state only shows while Enabled (it respects Enabled), and the
        // flow graph can flip Enabled with the Set Flashlight node.
        const char* toggleBtns[] = {"<none>",   "Cross",    "Circle",    "Square",
                                    "Triangle", "DpadUp",   "DpadDown",  "DpadLeft",
                                    "DpadRight", "L1",      "L2",        "L3",
                                    "R1",       "R2",       "R3",        "Start",
                                    "Select"};
        const std::string cur =
            o.flashlightToggleButton.empty() ? "<none>" : o.flashlightToggleButton;
        if (ImGui::BeginCombo("Toggle button", cur.c_str())) {
            for (const char* b : toggleBtns) {
                const bool isNone = std::strcmp(b, "<none>") == 0;
                const bool selected =
                    isNone ? o.flashlightToggleButton.empty() : o.flashlightToggleButton == b;
                if (ImGui::Selectable(b, selected)) {
                    o.flashlightToggleButton = isNone ? std::string() : std::string(b);
                    committed = true;
                }
            }
            ImGui::EndCombo();
        }
        ImGui::TextDisabled("Enabled is the master switch (Set Flashlight flow node\n"
                            "can change it). The toggle button gates the beam on/off\n"
                            "at runtime, but only while Enabled.");
    }

    // Attached scripts (Unity-style components): class names registered in
    // src/scripts/*.cpp with TYRA_OBJECT_SCRIPT(Name). The game creates one
    // instance per attachment at scene load - the same class on five objects
    // runs as five independent instances, each seeing its object as `self`.
    ImGui::SeparatorText("Scripts");
    {
        const std::vector<std::string> registered = objectScriptNames();
        auto isRegistered = [&](const std::string& n) {
            for (const std::string& r : registered)
                if (r == n) return true;
            return false;
        };
        for (int i = 0; i < (int)o.scripts.size();) {
            ImGui::PushID(i);
            const bool removed = ImGui::SmallButton("x");
            ImGui::SameLine();
            if (isRegistered(o.scripts[i])) {
                ImGui::TextUnformatted(o.scripts[i].c_str());
            } else {
                ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s - not found",
                                   o.scripts[i].c_str());
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("No TYRA_OBJECT_SCRIPT(%s) in src\\scripts\\*.cpp\n"
                                      "- the game skips it (with a log line).",
                                      o.scripts[i].c_str());
            }
            ImGui::PopID();
            if (removed) {
                o.scripts.erase(o.scripts.begin() + i);
                committed = true;
            } else {
                ++i;
            }
        }
        ImGui::SetNextItemWidth(ImGui::CalcItemWidth());
        if (ImGui::BeginCombo("##attach_script", "Attach script...")) {
            bool any = false;
            for (const std::string& r : registered) {
                bool attached = false;
                for (const std::string& s : o.scripts) attached |= (s == r);
                if (attached) continue;
                any = true;
                if (ImGui::Selectable(r.c_str(), false)) {
                    o.scripts.push_back(r);
                    committed = true;
                }
            }
            if (!any)
                ImGui::TextDisabled(registered.empty()
                                        ? "No object scripts in src\\scripts yet."
                                        : "Every script is already attached.");
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("New script...")) {
            openNewScriptPopup_ = true;
            newScriptError_.clear();
            newScriptAttachTo_ = selectedObject_;
        }
    }

    ImGui::Separator();
    if (ImGui::Button("Delete object")) {
        if (committed) commitChange();  // flush any pending field edit first
        deleteSelectedObjects();        // commits + clears on its own
        ImGui::End();
        return;
    }

    if (committed) commitChange();
    ImGui::End();
}

// Properties body for a multi-selection: only the fields common to every
// selected object. Transforms apply relatively (a drag nudges the whole group
// by the same delta, keeping its arrangement); other shared fields are set to
// one value for all and render a "mixed" dash while they differ. Inherently
// per-object fields (name, model/material/sound, scripts) are omitted - they
// are edited by selecting a single object. Called with the Properties window
// already open (drawPropertiesWindow handles Begin/End).
void App::drawMultiProperties() {
    std::vector<SceneObject*> objs;
    for (int i : selection_)
        if (i >= 0 && i < (int)project_.objects().size())
            objs.push_back(&project_.objects()[i]);
    if (objs.size() < 2) return;
    SceneObject& primary = *objs.back();  // anchor: seeds the transform values
    bool committed = false;

    // Header + per-type tally ("2 Box, 1 Sphere").
    ImGui::Text("%d objects selected", (int)objs.size());
    {
        std::string tally;
        for (int t = 0; t <= (int)PrimitiveType::Empty; ++t) {
            int n = 0;
            for (auto* p : objs)
                if ((int)p->type == t) ++n;
            if (!n) continue;
            if (!tally.empty()) tally += ", ";
            tally += std::to_string(n) + " " + typeLabel((PrimitiveType)t);
        }
        ImGui::TextDisabled("%s", tally.c_str());
    }
    ImGui::Separator();

    // Which field groups apply = the intersection over the whole selection.
    // Same type predicates the single-object view uses (isShape includes Plane;
    // Decal is a textured quad with transform/color/material but no game state).
    bool allShape = true, allSolid = true, allSaveable = true, allRot = true,
         allScale = true, allColor = true, allSameType = true, allModel = true,
         allEmitter = true, allLight = true, allDetail = true, anyModel = false,
         anySavePoint = false;
    for (auto* p : objs) {
        const SceneObject& o = *p;
        const bool shape = o.type == PrimitiveType::Box || o.type == PrimitiveType::Sphere ||
                           o.type == PrimitiveType::Cylinder ||
                           o.type == PrimitiveType::Cone || o.type == PrimitiveType::Plane;
        const bool solid =
            shape || o.type == PrimitiveType::Model || o.type == PrimitiveType::SavePoint;
        const bool empty = o.type == PrimitiveType::Empty;
        const bool decal = o.type == PrimitiveType::Decal;
        const bool mirror = o.type == PrimitiveType::Mirror;
        // Detail (segments/subdivisions) exists for the curved/box-like
        // primitives (SavePoint tessellates as a Box), not for the flat Plane.
        const bool hasDetail = o.type == PrimitiveType::Box ||
                               o.type == PrimitiveType::Sphere ||
                               o.type == PrimitiveType::Cylinder ||
                               o.type == PrimitiveType::Cone ||
                               o.type == PrimitiveType::SavePoint;
        allShape = allShape && shape;
        allSolid = allSolid && solid;
        allDetail = allDetail && hasDetail;
        allSameType = allSameType && (o.type == primary.type);
        allModel = allModel && (o.type == PrimitiveType::Model);
        allEmitter = allEmitter && (o.type == PrimitiveType::Emitter);
        allLight = allLight && (o.type == PrimitiveType::PointLight);
        anyModel = anyModel || (o.type == PrimitiveType::Model);
        anySavePoint = anySavePoint || (o.type == PrimitiveType::SavePoint);
        allRot = allRot && (solid || empty || decal || mirror ||
                            o.type == PrimitiveType::Camera ||
                            (o.type == PrimitiveType::Emitter && o.emitterKind == 5));
        allScale = allScale && (solid || empty || decal || mirror ||
                                o.type == PrimitiveType::Emitter);
        allColor = allColor && (solid || empty || decal || mirror ||
                                o.type == PrimitiveType::Emitter ||
                                o.type == PrimitiveType::PointLight ||
                                o.type == PrimitiveType::Camera);
        allSaveable = allSaveable && (solid || empty || o.type == PrimitiveType::Emitter ||
                                      o.type == PrimitiveType::SoundEmitter);
    }

    // --- edit helpers ---
    // Relative transform: seed from the anchor, apply the drag delta to all.
    auto relDrag3 = [&](const char* label, float* (*get)(SceneObject&), float speed,
                        float lo, float hi, const char* fmt) {
        float* pv = get(primary);
        float v[3] = {pv[0], pv[1], pv[2]};
        ImGui::DragFloat3(label, v, speed, lo, hi, fmt);
        for (int k = 0; k < 3; ++k) {
            const float d = v[k] - pv[k];
            if (d != 0.0f)
                for (auto* p : objs) get(*p)[k] += d;
        }
        if (ImGui::IsItemDeactivatedAfterEdit()) committed = true;
    };
    // Set-all scalar/bool/combo with a "mixed" indicator while values differ.
    auto multiCheck = [&](const char* label, bool SceneObject::* field) {
        bool mixed = false;
        for (auto* p : objs) mixed = mixed || (p->*field != primary.*field);
        bool v = primary.*field;
        if (mixed) ImGui::PushItemFlag(ImGuiItemFlags_MixedValue, true);
        const bool changed = ImGui::Checkbox(label, &v);
        if (mixed) ImGui::PopItemFlag();
        if (changed) {
            for (auto* p : objs) p->*field = v;
            committed = true;
        }
    };
    auto multiDragF = [&](const char* label, float SceneObject::* field, float speed,
                          float lo, float hi, const char* fmt) {
        bool mixed = false;
        for (auto* p : objs) mixed = mixed || (p->*field != primary.*field);
        float v = primary.*field;
        if (mixed) ImGui::PushItemFlag(ImGuiItemFlags_MixedValue, true);
        ImGui::DragFloat(label, &v, speed, lo, hi, fmt);
        if (mixed) ImGui::PopItemFlag();
        if (v != primary.*field)
            for (auto* p : objs) p->*field = v;
        if (ImGui::IsItemDeactivatedAfterEdit()) committed = true;
    };
    auto multiCombo = [&](const char* label, int SceneObject::* field,
                          const char* const items[], int count) {
        const int v0 = primary.*field;
        bool mixed = false;
        for (auto* p : objs) mixed = mixed || (p->*field != v0);
        const char* preview =
            mixed ? "(multiple)" : (v0 >= 0 && v0 < count ? items[v0] : "");
        if (ImGui::BeginCombo(label, preview)) {
            for (int i = 0; i < count; ++i)
                if (ImGui::Selectable(items[i], !mixed && v0 == i)) {
                    for (auto* p : objs) p->*field = i;
                    committed = true;
                }
            ImGui::EndCombo();
        }
    };

    // --- transforms (relative) ---
    relDrag3("Position", [](SceneObject& o) -> float* { return o.position; }, 0.1f, 0.0f,
             0.0f, "%.3f");
    if (allRot)
        relDrag3("Rotation", [](SceneObject& o) -> float* { return o.rotation; }, 1.0f,
                 -360.0f, 360.0f, "%.0f deg");
    if (allScale) {
        relDrag3("Scale", [](SceneObject& o) -> float* { return o.scale; }, 0.05f, 0.01f,
                 1000.0f, "%.3f");
        for (auto* p : objs)
            for (float& s : p->scale)
                if (s < 0.01f) s = 0.01f;  // additive delta must not go non-positive
    }
    ImGui::TextDisabled("Transforms apply to all - the arrangement is kept.");

    // --- color ---
    if (allColor) {
        float c[3] = {primary.color[0], primary.color[1], primary.color[2]};
        bool mixed = false;
        for (auto* p : objs)
            mixed = mixed || p->color[0] != c[0] || p->color[1] != c[1] ||
                    p->color[2] != c[2];
        if (mixed) ImGui::PushItemFlag(ImGuiItemFlags_MixedValue, true);
        const bool changed = ImGui::ColorEdit3("Color", c);
        if (mixed) ImGui::PopItemFlag();
        if (changed)
            for (auto* p : objs) {
                p->color[0] = c[0], p->color[1] = c[1], p->color[2] = c[2];
            }
        if (ImGui::IsItemDeactivatedAfterEdit()) committed = true;
    }

    // --- shape type / detail ---
    if (allShape) {
        // Shape enum values aren't contiguous (Plane = 12), so map combo indices
        // through an explicit list - same as the single-object view.
        static const PrimitiveType kShapeTypes[] = {
            PrimitiveType::Box, PrimitiveType::Sphere, PrimitiveType::Cylinder,
            PrimitiveType::Cone, PrimitiveType::Plane};
        const char* typeNames[] = {"Box", "Sphere", "Cylinder", "Cone", "Plane"};
        int t0 = 0;
        for (int i = 0; i < IM_ARRAYSIZE(kShapeTypes); ++i)
            if (kShapeTypes[i] == primary.type) t0 = i;
        bool mixedT = false;
        for (auto* p : objs) mixedT = mixedT || p->type != primary.type;
        const char* preview = mixedT ? "(multiple)" : typeNames[t0];
        if (ImGui::BeginCombo("Type", preview)) {
            for (int i = 0; i < IM_ARRAYSIZE(kShapeTypes); ++i)
                if (ImGui::Selectable(typeNames[i], !mixedT && t0 == i)) {
                    for (auto* p : objs) {
                        p->type = kShapeTypes[i];
                        p->primDetail = clampPrimDetail(p->type, p->primDetail);
                    }
                    committed = true;
                }
            ImGui::EndCombo();
        }
    }
    if (allDetail && allSameType) {
        const bool box = primDetailIsBoxLike(primary.type);
        int d = primary.primDetail;
        bool mixedD = false;
        for (auto* p : objs) mixedD = mixedD || p->primDetail != primary.primDetail;
        if (mixedD) ImGui::PushItemFlag(ImGuiItemFlags_MixedValue, true);
        ImGui::DragInt("Detail", &d, 0.2f, primDetailMin(primary.type),
                       primDetailMax(primary.type), box ? "%d subdivisions" : "%d segments");
        if (mixedD) ImGui::PopItemFlag();
        if (d != primary.primDetail)
            for (auto* p : objs) p->primDetail = clampPrimDetail(p->type, d);
        if (ImGui::IsItemDeactivatedAfterEdit()) committed = true;
    }

    // --- solid geometry fields ---
    if (allSolid) {
        multiDragF("Draw distance", &SceneObject::drawDistance, 0.5f, 0.0f, 2000.0f,
                   "%.0f units");
        multiCheck("Physics (falls with gravity)", &SceneObject::physics);
        if (!anySavePoint)
            multiCheck("Usable (USE prompt + On Used)", &SceneObject::usable);
        if (allModel) {
            const char* modes[] = {"Box (mesh AABB)", "Mesh (walkable triangles)", "None"};
            multiCombo("Collision", &SceneObject::collisionMode, modes, 3);
        } else if (!anyModel) {
            // primitives / save points: solid box or none
            bool mixed = false;
            for (auto* p : objs)
                mixed = mixed ||
                        (p->collisionMode != 2) != (primary.collisionMode != 2);
            bool solid = primary.collisionMode != 2;
            if (mixed) ImGui::PushItemFlag(ImGuiItemFlags_MixedValue, true);
            const bool changed = ImGui::Checkbox("Collision (blocks the player)", &solid);
            if (mixed) ImGui::PopItemFlag();
            if (changed) {
                for (auto* p : objs) p->collisionMode = solid ? 0 : 2;
                committed = true;
            }
        }
    }
    if (allSaveable)
        multiCheck("Save state (position/color/visibility in saves)",
                   &SceneObject::saveState);

    // --- emitter / light groups (only when the whole selection is that type) ---
    if (allEmitter) {
        ImGui::SeparatorText("Particle emitter");
        const char* kinds[] = {"Fire", "Smoke", "Fog", "Sparks", "Rain", "Custom"};
        multiCombo("Effect", &SceneObject::emitterKind, kinds, 6);
        multiDragF("Particle size", &SceneObject::emitterSize, 0.02f, 0.05f, 8.0f, "%.2f");
        multiDragF("Opacity", &SceneObject::emitterOpacity, 0.01f, 0.0f, 1.0f, "%.2f");
        multiCheck("Enabled", &SceneObject::emitterEnabled);
        multiCheck("Follow player", &SceneObject::emitterFollowPlayer);
    }
    if (allLight) {
        ImGui::SeparatorText("Point light");
        multiDragF("Brightness", &SceneObject::lightBright, 0.02f, 0.0f, 4.0f, "%.2f");
        multiDragF("Radius", &SceneObject::lightRadius, 0.1f, 0.1f, 100.0f, "%.1f units");
    }

    ImGui::Separator();
    ImGui::TextDisabled("Name, model, materials, sounds and scripts are edited\n"
                        "one object at a time - select a single object for those.");
    ImGui::Separator();
    const std::string delLabel =
        "Delete " + std::to_string((int)objs.size()) + " objects";
    if (ImGui::Button(delLabel.c_str())) {
        if (committed) commitChange();
        deleteSelectedObjects();  // commits + clears on its own
        return;
    }
    if (committed) commitChange();
}

// Class names registered with TYRA_OBJECT_SCRIPT(...) across src/scripts,
// sorted and deduplicated. The directory is walked on every call (the
// Scripts panel already pays that price per frame); file contents are
// cached by write time so files are only re-read after edits.
std::vector<std::string> App::objectScriptNames() {
    std::vector<std::string> names;
    const std::filesystem::path dir =
        std::filesystem::path(project_.dir) / "src" / "scripts";
    std::error_code ec;
    if (!std::filesystem::exists(dir, ec)) return names;
    for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
        if (entry.path().extension() != ".cpp") continue;
        const std::string key = entry.path().string();
        const auto mtime = std::filesystem::last_write_time(entry.path(), ec);
        auto it = scriptScanCache_.find(key);
        if (it == scriptScanCache_.end() || it->second.mtime != mtime) {
            ScriptFileScan scan;
            scan.mtime = mtime;
            std::ifstream f(entry.path(), std::ios::binary);
            std::stringstream ss;
            ss << f.rdbuf();
            const std::string src = ss.str();
            static const std::string kMacro = "TYRA_OBJECT_SCRIPT(";
            for (size_t pos = src.find(kMacro); pos != std::string::npos;
                 pos = src.find(kMacro, pos + kMacro.size())) {
                const size_t end = src.find(')', pos + kMacro.size());
                if (end == std::string::npos) break;
                std::string n =
                    src.substr(pos + kMacro.size(), end - pos - kMacro.size());
                while (!n.empty() && isspace((unsigned char)n.front())) n.erase(n.begin());
                while (!n.empty() && isspace((unsigned char)n.back())) n.pop_back();
                if (!n.empty()) scan.names.push_back(n);
            }
            it = scriptScanCache_.insert_or_assign(key, std::move(scan)).first;
        }
        for (const std::string& n : it->second.names) {
            bool seen = false;
            for (const std::string& e : names) seen |= (e == n);
            if (!seen) names.push_back(n);
        }
    }
    std::sort(names.begin(), names.end());
    return names;
}

std::vector<std::string> App::flowVarNames(const std::string& nodeType) const {
    // int / bool / position variables live in separate namespaces
    auto ns = [](const std::string& t) {
        if (t == "SetVarInt" || t == "VarAtLeast" || t == "GetVarIntText") return 0;
        if (t == "SetVarBool" || t == "GetVarBool") return 1;
        if (t == "SetVarPos" || t == "GetVarPos") return 2;
        return -1;
    };
    const int want = ns(nodeType);
    std::vector<std::string> names;
    if (want < 0) return names;
    for (const SceneData& sc : project_.scenes)
        for (const SceneObject& o : sc.objects)
            for (const FlowNode& n : o.flowGraph.nodes) {
                if (ns(n.type) != want || n.str.empty()) continue;
                bool seen = false;
                for (const std::string& e : names) seen |= (e == n.str);
                if (!seen) names.push_back(n.str);
            }
    return names;
}

void App::drawFlowGraphWindow() {
    flowGraphFocused_ = false;  // recomputed below; gates the global Ctrl+C/V
    ImGui::Begin("Flow Graph");
    if (!hasProject_) {
        ImGui::TextDisabled("No project open.");
        ImGui::End();
        return;
    }
    if (project_.objects().empty()) {
        ImGui::TextDisabled("Add an object first - every flow graph belongs to an object.");
        ImGui::End();
        return;
    }

    // --- which object's graph is being edited -------------------------------
    if (flowGraphObject_ < 0 || flowGraphObject_ >= (int)project_.objects().size()) {
        flowGraphObject_ =
            (selectedObject_ >= 0 && selectedObject_ < (int)project_.objects().size())
                ? selectedObject_
                : 0;
        flowPositionsApplied_ = false;
    }

    auto graphLabel = [&](int i) {
        // objects with a non-empty graph are marked with *
        return project_.objects()[i].name +
               (project_.objects()[i].flowGraph.empty() ? "" : " *");
    };
    ImGui::SetNextItemWidth(220.0f);
    if (ImGui::BeginCombo("Graph of", graphLabel(flowGraphObject_).c_str())) {
        for (int i = 0; i < (int)project_.objects().size(); ++i) {
            const std::string lbl = graphLabel(i) + "##fgobj" + std::to_string(i);
            if (ImGui::Selectable(lbl.c_str(), flowGraphObject_ == i) &&
                flowGraphObject_ != i) {
                flowGraphObject_ = i;
                flowPositionsApplied_ = false;
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Selected object") && selectedObject_ >= 0 &&
        selectedObject_ < (int)project_.objects().size() &&
        selectedObject_ != flowGraphObject_) {
        flowGraphObject_ = selectedObject_;
        flowPositionsApplied_ = false;
    }

    // Project-defined custom nodes: reload the flow-nodes/ folder, scaffold a
    // starter file, or open the project in VS Code (jumping to the C++ bodies
    // or a specific node file). See docs/custom-flow-nodes.md.
    ImGui::SameLine();
    if (ImGui::SmallButton("Custom nodes...")) ImGui::OpenPopup("##customnodes");
    if (ImGui::BeginPopup("##customnodes")) {
        ImGui::TextDisabled("%d loaded from flow-nodes/", (int)customFlowNodes().size());
        ImGui::Separator();
        if (ImGui::MenuItem("Reload from folder")) {
            const std::string msg = flownode::loadForProject(project_.dir);
            statusMessage_ =
                msg.empty() ? "No custom nodes found in flow-nodes/" : msg;
        }
        if (ImGui::MenuItem("New starter node (example.flownode)")) {
            const std::string path = flownode::writeExample(project_.dir);
            if (path.rfind("error:", 0) == 0) {
                statusMessage_ = path;
            } else {
                flownode::loadForProject(project_.dir);
                statusMessage_ = "Wrote " + path + " - edit it, then Reload";
            }
        }
        ImGui::Separator();
        // Open in VS Code, in the whole-project context. The C++ bodies file is
        // the natural landing spot for `call = fn` nodes; the submenu jumps to
        // an individual .flownode definition.
        if (ImGui::MenuItem("Open in VS Code (flow_nodes.hpp)"))
            openInVSCode("inc\\scripts\\flow_nodes.hpp");
        if (ImGui::BeginMenu("Jump to node file", !customFlowNodes().empty())) {
            for (const auto& c : customFlowNodes())
                if (ImGui::MenuItem(c->title.c_str())) openInVSCode(c->sourceFile);
            ImGui::EndMenu();
        }
        ImGui::Separator();
        // Syntax highlighting + validation for .flownode/.screenfx files. Opening
        // a project already installs it; this is the manual/refresh entry point.
        if (ImGui::MenuItem("Install VS Code extension"))
            statusMessage_ = installVsCodeExtension();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(
                "Install the Tyra extension (.flownode/.screenfx highlighting)\n"
                "into VS Code. Reload the VS Code window afterwards.");
        ImGui::EndPopup();
    }

    SceneObject& owner = project_.objects()[flowGraphObject_];
    FlowGraph& fg = owner.flowGraph;
    bool changed = false;

    // Drop links whose pins no longer exist (nodes deleted from the registry
    // or outputs removed in newer editor versions) - imnodes must never be
    // handed a link to a pin that was not submitted.
    {
        auto typeOf = [&](int nodeId) -> const FlowNodeType* {
            for (const FlowNode& n : fg.nodes)
                if (n.id == nodeId) return flowNodeType(n.type);
            return nullptr;
        };
        for (size_t i = fg.links.size(); i-- > 0;) {
            const FlowLink& l = fg.links[i];
            const FlowNodeType* from = typeOf(l.fromNode);
            const FlowNodeType* to = typeOf(l.toNode);
            bool ok = from && to;
            if (ok) {
                switch (l.kind) {
                    case FlowLinkExec:
                        ok = (from->trigger || from->execThrough) && !to->trigger &&
                             !to->pure;
                        break;
                    case FlowLinkObject: ok = from->idOut && to->idIn; break;
                    case FlowLinkPos: ok = from->posOut && to->posIn; break;
                    case FlowLinkBool: ok = from->boolOut && to->boolIn; break;
                    case FlowLinkText: ok = from->textOut && to->textIn; break;
                    default: ok = false; break;
                }
            }
            if (!ok) {
                fg.links.erase(fg.links.begin() + i);
                changed = true;
            }
        }
    }

    // Which object a node's target resolves to in the editor, mirroring the
    // codegen order: incoming object link chain > explicit name > the graph
    // owner ("self"). Used by the Play Animation clip picker.
    auto uiResolveTarget = [&](const FlowNode& start) -> int {
        const FlowNode* cur = &start;
        std::vector<int> visited;
        for (;;) {
            bool seen = false;
            for (int id : visited) seen |= (id == cur->id);
            if (seen) break;  // cycle guard
            visited.push_back(cur->id);
            const FlowNodeType* ct = flowNodeType(cur->type);
            if (!ct || !ct->idIn) break;
            const FlowNode* src = nullptr;
            for (const FlowLink& l : fg.links) {
                if (l.kind != FlowLinkObject || l.toNode != cur->id) continue;
                for (const FlowNode& m : fg.nodes)
                    if (m.id == l.fromNode) src = &m;
                break;
            }
            if (!src) break;
            cur = src;
        }
        const FlowNodeType* ct = flowNodeType(cur->type);
        if (ct && ct->strKind == FlowParamKind::ObjectName && !cur->str.empty()) {
            for (int i = 0; i < (int)project_.objects().size(); ++i)
                if (project_.objects()[i].name == cur->str) return i;
            return -1;
        }
        return flowGraphObject_;  // self
    };

    ImGui::TextDisabled(
        "Right-click: add node. Mouse wheel: zoom (%.0f%%). Round pins: execution, "
        "square pins: object id. Empty object param = self (%s).",
        flowZoom_ * 100.0f, owner.name.c_str());

    // imnodes has no native zoom: emulate it by scaling the font, the style
    // metrics and the grid-space node positions by flowZoom_. The ImGui
    // spacing vars scale too, so node layouts shrink uniformly instead of
    // drifting apart at low zoom.
    const float zoom = flowZoom_;
    const ImVec2 canvasOrigin = ImGui::GetCursorScreenPos();
    ImGui::SetWindowFontScale(zoom);
    const ImGuiStyle& gstyle = ImGui::GetStyle();
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,
                        ImVec2(gstyle.ItemSpacing.x * zoom, gstyle.ItemSpacing.y * zoom));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemInnerSpacing,
                        ImVec2(gstyle.ItemInnerSpacing.x * zoom,
                               gstyle.ItemInnerSpacing.y * zoom));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
                        ImVec2(gstyle.FramePadding.x * zoom, gstyle.FramePadding.y * zoom));
    ImNodesStyle& nstyle = ImNodes::GetStyle();
    const ImNodesStyle savedStyle = nstyle;
    nstyle.GridSpacing *= zoom;
    nstyle.NodeCornerRounding *= zoom;
    nstyle.NodePadding = ImVec2(savedStyle.NodePadding.x * zoom,
                                savedStyle.NodePadding.y * zoom);
    nstyle.NodeBorderThickness *= zoom;
    nstyle.LinkThickness *= zoom;
    nstyle.PinCircleRadius *= zoom;
    nstyle.PinQuadSideLength *= zoom;
    nstyle.PinHoverRadius *= zoom;
    nstyle.PinOffset *= zoom;

    // Node content width (params + right-aligned pin labels share it)
    const float nodeWidth =
        130.0f * zoom + ImGui::GetStyle().ItemInnerSpacing.x +
        ImGui::CalcTextSize("Object").x;
    // Right-aligns a pin label to the node edge. The ">" arrow is drawn as
    // its own item in a FIXED column (same x for every row) instead of being
    // part of a width-dependent indented string: text rendering truncates
    // the pen start to whole pixels, so a per-label fractional indent made
    // the arrows land on different pixels (visibly ragged at some DPI/zoom
    // combinations). A constant arrow column cannot drift by construction.
    auto rightLabel = [&](const char* txt, bool disabled) {
        const float left = ImGui::GetCursorPosX();
        const float arrowX = left + nodeWidth - ImGui::CalcTextSize(">").x;
        const float textX =
            arrowX - ImGui::CalcTextSize(" ").x - ImGui::CalcTextSize(txt).x;
        if (textX > left) ImGui::SetCursorPosX(textX);
        if (disabled)
            ImGui::TextDisabled("%s", txt);
        else
            ImGui::TextUnformatted(txt);
        ImGui::SameLine(0.0f, 0.0f);
        ImGui::SetCursorPosX(arrowX);
        if (disabled)
            ImGui::TextDisabled(">");
        else
            ImGui::TextUnformatted(">");
    };

    ImNodes::BeginNodeEditor();

    // Push stored node positions into imnodes whenever the edited graph or
    // the zoom changes (node ids repeat across graphs; positions are stored
    // unzoomed and scaled on the way in / divided on the way out)
    if (!flowPositionsApplied_) {
        flowPositionsApplied_ = true;
        for (const FlowNode& n : fg.nodes)
            ImNodes::SetNodeGridSpacePos(n.id, ImVec2(n.pos[0] * zoom, n.pos[1] * zoom));
    }

    for (FlowNode& n : fg.nodes) {
        const FlowNodeType* t = flowNodeType(n.type);
        if (!t) continue;

        if (t->pure && t->boolIn)  // logic gate
            ImNodes::PushColorStyle(ImNodesCol_TitleBar, IM_COL32(110, 70, 150, 255));
        else if (t->trigger)
            ImNodes::PushColorStyle(ImNodesCol_TitleBar, IM_COL32(40, 110, 60, 255));
        else
            ImNodes::PushColorStyle(ImNodesCol_TitleBar, IM_COL32(60, 80, 140, 255));

        ImNodes::BeginNode(n.id);
        ImNodes::BeginNodeTitleBar();
        ImGui::TextUnformatted(t->title);
        ImNodes::EndNodeTitleBar();

        ImGui::PushID(n.id);
        ImGui::PushItemWidth(130.0f * zoom);

        bool posLinked = false;
        for (const FlowLink& l : fg.links)
            posLinked |= (l.kind == FlowLinkPos && l.toNode == n.id);

        // string param
        if (t->strKind == FlowParamKind::ObjectName) {
            bool idLinked = false;
            for (const FlowLink& l : fg.links)
                idLinked |= (l.kind == FlowLinkObject && l.toNode == n.id);
            if (idLinked) {
                ImGui::TextDisabled("Object: from id link");
            } else {
                const char* current = n.str.empty() ? "(self)" : n.str.c_str();
                if (ImGui::BeginCombo("Object", current)) {
                    if (ImGui::Selectable("(self)", n.str.empty())) {
                        n.str.clear();
                        changed = true;
                    }
                    for (const SceneObject& o : project_.objects()) {
                        if (ImGui::Selectable(o.name.c_str(), o.name == n.str)) {
                            n.str = o.name;
                            changed = true;
                        }
                    }
                    ImGui::EndCombo();
                }
                if (ImGui::SmallButton("From selected") && selectedObject_ >= 0 &&
                    selectedObject_ < (int)project_.objects().size()) {
                    n.str = project_.objects()[selectedObject_].name;
                    changed = true;
                }
            }
        } else if (t->strKind == FlowParamKind::Button) {
            // every PadButtons field (pad.hpp) - the codegen uses the name as-is
            const char* buttons[] = {"Cross",    "Circle",   "Square", "Triangle",
                                     "DpadUp",   "DpadDown", "DpadLeft",
                                     "DpadRight", "L1",      "L2",     "L3",
                                     "R1",       "R2",       "R3",     "Start",
                                     "Select"};
            if (ImGui::BeginCombo("Button", n.str.empty() ? "Cross" : n.str.c_str())) {
                for (const char* b : buttons) {
                    if (ImGui::Selectable(b, n.str == b)) {
                        n.str = b;
                        changed = true;
                    }
                }
                ImGui::EndCombo();
            }
        } else if (n.type == "PlayAnimation") {
            // Clip picker when the resolved target is an animated .glb model
            // (explicit object wired/named, or self); free text otherwise.
            const int target = uiResolveTarget(n);
            bool picker = false;
            if (target >= 0 && target < (int)project_.objects().size() &&
                isAnimatedModelPath(project_.objects()[target].modelPath)) {
                const GlbInfo& info = glbInfo(project_.objects()[target].modelPath);
                if (info.ok && !info.clips.empty()) {
                    picker = true;
                    const std::string label =
                        n.str.empty() ? info.clips.front() + " (first)" : n.str;
                    if (ImGui::BeginCombo("Clip", label.c_str())) {
                        for (const std::string& c : info.clips) {
                            const bool selected =
                                c == n.str ||
                                (n.str.empty() && c == info.clips.front());
                            if (ImGui::Selectable(c.c_str(), selected) &&
                                n.str != c) {
                                n.str = c;
                                changed = true;
                            }
                        }
                        ImGui::EndCombo();
                    }
                }
            }
            if (!picker) {
                char buf[128];
                std::snprintf(buf, sizeof(buf), "%s", n.str.c_str());
                if (ImGui::InputText("Clip", buf, sizeof(buf))) n.str = buf;
                changed |= ImGui::IsItemDeactivatedAfterEdit();
                ImGui::TextDisabled("Target is not an animated\n.glb - type the clip name.");
            }
        } else if (t->strKind == FlowParamKind::Text) {
            // Patrol Waypoints repurposes the text param as the waypoint
            // name prefix (the target NPC comes from the object link / self).
            const bool patrol = n.type == "PatrolWaypoints";
            char buf[128];
            std::snprintf(buf, sizeof(buf), "%s", n.str.c_str());
            if (ImGui::InputText(patrol ? "Prefix" : "Text", buf, sizeof(buf)))
                n.str = buf;
            changed |= ImGui::IsItemDeactivatedAfterEdit();
            if (patrol) {
                int count = 0;
                if (!n.str.empty())
                    for (const SceneObject& o : project_.objects())
                        if (o.name.rfind(n.str, 0) == 0) ++count;
                ImGui::TextDisabled(
                    "Route: objects named %s1, %s2, ...\n(%d match%s in this "
                    "scene)",
                    n.str.c_str(), n.str.c_str(), count, count == 1 ? "" : "es");
            }
        } else if (t->strKind == FlowParamKind::MusicTrack) {
            const std::string current =
                n.str.empty() ? "<none>"
                              : std::filesystem::path(n.str).filename().string();
            if (ImGui::BeginCombo("Track", current.c_str())) {
                for (const std::string& m : project_.music) {
                    const std::string name = std::filesystem::path(m).filename().string();
                    if (ImGui::Selectable(name.c_str(), m == n.str)) {
                        n.str = m;
                        changed = true;
                    }
                }
                if (project_.music.empty())
                    ImGui::TextDisabled("Import tracks in the\nProject panel (Music).");
                ImGui::EndCombo();
            }
        } else if (t->strKind == FlowParamKind::SoundTrack) {
            const std::string current =
                n.str.empty() ? "<none>"
                              : std::filesystem::path(n.str).filename().string();
            if (ImGui::BeginCombo("Sound", current.c_str())) {
                for (const std::string& s : project_.sounds) {
                    const std::string name = std::filesystem::path(s).filename().string();
                    if (ImGui::Selectable(name.c_str(), s == n.str)) {
                        n.str = s;
                        changed = true;
                    }
                }
                if (project_.sounds.empty())
                    ImGui::TextDisabled("Import sounds in the\nProject panel (Sounds).");
                ImGui::EndCombo();
            }
        } else if (t->strKind == FlowParamKind::SceneName) {
            if (ImGui::BeginCombo("Scene", n.str.empty() ? "<none>" : n.str.c_str())) {
                for (const SceneData& s : project_.scenes) {
                    if (ImGui::Selectable(s.name.c_str(), s.name == n.str)) {
                        n.str = s.name;
                        changed = true;
                    }
                }
                ImGui::EndCombo();
            }
        } else if (t->strKind == FlowParamKind::LayerName) {
            if (ImGui::BeginCombo("Layer", n.str.empty() ? "<none>" : n.str.c_str())) {
                for (const SceneLayer& l : project_.active().layers) {
                    if (ImGui::Selectable(l.name.c_str(), l.name == n.str)) {
                        n.str = l.name;
                        changed = true;
                    }
                }
                if (project_.active().layers.empty())
                    ImGui::TextDisabled("Add layers in the\nProject panel (Layers).");
                ImGui::EndCombo();
            }
        } else if (t->strKind == FlowParamKind::SaveValue) {
            if (ImGui::BeginCombo("Value", n.str.empty() ? "<none>" : n.str.c_str())) {
                for (const SaveValue& v : project_.saveValues) {
                    if (ImGui::Selectable(v.name.c_str(), v.name == n.str)) {
                        n.str = v.name;
                        changed = true;
                    }
                }
                if (project_.saveValues.empty())
                    ImGui::TextDisabled("Add values in the\nProject panel (Save data).");
                ImGui::EndCombo();
            }
        } else if (t->strKind == FlowParamKind::SaveText) {
            if (ImGui::BeginCombo("Value", n.str.empty() ? "<none>" : n.str.c_str())) {
                for (const SaveTextValue& v : project_.saveTexts) {
                    if (ImGui::Selectable(v.name.c_str(), v.name == n.str)) {
                        n.str = v.name;
                        changed = true;
                    }
                }
                if (project_.saveTexts.empty())
                    ImGui::TextDisabled("Add text values in the\nProject panel (Save data).");
                ImGui::EndCombo();
            }
            if (n.type == "SetSaveText") {
                bool textLinked = false;
                for (const FlowLink& l : fg.links)
                    textLinked |= (l.kind == FlowLinkText && l.toNode == n.id);
                if (textLinked) {
                    ImGui::TextDisabled("Text: from link");
                } else {
                    char buf[64];
                    std::snprintf(buf, sizeof(buf), "%s", n.str2.c_str());
                    if (ImGui::InputText("Text", buf, sizeof(buf))) n.str2 = buf;
                    changed |= ImGui::IsItemDeactivatedAfterEdit();
                }
            }
        } else if (t->strKind == FlowParamKind::GradingName) {
            if (ImGui::BeginCombo("Preset", n.str.empty() ? "<none>" : n.str.c_str())) {
                if (ImGui::Selectable("<none>", n.str.empty())) {
                    n.str.clear();
                    changed = true;
                }
                for (const ColorGradingPreset& g : project_.gradings) {
                    if (ImGui::Selectable(g.name.c_str(), g.name == n.str)) {
                        n.str = g.name;
                        changed = true;
                    }
                }
                if (project_.gradings.empty())
                    ImGui::TextDisabled("Add presets in\nTools > Color Grading.");
                ImGui::EndCombo();
            }
        } else if (t->strKind == FlowParamKind::AmbienceName) {
            if (ImGui::BeginCombo("Preset", n.str.empty() ? "<none>" : n.str.c_str())) {
                if (ImGui::Selectable("<none>", n.str.empty())) {
                    n.str.clear();
                    changed = true;
                }
                for (const AmbiencePreset& a : project_.ambiencePresets) {
                    if (ImGui::Selectable(a.name.c_str(), a.name == n.str)) {
                        n.str = a.name;
                        changed = true;
                    }
                }
                if (project_.ambiencePresets.empty())
                    ImGui::TextDisabled("Add presets in\nTools > Ambience Editor.");
                ImGui::EndCombo();
            }
        } else if (t->strKind == FlowParamKind::SequenceName) {
            if (ImGui::BeginCombo("Sequence", n.str.empty() ? "<none>" : n.str.c_str())) {
                if (ImGui::Selectable("<none>", n.str.empty())) {
                    n.str.clear();
                    changed = true;
                }
                for (const Sequence& s : project_.sequences) {
                    if (ImGui::Selectable(s.name.c_str(), s.name == n.str)) {
                        n.str = s.name;
                        changed = true;
                    }
                }
                if (project_.sequences.empty())
                    ImGui::TextDisabled("Add sequences in\nTools > Cutscene Director.");
                ImGui::EndCombo();
            }
        } else if (t->strKind == FlowParamKind::MenuName) {
            if (ImGui::BeginCombo("Menu", n.str.empty() ? "<none>" : n.str.c_str())) {
                for (const GameMenu& gm : project_.menus) {
                    if (ImGui::Selectable(gm.name.c_str(), gm.name == n.str)) {
                        n.str = gm.name;
                        changed = true;
                    }
                }
                if (project_.menus.empty())
                    ImGui::TextDisabled("Add menus in the\nProject panel (Menus).");
                ImGui::EndCombo();
            }
        } else if (t->strKind == FlowParamKind::HudTextName) {
            if (ImGui::BeginCombo("Text", n.str.empty() ? "<none>" : n.str.c_str())) {
                for (const HudText& ht : project_.hudTexts) {
                    if (ImGui::Selectable(ht.name.c_str(), ht.name == n.str)) {
                        n.str = ht.name;
                        changed = true;
                    }
                }
                if (project_.hudTexts.empty())
                    ImGui::TextDisabled("Add texts in\nTools > UI Editor (Texts).");
                ImGui::EndCombo();
            }
        } else if (t->strKind == FlowParamKind::VarName) {
            char buf[64];
            std::snprintf(buf, sizeof(buf), "%s", n.str.c_str());
            if (ImGui::InputText("Variable", buf, sizeof(buf))) n.str = buf;
            changed |= ImGui::IsItemDeactivatedAfterEdit();
            // Same-type variable names already used anywhere in the project
            // (variables are game-global; picking beats retyping/typos).
            const std::vector<std::string> known = flowVarNames(n.type);
            if (!known.empty()) {
                if (ImGui::SmallButton("Pick...")) ImGui::OpenPopup("##pickvar");
                if (ImGui::BeginPopup("##pickvar")) {
                    for (const std::string& name : known) {
                        if (ImGui::Selectable(name.c_str(), name == n.str)) {
                            n.str = name;
                            changed = true;
                        }
                    }
                    ImGui::EndPopup();
                }
            }
        }

        if (n.type == "Self") ImGui::TextDisabled("(%s)", owner.name.c_str());

        // numeric params (own ID scope - a num label may repeat the string
        // param's label, e.g. Set Save Value's combo and drag are both "Value")
        ImGui::PushID("params");
        // X/Y/Z come from the position link; params past them (Speed) stay.
        // SetDof draws its own params (mode combo) below.
        int firstNum = 0;
        if (posLinked && t->posIn && t->numCount >= 3 && n.type != "SetDof") {
            ImGui::TextDisabled("Position: from link");
            firstNum = 3;
        }
        if (n.type == "SetVarBool" || n.type == "SetFlashlight" ||
            n.type == "SetFog" || n.type == "SetParticles" ||
            n.type == "SetWidescreen") {
            bool v = n.num[0] != 0.0f;
            if (ImGui::Checkbox(t->numLabels[0], &v)) {
                n.num[0] = v ? 1.0f : 0.0f;
                changed = true;
            }
        } else if (n.type == "SetDof") {
            const char* modes[] = {"Set custom", "Off", "Scene setting"};
            int mode = (int)n.num[3];
            mode = mode < 0 ? 0 : mode > 2 ? 2 : mode;
            if (ImGui::Combo("Mode", &mode, modes, 3)) {
                n.num[3] = (float)mode;
                changed = true;
            }
            if (mode == 0) {
                if (posLinked) {
                    // the link replaces Focus with the distance to the point
                    ImGui::TextDisabled("Focus: distance to linked point");
                } else {
                    ImGui::DragFloat("Focus", &n.num[0], 0.5f, 0.5f, 500.0f,
                                     "%.1f");
                    changed |= ImGui::IsItemDeactivatedAfterEdit();
                }
                ImGui::DragFloat("Range", &n.num[1], 0.5f, 0.1f, 500.0f,
                                 "%.1f");
                changed |= ImGui::IsItemDeactivatedAfterEdit();
                ImGui::SliderFloat("Amount", &n.num[2], 0.0f, 1.0f, "%.2f");
                changed |= ImGui::IsItemDeactivatedAfterEdit();
            } else if (mode == 1) {
                ImGui::TextDisabled("Turns depth of field off.");
            } else {
                ImGui::TextDisabled(
                    "Restores the scene's authored values\n"
                    "(Tools > UI Editor > Depth of field).");
            }
        } else if (n.type == "SetDisplayMode") {
            const char* modes[] = {"Interlaced (480i/576i)",
                                   "Progressive (480p)", "1080i"};
            int mode = (int)n.num[0];
            mode = mode < 0 ? 0 : mode > 2 ? 2 : mode;
            if (ImGui::Combo("Mode", &mode, modes, 3)) {
                n.num[0] = (float)mode;
                changed = true;
            }
            ImGui::DragFloat("Confirm s", &n.num[1], 0.5f, 0.0f, 60.0f, "%.0f");
            changed |= ImGui::IsItemDeactivatedAfterEdit();
            ImGui::TextDisabled(
                "Confirm > 0: the game asks to keep the\n"
                "mode (X = yes) and reverts automatically\n"
                "when the timer runs out. 0 = switch blind.");
        } else if (n.type == "SetStickCurve") {
            const char* sticks[] = {"Left (move)", "Right (camera)", "Both"};
            int stick = (int)n.num[0];
            stick = stick < 0 ? 0 : stick > 2 ? 2 : stick;
            if (ImGui::Combo("Stick", &stick, sticks, 3)) {
                n.num[0] = (float)stick;
                changed = true;
            }
            const char* curves[] = {"Linear", "Exponential", "S-Curve"};
            int curve = (int)n.num[1];
            curve = curve < 0 ? 0 : curve > 2 ? 2 : curve;
            if (ImGui::Combo("Curve", &curve, curves, 3)) {
                n.num[1] = (float)curve;
                changed = true;
            }
            if (curve != 0) {  // exponent only shapes Exponential / S-Curve
                ImGui::DragFloat("Exponent", &n.num[2], 0.05f, 1.0f, 6.0f, "%.2f");
                changed |= ImGui::IsItemDeactivatedAfterEdit();
            }
        } else if (t->numKind == FlowParamKind::Color) {
            ImGui::ColorEdit3("Color", n.num, ImGuiColorEditFlags_NoInputs);
            changed |= ImGui::IsItemDeactivatedAfterEdit();
        } else {
            for (int a = firstNum; a < t->numCount; ++a) {
                const bool isLoop = std::strcmp(t->numLabels[a], "Loop") == 0 ||
                                    std::strcmp(t->numLabels[a], "Once") == 0 ||
                                    std::strcmp(t->numLabels[a], "LOS") == 0;
                const bool isVolume = std::strcmp(t->numLabels[a], "Volume") == 0;
                const bool isChannel = std::strcmp(t->numLabels[a], "Channel") == 0;
                if (isChannel) {
                    // SPU channel 0-23; -1 = rotate through channels
                    ImGui::SliderFloat("Channel", &n.num[a], -1.0f, 23.0f,
                                       n.num[a] < 0.0f ? "auto" : "%.0f");
                    n.num[a] = (float)(int)n.num[a];
                    changed |= ImGui::IsItemDeactivatedAfterEdit();
                } else if (isLoop) {
                    bool loop = n.num[a] != 0.0f;
                    if (ImGui::Checkbox(t->numLabels[a], &loop)) {
                        n.num[a] = loop ? 1.0f : 0.0f;
                        changed = true;
                    }
                } else if (isVolume) {
                    ImGui::SliderFloat("Volume", &n.num[a], 0.0f, 100.0f, "%.0f");
                    changed |= ImGui::IsItemDeactivatedAfterEdit();
                } else {
                    ImGui::DragFloat(t->numLabels[a], &n.num[a], 0.1f);
                    changed |= ImGui::IsItemDeactivatedAfterEdit();
                }
            }
        }
        ImGui::PopID();  // "params"
        if (n.type == "ValueAtLeast" || n.type == "VarAtLeast")
            ImGui::TextDisabled("Checked every frame - wire the\nbool into On Condition or a gate.");
        if (n.type == "Raycast")
            ImGui::TextDisabled("Casts from the player's eye along\nthe view direction on exec.");
        if (n.type == "ChasePlayer" || n.type == "FleePlayer" ||
            n.type == "PatrolWaypoints")
            ImGui::TextDisabled(
                "Walks the baked nav grid (View >\nNav Mesh Overlay). One AI "
                "state per\nobject - a new command replaces it.");
        if (n.type == "OnPlayerSeen")
            ImGui::TextDisabled(
                "Vision cone around the NPC's facing.\nLOS: terrain blocks "
                "sight (hills).\nBool output = seen right now.");
        ImGui::PopItemWidth();
        ImGui::PopID();

        // pins: exec flow (round) + object id (square, amber) + position
        // (triangle, green) + bool value (circle, violet) + text (circle,
        // cyan). Pure data nodes have no exec pins; bool-in and text-in pins
        // accept several links (folded / concatenated).
        const unsigned idPinCol = IM_COL32(222, 170, 60, 255);
        const unsigned posPinCol = IM_COL32(110, 200, 120, 255);
        const unsigned boolPinCol = IM_COL32(180, 120, 220, 255);
        const unsigned textPinCol = IM_COL32(90, 190, 210, 255);
        if (t->idIn) {
            ImNodes::PushColorStyle(ImNodesCol_Pin, idPinCol);
            ImNodes::BeginInputAttribute(flowIdInPin(n.id), ImNodesPinShape_QuadFilled);
            ImGui::TextDisabled("object");
            ImNodes::EndInputAttribute();
            ImNodes::PopColorStyle();
        }
        if (t->posIn) {
            ImNodes::PushColorStyle(ImNodesCol_Pin, posPinCol);
            ImNodes::BeginInputAttribute(flowPosInPin(n.id),
                                         ImNodesPinShape_TriangleFilled);
            ImGui::TextDisabled("position");
            ImNodes::EndInputAttribute();
            ImNodes::PopColorStyle();
        }
        if (t->boolIn) {
            ImNodes::PushColorStyle(ImNodesCol_Pin, boolPinCol);
            ImNodes::BeginInputAttribute(flowBoolInPin(n.id), ImNodesPinShape_CircleFilled);
            ImGui::TextDisabled("bool");
            ImNodes::EndInputAttribute();
            ImNodes::PopColorStyle();
        }
        if (t->textIn) {
            ImNodes::PushColorStyle(ImNodesCol_Pin, textPinCol);
            ImNodes::BeginInputAttribute(flowTextInPin(n.id), ImNodesPinShape_CircleFilled);
            ImGui::TextDisabled("text");
            ImNodes::EndInputAttribute();
            ImNodes::PopColorStyle();
        }
        if (!t->pure) {
            if (t->trigger) {
                ImNodes::BeginOutputAttribute(flowOutPin(n.id));
                rightLabel("then", false);
                ImNodes::EndOutputAttribute();
            } else {
                ImNodes::BeginInputAttribute(flowInPin(n.id));
                ImGui::TextUnformatted("> do");
                ImNodes::EndInputAttribute();
                if (t->execThrough) {
                    // action that fires its own exec pulse later (Delay)
                    ImNodes::BeginOutputAttribute(flowOutPin(n.id));
                    rightLabel("after", false);
                    ImNodes::EndOutputAttribute();
                }
            }
        }
        if (t->idOut) {
            ImNodes::PushColorStyle(ImNodesCol_Pin, idPinCol);
            ImNodes::BeginOutputAttribute(flowIdOutPin(n.id), ImNodesPinShape_QuadFilled);
            rightLabel("object", true);
            ImNodes::EndOutputAttribute();
            ImNodes::PopColorStyle();
        }
        if (t->posOut) {
            ImNodes::PushColorStyle(ImNodesCol_Pin, posPinCol);
            ImNodes::BeginOutputAttribute(flowPosOutPin(n.id),
                                          ImNodesPinShape_TriangleFilled);
            rightLabel("position", true);
            ImNodes::EndOutputAttribute();
            ImNodes::PopColorStyle();
        }
        if (t->boolOut) {
            ImNodes::PushColorStyle(ImNodesCol_Pin, boolPinCol);
            ImNodes::BeginOutputAttribute(flowBoolOutPin(n.id),
                                          ImNodesPinShape_CircleFilled);
            rightLabel("bool", true);
            ImNodes::EndOutputAttribute();
            ImNodes::PopColorStyle();
        }
        if (t->textOut) {
            ImNodes::PushColorStyle(ImNodesCol_Pin, textPinCol);
            ImNodes::BeginOutputAttribute(flowTextOutPin(n.id),
                                          ImNodesPinShape_CircleFilled);
            rightLabel("text", true);
            ImNodes::EndOutputAttribute();
            ImNodes::PopColorStyle();
        }

        ImNodes::EndNode();
        ImNodes::PopColorStyle();
    }

    for (const FlowLink& l : fg.links) {
        if (l.kind == FlowLinkObject) {
            // object links amber, position links green, exec the default
            ImNodes::PushColorStyle(ImNodesCol_Link, IM_COL32(222, 170, 60, 255));
            ImNodes::Link(l.id, flowIdOutPin(l.fromNode), flowIdInPin(l.toNode));
            ImNodes::PopColorStyle();
        } else if (l.kind == FlowLinkPos) {
            ImNodes::PushColorStyle(ImNodesCol_Link, IM_COL32(110, 200, 120, 255));
            ImNodes::Link(l.id, flowPosOutPin(l.fromNode), flowPosInPin(l.toNode));
            ImNodes::PopColorStyle();
        } else if (l.kind == FlowLinkBool) {
            ImNodes::PushColorStyle(ImNodesCol_Link, IM_COL32(180, 120, 220, 255));
            ImNodes::Link(l.id, flowBoolOutPin(l.fromNode), flowBoolInPin(l.toNode));
            ImNodes::PopColorStyle();
        } else if (l.kind == FlowLinkText) {
            ImNodes::PushColorStyle(ImNodesCol_Link, IM_COL32(90, 190, 210, 255));
            ImNodes::Link(l.id, flowTextOutPin(l.fromNode), flowTextInPin(l.toNode));
            ImNodes::PopColorStyle();
        } else {
            ImNodes::Link(l.id, flowOutPin(l.fromNode), flowInPin(l.toNode));
        }
    }

    ImNodes::MiniMap(0.15f, ImNodesMiniMapLocation_BottomRight);
    const bool editorHovered = ImNodes::IsEditorHovered();
    ImNodes::EndNodeEditor();

    nstyle = savedStyle;
    ImGui::PopStyleVar(3);
    ImGui::SetWindowFontScale(1.0f);

    // Read node positions back in unzoomed model coordinates (imnodes owns
    // the zoomed ones while dragging)
    for (FlowNode& n : fg.nodes) {
        const ImVec2 pos = ImNodes::GetNodeGridSpacePos(n.id);
        n.pos[0] = pos.x / zoom;
        n.pos[1] = pos.y / zoom;
    }

    // Mouse wheel over the canvas: zoom, keeping the point under the cursor
    // fixed (the panning is adjusted for the new scale).
    if (editorHovered && ImGui::GetIO().MouseWheel != 0.0f) {
        float next = flowZoom_ * ImPow(1.1f, ImGui::GetIO().MouseWheel);
        if (next < 0.4f) next = 0.4f;
        if (next > 1.8f) next = 1.8f;
        if (next != flowZoom_) {
            const float ratio = next / flowZoom_;
            ImVec2 pan = ImNodes::EditorContextGetPanning();
            const ImVec2 mouse = ImGui::GetIO().MousePos;
            const float relX = mouse.x - canvasOrigin.x;
            const float relY = mouse.y - canvasOrigin.y;
            pan.x = relX - ratio * (relX - pan.x);
            pan.y = relY - ratio * (relY - pan.y);
            ImNodes::EditorContextResetPanning(pan);
            flowZoom_ = next;
            flowPositionsApplied_ = false;  // re-push positions at the new scale
        }
    }

    // New link dragged between pins. Pin kinds by id (pin % 16): 0 = object
    // in, 1 = exec out, 2 = exec in, 3 = object out, 4 = position in,
    // 5 = position out, 6 = bool in, 7 = bool out, 8 = text in,
    // 9 = text out; node = pin / 16.
    int startPin = 0, endPin = 0;
    if (ImNodes::IsLinkCreated(&startPin, &endPin)) {
        const int a = startPin % 16, b = endPin % 16;
        int outPin = -1, inPin = -1;
        int kind = FlowLinkExec;
        if ((a == 1 && b == 2) || (a == 2 && b == 1)) {
            outPin = a == 1 ? startPin : endPin;
            inPin = a == 2 ? startPin : endPin;
        } else if ((a == 3 && b == 0) || (a == 0 && b == 3)) {
            outPin = a == 3 ? startPin : endPin;
            inPin = a == 0 ? startPin : endPin;
            kind = FlowLinkObject;
        } else if ((a == 5 && b == 4) || (a == 4 && b == 5)) {
            outPin = a == 5 ? startPin : endPin;
            inPin = a == 4 ? startPin : endPin;
            kind = FlowLinkPos;
        } else if ((a == 7 && b == 6) || (a == 6 && b == 7)) {
            outPin = a == 7 ? startPin : endPin;
            inPin = a == 6 ? startPin : endPin;
            kind = FlowLinkBool;
        } else if ((a == 9 && b == 8) || (a == 8 && b == 9)) {
            outPin = a == 9 ? startPin : endPin;
            inPin = a == 8 ? startPin : endPin;
            kind = FlowLinkText;
        }
        if (outPin >= 0 && outPin / 16 != inPin / 16) {
            FlowLink l;
            l.fromNode = outPin / 16;
            l.toNode = inPin / 16;
            l.kind = kind;
            if (kind == FlowLinkObject || kind == FlowLinkPos) {
                // a node takes its object/position from at most one link
                // (bool-in and text-in pins fold over several links - keep them)
                for (size_t i = fg.links.size(); i-- > 0;)
                    if (fg.links[i].kind == kind && fg.links[i].toNode == l.toNode)
                        fg.links.erase(fg.links.begin() + i);
            }
            bool duplicate = false;
            for (const FlowLink& e : fg.links)
                duplicate |= (e.kind == l.kind && e.fromNode == l.fromNode &&
                              e.toNode == l.toNode);
            if (!duplicate) {
                l.id = fg.nextId++;
                fg.links.push_back(l);
                changed = true;
            }
        }
    }

    // Copy/paste nodes. When this window has focus, Ctrl+C/V operate on the
    // graph instead of the scene objects (the global handler stands down while
    // flowGraphFocused_ is set). Skip while typing in a node param so Ctrl+C
    // still copies text there.
    const bool fgFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows);
    flowGraphFocused_ = fgFocused;
    if (fgFocused && !ImGui::GetIO().WantTextInput) {
        if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_C)) {
            const int numSel = ImNodes::NumSelectedNodes();
            if (numSel > 0) {
                std::vector<int> ids(numSel);
                ImNodes::GetSelectedNodes(ids.data());
                flowClipboard_ = FlowGraph{};
                for (int id : ids)
                    for (const FlowNode& n : fg.nodes)
                        if (n.id == id) flowClipboard_.nodes.push_back(n);
                auto copied = [&](int id) {
                    for (const FlowNode& n : flowClipboard_.nodes)
                        if (n.id == id) return true;
                    return false;
                };
                for (const FlowLink& l : fg.links)
                    if (copied(l.fromNode) && copied(l.toNode))
                        flowClipboard_.links.push_back(l);
                statusMessage_ =
                    "Copied " + std::to_string(flowClipboard_.nodes.size()) +
                    (flowClipboard_.nodes.size() == 1 ? " node" : " nodes");
            }
        }
        if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_V) &&
            !flowClipboard_.nodes.empty()) {
            // Fresh ids from the target graph so a paste into the same or a
            // different graph never collides; links are remapped to them.
            std::vector<std::pair<int, int>> idMap;  // old id -> new id
            for (const FlowNode& src : flowClipboard_.nodes) {
                FlowNode n = src;
                n.id = fg.nextId++;
                n.pos[0] += 20.0f;  // offset so the paste sits beside the source
                n.pos[1] += 20.0f;
                idMap.push_back({src.id, n.id});
                fg.nodes.push_back(n);
            }
            auto mapId = [&](int old) {
                for (const auto& p : idMap)
                    if (p.first == old) return p.second;
                return -1;
            };
            for (const FlowLink& src : flowClipboard_.links) {
                FlowLink l = src;
                l.id = fg.nextId++;
                l.fromNode = mapId(src.fromNode);
                l.toNode = mapId(src.toNode);
                fg.links.push_back(l);
            }
            flowPositionsApplied_ = false;  // push the pasted node positions
            changed = true;
            statusMessage_ =
                "Pasted " + std::to_string(flowClipboard_.nodes.size()) +
                (flowClipboard_.nodes.size() == 1 ? " node" : " nodes");
        }
    }

    // Delete selection
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows) &&
        ImGui::IsKeyPressed(ImGuiKey_Delete)) {
        const int numLinks = ImNodes::NumSelectedLinks();
        if (numLinks > 0) {
            std::vector<int> ids(numLinks);
            ImNodes::GetSelectedLinks(ids.data());
            for (int id : ids)
                for (size_t i = 0; i < fg.links.size(); ++i)
                    if (fg.links[i].id == id) {
                        fg.links.erase(fg.links.begin() + i);
                        changed = true;
                        break;
                    }
        }
        const int numNodes = ImNodes::NumSelectedNodes();
        if (numNodes > 0) {
            std::vector<int> ids(numNodes);
            ImNodes::GetSelectedNodes(ids.data());
            for (int id : ids) {
                for (size_t i = 0; i < fg.nodes.size(); ++i)
                    if (fg.nodes[i].id == id) {
                        fg.nodes.erase(fg.nodes.begin() + i);
                        changed = true;
                        break;
                    }
                for (size_t i = fg.links.size(); i-- > 0;)
                    if (fg.links[i].fromNode == id || fg.links[i].toNode == id)
                        fg.links.erase(fg.links.begin() + i);
            }
            ImNodes::ClearNodeSelection();
        }
    }

    // Right-click: add node (categorized)
    if (editorHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
        ImGui::OpenPopup("##flow_add_node");
    if (ImGui::BeginPopup("##flow_add_node")) {
        const ImVec2 clickPos = ImGui::GetMousePosOnOpeningCurrentPopup();
        for (const char* cat : flowNodeCategories()) {
            if (!ImGui::BeginMenu(cat)) continue;
            for (const FlowNodeType* tp : flowAllNodeTypes()) {
                const FlowNodeType& t = *tp;
                if (std::strcmp(t.category, cat) != 0) continue;
                if (ImGui::MenuItem(t.title)) {
                    FlowNode n;
                    n.id = fg.nextId++;
                    n.type = t.key;
                    if (t.strKind == FlowParamKind::Button) n.str = "Cross";
                    if (t.numKind == FlowParamKind::Color)
                        n.num[0] = n.num[1] = n.num[2] = 1.0f;
                    if (std::string(t.key) == "NearObject") n.num[0] = 4.0f;
                    if (std::string(t.key) == "EverySeconds") n.num[0] = 1.0f;
                    if (std::string(t.key) == "Delay") n.num[0] = 1.0f;  // seconds
                    if (std::string(t.key) == "MoveObjectTo") n.num[3] = 2.0f;  // speed
                    if (std::string(t.key) == "PlayAnimation") n.num[1] = 1.0f;  // speed
                    if (std::string(t.key) == "PlayMusic") {
                        n.num[0] = 80.0f;  // volume
                        n.num[1] = 1.0f;   // loop
                        if (!project_.music.empty()) n.str = project_.music.front();
                    }
                    if (std::string(t.key) == "SetMusicVolume") n.num[0] = 80.0f;
                    if (t.strKind == FlowParamKind::LayerName &&
                        !project_.active().layers.empty())
                        n.str = project_.active().layers.front().name;
                    if (std::string(t.key) == "SetVarBool") n.num[0] = 1.0f;
                    if (std::string(t.key) == "SetFlashlight") n.num[0] = 1.0f;
                    if (std::string(t.key) == "Raycast") n.num[0] = 50.0f;  // max dist
                    if (std::string(t.key) == "SetDof") {
                        n.num[0] = 20.0f;  // focus distance
                        n.num[1] = 15.0f;  // range (full blur at focus+range)
                        n.num[2] = 1.0f;   // amount (num[3] mode: 0 = set)
                    }
                    if (std::string(t.key) == "SetStickCurve") n.num[2] = 2.0f;  // exponent
                    if (std::string(t.key) == "PlaySound") {
                        n.num[0] = 100.0f;  // volume
                        n.num[1] = -1.0f;   // channel: auto
                        if (!project_.sounds.empty()) n.str = project_.sounds.front();
                    }
                    fg.nodes.push_back(n);
                    ImNodes::SetNodeScreenSpacePos(n.id, clickPos);
                    changed = true;
                }
            }
            ImGui::EndMenu();
        }
        ImGui::EndPopup();
    }

    if (changed) commitChange();

    ImGui::End();
}

std::string App::installVsCodeExtension() {
    // The extension ships prebuilt as a .vsix next to the exe (dev tree:
    // <exe>/../tools/vscode-tyrax/*.vsix), resolved the same exe-relative way as
    // the generated c_cpp_properties.json. It MUST be installed through the
    // `code` CLI: modern VS Code (>=1.74) loads only what its own manifest cache
    // lists, so an extension folder merely copied into ~/.vscode/extensions is
    // silently ignored - which is why the earlier folder-copy install did
    // nothing and printed nothing. `code --install-extension <vsix>` registers
    // it properly.
    char exePath[MAX_PATH] = {};
    if (GetModuleFileNameA(nullptr, exePath, MAX_PATH) == 0)
        return "Could not locate the editor executable";
    std::error_code ec;
    const std::filesystem::path dir = std::filesystem::weakly_canonical(
        std::filesystem::path(exePath).parent_path() / ".." / "tools" / "vscode-tyrax", ec);
    std::filesystem::path vsix;
    if (std::filesystem::exists(dir, ec))
        for (const auto& e : std::filesystem::directory_iterator(dir, ec))
            if (e.path().extension() == ".vsix") {
                vsix = e.path();
                break;
            }
    if (vsix.empty())
        return "VS Code extension package not found (tools/vscode-tyrax/*.vsix)";

    // `code` is a .cmd shim, so route through cmd.exe (same as openInVSCode).
    // Run it synchronously so we can report the real outcome; --force reinstalls
    // in place, so this is idempotent.
    std::string cmd =
        "cmd.exe /S /C \"code --install-extension \"" + vsix.string() + "\" --force\"";
    STARTUPINFOA si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    std::string mutableCmd = cmd;
    if (!CreateProcessA(nullptr, mutableCmd.data(), nullptr, nullptr, FALSE,
                        CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi))
        return "Could not run VS Code's 'code' CLI - is it on PATH?";
    WaitForSingleObject(pi.hProcess, 60000);
    DWORD exitCode = 1;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    if (exitCode != 0)
        return "VS Code extension install failed - is the 'code' CLI on PATH? "
               "(VS Code: Shell Command: Install 'code' in PATH)";
    return "TyraX VS Code extension installed - reload the VS Code window if it "
           "was already open";
}

void App::openInVSCode(const std::string& file) {
    // Ensure our .flownode/.screenfx extension is installed (once per session;
    // the install runs `code --install-extension`, a couple of seconds). Surface
    // its result so a failure isn't silent the way the old copy-install was.
    if (!vsCodeExtInstallTried_) {
        vsCodeExtInstallTried_ = true;
        vsCodeExtStatus_ = installVsCodeExtension();
    }

    // `code` is a .cmd shim, so it has to go through cmd.exe. Passing the
    // project dir opens (or reuses) that workspace; an extra file path opens it
    // in the same window (-g = goto), so we can jump straight to a script or a
    // custom-node file while keeping the whole project in context.
    std::string cmd = "cmd.exe /S /C \"code \"" + project_.dir + "\"";
    if (!file.empty()) {
        std::filesystem::path fp(file);
        const std::string abs =
            fp.is_absolute() ? fp.string()
                             : (std::filesystem::path(project_.dir) / fp).string();
        cmd += " -g \"" + abs + "\"";
    }
    cmd += "\"";
    STARTUPINFOA si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    std::string mutableCmd = cmd;
    if (CreateProcessA(nullptr, mutableCmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                       nullptr, project_.dir.c_str(), &si, &pi)) {
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        statusMessage_ = "Opening in VS Code...";
        // Append the extension-install outcome so it is visible (a failure here
        // is the difference between highlighting working or not).
        if (!vsCodeExtStatus_.empty()) statusMessage_ += "  [" + vsCodeExtStatus_ + "]";
    } else {
        statusMessage_ = "Could not launch VS Code (is 'code' on PATH?)";
    }
}

const App::HudTexture* App::hudTexture(const std::string& relPath) {
    auto it = hudTexCache_.find(relPath);
    if (it != hudTexCache_.end()) return it->second.tex ? &it->second : nullptr;

    HudTexture entry;
    const std::string full = (std::filesystem::path(project_.dir) / relPath).string();
    int w = 0, h = 0, comp = 0;
    if (unsigned char* pixels = stbi_load(full.c_str(), &w, &h, &comp, 4)) {
        GLuint tex = 0;
        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        stbi_image_free(pixels);
        entry = {tex, w, h};
    }
    hudTexCache_[relPath] = entry;
    return entry.tex ? &hudTexCache_[relPath] : nullptr;
}

// The embedded built-in USE prompt sprite as a GL texture (lazy; process
// lifetime). Shown in the viewport overlay while no custom image overrides it.
const App::HudTexture* App::builtinUseTexture() {
    if (builtinUseTex_.tex) return &builtinUseTex_;
    size_t n = 0;
    const unsigned char* png = templates::usePromptPng(n);
    int w = 0, h = 0, comp = 0;
    unsigned char* pixels =
        stbi_load_from_memory(png, (int)n, &w, &h, &comp, 4);
    if (!pixels) return nullptr;
    glGenTextures(1, &builtinUseTex_.tex);
    glBindTexture(GL_TEXTURE_2D, builtinUseTex_.tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                 pixels);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    stbi_image_free(pixels);
    builtinUseTex_.w = w;
    builtinUseTex_.h = h;
    return &builtinUseTex_;
}

// A HUD text as a GL texture for the viewport overlay, re-baked when its
// content changes (keyed by name; a handful of small textures at most).
const App::HudTexture* App::hudTextTexture(const HudText& t) {
    const std::string key = t.text + "\x1f" + t.fontPath + "\x1f" +
                            std::to_string(t.size) + "\x1f" +
                            std::to_string(t.shadow) + "\x1f" +
                            std::to_string(t.color[0]) + "," +
                            std::to_string(t.color[1]) + "," +
                            std::to_string(t.color[2]);
    TextTexture& entry = textTexCache_[t.name];
    if (entry.tex && entry.key == key) return &entry.hud;
    std::vector<unsigned char> rgba;
    int w = 0, h = 0;
    if (!menubake::bakeTextRGBA(t, project_.dir, rgba, w, h)) return nullptr;
    if (!entry.tex) glGenTextures(1, &entry.tex);
    glBindTexture(GL_TEXTURE_2D, entry.tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                 rgba.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    entry.key = key;
    entry.hud = {entry.tex, w, h};
    return &entry.hud;
}

// Shared TTF picker combo (menus, HUD texts): default chain / fonts imported
// into the project (res/fonts) / a curated set of stock Windows fonts
// (existence-checked) / import a new TTF. Returns true when fontPath changed.
bool App::fontCombo(std::string& fontPath) {
    bool changed = false;
    std::string current = "Default (Consolas Bold)";
    if (!fontPath.empty())
        current = std::filesystem::path(fontPath).filename().string();
    ImGui::SetNextItemWidth(200.0f);
    if (ImGui::BeginCombo("Font", current.c_str())) {
        if (ImGui::Selectable("Default (Consolas Bold)", fontPath.empty())) {
            fontPath.clear();
            changed = true;
        }
        // fonts shipped inside the project (res/fonts)
        const std::filesystem::path fontsDir =
            std::filesystem::path(project_.dir) / "res" / "fonts";
        std::error_code ec;
        if (std::filesystem::exists(fontsDir, ec)) {
            for (const auto& entry :
                 std::filesystem::directory_iterator(fontsDir, ec)) {
                std::string ext = entry.path().extension().string();
                for (char& c : ext) c = (char)tolower((unsigned char)c);
                if (ext != ".ttf" && ext != ".otf") continue;
                const std::string rel =
                    "res/fonts/" + entry.path().filename().string();
                if (ImGui::Selectable(
                        (entry.path().filename().string() + "  [project]").c_str(),
                        fontPath == rel)) {
                    fontPath = rel;
                    changed = true;
                }
            }
        }
        // stock Windows fonts (bare file name - resolved via \Windows\Fonts)
        struct SysFont {
            const char* label;
            const char* file;
        };
        static const SysFont kSysFonts[] = {
            {"Arial Bold", "arialbd.ttf"},
            {"Comic Sans MS Bold", "comicbd.ttf"},
            {"Courier New Bold", "courbd.ttf"},
            {"Georgia Bold", "georgiab.ttf"},
            {"Impact", "impact.ttf"},
            {"Segoe UI Bold", "segoeuib.ttf"},
            {"Times New Roman Bold", "timesbd.ttf"},
            {"Trebuchet MS Bold", "trebucbd.ttf"},
            {"Verdana Bold", "verdanab.ttf"},
        };
        char windir[MAX_PATH] = {};
        GetWindowsDirectoryA(windir, MAX_PATH);
        for (const SysFont& sf : kSysFonts) {
            const std::filesystem::path p =
                std::filesystem::path(windir) / "Fonts" / sf.file;
            if (!std::filesystem::exists(p, ec)) continue;
            if (ImGui::Selectable(sf.label, fontPath == sf.file)) {
                fontPath = sf.file;
                changed = true;
            }
        }
        ImGui::Separator();
        if (ImGui::Selectable("Import TTF into the project...")) {
            const std::string src = pickTtfFile();
            if (!src.empty()) {
                const std::filesystem::path srcPath(src);
                const std::string fileName =
                    sanitizeAssetName(srcPath.filename().string());
                std::filesystem::create_directories(fontsDir, ec);
                std::filesystem::copy_file(
                    srcPath, fontsDir / fileName,
                    std::filesystem::copy_options::overwrite_existing, ec);
                if (!ec) {
                    fontPath = "res/fonts/" + fileName;
                    changed = true;
                }
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Rasterized at build time - any TTF works, nothing\n"
                          "ships to the PS2 but pixels. Project fonts\n"
                          "(res/fonts) travel with the project; Windows fonts\n"
                          "depend on this machine.");
    return changed;
}

int App::importHudImageInto(std::vector<HudImage>& target) {
    const std::string src = pickPngFile();
    if (src.empty()) return -1;

    const std::filesystem::path srcPath(src);
    const std::string fileName = sanitizeAssetName(srcPath.filename().string());
    const std::filesystem::path destDir = std::filesystem::path(project_.dir) / "res" / "hud";
    std::error_code ec;
    std::filesystem::create_directories(destDir, ec);
    std::filesystem::copy_file(srcPath, destDir / fileName,
                               std::filesystem::copy_options::overwrite_existing, ec);
    if (ec) {
        statusMessage_ = "HUD image import failed: " + ec.message();
        return -1;
    }

    HudImage h;
    h.name = srcPath.stem().string();
    h.imagePath = "res/hud/" + fileName;
    hudTexCache_.erase(h.imagePath);  // reload if replaced
    if (const HudTexture* t = hudTexture(h.imagePath)) {
        h.size[0] = (float)t->w;
        h.size[1] = (float)t->h;
    }
    target.push_back(std::move(h));
    return (int)target.size() - 1;
}

void App::importHudImage() {
    const int i = importHudImageInto(project_.hud);
    if (i < 0) return;
    selectedHud_ = i;
    uiFxSel_ = 0;
    saveAll("Saved");
}

// Texture-bake controls shared by HUD images and the USE prompt: the PS2
// only accepts 8/16/32/64/128/256/512-sized textures; the build resizes the
// imported PNG into .res-baked to that. "Auto" picks the nearest valid size,
// so a mis-sized import just works. Returns true when a setting changed.
bool App::hudBakeControls(HudImage& h) {
    bool changed = false;
    auto nearestValid = [](int v) {
        static const int V[] = {8, 16, 32, 64, 128, 256, 512};
        int best = V[0], bd = 1 << 30;
        for (int d : V) {
            const int dd = v > d ? v - d : d - v;
            if (dd < bd) { bd = dd; best = d; }
        }
        return best;
    };
    auto isValid = [&](int v) { return v > 0 && v == nearestValid(v); };
    auto dimCombo = [&](const char* label, int& dim) {
        static const int vals[] = {0, 8, 16, 32, 64, 128, 256, 512};
        static const char* names[] = {"Auto", "8",   "16",  "32",
                                      "64",   "128", "256", "512"};
        int cur = 0;
        for (int i = 0; i < 8; ++i)
            if (vals[i] == dim) { cur = i; break; }
        if (ImGui::Combo(label, &cur, names, 8)) {
            dim = vals[cur];
            changed = true;
        }
    };

    ImGui::SeparatorText("Texture (baked for PS2)");
    int sw = 0, sh = 0;
    if (const HudTexture* t = hudTexture(h.imagePath)) { sw = t->w; sh = t->h; }
    if (sw > 0) {
        const bool bad = !isValid(sw) || !isValid(sh);
        if (bad)
            ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.2f, 1.0f),
                               "Source %dx%d is not a PS2 size", sw, sh);
        else
            ImGui::TextDisabled("Source: %dx%d px", sw, sh);
    }
    ImGui::PushItemWidth(90.0f * uiScaleApplied_);
    dimCombo("Width##texw", h.texW);
    ImGui::SameLine();
    dimCombo("Height##texh", h.texH);
    ImGui::PopItemWidth();

    // Colors: like the per-asset material quality, "(project default)"
    // follows Preferences > Textures; the others override - e.g. keep an
    // important element full color while the rest of the HUD is quantized.
    int q = h.texQuant == "none" ? 1
            : h.texQuant == "8bit" ? 2
            : h.texQuant == "4bit" ? 3
                                   : 0;
    const char* qn[] = {"Project default", "Full color (32-bit)",
                        "256 colors (8-bit)", "16 colors (4-bit)"};
    if (ImGui::Combo("Colors##hudq", &q, qn, 4)) {
        h.texQuant = q == 1 ? "none" : q == 2 ? "8bit" : q == 3 ? "4bit" : "";
        changed = true;
    }

    // Resolve "(project default)" for the baked readout.
    auto colorLabel = [](const std::string& qv) {
        return qv == "8bit"   ? "256 colors (8-bit)"
               : qv == "4bit" ? "16 colors (4-bit)"
                              : "Full color (32-bit)";
    };
    const std::string effQ =
        h.texQuant.empty() ? project_.settings.textureQuant : h.texQuant;
    const int bw = h.texW > 0 ? h.texW : (sw > 0 ? nearestValid(sw) : 0);
    const int bh = h.texH > 0 ? h.texH : (sh > 0 ? nearestValid(sh) : 0);
    if (h.texQuant.empty())
        ImGui::TextDisabled("Baked: %dx%d, %s (from project)", bw, bh,
                            colorLabel(effQ));
    else
        ImGui::TextDisabled("Baked: %dx%d, %s", bw, bh, colorLabel(effQ));
    ImGui::TextDisabled(
        "Resized at build (source in res/hud stays untouched). The\n"
        "on-screen size above is separate - the sprite is stretched.");
    return changed;
}

// UI Editor window (Tools > UI Editor): everything composited over the 3D
// scene, as one reorderable "screen stack" - the HUD images plus two effect
// layers (bloom+grading, and film grain). The stack order is the game's draw
// order: entries above an effect layer stay crisp (e.g. the crosshair over the
// bloom), entries below are composited with it. Bloom and grain are separate
// entries so, say, bloom can sit under the HUD while grain overlays the whole
// screen.
namespace {
constexpr int kBloomMark = -2;
constexpr int kGrainMark = -3;
// Custom screen effect placements in the stack encode as kFxMarkBase - index
// (index into Project::screenFx). Any entry <= kFxMarkBase is a custom effect;
// bloom/grain (-2/-3) and HUD sprites (>= 0) stay clear of this range.
constexpr int kFxMarkBase = -100;
constexpr bool isFxMark(int e) { return e <= kFxMarkBase; }
constexpr int fxMarkIndex(int e) { return kFxMarkBase - e; }
constexpr int fxMark(int i) { return kFxMarkBase - i; }
}  // namespace

void App::drawUiEditorWindow() {
    if (!showUiEditor_ || !hasProject_) return;

    ImGui::SetNextWindowSize(ImVec2(scaled(560), scaled(420)),
                             ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("UI Editor", &showUiEditor_)) {
        ImGui::End();
        return;
    }

    bool changed = false;
    const int n = (int)project_.hud.size();

    // Render-order stack (bottom of the screen list = drawn first): hud indices
    // plus the two effect markers. A marker at layer L renders right before hud
    // sprite L; layer -1 (or >= n) renders after every sprite (topmost). Bloom
    // before grain when they share a slot (grain composites over the graded,
    // bloomed image - the fixed internal order).
    const int nFx = (int)project_.screenFx.size();
    auto topmost = [&](int L) { return L < 0 || L >= n; };
    auto emitMarkers = [&](std::vector<int>& s, int layer) {
        if (project_.hudBloomLayer == layer) s.push_back(kBloomMark);
        if (project_.hudGrainLayer == layer) s.push_back(kGrainMark);
        for (int fi = 0; fi < nFx; ++fi)
            if (project_.screenFx[fi].layer == layer) s.push_back(fxMark(fi));
    };
    auto buildStack = [&]() {
        std::vector<int> s;
        s.reserve(n + 2 + nFx);
        for (int i = 0; i < n; ++i) {
            emitMarkers(s, i);
            s.push_back(i);
        }
        // Topmost markers (layer -1 or >= n): bloom, grain, then effects in
        // placement order.
        if (topmost(project_.hudBloomLayer)) s.push_back(kBloomMark);
        if (topmost(project_.hudGrainLayer)) s.push_back(kGrainMark);
        for (int fi = 0; fi < nFx; ++fi)
            if (topmost(project_.screenFx[fi].layer)) s.push_back(fxMark(fi));
        return s;
    };
    // Rebuild the model from a render-order stack: hud array + screenFx list are
    // reordered to match, each layer = number of hud sprites before its marker
    // (n = -1, topmost). Fx markers carry their OLD placement index; screenFx is
    // rebuilt in stack order so composite order among same-slot effects follows
    // the stack.
    auto rebuild = [&](const std::vector<int>& s) {
        std::vector<HudImage> newHud;
        std::vector<ScreenFxPlacement> newFx;
        newHud.reserve(n);
        newFx.reserve(nFx);
        int before = 0, bl = -1, gr = -1;
        for (int e : s) {
            if (e == kBloomMark) bl = before;
            else if (e == kGrainMark) gr = before;
            else if (isFxMark(e)) {
                ScreenFxPlacement pl = project_.screenFx[fxMarkIndex(e)];
                pl.layer = before;
                newFx.push_back(std::move(pl));
            } else {
                newHud.push_back(project_.hud[e]);
                ++before;
            }
        }
        const int newN = (int)newHud.size();
        project_.hudBloomLayer = bl >= newN ? -1 : bl;
        project_.hudGrainLayer = gr >= newN ? -1 : gr;
        for (ScreenFxPlacement& f : newFx)
            if (f.layer >= newN) f.layer = -1;
        project_.hud = std::move(newHud);
        project_.screenFx = std::move(newFx);
    };

    // Display order: top of the screen (drawn last) first = reversed stack.
    std::vector<int> order = buildStack();
    std::reverse(order.begin(), order.end());

    // --- left: the screen stack ---------------------------------------------
    ImGui::BeginChild("##ui_stack", ImVec2(230 * uiScaleApplied_, 0),
                      ImGuiChildFlags_Borders);
    if (ImGui::Button("Import image (PNG)...", ImVec2(-1, 0))) importHudImage();
    ImGui::Checkbox("Show in viewport", &showHudInEditor_);

    // Triggerable on-screen texts. They draw above the whole HUD stack
    // (under menus), so they sit above the reorderable list.
    ImGui::SeparatorText("Texts");
    for (int i = 0; i < (int)project_.hudTexts.size(); ++i) {
        ImGui::PushID(1000 + i);
        if (ImGui::Selectable(project_.hudTexts[i].name.c_str(),
                              uiFxSel_ == 4 && selectedText_ == i)) {
            uiFxSel_ = 4;
            selectedText_ = i;
        }
        ImGui::PopID();
    }
    if (ImGui::SmallButton("+ Add text")) {
        HudText t;
        // unique name: "text", "text-2", ... (referenced by flow nodes)
        int suffix = 1;
        auto taken = [&](const std::string& n) {
            for (const HudText& e : project_.hudTexts)
                if (e.name == n) return true;
            return false;
        };
        while (taken(suffix == 1 ? "text" : "text-" + std::to_string(suffix)))
            ++suffix;
        t.name = suffix == 1 ? "text" : "text-" + std::to_string(suffix);
        project_.hudTexts.push_back(std::move(t));
        uiFxSel_ = 4;
        selectedText_ = (int)project_.hudTexts.size() - 1;
        changed = true;
    }
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip(
            "Baked to PNG sprites at build (the PS2 engine has no font).\n"
            "Show/hide them from the flow graph: Show Text / Hide Text.");

    // Custom screen effects loaded from screen-effects/*.screenfx. Effects not
    // yet placed in the stack are offered here with a "+ Add"; management
    // (reload / scaffold / jump) mirrors the Flow Graph "Custom nodes..." menu.
    ImGui::SeparatorText("Screen effects");
    {
        auto isPlaced = [&](const std::string& key) {
            for (const ScreenFxPlacement& f : project_.screenFx)
                if (f.key == key) return true;
            return false;
        };
        auto addToStack = [&](const CustomScreenFx* e) {
            ScreenFxPlacement f;
            f.key = e->key;
            f.layer = -1;  // topmost by default
            f.enabled = true;
            for (int i = 0; i < 4; ++i) f.params[i] = e->paramDefault[i];
            project_.screenFx.push_back(std::move(f));
            uiFxSel_ = 5;
            selectedFx_ = (int)project_.screenFx.size() - 1;
            changed = true;
        };
        int unplaced = 0;
        for (const auto& e : customScreenEffects()) {
            if (isPlaced(e->key)) continue;
            ++unplaced;
            ImGui::PushID(("addfx" + e->key).c_str());
            if (ImGui::SmallButton("+ Add")) addToStack(e.get());
            ImGui::SameLine();
            ImGui::TextUnformatted(e->title.c_str());
            ImGui::PopID();
        }
        if (customScreenEffects().empty())
            ImGui::TextDisabled("None. New starter effect below,\nor drop a "
                                ".screenfx in screen-effects/.");
        else if (unplaced == 0)
            ImGui::TextDisabled("All %d effect(s) placed in the stack.",
                                (int)customScreenEffects().size());

        if (ImGui::SmallButton("Custom effects...")) ImGui::OpenPopup("##customfx");
        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(
                "Low-level full-screen effects (GS blits, like bloom/grain),\n"
                "written in screen-effects/*.screenfx. See\n"
                "docs/custom-screen-effects.md.");
        if (ImGui::BeginPopup("##customfx")) {
            ImGui::TextDisabled("%d loaded from screen-effects/",
                                (int)customScreenEffects().size());
            ImGui::Separator();
            if (ImGui::MenuItem("Reload from folder")) {
                const std::string msg = screenfx::loadForProject(project_.dir);
                // Drop placements whose effect file vanished on reload.
                for (size_t i = project_.screenFx.size(); i-- > 0;)
                    if (!customScreenFx(project_.screenFx[i].key))
                        project_.screenFx.erase(project_.screenFx.begin() + i);
                statusMessage_ = msg.empty()
                                     ? "No custom effects found in screen-effects/"
                                     : msg;
                changed = true;
            }
            if (ImGui::MenuItem("New starter effect (example.screenfx)")) {
                const std::string path = screenfx::writeExample(project_.dir);
                if (path.rfind("error:", 0) == 0) {
                    statusMessage_ = path;
                } else {
                    screenfx::loadForProject(project_.dir);
                    statusMessage_ = "Wrote " + path + " - edit it, then Reload";
                }
            }
            if (ImGui::BeginMenu("Jump to effect file",
                                 !customScreenEffects().empty())) {
                for (const auto& e : customScreenEffects())
                    if (ImGui::MenuItem(e->title.c_str()))
                        openInVSCode(e->sourceFile);
                ImGui::EndMenu();
            }
            ImGui::EndPopup();
        }
    }

    ImGui::SeparatorText("Screen stack");
    ImGui::TextDisabled("Top entry draws last (on top).\nDrag to reorder.");
    // The USE prompt is part of the screen, but pinned: it always draws
    // above the HUD stack (and under menus), and cannot be deleted.
    if (ImGui::Selectable("[ USE prompt ]", uiFxSel_ == 3)) uiFxSel_ = 3;
    for (int r = 0; r < (int)order.size(); ++r) {
        const int id = order[r];
        ImGui::PushID(r);
        bool isSel;
        std::string label;
        bool dim = false;  // disabled custom effect
        if (id == kBloomMark) {
            isSel = uiFxSel_ == 1;
            label = "[ Bloom + color grading ]";
        } else if (id == kGrainMark) {
            isSel = uiFxSel_ == 2;
            label = "[ Film grain ]";
        } else if (isFxMark(id)) {
            const int fi = fxMarkIndex(id);
            const ScreenFxPlacement& pl = project_.screenFx[fi];
            const CustomScreenFx* e = customScreenFx(pl.key);
            isSel = uiFxSel_ == 5 && selectedFx_ == fi;
            label = "[ FX: " + (e ? e->title : pl.key) + " ]";
            dim = !pl.enabled;
            if (dim) label += "  (off)";
        } else {
            isSel = uiFxSel_ == 0 && selectedHud_ == id;
            label = project_.hud[id].name;
        }
        if (dim) ImGui::PushStyleColor(ImGuiCol_Text,
                                       ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        if (ImGui::Selectable(label.c_str(), isSel)) {
            if (id == kBloomMark) uiFxSel_ = 1;
            else if (id == kGrainMark) uiFxSel_ = 2;
            else if (isFxMark(id)) { uiFxSel_ = 5; selectedFx_ = fxMarkIndex(id); }
            else { uiFxSel_ = 0; selectedHud_ = id; }
        }
        if (dim) ImGui::PopStyleColor();
        // Drag to reorder: swap with the neighbor the cursor moved towards,
        // then rebuild the model from the new order.
        if (ImGui::IsItemActive() && !ImGui::IsItemHovered()) {
            const int dst = r + (ImGui::GetMouseDragDelta(0).y < 0.0f ? -1 : 1);
            if (dst >= 0 && dst < (int)order.size()) {
                // Remember the selected image / effect so its selection
                // survives the reorder (indices shift; identity does not).
                const bool hadHud =
                    uiFxSel_ == 0 && selectedHud_ >= 0 && selectedHud_ < n;
                HudImage selHud;
                if (hadHud) selHud = project_.hud[selectedHud_];
                const bool hadFx = uiFxSel_ == 5 && selectedFx_ >= 0 &&
                                   selectedFx_ < (int)project_.screenFx.size();
                ScreenFxPlacement selFx;
                if (hadFx) selFx = project_.screenFx[selectedFx_];

                std::swap(order[r], order[dst]);
                std::vector<int> s(order.rbegin(), order.rend());
                rebuild(s);

                if (hadHud)
                    for (int i = 0; i < (int)project_.hud.size(); ++i)
                        if (project_.hud[i] == selHud) { selectedHud_ = i; break; }
                if (hadFx)
                    for (int i = 0; i < (int)project_.screenFx.size(); ++i)
                        if (project_.screenFx[i] == selFx) { selectedFx_ = i; break; }
                ImGui::ResetMouseDragDelta();
                changed = true;
            }
        }
        ImGui::PopID();
    }
    // Depth of field is pinned at the very bottom: it composites right after
    // the 3D scene (per-pixel z-tested against scene depth), so it can never
    // sit above a sprite - sprites stamp z = max across their whole rect and
    // would punch sharp rectangles into the blur.
    if (ImGui::Selectable("[ Depth of field ]", uiFxSel_ == 6)) uiFxSel_ = 6;
    if (project_.hud.empty())
        ImGui::TextDisabled("No HUD images yet.\nImport a PNG above.");
    ImGui::EndChild();

    ImGui::SameLine();

    // --- right: selected entry ------------------------------------------------
    ImGui::BeginChild("##ui_props", ImVec2(0, 0));
    if (uiFxSel_ == 1) {
        ImGui::SeparatorText("Bloom + color grading");
        ImGui::SliderFloat("Bloom", &project_.settings.bloom, 0.0f, 1.0f, "%.2f");
        changed |= ImGui::IsItemDeactivatedAfterEdit();
        ImGui::TextDisabled(
            "GS framebuffer trick - no pixel shaders on the PS2. Quarter-res\n"
            "blur re-added over the frame (soft glow).");
        ImGui::Spacing();
        ImGui::TextWrapped(
            "Stack entries above this layer draw crisp on top of the bloom - "
            "put the crosshair or text there so the glow does not blur them. "
            "At the very top the bloom applies at the end of the frame, over "
            "everything including menus.");
        ImGui::Spacing();
        ImGui::TextDisabled(
            "Color grading applies with this layer. Author presets in\n"
            "Tools > Color Grading. Per-scene bloom strength: Scene > Scene\n"
            "Preferences > Post effects.");
    } else if (uiFxSel_ == 2) {
        ImGui::SeparatorText("Film grain");
        ImGui::SliderFloat("Film grain", &project_.settings.grain, 0.0f, 1.0f,
                           "%.2f");
        changed |= ImGui::IsItemDeactivatedAfterEdit();
        ImGui::TextDisabled(
            "Animated noise overlay (GS blits). Subtle values work best.");
        ImGui::Spacing();
        ImGui::TextWrapped(
            "As a separate layer the grain can sit above the bloom and the "
            "HUD - a filmic overlay over the whole screen - while the bloom "
            "stays underneath so it does not smear the UI.");
        ImGui::Spacing();
        ImGui::TextDisabled(
            "Per-scene grain strength: Scene > Scene Preferences > Post "
            "effects.");
    } else if (uiFxSel_ == 6) {
        ImGui::SeparatorText("Depth of field");
        ImGui::SliderFloat("Amount", &project_.settings.dofAmount, 0.0f, 1.0f,
                           "%.2f");
        changed |= ImGui::IsItemDeactivatedAfterEdit();
        ImGui::DragFloat("Focus", &project_.settings.dofFocus, 0.5f, 0.5f,
                         500.0f, "%.1f");
        changed |= ImGui::IsItemDeactivatedAfterEdit();
        ImGui::DragFloat("Range", &project_.settings.dofRange, 0.5f, 0.1f,
                         500.0f, "%.1f");
        changed |= ImGui::IsItemDeactivatedAfterEdit();
        ImGui::TextDisabled(
            "The image stays sharp up to Focus (world units from the\n"
            "camera) and blurs progressively, reaching the full Amount\n"
            "blur at Focus + Range. Amount 0 = off.");
        ImGui::Spacing();
        ImGui::TextWrapped(
            "Pinned under the whole stack: the blur follows real scene depth "
            "per pixel (z-tested GS blits), and sprites stamp z across their "
            "full rect - compositing DoF above them would punch sharp "
            "rectangles into the blur. Every HUD entry always draws crisp.");
        ImGui::Spacing();
        ImGui::TextDisabled(
            "Per-scene values: Scene > Scene Preferences > Post effects.\n"
            "Runtime: the Set Depth Of Field flow node overrides these\n"
            "(and can restore them with its Scene setting mode).");
    } else if (uiFxSel_ == 3) {
        // --- the pinned USE prompt ------------------------------------------
        HudImage& h = project_.usePrompt;
        ImGui::SeparatorText("USE prompt");
        ImGui::TextWrapped(
            "Shown while the player looks at a usable object up close. "
            "Always draws above the HUD stack (and under menus); cannot be "
            "deleted.");
        ImGui::Spacing();
        ImGui::DragFloat2("Position##use", h.pos, 0.005f, 0.0f, 1.0f, "%.3f");
        changed |= ImGui::IsItemDeactivatedAfterEdit();
        ImGui::DragFloat2("Size (px)##use", h.size, 1.0f, 1.0f, 512.0f, "%.0f");
        changed |= ImGui::IsItemDeactivatedAfterEdit();

        ImGui::SeparatorText("Image");
        if (h.imagePath.empty()) {
            ImGui::TextDisabled("Built-in \"USE\" sprite (res/hud/use.png).");
            if (ImGui::Button("Custom image (PNG)...")) {
                const std::string src = pickPngFile();
                if (!src.empty()) {
                    const std::filesystem::path srcPath(src);
                    const std::string fileName =
                        sanitizeAssetName(srcPath.filename().string());
                    const std::filesystem::path destDir =
                        std::filesystem::path(project_.dir) / "res" / "hud";
                    std::error_code ec;
                    std::filesystem::create_directories(destDir, ec);
                    std::filesystem::copy_file(
                        srcPath, destDir / fileName,
                        std::filesystem::copy_options::overwrite_existing, ec);
                    if (!ec) {
                        h.imagePath = "res/hud/" + fileName;
                        hudTexCache_.erase(h.imagePath);  // reload if replaced
                        changed = true;
                    } else {
                        statusMessage_ =
                            "USE prompt import failed: " + ec.message();
                    }
                }
            }
        } else {
            ImGui::TextDisabled("Custom: %s", h.imagePath.c_str());
            if (ImGui::Button("Replace image (PNG)...")) {
                const std::string src = pickPngFile();
                if (!src.empty()) {
                    const std::filesystem::path srcPath(src);
                    const std::string fileName =
                        sanitizeAssetName(srcPath.filename().string());
                    const std::filesystem::path destDir =
                        std::filesystem::path(project_.dir) / "res" / "hud";
                    std::error_code ec;
                    std::filesystem::create_directories(destDir, ec);
                    std::filesystem::copy_file(
                        srcPath, destDir / fileName,
                        std::filesystem::copy_options::overwrite_existing, ec);
                    if (!ec) {
                        h.imagePath = "res/hud/" + fileName;
                        hudTexCache_.erase(h.imagePath);
                        changed = true;
                    }
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Reset to built-in")) {
                h.imagePath.clear();
                changed = true;
            }
            // The bake (pow2 resize + quantization) only applies to custom
            // images; the built-in sprite is already a valid PS2 texture.
            changed |= hudBakeControls(h);
        }
    } else if (uiFxSel_ == 4 && selectedText_ >= 0 &&
               selectedText_ < (int)project_.hudTexts.size()) {
        // --- a triggerable on-screen text -----------------------------------
        HudText& t = project_.hudTexts[selectedText_];
        ImGui::SeparatorText(t.name.c_str());
        {
            char nameBuf[64];
            std::snprintf(nameBuf, sizeof(nameBuf), "%s", t.name.c_str());
            ImGui::SetNextItemWidth(160.0f);
            if (ImGui::InputText("Name", nameBuf, sizeof(nameBuf))) {
                // Renames follow into the flow graphs (Show/Hide Text nodes
                // reference texts by name), like layer renames do.
                const std::string oldName = t.name;
                t.name = nameBuf;
                for (SceneData& sc : project_.scenes)
                    for (SceneObject& o : sc.objects)
                        for (FlowNode& fn : o.flowGraph.nodes) {
                            const FlowNodeType* ft = flowNodeType(fn.type);
                            if (ft && ft->strKind == FlowParamKind::HudTextName &&
                                fn.str == oldName)
                                fn.str = t.name;
                        }
            }
            changed |= ImGui::IsItemDeactivatedAfterEdit();
        }
        {
            // multiline: '\n' becomes a new line in the baked sprite
            char textBuf[512];
            std::snprintf(textBuf, sizeof(textBuf), "%s", t.text.c_str());
            if (ImGui::InputTextMultiline("Text", textBuf, sizeof(textBuf),
                                          ImVec2(-1.0f, 80.0f * uiScaleApplied_)))
                t.text = textBuf;
            changed |= ImGui::IsItemDeactivatedAfterEdit();
        }
        changed |= fontCombo(t.fontPath);
        ImGui::SetNextItemWidth(120.0f);
        if (ImGui::DragInt("Font size", &t.size, 0.2f, 8, 48, "%d px"))
            t.size = t.size < 8 ? 8 : t.size > 48 ? 48 : t.size;
        changed |= ImGui::IsItemDeactivatedAfterEdit();
        if (ImGui::ColorEdit3("Color##text", t.color,
                              ImGuiColorEditFlags_NoInputs))
            changed = true;
        ImGui::DragFloat2("Position##text", t.pos, 0.005f, 0.0f, 1.0f, "%.3f");
        changed |= ImGui::IsItemDeactivatedAfterEdit();
        if (ImGui::Checkbox("Drop shadow", &t.shadow)) changed = true;
        if (ImGui::Checkbox("Visible at game start", &t.visibleAtStart))
            changed = true;
        ImGui::TextDisabled(
            "Show/hide from the flow graph: HUD > Show Text (with an\n"
            "optional auto-hide after N seconds) and Hide Text.");

        // Live preview: the exact sprite the build will bake.
        {
            std::string key = t.name + "\x1f" + t.text + "\x1f" + t.fontPath +
                              "\x1f" + std::to_string(t.size) + "\x1f" +
                              std::to_string(t.shadow) + "\x1f" +
                              std::to_string(t.color[0]) + "," +
                              std::to_string(t.color[1]) + "," +
                              std::to_string(t.color[2]);
            if (key != textPreviewKey_) {
                std::vector<unsigned char> rgba;
                int w = 0, h = 0;
                if (menubake::bakeTextRGBA(t, project_.dir, rgba, w, h)) {
                    if (!textPreviewTex_) glGenTextures(1, &textPreviewTex_);
                    glBindTexture(GL_TEXTURE_2D, textPreviewTex_);
                    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA,
                                 GL_UNSIGNED_BYTE, rgba.data());
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                                    GL_LINEAR);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER,
                                    GL_LINEAR);
                    textPreviewW_ = w;
                    textPreviewH_ = h;
                }
                textPreviewKey_ = key;
            }
            if (textPreviewTex_) {
                ImGui::SeparatorText("Preview");
                ImGui::Image((ImTextureID)(intptr_t)textPreviewTex_,
                             ImVec2((float)textPreviewW_, (float)textPreviewH_));
                ImGui::TextDisabled("Texture: %dx%d px", textPreviewW_,
                                    textPreviewH_);
            }
        }

        ImGui::Spacing();
        if (ImGui::Button("Delete text")) {
            project_.hudTexts.erase(project_.hudTexts.begin() + selectedText_);
            selectedText_ = -1;
            uiFxSel_ = 0;
            changed = true;
        }
    } else if (uiFxSel_ == 5 && selectedFx_ >= 0 &&
               selectedFx_ < (int)project_.screenFx.size()) {
        // --- a custom screen effect placement -------------------------------
        ScreenFxPlacement& pl = project_.screenFx[selectedFx_];
        const CustomScreenFx* e = customScreenFx(pl.key);
        ImGui::SeparatorText(e ? e->title.c_str() : pl.key.c_str());
        if (!e) {
            ImGui::TextWrapped(
                "The effect file for '%s' is missing from screen-effects/. "
                "Restore it (then Reload), or remove this placement.",
                pl.key.c_str());
        } else {
            if (ImGui::Checkbox("Enabled", &pl.enabled)) changed = true;
            ImGui::TextDisabled(
                "Low-level GS effect (screen-effects/%s). No pixel shaders on\n"
                "the PS2 - not previewed in the editor viewport; build to see it.",
                (std::filesystem::path(e->sourceFile).filename().string()).c_str());
            ImGui::Spacing();
            if (e->paramCount == 0) {
                ImGui::TextDisabled("This effect has no parameters.");
            } else {
                for (int i = 0; i < e->paramCount; ++i) {
                    ImGui::SetNextItemWidth(scaled(200));
                    if (ImGui::SliderFloat(e->paramLabel[i].c_str(), &pl.params[i],
                                           e->paramMin[i], e->paramMax[i], "%.3f"))
                        pl.params[i] = pl.params[i] < e->paramMin[i]
                                           ? e->paramMin[i]
                                           : pl.params[i] > e->paramMax[i]
                                                 ? e->paramMax[i]
                                                 : pl.params[i];
                    changed |= ImGui::IsItemDeactivatedAfterEdit();
                }
            }
            ImGui::Spacing();
            ImGui::TextWrapped(
                "Stack entries above this layer draw crisp on top of the "
                "effect; entries below are composited with it. Drag it in the "
                "stack to move it (e.g. under the HUD, or over everything).");
            ImGui::Spacing();
            if (ImGui::Button("Jump to effect file"))
                openInVSCode(e->sourceFile);
        }
        ImGui::Spacing();
        if (ImGui::Button("Remove from stack")) {
            project_.screenFx.erase(project_.screenFx.begin() + selectedFx_);
            selectedFx_ = -1;
            uiFxSel_ = 0;
            changed = true;
        }
    } else if (selectedHud_ >= 0 && selectedHud_ < n) {
        HudImage& h = project_.hud[selectedHud_];
        ImGui::SeparatorText(h.name.c_str());
        ImGui::DragFloat2("Position##hud", h.pos, 0.005f, 0.0f, 1.0f, "%.3f");
        changed |= ImGui::IsItemDeactivatedAfterEdit();
        ImGui::DragFloat2("Size (px)##hud", h.size, 1.0f, 1.0f, 512.0f, "%.0f");
        changed |= ImGui::IsItemDeactivatedAfterEdit();

        changed |= hudBakeControls(h);

        ImGui::Spacing();
        if (ImGui::Button("Delete HUD image"))
            requestAssetDelete(PendingAssetDelete::Hud, h.imagePath, h.name,
                               selectedHud_);
    } else {
        ImGui::TextDisabled("Select an entry on the left.");
    }
    ImGui::EndChild();

    if (changed) saveAll("Saved");  // UI edits are not on the undo stack
    ImGui::End();
}

// Property editor for one progress bar (Loading Screens). Returns true when a
// setting changed (the caller saves).
bool App::loadingBarControls(LoadingBar& b) {
    bool changed = false;
    const char* kinds[] = {"Continuous (fill)", "Quantized (segments)"};
    ImGui::SetNextItemWidth(scaled(200));
    if (ImGui::Combo("Type", &b.kind, kinds, 2)) changed = true;
    ImGui::DragFloat2("Position##bar", b.pos, 0.005f, 0.0f, 1.0f, "%.3f");
    changed |= ImGui::IsItemDeactivatedAfterEdit();
    ImGui::DragFloat2("Size (px)##bar", b.size, 1.0f, 1.0f, 512.0f, "%.0f");
    changed |= ImGui::IsItemDeactivatedAfterEdit();
    if (ImGui::ColorEdit3("Track / off color", b.bgColor)) changed = true;
    if (ImGui::ColorEdit3("Fill / on color", b.fillColor)) changed = true;
    if (b.kind == 1) {
        ImGui::SetNextItemWidth(scaled(120));
        if (ImGui::DragInt("Segments", &b.segments, 0.1f, 2, 16))
            b.segments = b.segments < 2 ? 2 : b.segments > 16 ? 16 : b.segments;
        changed |= ImGui::IsItemDeactivatedAfterEdit();
        ImGui::SetNextItemWidth(scaled(120));
        ImGui::DragFloat("Spacing (px)", &b.spacing, 0.2f, 0.0f, 64.0f, "%.0f");
        changed |= ImGui::IsItemDeactivatedAfterEdit();

        ImGui::SeparatorText("Segment image (optional)");
        if (b.segImage.imagePath.empty()) {
            ImGui::TextDisabled("Colored rectangles (no texture).");
            if (ImGui::Button("Set segment image (PNG)...")) {
                std::vector<HudImage> tmp;
                const int i = importHudImageInto(tmp);
                if (i >= 0) {
                    b.segImage.imagePath = tmp[i].imagePath;
                    changed = true;
                }
            }
        } else {
            ImGui::TextDisabled("%s", b.segImage.imagePath.c_str());
            if (ImGui::Button("Replace...")) {
                std::vector<HudImage> tmp;
                const int i = importHudImageInto(tmp);
                if (i >= 0) {
                    b.segImage.imagePath = tmp[i].imagePath;
                    changed = true;
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Clear image")) {
                b.segImage.imagePath.clear();
                changed = true;
            }
            changed |= hudBakeControls(b.segImage);
        }
        ImGui::TextDisabled(
            "Lit segments are tinted with the on color, unlit with the\n"
            "off color (color modulation of one texture).");
    }
    return changed;
}

// Draws the loading screen into the current window at 512x448 aspect, honoring
// `fraction` for the progress bars (matches loadingscreen::renderFrame on PS2).
void App::drawLoadingPreview(const LoadingScreenDef& ls, float fraction) {
    ImVec2 avail = ImGui::GetContentRegionAvail();
    if (avail.x < 20.0f || avail.y < 20.0f) return;
    const float aspect = 512.0f / 448.0f;
    float w = avail.x, h = w / aspect;
    if (h > avail.y) { h = avail.y; w = h * aspect; }
    ImVec2 p0 = ImGui::GetCursorScreenPos();
    p0.x += (avail.x - w) * 0.5f;
    const ImVec2 p1(p0.x + w, p0.y + h);
    ImDrawList* dl = ImGui::GetWindowDrawList();

    auto col = [](const float* c, float a = 1.0f) {
        return IM_COL32((int)(c[0] * 255.0f + 0.5f), (int)(c[1] * 255.0f + 0.5f),
                        (int)(c[2] * 255.0f + 0.5f), (int)(a * 255.0f + 0.5f));
    };
    dl->AddRectFilled(p0, p1, col(ls.bgColor));
    // Screen-space size of a value given in 512x448 pixels.
    auto sw = [&](float px) { return px / 512.0f * w; };
    auto sh = [&](float px) { return px / 448.0f * h; };
    auto cx = [&](float nx) { return p0.x + nx * w; };
    auto cy = [&](float ny) { return p0.y + ny * h; };

    for (int i = 0; i < (int)ls.images.size(); ++i) {
        const HudImage& im = ls.images[i];
        const float dw = sw(im.size[0]), dh = sh(im.size[1]);
        const ImVec2 a(cx(im.pos[0]) - dw * 0.5f, cy(im.pos[1]) - dh * 0.5f);
        const ImVec2 b(a.x + dw, a.y + dh);
        if (const HudTexture* t = hudTexture(im.imagePath))
            dl->AddImage((ImTextureID)(intptr_t)t->tex, a, b);
        else
            dl->AddRect(a, b, IM_COL32(255, 80, 80, 255));
        if (showLoadingEditor_ && lsSelKind_ == 0 && lsSelIdx_ == i)
            dl->AddRect(ImVec2(a.x - 1, a.y - 1), ImVec2(b.x + 1, b.y + 1),
                        IM_COL32(80, 200, 255, 255));
    }
    for (int i = 0; i < (int)ls.texts.size(); ++i) {
        const HudText& t = ls.texts[i];
        HudText tc = t;  // mangle the cache key so it never collides with HUD texts
        tc.name = "lsprev\x1f" + ls.name + "\x1f" + t.name;
        if (const HudTexture* tex = hudTextTexture(tc)) {
            const float dw = sw((float)tex->w), dh = sh((float)tex->h);
            const ImVec2 a(cx(t.pos[0]) - dw * 0.5f, cy(t.pos[1]) - dh * 0.5f);
            dl->AddImage((ImTextureID)(intptr_t)tex->tex, a,
                         ImVec2(a.x + dw, a.y + dh));
            if (showLoadingEditor_ && lsSelKind_ == 1 && lsSelIdx_ == i)
                dl->AddRect(ImVec2(a.x - 1, a.y - 1),
                            ImVec2(a.x + dw + 1, a.y + dh + 1),
                            IM_COL32(80, 200, 255, 255));
        }
    }
    for (int i = 0; i < (int)ls.bars.size(); ++i) {
        const LoadingBar& b = ls.bars[i];
        const float bw = sw(b.size[0]), bh = sh(b.size[1]);
        const float bx = cx(b.pos[0]) - bw * 0.5f;
        const float by = cy(b.pos[1]) - bh * 0.5f;
        if (b.kind == 0) {
            dl->AddRectFilled(ImVec2(bx, by), ImVec2(bx + bw, by + bh),
                              col(b.bgColor));
            if (fraction > 0.0f)
                dl->AddRectFilled(ImVec2(bx, by),
                                  ImVec2(bx + bw * fraction, by + bh),
                                  col(b.fillColor));
        } else {
            const int segs = b.segments < 1 ? 1 : b.segments;
            const int lit = (int)(fraction * segs + 0.001f);
            const float segW = (bw - sw(b.spacing) * (segs - 1)) / segs;
            for (int k = 0; k < segs; ++k) {
                const float sx = bx + k * (segW + sw(b.spacing));
                const float* c = (k < lit) ? b.fillColor : b.bgColor;
                dl->AddRectFilled(ImVec2(sx, by), ImVec2(sx + segW, by + bh),
                                  col(c));
            }
        }
        if (showLoadingEditor_ && lsSelKind_ == 2 && lsSelIdx_ == i)
            dl->AddRect(ImVec2(bx - 1, by - 1), ImVec2(bx + bw + 1, by + bh + 1),
                        IM_COL32(80, 200, 255, 255));
    }
    dl->AddRect(p0, p1, IM_COL32(120, 120, 120, 255));
    ImGui::Dummy(ImVec2(avail.x, h));
}

// Loading Screens (Tools > Loading Screens): named loading screens shown while
// a scene loads. Each has a background color, image + text elements (baked like
// the HUD) and progress bars (continuous or quantized). Scenes pick one in
// Scene > Preferences; one can be the project default. Like the other preset
// collections these live outside undo, so edits save immediately (saveAll).
// Boot splash screens: a collapsing section at the top of the Loading Screens
// window. Images shown in order at startup (after the Tyra logo, before the
// loading screen), each for its own duration. Self-contained (balanced
// Begin/EndChild) so the caller's later early-returns stay valid.
bool App::drawSplashSection() {
    if (!ImGui::CollapsingHeader("Boot splash screens")) return false;
    bool changed = false;
    auto& splashes = project_.splashScreens;
    if (selectedSplash_ >= (int)splashes.size()) selectedSplash_ = -1;

    ImGui::TextDisabled("Images shown at startup, in order, before the loading screen.");

    ImGui::BeginChild("##splash_body", ImVec2(0, scaled(190)),
                      ImGuiChildFlags_Borders);

    // --- left: splash list -------------------------------------------------
    ImGui::BeginChild("##splash_list", ImVec2(scaled(210), 0),
                      ImGuiChildFlags_Borders);
    if (ImGui::Button("+ Add splash (PNG)...", ImVec2(-1, 0))) {
        std::vector<HudImage> tmp;
        const int i = importHudImageInto(tmp);
        if (i >= 0) {
            SplashScreen s;
            s.name = tmp[i].name.empty() ? "splash" : tmp[i].name;
            s.image = tmp[i];
            s.image.pos[0] = 0.5f;
            s.image.pos[1] = 0.5f;
            s.image.size[0] = 512.0f;  // default fullscreen stretch
            s.image.size[1] = 448.0f;
            splashes.push_back(std::move(s));
            selectedSplash_ = (int)splashes.size() - 1;
            changed = true;
        }
    }
    ImGui::Separator();
    for (int i = 0; i < (int)splashes.size(); ++i) {
        ImGui::PushID(i);
        char label[96];
        std::snprintf(label, sizeof(label), "%d. %s  (%.1fs)", i + 1,
                      splashes[i].name.c_str(), splashes[i].duration);
        if (ImGui::Selectable(label, selectedSplash_ == i)) selectedSplash_ = i;
        ImGui::PopID();
    }
    if (splashes.empty())
        ImGui::TextDisabled("No splash screens.\nAdd one above.");
    ImGui::EndChild();
    ImGui::SameLine();

    // --- right: selected splash properties ---------------------------------
    ImGui::BeginChild("##splash_props", ImVec2(0, 0));
    if (selectedSplash_ >= 0 && selectedSplash_ < (int)splashes.size()) {
        SplashScreen& s = splashes[selectedSplash_];
        char nameBuf[64];
        std::snprintf(nameBuf, sizeof(nameBuf), "%s", s.name.c_str());
        ImGui::SetNextItemWidth(scaled(160));
        if (ImGui::InputText("Name##splash", nameBuf, sizeof(nameBuf)))
            s.name = nameBuf;
        changed |= ImGui::IsItemDeactivatedAfterEdit();
        ImGui::SetNextItemWidth(scaled(160));
        if (ImGui::DragFloat("Duration (s)", &s.duration, 0.05f, 0.1f, 10.0f, "%.1f"))
            s.duration =
                s.duration < 0.1f ? 0.1f : (s.duration > 10.0f ? 10.0f : s.duration);
        changed |= ImGui::IsItemDeactivatedAfterEdit();
        if (ImGui::ColorEdit3("Background##splash", s.bgColor,
                              ImGuiColorEditFlags_NoInputs))
            changed = true;
        ImGui::DragFloat2("Size (px)##splash", s.image.size, 1.0f, 1.0f, 512.0f, "%.0f");
        changed |= ImGui::IsItemDeactivatedAfterEdit();
        ImGui::DragFloat2("Position##splash", s.image.pos, 0.005f, 0.0f, 1.0f, "%.3f");
        changed |= ImGui::IsItemDeactivatedAfterEdit();
        changed |= hudBakeControls(s.image);

        // Structural actions last (they invalidate `s`), each returns cleanly.
        ImGui::Spacing();
        ImGui::BeginDisabled(selectedSplash_ == 0);
        if (ImGui::SmallButton("Move up")) {
            std::swap(splashes[selectedSplash_], splashes[selectedSplash_ - 1]);
            --selectedSplash_;
            ImGui::EndDisabled();
            ImGui::EndChild();
            ImGui::EndChild();
            return true;
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(selectedSplash_ >= (int)splashes.size() - 1);
        if (ImGui::SmallButton("Move down")) {
            std::swap(splashes[selectedSplash_], splashes[selectedSplash_ + 1]);
            ++selectedSplash_;
            ImGui::EndDisabled();
            ImGui::EndChild();
            ImGui::EndChild();
            return true;
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::SmallButton("Change image...")) {
            std::vector<HudImage> tmp;
            const int i = importHudImageInto(tmp);
            if (i >= 0) {
                s.image.imagePath = tmp[i].imagePath;
                changed = true;
            }
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Delete")) {
            splashes.erase(splashes.begin() + selectedSplash_);
            selectedSplash_ = -1;
            ImGui::EndChild();
            ImGui::EndChild();
            return true;
        }
    } else {
        ImGui::TextDisabled("Select a splash on the left, or add one.");
    }
    ImGui::EndChild();  // props
    ImGui::EndChild();  // body
    return changed;
}

void App::drawLoadingScreenWindow() {
    if (!showLoadingEditor_ || !hasProject_) return;

    ImGui::SetNextWindowSize(ImVec2(scaled(780), scaled(760)),
                             ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Loading Screens", &showLoadingEditor_)) {
        ImGui::End();
        return;
    }

    bool changed = false;
    auto& screens = project_.loadingScreens;
    if (selectedLoadingScreen_ >= (int)screens.size()) selectedLoadingScreen_ = -1;

    changed |= drawSplashSection();

    const float previewH = scaled(360);
    ImGui::BeginChild("##ls_top", ImVec2(0, -(previewH + scaled(34))));

    // --- left: screen list -------------------------------------------------
    ImGui::BeginChild("##ls_list", ImVec2(scaled(170), 0), ImGuiChildFlags_Borders);
    if (ImGui::Button("+ New screen", ImVec2(-1, 0))) {
        int counter = 0;
        std::string name;
        for (;;) {
            name = "loading-" + std::to_string(++counter);
            bool taken = false;
            for (const auto& s : screens) taken |= (s.name == name);
            if (!taken) break;
        }
        LoadingScreenDef s;
        s.name = name;
        screens.push_back(std::move(s));
        selectedLoadingScreen_ = (int)screens.size() - 1;
        lsSelKind_ = 0;
        lsSelIdx_ = -1;
        changed = true;
    }
    ImGui::Separator();
    for (int i = 0; i < (int)screens.size(); ++i) {
        ImGui::PushID(i);
        std::string tag = screens[i].name;
        if (project_.defaultLoadingScreen == i) tag += "  [default]";
        if (ImGui::Selectable(tag.c_str(), selectedLoadingScreen_ == i)) {
            selectedLoadingScreen_ = i;
            lsSelKind_ = 0;
            lsSelIdx_ = -1;
        }
        ImGui::PopID();
    }
    if (screens.empty())
        ImGui::TextDisabled("No screens yet.\n\nScenes with none use\n"
                            "the built-in loading.png\non black.");
    ImGui::EndChild();
    ImGui::SameLine();

    if (selectedLoadingScreen_ < 0 || selectedLoadingScreen_ >= (int)screens.size()) {
        ImGui::BeginChild("##ls_none", ImVec2(0, 0));
        ImGui::TextDisabled("Select a loading screen on the left (or create one).");
        ImGui::TextDisabled("\nUse loading screens by:");
        ImGui::BulletText("marking one \"Default at game start\"");
        ImGui::BulletText("picking one per scene in Scene > Preferences");
        ImGui::TextDisabled(
            "\nThe master toggle is Project > Preferences >\n"
            "\"Loading screen between scenes\".");
        ImGui::EndChild();
        ImGui::EndChild();  // ls_top
        if (changed) saveAll("Saved");
        ImGui::End();
        return;
    }
    LoadingScreenDef& ls = screens[selectedLoadingScreen_];

    // --- middle: screen header + element stack -----------------------------
    ImGui::BeginChild("##ls_stack", ImVec2(scaled(210), 0), ImGuiChildFlags_Borders);
    {
        char nameBuf[64];
        std::snprintf(nameBuf, sizeof(nameBuf), "%s", ls.name.c_str());
        ImGui::SetNextItemWidth(-1);
        if (ImGui::InputText("##lsname", nameBuf, sizeof(nameBuf))) {
            // Keep per-scene references pointing at the renamed screen.
            for (SceneData& sc : project_.scenes)
                if (sc.loadingScreen == ls.name) sc.loadingScreen = nameBuf;
            ls.name = nameBuf;
        }
        changed |= ImGui::IsItemDeactivatedAfterEdit();
    }
    if (ImGui::SmallButton("Duplicate")) {
        LoadingScreenDef copy = ls;
        std::string base = copy.name;
        for (int n = 2;; ++n) {
            copy.name = base + "-" + std::to_string(n);
            bool taken = false;
            for (const auto& o : screens) taken |= (o.name == copy.name);
            if (!taken) break;
        }
        screens.push_back(std::move(copy));
        selectedLoadingScreen_ = (int)screens.size() - 1;
        changed = true;
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Delete screen")) {
        const std::string gone = ls.name;
        for (SceneData& sc : project_.scenes)
            if (sc.loadingScreen == gone) sc.loadingScreen.clear();
        if (project_.defaultLoadingScreen == selectedLoadingScreen_)
            project_.defaultLoadingScreen = -1;
        else if (project_.defaultLoadingScreen > selectedLoadingScreen_)
            --project_.defaultLoadingScreen;
        screens.erase(screens.begin() + selectedLoadingScreen_);
        selectedLoadingScreen_ = -1;
        ImGui::EndChild();       // ls_stack
        ImGui::EndChild();       // ls_top
        saveAll("Saved");
        ImGui::End();
        return;
    }
    bool isDefault = project_.defaultLoadingScreen == selectedLoadingScreen_;
    if (ImGui::Checkbox("Default at game start", &isDefault)) {
        project_.defaultLoadingScreen = isDefault ? selectedLoadingScreen_ : -1;
        changed = true;
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Scenes that don't pick a screen use this one.");
    if (ImGui::ColorEdit3("Background", ls.bgColor,
                          ImGuiColorEditFlags_NoInputs))
        changed = true;

    ImGui::SeparatorText("Images");
    for (int i = 0; i < (int)ls.images.size(); ++i) {
        ImGui::PushID(1000 + i);
        const bool sel = lsSelKind_ == 0 && lsSelIdx_ == i;
        std::string label = ls.images[i].name.empty() ? "(image)" : ls.images[i].name;
        if (ImGui::Selectable(label.c_str(), sel)) { lsSelKind_ = 0; lsSelIdx_ = i; }
        ImGui::PopID();
    }
    if (ImGui::SmallButton("+ Import image (PNG)...")) {
        const int i = importHudImageInto(ls.images);
        if (i >= 0) { lsSelKind_ = 0; lsSelIdx_ = i; changed = true; }
    }

    ImGui::SeparatorText("Texts");
    for (int i = 0; i < (int)ls.texts.size(); ++i) {
        ImGui::PushID(2000 + i);
        const bool sel = lsSelKind_ == 1 && lsSelIdx_ == i;
        if (ImGui::Selectable(ls.texts[i].name.c_str(), sel)) {
            lsSelKind_ = 1;
            lsSelIdx_ = i;
        }
        ImGui::PopID();
    }
    if (ImGui::SmallButton("+ Add text")) {
        HudText t;
        int counter = 0;
        for (;;) {
            t.name = "text-" + std::to_string(++counter);
            bool taken = false;
            for (const auto& o : ls.texts) taken |= (o.name == t.name);
            if (!taken) break;
        }
        ls.texts.push_back(std::move(t));
        lsSelKind_ = 1;
        lsSelIdx_ = (int)ls.texts.size() - 1;
        changed = true;
    }

    ImGui::SeparatorText("Progress bars");
    for (int i = 0; i < (int)ls.bars.size(); ++i) {
        ImGui::PushID(3000 + i);
        const bool sel = lsSelKind_ == 2 && lsSelIdx_ == i;
        if (ImGui::Selectable(ls.bars[i].name.c_str(), sel)) {
            lsSelKind_ = 2;
            lsSelIdx_ = i;
        }
        ImGui::PopID();
    }
    if (ImGui::SmallButton("+ Add bar")) {
        LoadingBar b;
        int counter = 0;
        for (;;) {
            b.name = "bar-" + std::to_string(++counter);
            bool taken = false;
            for (const auto& o : ls.bars) taken |= (o.name == b.name);
            if (!taken) break;
        }
        ls.bars.push_back(std::move(b));
        lsSelKind_ = 2;
        lsSelIdx_ = (int)ls.bars.size() - 1;
        changed = true;
    }
    ImGui::EndChild();  // ls_stack
    ImGui::SameLine();

    // --- right: selected element properties --------------------------------
    ImGui::BeginChild("##ls_props", ImVec2(0, 0));
    if (lsSelKind_ == 0 && lsSelIdx_ >= 0 && lsSelIdx_ < (int)ls.images.size()) {
        HudImage& h = ls.images[lsSelIdx_];
        ImGui::SeparatorText(h.name.empty() ? "Image" : h.name.c_str());
        ImGui::DragFloat2("Position##lsimg", h.pos, 0.005f, 0.0f, 1.0f, "%.3f");
        changed |= ImGui::IsItemDeactivatedAfterEdit();
        ImGui::DragFloat2("Size (px)##lsimg", h.size, 1.0f, 1.0f, 512.0f, "%.0f");
        changed |= ImGui::IsItemDeactivatedAfterEdit();
        changed |= hudBakeControls(h);
        ImGui::Spacing();
        if (ImGui::Button("Delete image")) {
            ls.images.erase(ls.images.begin() + lsSelIdx_);
            lsSelIdx_ = -1;
            changed = true;
        }
    } else if (lsSelKind_ == 1 && lsSelIdx_ >= 0 &&
               lsSelIdx_ < (int)ls.texts.size()) {
        HudText& t = ls.texts[lsSelIdx_];
        ImGui::SeparatorText(t.name.c_str());
        {
            char nameBuf[64];
            std::snprintf(nameBuf, sizeof(nameBuf), "%s", t.name.c_str());
            ImGui::SetNextItemWidth(scaled(160));
            if (ImGui::InputText("Name##lstext", nameBuf, sizeof(nameBuf)))
                t.name = nameBuf;
            changed |= ImGui::IsItemDeactivatedAfterEdit();
        }
        {
            char textBuf[512];
            std::snprintf(textBuf, sizeof(textBuf), "%s", t.text.c_str());
            if (ImGui::InputTextMultiline("Text##lstext", textBuf, sizeof(textBuf),
                                          ImVec2(-1.0f, scaled(70))))
                t.text = textBuf;
            changed |= ImGui::IsItemDeactivatedAfterEdit();
        }
        changed |= fontCombo(t.fontPath);
        ImGui::SetNextItemWidth(scaled(120));
        if (ImGui::DragInt("Font size##lstext", &t.size, 0.2f, 8, 48, "%d px"))
            t.size = t.size < 8 ? 8 : t.size > 48 ? 48 : t.size;
        changed |= ImGui::IsItemDeactivatedAfterEdit();
        if (ImGui::ColorEdit3("Color##lstext", t.color, ImGuiColorEditFlags_NoInputs))
            changed = true;
        ImGui::DragFloat2("Position##lstext", t.pos, 0.005f, 0.0f, 1.0f, "%.3f");
        changed |= ImGui::IsItemDeactivatedAfterEdit();
        if (ImGui::Checkbox("Drop shadow##lstext", &t.shadow)) changed = true;
        ImGui::Spacing();
        if (ImGui::Button("Delete text")) {
            ls.texts.erase(ls.texts.begin() + lsSelIdx_);
            lsSelIdx_ = -1;
            changed = true;
        }
    } else if (lsSelKind_ == 2 && lsSelIdx_ >= 0 &&
               lsSelIdx_ < (int)ls.bars.size()) {
        LoadingBar& b = ls.bars[lsSelIdx_];
        ImGui::SeparatorText(b.name.c_str());
        {
            char nameBuf[64];
            std::snprintf(nameBuf, sizeof(nameBuf), "%s", b.name.c_str());
            ImGui::SetNextItemWidth(scaled(160));
            if (ImGui::InputText("Name##lsbar", nameBuf, sizeof(nameBuf)))
                b.name = nameBuf;
            changed |= ImGui::IsItemDeactivatedAfterEdit();
        }
        changed |= loadingBarControls(b);
        ImGui::Spacing();
        if (ImGui::Button("Delete bar")) {
            ls.bars.erase(ls.bars.begin() + lsSelIdx_);
            lsSelIdx_ = -1;
            changed = true;
        }
    } else {
        ImGui::TextDisabled("Select an element on the left,\nor add one.");
    }
    ImGui::EndChild();  // ls_props

    ImGui::EndChild();  // ls_top

    // --- preview + progress slider -----------------------------------------
    ImGui::SetNextItemWidth(scaled(240));
    ImGui::SliderFloat("Preview progress", &lsPreviewProgress_, 0.0f, 1.0f, "%.2f");
    ImGui::SameLine();
    ImGui::TextDisabled("(simulated load fraction)");
    drawLoadingPreview(ls, lsPreviewProgress_);

    if (changed) saveAll("Saved");  // project-wide data, outside undo
    ImGui::End();
}

void App::importMusicTrack() {
    const std::string src = pickWavFile();
    if (src.empty()) return;

    // The patched song player reads the WAV header (PCM 8/16-bit, mono or
    // stereo, standard rates - audsrv resamples on the IOP). Float, 24-bit
    // and compressed WAVs still play as noise, so flag those before import.
    int audioFormat = 0, channels = 0, rate = 0, bits = 0;
    if (!readWavFormat(src, audioFormat, channels, rate, bits)) {
        statusMessage_ = "Music import failed: not a readable WAV file";
        return;
    }
    // Formats the song player cannot stream (float/24/32-bit, compressed,
    // out-of-range rates) are converted in place after the copy. Rates above
    // 22050 Hz stream fine in PCSX2 but starve on a real console over the
    // network deploy (the byte rate doubles the per-chunk fread budget AND
    // the 36 MHz IOP resampler load, which shares the CPU with ps2link's
    // network stack) - playback drags into slow motion. Convert those too.
    std::string formatWarning;
    if (audioFormat != 1) {
        formatWarning = audioFormat == 3 ? "32-bit float WAV" : "compressed WAV";
    } else if (bits != 8 && bits != 16) {
        formatWarning = std::to_string(bits) + "-bit PCM";
    } else if (channels > 2) {
        formatWarning = std::to_string(channels) + "-channel WAV";
    } else if (rate < 11025 || rate > 48000) {
        formatWarning = std::to_string(rate) + " Hz sample rate";
    }

    const std::filesystem::path srcPath(src);
    const std::string fileName = sanitizeAssetName(srcPath.filename().string());
    const std::filesystem::path destDir =
        std::filesystem::path(project_.dir) / "res" / "audio";
    std::error_code ec;
    std::filesystem::create_directories(destDir, ec);
    std::filesystem::copy_file(srcPath, destDir / fileName,
                               std::filesystem::copy_options::overwrite_existing, ec);
    if (ec) {
        statusMessage_ = "Music import failed: " + ec.message();
        return;
    }

    std::string note = " (" + std::to_string(rate) + " Hz " + std::to_string(bits) +
                       "-bit " + (channels == 1 ? "mono" : "stereo") + ")";
    if (!formatWarning.empty()) {
        std::string convErr;
        const int targetRate = (rate >= 11025 && rate <= 22050) ? rate : 22050;
        if (wavconvert::convertTo16(destDir / fileName, targetRate, convErr))
            note = " - " + formatWarning + ", converted to 16-bit PCM " +
                   std::to_string(targetRate) + " Hz";
        else
            note = " - WARNING: " + formatWarning + " is not playable on PS2 and the "
                   "converter failed (" + convErr + "); it will play as noise";
    }
    wavIssueCache_.clear();

    const std::string relPath = "res/audio/" + fileName;
    bool exists = false;
    for (const std::string& m : project_.music) exists |= (m == relPath);
    if (!exists) project_.music.push_back(relPath);
    saveAll(("Imported " + fileName + note).c_str());
}

// Removing an audio track: clear every flow node that referenced it (in all
// scenes and object graphs) so nothing keeps playing it, and delete the
// file from res/.
static void removeAudioTrack(Project& p, const std::string& relPath, bool music) {
    for (SceneData& scene : p.scenes)
    for (SceneObject& o : scene.objects) {
        for (FlowNode& n : o.flowGraph.nodes) {
            const FlowNodeType* t = flowNodeType(n.type);
            if (!t || n.str != relPath) continue;
            if ((music && t->strKind == FlowParamKind::MusicTrack) ||
                (!music && t->strKind == FlowParamKind::SoundTrack))
                n.str.clear();
        }
    }
    std::error_code ec;
    std::filesystem::remove(std::filesystem::path(p.dir) / relPath, ec);
}

// Mirrors res/audio and res/sfx into the project lists, so assets can be
// dropped into the folders by hand (Explorer) instead of going through the
// import dialogs. New files are added, entries whose file vanished are
// removed like a manual delete (flow-node references cleared). Runs on
// project open and from the Rescan buttons.
void App::rescanAssets(bool announce) {
    if (!hasProject_) return;
    int added = 0, removed = 0;

    auto scan = [&](const char* sub, std::vector<std::string>& list, bool music) {
        for (size_t i = list.size(); i-- > 0;) {
            std::error_code ec;
            if (!std::filesystem::exists(std::filesystem::path(project_.dir) / list[i],
                                         ec)) {
                removeAudioTrack(project_, list[i], music);
                list.erase(list.begin() + i);
                ++removed;
            }
        }
        std::error_code ec;
        const std::filesystem::path dir =
            std::filesystem::path(project_.dir) / "res" / sub;
        if (!std::filesystem::exists(dir, ec)) return;
        // recursive: subfolders are fine (res/sfx/steps/wood.wav)
        for (const auto& e : std::filesystem::recursive_directory_iterator(dir, ec)) {
            if (!e.is_regular_file()) continue;
            std::string ext = e.path().extension().string();
            for (char& c : ext) c = (char)tolower((unsigned char)c);
            if (ext != ".wav") continue;
            const std::string rel =
                std::string("res/") + sub + "/" +
                std::filesystem::relative(e.path(), dir, ec).generic_string();
            bool known = false;
            for (const std::string& s : list) known |= (s == rel);
            if (!known) {
                list.push_back(rel);
                ++added;
            }
        }
    };
    scan("audio", project_.music, true);
    scan("sfx", project_.sounds, false);
    wavIssueCache_.clear();

    if (added || removed) {
        const std::string status = "Assets rescan: " + std::to_string(added) +
                                   " added, " + std::to_string(removed) + " removed";
        commitChange();
        statusMessage_ = status;
    } else if (announce) {
        statusMessage_ = "Assets rescan: no changes";
    }
}

// Cached WAV-format check for the Music/Sounds lists (file IO once per file,
// not every frame). Returns "" when the file is fine for its purpose.
const std::string& App::wavIssue(const std::string& relPath, bool sfx) {
    const std::string key = (sfx ? "s:" : "m:") + relPath;
    auto it = wavIssueCache_.find(key);
    if (it != wavIssueCache_.end()) return it->second;

    std::string issue;
    int audioFormat = 0, channels = 0, rate = 0, bits = 0;
    const std::string full =
        (std::filesystem::path(project_.dir) / relPath).string();
    if (!readWavFormat(full, audioFormat, channels, rate, bits)) {
        issue = "unreadable WAV";
    } else if (sfx) {
        if (audioFormat != 1 || rate != 22050 || bits != 16)
            issue = std::string(audioFormat != 1 ? "non-PCM" : "") +
                    (audioFormat == 1 ? std::to_string(bits) + "-bit " +
                                            std::to_string(rate) + " Hz"
                                      : "") +
                    " - adpenc needs 16-bit 22050 Hz";
    } else {
        if (audioFormat != 1 || (bits != 8 && bits != 16) || channels > 2 ||
            rate < 11025 || rate > 48000)
            issue = "not streamable (needs 8/16-bit PCM, mono/stereo, 11-48 kHz)";
        else if (rate != 11025 && rate != 12000 && rate != 22050 && rate != 24000 &&
                 rate != 32000 && rate != 44100 && rate != 48000)
            issue = std::to_string(rate) +
                    " Hz - audsrv has no upsampler for this rate (supported: "
                    "11025/12000/22050/24000/32000/44100/48000); use the PS2 "
                    "build controls below or Convert";
    }
    return wavIssueCache_.emplace(key, std::move(issue)).first->second;
}

void App::drawMusicSection() {
    if (!ImGui::CollapsingHeader("Music")) return;

    if (ImGui::SmallButton("Import WAV...##music")) importMusicTrack();
    ImGui::SameLine();
    if (ImGui::SmallButton("Rescan##music")) rescanAssets(true);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Pick up WAVs dropped by hand into res/audio and res/sfx.");
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("16-bit PCM WAV, mono or stereo, 11-48 kHz\n"
                          "(22050 Hz stereo recommended). Unplayable formats are\n"
                          "converted at import; hand-dropped files get a Convert\n"
                          "button. Play via Flow Graph: On Start -> Play Music.");

    for (int i = 0; i < (int)project_.music.size(); ++i) {
        const std::string name = std::filesystem::path(project_.music[i]).filename().string();
        ImGui::PushID(i);
        ImGui::Bullet();
        ImGui::SameLine();
        ImGui::TextUnformatted(name.c_str());
        const std::string& issue = wavIssue(project_.music[i], false);
        if (!issue.empty()) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.2f, 1.0f), "!");
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", issue.c_str());
            ImGui::SameLine();
            if (ImGui::SmallButton("Convert")) {
                std::string err;
                const std::filesystem::path full =
                    std::filesystem::path(project_.dir) / project_.music[i];
                statusMessage_ = wavconvert::convertTo16(full, 22050, err)
                                     ? name + " converted to 16-bit PCM 22050 Hz"
                                     : name + ": conversion failed - " + err;
                wavIssueCache_.clear();
            }
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("x"))
            requestAssetDelete(PendingAssetDelete::Music, project_.music[i], name);

        // Build-time conversion knobs: applied to the bin/audio copy after
        // every build (the res/ source stays untouched). Only rates audsrv
        // has an upsampler for (find_upsampler fails on anything else -
        // 16000 was offered here once and is NOT supported). 48000 is
        // special: SPU2-native, the IOP does a plain channel demux with zero
        // resampling arithmetic - the lever when the 36 MHz IOP is the
        // bottleneck (network deploys share it with the ps2link stack);
        // lower rates are the lever when the network itself cannot keep up.
        {
            auto it = project_.musicBuild.find(project_.music[i]);
            Project::MusicBuildOpt opt =
                it != project_.musicBuild.end() ? it->second : Project::MusicBuildOpt{};
            int rateIdx = opt.rate == 48000 ? 1 : opt.rate == 32000 ? 2
                          : opt.rate == 22050 ? 3 : opt.rate == 11025 ? 4 : 0;
            // Labels stay short so the mono checkbox on the same line fits
            // inside the (narrow) Project panel - a wider combo pushed it
            // past the clip rect and it silently disappeared.
            const char* rateNames[] = {"keep rate", "48000 Hz", "32000 Hz",
                                       "22050 Hz", "11025 Hz"};
            ImGui::Indent();
            ImGui::TextDisabled("PS2 build:");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(110.0f);
            bool edited = ImGui::Combo("##mbrate", &rateIdx, rateNames, 5);
            ImGui::SameLine();
            edited |= ImGui::Checkbox("mono##mb", &opt.mono);
            if (ImGui::IsItemHovered() || (ImGui::IsItemHovered(ImGuiHoveredFlags_None)))
                ImGui::SetTooltip(
                    "Converts the bin\\ copy after every build (source WAV stays\n"
                    "untouched). For network deploys try 48000 Hz stereo first:\n"
                    "SPU2-native, so the IOP skips resampling entirely (that CPU\n"
                    "also runs the ps2link network stack). Drop the rate only\n"
                    "when the network itself cannot keep up.");
            ImGui::Unindent();
            if (edited) {
                opt.rate = rateIdx == 1 ? 48000 : rateIdx == 2 ? 32000
                           : rateIdx == 3 ? 22050 : rateIdx == 4 ? 11025 : 0;
                if (opt.rate == 0 && !opt.mono)
                    project_.musicBuild.erase(project_.music[i]);
                else
                    project_.musicBuild[project_.music[i]] = opt;
                saveAll("Music build settings saved - rebuild to apply");
            }
        }
        ImGui::PopID();
    }
    if (project_.music.empty()) ImGui::TextDisabled("No music tracks.");
}

void App::importSoundEffect() {
    const std::string src = pickWavFile();
    if (src.empty()) return;

    // adpenc (runs in the toolchain container at build) expects 16-bit PCM
    // 22 kHz. Anything else is converted in place right after the copy.
    int audioFormat = 0, channels = 0, rate = 0, bits = 0;
    if (!readWavFormat(src, audioFormat, channels, rate, bits)) {
        statusMessage_ = "Sound import failed: not a readable WAV file";
        return;
    }

    const std::filesystem::path srcPath(src);
    const std::string fileName = sanitizeAssetName(srcPath.filename().string());
    const std::filesystem::path destDir = std::filesystem::path(project_.dir) / "res" / "sfx";
    std::error_code ec;
    std::filesystem::create_directories(destDir, ec);
    std::filesystem::copy_file(srcPath, destDir / fileName,
                               std::filesystem::copy_options::overwrite_existing, ec);
    if (ec) {
        statusMessage_ = "Sound import failed: " + ec.message();
        return;
    }

    std::string warning;
    if (audioFormat != 1 || rate != 22050 || bits != 16) {
        std::string convErr;
        if (!wavconvert::convertTo16(destDir / fileName, 22050, convErr))
            warning = "adpenc expects 16-bit PCM 22050 Hz and the converter "
                      "failed (" + convErr + ") - the sound may play wrong";
    }
    wavIssueCache_.clear();

    // Warn if the sample cannot fit SPU2's sample RAM - sound emitters load it
    // there whole, so an oversized one-shot silently plays nothing in-game.
    // Sized from the converted file, not the source.
    std::error_code szEc;
    const uintmax_t wavBytes = std::filesystem::file_size(destDir / fileName, szEc);
    if (!szEc && estimateAdpcmBytes(wavBytes) > kSpu2SampleBudgetBytes) {
        const std::string tooBig =
            "~" + std::to_string(estimateAdpcmBytes(wavBytes) / (1024 * 1024)) +
            " MB ADPCM exceeds SPU2's ~2 MB - too long for a sound emitter; use a "
            "short clip (mono 22050 Hz) or the Music system for full tracks";
        warning = warning.empty() ? tooBig : warning + "; " + tooBig;
    }

    const std::string relPath = "res/sfx/" + fileName;
    bool exists = false;
    for (const std::string& s : project_.sounds) exists |= (s == relPath);
    if (!exists) project_.sounds.push_back(relPath);
    const std::string status =
        (audioFormat != 1 || rate != 22050 || bits != 16) && warning.empty()
            ? "Imported " + fileName + " - converted to 16-bit PCM 22050 Hz"
        : warning.empty() ? "Imported " + fileName
                          : "Imported " + fileName + " - WARNING: " + warning;
    saveAll(status.c_str());
}

void App::drawSoundsSection() {
    if (!ImGui::CollapsingHeader("Sounds")) return;

    if (ImGui::SmallButton("Import WAV...##sfx")) importSoundEffect();
    ImGui::SameLine();
    if (ImGui::SmallButton("Rescan##sfx")) rescanAssets(true);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Pick up WAVs dropped by hand into res/audio and res/sfx.");
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Short one-shot SFX for sound emitters and the Flow Graph\n"
                          "Play Sound node. Best as mono 16-bit 22050 Hz WAV.\n"
                          "All sounds are loaded into SPU2's ~2 MB sample RAM at\n"
                          "scene start, so keep them short - use Music for full tracks.");

    uintmax_t totalAdpcm = 0;
    for (int i = 0; i < (int)project_.sounds.size(); ++i) {
        const std::string name =
            std::filesystem::path(project_.sounds[i]).filename().string();
        std::error_code ec;
        const uintmax_t wavBytes = std::filesystem::file_size(
            std::filesystem::path(project_.dir) / project_.sounds[i], ec);
        const uintmax_t adpcm = ec ? 0 : estimateAdpcmBytes(wavBytes);
        totalAdpcm += adpcm;

        ImGui::PushID(i + 1000);
        ImGui::Bullet();
        ImGui::SameLine();
        ImGui::TextUnformatted(name.c_str());
        const std::string& issue = wavIssue(project_.sounds[i], true);
        if (!issue.empty()) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.2f, 1.0f), "!");
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", issue.c_str());
            ImGui::SameLine();
            if (ImGui::SmallButton("Convert")) {
                std::string err;
                const std::filesystem::path full =
                    std::filesystem::path(project_.dir) / project_.sounds[i];
                statusMessage_ = wavconvert::convertTo16(full, 22050, err)
                                     ? name + " converted to 16-bit PCM 22050 Hz"
                                     : name + ": conversion failed - " + err;
                wavIssueCache_.clear();
            }
        }
        ImGui::SameLine();
        if (!ec) {
            const bool big = adpcm > kSpu2SampleBudgetBytes;
            const char* fmt = adpcm >= 1024 * 1024 ? "(~%.1f MB)" : "(~%.0f KB)";
            const double val = adpcm >= 1024 * 1024 ? adpcm / (1024.0 * 1024.0)
                                                    : adpcm / 1024.0;
            if (big)
                ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.3f, 1.0f), fmt, val);
            else
                ImGui::TextDisabled(fmt, val);
            ImGui::SameLine();
        }
        if (ImGui::SmallButton("x"))
            requestAssetDelete(PendingAssetDelete::Sound, project_.sounds[i], name);
        ImGui::PopID();
    }
    if (project_.sounds.empty()) {
        ImGui::TextDisabled("No sound effects.");
    } else {
        const double totalMb = totalAdpcm / (1024.0 * 1024.0);
        if (totalAdpcm > kSpu2SampleBudgetBytes)
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.3f, 1.0f),
                               "SPU2 sample RAM: ~%.1f / 2.0 MB - over budget, some "
                               "sounds will not play", totalMb);
        else
            ImGui::TextDisabled("SPU2 sample RAM: ~%.1f / 2.0 MB", totalMb);
    }
}

// Custom values persisted in memory card save slots. Flow graph "Save"
// nodes (Set/Add/Value At Least) reference them by name; the defaults are
// the fresh-game state.
void App::drawSaveDataSection() {
    if (!ImGui::CollapsingHeader("Save data")) return;

    if (ImGui::SmallButton("+ Value")) {
        // unique default name: value-1, value-2, ...
        int counter = 0;
        std::string name;
        for (;;) {
            name = "value-" + std::to_string(++counter);
            bool taken = false;
            for (const auto& v : project_.saveValues) taken |= (v.name == name);
            if (!taken) break;
        }
        project_.saveValues.push_back(SaveValue{name, 0.0f});
        saveAll("Saved");
    }
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Named values stored in every memory card save slot\n"
                          "(coins, progress flags...). Read/write them with the\n"
                          "Flow Graph \"Save\" nodes; the default is the value\n"
                          "a fresh game starts with.");

    bool changed = false;
    for (int i = 0; i < (int)project_.saveValues.size(); ++i) {
        SaveValue& v = project_.saveValues[i];
        ImGui::PushID(i + 2000);
        char nameBuf[64];
        std::snprintf(nameBuf, sizeof(nameBuf), "%s", v.name.c_str());
        ImGui::SetNextItemWidth(140.0f);
        if (ImGui::InputText("##name", nameBuf, sizeof(nameBuf))) {
            // keep flow nodes pointing at the renamed value
            for (SceneData& sc : project_.scenes)
                for (SceneObject& o : sc.objects)
                    for (FlowNode& fn : o.flowGraph.nodes) {
                        const FlowNodeType* ft = flowNodeType(fn.type);
                        if (ft && ft->strKind == FlowParamKind::SaveValue &&
                            fn.str == v.name)
                            fn.str = nameBuf;
                    }
            v.name = nameBuf;
        }
        changed |= ImGui::IsItemDeactivatedAfterEdit();
        ImGui::SameLine();
        ImGui::SetNextItemWidth(90.0f);
        ImGui::DragFloat("##default", &v.value, 0.1f, 0.0f, 0.0f, "%.2f");
        changed |= ImGui::IsItemDeactivatedAfterEdit();
        ImGui::SameLine();
        if (ImGui::SmallButton("x")) {
            // clear flow nodes that referenced the removed value
            for (SceneData& sc : project_.scenes)
                for (SceneObject& o : sc.objects)
                    for (FlowNode& fn : o.flowGraph.nodes) {
                        const FlowNodeType* ft = flowNodeType(fn.type);
                        if (ft && ft->strKind == FlowParamKind::SaveValue &&
                            fn.str == v.name)
                            fn.str.clear();
                    }
            project_.saveValues.erase(project_.saveValues.begin() + i);
            changed = true;
            ImGui::PopID();
            break;
        }
        ImGui::PopID();
    }
    if (project_.saveValues.empty()) ImGui::TextDisabled("No save values.");

    // Text values (fixed 32-byte slots in the save payload - keep them short)
    ImGui::Separator();
    if (ImGui::SmallButton("+ Text")) {
        int counter = 0;
        std::string name;
        for (;;) {
            name = "text-" + std::to_string(++counter);
            bool taken = false;
            for (const auto& v : project_.saveTexts) taken |= (v.name == name);
            if (!taken) break;
        }
        project_.saveTexts.push_back(SaveTextValue{name, ""});
        saveAll("Saved");
    }
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Named texts stored in every memory card save slot\n"
                          "(player name, chosen path...). Read/write them with\n"
                          "the Flow Graph \"Save\" nodes (Set/Get Save Text);\n"
                          "31 characters fit in a slot.");
    for (int i = 0; i < (int)project_.saveTexts.size(); ++i) {
        SaveTextValue& v = project_.saveTexts[i];
        ImGui::PushID(i + 3000);
        char nameBuf[64];
        std::snprintf(nameBuf, sizeof(nameBuf), "%s", v.name.c_str());
        ImGui::SetNextItemWidth(140.0f);
        if (ImGui::InputText("##name", nameBuf, sizeof(nameBuf))) {
            // keep flow nodes pointing at the renamed value
            for (SceneData& sc : project_.scenes)
                for (SceneObject& o : sc.objects)
                    for (FlowNode& fn : o.flowGraph.nodes) {
                        const FlowNodeType* ft = flowNodeType(fn.type);
                        if (ft && ft->strKind == FlowParamKind::SaveText &&
                            fn.str == v.name)
                            fn.str = nameBuf;
                    }
            v.name = nameBuf;
        }
        changed |= ImGui::IsItemDeactivatedAfterEdit();
        ImGui::SameLine();
        ImGui::SetNextItemWidth(90.0f);
        char valBuf[32];  // SAVE_TEXT_LEN - what a save slot can hold
        std::snprintf(valBuf, sizeof(valBuf), "%s", v.value.c_str());
        if (ImGui::InputText("##default", valBuf, sizeof(valBuf))) v.value = valBuf;
        changed |= ImGui::IsItemDeactivatedAfterEdit();
        ImGui::SameLine();
        if (ImGui::SmallButton("x")) {
            // clear flow nodes that referenced the removed value
            for (SceneData& sc : project_.scenes)
                for (SceneObject& o : sc.objects)
                    for (FlowNode& fn : o.flowGraph.nodes) {
                        const FlowNodeType* ft = flowNodeType(fn.type);
                        if (ft && ft->strKind == FlowParamKind::SaveText &&
                            fn.str == v.name)
                            fn.str.clear();
                    }
            project_.saveTexts.erase(project_.saveTexts.begin() + i);
            changed = true;
            ImGui::PopID();
            break;
        }
        ImGui::PopID();
    }
    if (project_.saveTexts.empty()) ImGui::TextDisabled("No save texts.");
    // commitChange: renames/deletes touch flow graphs (part of undo snapshots)
    if (changed) commitChange();
}

// Files dropped from Explorer onto the editor window. PNGs are copied into
// res/hud; with the Menu Editor open they also attach to the selected menu
// (the dialog-free import path - native file dialogs wedge on some setups).
void App::handleFileDrop(int count, const char** paths) {
    if (!hasProject_) return;

    const bool menuTarget = showMenusEditor_ && selectedMenu_ >= 0 &&
                            selectedMenu_ < (int)project_.menus.size();
    int copied = 0, attached = 0, fonts = 0, skipped = 0;
    for (int i = 0; i < count; ++i) {
        const std::filesystem::path src(paths[i]);
        std::string ext = src.extension().string();
        for (char& c : ext) c = (char)tolower((unsigned char)c);
        const bool isPng = ext == ".png";
        const bool isFont = ext == ".ttf" || ext == ".otf";
        if (!isPng && !isFont) {
            ++skipped;
            continue;
        }
        const std::string fileName = sanitizeAssetName(src.filename().string());
        const std::filesystem::path destDir =
            std::filesystem::path(project_.dir) / "res" / (isFont ? "fonts" : "hud");
        std::error_code ec;
        std::filesystem::create_directories(destDir, ec);
        std::filesystem::copy_file(src, destDir / fileName,
                                   std::filesystem::copy_options::overwrite_existing,
                                   ec);
        if (ec) {
            statusMessage_ = "Drop import failed: " + ec.message();
            continue;
        }
        if (isFont) {
            ++fonts;
            if (menuTarget) {
                project_.menus[selectedMenu_].fontPath =
                    "res/fonts/" + fileName;
            }
            continue;
        }
        ++copied;
        if (menuTarget) {
            MenuImage img;
            img.path = "res/hud/" + fileName;
            project_.menus[selectedMenu_].images.push_back(std::move(img));
            ++attached;
        }
    }

    if ((attached > 0 || fonts > 0) && menuTarget) {
        commitChange();
        std::string what;
        if (attached > 0) what = std::to_string(attached) + " image(s)";
        if (fonts > 0)
            what += (what.empty() ? "" : " + ") + std::string("font");
        statusMessage_ = "Added " + what + " to menu \"" +
                         project_.menus[selectedMenu_].name + "\"";
    } else if (copied > 0 || fonts > 0) {
        statusMessage_ = "Copied into res/ - attach in the Menu Editor (images: "
                         "Images list, fonts: Font combo) or Tools > UI Editor";
    } else if (skipped > 0) {
        statusMessage_ = "Drop: PNG images and TTF/OTF fonts are handled here";
    }
}

// Resolve-style color wheel (trackball). The puck edits the zero-mean part
// of the three channels around their common level: hue direction = which
// channels move apart, radius = how far. The common (master) level is NOT on
// the wheel - the caller pairs it with a master slider - so puck <-> rgb is
// an exact roundtrip (a zero-mean 3-vector has exactly the wheel's 2 DOF).
// Mutates rgb live while dragging; returns true when an edit FINISHED
// (release / double-click reset) so the caller commits once per gesture.
static bool gradingWheel(const char* id, float* rgb, float lo, float hi,
                         float wheelRange, float scale) {
    constexpr float kTau = 6.28318530f;
    const float radius = 54.0f * scale;
    ImGui::PushID(id);

    const ImVec2 p = ImGui::GetCursorScreenPos();
    const ImVec2 c(p.x + radius, p.y + radius);
    ImGui::InvisibleButton("wheel", ImVec2(radius * 2, radius * 2));
    bool finished = ImGui::IsItemDeactivated();

    // Channel directions on the disc: R up, G lower-left, B lower-right
    // (vectorscope-like). u/v are a linear basis for zero-mean offsets.
    const float ang[3] = {0.25f * kTau, 0.5833333f * kTau, 0.9166667f * kTau};
    float u[3], v[3];
    for (int i = 0; i < 3; ++i) {
        u[i] = std::cos(ang[i]);
        v[i] = std::sin(ang[i]);
    }
    auto clampf = [](float x, float a, float b) {
        return x < a ? a : (x > b ? b : x);
    };

    float master = (rgb[0] + rgb[1] + rgb[2]) / 3.0f;
    if (ImGui::IsItemActive()) {
        const ImVec2 m = ImGui::GetIO().MousePos;
        const float reach = radius - 8.0f * scale;
        float px = (m.x - c.x) / reach;
        float py = -(m.y - c.y) / reach;  // screen y is down
        const float r = std::sqrt(px * px + py * py);
        if (r > 1.0f) px /= r, py /= r;
        for (int i = 0; i < 3; ++i)
            rgb[i] = clampf(master + wheelRange * (px * u[i] + py * v[i]), lo, hi);
    } else if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
        for (int i = 0; i < 3; ++i) rgb[i] = clampf(master, lo, hi);
        finished = true;
    }

    // Puck position from the current value (inverse of the mapping above;
    // sum(u^2) = sum(v^2) = 3/2 and the u/v bases are orthogonal).
    float px = 0.0f, py = 0.0f;
    master = (rgb[0] + rgb[1] + rgb[2]) / 3.0f;
    for (int i = 0; i < 3; ++i) {
        px += (rgb[i] - master) * u[i];
        py += (rgb[i] - master) * v[i];
    }
    px *= (2.0f / 3.0f) / wheelRange;
    py *= (2.0f / 3.0f) / wheelRange;
    const float pr = std::sqrt(px * px + py * py);
    if (pr > 1.0f) px /= pr, py /= pr;

    // Hue disc: triangle fan with a dark neutral center fading to the hue at
    // the rim (per-vertex colors - ImDrawList prims, white-pixel UV).
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const int SEG = 48;
    const ImVec2 uvWhite = ImGui::GetFontTexUvWhitePixel();
    const ImU32 colCenter = IM_COL32(48, 48, 52, 255);
    dl->PrimReserve(SEG * 3, SEG * 3);
    for (int i = 0; i < SEG; ++i) {
        const float a0 = (float)i / SEG * kTau, a1 = (float)(i + 1) / SEG * kTau;
        auto rim = [&](float a) {
            // hue 0 (red) at the R direction, increasing toward G then B
            float hue = a / kTau - 0.25f;
            hue -= std::floor(hue);
            float r, g, b;
            ImGui::ColorConvertHSVtoRGB(hue, 0.8f, 0.85f, r, g, b);
            return IM_COL32((int)(r * 255), (int)(g * 255), (int)(b * 255), 255);
        };
        const ImVec2 p0(c.x + radius * std::cos(a0), c.y - radius * std::sin(a0));
        const ImVec2 p1(c.x + radius * std::cos(a1), c.y - radius * std::sin(a1));
        dl->PrimVtx(c, uvWhite, colCenter);
        dl->PrimVtx(p0, uvWhite, rim(a0));
        dl->PrimVtx(p1, uvWhite, rim(a1));
    }
    dl->AddCircle(c, radius, IM_COL32(0, 0, 0, 110), 0, 1.5f);
    dl->AddCircleFilled(c, 2.0f * scale, IM_COL32(160, 160, 160, 160));

    // Puck: white dot with a dark outline (like Resolve's trackball)
    const ImVec2 puck(c.x + px * (radius - 8.0f * scale),
                      c.y - py * (radius - 8.0f * scale));
    dl->AddCircleFilled(puck, 6.0f * scale, IM_COL32(235, 235, 235, 255));
    dl->AddCircle(puck, 6.5f * scale, IM_COL32(20, 20, 20, 200), 0, 1.5f);

    if (ImGui::IsItemHovered() && !ImGui::IsItemActive())
        ImGui::SetTooltip("Drag to tint, double-click to reset");

    ImGui::PopID();
    return finished;
}

// Color Grading window (Tools > Color Grading): preset list on the left,
// DaVinci-style controls for the selected preset on the right. The viewport
// previews the selected preset live with the same quantized math the PS2 GS
// runs, so what you see is what the console draws.
void App::drawGradingWindow() {
    if (!showGradingEditor_ || !hasProject_) return;

    ImGui::SetNextWindowSize(ImVec2(scaled(560), scaled(520)),
                             ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Color Grading", &showGradingEditor_)) {
        ImGui::End();
        return;
    }

    bool changed = false;

    // --- left: preset list -------------------------------------------------
    ImGui::BeginChild("##grading_list", ImVec2(scaled(170), 0), ImGuiChildFlags_Borders);
    if (ImGui::Button("+ New preset", ImVec2(-1, 0))) {
        int counter = 0;
        std::string name;
        for (;;) {
            name = "look-" + std::to_string(++counter);
            bool taken = false;
            for (const auto& g : project_.gradings) taken |= (g.name == name);
            if (!taken) break;
        }
        ColorGradingPreset g;
        g.name = name;
        project_.gradings.push_back(std::move(g));
        selectedGrading_ = (int)project_.gradings.size() - 1;
        changed = true;
    }
    ImGui::Separator();
    for (int i = 0; i < (int)project_.gradings.size(); ++i) {
        ImGui::PushID(i);
        std::string tag = project_.gradings[i].name;
        if (project_.defaultGrading == i) tag += "  [default]";
        if (ImGui::Selectable(tag.c_str(), selectedGrading_ == i))
            selectedGrading_ = i;
        ImGui::PopID();
    }
    if (project_.gradings.empty())
        ImGui::TextDisabled("No presets yet.\nA preset is a full-screen\n"
                            "look (a few GS sprites\nper frame - no EE cost).");
    ImGui::EndChild();

    ImGui::SameLine();

    // --- right: selected preset editor --------------------------------------
    ImGui::BeginChild("##grading_edit", ImVec2(0, 0));
    if (selectedGrading_ < 0 || selectedGrading_ >= (int)project_.gradings.size()) {
        ImGui::TextDisabled("Select a preset on the left (or create one).");
        ImGui::TextDisabled("\nApply presets in the game with:");
        ImGui::BulletText("\"Default at game start\" on a preset");
        ImGui::BulletText("the Set Color Grading flow node (category \"Scene\")");
        ImGui::EndChild();
        ImGui::End();
        return;
    }
    ColorGradingPreset& g = project_.gradings[selectedGrading_];

    char nameBuf[64];
    std::snprintf(nameBuf, sizeof(nameBuf), "%s", g.name.c_str());
    ImGui::SetNextItemWidth(scaled(180.0f));
    if (ImGui::InputText("Name", nameBuf, sizeof(nameBuf))) {
        // keep Set Color Grading flow nodes pointing here
        for (SceneData& sc : project_.scenes)
            for (SceneObject& o : sc.objects)
                for (FlowNode& fn : o.flowGraph.nodes) {
                    const FlowNodeType* ft = flowNodeType(fn.type);
                    if (ft && ft->strKind == FlowParamKind::GradingName &&
                        fn.str == g.name)
                        fn.str = nameBuf;
                }
        g.name = nameBuf;
    }
    changed |= ImGui::IsItemDeactivatedAfterEdit();
    ImGui::SameLine();
    if (ImGui::SmallButton("Duplicate")) {
        ColorGradingPreset copy = g;
        std::string base = copy.name;
        for (int n = 2;; ++n) {
            copy.name = base + "-" + std::to_string(n);
            bool taken = false;
            for (const auto& other : project_.gradings)
                taken |= (other.name == copy.name);
            if (!taken) break;
        }
        project_.gradings.push_back(std::move(copy));
        selectedGrading_ = (int)project_.gradings.size() - 1;
        changed = true;
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Delete")) {
        for (SceneData& sc : project_.scenes)
            for (SceneObject& o : sc.objects)
                for (FlowNode& fn : o.flowGraph.nodes) {
                    const FlowNodeType* ft = flowNodeType(fn.type);
                    if (ft && ft->strKind == FlowParamKind::GradingName &&
                        fn.str == g.name)
                        fn.str.clear();
                }
        if (project_.defaultGrading == selectedGrading_) project_.defaultGrading = -1;
        else if (project_.defaultGrading > selectedGrading_) --project_.defaultGrading;
        project_.gradings.erase(project_.gradings.begin() + selectedGrading_);
        selectedGrading_ = -1;
        commitChange();
        ImGui::EndChild();
        ImGui::End();
        return;
    }

    bool isDefault = project_.defaultGrading == selectedGrading_;
    if (ImGui::Checkbox("Default at game start", &isDefault)) {
        project_.defaultGrading = isDefault ? selectedGrading_ : -1;
        changed = true;
    }
    ImGui::SameLine(0.0f, scaled(24.0f));
    ImGui::Checkbox("Preview in viewport", &gradingPreview_);

    ImGui::SeparatorText("Quick looks");
    auto quick = [&](const char* label, float bright, float contrast, float sat,
                     float temp, float tr, float tg, float tb, float tintAmt) {
        if (ImGui::SmallButton(label)) {
            g.brightness = bright;
            g.contrast = contrast;
            g.saturation = sat;
            g.temperature = temp;
            g.tint[0] = tr, g.tint[1] = tg, g.tint[2] = tb;
            g.tintAmount = tintAmt;
            for (int c = 0; c < 3; ++c) g.lift[c] = 0.0f, g.gain[c] = 1.0f;
            changed = true;
        }
    };
    quick("Warm", 1.05f, 1.05f, 1.0f, 0.45f, 1.0f, 0.75f, 0.45f, 0.10f);
    ImGui::SameLine();
    quick("Cool night", 0.85f, 1.10f, 0.85f, -0.45f, 0.15f, 0.25f, 0.60f, 0.22f);
    ImGui::SameLine();
    quick("Sepia", 1.0f, 1.0f, 0.30f, 0.10f, 1.0f, 0.84f, 0.62f, 0.35f);
    ImGui::SameLine();
    quick("Faded", 1.10f, 0.80f, 0.70f, 0.0f, 1.0f, 1.0f, 1.0f, 0.0f);
    ImGui::SameLine();
    if (ImGui::SmallButton("Neutral")) {
        const std::string keep = g.name;
        g = ColorGradingPreset{};
        g.name = keep;
        changed = true;
    }

    ImGui::SeparatorText("Look");
    ImGui::SliderFloat("Brightness", &g.brightness, 0.0f, 2.0f, "%.2f");
    changed |= ImGui::IsItemDeactivatedAfterEdit();
    ImGui::SliderFloat("Contrast", &g.contrast, 0.0f, 2.0f, "%.2f");
    changed |= ImGui::IsItemDeactivatedAfterEdit();
    ImGui::SliderFloat("Saturation", &g.saturation, 0.0f, 1.0f, "%.2f");
    changed |= ImGui::IsItemDeactivatedAfterEdit();
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Approximation: the GS has no per-pixel luma, so\n"
                          "desaturation mixes toward mid-gray (flattens a bit).");
    ImGui::SliderFloat("Temperature", &g.temperature, -1.0f, 1.0f, "%.2f");
    changed |= ImGui::IsItemDeactivatedAfterEdit();

    ImGui::SeparatorText("Tint");
    ImGui::ColorEdit3("Color", g.tint, ImGuiColorEditFlags_NoInputs);
    changed |= ImGui::IsItemDeactivatedAfterEdit();
    ImGui::SameLine();
    ImGui::SetNextItemWidth(scaled(160.0f));
    ImGui::SliderFloat("Amount", &g.tintAmount, 0.0f, 1.0f, "%.2f");
    changed |= ImGui::IsItemDeactivatedAfterEdit();

    ImGui::SeparatorText("Color wheels");
    // Two Resolve-style trackballs: the wheel carries the between-channel
    // tint, the slider under it the common (master) level, the drag row the
    // exact numbers. All three edit the same lift/gain floats.
    auto wheelColumn = [&](const char* label, float* rgb, float lo, float hi,
                           float wheelRange) {
        const float width = scaled(108.0f);
        ImGui::BeginGroup();
        ImGui::PushID(label);
        const float tw = ImGui::CalcTextSize(label).x;
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() +
                             (width - (tw < width ? tw : width)) * 0.5f);
        ImGui::TextUnformatted(label);
        changed |= gradingWheel(label, rgb, lo, hi, wheelRange, uiScaleApplied_);
        float master = (rgb[0] + rgb[1] + rgb[2]) / 3.0f;
        ImGui::SetNextItemWidth(width);
        if (ImGui::SliderFloat("##master", &master, lo, hi, "%.2f")) {
            const float old = (rgb[0] + rgb[1] + rgb[2]) / 3.0f;
            for (int i = 0; i < 3; ++i) {
                rgb[i] += master - old;
                rgb[i] = rgb[i] < lo ? lo : (rgb[i] > hi ? hi : rgb[i]);
            }
        }
        changed |= ImGui::IsItemDeactivatedAfterEdit();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Master: all three channels together");
        ImGui::SetNextItemWidth(width);
        ImGui::DragFloat3("##rgb", rgb, 0.005f, lo, hi, "%.2f");
        changed |= ImGui::IsItemDeactivatedAfterEdit();
        ImGui::PopID();
        ImGui::EndGroup();
    };
    wheelColumn("Lift (shadows)", g.lift, -0.5f, 0.5f, 0.35f);
    ImGui::SameLine(0.0f, scaled(28.0f));
    wheelColumn("Gain (highlights)", g.gain, 0.0f, 2.0f, 0.75f);

    // The exact GS numbers this preset compiles to (scene_data.hpp /
    // RendererCorePostFx::setGrading) - also what the viewport previews.
    const CompiledGrading cg = compileGrading(g);
    ImGui::Spacing();
    ImGui::TextDisabled("GS pass: gain %d/%d/%d  lift %+d/%+d/%+d  mix %d%% -> "
                        "(%d,%d,%d)  |  %s",
                        cg.gain[0], cg.gain[1], cg.gain[2], cg.lift[0], cg.lift[1],
                        cg.lift[2], cg.mixAmt * 100 / 128, cg.mixColor[0],
                        cg.mixColor[1], cg.mixColor[2],
                        cg.neutral() ? "neutral (skipped)" : "3-6 sprites, GS only");

    ImGui::EndChild();
    ImGui::End();

    if (changed) commitChange();
}

// Ambience Editor (Tools > Ambience Editor): preset list on the left, the
// sky / lighting / fog controls for the selected preset on the right. A preset
// is a scene's "mood" bundle; scenes pick one in Scene > Preferences (empty =
// the default), and the Set Ambience flow node repaints the sky at runtime.
// These controls used to live in Project Preferences.
void App::drawAmbienceWindow() {
    if (!showAmbienceEditor_ || !hasProject_) return;

    ImGui::SetNextWindowSize(ImVec2(scaled(560), scaled(540)),
                             ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Ambience Editor", &showAmbienceEditor_)) {
        ImGui::End();
        return;
    }

    bool changed = false;

    // --- left: preset list -------------------------------------------------
    ImGui::BeginChild("##ambience_list", ImVec2(scaled(170), 0), ImGuiChildFlags_Borders);
    if (ImGui::Button("+ New preset", ImVec2(-1, 0))) {
        int counter = 0;
        std::string name;
        for (;;) {
            name = "ambience-" + std::to_string(++counter);
            bool taken = false;
            for (const auto& a : project_.ambiencePresets) taken |= (a.name == name);
            if (!taken) break;
        }
        AmbiencePreset a;
        a.name = name;
        project_.ambiencePresets.push_back(std::move(a));
        selectedAmbience_ = (int)project_.ambiencePresets.size() - 1;
        changed = true;
    }
    ImGui::Separator();
    for (int i = 0; i < (int)project_.ambiencePresets.size(); ++i) {
        ImGui::PushID(i);
        std::string tag = project_.ambiencePresets[i].name;
        if (project_.defaultAmbience == i) tag += "  [default]";
        if (ImGui::Selectable(tag.c_str(), selectedAmbience_ == i))
            selectedAmbience_ = i;
        ImGui::PopID();
    }
    if (project_.ambiencePresets.empty())
        ImGui::TextDisabled("No presets yet.\nA preset bundles the\n"
                            "sky, lighting and fog\ninto one reusable mood.");
    ImGui::EndChild();

    ImGui::SameLine();

    // --- right: selected preset editor -------------------------------------
    ImGui::BeginChild("##ambience_edit", ImVec2(0, 0));
    if (selectedAmbience_ < 0 ||
        selectedAmbience_ >= (int)project_.ambiencePresets.size()) {
        ImGui::TextDisabled("Select a preset on the left (or create one).");
        ImGui::TextDisabled("\nUse presets by:");
        ImGui::BulletText("marking one \"Default at game start\"");
        ImGui::BulletText("picking one per scene in Scene > Preferences");
        ImGui::BulletText("the Set Ambience flow node (category \"Scene\")");
        ImGui::EndChild();
        ImGui::End();
        return;
    }
    AmbiencePreset& a = project_.ambiencePresets[selectedAmbience_];

    char nameBuf[64];
    std::snprintf(nameBuf, sizeof(nameBuf), "%s", a.name.c_str());
    ImGui::SetNextItemWidth(scaled(180.0f));
    if (ImGui::InputText("Name", nameBuf, sizeof(nameBuf))) {
        // keep scene references and Set Ambience flow nodes pointing here
        for (SceneData& sc : project_.scenes) {
            if (sc.ambiencePreset == a.name) sc.ambiencePreset = nameBuf;
            for (SceneObject& o : sc.objects)
                for (FlowNode& fn : o.flowGraph.nodes) {
                    const FlowNodeType* ft = flowNodeType(fn.type);
                    if (ft && ft->strKind == FlowParamKind::AmbienceName &&
                        fn.str == a.name)
                        fn.str = nameBuf;
                }
        }
        a.name = nameBuf;
    }
    changed |= ImGui::IsItemDeactivatedAfterEdit();
    ImGui::SameLine();
    if (ImGui::SmallButton("Duplicate")) {
        AmbiencePreset copy = a;
        std::string base = copy.name;
        for (int n = 2;; ++n) {
            copy.name = base + "-" + std::to_string(n);
            bool taken = false;
            for (const auto& other : project_.ambiencePresets)
                taken |= (other.name == copy.name);
            if (!taken) break;
        }
        project_.ambiencePresets.push_back(std::move(copy));
        selectedAmbience_ = (int)project_.ambiencePresets.size() - 1;
        changed = true;
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Delete")) {
        const std::string gone = a.name;
        for (SceneData& sc : project_.scenes) {
            if (sc.ambiencePreset == gone) sc.ambiencePreset.clear();
            for (SceneObject& o : sc.objects)
                for (FlowNode& fn : o.flowGraph.nodes) {
                    const FlowNodeType* ft = flowNodeType(fn.type);
                    if (ft && ft->strKind == FlowParamKind::AmbienceName &&
                        fn.str == gone)
                        fn.str.clear();
                }
        }
        if (project_.defaultAmbience == selectedAmbience_)
            project_.defaultAmbience = -1;
        else if (project_.defaultAmbience > selectedAmbience_)
            --project_.defaultAmbience;
        project_.ambiencePresets.erase(project_.ambiencePresets.begin() +
                                       selectedAmbience_);
        selectedAmbience_ = -1;
        commitChange();
        ImGui::EndChild();
        ImGui::End();
        return;
    }

    bool isDefault = project_.defaultAmbience == selectedAmbience_;
    if (ImGui::Checkbox("Default at game start", &isDefault)) {
        project_.defaultAmbience = isDefault ? selectedAmbience_ : -1;
        changed = true;
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Scenes that don't pick a preset use this one.");
    ImGui::SameLine(0.0f, scaled(24.0f));
    ImGui::Checkbox("Preview in viewport", &ambiencePreview_);

    ImGui::SeparatorText("Sky");
    ImGui::ColorEdit3("Sky horizon color", a.skyColor);
    changed |= ImGui::IsItemDeactivatedAfterEdit();
    ImGui::ColorEdit3("Sky zenith color", a.skyTopColor);
    changed |= ImGui::IsItemDeactivatedAfterEdit();
    ImGui::Checkbox("Gradient sky dome", &a.skyDome);
    changed |= ImGui::IsItemDeactivatedAfterEdit();
    ImGui::BeginDisabled(!a.skyDome);
    ImGui::SliderFloat("Zenith size", &a.zenithSize, 0.05f, 0.95f, "%.2f");
    changed |= ImGui::IsItemDeactivatedAfterEdit();
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("How much of the sky the zenith color fills.\n"
                          "0.5 = linear; higher spreads the zenith color down\n"
                          "toward the horizon, lower keeps it near the top.");
    ImGui::EndDisabled();

    ImGui::SeparatorText("Lighting");
    ImGui::TextDisabled("Baked into vertex colors at build (per scene). The Set "
                        "Ambience flow node repaints only the sky.");
    ImGui::DragFloat3("Light direction", a.lightDir, 0.02f, -1.0f, 1.0f, "%.2f");
    changed |= ImGui::IsItemDeactivatedAfterEdit();
    ImGui::ColorEdit3("Light color", a.lightColor);
    changed |= ImGui::IsItemDeactivatedAfterEdit();
    ImGui::SliderFloat("Brightness", &a.brightness, 0.0f, 2.0f, "%.2f");
    changed |= ImGui::IsItemDeactivatedAfterEdit();
    ImGui::SliderFloat("Ambient", &a.ambient, 0.0f, 1.0f, "%.2f");
    changed |= ImGui::IsItemDeactivatedAfterEdit();
    ImGui::SliderFloat("Diffuse", &a.diffuse, 0.0f, 1.0f, "%.2f");
    changed |= ImGui::IsItemDeactivatedAfterEdit();

    ImGui::SeparatorText("Distance fog");
    ImGui::Checkbox("Fog enabled", &a.fogEnabled);
    changed |= ImGui::IsItemDeactivatedAfterEdit();
    if (a.fogEnabled) {
        ImGui::ColorEdit3("Fog color", a.fogColor);
        changed |= ImGui::IsItemDeactivatedAfterEdit();
        ImGui::DragFloat("Fog start", &a.fogStart, 0.5f, 0.0f, 1000.0f, "%.1f");
        changed |= ImGui::IsItemDeactivatedAfterEdit();
        ImGui::DragFloat("Fog end", &a.fogEnd, 0.5f, 0.0f, 1000.0f, "%.1f");
        changed |= ImGui::IsItemDeactivatedAfterEdit();
        if (a.fogEnd <= a.fogStart + 1.0f) a.fogEnd = a.fogStart + 1.0f;
    }

    ImGui::EndChild();
    ImGui::End();

    if (changed) commitChange();
}

// --- Cutscene Director -------------------------------------------------------

// Snapshots the track target's current static pose into a key at `time`.
// A key within 1/60 s of that time is replaced instead (keeping its easing
// and visibility flag); the key list stays sorted.
bool App::cutsceneSnapshotObjectKey(SeqTrack& tr, float time) {
    const SceneObject* src = nullptr;
    for (const SceneObject& o : project_.objects())
        if (o.name == tr.target) {
            src = &o;
            break;
        }
    if (!src) return false;
    SeqObjectKey k;
    k.time = time;
    for (int c = 0; c < 3; ++c) {
        k.position[c] = src->position[c];
        k.rotation[c] = src->rotation[c];
        k.scale[c] = src->scale[c];
        k.color[c] = src->color[c];
    }
    int repl = -1;
    for (int i = 0; i < (int)tr.keys.size(); ++i)
        if (std::fabs(tr.keys[i].time - k.time) < 0.017f) repl = i;
    if (repl >= 0) {
        k.easing = tr.keys[repl].easing;
        k.visible = tr.keys[repl].visible;
        tr.keys[repl] = k;
    } else {
        tr.keys.push_back(k);
    }
    std::sort(tr.keys.begin(), tr.keys.end(),
              [](const SeqObjectKey& a, const SeqObjectKey& b) {
                  return a.time < b.time;
              });
    return true;
}

// Auto-key: called when a gizmo drag ends, just before its commitChange(), so
// the dropped keys ride the same undo snapshot as the transform edit itself.
void App::cutsceneAutoKey() {
    if (!seqAutoKey_ || !showCutsceneEditor_ || !seqPreview_ || !hasProject_) return;
    if (selectedSequence_ < 0 || selectedSequence_ >= (int)project_.sequences.size())
        return;
    Sequence& s = project_.sequences[selectedSequence_];
    for (int sel : selection_) {
        if (sel < 0 || sel >= (int)project_.objects().size()) continue;
        const std::string& name = project_.objects()[sel].name;
        for (SeqTrack& tr : s.tracks)
            if (tr.target == name) cutsceneSnapshotObjectKey(tr, seqPlayhead_);
    }
}

// Applies the loaded camera take to a sequence. Free target -> camera-lane
// shots (replace or append). A Camera-entity target -> the take is baked into
// that entity's transform track (position = eye, rotation = the Euler whose
// +Z lens points along the recorded view), the entity's FOV is set from the
// take, and a bound camera key is ensured so the shot dollies along the path.
// Returns the first key time, or -1 on no-op.
float App::applyCamTake(Sequence& s, bool replace) {
    std::vector<SeqCameraKey> baked = bakeCamTake(seqTake_, seqTakeMap_, &seqTakeStats_);
    if (baked.empty()) return -1.0f;
    auto sortCam = [&]() {
        std::sort(s.cameraKeys.begin(), s.cameraKeys.end(),
                  [](const SeqCameraKey& a, const SeqCameraKey& b) {
                      return a.time < b.time;
                  });
    };

    if (seqTakeTarget_.empty()) {
        // free camera shots on the camera lane
        if (replace)
            s.cameraKeys = baked;
        else {
            s.cameraKeys.insert(s.cameraKeys.end(), baked.begin(), baked.end());
            sortCam();
        }
        s.cameraEnabled = true;
        return baked.front().time;
    }

    // Camera-entity target: bake into the entity's transform track. Euler
    // angles wrap at +-180, so unwrap each channel to stay continuous with the
    // previous key - otherwise a pan crossing 180 deg (e.g. 170 -> -175) makes
    // the linear rotation interp spin the long way round: the sudden 180/360
    // whip after import.
    std::vector<SeqObjectKey> objKeys;
    objKeys.reserve(baked.size());
    float prevRot[3] = {0.0f, 0.0f, 0.0f};
    for (size_t i = 0; i < baked.size(); ++i) {
        const SeqCameraKey& k = baked[i];
        SeqObjectKey o;
        o.time = k.time;
        for (int c = 0; c < 3; ++c) o.position[c] = k.eye[c];
        const float dir[3] = {k.target[0] - k.eye[0], k.target[1] - k.eye[1],
                              k.target[2] - k.eye[2]};
        seqEulerFromForward(dir, o.rotation);
        if (i > 0)
            for (int c = 0; c < 3; ++c) {
                while (o.rotation[c] - prevRot[c] > 180.0f) o.rotation[c] -= 360.0f;
                while (o.rotation[c] - prevRot[c] < -180.0f) o.rotation[c] += 360.0f;
            }
        for (int c = 0; c < 3; ++c) prevRot[c] = o.rotation[c];
        o.easing = 0;  // the take IS the ease
        objKeys.push_back(o);
    }
    // find (or create) the object track that drives this camera entity
    SeqTrack* track = nullptr;
    for (SeqTrack& tr : s.tracks)
        if (tr.target == seqTakeTarget_) {
            track = &tr;
            break;
        }
    if (!track) {
        SeqTrack tr;
        tr.target = seqTakeTarget_;
        s.tracks.push_back(std::move(tr));
        track = &s.tracks.back();
    }
    track->animPos = true;
    track->animRot = true;
    track->animScale = false;
    track->animColor = false;
    track->animVis = false;
    track->keys = std::move(objKeys);
    // set the entity's FOV from the take (active-scene object of that name)
    for (SceneObject& o : project_.objects())
        if (o.name == seqTakeTarget_ && o.type == PrimitiveType::Camera) {
            if (seqTakeStats_.fovDeg > 0.0f) o.cameraFov = seqTakeStats_.fovDeg;
            break;
        }
    // ensure a bound camera key so the entity is actually filmed
    bool bound = false;
    for (const SeqCameraKey& k : s.cameraKeys)
        if (k.camera == seqTakeTarget_) {
            bound = true;
            break;
        }
    if (!bound) {
        SeqCameraKey k;
        k.time = track->keys.front().time;
        k.camera = seqTakeTarget_;
        k.easing = 0;
        s.cameraKeys.push_back(k);
        sortCam();
    }
    s.cameraEnabled = true;
    return track->keys.front().time;
}

// "From view" for take import: drop the take's first sample at the preview
// camera AND rotate the whole path so its first sample looks the way the
// editor camera does - so you frame the shot in the viewport, hit From view,
// and the recording is aimed there.
void App::takeOriginAimFromView() {
    float eye[3], at[3];
    viewport_.currentCamera(eye, at);
    for (int c = 0; c < 3; ++c) seqTakeMap_.origin[c] = eye[c];
    const float viewYaw =
        std::atan2(at[0] - eye[0], at[2] - eye[2]) * 180.0f / 3.14159265f;
    float y = viewYaw - camTakeInitialYawDeg(seqTake_);
    while (y > 180.0f) y -= 360.0f;
    while (y < -180.0f) y += 360.0f;
    seqTakeMap_.yawDeg = y;
}

// Poses a copy of the active scene's objects at the playhead using the SAME
// interpolation the PS2 runtime uses (sequence.hpp seqSample/seqEase), and -
// for a sequence with a camera track - flies the viewport camera along it. So
// scrubbing the timeline shows exactly what the console will render.
const std::vector<SceneObject>& App::cutscenePosedObjects() {
    // Pose the scene whenever the Director is open on a valid sequence. The
    // "Preview in viewport" toggle only controls whether the sequence drives
    // the VIEWPORT CAMERA (and the bars/fade overlay) - with it off, objects
    // (including a Camera entity dollying along its track) still animate, so
    // you can watch the move from a free vantage point.
    const bool active = showCutsceneEditor_ && hasProject_ &&
                        selectedSequence_ >= 0 &&
                        selectedSequence_ < (int)project_.sequences.size();
    if (!active) {
        if (seqCameraPushed_) {
            viewport_.clearCameraOverride();
            seqCameraPushed_ = false;
        }
        seqBarsStyleNow_ = 0;
        seqBarsNow_ = 0.0f;
        seqFadeNow_ = 0.0f;
        return project_.objects();
    }

    const Sequence& s = project_.sequences[selectedSequence_];
    const float t = seqPlayhead_;
    seqPosed_ = project_.objects();  // copy the active scene's objects

    // Editor-hidden layers stay hidden; cutscene visibility keys add to that.
    std::vector<char> hidden(seqPosed_.size(), 0);
    for (size_t i = 0; i < seqPosed_.size(); ++i)
        hidden[i] = isObjectHiddenInEditor(seqPosed_[i]) ? 1 : 0;

    // While paused, SELECTED objects keep their real (static) transform so
    // the gizmo edits what you see - otherwise posing an object between two
    // keys is blind (the track keeps snapping the preview back). Playback
    // poses everything. Bound camera shots read seqPosed_, so aiming a
    // selected Camera entity updates its shot live too.
    std::vector<char> editing(seqPosed_.size(), 0);
    if (!seqPlaying_)
        for (int sel : selection_)
            if (sel >= 0 && sel < (int)editing.size()) editing[sel] = 1;

    for (const SeqTrack& tr : s.tracks) {
        int idx = -1;
        for (size_t i = 0; i < seqPosed_.size(); ++i)
            if (seqPosed_[i].name == tr.target) {
                idx = (int)i;
                break;
            }
        if (idx < 0 || tr.keys.empty()) continue;
        if (editing[idx]) continue;  // selected: leave it editable

        std::vector<SeqObjectKey> keys = tr.keys;
        std::sort(keys.begin(), keys.end(),
                  [](const SeqObjectKey& a, const SeqObjectKey& b) {
                      return a.time < b.time;
                  });
        const int n = (int)keys.size();
        std::vector<float> times(n);
        std::vector<int> eas(n);
        for (int i = 0; i < n; ++i) times[i] = keys[i].time, eas[i] = keys[i].easing;
        auto samp = [&](std::function<float(const SeqObjectKey&)> g) {
            std::vector<float> v(n);
            for (int i = 0; i < n; ++i) v[i] = g(keys[i]);
            return seqSample(times.data(), v.data(), eas.data(), n, t);
        };

        SceneObject& o = seqPosed_[idx];
        if (tr.animPos)
            for (int c = 0; c < 3; ++c)
                o.position[c] = samp([c](const SeqObjectKey& k) { return k.position[c]; });
        if (tr.animRot)
            for (int c = 0; c < 3; ++c)
                o.rotation[c] = samp([c](const SeqObjectKey& k) { return k.rotation[c]; });
        if (tr.animScale)
            for (int c = 0; c < 3; ++c)
                o.scale[c] = samp([c](const SeqObjectKey& k) { return k.scale[c]; });
        if (tr.animColor)
            for (int c = 0; c < 3; ++c)
                o.color[c] = samp([c](const SeqObjectKey& k) { return k.color[c]; });
        if (tr.animVis) {
            int j = 0;
            while (j < n - 1 && t >= keys[j + 1].time) ++j;
            if (!keys[j].visible) hidden[idx] = 1;  // steps between keys
        }
    }
    viewport_.setHiddenMask(std::move(hidden));

    // Camera track: fly the preview camera (or release it back to the orbit).
    // Each key is a shot - free (stored eye/at/fov) or bound to a Camera
    // entity, in which case eye/at/fov come from the entity's CURRENT pose in
    // seqPosed_ (object tracks already ran, so an animated camera entity gives
    // a dolly shot). Shots blend across the segment; Step easing = hard cut.
    // The exact same resolution runs in the generated PS2 player.
    if (seqPreview_ && s.cameraEnabled && !s.cameraKeys.empty()) {
        std::vector<SeqCameraKey> ck = s.cameraKeys;
        std::sort(ck.begin(), ck.end(),
                  [](const SeqCameraKey& a, const SeqCameraKey& b) {
                      return a.time < b.time;
                  });
        const int n = (int)ck.size();
        auto shot = [&](int i, float eye[3], float at[3], float& fov) {
            const SeqCameraKey& k = ck[i];
            const SceneObject* cam = nullptr;
            if (!k.camera.empty())
                for (const SceneObject& o : seqPosed_)
                    if (o.name == k.camera && o.type == PrimitiveType::Camera) {
                        cam = &o;
                        break;
                    }
            if (cam) {
                float fwd[3];
                seqCameraForward(cam->rotation, fwd);
                for (int c = 0; c < 3; ++c) {
                    eye[c] = cam->position[c];
                    at[c] = cam->position[c] + fwd[c];
                }
                fov = cam->cameraFov;
            } else {
                for (int c = 0; c < 3; ++c) eye[c] = k.eye[c], at[c] = k.target[c];
                fov = k.fov;
            }
        };
        int i = 0;
        while (i < n - 1 && t >= ck[i + 1].time) ++i;
        float e0[3], a0[3], f0, shake;
        shot(i, e0, a0, f0);
        shake = ck[i].shake;
        if (t > ck[i].time && i < n - 1) {
            const float span = ck[i + 1].time - ck[i].time;
            const float u = span > 1e-6f ? (t - ck[i].time) / span : 0.0f;
            const float w = seqEase(ck[i].easing, u);
            float e1[3], a1[3], f1;
            shot(i + 1, e1, a1, f1);
            for (int c = 0; c < 3; ++c) {
                e0[c] += (e1[c] - e0[c]) * w;
                a0[c] += (a1[c] - a0[c]) * w;
            }
            f0 += (f1 - f0) * w;
            shake += (ck[i + 1].shake - shake) * w;
        }
        if (shake > 0.0f) {
            float off[3];
            seqShakeOffset(t, shake, off);
            for (int c = 0; c < 3; ++c) e0[c] += off[c], a0[c] += off[c];
        }
        viewport_.setCameraOverride(e0, a0, f0);
        seqCameraPushed_ = true;
    } else if (seqCameraPushed_) {
        viewport_.clearCameraOverride();
        seqCameraPushed_ = false;
    }

    // Widescreen bars + fades preview, overlaid on the viewport image where it
    // is drawn (same envelope math as the PS2 player). Only while the sequence
    // drives the viewport camera - with preview off you watch from outside.
    if (seqPreview_) {
        seqBarsStyleNow_ = s.bars;
        seqBarsNow_ = s.bars != kSeqBarsNone
                          ? seqBarsAmount(t, s.duration, s.barsSlideIn, s.barsSlideOut)
                          : 0.0f;
        seqFadeNow_ = seqFadeAlpha(t, s.duration, s.fadeIn, s.fadeOut);
    } else {
        seqBarsStyleNow_ = 0;
        seqBarsNow_ = 0.0f;
        seqFadeNow_ = 0.0f;
    }

    return seqPosed_;
}

// Cutscene Director window (Tools > Cutscene Director): sequence list on the
// left, the selected sequence's timeline on the right - object tracks (each a
// list of pose keyframes) plus an optional camera track. The playhead scrubs
// the whole scene live in the viewport; keys are authored by posing an object
// (or the view) and snapshotting it at the playhead.
void App::drawCutsceneWindow() {
    if (!showCutsceneEditor_ || !hasProject_) return;

    ImGui::SetNextWindowSize(ImVec2(scaled(880), scaled(620)),
                             ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Cutscene Director", &showCutsceneEditor_)) {
        ImGui::End();
        return;
    }

    bool changed = false;
    auto uniqueSeqName = [&](std::string base) {
        std::string n = base;
        for (int k = 2;; ++k) {
            bool taken = false;
            for (const auto& s : project_.sequences) taken |= (s.name == n);
            if (!taken) return n;
            n = base + "-" + std::to_string(k);
        }
    };

    // --- left: sequence list ------------------------------------------------
    ImGui::BeginChild("##seq_list", ImVec2(scaled(170), 0), ImGuiChildFlags_Borders);
    if (ImGui::Button("+ New sequence", ImVec2(-1, 0))) {
        Sequence s;
        s.name = uniqueSeqName("Cutscene");
        project_.sequences.push_back(std::move(s));
        selectedSequence_ = (int)project_.sequences.size() - 1;
        selectedSeqTrack_ = -1;
        seqPlayhead_ = 0.0f;
        changed = true;
    }
    ImGui::Separator();
    for (int i = 0; i < (int)project_.sequences.size(); ++i) {
        ImGui::PushID(i);
        if (ImGui::Selectable(project_.sequences[i].name.c_str(), selectedSequence_ == i)) {
            selectedSequence_ = i;
            selectedSeqTrack_ = -1;
            seqPlayhead_ = 0.0f;
        }
        ImGui::PopID();
    }
    if (project_.sequences.empty())
        ImGui::TextDisabled("No cutscenes yet.\nA sequence poses objects\n"
                            "+ the camera over time,\nfired by the Play\n"
                            "Sequence flow node.");
    ImGui::EndChild();
    ImGui::SameLine();

    // --- right: selected sequence -------------------------------------------
    ImGui::BeginChild("##seq_edit", ImVec2(0, 0));
    if (selectedSequence_ < 0 || selectedSequence_ >= (int)project_.sequences.size()) {
        ImGui::TextDisabled("Select a cutscene on the left (or create one).");
        ImGui::TextDisabled("\nPlay it in the game with the Play Sequence flow\n"
                            "node (category \"Scene\"); Stop Sequence ends it.");
        ImGui::EndChild();
        ImGui::End();
        return;
    }
    Sequence& s = project_.sequences[selectedSequence_];

    char nameBuf[64];
    std::snprintf(nameBuf, sizeof(nameBuf), "%s", s.name.c_str());
    ImGui::SetNextItemWidth(scaled(180.0f));
    if (ImGui::InputText("Name", nameBuf, sizeof(nameBuf))) {
        for (SceneData& sc : project_.scenes)
            for (SceneObject& o : sc.objects)
                for (FlowNode& fn : o.flowGraph.nodes) {
                    const FlowNodeType* ft = flowNodeType(fn.type);
                    if (ft && ft->strKind == FlowParamKind::SequenceName && fn.str == s.name)
                        fn.str = nameBuf;
                }
        s.name = nameBuf;
    }
    changed |= ImGui::IsItemDeactivatedAfterEdit();
    ImGui::SameLine();
    if (ImGui::SmallButton("Duplicate")) {
        Sequence copy = s;
        copy.name = uniqueSeqName(s.name);
        project_.sequences.push_back(std::move(copy));
        selectedSequence_ = (int)project_.sequences.size() - 1;
        changed = true;
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Delete")) {
        for (SceneData& sc : project_.scenes)
            for (SceneObject& o : sc.objects)
                for (FlowNode& fn : o.flowGraph.nodes) {
                    const FlowNodeType* ft = flowNodeType(fn.type);
                    if (ft && ft->strKind == FlowParamKind::SequenceName && fn.str == s.name)
                        fn.str.clear();
                }
        project_.sequences.erase(project_.sequences.begin() + selectedSequence_);
        selectedSequence_ = -1;
        selectedSeqTrack_ = -1;
        commitChange();
        ImGui::EndChild();
        ImGui::End();
        return;
    }

    ImGui::SetNextItemWidth(scaled(110.0f));
    if (ImGui::DragFloat("Duration (s)", &s.duration, 0.1f, 0.1f, 600.0f, "%.2f"))
        changed = true;
    if (s.duration < 0.1f) s.duration = 0.1f;
    ImGui::SameLine(0.0f, scaled(14.0f));
    if (ImGui::Checkbox("Loop", &s.loop)) changed = true;
    ImGui::SameLine(0.0f, scaled(14.0f));
    if (ImGui::Checkbox("Skippable", &s.skippable)) changed = true;
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Pressing START in the game ends the cutscene early.");
    ImGui::SameLine(0.0f, scaled(14.0f));
    if (ImGui::Checkbox("Camera track", &s.cameraEnabled)) changed = true;
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Drive the game camera from the camera lane's shots\n"
                          "for the duration of playback.");
    ImGui::SameLine(0.0f, scaled(14.0f));
    if (ImGui::Checkbox("Hide player", &s.hidePlayer)) changed = true;
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Hide the third-person player avatar while the cutscene\n"
                          "plays (no effect in FPP/noclip - they have no body).");

    // Cinematic dressing: widescreen masks + fades, composited over the frame
    // (and the HUD) on the PS2 and previewed on the viewport image.
    static const char* kBarsNames[] = {"None", "Cinema 2.39:1", "Wide 16:9",
                                       "Pillarbox", "Frame"};
    ImGui::SetNextItemWidth(scaled(130.0f));
    if (ImGui::Combo("Widescreen bars", &s.bars, kBarsNames, kSeqBarsStyleCount))
        changed = true;
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Solid black masks while the cutscene plays; they\n"
                          "slide in at the start and out before the end (times\n"
                          "below; 0 = they appear/vanish instantly).");
    // Bars slide-in/out times, only meaningful when bars are on. Authored
    // just like the fades, right next to them.
    if (s.bars != kSeqBarsNone) {
        ImGui::SameLine(0.0f, scaled(14.0f));
        ImGui::SetNextItemWidth(scaled(76.0f));
        if (ImGui::DragFloat("Bars in", &s.barsSlideIn, 0.05f, 0.0f, 10.0f, "%.2f s"))
            changed = true;
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("How long the bars take to slide in at the start.\n"
                              "0 = they are there from the first frame.");
        ImGui::SameLine(0.0f, scaled(10.0f));
        ImGui::SetNextItemWidth(scaled(76.0f));
        if (ImGui::DragFloat("Bars out", &s.barsSlideOut, 0.05f, 0.0f, 10.0f, "%.2f s"))
            changed = true;
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("How long the bars take to slide out before the\n"
                              "end. 0 = they stay until the last frame.");
        if (s.barsSlideIn < 0.0f) s.barsSlideIn = 0.0f;
        if (s.barsSlideOut < 0.0f) s.barsSlideOut = 0.0f;
    }
    ImGui::SetNextItemWidth(scaled(76.0f));
    if (ImGui::DragFloat("Fade in", &s.fadeIn, 0.05f, 0.0f, 10.0f, "%.2f s"))
        changed = true;
    ImGui::SameLine(0.0f, scaled(10.0f));
    ImGui::SetNextItemWidth(scaled(76.0f));
    if (ImGui::DragFloat("Fade out", &s.fadeOut, 0.05f, 0.0f, 10.0f, "%.2f s"))
        changed = true;
    if (s.fadeIn < 0.0f) s.fadeIn = 0.0f;
    if (s.fadeOut < 0.0f) s.fadeOut = 0.0f;

    // --- Adjust imported take -----------------------------------------------
    // After a take is imported the recording + mapping stay loaded, so the
    // whole path can be re-positioned and re-oriented in place (start point,
    // start yaw, scale) without re-importing. Re-bakes the same target.
    if (seqTakeActive_ && seqTakeSeqIdx_ == selectedSequence_ &&
        !seqTake_.samples.empty()) {
        const std::string what =
            seqTakeTarget_.empty() ? std::string("free camera shots")
                                   : ("camera \"" + seqTakeTarget_ + "\"");
        if (ImGui::CollapsingHeader("Adjust imported take",
                                    ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::TextDisabled("Re-positions the imported %s.", what.c_str());
            bool rebake = false;
            if (ImGui::DragFloat3("Start point", seqTakeMap_.origin, 0.1f)) rebake = true;
            ImGui::SameLine();
            if (ImGui::SmallButton("From view")) {
                takeOriginAimFromView();  // position + aim
                rebake = true;
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Move the start point to the editor camera AND\n"
                                  "aim the path where you are looking.");
            ImGui::SetNextItemWidth(scaled(140.0f));
            if (ImGui::DragFloat("Start yaw", &seqTakeMap_.yawDeg, 1.0f, -360.0f,
                                 360.0f, "%.0f deg"))
                rebake = true;
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Rotates the whole path about the up axis,\n"
                                  "pivoting on the start point.");
            ImGui::SameLine(0.0f, scaled(14.0f));
            ImGui::SetNextItemWidth(scaled(120.0f));
            if (ImGui::DragFloat("Scale", &seqTakeMap_.scale, 0.02f, 0.01f, 1000.0f,
                                 "%.2f u/m"))
                rebake = true;
            ImGui::SameLine(0.0f, scaled(14.0f));
            if (ImGui::SmallButton("Done")) {
                seqTakeActive_ = false;  // stop tracking; keys stay
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Finish adjusting - the keys become ordinary\n"
                                  "editable keyframes.");
            if (rebake) {
                applyCamTake(s, true);
                const float lastT =
                    s.cameraKeys.empty() ? 0.0f : s.cameraKeys.back().time;
                if (lastT > s.duration) s.duration = lastT;
                changed = true;
            }
        }
    }

    // "Import take...": pick a phone-recorded 6DoF camera take (CamTrackAR
    // .hfcs or the canonical CSV - docs/camera-takes.md) and stage it for the
    // mapping modal below. The default landing point is the preview camera,
    // so the path starts where the user is looking.
    auto beginTakeImport = [&]() {
        const std::string path = pickPath(PickKind::CamTake);
        if (path.empty()) return;
        seqTakePath_ = path;
        seqTakeError_.clear();
        if (!loadCamTakeAuto(path, seqTake_, seqTakeError_)) seqTake_ = CamTake{};
        seqTakeMap_.yawDeg = 0.0f;
        takeOriginAimFromView();  // land + aim at the current view by default
        seqTakeMap_.timeOffset = 0.0f;
        // default the target to the camera you're looking through, or the
        // first Camera entity (a take always bakes into a camera now)
        auto isCam = [&](const std::string& n) {
            for (const SceneObject& o : project_.objects())
                if (o.name == n && o.type == PrimitiveType::Camera) return true;
            return false;
        };
        if (seqTakeTarget_.empty() || !isCam(seqTakeTarget_)) {
            seqTakeTarget_.clear();
            if (!lookThroughCam_.empty() && isCam(lookThroughCam_))
                seqTakeTarget_ = lookThroughCam_;
            else
                for (const SceneObject& o : project_.objects())
                    if (o.type == PrimitiveType::Camera) {
                        seqTakeTarget_ = o.name;
                        break;
                    }
        }
        seqTakeDirty_ = true;
        seqTakeOpen_ = true;
    };

    // --- transport -----------------------------------------------------------
    ImGui::SeparatorText("Timeline");
    // Space toggles play/stop while the Cutscene Director is focused (but not
    // while typing in a field or dragging a widget - Space also clicks a
    // focused button, so skip when an item is active to avoid a double toggle).
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
        !ImGui::GetIO().WantTextInput && !ImGui::IsAnyItemActive() &&
        ImGui::IsKeyPressed(ImGuiKey_Space, false))
        seqPlaying_ = !seqPlaying_;
    if (ImGui::Button(seqPlaying_ ? "Pause" : "Play")) seqPlaying_ = !seqPlaying_;
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Play / pause (Space)");
    ImGui::SameLine();
    if (ImGui::Button("Rewind")) {
        seqPlayhead_ = 0.0f;
        seqPlaying_ = false;
    }
    ImGui::SameLine(0.0f, scaled(14.0f));
    ImGui::Checkbox("Preview in viewport", &seqPreview_);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Pose the scene at the playhead. Objects you have\n"
                          "SELECTED stay at their real transform while paused,\n"
                          "so the gizmo edits what you see - snapshot or\n"
                          "auto-key to turn that pose into a keyframe.");
    ImGui::SameLine(0.0f, scaled(14.0f));
    ImGui::Checkbox("Auto-key", &seqAutoKey_);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Finishing a gizmo drag drops a keyframe at the\n"
                          "playhead for every selected object that has a\n"
                          "track in this sequence.");
    ImGui::SameLine(0.0f, scaled(14.0f));
    ImGui::SetNextItemWidth(scaled(100.0f));
    ImGui::SliderFloat("Zoom", &seqZoom_, 1.0f, 8.0f, "%.1fx");
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Timeline horizontal zoom (also Ctrl + mouse wheel\n"
                          "over the dopesheet).");
    ImGui::SameLine(0.0f, scaled(14.0f));
    ImGui::Text("t = %.2f s", seqPlayhead_);
    ImGui::SameLine(0.0f, 14.0f);
    if (ImGui::SmallButton("Import take...")) beginTakeImport();
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Import a phone-recorded 6DoF camera move (CamTrackAR\n"
                          ".hfcs or the CSV spec in docs/camera-takes.md) as free\n"
                          "camera shots on the camera lane.");
    if (seqPlaying_) {
        seqPlayhead_ += ImGui::GetIO().DeltaTime;
        if (seqPlayhead_ >= s.duration) {
            if (s.loop)
                seqPlayhead_ = std::fmod(seqPlayhead_, s.duration);
            else {
                seqPlayhead_ = s.duration;
                seqPlaying_ = false;
            }
        }
    }
    if (seqPlayhead_ < 0.0f) seqPlayhead_ = 0.0f;
    if (seqPlayhead_ > s.duration) seqPlayhead_ = s.duration;

    static const char* kEaseNames[] = {"Linear", "Smooth", "Step (hold)"};

    // Snapshot helpers. A key within 1/60 s of the requested time is replaced
    // (keeping its easing and, for camera keys, the shot binding/fov/shake).
    auto sortObjKeys = [](SeqTrack& tr) {
        std::sort(tr.keys.begin(), tr.keys.end(),
                  [](const SeqObjectKey& a, const SeqObjectKey& b) {
                      return a.time < b.time;
                  });
    };
    auto sortCamKeys = [&]() {
        std::sort(s.cameraKeys.begin(), s.cameraKeys.end(),
                  [](const SeqCameraKey& a, const SeqCameraKey& b) {
                      return a.time < b.time;
                  });
    };
    auto snapshotObjectKey = [&](SeqTrack& tr, float time) {
        return cutsceneSnapshotObjectKey(tr, time);
    };
    // The camera to film a new shot from: the one you're looking through, else
    // a selected Camera, else the only Camera in the scene, else "" (none).
    auto activeCameraName = [&]() -> std::string {
        auto isCam = [&](const std::string& n) {
            for (const SceneObject& o : project_.objects())
                if (o.name == n && o.type == PrimitiveType::Camera) return true;
            return false;
        };
        if (!lookThroughCam_.empty() && isCam(lookThroughCam_)) return lookThroughCam_;
        if (selectedObject_ >= 0 && selectedObject_ < (int)project_.objects().size() &&
            project_.objects()[selectedObject_].type == PrimitiveType::Camera)
            return project_.objects()[selectedObject_].name;
        std::string first;
        for (const SceneObject& o : project_.objects())
            if (o.type == PrimitiveType::Camera) {
                first = o.name;
                break;
            }
        return first;
    };
    // Adds/updates a camera-lane shot at `time`, bound to the active camera.
    // Returns false when the scene has no Camera entity to film from.
    auto snapshotCameraKey = [&](float time) -> bool {
        const std::string cam = activeCameraName();
        if (cam.empty()) return false;
        SeqCameraKey k;
        k.time = time;
        k.camera = cam;
        int repl = -1;
        for (int i = 0; i < (int)s.cameraKeys.size(); ++i)
            if (std::fabs(s.cameraKeys[i].time - k.time) < 0.017f) repl = i;
        if (repl >= 0) {
            k.easing = s.cameraKeys[repl].easing;
            k.shake = s.cameraKeys[repl].shake;
            s.cameraKeys[repl] = k;
        } else {
            s.cameraKeys.push_back(k);
        }
        sortCamKeys();
        return true;
    };

    // --- the dopesheet ---------------------------------------------------
    // One lane per track (the camera lane first), keys as draggable diamonds,
    // a click/drag-scrubbed time ruler and a playhead line across all lanes.
    // The label column stays pinned while the lanes scroll horizontally.
    const float laneH = scaled(26.0f), rulerH = scaled(22.0f), labelW = scaled(170.0f);
    const int laneCount = (s.cameraEnabled ? 1 : 0) + (int)s.tracks.size();
    const float sheetH =
        rulerH + laneCount * laneH + ImGui::GetStyle().ScrollbarSize + scaled(8.0f);
    // sanity: a deleted/toggled lane can strand the key selection
    if (selectedSeqTrack_ >= (int)s.tracks.size() ||
        (selectedSeqTrack_ < 0 && !s.cameraEnabled))
        selectedSeqKey_ = -1;

    int deleteTrack = -1;
    ImGui::BeginChild("##dopesheet", ImVec2(0, sheetH), ImGuiChildFlags_Borders,
                      ImGuiWindowFlags_HorizontalScrollbar);
    {
        // Ctrl + mouse wheel zooms the timeline (like the flow graph); a plain
        // wheel keeps scrolling the dopesheet.
        if (ImGui::IsWindowHovered() && ImGui::GetIO().KeyCtrl &&
            ImGui::GetIO().MouseWheel != 0.0f) {
            seqZoom_ *= ImPow(1.1f, ImGui::GetIO().MouseWheel);
            if (seqZoom_ < 1.0f) seqZoom_ = 1.0f;
            if (seqZoom_ > 8.0f) seqZoom_ = 8.0f;
            ImGui::GetIO().MouseWheel = 0.0f;  // consume: don't also scroll
        }
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 origin = ImGui::GetCursorScreenPos();  // content space
        const float visibleW = ImGui::GetWindowSize().x;
        float timeW = (visibleW - labelW - scaled(8.0f)) * seqZoom_;
        if (timeW < scaled(160.0f)) timeW = scaled(160.0f);
        const float pps = timeW / s.duration;  // pixels per second
        const float x0 = origin.x + labelW;    // timeline left edge
        const float contentH = rulerH + laneCount * laneH;
        ImGui::Dummy(ImVec2(labelW + timeW + scaled(4.0f), contentH));
        const float laneY0 = origin.y + rulerH;
        const ImVec2 winPos = ImGui::GetWindowPos();  // pinned label column x
        const float labelX = winPos.x;

        const ImU32 colRuler = ImGui::GetColorU32(ImGuiCol_TableHeaderBg);
        const ImU32 colLaneA = ImGui::GetColorU32(ImGuiCol_TableRowBg);
        const ImU32 colLaneB = ImGui::GetColorU32(ImGuiCol_TableRowBgAlt);
        const ImU32 colGrid = ImGui::GetColorU32(ImGuiCol_Border);
        const ImU32 colText = ImGui::GetColorU32(ImGuiCol_Text);
        const ImU32 colDim = ImGui::GetColorU32(ImGuiCol_TextDisabled);
        const ImU32 colPlayhead = IM_COL32(255, 80, 80, 255);
        const ImU32 colSelected = IM_COL32(255, 200, 70, 255);
        // key fill encodes the outgoing easing
        auto keyColor = [&](int easing) {
            if (easing == 2) return IM_COL32(255, 176, 80, 255);   // step
            if (easing == 0) return IM_COL32(200, 200, 200, 255);  // linear
            return IM_COL32(120, 200, 255, 255);                   // smooth
        };
        auto timeAtMouse = [&]() {
            float t = (ImGui::GetIO().MousePos.x - x0) / pps;
            t = t < 0.0f ? 0.0f : (t > s.duration ? s.duration : t);
            return std::round(t * 100.0f) / 100.0f;  // snap to 10 ms
        };

        // lane backgrounds (before the interactive items on them)
        dl->AddRectFilled(ImVec2(x0, origin.y), ImVec2(x0 + timeW, origin.y + rulerH),
                          colRuler);
        for (int li = 0; li < laneCount; ++li) {
            const float y = laneY0 + li * laneH;
            dl->AddRectFilled(ImVec2(x0, y), ImVec2(x0 + timeW, y + laneH),
                              (li & 1) ? colLaneB : colLaneA);
        }

        // lane hit areas: double-click an empty spot = drop a key there.
        // AllowOverlap so the keyframe buttons submitted on top of the lane
        // still catch the mouse (drag a key to retime) - without it the lane
        // eats every click over it.
        for (int li = 0; li < laneCount; ++li) {
            const int ti = s.cameraEnabled ? li - 1 : li;  // -1 = camera lane
            ImGui::PushID(100 + li);
            ImGui::SetCursorScreenPos(ImVec2(x0, laneY0 + li * laneH));
            ImGui::SetNextItemAllowOverlap();
            ImGui::InvisibleButton("##lane", ImVec2(timeW, laneH));
            if (ImGui::IsItemHovered() &&
                ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                const float t = timeAtMouse();
                bool did = false;
                if (ti < 0) {
                    did = snapshotCameraKey(t);
                } else {
                    did = snapshotObjectKey(s.tracks[ti], t);
                }
                if (did) {
                    selectedSeqTrack_ = ti;
                    selectedSeqKey_ = -1;  // select the dropped key below
                    if (ti < 0) {
                        for (int i = 0; i < (int)s.cameraKeys.size(); ++i)
                            if (s.cameraKeys[i].time == t) selectedSeqKey_ = i;
                    } else {
                        for (int i = 0; i < (int)s.tracks[ti].keys.size(); ++i)
                            if (s.tracks[ti].keys[i].time == t) selectedSeqKey_ = i;
                    }
                    seqPlayhead_ = t;
                    changed = true;
                }
            }
            ImGui::PopID();
        }

        // ruler: ticks + labels, click/drag scrubs the playhead
        ImGui::SetCursorScreenPos(ImVec2(x0, origin.y));
        ImGui::InvisibleButton("##ruler", ImVec2(timeW, rulerH));
        if (ImGui::IsItemActive()) {
            seqPlayhead_ = timeAtMouse();
            seqPlaying_ = false;
        }
        {
            static const float kSteps[] = {0.1f, 0.2f, 0.5f, 1.0f,  2.0f,
                                           5.0f, 10.0f, 15.0f, 30.0f, 60.0f};
            float step = 60.0f;
            for (float c : kSteps)
                if (c * pps >= scaled(56.0f)) {
                    step = c;
                    break;
                }
            const float minor = step / 5.0f;
            for (float t = 0.0f; t <= s.duration + 1e-4f; t += minor) {
                const float x = x0 + t * pps;
                const bool major = std::fabs(std::fmod(t + 1e-4f, step)) < 2e-3f;
                dl->AddLine(ImVec2(x, origin.y + (major ? scaled(4.0f) : scaled(13.0f))),
                            ImVec2(x, origin.y + rulerH), colGrid);
                if (major) {
                    char buf[16];
                    std::snprintf(buf, sizeof(buf), "%g s", t);
                    dl->AddText(ImVec2(x + scaled(3.0f), origin.y + scaled(2.0f)), colDim, buf);
                    // faint grid line down the lanes
                    dl->AddLine(ImVec2(x, laneY0), ImVec2(x, laneY0 + laneCount * laneH),
                                ImGui::GetColorU32(ImGuiCol_Border, 0.4f));
                }
            }
        }

        // keys: diamonds (free camera shots and object keys) / circles (shots
        // bound to a Camera entity). Click selects, drag retimes, right-click
        // opens easing/delete.
        auto keyWidget = [&](int lane, int ki, float& time, int& easing,
                             bool bound) -> int {
            // returns 0 = untouched, 1 = edited (uncommitted), 2 = committed,
            // 3 = delete me
            int result = 0;
            const int li = s.cameraEnabled ? lane + 1 : lane;
            const float cx = x0 + time * pps;
            const float cy = laneY0 + li * laneH + laneH * 0.5f;
            const float r = scaled(6.0f);
            const float pad = scaled(4.0f);  // hit-area margin around the diamond
            ImGui::PushID((lane + 2) * 1000 + ki);
            ImGui::SetCursorScreenPos(ImVec2(cx - r - pad, cy - r - pad));
            ImGui::InvisibleButton("##key", ImVec2(2.0f * (r + pad), 2.0f * (r + pad)));
            const bool hovered = ImGui::IsItemHovered();
            const bool dragging = ImGui::IsItemActive();
            // the horizontal-resize cursor + tooltip make retiming discoverable
            if (hovered || dragging)
                ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
            if (hovered && !dragging)
                ImGui::SetTooltip("%.2f s - drag to retime,\nright-click for easing/delete",
                                  time);
            if (ImGui::IsItemActivated()) {
                selectedSeqTrack_ = lane;
                selectedSeqKey_ = ki;
            }
            if (dragging && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 1.0f)) {
                time = timeAtMouse();
                seqPlayhead_ = time;
                result = 1;
            }
            if (ImGui::IsItemDeactivated()) result = 2;  // sort + commit outside
            if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                seqPlayhead_ = time;
            ImGui::OpenPopupOnItemClick("##keyctx", ImGuiPopupFlags_MouseButtonRight);
            if (ImGui::BeginPopup("##keyctx")) {
                selectedSeqTrack_ = lane;
                selectedSeqKey_ = ki;
                ImGui::TextDisabled("Key @ %.2f s", time);
                ImGui::Separator();
                for (int e = 0; e < 3; ++e)
                    if (ImGui::MenuItem(kEaseNames[e], nullptr, easing == e)) {
                        easing = e;
                        result = 2;
                    }
                ImGui::Separator();
                if (ImGui::MenuItem("Playhead here")) seqPlayhead_ = time;
                if (ImGui::MenuItem("Delete key")) result = 3;
                ImGui::EndPopup();
            }
            const bool sel = selectedSeqTrack_ == lane && selectedSeqKey_ == ki;
            const ImU32 fill = keyColor(easing);
            if (bound) {
                dl->AddCircleFilled(ImVec2(cx, cy), r - 1.0f, fill);
                dl->AddCircle(ImVec2(cx, cy), r - 1.0f, IM_COL32(20, 20, 20, 255));
            } else {
                const ImVec2 pts[4] = {{cx, cy - r}, {cx + r, cy}, {cx, cy + r},
                                       {cx - r, cy}};
                dl->AddConvexPolyFilled(pts, 4, fill);
                dl->AddPolyline(pts, 4, IM_COL32(20, 20, 20, 255),
                                ImDrawFlags_Closed, 1.0f);
            }
            if (sel || hovered)
                dl->AddCircle(ImVec2(cx, cy), r + scaled(2.5f),
                              sel ? colSelected : ImGui::GetColorU32(ImGuiCol_Text, 0.6f),
                              0, sel ? 2.0f : 1.0f);
            ImGui::PopID();
            return result;
        };

        // camera lane keys
        if (s.cameraEnabled) {
            int del = -1, resort = -1;
            for (int ki = 0; ki < (int)s.cameraKeys.size(); ++ki) {
                SeqCameraKey& k = s.cameraKeys[ki];
                const int r =
                    keyWidget(-1, ki, k.time, k.easing, !k.camera.empty());
                if (r == 2) resort = ki;
                if (r == 3) del = ki;
            }
            if (del >= 0) {
                s.cameraKeys.erase(s.cameraKeys.begin() + del);
                selectedSeqKey_ = -1;
                changed = true;
            } else if (resort >= 0) {
                const float t = s.cameraKeys[resort].time;
                sortCamKeys();
                if (selectedSeqTrack_ == -1)
                    for (int i = 0; i < (int)s.cameraKeys.size(); ++i)
                        if (s.cameraKeys[i].time == t) {
                            selectedSeqKey_ = i;
                            break;
                        }
                changed = true;
            }
        }
        // object lane keys
        for (int ti = 0; ti < (int)s.tracks.size(); ++ti) {
            SeqTrack& tr = s.tracks[ti];
            int del = -1, resort = -1;
            for (int ki = 0; ki < (int)tr.keys.size(); ++ki) {
                const int r = keyWidget(ti, ki, tr.keys[ki].time,
                                        tr.keys[ki].easing, false);
                if (r == 2) resort = ki;
                if (r == 3) del = ki;
            }
            if (del >= 0) {
                tr.keys.erase(tr.keys.begin() + del);
                selectedSeqKey_ = -1;
                changed = true;
            } else if (resort >= 0) {
                const float t = tr.keys[resort].time;
                sortObjKeys(tr);
                if (selectedSeqTrack_ == ti)
                    for (int i = 0; i < (int)tr.keys.size(); ++i)
                        if (tr.keys[i].time == t) {
                            selectedSeqKey_ = i;
                            break;
                        }
                changed = true;
            }
        }

        // playhead: a line across ruler + lanes with a grabber triangle
        {
            const float x = x0 + seqPlayhead_ * pps;
            dl->AddLine(ImVec2(x, origin.y), ImVec2(x, laneY0 + laneCount * laneH),
                        colPlayhead, 1.5f);
            dl->AddTriangleFilled(ImVec2(x - scaled(5.0f), origin.y),
                                  ImVec2(x + scaled(5.0f), origin.y),
                                  ImVec2(x, origin.y + scaled(9.0f)), colPlayhead);
        }

        // pinned label column, drawn last so it occludes scrolled-under keys.
        // Right-click a label = track settings; [+] = snapshot key @ playhead.
        dl->AddRectFilled(ImVec2(labelX, origin.y),
                          ImVec2(labelX + labelW, origin.y + contentH),
                          ImGui::GetColorU32(ImGuiCol_ChildBg));
        dl->AddRectFilled(ImVec2(labelX, origin.y),
                          ImVec2(labelX + labelW, origin.y + rulerH), colRuler);
        dl->AddText(ImVec2(labelX + scaled(8.0f), origin.y + scaled(3.0f)), colDim, "Track");
        dl->AddLine(ImVec2(labelX + labelW, origin.y),
                    ImVec2(labelX + labelW, origin.y + contentH), colGrid);
        for (int li = 0; li < laneCount; ++li) {
            const int ti = s.cameraEnabled ? li - 1 : li;
            const float y = laneY0 + li * laneH;
            dl->AddLine(ImVec2(labelX, y), ImVec2(labelX + labelW, y),
                        ImGui::GetColorU32(ImGuiCol_Border, 0.5f));
            ImGui::PushID(500 + li);
            // snapshot button on the right edge of the label cell
            ImGui::SetCursorScreenPos(ImVec2(labelX + labelW - scaled(24.0f), y + scaled(3.0f)));
            if (ImGui::SmallButton("+")) {
                if (ti < 0) {
                    if (snapshotCameraKey(seqPlayhead_)) changed = true;
                } else if (snapshotObjectKey(s.tracks[ti], seqPlayhead_)) {
                    changed = true;
                }
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(ti < 0 ? "Add a shot at the playhead, filming from the\n"
                                           "active camera (looked-through / selected /\n"
                                           "first). Needs a Camera entity in the scene."
                                         : "Snapshot the object's pose as a key\n"
                                           "at the playhead.");
            // the rest of the cell: click selects the lane, right-click = setup
            ImGui::SetCursorScreenPos(ImVec2(labelX, y));
            ImGui::InvisibleButton("##label", ImVec2(labelW - scaled(26.0f), laneH));
            if (ImGui::IsItemClicked()) {
                selectedSeqTrack_ = ti;
                selectedSeqKey_ = -1;
            }
            ImGui::OpenPopupOnItemClick("##trackctx", ImGuiPopupFlags_MouseButtonRight);
            if (ti < 0) {
                dl->AddText(ImVec2(labelX + scaled(8.0f), y + scaled(5.0f)), colText, "[*] Camera");
                dl->AddText(ImVec2(labelX + scaled(86.0f), y + scaled(5.0f)), colDim,
                            ("(" + std::to_string(s.cameraKeys.size()) + ")").c_str());
            } else {
                const SeqTrack& tr = s.tracks[ti];
                const std::string label =
                    (tr.target.empty() ? "<no object>" : tr.target);
                dl->AddText(ImVec2(labelX + scaled(8.0f), y + scaled(5.0f)), colText, label.c_str());
                // animated-channel letters, dimmed when off
                const char* chs[] = {"P", "R", "S", "C", "V"};
                const bool on[] = {tr.animPos, tr.animRot, tr.animScale,
                                   tr.animColor, tr.animVis};
                float cxs = labelX + labelW - scaled(88.0f);
                for (int c = 0; c < 5; ++c) {
                    dl->AddText(ImVec2(cxs, y + scaled(5.0f)),
                                on[c] ? colText : ImGui::GetColorU32(ImGuiCol_TextDisabled, 0.4f),
                                chs[c]);
                    cxs += scaled(11.0f);
                }
            }
            if (ImGui::BeginPopup("##trackctx")) {
                if (ti < 0) {
                    ImGui::TextDisabled("Camera track");
                    ImGui::Separator();
                    if (ImGui::MenuItem("Add shot @ playhead (active camera)")) {
                        if (snapshotCameraKey(seqPlayhead_)) changed = true;
                    }
                    if (ImGui::MenuItem("Clear all shots", nullptr, false,
                                        !s.cameraKeys.empty())) {
                        s.cameraKeys.clear();
                        selectedSeqKey_ = -1;
                        changed = true;
                    }
                    if (ImGui::MenuItem("Import take...")) beginTakeImport();
                } else {
                    SeqTrack& tr = s.tracks[ti];
                    ImGui::SetNextItemWidth(scaled(160.0f));
                    if (ImGui::BeginCombo("Object",
                                          tr.target.empty() ? "<pick>" : tr.target.c_str())) {
                        for (const SceneObject& o : project_.objects())
                            if (ImGui::Selectable(o.name.c_str(), o.name == tr.target)) {
                                tr.target = o.name;
                                changed = true;
                            }
                        ImGui::EndCombo();
                    }
                    ImGui::TextDisabled("Animated channels:");
                    if (ImGui::Checkbox("Position", &tr.animPos)) changed = true;
                    if (ImGui::Checkbox("Rotation", &tr.animRot)) changed = true;
                    if (ImGui::Checkbox("Scale", &tr.animScale)) changed = true;
                    if (ImGui::Checkbox("Color", &tr.animColor)) changed = true;
                    if (ImGui::Checkbox("Visibility", &tr.animVis)) changed = true;
                    ImGui::Separator();
                    if (ImGui::MenuItem("Snapshot key @ playhead")) {
                        if (snapshotObjectKey(tr, seqPlayhead_)) changed = true;
                    }
                    if (ImGui::MenuItem("Remove track")) deleteTrack = ti;
                }
                ImGui::EndPopup();
            }
            ImGui::PopID();
        }
    }
    ImGui::EndChild();

    if (deleteTrack >= 0) {
        s.tracks.erase(s.tracks.begin() + deleteTrack);
        if (selectedSeqTrack_ == deleteTrack) selectedSeqTrack_ = -1, selectedSeqKey_ = -1;
        changed = true;
    }
    if (ImGui::SmallButton("+ Add object track")) ImGui::OpenPopup("##addtrack");
    if (ImGui::BeginPopup("##addtrack")) {
        auto hasTrack = [&](const std::string& name) {
            for (const SeqTrack& tr : s.tracks)
                if (tr.target == name) return true;
            return false;
        };
        // one track per object; a fresh track gets a starting key at the
        // playhead from the object's current pose, so it animates immediately
        auto addTrackFor = [&](const std::string& name) {
            SeqTrack tr;
            tr.target = name;
            cutsceneSnapshotObjectKey(tr, seqPlayhead_);
            s.tracks.push_back(std::move(tr));
            selectedSeqTrack_ = (int)s.tracks.size() - 1;
            selectedSeqKey_ = -1;
            changed = true;
        };
        int freshSelected = 0;
        for (int sel : selection_)
            if (sel >= 0 && sel < (int)project_.objects().size() &&
                !hasTrack(project_.objects()[sel].name))
                ++freshSelected;
        char selLabel[48];
        std::snprintf(selLabel, sizeof(selLabel), "Add selected (%d)", freshSelected);
        if (ImGui::MenuItem(selLabel, nullptr, false, freshSelected > 0)) {
            for (int sel : selection_)
                if (sel >= 0 && sel < (int)project_.objects().size() &&
                    !hasTrack(project_.objects()[sel].name))
                    addTrackFor(project_.objects()[sel].name);
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("One track per selected object (objects that\n"
                              "already have a track are skipped).");
        ImGui::Separator();
        for (const SceneObject& o : project_.objects()) {
            const bool tracked = hasTrack(o.name);
            if (ImGui::MenuItem(o.name.c_str(), tracked ? "tracked" : nullptr,
                                false, !tracked))
                addTrackFor(o.name);
        }
        if (project_.objects().empty())
            ImGui::TextDisabled("No objects in this scene.");
        ImGui::EndPopup();
    }
    ImGui::SameLine();
    ImGui::TextDisabled("Double-click a lane to drop a key - right-click keys & labels.");

    // --- selected key inspector ----------------------------------------------
    ImGui::SeparatorText("Key");
    const bool camSel = selectedSeqTrack_ == -1 && s.cameraEnabled &&
                        selectedSeqKey_ >= 0 &&
                        selectedSeqKey_ < (int)s.cameraKeys.size();
    const bool objSel = selectedSeqTrack_ >= 0 &&
                        selectedSeqTrack_ < (int)s.tracks.size() &&
                        selectedSeqKey_ >= 0 &&
                        selectedSeqKey_ < (int)s.tracks[selectedSeqTrack_].keys.size();
    if (camSel) {
        SeqCameraKey& k = s.cameraKeys[selectedSeqKey_];
        ImGui::SetNextItemWidth(scaled(90.0f));
        if (ImGui::DragFloat("Time", &k.time, 0.02f, 0.0f, s.duration, "%.2f s")) {
            seqPlayhead_ = k.time;
        }
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            const float t = k.time;
            sortCamKeys();
            for (int i = 0; i < (int)s.cameraKeys.size(); ++i)
                if (s.cameraKeys[i].time == t) selectedSeqKey_ = i;
            changed = true;
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(scaled(110.0f));
        if (ImGui::Combo("Easing", &k.easing, kEaseNames, 3)) changed = true;
        ImGui::SameLine();
        ImGui::SetNextItemWidth(scaled(90.0f));
        if (ImGui::DragFloat("Shake", &k.shake, 0.005f, 0.0f, 2.0f, "%.2f")) {}
        changed |= ImGui::IsItemDeactivatedAfterEdit();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Handheld camera shake amplitude in world units\n"
                              "(interpolates between shots; 0 = steady).");

        // Every shot films from a Camera entity. (Legacy free shots from older
        // projects still play - their stored eye/look-at is the fallback - but
        // new shots always pick a camera; add them with + Add object > Camera.)
        const char* shotLabel = k.camera.empty() ? "<pick a camera>" : k.camera.c_str();
        ImGui::SetNextItemWidth(scaled(160.0f));
        bool anyCam = false;
        if (ImGui::BeginCombo("Shot from", shotLabel)) {
            for (const SceneObject& o : project_.objects()) {
                if (o.type != PrimitiveType::Camera) continue;
                anyCam = true;
                if (ImGui::Selectable(o.name.c_str(), o.name == k.camera)) {
                    k.camera = o.name;
                    changed = true;
                }
            }
            if (!anyCam)
                ImGui::TextDisabled("No Camera entities - add one with\n"
                                    "+ Add object > Gameplay > Camera.");
            ImGui::EndCombo();
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("The shot films from this Camera entity's pose + FOV.\n"
                              "Animate the entity (or import a take into it) for a\n"
                              "moving shot; Step easing between two cameras = a cut.");
        {
            bool found = false;
            for (const SceneObject& o : project_.objects())
                if (o.name == k.camera && o.type == PrimitiveType::Camera) {
                    ImGui::TextDisabled("Films from \"%s\" (FOV %.0f deg).",
                                        o.name.c_str(), o.cameraFov);
                    found = true;
                    break;
                }
            if (k.camera.empty())
                ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.3f, 1.0f),
                                   "Pick a camera above for this shot.");
            else if (!found)
                ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.3f, 1.0f),
                                   "Camera \"%s\" is not in this scene.",
                                   k.camera.c_str());
        }
        if (ImGui::SmallButton("Delete key")) {
            s.cameraKeys.erase(s.cameraKeys.begin() + selectedSeqKey_);
            selectedSeqKey_ = -1;
            changed = true;
        }
    } else if (objSel) {
        SeqTrack& tr = s.tracks[selectedSeqTrack_];
        SeqObjectKey& k = tr.keys[selectedSeqKey_];
        ImGui::SetNextItemWidth(scaled(90.0f));
        if (ImGui::DragFloat("Time", &k.time, 0.02f, 0.0f, s.duration, "%.2f s")) {
            seqPlayhead_ = k.time;
        }
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            const float t = k.time;
            sortObjKeys(tr);
            for (int i = 0; i < (int)tr.keys.size(); ++i)
                if (tr.keys[i].time == t) selectedSeqKey_ = i;
            changed = true;
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(scaled(110.0f));
        if (ImGui::Combo("Easing", &k.easing, kEaseNames, 3)) changed = true;
        ImGui::SameLine();
        if (ImGui::SmallButton("Re-snapshot")) {
            if (snapshotObjectKey(tr, k.time)) changed = true;
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Replace this key with the object's current pose.");
        // pose fields for the channels this track animates
        if (tr.animPos) {
            ImGui::DragFloat3("Position", k.position, 0.1f);
            changed |= ImGui::IsItemDeactivatedAfterEdit();
        }
        if (tr.animRot) {
            ImGui::DragFloat3("Rotation", k.rotation, 1.0f, -360.0f, 360.0f, "%.0f deg");
            changed |= ImGui::IsItemDeactivatedAfterEdit();
        }
        if (tr.animScale) {
            ImGui::DragFloat3("Scale", k.scale, 0.05f, 0.01f, 1000.0f);
            changed |= ImGui::IsItemDeactivatedAfterEdit();
        }
        if (tr.animColor) {
            ImGui::ColorEdit3("Color", k.color);
            changed |= ImGui::IsItemDeactivatedAfterEdit();
        }
        if (tr.animVis) {
            if (ImGui::Checkbox("Visible (holds to the next key)", &k.visible))
                changed = true;
        }
        if (!tr.animPos && !tr.animRot && !tr.animScale && !tr.animColor && !tr.animVis)
            ImGui::TextDisabled("No channels enabled - right-click the track label.");
        if (ImGui::SmallButton("Delete key")) {
            tr.keys.erase(tr.keys.begin() + selectedSeqKey_);
            selectedSeqKey_ = -1;
            changed = true;
        }
    } else {
        ImGui::TextDisabled("No key selected. Double-click a lane to drop one, or use\n"
                            "the [+] on a track label to snapshot at the playhead.\n"
                            "Play the cutscene in the game with the Play Sequence node.");
    }

    // --- Import take modal ----------------------------------------------------
    // Maps a loaded phone take (beginTakeImport) into the scene and bakes it
    // to free camera shots. The bake is pure (src/camtake.cpp) and cheap, but
    // only recomputed when a control changes; the readout shows the resulting
    // key count live so the tolerance slider can be tuned by eye.
    if (seqTakeOpen_) {
        ImGui::OpenPopup("Import camera take");
        seqTakeOpen_ = false;
    }
    if (ImGui::BeginPopupModal("Import camera take", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        const std::string file =
            std::filesystem::path(seqTakePath_).filename().string();
        if (!seqTakeError_.empty()) {
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.3f, 1.0f), "Failed to load take:");
            ImGui::TextWrapped("%s", seqTakeError_.c_str());
            ImGui::Separator();
            if (ImGui::Button("Close")) ImGui::CloseCurrentPopup();
        } else {
            ImGui::Text("%s", file.c_str());
            ImGui::TextDisabled("%s - %d samples @ %.0f Hz, %.2f s",
                                seqTake_.source.c_str(), (int)seqTake_.samples.size(),
                                seqTake_.fps, (float)seqTake_.duration());
            ImGui::Separator();

            // Target: which Camera entity the move bakes into (position +
            // rotation track + FOV + a bound shot, so it dollies along the
            // path). Two cameras in one scene each carry their own recording.
            bool anyCam = false;
            for (const SceneObject& o : project_.objects())
                if (o.type == PrimitiveType::Camera) anyCam = true;
            const std::string targetLabel =
                seqTakeTarget_.empty() ? "<pick a camera>" : seqTakeTarget_;
            ImGui::SetNextItemWidth(200.0f);
            if (ImGui::BeginCombo("Into camera", targetLabel.c_str())) {
                for (const SceneObject& o : project_.objects())
                    if (o.type == PrimitiveType::Camera)
                        if (ImGui::Selectable(o.name.c_str(), seqTakeTarget_ == o.name))
                            seqTakeTarget_ = o.name;
                if (!anyCam)
                    ImGui::TextDisabled("No Camera entities in this scene\n"
                                        "(+ Add object > Gameplay > Camera).");
                ImGui::EndCombo();
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("The move bakes into this Camera entity's transform\n"
                                  "track (position + rotation) and its FOV, plus a\n"
                                  "bound shot on the camera lane - so it dollies along\n"
                                  "the path. Two cameras each take their own recording.");
            if (!anyCam)
                ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.3f, 1.0f),
                                   "Add a Camera entity first (+ Add object >\n"
                                   "Gameplay > Camera), then re-import.");
            ImGui::Separator();

            ImGui::SetNextItemWidth(110.0f);
            if (ImGui::DragFloat("Scale (units per meter)", &seqTakeMap_.scale, 0.02f,
                                 0.01f, 1000.0f, "%.2f"))
                seqTakeDirty_ = true;
            ImGui::SetNextItemWidth(110.0f);
            if (ImGui::DragFloat("Extra yaw", &seqTakeMap_.yawDeg, 1.0f, -360.0f,
                                 360.0f, "%.0f deg"))
                seqTakeDirty_ = true;
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Rotates the whole path about the up axis, pivoting\n"
                                  "on the take's first sample.");
            if (ImGui::DragFloat3("Origin", seqTakeMap_.origin, 0.1f))
                seqTakeDirty_ = true;
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Where the take's first sample lands in the scene.");
            ImGui::SameLine();
            if (ImGui::SmallButton("From view")) {
                takeOriginAimFromView();  // position + aim
                seqTakeDirty_ = true;
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Drop the start point at the editor camera AND\n"
                                  "aim the path where you are looking.");
            if (ImGui::Checkbox("Start at playhead", &seqTakeAtPlayhead_))
                seqTakeDirty_ = true;
            ImGui::SameLine();
            ImGui::TextDisabled("(t = %.2f s)", seqPlayhead_);
            ImGui::SetNextItemWidth(160.0f);
            if (ImGui::SliderFloat("Tolerance", &seqTakeMap_.tolerance, 0.005f, 1.0f,
                                   "%.3f units", ImGuiSliderFlags_Logarithmic))
                seqTakeDirty_ = true;
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Decimation error bound in world units: dropping a\n"
                                  "sample never moves the interpolated camera by more\n"
                                  "than this. Lower = more keys, bigger PS2 tables.");

            const float wantOffset = seqTakeAtPlayhead_ ? seqPlayhead_ : 0.0f;
            if (wantOffset != seqTakeMap_.timeOffset) {
                seqTakeMap_.timeOffset = wantOffset;
                seqTakeDirty_ = true;
            }
            if (seqTakeDirty_) {
                seqTakeBaked_ = bakeCamTake(seqTake_, seqTakeMap_, &seqTakeStats_);
                seqTakeDirty_ = false;
            }
            ImGui::Separator();
            ImGui::Text("%d samples  ->  %d keys   (%.2f s, FOV %.0f deg)",
                        seqTakeStats_.sampleCount, seqTakeStats_.keyCount,
                        seqTakeStats_.duration, seqTakeStats_.fovDeg);
            if (seqTakeStats_.keyCount > 300)
                ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.3f, 1.0f),
                                   "That is a lot of keys - the PS2 keyframe table\n"
                                   "grows with every key. Raise the tolerance.");

            if (!seqTakeTarget_.empty())
                ImGui::TextDisabled("Rewrites \"%s\" transform track + FOV, and adds a\n"
                                    "bound shot if the camera lane has none yet.",
                                    seqTakeTarget_.c_str());

            ImGui::Separator();
            ImGui::BeginDisabled(seqTakeBaked_.empty() || seqTakeTarget_.empty());
            if (ImGui::Button("Import", ImVec2(120.0f, 0.0f))) {
                const float firstT = applyCamTake(s, true);
                if (firstT >= 0.0f) {
                    const float lastT =
                        s.cameraKeys.empty() ? 0.0f : s.cameraKeys.back().time;
                    if (lastT > s.duration) s.duration = lastT;
                    selectedSeqTrack_ = -1;
                    selectedSeqKey_ = -1;
                    seqPlayhead_ = firstT;
                    // keep the take loaded so the path can be re-positioned /
                    // re-oriented afterwards (Adjust imported take section)
                    seqTakeActive_ = true;
                    seqTakeSeqIdx_ = selectedSequence_;
                    changed = true;
                }
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(120.0f, 0.0f))) ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    ImGui::EndChild();
    ImGui::End();

    if (changed) commitChange();
}

// --- Material Editor ---------------------------------------------------------
// Materials are the project's plain Wavefront .mtl asset files - the same
// files objects reference via materialPath and the PS2 runtime parses with
// LeanObjLoader. The editor reads/writes the subset the whole pipeline
// understands (newmtl / Kd / map_Kd) plus a "# tyra-brightness" hint line so
// the color x brightness split survives a round trip (both multiply into the
// written Kd; every parser ignores comments). Unrecognized lines of
// hand-imported files are preserved verbatim. Edits are saved straight to
// disk on commit - assets are not project data, so no undo (same as imports).

bool App::loadMaterialFile(const std::string& relPath) {
    matEdMats_.clear();
    matEdSel_ = 0;
    std::ifstream in(std::filesystem::path(project_.dir) / relPath);
    if (!in) return false;

    std::vector<char> gotHint;
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::istringstream ss(line);
        std::string tag;
        ss >> tag;
        if (tag == "newmtl") {
            MatEdEntry e;
            ss >> e.name;
            matEdMats_.push_back(std::move(e));
            gotHint.push_back(0);
            continue;
        }
        if (matEdMats_.empty()) continue;  // stray header lines
        MatEdEntry& e = matEdMats_.back();
        if (tag == "Kd") {
            ss >> e.color[0] >> e.color[1] >> e.color[2];
        } else if (tag == "map_Kd") {
            std::vector<std::string> toks;  // "<options> filename"; filename last
            for (std::string t; ss >> t;) toks.push_back(t);
            if (!toks.empty()) {
                e.texture = toks.back();
                for (char& c : e.texture)
                    if (c == '\\') c = '/';
                // -s <u> [v] [w]: tiling (a UV multiplier); take the u factor.
                for (size_t i = 0; i + 1 < toks.size(); ++i)
                    if (toks[i] == "-s") {
                        std::istringstream(toks[i + 1]) >> e.tile;
                        break;
                    }
            }
        } else if (tag == "#") {
            std::string what;
            ss >> what;
            if (what == "tyra-brightness") {
                ss >> e.brightness;
                gotHint.back() = 1;
            }
            // other comments are dropped - saveMaterialFile rewrites its own
        } else if (!tag.empty()) {
            e.extra.push_back(line);
        }
    }
    if (matEdMats_.empty()) {  // readable but no materials - start a fresh one
        MatEdEntry e;
        e.name = std::filesystem::path(relPath).stem().string();
        matEdMats_.push_back(std::move(e));
        gotHint.push_back(1);
    }

    // The file's Kd = color x brightness; split them back for the UI. Without
    // a hint the split is ambiguous - treat Kd as the color (brightness 1),
    // except components > 1 which can only come from brightness.
    for (size_t i = 0; i < matEdMats_.size(); ++i) {
        MatEdEntry& e = matEdMats_[i];
        float b = e.brightness;
        if (!gotHint[i]) {
            b = std::max(e.color[0], std::max(e.color[1], e.color[2]));
            if (b <= 1.0f) b = 1.0f;
        }
        b = b < 0.0f ? 0.0f : b > 2.0f ? 2.0f : b;
        for (float& c : e.color) {
            c = b > 0.01f ? c / b : 1.0f;
            c = c < 0.0f ? 0.0f : c > 1.0f ? 1.0f : c;
        }
        e.brightness = b;
    }
    matEdPrevMats_ = matEdMats_;  // undo baseline for the first edit
    return true;
}

void App::saveMaterialFile() {
    if (matEdPath_.empty()) return;
    const std::filesystem::path full = std::filesystem::path(project_.dir) / matEdPath_;
    std::error_code ec;
    std::filesystem::create_directories(full.parent_path(), ec);
    std::ofstream out(full, std::ios::trunc);
    if (!out) {
        statusMessage_ = "Cannot write " + matEdPath_;
        return;
    }
    char buf[128];
    for (const MatEdEntry& e : matEdMats_) {
        out << "newmtl " << e.name << "\n";
        std::snprintf(buf, sizeof(buf), "# tyra-brightness %.4g", e.brightness);
        out << buf << "\n";
        // Kd = color x brightness, capped at 1.99: the PS2 texture-modulate
        // color tops out at 255 where 128 = 1.0 (untextured draws clamp in
        // the generated pushVert)
        auto kd = [&](int i) {
            const float v = e.color[i] * e.brightness;
            return v < 0.0f ? 0.0f : v > 1.99f ? 1.99f : v;
        };
        std::snprintf(buf, sizeof(buf), "Kd %.4f %.4f %.4f", kd(0), kd(1), kd(2));
        out << buf << "\n";
        if (!e.texture.empty()) {
            // -s tiling (repeats per world unit) matters only for terrain; skip
            // it at the default 1 to keep files clean. Wavefront: "-s u v w".
            out << "map_Kd";
            if (e.tile != 1.0f) {
                std::snprintf(buf, sizeof(buf), " -s %.4g %.4g 1", e.tile, e.tile);
                out << buf;
            }
            out << " " << e.texture << "\n";
        }
        for (const std::string& x : e.extra) out << x << "\n";
        out << "\n";
    }
    out.close();
    // every consumer caches the parsed file - drop them so the scene viewport
    // and the properties panel pick the change up next frame
    viewport_.invalidateAssets();
    modelInfoCache_.clear();
    statusMessage_ = "Saved " + matEdPath_;
}

void App::openMaterialEditor(const std::string& relPath,
                             const std::string& modelHint) {
    showMaterialEditor_ = true;
    // preview straight on the mesh the material is used by (.obj only - .glb
    // materials are embedded and never take an .mtl override)
    if (!modelHint.empty() && !isAnimatedModelPath(modelHint)) {
        matEdShape_ = 4;
        matEdModel_ = modelHint;
    }
    if (relPath.empty() || relPath == matEdPath_) return;  // keep staged edits
    if (loadMaterialFile(relPath)) {
        matEdPath_ = relPath;
        // different file - drop the paint session and its undo (the steps
        // reference the previous file's entries/textures)
        matEdPaint_ = false;
        matEdStroke_ = false;
        matEdPaintTexRel_.clear();
        matEdLayers_.clear();
        matEdUndo_.clear();
        // a model's own library previews best on its model: pick the sibling
        // .obj when the .mtl lives under res/models and no hint was given
        if (modelHint.empty() &&
            relPath.rfind("res/models/", 0) == 0) {
            std::filesystem::path obj(relPath);
            obj.replace_extension(".obj");
            std::error_code ec;
            if (std::filesystem::exists(std::filesystem::path(project_.dir) / obj,
                                        ec)) {
                matEdShape_ = 4;
                matEdModel_ = obj.generic_string();
            }
        }
    } else {
        matEdPath_.clear();
        statusMessage_ = "Cannot read " + relPath;
    }
}

// Duplicate the OPEN .mtl under a fresh "-copy" name. Referenced textures are
// copied along (once each) and the map_Kd lines rewritten, so repainting the
// duplicate never bleeds into the original's pixels.
void App::duplicateMaterialAsset() {
    if (matEdPath_.empty()) return;
    const std::filesystem::path rel(matEdPath_);
    const std::filesystem::path dirAbs =
        (std::filesystem::path(project_.dir) / rel).parent_path();
    std::error_code ec;
    const std::string stem = rel.stem().string();
    std::string newBase;
    for (int n = 1;; ++n) {
        newBase = stem + "-copy" + (n == 1 ? "" : std::to_string(n));
        if (!std::filesystem::exists(dirAbs / (newBase + ".mtl"), ec)) break;
    }

    std::map<std::string, std::string> texMap;  // old rel -> copied rel
    for (MatEdEntry& e : matEdMats_) {
        if (e.texture.empty()) continue;
        auto it = texMap.find(e.texture);
        if (it == texMap.end()) {
            const std::filesystem::path tex(e.texture);
            const std::string copied =
                (tex.parent_path() / (newBase + "-" + tex.filename().string()))
                    .generic_string();
            std::error_code cec;
            std::filesystem::copy_file(dirAbs / e.texture, dirAbs / copied,
                                       std::filesystem::copy_options::overwrite_existing,
                                       cec);
            if (!cec) {
                // paint-layer sidecar travels with its texture so the copy
                // keeps the editable stack, not just the flattened composite
                std::error_code lec;
                const std::filesystem::path srcLayers =
                    dirAbs / (e.texture + ".layers");
                if (std::filesystem::exists(srcLayers, lec))
                    std::filesystem::copy(
                        srcLayers, dirAbs / (copied + ".layers"),
                        std::filesystem::copy_options::recursive |
                            std::filesystem::copy_options::overwrite_existing,
                        lec);
            }
            // keep pointing at the shared original when the copy failed
            // (missing source) - the duplicate still renders
            it = texMap.emplace(e.texture, cec ? e.texture : copied).first;
        }
        e.texture = it->second;
    }

    matEdPath_ = (rel.parent_path() / (newBase + ".mtl")).generic_string();
    matEdSel_ = 0;
    matEdPaint_ = false;
    matEdStroke_ = false;
    matEdPaintTexRel_.clear();
    matEdLayers_.clear();
    matEdUndo_.clear();
    matEdPrevMats_ = matEdMats_;
    saveMaterialFile();
    statusMessage_ = "Duplicated to " + matEdPath_;
}

// --- Texture painting --------------------------------------------------------
// Strokes paint the selected entry's PNG through the preview mesh's UVs: the
// pick returns a surface UV, the brush splats texels around it in a CPU RGBA
// buffer, the shared GL texture is re-uploaded live (scene viewport included)
// and the PNG is written back on mouse release. The flat texture on disk IS
// the bake - the PS2 loads it like any hand-made texture.

// One stack for everything the window changes: a paint step snapshots the
// painted LAYER before the stroke, layer add/remove snapshot the structure,
// a property step snapshots the entries as they were at the previous save
// (matEdPrevMats_). Capped - textures are small (<=512^2 RGBA = 1 MB a step).
void App::matEdPushUndo(MatEdUndoStep::Kind kind, int layer,
                        const MatEdLayer* removed) {
    MatEdUndoStep s;
    s.kind = kind;
    switch (kind) {
        case MatEdUndoStep::Kind::Paint:
            if (matEdPaintW_ < 1 || layer < 0 || layer >= (int)matEdLayers_.size())
                return;
            s.texRel = matEdPaintTexRel_;
            s.layer = layer;
            s.pixels = matEdLayers_[layer].pixels;
            break;
        case MatEdUndoStep::Kind::LayerAdd:
            s.texRel = matEdPaintTexRel_;
            s.layer = layer;
            break;
        case MatEdUndoStep::Kind::LayerRemove:
            if (!removed) return;
            s.texRel = matEdPaintTexRel_;
            s.layer = layer;
            s.layerData = *removed;
            break;
        case MatEdUndoStep::Kind::Props:
            s.mats = matEdPrevMats_;
            s.sel = matEdSel_;
            matEdPrevMats_ = matEdMats_;
            break;
    }
    if (matEdUndo_.size() >= 16) matEdUndo_.erase(matEdUndo_.begin());
    matEdUndo_.push_back(std::move(s));
}

void App::matEdUndoLast() {
    if (matEdPath_.empty()) return;
    if (matEdUndo_.empty()) {
        statusMessage_ = "Material Editor: nothing to undo";
        return;
    }
    MatEdUndoStep s = std::move(matEdUndo_.back());
    matEdUndo_.pop_back();
    switch (s.kind) {
        case MatEdUndoStep::Kind::Props:
            matEdMats_ = std::move(s.mats);
            if (matEdMats_.empty()) matEdMats_.push_back(MatEdEntry{});
            matEdSel_ = s.sel < 0                         ? 0
                        : s.sel >= (int)matEdMats_.size() ? (int)matEdMats_.size() - 1
                                                          : s.sel;
            matEdPrevMats_ = matEdMats_;
            saveMaterialFile();
            statusMessage_ = "Undid material edit";
            return;
        default: break;
    }
    // paint/layer steps apply to the loaded target only - the layer stack of
    // another texture is not in memory (switch the entry back to undo there)
    if (s.texRel != matEdPaintTexRel_ || matEdPaintW_ < 1) {
        statusMessage_ = "Undo skipped - stroke belongs to " + s.texRel;
        return;
    }
    switch (s.kind) {
        case MatEdUndoStep::Kind::Paint:
            if (s.layer < 0 || s.layer >= (int)matEdLayers_.size() ||
                s.pixels.size() != matEdLayers_[s.layer].pixels.size()) {
                statusMessage_ = "Undo skipped - the layer is gone";
                return;
            }
            matEdLayers_[s.layer].pixels = std::move(s.pixels);
            matEdHaveLastUV_ = false;
            statusMessage_ = "Undid paint stroke";
            break;
        case MatEdUndoStep::Kind::LayerAdd:
            if (s.layer <= 0 || s.layer >= (int)matEdLayers_.size()) {
                statusMessage_ = "Undo skipped - the layer is gone";
                return;
            }
            matEdLayers_.erase(matEdLayers_.begin() + s.layer);
            if (matEdActiveLayer_ >= (int)matEdLayers_.size())
                matEdActiveLayer_ = (int)matEdLayers_.size() - 1;
            statusMessage_ = "Undid add layer";
            break;
        case MatEdUndoStep::Kind::LayerRemove: {
            int at = s.layer;
            if (at < 1) at = 1;
            if (at > (int)matEdLayers_.size()) at = (int)matEdLayers_.size();
            matEdLayers_.insert(matEdLayers_.begin() + at, std::move(s.layerData));
            matEdActiveLayer_ = at;
            statusMessage_ = "Undid remove layer";
            break;
        }
        default: return;
    }
    matEdComposite();
    matEdSavePaintTarget();
}

std::filesystem::path App::matEdLayersDirAbs() const {
    return std::filesystem::path(project_.dir) / (matEdPaintTexRel_ + ".layers");
}

bool App::matEdLoadPaintTarget(const std::string& texRel) {
    matEdPaintTexRel_ = texRel;  // remembered even on failure (no retry loop)
    matEdPaintPixels_.clear();
    matEdLayers_.clear();
    matEdActiveLayer_ = 0;
    matEdPaintW_ = matEdPaintH_ = 0;
    matEdHaveLastUV_ = false;
    const std::string full =
        (std::filesystem::path(project_.dir) / texRel).string();
    int w = 0, h = 0, comp = 0;
    unsigned char* pixels = stbi_load(full.c_str(), &w, &h, &comp, 4);
    if (!pixels) return false;
    matEdPaintPixels_.assign(pixels, pixels + (size_t)w * h * 4);
    stbi_image_free(pixels);
    matEdPaintW_ = w;
    matEdPaintH_ = h;

    // Layer sidecar: `<texture>.layers/layers.json` + one PNG per layer. Any
    // inconsistency (missing/mis-sized layer, bad json) falls back to a single
    // Background layer built from the composite - never fails the load.
    const std::filesystem::path dir = matEdLayersDirAbs();
    std::error_code ec;
    bool loadedStack = false;
    if (std::filesystem::exists(dir / "layers.json", ec)) {
        std::ifstream in(dir / "layers.json");
        std::stringstream ss;
        ss << in.rdbuf();
        json::Value root;
        if (json::parse(ss.str(), root)) {
            std::vector<MatEdLayer> stack;
            bool ok = true;
            if (const json::Value* arr = root.find("layers");
                arr && arr->type == json::Value::Type::Array) {
                for (const json::Value& l : arr->arr) {
                    MatEdLayer layer;
                    layer.name = l.find("name") ? l.find("name")->stringOr("Layer")
                                                : "Layer";
                    layer.blend =
                        l.find("blend") ? (int)l.find("blend")->numberOr(0) : 0;
                    layer.opacity = l.find("opacity")
                                        ? (float)l.find("opacity")->numberOr(1.0)
                                        : 1.0f;
                    layer.visible =
                        l.find("visible") ? l.find("visible")->boolOr(true) : true;
                    const std::string file =
                        l.find("file") ? l.find("file")->stringOr("") : "";
                    int lw = 0, lh = 0, lc = 0;
                    unsigned char* lp = stbi_load((dir / file).string().c_str(),
                                                  &lw, &lh, &lc, 4);
                    if (!lp || lw != w || lh != h) {
                        if (lp) stbi_image_free(lp);
                        ok = false;
                        break;
                    }
                    layer.pixels.assign(lp, lp + (size_t)w * h * 4);
                    stbi_image_free(lp);
                    stack.push_back(std::move(layer));
                }
            } else {
                ok = false;
            }
            if (ok && !stack.empty()) {
                matEdLayers_ = std::move(stack);
                if (const json::Value* a = root.find("active"))
                    matEdActiveLayer_ = (int)a->numberOr(0);
                if (matEdActiveLayer_ < 0 ||
                    matEdActiveLayer_ >= (int)matEdLayers_.size())
                    matEdActiveLayer_ = (int)matEdLayers_.size() - 1;
                loadedStack = true;
                // the sidecar is the truth - rebuild the composite from it
                // (the PNG may lag behind a crashed session)
                matEdComposite();
            } else {
                statusMessage_ =
                    "Layer sidecar unreadable - flattened to Background";
            }
        }
    }
    if (!loadedStack) {
        MatEdLayer bg;
        bg.name = "Background";
        bg.pixels = matEdPaintPixels_;
        matEdLayers_.push_back(std::move(bg));
        matEdActiveLayer_ = 0;
    }
    return true;
}

// Rebuilds the composite (what the PNG holds and every mesh samples) from
// the layer stack and uploads it into the shared GL texture. Blends run in
// 0..255 with the layer's per-pixel alpha x opacity as the mask; the
// composite alpha is a plain "over" so erased background shows through
// (decal cutouts paint the same way).
void App::matEdComposite() {
    const int w = matEdPaintW_, h = matEdPaintH_;
    if (w < 1 || h < 1 || matEdLayers_.empty()) return;
    const size_t count = (size_t)w * h;
    matEdPaintPixels_.assign(count * 4, 0);
    for (size_t li = 0; li < matEdLayers_.size(); ++li) {
        const MatEdLayer& L = matEdLayers_[li];
        if (!L.visible || L.pixels.size() != count * 4) continue;
        const float op = L.opacity < 0.0f ? 0.0f : L.opacity > 1.0f ? 1.0f : L.opacity;
        unsigned char* dst = matEdPaintPixels_.data();
        const unsigned char* src = L.pixels.data();
        for (size_t i = 0; i < count; ++i, dst += 4, src += 4) {
            const float sa = (src[3] / 255.0f) * op;
            if (sa <= 0.0f) continue;
            for (int c = 0; c < 3; ++c) {
                const int d = dst[c], s = src[c];
                int b;
                switch (L.blend) {
                    case 1: b = d * s / 255; break;               // multiply
                    case 2: b = d + s > 255 ? 255 : d + s; break; // add
                    case 3:                                       // overlay
                        b = d < 128 ? 2 * d * s / 255
                                    : 255 - 2 * (255 - d) * (255 - s) / 255;
                        break;
                    default: b = s; break;                        // normal
                }
                dst[c] = (unsigned char)(d + (b - d) * sa + 0.5f);
            }
            dst[3] = (unsigned char)(dst[3] + (255.0f - dst[3]) * sa + 0.5f);
        }
    }
    viewport_.updateTexturePixels(matEdPaintTexRel_, w, h,
                                  matEdPaintPixels_.data());
}

void App::matEdSaveLayers() {
    if (matEdPaintTexRel_.empty() || matEdPaintW_ < 1) return;
    const std::filesystem::path dir = matEdLayersDirAbs();
    std::error_code ec;
    if (matEdLayers_.size() <= 1) {
        // a lone Background equals the composite - no sidecar needed
        std::filesystem::remove_all(dir, ec);
        return;
    }
    std::filesystem::create_directories(dir, ec);
    std::ostringstream manifest;
    manifest << "{\n  \"active\": " << matEdActiveLayer_ << ",\n  \"layers\": [\n";
    for (size_t i = 0; i < matEdLayers_.size(); ++i) {
        const MatEdLayer& L = matEdLayers_[i];
        const std::string file = "layer" + std::to_string(i) + ".png";
        stbi_write_png((dir / file).string().c_str(), matEdPaintW_, matEdPaintH_,
                       4, L.pixels.data(), matEdPaintW_ * 4);
        char op[16];
        std::snprintf(op, sizeof(op), "%.4g", L.opacity);
        std::string name = L.name;
        for (char& c : name)  // keep the hand-written json trivially valid
            if (c == '"' || c == '\\') c = '\'';
        manifest << "    {\"name\": \"" << name << "\", \"blend\": " << L.blend
                 << ", \"opacity\": " << op << ", \"visible\": "
                 << (L.visible ? "true" : "false") << ", \"file\": \"" << file
                 << "\"}" << (i + 1 < matEdLayers_.size() ? "," : "") << "\n";
    }
    manifest << "  ]\n}\n";
    std::ofstream out(dir / "layers.json", std::ios::trunc);
    out << manifest.str();
    // drop stale layer files past the current count
    for (int i = (int)matEdLayers_.size();; ++i) {
        const std::filesystem::path stale = dir / ("layer" + std::to_string(i) + ".png");
        std::error_code sec;
        if (!std::filesystem::exists(stale, sec)) break;
        std::filesystem::remove(stale, sec);
    }
}

void App::matEdSavePaintTarget() {
    if (matEdPaintTexRel_.empty() || matEdPaintW_ < 1) return;
    const std::string full =
        (std::filesystem::path(project_.dir) / matEdPaintTexRel_).string();
    if (stbi_write_png(full.c_str(), matEdPaintW_, matEdPaintH_, 4,
                       matEdPaintPixels_.data(), matEdPaintW_ * 4))
        statusMessage_ = "Painted " + matEdPaintTexRel_;
    else
        statusMessage_ = "Cannot write " + matEdPaintTexRel_;
    matEdSaveLayers();
}

// One stamp at a surface UV (image space, v down), onto the ACTIVE layer
// (straight-alpha "over"; the eraser mode takes alpha away instead). The
// color brush is a soft round splat; a Brush is the brush IMAGE fitted into
// the brush diameter - its alpha is the stamp shape, its RGB the paint
// (GIMP-style dabs, not a tiled pattern). Coordinates wrap (GS textures
// repeat), so strokes cross seams cleanly. The caller recomposites once per
// frame, not per stamp.
void App::matEdStamp(float u, float v) {
    const int w = matEdPaintW_, h = matEdPaintH_;
    if (w < 1 || h < 1 || matEdActiveLayer_ < 0 ||
        matEdActiveLayer_ >= (int)matEdLayers_.size())
        return;
    MatEdLayer& L = matEdLayers_[matEdActiveLayer_];
    if (L.pixels.size() != (size_t)w * h * 4) return;
    u -= std::floor(u);
    v -= std::floor(v);
    const float cx = u * w, cy = v * h;
    const float size = matEdBrushSize_ < 1.0f ? 1.0f : matEdBrushSize_;
    const bool brush = matEdBrushMode_ == 1 && matEdPatternW_ > 0;
    const bool eraser = matEdBrushMode_ == 2;
    // Dab rotation (brush images only): the manual Angle, or a fresh random
    // one per dab. The ghost pass never rolls - a preview that spins every
    // frame reads as noise (and would advance the sequence).
    float rotC = 1.0f, rotS = 0.0f;
    if (brush) {
        float deg = matEdBrushAngle_;
        if (matEdBrushRandomRot_ && !matEdGhostPass_) {
            matEdRng_ = matEdRng_ * 1664525u + 1013904223u;
            deg = (float)(matEdRng_ >> 8) * (360.0f / 16777216.0f);
        }
        const float rad = deg * 3.14159265f / 180.0f;
        rotC = std::cos(rad);
        rotS = std::sin(rad);
    }
    // Per-dab opacity: the base Opacity, randomly reduced by up to Vary%
    // (ghost pass exempt - the preview shows the base strength).
    float dabOpacity = matEdBrushOpacity_;
    if (matEdBrushOpacityVary_ > 0.0f && !matEdGhostPass_) {
        matEdRng_ = matEdRng_ * 1664525u + 1013904223u;
        const float r01 = (float)(matEdRng_ >> 8) * (1.0f / 16777216.0f);
        dabOpacity *= 1.0f - matEdBrushOpacityVary_ * 0.01f * r01;
    }
    // a rotated square dab pokes past the inscribed circle - pad the loop
    const int r = (int)std::ceil(size * (brush ? 1.4143f : 1.0f));
    const int icx = (int)cx, icy = (int)cy;
    for (int dy = -r; dy <= r; ++dy)
        for (int dx = -r; dx <= r; ++dx) {
            const int px = icx + dx, py = icy + dy;
            const float ox = px + 0.5f - cx, oy = py + 0.5f - cy;
            float a;
            float src[3];
            if (brush) {
                // the whole brush image spans the stamp: rotate the offset
                // back into image space, then map to image UV
                const float rx = ox * rotC + oy * rotS;
                const float ry = -ox * rotS + oy * rotC;
                const float fx = rx / size * 0.5f + 0.5f;
                const float fy = ry / size * 0.5f + 0.5f;
                if (fx < 0.0f || fx >= 1.0f || fy < 0.0f || fy >= 1.0f) continue;
                const int qx = (int)(fx * matEdPatternW_);
                const int qy = (int)(fy * matEdPatternH_);
                const unsigned char* sp =
                    &matEdPatternPixels_[((size_t)qy * matEdPatternW_ + qx) * 4];
                // the image's own alpha IS the dab shape - no radial falloff
                a = dabOpacity * (sp[3] / 255.0f);
                if (a <= 0.0f) continue;
                src[0] = sp[0], src[1] = sp[1], src[2] = sp[2];
            } else {
                const float d2 = ox * ox + oy * oy;
                if (d2 > size * size) continue;
                const float t = std::sqrt(d2) / size;
                a = dabOpacity * (1.0f - t * t);  // soft falloff
                if (a <= 0.0f) continue;
                src[0] = matEdBrushColor_[0] * 255.0f;
                src[1] = matEdBrushColor_[1] * 255.0f;
                src[2] = matEdBrushColor_[2] * 255.0f;
            }
            const int sx = ((px % w) + w) % w;
            const int sy = ((py % h) + h) % h;
            unsigned char* dst = &L.pixels[((size_t)sy * w + sx) * 4];
            if (eraser) {
                dst[3] = (unsigned char)(dst[3] * (1.0f - a) + 0.5f);
                continue;
            }
            // straight-alpha "over" onto the layer: a transparent texel takes
            // the stroke color outright (no dark fringe from the RGB zeros)
            const float da = dst[3] / 255.0f;
            const float outA = a + da * (1.0f - a);
            if (outA <= 0.0f) continue;
            for (int c = 0; c < 3; ++c) {
                const float blended =
                    (src[c] * a + dst[c] * da * (1.0f - a)) / outA;
                dst[c] = (unsigned char)(blended + 0.5f);
            }
            dst[3] = (unsigned char)(outA * 255.0f + 0.5f);
        }
}

// Lays stamps along the stroke at the Spacing interval (a % of the brush
// diameter, GIMP semantics): the residual distance carries across mouse
// samples, so low spacing draws one continuous line and >=100% drops clearly
// separated dabs no matter how fast the mouse moves. A jump longer than a
// third of the texture is a UV-seam crossing - laying dabs through it would
// smear a line across unrelated texels, so the stroke restarts there instead.
void App::matEdPaintTo(float u, float v) {
    const float w = (float)matEdPaintW_, h = (float)matEdPaintH_;
    if (w < 1.0f || h < 1.0f) return;
    const float size = matEdBrushSize_ < 1.0f ? 1.0f : matEdBrushSize_;
    const float step =
        std::max(1.0f, matEdBrushSpacing_ * 0.01f * 2.0f * size);
    if (!matEdHaveLastUV_) {  // stroke start: a dab right under the click
        matEdStamp(u, v);
        matEdLastUV_[0] = u, matEdLastUV_[1] = v;
        matEdHaveLastUV_ = true;
        matEdStampResidual_ = 0.0f;
        return;
    }
    const float x0 = matEdLastUV_[0] * w, y0 = matEdLastUV_[1] * h;
    const float x1 = u * w, y1 = v * h;
    const float dx = x1 - x0, dy = y1 - y0;
    const float dist = std::sqrt(dx * dx + dy * dy);
    const float seamGuard = 0.33f * (w < h ? w : h);
    if (dist >= seamGuard) {  // seam crossing - restart, dab at the new spot
        matEdStamp(u, v);
        matEdLastUV_[0] = u, matEdLastUV_[1] = v;
        matEdStampResidual_ = 0.0f;
        return;
    }
    float done = 0.0f;
    float need = step - matEdStampResidual_;  // distance to the next dab
    while (need <= dist - done) {
        done += need;
        const float t = done / dist;
        matEdStamp((x0 + dx * t) / w, (y0 + dy * t) / h);
        need = step;
        matEdStampResidual_ = 0.0f;
    }
    matEdStampResidual_ += dist - done;
    matEdLastUV_[0] = u;
    matEdLastUV_[1] = v;
}

void App::drawMaterialEditorWindow() {
    if (!showMaterialEditor_ || !hasProject_) {
        matEdFocused_ = false;
        return;
    }

    ImGui::SetNextWindowSize(ImVec2(scaled(1020), scaled(600)),
                             ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Material Editor", &showMaterialEditor_)) {
        matEdFocused_ = false;
        ImGui::End();
        return;
    }
    // Routes Ctrl+Z to this window's own undo stack (see handleShortcuts)
    matEdFocused_ = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);

    // --- left: .mtl asset list ----------------------------------------------
    ImGui::BeginChild("##mat_list", ImVec2(scaled(190), 0), ImGuiChildFlags_Borders);
    if (ImGui::Button("+ New material...", ImVec2(-1, 0))) {
        openNewMaterialPopup_ = true;
        matEdNewError_.clear();
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Creates a .mtl in res/materials - assign it to any\n"
                          "object in Properties > Material.");
    ImGui::Separator();
    for (const std::string& rel : listMaterialAssets()) {
        if (ImGui::Selectable(rel.substr(4).c_str(), rel == matEdPath_))
            openMaterialEditor(rel);
    }
    if (listMaterialAssets().empty())
        ImGui::TextDisabled("No materials yet.\nA material is a color +\n"
                            "optional texture shared\nby any number of objects.");
    ImGui::EndChild();

    // --- "New material" modal ------------------------------------------------
    if (openNewMaterialPopup_) {
        ImGui::OpenPopup("New material");
        openNewMaterialPopup_ = false;
    }
    if (ImGui::BeginPopupModal("New material", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::SetNextItemWidth(scaled(220.0f));
        ImGui::InputText("Name", matEdNewName_, sizeof(matEdNewName_));
        if (!matEdNewError_.empty())
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.3f, 1.0f), "%s",
                               matEdNewError_.c_str());
        if (ImGui::Button("Create", ImVec2(scaled(120), 0))) {
            const std::string base =
                sanitizeAssetName(std::string(matEdNewName_).empty() ? "material"
                                                                     : matEdNewName_);
            const std::string rel = "res/materials/" + base + ".mtl";
            std::error_code ec;
            if (std::filesystem::exists(std::filesystem::path(project_.dir) / rel, ec)) {
                matEdNewError_ = base + ".mtl already exists.";
            } else {
                matEdPath_ = rel;
                matEdMats_.clear();
                MatEdEntry e;
                e.name = base;
                e.color[0] = 0.8f, e.color[1] = 0.8f, e.color[2] = 0.8f;
                matEdMats_.push_back(std::move(e));
                matEdSel_ = 0;
                matEdUndo_.clear();
                matEdPrevMats_ = matEdMats_;
                matEdPaint_ = false;
                matEdPaintTexRel_.clear();
                saveMaterialFile();
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(scaled(120), 0))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    ImGui::SameLine();

    if (matEdPath_.empty()) {
        ImGui::BeginChild("##mat_edit");
        ImGui::TextDisabled("Select a material on the left (or create one).");
        ImGui::TextDisabled("\nMaterials are .mtl files under res/ - primitives use the");
        ImGui::TextDisabled("file's first entry, models override their own libraries");
        ImGui::TextDisabled("(usemtl names resolve against it), emitters take the");
        ImGui::TextDisabled("first entry's texture for their particles.");
        ImGui::EndChild();
        ImGui::End();
        return;
    }

    if (matEdSel_ < 0 || matEdSel_ >= (int)matEdMats_.size()) matEdSel_ = 0;
    bool committed = false;

    // --- middle: the selected entry's properties ------------------------------
    // The preview is the working surface (paint lives there): give it the
    // larger share of the window, the property column keeps a workable floor.
    float previewW = ImGui::GetContentRegionAvail().x * 0.48f;
    if (previewW < scaled(260.0f)) previewW = scaled(260.0f);
    ImGui::BeginChild("##mat_edit",
                      ImVec2(ImGui::GetContentRegionAvail().x - previewW - scaled(8.0f), 0));
    ImGui::TextDisabled("%s", matEdPath_.c_str());
    ImGui::SameLine();
    if (ImGui::SmallButton("Duplicate")) duplicateMaterialAsset();
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Copy this .mtl (and its textures) under a new name -\n"
                          "safe to recolor or repaint without touching the original.");
    ImGui::SameLine();
    if (ImGui::SmallButton("Delete..."))
        requestAssetDelete(PendingAssetDelete::Material, matEdPath_,
                           matEdPath_.rfind("res/", 0) == 0 ? matEdPath_.substr(4)
                                                            : matEdPath_);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Remove this .mtl from the project (asks first).\n"
                          "Objects using it fall back to plain color, models\n"
                          "to their own libraries; textures stay on disk.");
    ImGui::SameLine();
    ImGui::BeginDisabled(matEdUndo_.empty());
    if (ImGui::SmallButton("Undo")) matEdUndoLast();
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Ctrl+Z while this window is focused: undoes the\n"
                          "last paint stroke or material edit (the editor's\n"
                          "own history - scene undo is untouched).");

    // entry list within the file (universal libraries hold several; the FIRST
    // one is what primitives and emitters use)
    if (matEdMats_.size() > 1) {
        ImGui::SetNextItemWidth(scaled(180.0f));
        if (ImGui::BeginCombo("Entry", matEdMats_[matEdSel_].name.c_str())) {
            for (int i = 0; i < (int)matEdMats_.size(); ++i) {
                ImGui::PushID(i);
                std::string label = matEdMats_[i].name;
                if (i == 0) label += "  [primitives use this]";
                if (ImGui::Selectable(label.c_str(), i == matEdSel_)) matEdSel_ = i;
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
    }
    if (ImGui::SmallButton("+ Add")) {
        MatEdEntry e;
        std::string base = "mat";
        for (int n = 1;; ++n) {
            e.name = base + "-" + std::to_string(n);
            bool taken = false;
            for (const auto& other : matEdMats_) taken |= (other.name == e.name);
            if (!taken) break;
        }
        matEdMats_.push_back(std::move(e));
        matEdSel_ = (int)matEdMats_.size() - 1;
        committed = true;
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Another material in the same file. Only universal\n"
                          "model libraries need this - usemtl names resolve\n"
                          "against the file. Primitives always use the first.");
    if (matEdMats_.size() > 1) {
        ImGui::SameLine();
        if (ImGui::SmallButton("Remove")) {
            matEdMats_.erase(matEdMats_.begin() + matEdSel_);
            if (matEdSel_ >= (int)matEdMats_.size())
                matEdSel_ = (int)matEdMats_.size() - 1;
            committed = true;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Up") && matEdSel_ > 0) {
            std::swap(matEdMats_[matEdSel_], matEdMats_[matEdSel_ - 1]);
            --matEdSel_;
            committed = true;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Down") && matEdSel_ + 1 < (int)matEdMats_.size()) {
            std::swap(matEdMats_[matEdSel_], matEdMats_[matEdSel_ + 1]);
            ++matEdSel_;
            committed = true;
        }
    }

    MatEdEntry& e = matEdMats_[matEdSel_];
    ImGui::Separator();

    char nameBuf[64];
    std::snprintf(nameBuf, sizeof(nameBuf), "%s", e.name.c_str());
    ImGui::SetNextItemWidth(scaled(180.0f));
    if (ImGui::InputText("Name", nameBuf, sizeof(nameBuf))) {
        // .mtl names are whitespace-delimited tokens - keep them pipeline-safe
        e.name = sanitizeAssetName(nameBuf);
        if (e.name.empty()) e.name = "material";
    }
    committed |= ImGui::IsItemDeactivatedAfterEdit();

    ImGui::ColorEdit3("Color", e.color);
    committed |= ImGui::IsItemDeactivatedAfterEdit();

    ImGui::SliderFloat("Brightness", &e.brightness, 0.0f, 2.0f, "%.2f");
    committed |= ImGui::IsItemDeactivatedAfterEdit();
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Multiplies the color. Above 1.0 a textured material\n"
                          "over-brightens its texture (PS2 modulation goes up\n"
                          "to 2x); plain colors clamp at full white.");

    // texture: PNGs living next to the .mtl (map_Kd resolves relative to it -
    // the game copies that rule)
    const std::filesystem::path mtlDirAbs =
        (std::filesystem::path(project_.dir) / matEdPath_).parent_path();
    {
        const char* noneLabel = "<none - plain color>";
        ImGui::SetNextItemWidth(scaled(240.0f));
        if (ImGui::BeginCombo("Texture",
                              e.texture.empty() ? noneLabel : e.texture.c_str())) {
            if (ImGui::Selectable(noneLabel, e.texture.empty()) && !e.texture.empty()) {
                e.texture.clear();
                committed = true;
            }
            std::error_code ec;
            for (const auto& f :
                 std::filesystem::recursive_directory_iterator(mtlDirAbs, ec)) {
                if (!f.is_regular_file()) continue;
                std::string ext = f.path().extension().string();
                for (char& c : ext) c = (char)tolower((unsigned char)c);
                if (ext != ".png") continue;
                const std::string rel =
                    std::filesystem::relative(f.path(), mtlDirAbs, ec).generic_string();
                if (ImGui::Selectable(rel.c_str(), rel == e.texture) &&
                    rel != e.texture) {
                    e.texture = rel;
                    committed = true;
                }
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Import PNG...")) {
                const std::string src = pickPngFile();
                if (!src.empty()) {
                    const std::string fileName = sanitizeAssetName(
                        std::filesystem::path(src).filename().string());
                    std::error_code cec;
                    std::filesystem::copy_file(
                        src, mtlDirAbs / fileName,
                        std::filesystem::copy_options::overwrite_existing, cec);
                    if (cec) {
                        statusMessage_ = "Texture import failed: " + cec.message();
                    } else {
                        e.texture = fileName;
                        committed = true;
                    }
                }
            }
            if (ImGui::MenuItem("New paintable texture...")) {
                openNewTexturePopup_ = true;
                std::snprintf(matEdNewTexName_, sizeof(matEdNewTexName_), "%s-tex",
                              e.name.c_str());
                matEdNewTexError_.clear();
            }
            ImGui::EndCombo();
        }
    }

    // --- "New texture" modal: a blank pow2 PNG next to the .mtl, assigned as
    // this entry's map_Kd - the canvas for the paint tool.
    if (openNewTexturePopup_) {
        ImGui::OpenPopup("New texture");
        openNewTexturePopup_ = false;
    }
    if (ImGui::BeginPopupModal("New texture", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::SetNextItemWidth(scaled(220.0f));
        ImGui::InputText("Name", matEdNewTexName_, sizeof(matEdNewTexName_));
        const char* sizes[] = {"64 x 64", "128 x 128", "256 x 256", "512 x 512"};
        ImGui::SetNextItemWidth(scaled(220.0f));
        ImGui::Combo("Size", &matEdNewTexSize_, sizes, 4);
        ImGui::TextDisabled("Power-of-two, as the PS2 GS requires. Bigger eats\n"
                            "video memory - 256 is plenty for most props.");
        ImGui::ColorEdit3("Fill color", matEdNewTexColor_);
        if (!matEdNewTexError_.empty())
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.3f, 1.0f), "%s",
                               matEdNewTexError_.c_str());
        if (ImGui::Button("Create", ImVec2(scaled(120), 0))) {
            const std::string base = sanitizeAssetName(
                std::string(matEdNewTexName_).empty() ? "painted" : matEdNewTexName_);
            const std::string fileName = base + ".png";
            std::error_code cec;
            if (std::filesystem::exists(mtlDirAbs / fileName, cec)) {
                matEdNewTexError_ = fileName + " already exists.";
            } else {
                const int s = 64 << (matEdNewTexSize_ < 0   ? 0
                                     : matEdNewTexSize_ > 3 ? 3
                                                            : matEdNewTexSize_);
                std::vector<unsigned char> px((size_t)s * s * 4);
                for (size_t i = 0; i < px.size(); i += 4) {
                    px[i] = (unsigned char)(matEdNewTexColor_[0] * 255.0f + 0.5f);
                    px[i + 1] = (unsigned char)(matEdNewTexColor_[1] * 255.0f + 0.5f);
                    px[i + 2] = (unsigned char)(matEdNewTexColor_[2] * 255.0f + 0.5f);
                    px[i + 3] = 255;
                }
                if (stbi_write_png((mtlDirAbs / fileName).string().c_str(), s, s, 4,
                                   px.data(), s * 4)) {
                    e.texture = fileName;
                    committed = true;
                    matEdPaint_ = true;  // the whole point of a blank canvas
                    ImGui::CloseCurrentPopup();
                } else {
                    matEdNewTexError_ = "Cannot write " + fileName;
                }
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(scaled(120), 0))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    if (!e.texture.empty()) {
        const std::filesystem::path texAbs = mtlDirAbs / e.texture;
        std::error_code ec;
        if (!std::filesystem::exists(texAbs, ec)) {
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.3f, 1.0f),
                               "Texture missing - renders as plain color.");
        } else {
            int tw = 0, th = 0, comp = 0;
            if (stbi_info(texAbs.string().c_str(), &tw, &th, &comp)) {
                const bool pow2 = tw > 0 && th > 0 && (tw & (tw - 1)) == 0 &&
                                  (th & (th - 1)) == 0;
                if (!pow2)
                    ImGui::TextColored(
                        ImVec4(1.0f, 0.7f, 0.3f, 1.0f),
                        "%dx%d - not power-of-two; the PS2 GS needs\n"
                        "pow2 texture sizes (e.g. 128x128, 256x256).", tw, th);
                else
                    ImGui::TextDisabled("%dx%d", tw, th);
            }
        }

        // Tiling (map_Kd -s): how densely the texture repeats. Used by terrain
        // (which generates its own UVs); objects carry baked UVs and ignore it.
        ImGui::SetNextItemWidth(scaled(180.0f));
        ImGui::DragFloat("Tile repeat", &e.tile, 0.05f, 0.01f, 64.0f, "%.2f/unit");
        committed |= ImGui::IsItemDeactivatedAfterEdit();
        if (e.tile < 0.01f) e.tile = 0.01f;
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Terrain only: texture repeats per world unit.\n"
                              "Higher = smaller, denser tiles. Objects use their\n"
                              "mesh UVs and ignore this.");
    }

    ImGui::Spacing();
    ImGui::TextDisabled("Saved to the file on every change. The object's own\n"
                        "Color multiplies on top per object.");
    ImGui::EndChild();

    ImGui::SameLine();

    // --- right: live preview + paint tool ---------------------------------------
    ImGui::BeginChild("##mat_preview", ImVec2(previewW, 0));  // previewW already scaled
    {
        ImGuiIO& io = ImGui::GetIO();
        const MatEdEntry& sel = matEdMats_[matEdSel_];

        // Shape: the four unit primitives or any of the project's .obj models
        const char* shapes[] = {"Box", "Sphere", "Cylinder", "Cone"};
        const std::string shapeLabel =
            matEdShape_ == 4
                ? std::filesystem::path(matEdModel_).filename().string()
                : shapes[matEdShape_ < 0 || matEdShape_ > 3 ? 1 : matEdShape_];
        ImGui::SetNextItemWidth(scaled(110.0f));
        if (ImGui::BeginCombo("##mat_shape", shapeLabel.c_str())) {
            for (int i = 0; i < 4; ++i)
                if (ImGui::Selectable(shapes[i], matEdShape_ == i)) matEdShape_ = i;
            const std::vector<std::string> models = listAssetFiles("models", ".obj");
            if (!models.empty()) ImGui::Separator();
            for (const std::string& m : models) {
                const std::string rel = "res/models/" + m;
                if (ImGui::Selectable(m.c_str(),
                                      matEdShape_ == 4 && matEdModel_ == rel)) {
                    matEdShape_ = 4;
                    matEdModel_ = rel;
                }
            }
            ImGui::EndCombo();
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Preview mesh. Pick one of your models to see the\n"
                              "material (and paint) on the real thing - this file\n"
                              "acts as the model's material override, entries are\n"
                              "matched to the model's usemtl names.");
        ImGui::SameLine();
        ImGui::Checkbox("Spin", &matEdSpin_);
        if (matEdSpin_ && !matEdPaint_) matEdAngle_ += io.DeltaTime * 24.0f;

        // Staged values of the selected entry (live during slider drags)
        float kd[3];
        for (int i = 0; i < 3; ++i) {
            kd[i] = sel.color[i] * sel.brightness;
            if (kd[i] > 1.99f) kd[i] = 1.99f;
        }
        const std::string texRel =
            sel.texture.empty()
                ? ""
                : (std::filesystem::path(matEdPath_).parent_path() / sel.texture)
                      .generic_string();

        // --- paint tool ------------------------------------------------------
        if (ImGui::Checkbox("Paint", &matEdPaint_) && matEdPaint_)
            matEdPaintTexRel_.clear();  // (re)load the target below
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Paint on the preview mesh, straight into this\n"
                              "entry's texture (through the UVs). The PNG is\n"
                              "saved on every stroke - what you paint is what\n"
                              "the PS2 loads.");
        if (matEdPaint_) {
            ImGui::SameLine();
            ImGui::Checkbox("Live dab", &matEdGhostOn_);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Preview the stamp under the cursor before\n"
                                  "clicking - one uncommitted dab drawn on the\n"
                                  "preview each frame.");
        }
        bool canPaint = false;
        if (matEdPaint_) {
            if (texRel.empty()) {
                ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.3f, 1.0f),
                                   "No texture on this entry.\n"
                                   "Texture > New paintable texture...");
            } else {
                if (matEdPaintTexRel_ != texRel && !matEdLoadPaintTarget(texRel))
                    statusMessage_ = "Cannot read " + texRel;
                canPaint = matEdPaintW_ > 0;
                if (!canPaint)
                    ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.3f, 1.0f),
                                       "Texture file unreadable.");
            }
        }
        if (matEdPaint_ && canPaint) {

            ImGui::SetNextItemWidth(scaled(90.0f));
            ImGui::Combo("##brush_mode", &matEdBrushMode_,
                         "Color\0Brush\0Eraser\0");
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Color: solid paint. Brush: paints with a\n"
                                  "project brush image (res/brushes), tiled\n"
                                  "across the texture. Eraser: takes paint\n"
                                  "off the active layer.");
            if (matEdBrushMode_ == 0) {
                ImGui::SameLine();
                ImGui::ColorEdit3("##brush_col", matEdBrushColor_,
                                  ImGuiColorEditFlags_NoInputs);
            } else if (matEdBrushMode_ == 1) {
                ImGui::SameLine();
                // Brushes are project-global assets: res/brushes/*.png
                const std::string brushLabel =
                    matEdBrush_.empty()
                        ? "<pick brush>"
                        : std::filesystem::path(matEdBrush_).filename().string();
                ImGui::SetNextItemWidth(-FLT_MIN);
                if (ImGui::BeginCombo("##brush_pick", brushLabel.c_str())) {
                    for (const std::string& b : listAssetFiles("brushes", ".png")) {
                        const std::string rel = "res/brushes/" + b;
                        if (ImGui::Selectable(b.c_str(), rel == matEdBrush_))
                            matEdBrush_ = rel;
                    }
                    if (listAssetFiles("brushes", ".png").empty())
                        ImGui::TextDisabled("No brushes yet - import one below.");
                    ImGui::Separator();
                    if (ImGui::MenuItem("Import brush from PNG...")) {
                        const std::string src = pickPngFile();
                        if (!src.empty()) {
                            const std::string fileName = sanitizeAssetName(
                                std::filesystem::path(src).filename().string());
                            const std::filesystem::path dirAbs =
                                std::filesystem::path(project_.dir) / "res" /
                                "brushes";
                            std::error_code cec;
                            std::filesystem::create_directories(dirAbs, cec);
                            std::filesystem::copy_file(
                                src, dirAbs / fileName,
                                std::filesystem::copy_options::overwrite_existing,
                                cec);
                            if (cec) {
                                statusMessage_ =
                                    "Brush import failed: " + cec.message();
                            } else {
                                matEdBrush_ = "res/brushes/" + fileName;
                                statusMessage_ = "Imported brush " + fileName;
                            }
                        }
                    }
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip(
                            "Copies the PNG into res/brushes - brushes are\n"
                            "shared by the whole project and never ship\n"
                            "with the game.");
                    ImGui::EndCombo();
                }
            }
            ImGui::SetNextItemWidth(scaled(110.0f));
            ImGui::SliderFloat("Size", &matEdBrushSize_, 1.0f, 128.0f, "%.0f px",
                               ImGuiSliderFlags_Logarithmic);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(scaled(110.0f));
            ImGui::SliderFloat("Opacity", &matEdBrushOpacity_, 0.05f, 1.0f,
                               "%.2f");
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("How strongly each dab covers what is\n"
                                  "underneath (per dab, not per stroke).");
            ImGui::SetNextItemWidth(scaled(110.0f));
            ImGui::SliderFloat("Spacing", &matEdBrushSpacing_, 5.0f, 300.0f,
                               "%.0f%%", ImGuiSliderFlags_Logarithmic);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(
                    "Distance between dabs, as %% of the brush size\n"
                    "(GIMP-style). Low = one continuous line, 100%%\n"
                    "and up = clearly separated stamps.");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(scaled(110.0f));
            ImGui::SliderFloat("Vary", &matEdBrushOpacityVary_, 0.0f, 100.0f,
                               "%.0f%%");
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(
                    "Random per-dab opacity variation: each dab's\n"
                    "opacity is reduced by up to this much - organic,\n"
                    "hand-worn strokes instead of a uniform coat.");
            if (matEdBrushMode_ == 1) {
                // dab orientation: dial bricks in by hand, or scatter organic
                // splats with a fresh random rotation per dab
                ImGui::SetNextItemWidth(scaled(110.0f));
                ImGui::BeginDisabled(matEdBrushRandomRot_);
                ImGui::SliderFloat("Angle", &matEdBrushAngle_, 0.0f, 360.0f,
                                   "%.0f deg");
                ImGui::EndDisabled();
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Rotates every dab - orient bricks,\n"
                                      "planks, arrows...");
                ImGui::SameLine();
                ImGui::Checkbox("Random", &matEdBrushRandomRot_);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Fresh random rotation per dab - organic\n"
                                      "scatter (leaves, splats, rubble).");
                // (re)decode the brush image when the pick changes
                if (matEdBrush_ != matEdPatternLoaded_) {
                    matEdPatternLoaded_ = matEdBrush_;
                    matEdPatternPixels_.clear();
                    matEdPatternW_ = matEdPatternH_ = 0;
                    int w = 0, h = 0, comp = 0;
                    unsigned char* p = stbi_load(
                        (std::filesystem::path(project_.dir) / matEdBrush_)
                            .string()
                            .c_str(),
                        &w, &h, &comp, 4);
                    if (p) {
                        matEdPatternPixels_.assign(p, p + (size_t)w * h * 4);
                        matEdPatternW_ = w;
                        matEdPatternH_ = h;
                        stbi_image_free(p);
                    }
                }
                if (!matEdBrush_.empty() && matEdPatternW_ == 0)
                    ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.3f, 1.0f),
                                       "Brush unreadable.");
            }

            // --- layers: painted strokes land on the active (selected) layer;
            // the composite of the stack is the PNG that ships. Top-most first.
            ImGui::Spacing();
            ImGui::TextDisabled("Layers");
            ImGui::SameLine();
            if (ImGui::SmallButton("+##layer_add")) {
                MatEdLayer l;
                int n = 1;
                for (const MatEdLayer& other : matEdLayers_)
                    if (other.name.rfind("Layer ", 0) == 0) ++n;
                l.name = "Layer " + std::to_string(n);
                l.pixels.assign((size_t)matEdPaintW_ * matEdPaintH_ * 4, 0);
                const int at = matEdActiveLayer_ + 1;
                matEdLayers_.insert(matEdLayers_.begin() + at, std::move(l));
                matEdActiveLayer_ = at;
                matEdPushUndo(MatEdUndoStep::Kind::LayerAdd, at);
                matEdSaveLayers();
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("New transparent layer above the active one.");
            ImGui::SameLine();
            ImGui::BeginDisabled(matEdActiveLayer_ == 0);
            if (ImGui::SmallButton("-##layer_del") && matEdActiveLayer_ > 0) {
                matEdPushUndo(MatEdUndoStep::Kind::LayerRemove, matEdActiveLayer_,
                              &matEdLayers_[matEdActiveLayer_]);
                matEdLayers_.erase(matEdLayers_.begin() + matEdActiveLayer_);
                if (matEdActiveLayer_ >= (int)matEdLayers_.size())
                    matEdActiveLayer_ = (int)matEdLayers_.size() - 1;
                matEdComposite();
                matEdSavePaintTarget();
            }
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Delete the active layer (Background stays;\n"
                                  "undo with Ctrl+Z).");
            ImGui::SameLine();
            ImGui::BeginDisabled(matEdActiveLayer_ + 1 >= (int)matEdLayers_.size());
            if (ImGui::SmallButton("Up##layer_up")) {
                std::swap(matEdLayers_[matEdActiveLayer_],
                          matEdLayers_[matEdActiveLayer_ + 1]);
                ++matEdActiveLayer_;
                matEdComposite();
                matEdSavePaintTarget();
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::BeginDisabled(matEdActiveLayer_ <= 1);
            if (ImGui::SmallButton("Down##layer_dn")) {
                std::swap(matEdLayers_[matEdActiveLayer_],
                          matEdLayers_[matEdActiveLayer_ - 1]);
                --matEdActiveLayer_;
                matEdComposite();
                matEdSavePaintTarget();
            }
            ImGui::EndDisabled();

            const char* blends[] = {"Normal", "Multiply", "Add", "Overlay"};
            for (int i = (int)matEdLayers_.size() - 1; i >= 0; --i) {
                MatEdLayer& L = matEdLayers_[i];
                ImGui::PushID(i);
                if (ImGui::Checkbox("##vis", &L.visible)) {
                    matEdComposite();
                    matEdSavePaintTarget();
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Show/hide layer");
                ImGui::SameLine();
                if (ImGui::Selectable(L.name.c_str(), matEdActiveLayer_ == i,
                                      0, ImVec2(scaled(96.0f), 0)))
                    matEdActiveLayer_ = i;
                if (i > 0) {
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(scaled(86.0f));
                    int blend = L.blend;
                    if (ImGui::Combo("##blend", &blend, blends, 4)) {
                        L.blend = blend;
                        matEdComposite();
                        matEdSavePaintTarget();
                    }
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(scaled(64.0f));
                    if (ImGui::DragFloat("##opacity", &L.opacity, 0.01f, 0.0f,
                                         1.0f, "%.2f"))
                        matEdComposite();
                    if (ImGui::IsItemDeactivatedAfterEdit()) matEdSavePaintTarget();
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Layer opacity");
                }
                ImGui::PopID();
            }
        }

        // --- the preview image + mouse interaction ---------------------------
        Viewport::MatPreviewDesc desc;
        desc.kd[0] = kd[0], desc.kd[1] = kd[1], desc.kd[2] = kd[2];
        desc.texRel = texRel;
        desc.shape = matEdShape_;
        desc.modelRel = matEdModel_;
        desc.mtlRel = matEdPath_;
        desc.entryName = sel.name;
        desc.angleDeg = matEdAngle_;
        desc.pitchDeg = matEdPitch_;
        desc.zoom = matEdZoom_;

        const ImVec2 avail = ImGui::GetContentRegionAvail();
        const int pw = (int)avail.x < 1 ? 1 : (int)avail.x;
        const int ph = (int)avail.y < 1 ? 1 : (int)avail.y;
        const uint32_t tex = viewport_.renderMaterialPreview(pw, ph, desc);
        if (tex) {
            const ImVec2 imgPos = ImGui::GetCursorScreenPos();
            ImGui::Image((ImTextureID)(intptr_t)tex, ImVec2((float)pw, (float)ph),
                         ImVec2(0, 1), ImVec2(1, 0));
            ImGui::SetCursorScreenPos(imgPos);
            ImGui::InvisibleButton("##mat_prev_in", ImVec2((float)pw, (float)ph),
                                   ImGuiButtonFlags_MouseButtonLeft |
                                       ImGuiButtonFlags_MouseButtonRight);
            const bool hovered = ImGui::IsItemHovered();
            const bool active = ImGui::IsItemActive();

            if (hovered && io.MouseWheel != 0.0f) {
                matEdZoom_ *= std::pow(1.15f, io.MouseWheel);
                matEdZoom_ = matEdZoom_ < 0.2f ? 0.2f
                             : matEdZoom_ > 12.0f ? 12.0f
                                                  : matEdZoom_;
            }
            // orbit: LMB (RMB while painting - LMB is the brush then)
            const bool orbiting =
                active && ((matEdPaint_ && ImGui::IsMouseDown(1)) ||
                           (!matEdPaint_ && ImGui::IsMouseDown(0)));
            if (orbiting) {
                matEdAngle_ += io.MouseDelta.x * 0.5f;
                matEdPitch_ += io.MouseDelta.y * 0.4f;
                matEdPitch_ = matEdPitch_ < -5.0f ? -5.0f
                              : matEdPitch_ > 85.0f ? 85.0f
                                                    : matEdPitch_;
            }

            if (matEdPaint_ && canPaint) {
                if (ImGui::IsItemActivated() && ImGui::IsMouseDown(0)) {
                    // snapshot the painted layer before the stroke
                    matEdPushUndo(MatEdUndoStep::Kind::Paint, matEdActiveLayer_);
                    matEdStroke_ = true;
                    matEdHaveLastUV_ = false;
                }
                if (matEdStroke_ && ImGui::IsMouseDown(0)) {
                    const float u = (io.MousePos.x - imgPos.x) / (float)pw;
                    const float v = (io.MousePos.y - imgPos.y) / (float)ph;
                    float hu = 0.0f, hv = 0.0f;
                    bool paintable = false;
                    if (viewport_.materialPreviewPick(u, v, hu, hv, paintable) &&
                        paintable) {
                        matEdPaintTo(hu, hv);
                        matEdComposite();  // layer stack -> texture + GL upload
                    } else {
                        matEdHaveLastUV_ = false;  // left the paintable surface
                    }
                }
                if (matEdStroke_ && !ImGui::IsMouseDown(0)) {
                    matEdStroke_ = false;
                    matEdSavePaintTarget();  // the painted PNG ships as-is
                }
                // Live dab ghost: composite one uncommitted stamp under the
                // cursor. The active layer is backed up and restored right
                // away, so everything else (undo snapshots, stroke starts,
                // saves - which all recomposite first) sees clean layers.
                bool ghostDrawn = false;
                if (matEdGhostOn_ && hovered && !matEdStroke_ &&
                    !ImGui::IsMouseDown(0) && matEdActiveLayer_ >= 0 &&
                    matEdActiveLayer_ < (int)matEdLayers_.size()) {
                    const float u = (io.MousePos.x - imgPos.x) / (float)pw;
                    const float v = (io.MousePos.y - imgPos.y) / (float)ph;
                    float hu = 0.0f, hv = 0.0f;
                    bool paintable = false;
                    if (viewport_.materialPreviewPick(u, v, hu, hv, paintable) &&
                        paintable) {
                        MatEdLayer& L = matEdLayers_[matEdActiveLayer_];
                        std::vector<unsigned char> backup = L.pixels;
                        matEdGhostPass_ = true;
                        matEdStamp(hu, hv);
                        matEdGhostPass_ = false;
                        matEdComposite();
                        L.pixels = std::move(backup);
                        matEdGhostShown_ = true;
                        ghostDrawn = true;
                    }
                }
                if (matEdGhostShown_ && !ghostDrawn) {
                    matEdComposite();  // wipe the ghost off the composite
                    matEdGhostShown_ = false;
                }
                if (hovered) {
                    // approximate brush footprint (cosmetic cursor)
                    const float r = matEdBrushSize_ / (float)matEdPaintW_ *
                                    (float)(pw < ph ? pw : ph) * 0.5f;
                    ImGui::GetWindowDrawList()->AddCircle(
                        io.MousePos, r < 3.0f ? 3.0f : r,
                        IM_COL32(255, 255, 255, 180), 0, 1.5f);
                }
            }
        }
    }
    ImGui::EndChild();

    ImGui::End();

    if (committed) {
        matEdPushUndo(MatEdUndoStep::Kind::Props);  // pre-edit entries -> Ctrl+Z
        saveMaterialFile();
    }
}

// --- Options-menu "option blocks" ------------------------------------------
// Ready-made stateful menu rows (Menu Editor > Insert option block) that bind
// to a built-in engine setting. Each is a Toggle/Choice entry with a curated
// option set, a backing save value and a MenuEntry::Setting binding the
// generated game reads (applyMenuBindings). The option index -> value mapping
// is spread evenly across the options, so editing the labels/count still maps
// sensibly (e.g. six volume options -> 0/20/40/.../100 %).
namespace {
struct OptionBlockSpec {
    const char* label;         // menu row label
    int action;                // MenuEntry::Toggle | Choice
    const char* valueName;     // backing save value (created if missing)
    float defaultIndex;        // initial option index (save value default)
    int bind;                  // MenuEntry::Setting
    std::vector<const char*> options;
};
// Index order == the Insert-option-block menu order below.
const OptionBlockSpec kOptionBlocks[] = {
    {"MUSIC", MenuEntry::Choice, "opt_music_vol", 4.0f, MenuEntry::BindMusicVolume,
     {"0%", "25%", "50%", "75%", "100%"}},
    {"SOUND", MenuEntry::Choice, "opt_sfx_vol", 4.0f, MenuEntry::BindSfxVolume,
     {"0%", "25%", "50%", "75%", "100%"}},
    {"DEADZONE", MenuEntry::Choice, "opt_deadzone", 2.0f, MenuEntry::BindDeadzone,
     {"None", "Low", "Medium", "High", "Max"}},
    {"AIM CURVE", MenuEntry::Choice, "opt_stick_curve", 0.0f, MenuEntry::BindStickCurve,
     {"Linear", "Smooth", "Precise"}},
    {"DISPLAY", MenuEntry::Choice, "opt_display", 0.0f, MenuEntry::BindDisplayMode,
     {"480i", "480p", "1080i"}},
    {"ASPECT", MenuEntry::Toggle, "opt_widescreen", 0.0f, MenuEntry::BindWidescreen,
     {"4:3", "16:9"}},
};
constexpr int kOptionBlockCount = (int)(sizeof(kOptionBlocks) / sizeof(kOptionBlocks[0]));

// Append a configured option-block row to a menu, creating its backing save
// value (with the block's initial option index as the default) if it does not
// exist yet. No-op when the menu is already at the entry cap.
void addOptionBlock(Project& p, GameMenu& m, int kind) {
    if (kind < 0 || kind >= kOptionBlockCount) return;
    if ((int)m.entries.size() >= menubake::kMaxEntries) return;
    const OptionBlockSpec& s = kOptionBlocks[kind];
    bool haveValue = false;
    for (const SaveValue& sv : p.saveValues)
        if (sv.name == s.valueName) haveValue = true;
    if (!haveValue) {
        SaveValue sv;
        sv.name = s.valueName;
        sv.value = s.defaultIndex;
        p.saveValues.push_back(std::move(sv));
    }
    MenuEntry en;
    en.label = s.label;
    en.action = s.action;
    en.param = s.valueName;
    en.settingBind = s.bind;
    for (const char* opt : s.options) en.options.push_back(opt);
    m.entries.push_back(std::move(en));
}

// Scaffold a full paged options menu: a root OPTIONS menu whose rows open one
// submenu per category (audio / controls / display), each pre-filled with the
// matching option blocks. Triangle backs out of a submenu; the root's CLOSE
// row dismisses everything. Returns the new root menu's index.
int addOptionsMenuPages(Project& p) {
    auto uniqueName = [&](const std::string& base) {
        std::string name = base;
        for (int n = 2;; ++n) {
            bool taken = false;
            for (const GameMenu& o : p.menus) taken |= (o.name == name);
            if (!taken) break;
            name = base + "-" + std::to_string(n);
        }
        return name;
    };
    // Submenus rely on Triangle to return to the root (the baked panel already
    // shows the "^ BACK" hint); a Close-action "back" row would instead dismiss
    // the whole menu tree, so submenus carry only their option blocks.
    auto makeSub = [&](const char* base, const char* title, int b0, int b1) {
        GameMenu sub;
        sub.name = uniqueName(base);
        sub.title = title;
        addOptionBlock(p, sub, b0);
        addOptionBlock(p, sub, b1);
        p.menus.push_back(std::move(sub));
        return p.menus.back().name;
    };
    const std::string audio = makeSub("options-audio", "AUDIO", 0, 1);
    const std::string controls = makeSub("options-controls", "CONTROLS", 2, 3);
    const std::string display = makeSub("options-display", "DISPLAY", 4, 5);
    GameMenu root;
    root.name = uniqueName("options");
    root.title = "OPTIONS";
    root.entries.push_back(MenuEntry{"AUDIO", MenuEntry::OpenMenu, audio, 0.0f});
    root.entries.push_back(MenuEntry{"CONTROLS", MenuEntry::OpenMenu, controls, 0.0f});
    root.entries.push_back(MenuEntry{"DISPLAY", MenuEntry::OpenMenu, display, 0.0f});
    root.entries.push_back(MenuEntry{"CLOSE", MenuEntry::Close, "", 0.0f});
    p.menus.push_back(std::move(root));
    return (int)p.menus.size() - 1;
}
}  // namespace

// Menu Editor window: menu list on the left, the selected menu's properties,
// entries and a live baked-panel preview (the exact pixels the PS2 will
// draw) on the right.
void App::drawMenusWindow() {
    if (!showMenusEditor_ || !hasProject_) return;

    ImGui::SetNextWindowSize(ImVec2(scaled(680), scaled(540)),
                             ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Menu Editor", &showMenusEditor_)) {
        ImGui::End();
        return;
    }

    bool changed = false;

    // --- left: menu list -------------------------------------------------
    ImGui::BeginChild("##menu_list", ImVec2(scaled(170), 0), ImGuiChildFlags_Borders);
    if (ImGui::Button("+ New menu", ImVec2(-1, 0))) {
        int counter = 0;
        std::string name;
        for (;;) {
            name = "menu-" + std::to_string(++counter);
            bool taken = false;
            for (const auto& m : project_.menus) taken |= (m.name == name);
            if (!taken) break;
        }
        GameMenu m;
        m.name = name;
        m.title = "MENU";
        m.entries.push_back(MenuEntry{"Continue", MenuEntry::Close, "", 0.0f});
        project_.menus.push_back(std::move(m));
        selectedMenu_ = (int)project_.menus.size() - 1;
        changed = true;
    }
    if (ImGui::Button("+ Options menu", ImVec2(-1, 0))) {
        selectedMenu_ = addOptionsMenuPages(project_);
        changed = true;
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip(
            "Scaffold a paged options menu: an OPTIONS root that opens\n"
            "AUDIO / CONTROLS / DISPLAY submenus, each pre-filled with\n"
            "ready-made setting rows (volume, deadzone, aim curve,\n"
            "display mode, aspect). Style and edit them like any menu.");
    ImGui::Separator();
    for (int i = 0; i < (int)project_.menus.size(); ++i) {
        ImGui::PushID(i);
        std::string tag;
        if (project_.menus[i].titleScreen) tag += "  [title]";
        if (project_.menus[i].pauseMenu) tag += "  [start]";
        if (ImGui::Selectable((project_.menus[i].name + tag).c_str(),
                              selectedMenu_ == i))
            selectedMenu_ = i;
        ImGui::PopID();
    }
    if (project_.menus.empty())
        ImGui::TextDisabled("No menus yet.\nEvery menu becomes a\nbaked panel sprite.");
    ImGui::EndChild();

    ImGui::SameLine();

    // --- right: selected menu editor -------------------------------------
    ImGui::BeginChild("##menu_edit", ImVec2(0, 0));
    if (selectedMenu_ < 0 || selectedMenu_ >= (int)project_.menus.size()) {
        ImGui::TextDisabled("Select a menu on the left (or create one).");
        ImGui::TextDisabled("\nOpen menus in the game with:");
        ImGui::BulletText("the Open Menu flow node (category \"Menus\")");
        ImGui::BulletText("a menu entry (\"Open menu\" action = submenus)");
        ImGui::BulletText("the Start button (\"pause menu\" checkbox)");
        ImGui::BulletText("game start (\"title screen\" checkbox)");
        ImGui::EndChild();
        ImGui::End();
        return;
    }
    GameMenu& m = project_.menus[selectedMenu_];

    char nameBuf[64];
    std::snprintf(nameBuf, sizeof(nameBuf), "%s", m.name.c_str());
    ImGui::SetNextItemWidth(scaled(180.0f));
    if (ImGui::InputText("Name", nameBuf, sizeof(nameBuf))) {
        // keep references (flow OpenMenu nodes, submenu entries) pointing here
        for (SceneData& sc : project_.scenes)
            for (SceneObject& o : sc.objects)
                for (FlowNode& fn : o.flowGraph.nodes) {
                    const FlowNodeType* ft = flowNodeType(fn.type);
                    if (ft && ft->strKind == FlowParamKind::MenuName &&
                        fn.str == m.name)
                        fn.str = nameBuf;
                }
        for (GameMenu& other : project_.menus)
            for (MenuEntry& en : other.entries)
                if (en.action == MenuEntry::OpenMenu && en.param == m.name)
                    en.param = nameBuf;
        m.name = nameBuf;
    }
    changed |= ImGui::IsItemDeactivatedAfterEdit();
    ImGui::SameLine();
    if (ImGui::SmallButton("Duplicate")) {
        GameMenu copy = m;
        copy.titleScreen = false;  // the boot/Start slots stay unique
        copy.pauseMenu = false;
        std::string base = copy.name;
        for (int n = 2;; ++n) {
            copy.name = base + "-" + std::to_string(n);
            bool taken = false;
            for (const auto& other : project_.menus) taken |= (other.name == copy.name);
            if (!taken) break;
        }
        project_.menus.push_back(std::move(copy));
        selectedMenu_ = (int)project_.menus.size() - 1;
        changed = true;
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Delete")) {
        for (SceneData& sc : project_.scenes)
            for (SceneObject& o : sc.objects)
                for (FlowNode& fn : o.flowGraph.nodes) {
                    const FlowNodeType* ft = flowNodeType(fn.type);
                    if (ft && ft->strKind == FlowParamKind::MenuName &&
                        fn.str == m.name)
                        fn.str.clear();
                }
        project_.menus.erase(project_.menus.begin() + selectedMenu_);
        selectedMenu_ = -1;
        commitChange();
        ImGui::EndChild();
        ImGui::End();
        return;
    }

    char titleBuf[64];
    std::snprintf(titleBuf, sizeof(titleBuf), "%s", m.title.c_str());
    ImGui::SetNextItemWidth(scaled(180.0f));
    if (ImGui::InputText("Title", titleBuf, sizeof(titleBuf))) m.title = titleBuf;
    changed |= ImGui::IsItemDeactivatedAfterEdit();
    ImGui::SameLine();
    ImGui::ColorEdit3("Accent", m.accent, ImGuiColorEditFlags_NoInputs);
    changed |= ImGui::IsItemDeactivatedAfterEdit();

    if (ImGui::Checkbox("Title screen (opens at game start)", &m.titleScreen)) {
        if (m.titleScreen)  // only one menu can own the boot slot
            for (int i = 0; i < (int)project_.menus.size(); ++i)
                if (i != selectedMenu_) project_.menus[i].titleScreen = false;
        changed = true;
    }
    if (ImGui::Checkbox("Open on Start button (pause menu)", &m.pauseMenu)) {
        if (m.pauseMenu)  // only one menu answers the Start button
            for (int i = 0; i < (int)project_.menus.size(); ++i)
                if (i != selectedMenu_) project_.menus[i].pauseMenu = false;
        changed = true;
    }
    if (ImGui::Checkbox("Pauses the game", &m.pauseGame)) changed = true;
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("On: gameplay freezes under a dim overlay while the\n"
                          "menu is open. Off: the menu floats over the running\n"
                          "game - note that pad presses then reach both the\n"
                          "menu and the game (X also jumps etc.).");

    // --- panel geometry: width, screen position, presets ------------------
    ImGui::SeparatorText("Layout");
    struct LayoutPreset {
        const char* name;
        int panelW;
        float x, y;
    };
    static const LayoutPreset kPresets[] = {
        {"Centered dialog", 256, 0.5f, 0.45f},
        {"Title at the bottom", 256, 0.5f, 0.72f},
        {"Corner card", 256, 0.78f, 0.74f},
        {"Wide banner", 512, 0.5f, 0.5f},
    };
    ImGui::SetNextItemWidth(scaled(180.0f));
    if (ImGui::BeginCombo("Preset", "apply a preset...")) {
        for (const LayoutPreset& pr : kPresets) {
            if (ImGui::Selectable(pr.name)) {
                m.panelW = pr.panelW;
                m.screenPos[0] = pr.x;
                m.screenPos[1] = pr.y;
                changed = true;
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Presets only set the width and screen position -\n"
                          "images and entries stay. Fine-tune below.");
    {
        int wIdx = m.panelW == 128 ? 0 : m.panelW == 512 ? 2 : 1;
        ImGui::SetNextItemWidth(scaled(100.0f));
        if (ImGui::Combo("Panel width", &wIdx, "128 px\000256 px\000512 px\000")) {
            m.panelW = wIdx == 0 ? 128 : wIdx == 2 ? 512 : 256;
            changed = true;
        }
    }
    ImGui::SetNextItemWidth(scaled(180.0f));
    ImGui::DragFloat2("Screen position", m.screenPos, 0.005f, 0.0f, 1.0f, "%.3f");
    changed |= ImGui::IsItemDeactivatedAfterEdit();
    if (ImGui::Checkbox("Show title", &m.showTitle)) changed = true;

    // Font: default chain / fonts imported into the project / a curated set
    // of stock Windows fonts (existence-checked) / import a new TTF.
    changed |= fontCombo(m.fontPath);
    {
        int sizes[2] = {m.titleSize, m.entrySize};
        ImGui::SetNextItemWidth(scaled(140.0f));
        if (ImGui::DragInt2("Title / entry size", sizes, 0.2f, 8, 48)) {
            m.titleSize = sizes[0] < 10 ? 10 : sizes[0] > 48 ? 48 : sizes[0];
            m.entrySize = sizes[1] < 8 ? 8 : sizes[1] > 32 ? 32 : sizes[1];
        }
        changed |= ImGui::IsItemDeactivatedAfterEdit();
    }

    if (menuPreviewClipped_)
        ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.4f, 1.0f),
                           "Panel taller than 512 px - the bottom gets clipped."
                           " Shrink or remove images.");

    // --- images composited into the panel ---------------------------------
    ImGui::SeparatorText("Images");
    static const char* kSlotNames[] = {"Above title", "Above entries",
                                       "Below entries", "Background", "Overlay"};
    for (int i = 0; i < (int)m.images.size(); ++i) {
        MenuImage& img = m.images[i];
        ImGui::PushID(i);
        const bool canUp = i > 0;
        const bool canDown = i + 1 < (int)m.images.size();
        ImGui::BeginDisabled(!canUp);
        if (ImGui::ArrowButton("##imgup", ImGuiDir_Up)) {
            std::swap(m.images[i], m.images[i - 1]);
            changed = true;
        }
        ImGui::EndDisabled();
        ImGui::SameLine(0.0f, 2.0f);
        ImGui::BeginDisabled(!canDown);
        if (ImGui::ArrowButton("##imgdown", ImGuiDir_Down)) {
            std::swap(m.images[i], m.images[i + 1]);
            changed = true;
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        const std::string fileName =
            std::filesystem::path(img.path).filename().string();
        ImGui::TextUnformatted(fileName.c_str());
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", img.path.c_str());
        ImGui::SameLine(scaled(190.0f));
        ImGui::SetNextItemWidth(scaled(120.0f));
        if (ImGui::Combo("##slot", &img.slot,
                         "Above title\0Above entries\0Below entries\0"
                         "Background\0Overlay\0"))
            changed = true;
        ImGui::SameLine();
        if (ImGui::SmallButton("x##imgdel")) {
            m.images.erase(m.images.begin() + i);
            changed = true;
            ImGui::PopID();
            break;
        }
        // second row: size + position nudge (Background stretches, no knobs)
        if (img.slot != MenuImage::Background) {
            ImGui::Indent(scaled(46.0f));
            ImGui::SetNextItemWidth(scaled(90.0f));
            ImGui::DragFloat("scale##img", &img.scale, 0.02f, 0.05f, 4.0f, "%.2fx");
            changed |= ImGui::IsItemDeactivatedAfterEdit();
            ImGui::SameLine();
            ImGui::SetNextItemWidth(scaled(120.0f));
            ImGui::DragFloat2("offset##img", img.offset, 1.0f, -512.0f, 512.0f,
                              "%.0f px");
            changed |= ImGui::IsItemDeactivatedAfterEdit();
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(img.slot == MenuImage::Overlay
                                      ? "Top-left position inside the panel."
                                      : "Nudge from the centered flow position.");
            ImGui::Unindent(scaled(46.0f));
        }
        ImGui::PopID();
    }
    if (ImGui::SmallButton("+ Image (PNG)...")) {
        const std::string src = pickPngFile();
        if (!src.empty()) {
            const std::filesystem::path srcPath(src);
            const std::string fileName = sanitizeAssetName(srcPath.filename().string());
            const std::filesystem::path destDir =
                std::filesystem::path(project_.dir) / "res" / "hud";
            std::error_code ec;
            std::filesystem::create_directories(destDir, ec);
            std::filesystem::copy_file(srcPath, destDir / fileName,
                                       std::filesystem::copy_options::overwrite_existing,
                                       ec);
            if (!ec) {
                MenuImage img;
                img.path = "res/hud/" + fileName;
                m.images.push_back(std::move(img));
                changed = true;
            }
        }
    }
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Baked into the panel sprite - free at runtime.\n"
                          "Flow slots stack in list order and push the text\n"
                          "down; Background stretches under everything;\n"
                          "Overlay draws over the text at its offset.");

    ImGui::SeparatorText("Entries (dpad rows)");
    static const char* kActionNames[] = {
        "Close menu",     "Switch scene",      "Open save menu", "Open menu",
        "Set save value", "Add to save value", "Flow event",     "Toggle",
        "Choice"};
    for (int e = 0; e < (int)m.entries.size(); ++e) {
        MenuEntry& en = m.entries[e];
        ImGui::PushID(e);

        // reorder arrows (rows = dpad order in the game)
        const bool canUp = e > 0;
        const bool canDown = e + 1 < (int)m.entries.size();
        ImGui::BeginDisabled(!canUp);
        if (ImGui::ArrowButton("##up", ImGuiDir_Up)) {
            std::swap(m.entries[e], m.entries[e - 1]);
            changed = true;
        }
        ImGui::EndDisabled();
        ImGui::SameLine(0.0f, 2.0f);
        ImGui::BeginDisabled(!canDown);
        if (ImGui::ArrowButton("##down", ImGuiDir_Down)) {
            std::swap(m.entries[e], m.entries[e + 1]);
            changed = true;
        }
        ImGui::EndDisabled();
        ImGui::SameLine();

        char labelBuf[64];
        std::snprintf(labelBuf, sizeof(labelBuf), "%s", en.label.c_str());
        ImGui::SetNextItemWidth(scaled(140.0f));
        if (ImGui::InputText("##label", labelBuf, sizeof(labelBuf))) en.label = labelBuf;
        changed |= ImGui::IsItemDeactivatedAfterEdit();
        ImGui::SameLine();
        ImGui::SetNextItemWidth(scaled(150.0f));
        if (ImGui::Combo("##action", &en.action, kActionNames, 9)) {
            en.param.clear();
            // Stateful rows start with a sensible option set; everything
            // else drops the list so it does not linger in the file.
            if (en.action == MenuEntry::Toggle)
                en.options = {"Off", "On"};
            else if (en.action == MenuEntry::Choice)
                en.options = {"Low", "Medium", "High"};
            else
                en.options.clear();
            changed = true;
        }
        ImGui::SameLine();

        // Action target inline (scene / menu / value / event)
        auto paramCombo = [&](const char* comboId, const char* hint, auto&& items,
                              auto&& nameOf) {
            ImGui::SetNextItemWidth(scaled(120.0f));
            if (ImGui::BeginCombo(comboId,
                                  en.param.empty() ? hint : en.param.c_str())) {
                for (const auto& item : items) {
                    const std::string& n = nameOf(item);
                    if (ImGui::Selectable(n.c_str(), n == en.param)) {
                        en.param = n;
                        changed = true;
                    }
                }
                ImGui::EndCombo();
            }
            ImGui::SameLine();
        };
        if (en.action == MenuEntry::SwitchScene) {
            paramCombo("##scene", "<scene>", project_.scenes,
                       [](const SceneData& s) -> const std::string& { return s.name; });
        } else if (en.action == MenuEntry::OpenMenu) {
            paramCombo("##menu", "<menu>", project_.menus,
                       [](const GameMenu& gm) -> const std::string& { return gm.name; });
        } else if (en.action == MenuEntry::SetValue ||
                   en.action == MenuEntry::AddValue) {
            paramCombo("##value", "<value>", project_.saveValues,
                       [](const SaveValue& v) -> const std::string& { return v.name; });
            ImGui::SetNextItemWidth(scaled(70.0f));
            ImGui::DragFloat("##amount", &en.amount, 0.1f, 0.0f, 0.0f, "%.2f");
            changed |= ImGui::IsItemDeactivatedAfterEdit();
            ImGui::SameLine();
        } else if (en.action == MenuEntry::FlowEvent) {
            char eventBuf[64];
            std::snprintf(eventBuf, sizeof(eventBuf), "%s", en.param.c_str());
            ImGui::SetNextItemWidth(scaled(120.0f));
            if (ImGui::InputText("##event", eventBuf, sizeof(eventBuf)))
                en.param = eventBuf;
            changed |= ImGui::IsItemDeactivatedAfterEdit();
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Event name for the On Menu Event flow trigger.");
            ImGui::SameLine();
        } else if (en.action == MenuEntry::Toggle ||
                   en.action == MenuEntry::Choice) {
            paramCombo("##togglevalue", "<value>", project_.saveValues,
                       [](const SaveValue& v) -> const std::string& { return v.name; });
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(
                    "Save value holding the state (the option index).\n"
                    "Its default is the initial state; flow graphs react\n"
                    "via Value At Least -> On Condition.");
        }

        if (ImGui::SmallButton("x##delete")) {
            m.entries.erase(m.entries.begin() + e);
            changed = true;
            ImGui::PopID();
            break;
        }

        // Option labels on their own row (Toggle: off/on pair; Choice: a
        // reorderable-by-editing list). Cross / dpad cycle these in-game.
        if (en.action == MenuEntry::Toggle || en.action == MenuEntry::Choice) {
            ImGui::Indent(scaled(46.0f));
            if (en.action == MenuEntry::Toggle) {
                if (en.options.size() < 2) en.options = {"Off", "On"};
                char offBuf[32], onBuf[32];
                std::snprintf(offBuf, sizeof(offBuf), "%s", en.options[0].c_str());
                std::snprintf(onBuf, sizeof(onBuf), "%s", en.options[1].c_str());
                ImGui::SetNextItemWidth(scaled(90.0f));
                if (ImGui::InputText("Off label", offBuf, sizeof(offBuf)))
                    en.options[0] = offBuf;
                changed |= ImGui::IsItemDeactivatedAfterEdit();
                ImGui::SameLine();
                ImGui::SetNextItemWidth(scaled(90.0f));
                if (ImGui::InputText("On label", onBuf, sizeof(onBuf)))
                    en.options[1] = onBuf;
                changed |= ImGui::IsItemDeactivatedAfterEdit();
            } else {
                for (int o = 0; o < (int)en.options.size(); ++o) {
                    ImGui::PushID(o);
                    char optBuf[32];
                    std::snprintf(optBuf, sizeof(optBuf), "%s",
                                  en.options[o].c_str());
                    ImGui::SetNextItemWidth(scaled(110.0f));
                    if (ImGui::InputText("##opt", optBuf, sizeof(optBuf)))
                        en.options[o] = optBuf;
                    changed |= ImGui::IsItemDeactivatedAfterEdit();
                    ImGui::SameLine();
                    ImGui::BeginDisabled((int)en.options.size() <= 1);
                    if (ImGui::SmallButton("x##optdel")) {
                        en.options.erase(en.options.begin() + o);
                        changed = true;
                        ImGui::EndDisabled();
                        ImGui::PopID();
                        break;
                    }
                    ImGui::EndDisabled();
                    if (o + 1 < (int)en.options.size() ||
                        (int)en.options.size() < menubake::kMaxOptions)
                        ImGui::SameLine();
                    ImGui::PopID();
                }
                if ((int)en.options.size() < menubake::kMaxOptions) {
                    if (ImGui::SmallButton("+##optadd")) {
                        en.options.push_back("Option");
                        changed = true;
                    }
                }
            }
            // Setting binding: makes this stateful row drive a built-in engine
            // setting at runtime (no flow graph). The option index maps evenly
            // onto the setting - see the Insert-option-block presets.
            ImGui::SetNextItemWidth(scaled(150.0f));
            if (ImGui::Combo("Bind##optbind", &en.settingBind,
                             "None\0Music volume\0Sound volume\0Deadzone\0"
                             "Stick curve\0Display mode\0Widescreen\0"))
                changed = true;
            ImGui::SameLine();
            ImGui::TextDisabled("(?)");
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(
                    "Drives a built-in setting from this row's option index,\n"
                    "spread evenly across the options: volume 0-100%%, deadzone\n"
                    "0-0.4, aim curve 1-3, display 480i/480p/1080i, aspect\n"
                    "4:3/16:9. None = a plain save-value row (flow graphs react).");
            ImGui::Unindent(scaled(46.0f));
        }
        ImGui::PopID();
    }
    if ((int)m.entries.size() < menubake::kMaxEntries) {
        if (ImGui::SmallButton("+ Entry")) {
            m.entries.push_back(MenuEntry{});
            changed = true;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("+ Option block")) ImGui::OpenPopup("##optblock");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(
                "Insert a ready-made setting row (backed by a save value):\n"
                "volume, controller deadzone / aim curve, display mode,\n"
                "aspect ratio. Restyle and relabel it like any other entry.");
        if (ImGui::BeginPopup("##optblock")) {
            static const char* kBlockMenu[] = {
                "Music volume", "Sound volume", "Controller deadzone",
                "Aim response curve", "Display mode", "Widescreen (aspect)"};
            for (int b = 0; b < kOptionBlockCount; ++b)
                if (ImGui::Selectable(kBlockMenu[b])) {
                    addOptionBlock(project_, m, b);
                    changed = true;
                }
            ImGui::EndPopup();
        }
    } else {
        ImGui::TextDisabled("Max %d entries per menu.", menubake::kMaxEntries);
    }

    // Live preview: the exact panel the build will bake, either 1:1 or
    // composited onto a mock TV screen (the 512x448 buffer stretched to the
    // PAL / NTSC display aspect, with the pause dim when it applies).
    ImGui::SeparatorText("Preview");
    ImGui::SetNextItemWidth(scaled(140.0f));
    ImGui::Combo("##previewmode", &menuPreviewMode_,
                 "Panel (1:1)\0TV PAL\0TV NTSC\0");
    {
        std::string key = m.name + "\x1f" + m.title + "\x1f" +
                          std::to_string(m.panelW) + "\x1f" +
                          std::to_string(m.showTitle) + "\x1f" + m.fontPath +
                          "\x1f" + std::to_string(m.titleSize) + "|" +
                          std::to_string(m.entrySize) + "\x1f" +
                          std::to_string(m.accent[0]) + "," +
                          std::to_string(m.accent[1]) + "," +
                          std::to_string(m.accent[2]);
        for (const MenuImage& img : m.images)
            key += "\x1f" + img.path + "|" + std::to_string(img.slot) + "|" +
                   std::to_string(img.scale) + "|" + std::to_string(img.offset[0]) +
                   "," + std::to_string(img.offset[1]);
        for (const MenuEntry& en : m.entries) {
            key += "\x1f" + en.label + "|" + std::to_string(en.action) + "|" +
                   en.param;
            for (const std::string& opt : en.options) key += "," + opt;
        }
        // Toggle/Choice initial states (save value defaults) show in the
        // preview - refresh when they change too.
        for (const SaveValue& sv : project_.saveValues)
            key += "\x1f" + sv.name + "=" + std::to_string((int)sv.value);
        if (key != menuPreviewKey_) {
            std::vector<unsigned char> rgba;
            int w = 0, h = 0;
            if (menubake::bakePanelRGBA(m, project_.dir, rgba, w, h)) {
                // Composite each Toggle/Choice row's initial option label
                // where the game draws the value strip cell.
                std::vector<int> current(m.entries.size(), 0);
                for (size_t e = 0; e < m.entries.size(); ++e)
                    for (const SaveValue& sv : project_.saveValues)
                        if (sv.name == m.entries[e].param)
                            current[e] = (int)sv.value;
                menubake::overlayValuePreview(m, project_.dir, current, rgba, w, h);
                if (!menuPreviewTex_) glGenTextures(1, &menuPreviewTex_);
                glBindTexture(GL_TEXTURE_2D, menuPreviewTex_);
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA,
                             GL_UNSIGNED_BYTE, rgba.data());
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                menuPreviewW_ = w;
                menuPreviewH_ = h;
                const menubake::PanelLayout lay =
                    menubake::panelLayout(m, project_.dir);
                menuPreviewContentH_ = lay.contentH;
                menuPreviewClipped_ = lay.clipped;
            }
            menuPreviewKey_ = key;
        }
        if (menuPreviewTex_ && menuPreviewMode_ == 0) {
            // Baked at native PS2 pixels; upscale the on-screen copy so it
            // isn't a postage stamp next to the DPI-scaled controls.
            ImGui::Image((ImTextureID)(intptr_t)menuPreviewTex_,
                         ImVec2(scaled((float)menuPreviewW_),
                                scaled((float)menuPreviewContentH_)),
                         ImVec2(0, 0),
                         ImVec2(1.0f, (float)menuPreviewContentH_ /
                                          (float)menuPreviewH_));
        } else if (menuPreviewTex_) {
            // What a TV shows of the 512x448 buffer: PAL fills 4:3 exactly,
            // NTSC has fewer active lines so the picture is a touch wider
            // (same approximations as the viewport TV frames).
            const float aspect = menuPreviewMode_ == 1
                                     ? 4.0f / 3.0f
                                     : 480.0f / 448.0f * 4.0f / 3.0f;
            float sw = ImGui::GetContentRegionAvail().x - scaled(8.0f);
            if (sw > scaled(460.0f)) sw = scaled(460.0f);
            if (sw < scaled(200.0f)) sw = scaled(200.0f);
            const float sh = sw / aspect;
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const ImVec2 p0 = ImGui::GetCursorScreenPos();
            const ImVec2 p1(p0.x + sw, p0.y + sh);
            // mock scene behind the menu: project sky over terrain green
            const float* sc = project_.settings.skyColor;
            dl->AddRectFilledMultiColor(
                p0, ImVec2(p1.x, p0.y + sh * 0.55f),
                IM_COL32((int)(project_.settings.skyTopColor[0] * 255),
                         (int)(project_.settings.skyTopColor[1] * 255),
                         (int)(project_.settings.skyTopColor[2] * 255), 255),
                IM_COL32((int)(project_.settings.skyTopColor[0] * 255),
                         (int)(project_.settings.skyTopColor[1] * 255),
                         (int)(project_.settings.skyTopColor[2] * 255), 255),
                IM_COL32((int)(sc[0] * 255), (int)(sc[1] * 255),
                         (int)(sc[2] * 255), 255),
                IM_COL32((int)(sc[0] * 255), (int)(sc[1] * 255),
                         (int)(sc[2] * 255), 255));
            dl->AddRectFilled(ImVec2(p0.x, p0.y + sh * 0.55f), p1,
                              IM_COL32(96, 150, 72, 255));
            if (m.pauseGame)  // the in-game dim overlay under pausing menus
                dl->AddRectFilled(p0, p1, IM_COL32(0, 0, 0, 115));
            // panel mapped from buffer coordinates (same math as buildScene)
            const float sx = sw / 512.0f, sy = sh / 448.0f;
            const float panelX = m.screenPos[0] * 512.0f - menuPreviewW_ * 0.5f;
            const float panelY =
                m.screenPos[1] * 448.0f - menuPreviewContentH_ * 0.5f;
            const ImVec2 m0(p0.x + panelX * sx, p0.y + panelY * sy);
            const ImVec2 m1(m0.x + menuPreviewW_ * sx,
                            m0.y + menuPreviewContentH_ * sy);
            dl->AddImage((ImTextureID)(intptr_t)menuPreviewTex_, m0, m1,
                         ImVec2(0, 0),
                         ImVec2(1.0f, (float)menuPreviewContentH_ /
                                          (float)menuPreviewH_));
            dl->AddRect(p0, p1, IM_COL32(255, 255, 255, 120));
            dl->AddText(ImVec2(p0.x + 4, p1.y - 18), IM_COL32(255, 255, 255, 160),
                        menuPreviewMode_ == 1 ? "PAL 4:3" : "NTSC");
            ImGui::Dummy(ImVec2(sw, sh + 4.0f));
        }
    }

    ImGui::EndChild();
    ImGui::End();

    // commitChange: renames/deletes touch flow graphs (part of undo snapshots)
    if (changed) commitChange();
}

void App::drawScriptsSection() {
    if (!ImGui::CollapsingHeader("Scripts")) return;

    if (ImGui::SmallButton("New script...")) {
        openNewScriptPopup_ = true;
        newScriptError_.clear();
        newScriptAttachTo_ = -1;  // created loose - attach later in Properties
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Open in VS Code")) openInVSCode();

    // List src/scripts/*.cpp (user scripts live there and are compiled
    // automatically by the project Makefile). Click one to open it in VS Code
    // in the project context.
    const std::filesystem::path dir = std::filesystem::path(project_.dir) / "src" / "scripts";
    std::error_code ec;
    bool any = false;
    if (std::filesystem::exists(dir, ec)) {
        for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
            if (entry.path().extension() != ".cpp") continue;
            const std::string fname = entry.path().filename().string();
            ImGui::Bullet();
            ImGui::SameLine();
            if (ImGui::Selectable(fname.c_str())) openInVSCode("src\\scripts\\" + fname);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Open in VS Code");
            any = true;
        }
    }
    if (!any) {
        ImGui::TextDisabled("No scripts yet.");
    } else {
        ImGui::TextDisabled("Object scripts (TYRA_OBJECT_SCRIPT) run when attached\n"
                            "to objects: Properties > Scripts. Plain TYRA_SCRIPT\n"
                            "classes run globally every frame.");
    }
}

void App::drawNewScriptModal() {
    if (openNewScriptPopup_) {
        ImGui::OpenPopup("New Script");
        openNewScriptPopup_ = false;
    }

    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

    if (!ImGui::BeginPopupModal("New Script", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        return;

    ImGui::InputText("File name", newScriptName_, sizeof(newScriptName_));
    ImGui::TextDisabled("Creates src\\scripts\\%s.cpp", newScriptName_);
    if (newScriptAttachTo_ >= 0 && newScriptAttachTo_ < (int)project_.objects().size())
        ImGui::TextDisabled("Attaches it to \"%s\".",
                            project_.objects()[newScriptAttachTo_].name.c_str());

    if (!newScriptError_.empty())
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", newScriptError_.c_str());

    ImGui::Separator();
    if (ImGui::Button("Create", ImVec2(scaled(120), 0))) {
        std::string name = newScriptName_;
        bool valid = !name.empty();
        for (char c : name)
            if (!isalnum((unsigned char)c) && c != '_' && c != '-') valid = false;

        if (!valid) {
            newScriptError_ = "Name may contain only letters, digits, '_' and '-'";
        } else {
            // File name -> C++ class name (my-script -> My_script)
            std::string className;
            for (char c : name) className += (isalnum((unsigned char)c) ? c : '_');
            if (isdigit((unsigned char)className[0])) className = "Script" + className;
            className[0] = (char)toupper((unsigned char)className[0]);

            const std::filesystem::path path =
                std::filesystem::path(project_.dir) / "src" / "scripts" / (name + ".cpp");
            std::error_code ec;
            if (std::filesystem::exists(path, ec)) {
                newScriptError_ = "Script already exists: " + name + ".cpp";
            } else {
                std::filesystem::create_directories(path.parent_path(), ec);
                std::ofstream f(path, std::ios::binary);
                if (f) {
                    f << templates::scriptStub(project_, className, name + ".cpp");
                    f.close();
                    // Invoked from Properties > Scripts: attach the new class
                    // to the object right away.
                    if (newScriptAttachTo_ >= 0 &&
                        newScriptAttachTo_ < (int)project_.objects().size()) {
                        project_.objects()[newScriptAttachTo_].scripts.push_back(
                            className);
                        commitChange();
                        statusMessage_ = "Created " + name + ".cpp and attached " +
                                         className;
                    } else {
                        statusMessage_ = "Created " + name + ".cpp";
                    }
                    newScriptAttachTo_ = -1;
                    ImGui::CloseCurrentPopup();
                } else {
                    newScriptError_ = "Cannot write " + path.string();
                }
            }
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(scaled(120), 0))) {
        newScriptAttachTo_ = -1;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void App::drawDeleteSceneModal() {
    if (deleteScenePending_ >= 0 && !ImGui::IsPopupOpen("Delete Scene?"))
        ImGui::OpenPopup("Delete Scene?");
    if (deleteScenePending_ < 0 || deleteScenePending_ >= (int)project_.scenes.size())
        return;

    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (!ImGui::BeginPopupModal("Delete Scene?", nullptr,
                                ImGuiWindowFlags_AlwaysAutoResize))
        return;

    const SceneData& sc = project_.scenes[deleteScenePending_];
    ImGui::Text("Delete scene \"%s\"?", sc.name.c_str());
    ImGui::TextDisabled("%d object(s), their flow graphs and the sculpted terrain\n"
                        "go with it. Undo (Ctrl+Z) can bring the scene back,\n"
                        "but not its heightmap.",
                        (int)sc.objects.size());
    if (deleteScenePending_ == 0)
        ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.3f, 1.0f),
                           "This is the start scene - the next one takes its place.");

    ImGui::Separator();
    if (ImGui::Button("Delete", ImVec2(scaled(120), 0))) {
        project_.scenes.erase(project_.scenes.begin() + deleteScenePending_);
        if (project_.activeScene >= (int)project_.scenes.size() ||
            project_.activeScene == deleteScenePending_)
            project_.activeScene = 0;
        clearSelection();
        flowGraphObject_ = -1;
        flowPositionsApplied_ = false;
        deleteScenePending_ = -1;
        commitChange();
        applyProjectToViewport();
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(scaled(120), 0))) {
        deleteScenePending_ = -1;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

void App::requestAssetDelete(PendingAssetDelete::Kind kind, const std::string& relPath,
                             const std::string& label, int hudIndex) {
    assetDeletePending_ = PendingAssetDelete{kind, relPath, label, hudIndex};
    assetDeleteActive_ = true;
}

// Scene objects (across every scene) and flow-graph audio nodes still pointing
// at the staged asset. Drives the confirmation dialog's warning and the
// reference cleanup on confirm.
void App::countAssetUsers(const PendingAssetDelete& d, int& objectUsers,
                          int& nodeUsers) const {
    objectUsers = 0;
    nodeUsers = 0;
    for (const SceneData& scene : project_.scenes) {
        for (const SceneObject& o : scene.objects) {
            switch (d.kind) {
                case PendingAssetDelete::Model:
                    if (o.type == PrimitiveType::Model && o.modelPath == d.relPath)
                        ++objectUsers;
                    break;
                case PendingAssetDelete::Material:
                    if (o.materialPath == d.relPath) ++objectUsers;
                    break;
                case PendingAssetDelete::Sound:
                    if (o.type == PrimitiveType::SoundEmitter &&
                        o.soundPath == d.relPath)
                        ++objectUsers;
                    break;
                default:
                    break;
            }
            if (d.kind == PendingAssetDelete::Music ||
                d.kind == PendingAssetDelete::Sound) {
                for (const FlowNode& n : o.flowGraph.nodes) {
                    const FlowNodeType* t = flowNodeType(n.type);
                    if (!t || n.str != d.relPath) continue;
                    if ((d.kind == PendingAssetDelete::Music &&
                         t->strKind == FlowParamKind::MusicTrack) ||
                        (d.kind == PendingAssetDelete::Sound &&
                         t->strKind == FlowParamKind::SoundTrack))
                        ++nodeUsers;
                }
            }
        }
    }
}

void App::drawDeleteAssetModal() {
    if (assetDeleteActive_ && !ImGui::IsPopupOpen("Delete Asset?"))
        ImGui::OpenPopup("Delete Asset?");
    if (!assetDeleteActive_) return;

    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (!ImGui::BeginPopupModal("Delete Asset?", nullptr,
                                ImGuiWindowFlags_AlwaysAutoResize))
        return;

    const PendingAssetDelete& d = assetDeletePending_;
    const char* kindName = d.kind == PendingAssetDelete::Model      ? "model"
                           : d.kind == PendingAssetDelete::Material ? "material"
                           : d.kind == PendingAssetDelete::Music    ? "music track"
                           : d.kind == PendingAssetDelete::Sound    ? "sound"
                                                                    : "HUD image";
    ImGui::Text("Delete %s \"%s\"?", kindName, d.label.c_str());

    int objUsers = 0, nodeUsers = 0;
    countAssetUsers(d, objUsers, nodeUsers);
    const ImVec4 warn(1.0f, 0.75f, 0.3f, 1.0f);
    switch (d.kind) {
        case PendingAssetDelete::Model:
            if (objUsers > 0)
                ImGui::TextColored(warn,
                    "Used by %d object(s) - they will show as missing until you\n"
                    "repoint or delete them.", objUsers);
            break;
        case PendingAssetDelete::Material:
            if (objUsers > 0)
                ImGui::TextColored(warn,
                    "Assigned to %d object(s) - they revert to plain color /\n"
                    "the model's own materials.", objUsers);
            break;
        case PendingAssetDelete::Music:
            if (nodeUsers > 0)
                ImGui::TextColored(warn,
                    "Referenced by %d Music flow node(s) - they will be cleared.",
                    nodeUsers);
            break;
        case PendingAssetDelete::Sound:
            if (objUsers > 0 || nodeUsers > 0)
                ImGui::TextColored(warn,
                    "Used by %d sound emitter(s) and %d flow node(s) -\n"
                    "the references will be cleared.", objUsers, nodeUsers);
            break;
        case PendingAssetDelete::Hud:
            break;
    }
    ImGui::TextDisabled("The file is removed from res/. This cannot be undone.");

    ImGui::Separator();
    if (ImGui::Button("Delete", ImVec2(scaled(120), 0))) {
        performAssetDelete(d);
        assetDeleteActive_ = false;
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(scaled(120), 0))) {
        assetDeleteActive_ = false;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

// Deletes the staged asset file and clears the project references the dialog
// warned about. Model/material live on the filesystem (the Assets lists rescan
// them); music/sound/hud also carry a project-list entry to drop.
void App::performAssetDelete(const PendingAssetDelete& d) {
    std::error_code ec;
    const std::filesystem::path full = std::filesystem::path(project_.dir) / d.relPath;

    switch (d.kind) {
        case PendingAssetDelete::Model:
            std::filesystem::remove(full, ec);
            project_.textureQuality.erase(d.relPath);
            modelInfoCache_.clear();
            glbInfoCache_.clear();
            statusMessage_ = "Deleted " + d.label;
            commitChange();
            break;

        case PendingAssetDelete::Material:
            std::filesystem::remove(full, ec);
            project_.textureQuality.erase(d.relPath);
            // Objects keep working: an empty material is plain color for a
            // primitive and the model's own .mtl for a model.
            for (SceneData& scene : project_.scenes)
                for (SceneObject& o : scene.objects)
                    if (o.materialPath == d.relPath) o.materialPath.clear();
            // A terrain that used it falls back to the checker greens.
            if (project_.settings.terrainMaterial == d.relPath)
                project_.settings.terrainMaterial.clear();
            for (SceneData& scene : project_.scenes)
                if (scene.settings.terrainMaterial == d.relPath)
                    scene.settings.terrainMaterial.clear();
            modelInfoCache_.clear();
            // The Material Editor may have this very file open (its Delete...
            // button routes here) - drop the staged copy and paint session.
            if (matEdPath_ == d.relPath) {
                matEdPath_.clear();
                matEdMats_.clear();
                matEdUndo_.clear();
                matEdPaint_ = false;
                matEdStroke_ = false;
                matEdPaintTexRel_.clear();
                matEdLayers_.clear();
            }
            viewport_.invalidateAssets();  // cached draws may reference the file
            statusMessage_ = "Deleted " + d.label;
            commitChange();
            break;

        case PendingAssetDelete::Music:
            // removeAudioTrack clears Play/Stop Music nodes and deletes the file.
            removeAudioTrack(project_, d.relPath, true);
            project_.musicBuild.erase(d.relPath);
            for (size_t i = 0; i < project_.music.size(); ++i)
                if (project_.music[i] == d.relPath) {
                    project_.music.erase(project_.music.begin() + i);
                    break;
                }
            wavIssueCache_.clear();
            statusMessage_ = "Deleted " + d.label;
            commitChange();
            break;

        case PendingAssetDelete::Sound:
            // Clears Play Sound nodes and deletes the file; sound emitters point
            // at it through soundPath, which removeAudioTrack does not touch.
            removeAudioTrack(project_, d.relPath, false);
            for (SceneData& scene : project_.scenes)
                for (SceneObject& o : scene.objects)
                    if (o.type == PrimitiveType::SoundEmitter &&
                        o.soundPath == d.relPath)
                        o.soundPath.clear();
            for (size_t i = 0; i < project_.sounds.size(); ++i)
                if (project_.sounds[i] == d.relPath) {
                    project_.sounds.erase(project_.sounds.begin() + i);
                    break;
                }
            wavIssueCache_.clear();
            statusMessage_ = "Deleted " + d.label;
            commitChange();
            break;

        case PendingAssetDelete::Hud: {
            // Drop the list entry first; delete the file only if no other HUD
            // entry still references it (imports can duplicate a path).
            if (d.hudIndex >= 0 && d.hudIndex < (int)project_.hud.size()) {
                project_.hud.erase(project_.hud.begin() + d.hudIndex);
                // Keep the effect layers where they were in the stack: entries
                // above the erased one shift down by one.
                auto fixLayer = [&](int& L) {
                    if (L > d.hudIndex) --L;
                    if (L >= (int)project_.hud.size()) L = -1;
                };
                fixLayer(project_.hudBloomLayer);
                fixLayer(project_.hudGrainLayer);
                for (ScreenFxPlacement& f : project_.screenFx) fixLayer(f.layer);
            }
            selectedHud_ = -1;
            bool stillUsed = false;
            for (const HudImage& h : project_.hud)
                stillUsed |= (h.imagePath == d.relPath);
            if (!stillUsed) {
                std::filesystem::remove(full, ec);
                hudTexCache_.erase(d.relPath);
            }
            statusMessage_ = "Deleted " + d.label;
            saveAll("Saved");  // HUD edits are not on the undo stack
            break;
        }
    }
}

void App::drawNewSceneModal() {
    if (openNewScenePopup_) {
        ImGui::OpenPopup("New Scene");
        openNewScenePopup_ = false;
    }

    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (!ImGui::BeginPopupModal("New Scene", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        return;

    ImGui::InputText("Name", newSceneName_, sizeof(newSceneName_));
    ImGui::DragInt("Terrain width", &newSceneWidth_, 1.0f, 8, 4096, "%d units");
    ImGui::DragInt("Terrain depth", &newSceneDepth_, 1.0f, 8, 4096, "%d units");
    if (!newSceneError_.empty())
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", newSceneError_.c_str());

    ImGui::Separator();
    if (ImGui::Button("Create", ImVec2(scaled(120), 0))) {
        std::string name = newSceneName_;
        bool valid = !name.empty();
        for (char c : name)
            if (!isalnum((unsigned char)c) && c != '_' && c != '-') valid = false;
        for (const SceneData& s : project_.scenes)
            if (s.name == name) valid = false;

        if (!valid) {
            newSceneError_ = "Name must be unique, letters/digits/'-'/'_' only";
        } else {
            SceneData sc;
            sc.name = name;
            sc.terrain.width = newSceneWidth_;
            sc.terrain.depth = newSceneDepth_;
            project_.scenes.push_back(std::move(sc));
            project_.activeScene = (int)project_.scenes.size() - 1;
            clearSelection();
            flowGraphObject_ = -1;
            flowPositionsApplied_ = false;
            applyProjectToViewport();  // builds the flat heightmap + mesh
            commitChange();
            statusMessage_ = "Created scene " + name;
            ImGui::CloseCurrentPopup();
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(scaled(120), 0))) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

void App::drawOutputWindow() {
    ImGui::Begin("Output");
    const std::string log = runner_.log();

    if (ImGui::SmallButton("Clear")) runner_.clearLog();
    ImGui::SameLine();
    if (ImGui::SmallButton("Copy all")) ImGui::SetClipboardText(log.c_str());
    ImGui::Separator();

    // Own scrolling child so we can drive the scroll directly. The nested
    // InputTextMultiline keeps the text mouse-selectable; sizing it to its own
    // content means this child (not the input) owns the scrollbars, so
    // GetScrollY/SetScrollHereY below actually refer to what we see.
    ImGui::BeginChild("##logscroll", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None,
                      ImGuiWindowFlags_HorizontalScrollbar);

    const ImVec2 pad = ImGui::GetStyle().FramePadding;
    const ImVec2 textSize = ImGui::CalcTextSize(log.c_str(), nullptr, false);
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const ImVec2 inputSize(ImMax(textSize.x + pad.x * 2.0f, avail.x),
                           ImMax(textSize.y + pad.y * 2.0f, avail.y));
    ImGui::InputTextMultiline("##log", const_cast<char*>(log.c_str()), log.size() + 1,
                              inputSize, ImGuiInputTextFlags_ReadOnly);

    // Stick to the bottom while new lines arrive, but only when the user is
    // already at the bottom (scrolling up to read or select holds position).
    // GetScrollMaxY() lags one frame behind the content just appended, so when
    // we were pinned last frame Scroll.y still equals it here and the test
    // passes; once the user scrolls up it no longer does and we let go.
    static size_t lastLogSize = 0;
    if (log.size() != lastLogSize && ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 1.0f)
        ImGui::SetScrollHereY(1.0f);
    lastLogSize = log.size();

    ImGui::EndChild();
    ImGui::End();
}

// Reads at most maxBytes from the end of a text file (emulog.txt can grow
// across long sessions). Returns "" when the file is absent or unreadable.
static std::string readTextFileTail(const std::string& path, size_t maxBytes) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) return "";
    const std::streamoff size = in.tellg();
    if (size <= 0) return "";
    const size_t want = (size_t)size < maxBytes ? (size_t)size : maxBytes;
    in.seekg(size - (std::streamoff)want, std::ios::beg);
    std::string data(want, '\0');
    in.read(data.data(), (std::streamsize)want);
    data.resize((size_t)in.gcount());
    // Drop a partial first line when we started mid-file.
    if ((size_t)size > maxBytes) {
        const size_t nl = data.find('\n');
        if (nl != std::string::npos) data.erase(0, nl + 1);
    }
    return data;
}

// ---------------------------------------------------------------------------
// Debug window: tails a log file so game output is visible without leaving the
// editor. Two sources: the game's own log (bin/log.txt - TYRA_LOG output and
// assertion dumps, written to the host fs by the running ELF because generated
// games set Tyra::Info::writeLogsToFile) and the emulator's console log (PCSX2
// emulog.txt - boot progress, BIOS/ELF-load errors). The game log is the
// primary channel; EE printf does not reliably reach emulog (see tyra-testing).
// ---------------------------------------------------------------------------
void App::drawDebugWindow() {
    ImGui::Begin("Debug");

    // Game log = the game's own TYRA_LOG output (bin/log.txt, written on the
    // host fs by the running ELF); Emulator log = PCSX2's console (emulog.txt).
    const char* sources[] = {"Game log", "Emulator log"};
    ImGui::SetNextItemWidth(140.0f);
    if (ImGui::Combo("Source", &debugLogSource_, sources, 2)) {
        debugLog_.clear();       // don't show the other source's stale content
        debugNextReload_ = 0.0;  // reload immediately from the new source
    }

    std::string path;
    if (hasProject_) {
        if (debugLogSource_ == 0)
            path = (std::filesystem::path(project_.dir) / "bin" / "log.txt").string();
        else
            path = runner_.emulatorLogPath(project_);
    }

    ImGui::SameLine();
    bool reloadNow = false;
    if (ImGui::SmallButton("Reload")) reloadNow = true;
    ImGui::SameLine();
    ImGui::Checkbox("Auto", &debugAutoReload_);
    ImGui::SameLine();
    if (ImGui::SmallButton("Copy all")) ImGui::SetClipboardText(debugLog_.c_str());
    ImGui::SameLine();
    if (ImGui::SmallButton("Clear log") && !path.empty()) {
        std::ofstream(path, std::ios::trunc);  // best effort; may be held open
        debugLog_.clear();
        debugNextReload_ = 0.0;
    }
    ImGui::SameLine();
    // Debug option: pop a copyable dialog when the game hits an assertion.
    // Off -> errors go only here / to the console (the game logs them either
    // way). Persisted machine-wide in editor.ini.
    if (ImGui::Checkbox("Pop up on errors", &errorPopupEnabled_)) saveGlobalConfig();
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip(
            "When the running game hits a fatal error (a failed assertion, e.g.\n"
            "a missing texture) show it in a copyable dialog. When off, errors\n"
            "go only to this log / the console.");

    if (path.empty())
        ImGui::TextDisabled(
            debugLogSource_ == 0
                ? "No project open."
                : "Emulator not found. Set the path in Edit > Preferences.");
    else
        ImGui::TextDisabled("%s", path.c_str());
    ImGui::Separator();

    // Refresh from disk on demand, and while Auto is on, at most twice a second
    // (per-frame file reads would be wasteful for a possibly large log).
    const double now = ImGui::GetTime();
    if (!path.empty() && (reloadNow || (debugAutoReload_ && now >= debugNextReload_))) {
        debugLog_ = readTextFileTail(path, 1u << 20);  // last 1 MB
        debugNextReload_ = now + 0.5;
    }

    ImGui::BeginChild("##debugscroll", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None,
                      ImGuiWindowFlags_HorizontalScrollbar);
    const ImVec2 pad = ImGui::GetStyle().FramePadding;
    const ImVec2 textSize = ImGui::CalcTextSize(debugLog_.c_str(), nullptr, false);
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const ImVec2 inputSize(ImMax(textSize.x + pad.x * 2.0f, avail.x),
                           ImMax(textSize.y + pad.y * 2.0f, avail.y));
    ImGui::InputTextMultiline("##debuglog", const_cast<char*>(debugLog_.c_str()),
                              debugLog_.size() + 1, inputSize, ImGuiInputTextFlags_ReadOnly);

    // Follow the tail while new output arrives, unless the user scrolled up.
    static size_t lastSize = 0;
    if (debugLog_.size() != lastSize && ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 1.0f)
        ImGui::SetScrollHereY(1.0f);
    lastSize = debugLog_.size();

    ImGui::EndChild();
    ImGui::End();
}

// ---------------------------------------------------------------------------
// Game error catcher. A failed TYRA_ASSERT / TYRA_TRAP in the running game
// (e.g. a missing texture) no longer takes over the console screen - the engine
// prints the dump to the log and halts (see vendor/tyra debug.hpp). The editor
// tails that log and raises a copyable dialog so the error is not silent.
// ---------------------------------------------------------------------------

// The stable delimiters TyraDebug::trap() prints around an assertion dump (see
// vendor/tyra/engine/inc/debug/debug.hpp). Matched as substrings so a leading
// log prefix (the Debug window's nothing, or the runner's "[ps2] "/timestamp)
// does not defeat detection.
static const char kTyraAssertBanner[] = "==============  TYRA  ==============";
static const char kTyraAssertClose[] = "====================================";

// Returns the last complete TYRA assertion block in `text` (from its banner
// line through the closing rule, inclusive), or "" when there is none / it is
// still being written.
static std::string extractLastTyraAssert(const std::string& text) {
    const size_t banner = text.rfind(kTyraAssertBanner);
    if (banner == std::string::npos) return "";
    // Back up to the start of the banner's line so a log prefix is dropped and
    // the dump reads cleanly in the dialog.
    const size_t nl = text.rfind('\n', banner);
    const size_t lineStart = (nl == std::string::npos) ? 0 : nl + 1;
    const size_t close = text.find(kTyraAssertClose, banner);
    if (close == std::string::npos) return "";
    const size_t lineEnd = text.find('\n', close);
    return text.substr(lineStart,
                       (lineEnd == std::string::npos ? text.size() : lineEnd) - lineStart);
}

// Size of a file in bytes, or 0 when it is absent/unreadable (a deleted or
// not-yet-written log reads as 0 - which is exactly the "shrank" signal we want).
static size_t fileSizeOr0(const std::string& path) {
    std::error_code ec;
    const auto n = std::filesystem::file_size(path, ec);
    return ec ? 0 : (size_t)n;
}

std::string App::latestGameAssert() const {
    // PCSX2 runs: the game writes TYRA output to bin/log.txt on the host fs.
    std::string text;
    if (hasProject_) {
        const std::string gameLog =
            (std::filesystem::path(project_.dir) / "bin" / "log.txt").string();
        text = readTextFileTail(gameLog, 1u << 20);  // last 1 MB
    }
    // "Run on PS2" logs to the console instead (writeLogsToFile is off under
    // ps2link); that stream arrives in the runner log as "[ps2] ..." lines.
    // Append its tail so networked asserts are caught too - a PCSX2 run's
    // runner log carries no assertion banner, so this stays harmless there.
    const std::string rlog = runner_.log();
    const size_t kRunnerTail = 256u * 1024u;
    text += '\n';
    text += rlog.size() > kRunnerTail ? rlog.substr(rlog.size() - kRunnerTail) : rlog;
    return extractLastTyraAssert(text);
}

// Streams scene edits to the running (debug-profile) game - see the member
// block in app.hpp for the design. Self-throttled; the actual file write only
// happens when the live-representable state really changed.
void App::liveLinkTick() {
    namespace fs = std::filesystem;
    if (!hasProject_ || !project_.settings.liveLink ||
        project_.settings.buildProfile != "debug") {
        liveLinkState_ = LiveLinkState::Off;
        return;
    }

    const double now = ImGui::GetTime();
    if (now < liveLinkNextTick_) return;  // keep the last state between ticks
    liveLinkNextTick_ = now + 0.1;  // ~10 Hz matches the game's poll cadence

    // The as-built record the running build was made from. Re-read at most
    // every 1.5 s - a finished build (or Clean) must be picked up, but the
    // file is not worth hitting the disk for at tick rate.
    const fs::path binDir = fs::path(project_.dir) / "bin";
    if (now >= liveLinkSigNextRead_) {
        liveLinkSigNextRead_ = now + 1.5;
        liveLinkBuilt_ = LiveLinkBuilt();
        std::ifstream f(binDir / "livelink.sig");
        std::string line;
        if (f && std::getline(f, line) && line == "2") {
            LiveLinkBuilt b;
            bool ok = true;
            while (std::getline(f, line)) {
                if (line.rfind("ctx ", 0) == 0) {
                    b.ctxHash = std::strtoull(line.c_str() + 4, nullptr, 16);
                } else if (line.rfind("scene ", 0) == 0) {
                    b.scenes.emplace_back();
                } else if (line.size() >= 33 && !b.scenes.empty()) {
                    char* end = nullptr;
                    const uint64_t id = std::strtoull(line.c_str(), &end, 16);
                    const uint64_t recipe = std::strtoull(end, nullptr, 16);
                    b.scenes.back().emplace_back(id, recipe);
                } else {
                    ok = false;
                    break;
                }
            }
            if (ok) {
                b.loaded = true;
                liveLinkBuilt_ = std::move(b);
            }
        }
    }
    if (!liveLinkBuilt_.loaded) {
        liveLinkState_ = LiveLinkState::NoBuild;
        return;
    }

    // Is the live project representable against the as-built record? Built
    // objects must keep their recipe (a live patch can't change it); new
    // objects need an equal-recipe as-built template to clone (and must be
    // spawnable at all); the cross-object context must be untouched.
    const int scene = project_.activeScene;
    if (liveLinkBuilt_.ctxHash != project::liveLinkContextHash(project_) ||
        scene >= (int)liveLinkBuilt_.scenes.size()) {
        liveLinkState_ = LiveLinkState::RebuildNeeded;
        return;
    }
    const auto& built = liveLinkBuilt_.scenes[scene];
    const SceneData& sc = project_.active();

    // Per-record resolution: templateIdx = -1 for as-built ids, else the
    // as-built index of an equal-recipe object to clone. The spawn pool holds
    // 32 clones - past that the session stops being representable.
    struct Rec {
        uint64_t id;
        int32_t tmpl;
        const SceneObject* o;
    };
    std::vector<Rec> recs;
    recs.reserve(sc.objects.size());
    int spawned = 0;
    for (const SceneObject& o : sc.objects) {
        const uint64_t id = project::liveLinkIdHash(o);
        const uint64_t recipe = project::liveLinkRecipeHash(o);
        int32_t tmpl = -1;
        bool foundId = false;
        for (size_t i = 0; i < built.size(); ++i) {
            if (built[i].first == id) {
                foundId = true;
                if (built[i].second != recipe) {
                    liveLinkState_ = LiveLinkState::RebuildNeeded;
                    return;
                }
                break;
            }
        }
        if (!foundId) {
            if (!project::liveLinkCanSpawnLive(o) ||
                ++spawned > 32 /* MAX_SPAWNED_OBJECTS */) {
                liveLinkState_ = LiveLinkState::RebuildNeeded;
                return;
            }
            tmpl = -1;
            for (size_t i = 0; i < built.size(); ++i)
                if (built[i].second == recipe) {
                    tmpl = (int32_t)i;
                    break;
                }
            if (tmpl < 0) {
                liveLinkState_ = LiveLinkState::RebuildNeeded;
                return;
            }
        }
        recs.push_back({id, tmpl, &o});
    }
    liveLinkState_ = LiveLinkState::Live;

    // Snapshot body: scene + count + one 64-byte record per object (id +
    // template + 12 floats). Deletions are implicit - the game hides whatever
    // is absent.
    std::vector<unsigned char> body;
    body.reserve(8 + recs.size() * 64);
    auto put = [&](const void* v, size_t n) {
        const unsigned char* b = static_cast<const unsigned char*>(v);
        body.insert(body.end(), b, b + n);
    };
    const int32_t scene32 = scene;
    const int32_t count = (int32_t)recs.size();
    put(&scene32, 4);
    put(&count, 4);
    const uint32_t pad = 0;
    for (const Rec& r : recs) {
        put(&r.id, 8);
        put(&r.tmpl, 4);
        put(&pad, 4);
        put(r.o->position, 12);
        put(r.o->rotation, 12);
        put(r.o->scale, 12);
        put(r.o->color, 12);
    }
    if (body == liveLinkLastPayload_) return;  // nothing to stream

    // Full file: header (magic/version/seq + body's scene/count/reserved),
    // records, footer echoing seq - the game rejects torn writes. Written to
    // a sibling tmp and renamed so the game never sees a half file; if the
    // rename loses a race with the game's fread, retry on the next tick.
    const uint32_t seq = liveLinkSeq_ + 1;
    std::vector<unsigned char> file;
    file.reserve(24 + body.size() - 8 + 4);
    const uint32_t magic = 0x4C4C5854, version = 2, reserved = 0;
    const uint32_t footer = seq ^ 0x5A5A5A5AU;
    auto app32 = [&](const void* v) {
        const unsigned char* b = static_cast<const unsigned char*>(v);
        file.insert(file.end(), b, b + 4);
    };
    app32(&magic), app32(&version), app32(&seq);
    file.insert(file.end(), body.begin(), body.begin() + 8);  // scene, count
    app32(&reserved);
    file.insert(file.end(), body.begin() + 8, body.end());
    app32(&footer);

    const fs::path tmp = binDir / "livelink.tmp";
    const fs::path dst = binDir / "livelink.bin";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) return;
        out.write(reinterpret_cast<const char*>(file.data()),
                  (std::streamsize)file.size());
        if (!out) return;
    }
    std::error_code ec;
    fs::rename(tmp, dst, ec);
    if (ec) return;  // game holds the file open right now - next tick retries

    liveLinkSeq_ = seq;
    liveLinkLastPayload_ = std::move(body);
}

void App::pollGameError() {
    if (!hasProject_) return;
    const double now = ImGui::GetTime();
    if (now < errorNextPoll_) return;
    errorNextPoll_ = now + 0.5;  // a log tail twice a second is plenty

    // Detect a fresh run / a cleared log by a shrinking source. The Runner
    // deletes bin/log.txt before each launch, and Output "Clear" empties the
    // runner log - either shrinking means the previously seen dump is gone, so
    // forget it and let an identical new error (same missing file, same line)
    // pop again instead of being deduped against the stale text.
    const size_t gsz =
        fileSizeOr0((std::filesystem::path(project_.dir) / "bin" / "log.txt").string());
    const size_t rsz = runner_.log().size();
    if (gsz < errorGameLogSize_ || rsz < errorRunnerLogSize_) errorSeenSig_.clear();
    errorGameLogSize_ = gsz;
    errorRunnerLogSize_ = rsz;

    const std::string block = latestGameAssert();
    if (block.empty() || block == errorSeenSig_) return;
    errorSeenSig_ = block;  // handled once, whether or not we pop
    if (errorPopupEnabled_) {
        errorModalText_ = block;
        openErrorPopup_ = true;
        // The game window (PCSX2) has the foreground when an error fires, so the
        // dialog would open behind it unnoticed. Flash the taskbar entry and
        // pull the editor forward so the error actually gets the user's
        // attention. (requestAttention flashes even if focusWindow is refused.)
        if (window_) {
            glfwRequestWindowAttention(window_);
            glfwFocusWindow(window_);
        }
    }
}

void App::drawErrorModal() {
    if (openErrorPopup_) {
        ImGui::OpenPopup("Game error");
        openErrorPopup_ = false;
    }
    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(scaled(640), 0), ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal("Game error", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        return;

    // The engine tags non-fatal errors (a recovered asset load) with this
    // header; everything else is a fatal assertion that stopped the game.
    const bool fatal = errorModalText_.find("Non-fatal") == std::string::npos;
    if (fatal) {
        ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.4f, 1.0f),
                           "The game hit an assertion and stopped.");
        ImGui::TextDisabled(
            "The same dump is in the Debug window (Game log). Fix the cause and\n"
            "run again - a common one is a missing or non-power-of-two texture.");
    } else {
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.35f, 1.0f),
                           "The game reported an error but kept running.");
        ImGui::TextDisabled(
            "A missing asset was skipped - a placeholder (magenta checkerboard)\n"
            "texture, or no sound. Fix it and rebuild. The same dump is in the\n"
            "Debug window (Game log).");
    }
    ImGui::Separator();

    // Read-only but fully selectable, so the whole dump can be copied.
    ImGui::InputTextMultiline("##errtext", const_cast<char*>(errorModalText_.c_str()),
                              errorModalText_.size() + 1,
                              ImVec2(scaled(620), scaled(240)),
                              ImGuiInputTextFlags_ReadOnly);
    if (ImGui::Button("Copy", ImVec2(scaled(120), 0)))
        ImGui::SetClipboardText(errorModalText_.c_str());
    ImGui::SameLine();
    if (ImGui::Button("Close", ImVec2(scaled(120), 0))) ImGui::CloseCurrentPopup();

    // The off switch, right where the noise is (mirrors the Debug window
    // checkbox; both persist to editor.ini).
    bool consoleOnly = !errorPopupEnabled_;
    if (ImGui::Checkbox("Only log to console (don't pop up on errors)", &consoleOnly)) {
        errorPopupEnabled_ = !consoleOnly;
        saveGlobalConfig();
    }
    ImGui::EndPopup();
}

// ---------------------------------------------------------------------------
// Disc Layout window: the ISO export plan as a reorderable list plus a disc
// visualization (files as arcs, angle proportional to sectors, colored by
// load group). Dragging rows persists the order into <project>/iso-layout.txt
// so Export PS2 ISO lays the data out the same way.
// ---------------------------------------------------------------------------

static ImU32 discGroupColor(const std::string& g) {
    if (g == "boot") return IM_COL32(235, 140, 52, 255);
    if (g == "startup") return IM_COL32(84, 190, 247, 255);
    if (g == "music") return IM_COL32(186, 104, 200, 255);
    if (g == "other") return IM_COL32(150, 150, 150, 255);
    // scene:<name> - stable green/teal per scene name
    unsigned h = 2166136261u;
    for (char c : g) h = (h ^ (unsigned char)c) * 16777619u;
    static const ImU32 greens[] = {IM_COL32(102, 187, 106, 255), IM_COL32(38, 166, 154, 255),
                                   IM_COL32(174, 213, 129, 255), IM_COL32(0, 150, 136, 255),
                                   IM_COL32(205, 220, 57, 255)};
    return greens[h % 5];
}

static std::string discPrettySize(uint64_t bytes) {
    char buf[32];
    if (bytes >= 1024 * 1024)
        snprintf(buf, sizeof(buf), "%.1f MB", bytes / (1024.0 * 1024.0));
    else if (bytes >= 1024)
        snprintf(buf, sizeof(buf), "%.1f KB", bytes / 1024.0);
    else
        snprintf(buf, sizeof(buf), "%llu B", (unsigned long long)bytes);
    return buf;
}

static ImU32 discBrighten(ImU32 col, float amount) {
    ImVec4 f = ImGui::ColorConvertU32ToFloat4(col);
    f.x += (1.0f - f.x) * amount;
    f.y += (1.0f - f.y) * amount;
    f.z += (1.0f - f.z) * amount;
    return ImGui::ColorConvertFloat4ToU32(f);
}

void App::drawDiscLayoutWindow() {
    if (!showDiscLayout_ || !hasProject_) return;

    // Replan after any build/export finishes (bin/ contents changed).
    if (runner_.busy())
        discRunnerWasBusy_ = true;
    else if (discRunnerWasBusy_) {
        discRunnerWasBusy_ = false;
        discPlanDirty_ = true;
    }
    if (discPlanDirty_) {
        discPlanDirty_ = false;
        discPlanError_.clear();
        discPlanWarnings_.clear();
        isoexport::Plan plan;
        const std::string err = isoexport::plan(project_, &plan, [this](const std::string& l) {
            discPlanWarnings_ += l + "\n";
        });
        if (err.empty()) {
            discPlan_ = std::move(plan);
        } else {
            discPlan_ = {};
            discPlanError_ = err;
        }
        if (discSelected_ >= (int)discPlan_.items.size()) discSelected_ = -1;
    }

    ImGui::SetNextWindowSize(ImVec2(scaled(980), scaled(560)),
                             ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Disc Layout", &showDiscLayout_)) {
        ImGui::End();
        return;
    }

    if (!discPlanError_.empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.4f, 1.0f), "%s", discPlanError_.c_str());
        ImGui::TextDisabled("Project > Build produces bin/, then come back here.");
        if (ImGui::Button("Refresh")) discPlanDirty_ = true;
        ImGui::End();
        return;
    }

    // --- Toolbar -----------------------------------------------------------
    const bool busy = runner_.busy();
    if (ImGui::Button("Refresh")) discPlanDirty_ = true;
    ImGui::SameLine();
    ImGui::BeginDisabled(busy);
    if (ImGui::Button("Export ISO")) runner_.exportIso(project_);
    ImGui::EndDisabled();
    if (discPlan_.manualOrder) {
        ImGui::SameLine();
        if (ImGui::Button("Reset to automatic order")) {
            isoexport::saveManualOrder(project_, {});
            discPlanDirty_ = true;
        }
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(scaled(150.0f));
    ImGui::Combo("##capacity", &discCapacity_, "Fit to data\0CD-R (700 MB)\0DVD-5 (4.7 GB)\0");

    const uint32_t kCdSectors = 360000, kDvd5Sectors = 2298496;
    uint32_t capSectors = discPlan_.totalSectors;
    if (discCapacity_ == 1) capSectors = std::max(capSectors, kCdSectors);
    if (discCapacity_ == 2) capSectors = std::max(capSectors, kDvd5Sectors);

    uint64_t dataBytes = 0;
    for (const auto& it : discPlan_.items) dataBytes += it.size;
    ImGui::SameLine();
    ImGui::Text("%zu files, %s data, image %s", discPlan_.items.size(),
                discPrettySize(dataBytes).c_str(),
                discPrettySize((uint64_t)discPlan_.totalSectors * 2048).c_str());
    if ((discCapacity_ == 1 && discPlan_.totalSectors > kCdSectors) ||
        (discCapacity_ == 2 && discPlan_.totalSectors > kDvd5Sectors)) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.35f, 1.0f), "- does not fit this disc!");
    }
    if (!discPlanWarnings_.empty()) {
        if (ImGui::CollapsingHeader("Name warnings"))
            ImGui::TextWrapped("%s", discPlanWarnings_.c_str());
    }
    ImGui::Separator();

    // --- Left: file table in burn order, drag rows to reorder ---------------
    const float discPaneW =
        std::max(scaled(280.0f), ImGui::GetContentRegionAvail().x * 0.38f);
    ImGui::BeginChild("##discfiles",
                      ImVec2(ImGui::GetContentRegionAvail().x - discPaneW - scaled(8.0f), 0));
    ImGui::TextDisabled("Drag rows to change the disc order (saved to iso-layout.txt).");
    if (ImGui::BeginTable("##disctable", 5,
                          ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
                              ImGuiTableFlags_ScrollY)) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, scaled(28.0f));
        ImGui::TableSetupColumn("File", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Group", ImGuiTableColumnFlags_WidthFixed, scaled(100.0f));
        ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed, scaled(72.0f));
        ImGui::TableSetupColumn("LBA", ImGuiTableColumnFlags_WidthFixed, scaled(64.0f));
        ImGui::TableHeadersRow();

        int dragSrc = -1, dragDst = -1;
        for (int i = 0; i < (int)discPlan_.items.size(); ++i) {
            const auto& it = discPlan_.items[i];
            const bool isBoot = it.group == "boot";
            ImGui::PushID(i);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            char idx[16];
            snprintf(idx, sizeof(idx), "%d", i);
            if (ImGui::Selectable(idx, discSelected_ == i,
                                  ImGuiSelectableFlags_SpanAllColumns))
                discSelected_ = i;
            if (!isBoot && ImGui::BeginDragDropSource()) {
                ImGui::SetDragDropPayload("DISC_FILE", &i, sizeof(int));
                ImGui::TextUnformatted(it.relPath.c_str());
                ImGui::EndDragDropSource();
            }
            if (!isBoot && ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload* pl = ImGui::AcceptDragDropPayload("DISC_FILE")) {
                    dragSrc = *(const int*)pl->Data;
                    dragDst = i;
                }
                ImGui::EndDragDropTarget();
            }
            ImGui::TableNextColumn();
            if (it.pinned) {
                ImGui::TextDisabled("*");
                ImGui::SameLine(0.0f, scaled(3.0f));
            }
            ImGui::TextUnformatted(it.isoPath.c_str());
            ImGui::TableNextColumn();
            const ImU32 col = discGroupColor(it.group);
            ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(col), "%s", it.group.c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(discPrettySize(it.size).c_str());
            ImGui::TableNextColumn();
            ImGui::Text("%u", it.lba);
            ImGui::PopID();
        }
        ImGui::EndTable();

        if (dragSrc >= 0 && dragDst >= 0 && dragSrc != dragDst) {
            // Move src to dst in display order, then persist every non-boot
            // path - the whole visible order becomes the explicit one.
            std::vector<isoexport::PlanItem> items = discPlan_.items;
            isoexport::PlanItem moved = items[dragSrc];
            items.erase(items.begin() + dragSrc);
            items.insert(items.begin() + dragDst, moved);
            std::vector<std::string> rels;
            for (const auto& it : items)
                if (it.group != "boot") rels.push_back(it.relPath);
            isoexport::saveManualOrder(project_, rels);
            discPlanDirty_ = true;
            discSelected_ = dragDst;
        }
    }
    ImGui::EndChild();
    ImGui::SameLine();

    // --- Right: the disc ----------------------------------------------------
    // Drawn the way data actually sits on a PS2 disc: a spiral track that
    // starts at the hub and winds outward, approximated as concentric track
    // rings with a constant pitch. Like the real thing (constant linear
    // density), an outer turn holds more data than an inner one, so
    // LBA -> radius follows the area formula r = sqrt(rIn^2 + f*(rOut^2-rIn^2)).
    // A small file is a short arc on one track; a big one is a band of turns.
    ImGui::BeginChild("##discview", ImVec2(0, 0));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float legendH = ImGui::GetTextLineHeightWithSpacing() * 2.4f;
    const float side = std::min(avail.x, std::max(scaled(120.0f), avail.y - legendH));
    const ImVec2 c(origin.x + avail.x * 0.5f, origin.y + side * 0.5f);
    const float rOut = side * 0.48f;
    const float rIn = side * 0.165f;
    const float kTop = -1.5707963f, kTau = 6.2831853f;

    const int tracks = std::max(24, (int)((rOut - rIn) / 3.0f));
    const float pitch = (rOut - rIn) / (float)tracks;
    const double areaSpan = (double)rOut * rOut - (double)rIn * rIn;
    // LBA of the spiral position at radius r (area-proportional fill)
    auto lbaAtR = [&](float r) {
        return (double)capSectors * ((double)r * r - (double)rIn * rIn) / areaSpan;
    };

    // Segment list covering the whole capacity: metadata, files, free space.
    struct Seg {
        double begin, end;
        ImU32 col;
        int item;  // index into items, or -1 metadata / -2 free
    };
    std::vector<Seg> segs;
    segs.push_back({0.0, (double)discPlan_.dataStartLba, IM_COL32(120, 120, 128, 255), -1});
    for (int i = 0; i < (int)discPlan_.items.size(); ++i) {
        const auto& it = discPlan_.items[i];
        segs.push_back({(double)it.lba, (double)(it.lba + std::max(it.sectors, 1u)),
                        discGroupColor(it.group), i});
    }
    segs.push_back({(double)discPlan_.totalSectors, (double)capSectors,
                    IM_COL32(46, 46, 50, 255), -2});

    // Mouse -> track -> LBA -> segment (for hover/click before drawing)
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    const float mdx = mouse.x - c.x, mdy = mouse.y - c.y;
    const float mr = std::sqrt(mdx * mdx + mdy * mdy);
    float ma = std::atan2(mdy, mdx) - kTop;
    while (ma < 0.0f) ma += kTau;
    const bool overDisc = ImGui::IsWindowHovered() && mr >= rIn && mr <= rOut;
    int hovered = -1;
    bool hoverMeta = false, hoverFree = false;
    if (overDisc) {
        const int k = std::min(tracks - 1, std::max(0, (int)((mr - rIn) / pitch)));
        const double t0 = lbaAtR(rIn + k * pitch), t1 = lbaAtR(rIn + (k + 1) * pitch);
        const double lba = t0 + (t1 - t0) * (ma / kTau);
        for (const auto& s : segs)
            if (lba >= s.begin && lba < s.end) {
                hovered = s.item;
                hoverMeta = s.item == -1;
                hoverFree = s.item == -2;
            }
    }

    // Track rings, inside out; a ring is split into arcs where segments
    // begin/end mid-turn. Angles stay continuous across turns: an LBA's
    // angle is its fraction of the turn it lives on, measured from the top.
    size_t cursor = 0;
    for (int k = 0; k < tracks; ++k) {
        const float rc = rIn + (k + 0.5f) * pitch;
        const double t0 = lbaAtR(rIn + k * pitch);
        const double t1 = std::max(t0 + 1.0, lbaAtR(rIn + (k + 1) * pitch));
        while (cursor > 0 && segs[cursor].begin > t0) --cursor;  // capacity changed
        size_t s = cursor;
        while (s < segs.size() && segs[s].end <= t0) ++s;
        cursor = s;
        for (; s < segs.size() && segs[s].begin < t1; ++s) {
            const double b0 = std::max(segs[s].begin, t0), b1 = std::min(segs[s].end, t1);
            if (b1 <= b0) continue;
            float a0 = kTop + (float)((b0 - t0) / (t1 - t0)) * kTau;
            float a1 = kTop + (float)((b1 - t0) / (t1 - t0)) * kTau;
            // keep hairline files visible on their track
            if (segs[s].item >= 0) a1 = std::max(a1, a0 + 0.03f);
            ImU32 col = segs[s].col;
            if (segs[s].item >= 0 &&
                (segs[s].item == hovered || segs[s].item == discSelected_))
                col = discBrighten(col, segs[s].item == discSelected_ ? 0.45f : 0.3f);
            dl->PathArcTo(c, rc, a0, a1);
            dl->PathStroke(col, 0, pitch + 0.75f);
        }
    }
    dl->AddCircle(c, rIn - pitch * 0.5f, IM_COL32(20, 20, 22, 255), 64, 1.5f);
    dl->AddCircle(c, rOut + pitch * 0.5f, IM_COL32(20, 20, 22, 255), 96, 1.5f);

    // Selection marker: radial tick at the file's first sector + a pointer
    // ring so even a hairline arc deep in the band is findable.
    if (discSelected_ >= 0 && discSelected_ < (int)discPlan_.items.size()) {
        const auto& it = discPlan_.items[discSelected_];
        const float rSel = (float)std::sqrt((double)rIn * rIn +
                                            (double)it.lba / (double)capSectors * areaSpan);
        const int k = std::min(tracks - 1, std::max(0, (int)((rSel - rIn) / pitch)));
        const double t0 = lbaAtR(rIn + k * pitch), t1 = lbaAtR(rIn + (k + 1) * pitch);
        const float a = kTop + (float)(((double)it.lba - t0) / (t1 - t0)) * kTau;
        const float rc = rIn + (k + 0.5f) * pitch;
        const ImVec2 dir(std::cos(a), std::sin(a));
        dl->AddLine(ImVec2(c.x + dir.x * (rc - pitch), c.y + dir.y * (rc - pitch)),
                    ImVec2(c.x + dir.x * (rc + pitch), c.y + dir.y * (rc + pitch)),
                    IM_COL32(255, 255, 255, 230), 2.0f);
        dl->AddCircle(ImVec2(c.x + dir.x * rc, c.y + dir.y * rc), pitch * 1.6f,
                      IM_COL32(255, 255, 255, 200), 24, 1.5f);
    }

    // center label
    {
        char mid[64];
        snprintf(mid, sizeof(mid), "%s", discPrettySize(dataBytes).c_str());
        const ImVec2 ts = ImGui::CalcTextSize(mid);
        dl->AddText(ImVec2(c.x - ts.x * 0.5f, c.y - ts.y), IM_COL32(220, 220, 220, 255), mid);
        char pct[64];
        snprintf(pct, sizeof(pct), "%.1f%% of disc",
                 100.0 * (double)discPlan_.totalSectors / (double)capSectors);
        const ImVec2 ts2 = ImGui::CalcTextSize(pct);
        dl->AddText(ImVec2(c.x - ts2.x * 0.5f, c.y + 2.0f), IM_COL32(140, 140, 140, 255), pct);
    }

    if (hovered >= 0) {
        const auto& it = discPlan_.items[hovered];
        ImGui::BeginTooltip();
        ImGui::TextUnformatted(it.isoPath.c_str());
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(discGroupColor(it.group)), "%s%s",
                           it.group.c_str(), it.pinned ? " (pinned)" : "");
        ImGui::Text("%s, LBA %u-%u", discPrettySize(it.size).c_str(), it.lba,
                    it.lba + std::max(it.sectors, 1u) - 1);
        ImGui::EndTooltip();
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) discSelected_ = hovered;
    } else if (hoverMeta) {
        ImGui::SetTooltip("ISO9660 metadata (volume descriptor, path tables, directories)");
    } else if (hoverFree) {
        ImGui::SetTooltip("Free space");
    }

    // legend under the disc, in first-appearance order
    ImGui::SetCursorScreenPos(ImVec2(origin.x, origin.y + side + 6.0f));
    std::vector<std::string> groups;
    for (const auto& it : discPlan_.items)
        if (std::find(groups.begin(), groups.end(), it.group) == groups.end())
            groups.push_back(it.group);
    for (size_t i = 0; i < groups.size(); ++i) {
        if (i) ImGui::SameLine();
        const ImU32 col = discGroupColor(groups[i]);
        const ImVec2 p = ImGui::GetCursorScreenPos();
        dl->AddRectFilled(p, ImVec2(p.x + 10.0f, p.y + 10.0f), col, 2.0f);
        ImGui::Dummy(ImVec2(12.0f, 10.0f));
        ImGui::SameLine();
        ImGui::TextUnformatted(groups[i].c_str());
    }
    ImGui::TextDisabled("* = pinned by iso-layout.txt; inner rim = disc start (lowest LBA)");
    ImGui::EndChild();

    ImGui::End();
}

void App::drawNewProjectModal() {
    if (openNewProjectPopup_) {
        ImGui::OpenPopup("New Project");
        openNewProjectPopup_ = false;
        newProjectError_.clear();
        // Propose the configured default projects folder (Edit > Preferences).
        std::snprintf(newLocation_, sizeof(newLocation_), "%s",
                      defaultNewProjectLocation(globalDefaultProjectsDir_).c_str());
    }

    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(scaled(560), 0), ImGuiCond_Appearing);

    if (ImGui::BeginPopupModal("New Project", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::InputText("Project name", newName_, sizeof(newName_));

        ImGui::InputText("Location", newLocation_, sizeof(newLocation_));
        ImGui::SameLine();
        if (ImGui::Button("...")) {
            std::string dir = pickFolder();
            if (!dir.empty())
                std::snprintf(newLocation_, sizeof(newLocation_), "%s", dir.c_str());
        }

        ImGui::SeparatorText("Terrain (flat)");
        ImGui::InputInt("Width (units)", &newWidth_);
        ImGui::InputInt("Depth (units)", &newDepth_);
        if (newWidth_ < 1) newWidth_ = 1;
        if (newDepth_ < 1) newDepth_ = 1;
        if (newWidth_ > 4096) newWidth_ = 4096;
        if (newDepth_ > 4096) newDepth_ = 4096;

        ImGui::SeparatorText("Preset");
        const char* presetNames[] = {
            "Empty (orbit camera, no objects)",
            "FPP (a single player entity)"};
        ImGui::Combo("Preset", &newTemplate_, presetNames, 2);

        ImGui::TextDisabled("Creates: %s\\%s", newLocation_, newName_);
        ImGui::TextDisabled("Default scene \"main\" with a flat %d x %d terrain.%s", newWidth_,
                            newDepth_,
                            newTemplate_ == 1 ? " Adds a player entity." : "");

        if (!newProjectError_.empty())
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", newProjectError_.c_str());

        ImGui::Separator();
        if (ImGui::Button("Create", ImVec2(scaled(120), 0))) {
            Project p;
            TerrainConfig t{newWidth_, newDepth_};
            const char* preset = newTemplate_ == 1 ? "fpp" : "empty";
            std::string err = project::create(p, newName_, newLocation_, t, preset);
            if (err.empty()) {
                project_ = p;
                hasProject_ = true;
                applyProjectToViewport();
                attachProject();  // resets dirty + window title
                ImGui::CloseCurrentPopup();
            } else {
                newProjectError_ = err;
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(scaled(120), 0))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

void App::applyProjectToViewport() {
    project::ensureHeightmap(project_);
    const SceneData& sc = project_.active();
    // Scene-visual settings resolve project defaults + this scene's overrides.
    const ProjectSettings rs = project::resolvedSettings(project_, sc);
    viewport_.setProjectDir(project_.dir);
    const project::TerrainMaterial tm =
        project::resolveTerrainMaterial(project_, rs.terrainMaterial);
    viewport_.setTerrainMaterial(tm.texture, tm.kd, tm.present, tm.tile);
    viewport_.setTerrain(sc.terrain, project_.settings.terrainDetail, sc.heights, sc.hmW,
                         sc.hmD);
    viewport_.setSky(rs.skyColor, rs.skyTopColor, rs.skyDome, rs.zenithSize);
    viewport_.setUsableHighlight(rs.highlightUsable, rs.highlightColor);
    viewport_.setLighting(rs.lightDir, rs.ambient, rs.diffuse, rs.lightColor, rs.brightness);
    viewport_.setFog(rs.fogEnabled && showFog_, rs.fogColor, rs.fogStart, rs.fogEnd);
    // The flashlight is a Player object property; preview the first player's
    // (its Enabled flag is the initial state - the toggle button / flow graph
    // only act at runtime, which the editor preview cannot simulate).
    const SceneObject* player = nullptr;
    for (const SceneObject& o : sc.objects)
        if (o.type == PrimitiveType::Player) {
            player = &o;
            break;
        }
    const float offColor[3] = {0.75f, 0.75f, 0.62f};
    if (player && player->flashlightEnabled)
        viewport_.setFlashlight(true, player->flashlightColor, player->flashlightRange,
                                player->flashlightAngle);
    else
        viewport_.setFlashlight(false, offColor, 30.0f, 20.0f);
}

void App::drawPreferencesModal() {
    if (openPreferencesPopup_) {
        ImGui::OpenPopup("Project Preferences");
        openPreferencesPopup_ = false;
    }

    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(scaled(560), 0), ImGuiCond_Appearing);

    if (!ImGui::BeginPopupModal("Project Preferences", nullptr,
                                ImGuiWindowFlags_AlwaysAutoResize))
        return;

    ImGui::SeparatorText("Game");
    const char* templateNames[] = {"Terrain orbit", "FPP walkthrough"};
    ImGui::Combo("Template", &prefTemplate_, templateNames, 2);
    ImGui::TextDisabled("Applies to generated sources (files with the editor marker).");

    ImGui::SeparatorText("Build");
    int videoSys = prefSettings_.videoSystem == "pal"    ? 2
                   : prefSettings_.videoSystem == "ntsc" ? 1
                                                         : 0;
    const char* videoSysNames[] = {"Auto (console region)", "NTSC (60 Hz)",
                                   "PAL (50 Hz)"};
    if (ImGui::Combo("Target system", &videoSys, videoSysNames, 3))
        prefSettings_.videoSystem = videoSys == 2 ? "pal" : videoSys == 1 ? "ntsc" : "auto";
    ImGui::TextDisabled(
        "Video signal of the built game (also on exported ISOs). Auto follows\n"
        "the console region. Game speed is normalized - PAL (50 Hz) and NTSC\n"
        "(60 Hz) play at the same wall-clock speed.");
    int dispMode = prefSettings_.displayMode == "1080i"         ? 2
                   : prefSettings_.displayMode == "progressive" ? 1
                                                                : 0;
    const char* dispModeNames[] = {"Interlaced (480i/576i)",
                                   "Progressive scan (480p)", "1080i (HD)"};
    if (ImGui::Combo("Display mode", &dispMode, dispModeNames, 3))
        prefSettings_.displayMode =
            dispMode == 2 ? "1080i" : dispMode == 1 ? "progressive" : "interlaced";
    ImGui::TextDisabled(
        "Scan mode of the built game. Interlaced is the stock TV signal and\n"
        "follows Target system. Progressive (flicker-free 480p) and 1080i\n"
        "always run at 60 Hz and need component (YPbPr) cables on a real\n"
        "console - PCSX2 displays every mode. 1080i renders a 448x540 frame\n"
        "(sharper vertically) and leaves less VRAM for textures. Both can\n"
        "also be switched at runtime with the Set Display Mode flow node,\n"
        "which shows a keep-or-revert prompt with an automatic rollback.");
    ImGui::Checkbox("Widescreen (16:9)", &prefSettings_.widescreen);
    ImGui::TextDisabled(
        "Widens the projection so proportions are correct on a 16:9 TV\n"
        "(anamorphic - on a 4:3 set the picture looks squeezed). In 1080i\n"
        "the picture also fills more of the screen. HUD sprites stretch\n"
        "with the screen. Runtime switch: the Set Widescreen flow node.");
    int profile = prefSettings_.buildProfile == "debug" ? 1 : 0;
    const char* profileNames[] = {"Release", "Debug"};
    if (ImGui::Combo("Profile", &profile, profileNames, 2))
        prefSettings_.buildProfile = profile == 1 ? "debug" : "release";
    ImGui::Checkbox("Disable VSync (experimental)", &prefSettings_.disableVsync);
    ImGui::TextDisabled(
        "Skips the vsync wait before the buffer flip. The frame rate becomes\n"
        "continuous instead of snapping between 50 and 25 (PAL), at the cost\n"
        "of screen tearing. Gameplay speed is unaffected either way.");
    ImGui::BeginDisabled(profile == 0);
    ImGui::Checkbox("Show FPS", &prefSettings_.showFps);
    ImGui::Checkbox("Show memory usage", &prefSettings_.showMemory);
    ImGui::Checkbox("Show frame profiler", &prefSettings_.showProfiler);
    ImGui::EndDisabled();
    ImGui::TextDisabled(
        "Debug-profile overlays drawn in the top-left corner of the game:\n"
        "frames per second, free EE RAM, and a per-phase EE-time breakdown\n"
        "(whole frame / scene / usable-highlight / particles, avg ms over\n"
        "~1s). Stripped from release builds.");
    ImGui::BeginDisabled(profile == 0);
    ImGui::Checkbox("Live Link", &prefSettings_.liveLink);
    ImGui::EndDisabled();
    ImGui::TextDisabled(
        "Debug builds poll livelink.bin next to the ELF so the editor can\n"
        "stream scene edits into the running game (docs/live-link.md). Turn\n"
        "off if you don't want your game patched from outside; release\n"
        "builds never carry the poller. Also toggled by the LIVE chip in\n"
        "the toolbar and Build > Live Link.");

    ImGui::SeparatorText("Terrain");
    ImGui::InputInt("Width (units)", &prefTerrain_.width);
    ImGui::InputInt("Depth (units)", &prefTerrain_.depth);
    prefTerrain_.width = prefTerrain_.width < 1 ? 1 : prefTerrain_.width > 4096 ? 4096
                                                                                : prefTerrain_.width;
    prefTerrain_.depth = prefTerrain_.depth < 1 ? 1 : prefTerrain_.depth > 4096 ? 4096
                                                                                : prefTerrain_.depth;
    ImGui::SliderInt("Detail (max grid cells)", &prefSettings_.terrainDetail, 4, 512);
    ImGui::TextDisabled("More cells = smaller triangles = fewer clipping artifacts,");
    ImGui::TextDisabled("but more geometry for the PS2 to push.");

    ImGui::DragFloat("View distance", &prefSettings_.terrainViewDistance, 1.0f, 0.0f,
                     2000.0f,
                     prefSettings_.terrainViewDistance > 0.0f ? "%.0f units"
                                                              : "off (whole map)");
    if (prefSettings_.terrainViewDistance < 0.0f)
        prefSettings_.terrainViewDistance = 0.0f;
    ImGui::TextDisabled(
        "The game keeps only the terrain chunks within this range of the\n"
        "camera in memory; the rest streams in as the player moves. Pair it\n"
        "with fog (view distance ~ fog end) to hide the pop-in. 0 keeps the\n"
        "whole map resident. Meant for FPP - orbit showcases see the whole\n"
        "map at once and should leave it 0.");

    // Worst-case resident mesh memory so oversized configs are caught here,
    // not by an out-of-memory PS2. Mirrors the generated game: 6 verts/cell,
    // 32 B untextured / 48 B textured, chunks of 16x16 cells.
    {
        const SceneData& sc = project_.active();
        const int cellsX = sc.terrain.width < prefSettings_.terrainDetail
                               ? sc.terrain.width
                               : prefSettings_.terrainDetail;
        const int cellsZ = sc.terrain.depth < prefSettings_.terrainDetail
                               ? sc.terrain.depth
                               : prefSettings_.terrainDetail;
        const int bytesPerVert = prefSettings_.terrainMaterial.empty() ? 32 : 48;
        double cells = (double)cellsX * cellsZ;
        if (prefSettings_.terrainViewDistance > 0.0f) {
            // resident rect in chunks (16 cells each), as in the generated game
            const float spanX = 16.0f * (float)sc.terrain.width / (float)cellsX;
            const float spanZ = 16.0f * (float)sc.terrain.depth / (float)cellsZ;
            const double nx = (int)(2.0f * prefSettings_.terrainViewDistance / spanX) + 3;
            const double nz = (int)(2.0f * prefSettings_.terrainViewDistance / spanZ) + 3;
            const double rectCells = nx * nz * 16.0 * 16.0;
            if (rectCells < cells) cells = rectCells;
        }
        const double mb = cells * 6.0 * bytesPerVert / (1024.0 * 1024.0);
        if (mb > 8.0)
            ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f),
                               "Resident terrain mesh: ~%.1f MB of the PS2's 32 MB - set"
                               " a view distance or lower the detail.",
                               mb);
        else
            ImGui::TextDisabled("Resident terrain mesh: ~%.1f MB (active scene).", mb);
    }

    ImGui::SeparatorText("Rendering");
    int clipMode = prefSettings_.clipping == "fast" ? 1 : 0;
    const char* clipNames[] = {
        "Precise clipping (no holes at screen edges, costs EE time)",
        "Fast culling (fastest; big near triangles may vanish)"};
    if (ImGui::Combo("Triangles", &clipMode, clipNames, 2))
        prefSettings_.clipping = clipMode == 1 ? "fast" : "precise";

    ImGui::DragFloat("Animation LOD distance", &prefSettings_.animLodDistance,
                     0.5f, 0.0f, 2000.0f,
                     prefSettings_.animLodDistance > 0.0f ? "%.0f units" : "off");
    if (prefSettings_.animLodDistance < 0.0f) prefSettings_.animLodDistance = 0.0f;
    ImGui::TextDisabled(
        "Animated models farther than this refresh their pose every 2nd frame\n"
        "(every 4th beyond twice the distance). Playback time is unaffected.\n"
        "Cuts the per-instance EE cost of distant animated crowds.");

    ImGui::DragFloat("Mesh LOD distance", &prefSettings_.meshLodDistance,
                     0.5f, 0.0f, 2000.0f,
                     prefSettings_.meshLodDistance > 0.0f ? "%.0f units" : "off");
    if (prefSettings_.meshLodDistance < 0.0f) prefSettings_.meshLodDistance = 0.0f;
    ImGui::TextDisabled(
        "The build bakes ~50%% and ~25%%-vertex variants of animated models;\n"
        "instances farther than this render the reduced meshes. Costs RAM\n"
        "and .tskl size; the editor viewport always shows the full mesh.");

    // Texture quantization - the PS2-native "compression" (palettized
    // PSMT8/PSMT4 textures). Applied at build time into .res-baked; per
    // model/material overrides live in the Assets section.
    int quantMode = prefSettings_.textureQuant == "none" ? 0
                    : prefSettings_.textureQuant == "8bit" ? 1
                                                           : 2;
    const char* quantNames[] = {
        "Full color (32-bit - heavy on the 4 MB VRAM)",
        "256 colors (8-bit palette)",
        "16 colors (4-bit palette - the PS2-era default)"};
    if (ImGui::Combo("Textures", &quantMode, quantNames, 3))
        prefSettings_.textureQuant =
            quantMode == 0 ? "none" : quantMode == 1 ? "8bit" : "4bit";
    ImGui::TextDisabled(
        "Quantized at build (sources in res/ stay untouched). Override per\n"
        "model/material in the Assets section - e.g. keep the hero's textures\n"
        "full color while everything else goes 4-bit.");

    drawTerrainMaterialCombo("Terrain material", prefSettings_.terrainMaterial);
    ImGui::TextDisabled("The material's color tints the terrain; its texture (map_Kd),\n"
                        "if any, tiles across it - set the tiling on the material's\n"
                        "texture in the Material Editor. Import .mtl in the Assets section.");

    ImGui::SeparatorText("AI navigation");
    ImGui::DragFloat("Nav cell size", &prefSettings_.navCellSize, 0.05f, 0.25f,
                     16.0f, "%.2f units");
    if (prefSettings_.navCellSize < 0.25f) prefSettings_.navCellSize = 0.25f;
    ImGui::DragFloat("Max walkable slope", &prefSettings_.navMaxSlope, 0.5f,
                     1.0f, 89.0f, "%.0f deg");
    prefSettings_.navMaxSlope = prefSettings_.navMaxSlope < 1.0f ? 1.0f
                                : prefSettings_.navMaxSlope > 89.0f
                                    ? 89.0f
                                    : prefSettings_.navMaxSlope;
    ImGui::DragFloat("Agent radius", &prefSettings_.navAgentRadius, 0.05f, 0.0f,
                     4.0f, "%.2f units");
    if (prefSettings_.navAgentRadius < 0.0f) prefSettings_.navAgentRadius = 0.0f;
    ImGui::TextDisabled(
        "The NPC nav grid, baked at build time from the terrain slope and\n"
        "blocking objects (grid capped at 128x128 cells - big maps get\n"
        "bigger cells). Agent radius widens every obstacle so NPCs keep\n"
        "their distance from walls. Used by the AI flow nodes (Patrol /\n"
        "Chase / Flee); preview with View > Nav Mesh Overlay. Scenes whose\n"
        "graphs use no AI nodes carry no nav data at all.");

    ImGui::SeparatorText("Post effects");
    ImGui::TextDisabled(
        "Bloom and film grain moved to Tools > UI Editor, where their\n"
        "on-screen layer is also set (e.g. bloom under the HUD, so it\n"
        "does not blur the crosshair or text).");

    ImGui::SeparatorText("Ambience (sky, lighting, fog)");
    ImGui::TextDisabled(
        "Sky gradient, baked lighting and distance fog now live in presets.\n"
        "Author them in Tools > Ambience Editor; each scene picks a preset in\n"
        "Scene > Preferences (or uses the default).");
    if (ImGui::Button("Open Ambience Editor")) showAmbienceEditor_ = true;

    ImGui::SeparatorText("Scenes");
    ImGui::Checkbox("Loading screen between scenes", &prefSettings_.loadingScreen);
    ImGui::TextDisabled(
        "Master switch: shows a loading screen while a scene loads (also at\n"
        "boot). Design them in Tools > Loading Screens - each scene picks one\n"
        "(Scene > Preferences), or a project default is used; with none, a\n"
        "built-in loading.png on black is shown.");
    if (ImGui::Button("Open Loading Screens editor")) {
        showLoadingEditor_ = true;
        ImGui::CloseCurrentPopup();
    }

    ImGui::SeparatorText("Usable objects");
    ImGui::Checkbox("Highlight usable objects", &prefSettings_.highlightUsable);
    if (prefSettings_.highlightUsable) {
        ImGui::DragFloat("Proximity (units)", &prefSettings_.highlightDistance, 0.1f,
                         0.5f, 1000.0f, "%.1f");
        ImGui::ColorEdit3("Highlight color", prefSettings_.highlightColor);
        ImGui::DragFloat("Blur width (units)", &prefSettings_.highlightWidth, 0.01f,
                         0.05f, 2.0f, "%.2f");
        ImGui::SliderInt("Blur steps", &prefSettings_.highlightSteps, 1, 8);
        ImGui::SliderFloat("Opacity", &prefSettings_.highlightOpacity, 0.0f, 1.0f,
                           "%.2f");
        ImGui::Checkbox("Draw over object (experimental)",
                        &prefSettings_.highlightOverlay);
        ImGui::TextDisabled(
            "Width = total rim size; steps = shells in the fade (1 = sharp edge).\n"
            "Opacity = transparency of the strongest shell (the rest fade from it).\n"
            "Draw over object = a colored glow ON the surface fading outward,\n"
            "instead of only a rim behind the silhouette.");
    }
    ImGui::TextDisabled(
        "In-game outline around objects marked 'Usable' while the player is\n"
        "within the proximity distance. The viewport marks them with a wire box.");

    if (prefTemplate_ == 1) {
        ImGui::SeparatorText("FPP camera");
        ImGui::DragFloat("Eye height", &prefSettings_.eyeHeight, 0.05f, 0.2f, 50.0f, "%.2f");
        ImGui::DragFloat("Walk speed", &prefSettings_.walkSpeed, 0.02f, 0.05f, 10.0f, "%.2f");
        ImGui::DragFloat("Look speed", &prefSettings_.lookSpeed, 0.05f, 0.1f, 5.0f, "%.2f");
    } else {
        ImGui::SeparatorText("Orbit camera");
        ImGui::DragFloat("Orbit speed", &prefSettings_.orbitSpeed, 0.05f, 0.0f, 10.0f, "%.2f");
    }

    ImGui::SeparatorText("Input");
    ImGui::SliderFloat("Left stick deadzone", &prefSettings_.stickDeadzoneL, 0.0f, 0.9f,
                       "%.2f");
    ImGui::SliderFloat("Right stick deadzone", &prefSettings_.stickDeadzoneR, 0.0f, 0.9f,
                       "%.2f");
    ImGui::TextDisabled(
        "Analog stick offsets below this fraction read as zero. Raise it when\n"
        "a worn pad drifts (left = movement, right = camera); motion above the\n"
        "deadzone ramps smoothly, so higher values only cost range, not control.");

    // Response curve per stick, applied after the deadzone: Linear passes the
    // rescaled magnitude through; Exponential/S-Curve reshape it (the exponent
    // tunes how much). The plot previews deflection (x) -> output (y). These
    // are project-wide defaults; a flow graph can change them live with the
    // Set Stick Curve node.
    const char* curveNames[] = {"Linear", "Exponential", "S-Curve"};
    auto stickCurveUi = [&](const char* label, int& curve, float& exponent) {
        ImGui::PushID(label);
        if (curve < 0 || curve > 2) curve = 0;
        ImGui::Combo(label, &curve, curveNames, 3);
        if (curve != 0) {
            ImGui::SliderFloat("Exponent", &exponent, 1.0f, 6.0f, "%.2f");
            if (exponent < 1.0f) exponent = 1.0f;
        }
        float samples[48];
        for (int i = 0; i < 48; ++i) {
            float m = i / 47.0f;
            if (curve == 1) {
                m = powf(m, exponent);
            } else if (curve == 2) {
                const float s = m * m * (3.0f - 2.0f * m);
                m = exponent == 1.0f ? s : powf(s, exponent);
            }
            samples[i] = m;
        }
        ImGui::PlotLines("##curvePreview", samples, 48, 0, nullptr, 0.0f, 1.0f,
                         ImVec2(0, scaled(46)));
        ImGui::PopID();
    };
    stickCurveUi("Left stick curve", prefSettings_.stickCurveL, prefSettings_.stickExpL);
    stickCurveUi("Right stick curve", prefSettings_.stickCurveR, prefSettings_.stickExpR);
    ImGui::TextDisabled(
        "Exponential is gentle near center and snappy at the edge (aiming);\n"
        "S-Curve eases in and out. Higher exponent = more pronounced.");

    ImGui::SeparatorText("Physics");
    ImGui::DragFloat("Gravity (units/s^2)", &prefSettings_.gravity, 0.1f, 0.0f, 100.0f,
                     "%.1f");
    if (prefTemplate_ == 1)
        ImGui::DragFloat("Jump speed (units/s)", &prefSettings_.jumpSpeed, 0.1f, 0.0f, 50.0f,
                         "%.1f");
    ImGui::TextDisabled("Objects with the 'Physics' flag fall; the FPP player jumps with X.");

    ImGui::Separator();
    ImGui::TextDisabled(
        "These are project-wide defaults. Scenes inherit them unless a\n"
        "category is overridden in Scene > Scene Preferences. The emulator\n"
        "path and dev-PS2 IP are machine-global - set them in Edit > Preferences.");
    if (ImGui::Button("OK", ImVec2(scaled(120), 0))) {
        project_.gameTemplate = prefTemplate_ == 1 ? "fpp" : "orbit";
        project_.settings = prefSettings_;
        project_.active().terrain = prefTerrain_;
        applyProjectToViewport();  // scenes that inherit follow the new defaults
        commitChange();
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(scaled(120), 0))) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

// Machine-global editor settings (Edit > Preferences), stored in editor.ini and
// shared by every project on this PC: the emulator path and the dev-PS2 IP.
// Applied on Save - persisted to the global config and pushed into the open
// project so the Runner (which reads project_) picks them up immediately.
void App::drawEditorPreferencesModal() {
    if (openEditorPrefsPopup_) {
        ImGui::OpenPopup("Editor Preferences");
        openEditorPrefsPopup_ = false;
    }

    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(scaled(560), 0), ImGuiCond_Appearing);

    if (!ImGui::BeginPopupModal("Editor Preferences", nullptr,
                                ImGuiWindowFlags_AlwaysAutoResize))
        return;

    ImGui::TextDisabled(
        "Settings for this editor installation - shared by every project and\n"
        "stored outside the .tyra file (in editor.ini under %%LOCALAPPDATA%%).");

    ImGui::SeparatorText("New projects");
    ImGui::InputText("Default folder", prefDefaultProjectsDir_,
                     sizeof(prefDefaultProjectsDir_));
    ImGui::SameLine();
    if (ImGui::SmallButton("Browse...##projdir")) {
        const std::string dir = pickFolder();
        if (!dir.empty())
            snprintf(prefDefaultProjectsDir_, sizeof(prefDefaultProjectsDir_), "%s",
                     dir.c_str());
    }
    if (prefDefaultProjectsDir_[0] != '\0') {
        ImGui::SameLine();
        if (ImGui::SmallButton("Clear##projdir")) prefDefaultProjectsDir_[0] = '\0';
    }
    ImGui::TextDisabled(
        "Parent folder proposed as the location when you create a new project\n"
        "(File > New Project). Leave empty to default to ~/TyraProjects.");

    ImGui::SeparatorText("Emulator");
    ImGui::InputText("PCSX2 path", prefEmulatorPath_, sizeof(prefEmulatorPath_));
    ImGui::SameLine();
    if (ImGui::SmallButton("Browse...##pcsx2")) {
        const std::string exe = pickExeFile();
        if (!exe.empty())
            snprintf(prefEmulatorPath_, sizeof(prefEmulatorPath_), "%s", exe.c_str());
    }
    if (prefEmulatorPath_[0] != '\0') {
        ImGui::SameLine();
        if (ImGui::SmallButton("Clear##pcsx2")) prefEmulatorPath_[0] = '\0';
    }
    ImGui::TextDisabled(
        "Path to pcsx2-qt.exe used by Build && Run. Leave empty to auto-detect\n"
        "under Program Files. The emulator's log appears in the Debug window.");

    ImGui::SeparatorText("Real PS2 (network deploy)");
    ImGui::InputText("PS2 (ps2link) IP", prefPs2Ip_, sizeof(prefPs2Ip_));
    ImGui::TextDisabled(
        "IP of a PS2 on the LAN running PS2LINK.ELF. Enables Build > Build &&\n"
        "Run on PS2 (F6): the game boots on the console over ethernet with its\n"
        "assets served from this PC - no ISO, no SMB. Leave empty to disable.");

    ImGui::Separator();
    if (ImGui::Button("Save", ImVec2(scaled(120), 0))) {
        globalEmulatorPath_ = prefEmulatorPath_;
        globalPs2Ip_ = prefPs2Ip_;
        globalDefaultProjectsDir_ = prefDefaultProjectsDir_;
        saveGlobalConfig();
        // Feed the new values into the open project (the Runner's transport).
        if (hasProject_) {
            project_.emulatorPath = globalEmulatorPath_;
            project_.ps2LinkIp = globalPs2Ip_;
        }
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(scaled(120), 0))) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

void App::drawNavigationModal() {
    if (openNavigationPopup_) {
        ImGui::OpenPopup("Navigation controls");
        openNavigationPopup_ = false;
    }

    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(scaled(520), 0), ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal("Navigation controls", nullptr,
                                ImGuiWindowFlags_AlwaysAutoResize))
        return;

    // Global editor config (not project data): every change is persisted to
    // editor.ini immediately, so there is no OK/Cancel staging.
    bool changed = false;

    ImGui::SeparatorText("Mouse scheme");
    int scheme = (int)nav_.scheme;
    const char* schemeNames[] = {"Tyra (default)", "Blender", "Maya", "Unity"};
    if (ImGui::Combo("Scheme", &scheme, schemeNames, 4)) {
        nav_.scheme = (NavScheme)scheme;
        changed = true;
    }
    const char* schemeHelp[] = {
        "Left or right drag orbits, middle drag pans, wheel zooms.",
        "Middle drag orbits, Shift+middle pans, wheel zooms.\n"
        "Left mouse stays free for selection.",
        "Alt+left orbits, Alt+middle pans, Alt+right dollies, wheel zooms.",
        "Right drag orbits, middle drag pans, wheel zooms.\n"
        "Left mouse stays free for selection.",
    };
    ImGui::TextDisabled("%s", schemeHelp[scheme]);

    ImGui::SeparatorText("Movement keys");
    int keys = (int)nav_.moveKeys;
    const char* keyNames[] = {"WASD", "Arrow keys"};
    if (ImGui::Combo("Fly keys", &keys, keyNames, 2)) {
        nav_.moveKeys = (NavMoveKeys)keys;
        changed = true;
    }
    ImGui::TextDisabled("Move the camera across the scene. Tool shortcuts stay on 1-5.");

    ImGui::SeparatorText("Sensitivity");
    changed |= ImGui::SliderFloat("Orbit", &nav_.orbitSensitivity, 0.2f, 3.0f, "%.2fx");
    changed |= ImGui::SliderFloat("Pan", &nav_.panSensitivity, 0.2f, 3.0f, "%.2fx");
    changed |= ImGui::SliderFloat("Zoom", &nav_.zoomSensitivity, 0.2f, 3.0f, "%.2fx");
    changed |= ImGui::Checkbox("Invert horizontal", &nav_.invertX);
    ImGui::SameLine();
    changed |= ImGui::Checkbox("Invert vertical", &nav_.invertY);

    ImGui::SeparatorText("Focus");
    changed |= ImGui::Checkbox("Orbit around selected object", &nav_.orbitAroundSelection);
    ImGui::TextDisabled(
        "When on, selecting an object recenters the camera pivot on it,\n"
        "regardless of the transform gizmo mode. Pan/fly still move freely.");

    if (changed) {
        saveGlobalConfig();
        // Re-snap the pivot on the next frame if a selection is already active.
        navFocusedIndex_ = -1;
    }

    ImGui::Separator();
    if (ImGui::Button("Restore defaults", ImVec2(scaled(140), 0))) {
        nav_ = NavConfig{};
        saveGlobalConfig();
        navFocusedIndex_ = -1;
    }
    ImGui::SameLine();
    if (ImGui::Button("Close", ImVec2(scaled(120), 0))) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

void App::openScenePreferences() {
    if (!hasProject_) return;
    scenePrefScene_ = project_.activeScene;
    // Stage the scene's *resolved* settings: overridden categories show the
    // scene's own values, inherited categories show the project defaults - so
    // a grayed-out category previews exactly what the scene inherits, and
    // ticking its override starts editing from that value with no jump.
    scenePrefSettings_ = project::resolvedSettings(project_, project_.active());
    scenePrefOverrides_ = project_.active().overrides;
    scenePrefAmbience_ = project_.active().ambiencePreset;
    scenePrefLoading_ = project_.active().loadingScreen;
    openScenePrefsPopup_ = true;
}

void App::drawScenePreferencesModal() {
    if (openScenePrefsPopup_) {
        ImGui::OpenPopup("Scene Preferences");
        openScenePrefsPopup_ = false;
    }

    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(scaled(560), 0), ImGuiCond_Appearing);

    if (!ImGui::BeginPopupModal("Scene Preferences", nullptr,
                                ImGuiWindowFlags_AlwaysAutoResize))
        return;
    if (scenePrefScene_ < 0 || scenePrefScene_ >= (int)project_.scenes.size()) {
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }

    ProjectSettings& s = scenePrefSettings_;
    SceneOverrides& ov = scenePrefOverrides_;
    ImGui::Text("Scene: %s", project_.scenes[scenePrefScene_].name.c_str());
    ImGui::TextDisabled(
        "Each category inherits Project > Preferences until you tick\n"
        "\"Override project settings\" for it.");

    // One category: a header, an override toggle, then its widgets disabled
    // (grayed, previewing the inherited value) until the toggle is on.
    auto category = [&](const char* title, bool& flag, auto widgets) {
        ImGui::SeparatorText(title);
        ImGui::PushID(title);
        ImGui::Checkbox("Override project settings", &flag);
        ImGui::BeginDisabled(!flag);
        widgets();
        ImGui::EndDisabled();
        ImGui::PopID();
    };

    // Ambience (sky + lighting + fog) comes from a preset, not per-scene
    // overrides. Empty = the project default preset.
    ImGui::SeparatorText("Ambience (sky, lighting, fog)");
    {
        const char* cur = scenePrefAmbience_.empty()
                              ? (project_.defaultAmbience >= 0 ? "<default>" : "<none>")
                              : scenePrefAmbience_.c_str();
        if (ImGui::BeginCombo("Preset", cur)) {
            const char* dflt = project_.defaultAmbience >= 0 &&
                                       project_.defaultAmbience <
                                           (int)project_.ambiencePresets.size()
                                   ? project_.ambiencePresets[project_.defaultAmbience]
                                         .name.c_str()
                                   : nullptr;
            std::string dfltLabel =
                dflt ? std::string("<default> (") + dflt + ")" : "<default>";
            if (ImGui::Selectable(dfltLabel.c_str(), scenePrefAmbience_.empty()))
                scenePrefAmbience_.clear();
            for (const AmbiencePreset& ap : project_.ambiencePresets)
                if (ImGui::Selectable(ap.name.c_str(), ap.name == scenePrefAmbience_))
                    scenePrefAmbience_ = ap.name;
            ImGui::EndCombo();
        }
        if (project_.ambiencePresets.empty())
            ImGui::TextDisabled("Add presets in Tools > Ambience Editor.");
        else
            ImGui::TextDisabled("Author presets in Tools > Ambience Editor.");
    }

    // Loading screen shown while this scene loads. Empty = the project default.
    ImGui::SeparatorText("Loading screen");
    {
        const char* cur =
            scenePrefLoading_.empty()
                ? (project_.defaultLoadingScreen >= 0 ? "<default>" : "<built-in>")
                : scenePrefLoading_.c_str();
        if (ImGui::BeginCombo("Screen", cur)) {
            const char* dflt =
                project_.defaultLoadingScreen >= 0 &&
                        project_.defaultLoadingScreen <
                            (int)project_.loadingScreens.size()
                    ? project_.loadingScreens[project_.defaultLoadingScreen]
                          .name.c_str()
                    : nullptr;
            std::string dfltLabel =
                dflt ? std::string("<default> (") + dflt + ")" : "<default>";
            if (ImGui::Selectable(dfltLabel.c_str(), scenePrefLoading_.empty()))
                scenePrefLoading_.clear();
            for (const LoadingScreenDef& ld : project_.loadingScreens)
                if (ImGui::Selectable(ld.name.c_str(), ld.name == scenePrefLoading_))
                    scenePrefLoading_ = ld.name;
            ImGui::EndCombo();
        }
        ImGui::TextDisabled("Author screens in Tools > Loading Screens.");
    }

    category("Clipping", ov.clipping, [&] {
        int clipMode = s.clipping == "fast" ? 1 : 0;
        const char* clipNames[] = {
            "Precise clipping (no holes at screen edges, costs EE time)",
            "Fast culling (fastest; big near triangles may vanish)"};
        if (ImGui::Combo("Triangles", &clipMode, clipNames, 2))
            s.clipping = clipMode == 1 ? "fast" : "precise";
    });

    category("Terrain material", ov.terrainMat, [&] {
        drawTerrainMaterialCombo("Material", s.terrainMaterial);
    });

    category("Post effects", ov.postFx, [&] {
        ImGui::SliderFloat("Bloom", &s.bloom, 0.0f, 1.0f, "%.2f");
        ImGui::SliderFloat("Film grain", &s.grain, 0.0f, 1.0f, "%.2f");
        ImGui::SliderFloat("DoF amount", &s.dofAmount, 0.0f, 1.0f, "%.2f");
        ImGui::DragFloat("DoF focus", &s.dofFocus, 0.5f, 0.5f, 500.0f, "%.1f");
        ImGui::DragFloat("DoF range", &s.dofRange, 0.5f, 0.1f, 500.0f, "%.1f");
    });

    category("Usable objects", ov.highlight, [&] {
        ImGui::Checkbox("Highlight usable objects", &s.highlightUsable);
        ImGui::DragFloat("Proximity (units)", &s.highlightDistance, 0.1f, 0.5f, 1000.0f, "%.1f");
        ImGui::ColorEdit3("Highlight color", s.highlightColor);
        ImGui::DragFloat("Blur width (units)", &s.highlightWidth, 0.01f, 0.05f, 2.0f, "%.2f");
        ImGui::SliderInt("Blur steps", &s.highlightSteps, 1, 8);
        ImGui::SliderFloat("Opacity", &s.highlightOpacity, 0.0f, 1.0f, "%.2f");
        ImGui::Checkbox("Draw over object (experimental)", &s.highlightOverlay);
    });

    ImGui::Separator();
    if (ImGui::Button("OK", ImVec2(scaled(120), 0))) {
        SceneData& sc = project_.scenes[scenePrefScene_];
        sc.settings = scenePrefSettings_;
        sc.overrides = scenePrefOverrides_;
        sc.ambiencePreset = scenePrefAmbience_;
        sc.loadingScreen = scenePrefLoading_;
        applyProjectToViewport();
        commitChange();
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(scaled(120), 0))) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

void App::openProjectDialog() {
    // Projects are opened through their project file (<name>.tyra).
    std::string solutionFile = pickSolutionFile();
    if (solutionFile.empty()) return;
    const std::string dir = std::filesystem::path(solutionFile).parent_path().string();
    Project p;
    std::string err = project::load(p, dir);
    if (err.empty()) {
        project_ = p;
        hasProject_ = true;
        applyProjectToViewport();
        attachProject();  // resets dirty + window title
    } else {
        runner_.clearLog();
        // Surface the error in the Output window via the runner log is hacky;
        // show a popup instead on next frame. Simple approach: message box.
        MessageBoxA(nullptr, err.c_str(), "Open Project", MB_ICONERROR | MB_OK);
    }
}
