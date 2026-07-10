#include "app.hpp"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstring>

#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>

#include "gl_loader.h"
#include "glbparser.hpp"
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
enum class PickKind { Folder, Solution, ObjModel, Mtl, Png, Wav, Ttf, Executable };

// Owner window for the native dialogs. An unowned modal (Show(nullptr))
// leaves the frozen GLFW window active behind it - Windows then wedges the
// dialog when it interacts with the non-pumping app (grayed Open button).
static HWND g_dialogOwner = nullptr;

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
    window_ = glfwCreateWindow(1600, 900, "Tyra Editor", nullptr, nullptr);
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

    ImGui_ImplGlfw_InitForOpenGL(window_, true);
    ImGui_ImplOpenGL3_Init("#version 330 core");
    ImNodes::CreateContext();

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

    // Default location for new projects: the user's home dir
    if (const char* home = getenv("USERPROFILE"))
        std::snprintf(newLocation_, sizeof(newLocation_), "%s\\TyraProjects", home);

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
            attachProject();
            glfwSetWindowTitle(window_, ("Tyra Editor - " + project_.name).c_str());
        }
    }

    while (!glfwWindowShouldClose(window_)) {
        glfwPollEvents();

        // Deferred from attachProject(): the saved window layout can only be
        // (re)applied between frames - existing windows get re-docked here.
        if (layoutLoadPending_) {
            layoutLoadPending_ = false;
            ImGui::LoadIniSettingsFromMemory(project_.windowLayout.c_str(),
                                             project_.windowLayout.size());
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        drawUI();

        // Window layout is part of the project: whenever ImGui settles a
        // layout change (docking, resizes), fold it into the .tyra file.
        if (hasProject_ && io.WantSaveIniSettings) saveProject();

        ImGui::Render();
        int w, h;
        glfwGetFramebufferSize(window_, &w, &h);
        glViewport(0, 0, w, h);
        glClearColor(0.08f, 0.08f, 0.10f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(window_);
    }

    // Flush any layout change that has not hit the save timer yet.
    if (hasProject_) saveProject();

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

    // Default layout on first run (no imgui.ini yet)
    static bool layoutDone = false;
    if (!layoutDone) {
        layoutDone = true;
        if (ImGui::DockBuilderGetNode(dockspace) == nullptr ||
            ImGui::DockBuilderGetNode(dockspace)->IsLeafNode()) {
            ImGui::DockBuilderRemoveNode(dockspace);
            ImGui::DockBuilderAddNode(dockspace, ImGuiDockNodeFlags_DockSpace);
            ImGui::DockBuilderSetNodeSize(dockspace, ImGui::GetMainViewport()->Size);

            ImGuiID center = dockspace;
            ImGuiID left = ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, 0.24f, nullptr,
                                                       &center);
            ImGuiID leftBottom = ImGui::DockBuilderSplitNode(left, ImGuiDir_Down, 0.5f,
                                                             nullptr, &left);
            ImGuiID bottom = ImGui::DockBuilderSplitNode(center, ImGuiDir_Down, 0.26f, nullptr,
                                                         &center);
            ImGui::DockBuilderDockWindow("Project", left);
            ImGui::DockBuilderDockWindow("Properties", leftBottom);
            ImGui::DockBuilderDockWindow("Output", bottom);
            ImGui::DockBuilderDockWindow("Debug", bottom);
            ImGui::DockBuilderDockWindow("Flow Graph", center);
            ImGui::DockBuilderDockWindow("Viewport", center);
            ImGui::DockBuilderFinish(dockspace);
        }
    }

    // Layouts saved before the Properties window existed: carve a slot for it
    // under the Project panel once that panel has settled into its dock node.
    if (dockPropertiesPending_) {
        if (ImGuiWindow* proj = ImGui::FindWindowByName("Project")) {
            if (proj->DockId != 0 && ImGui::DockBuilderGetNode(proj->DockId)) {
                ImGuiID top = proj->DockId;
                ImGuiID slot = ImGui::DockBuilderSplitNode(top, ImGuiDir_Down, 0.5f,
                                                           nullptr, &top);
                ImGui::DockBuilderDockWindow("Properties", slot);
                ImGui::DockBuilderFinish(dockspace);
            }
            dockPropertiesPending_ = false;  // Project floating -> Properties floats too
        }
    }

    drawMenuBar();
    drawViewportWindow();
    drawProjectWindow();
    drawPropertiesWindow();
    drawFlowGraphWindow();
    drawOutputWindow();
    drawDebugWindow();
    drawDiscLayoutWindow();
    drawMenusWindow();
    drawNewProjectModal();
    drawPreferencesModal();
    drawScenePreferencesModal();
    drawNewScriptModal();
    drawNewSceneModal();
    drawDeleteSceneModal();

    // Keyboard shortcuts
    ImGuiIO& io = ImGui::GetIO();
    if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_N)) openNewProjectPopup_ = true;
    if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_O)) openProjectDialog();
    if (hasProject_ && !runner_.busy() && ImGui::IsKeyPressed(ImGuiKey_F5))
        runner_.buildAndRun(project_, true);
    if (hasProject_ && !runner_.busy() && !project_.ps2LinkIp.empty() &&
        ImGui::IsKeyPressed(ImGuiKey_F6))
        runner_.buildAndRunPs2(project_, true);
    if (hasProject_ && !runner_.busy() &&
        ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_B))
        runner_.buildAndRun(project_, false);
    if (hasProject_) {
        if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_S)) saveAll("Saved");
        if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Comma)) {
            prefTerrain_ = project_.active().terrain;
            prefTemplate_ = project_.gameTemplate == "fpp" ? 1 : 0;
            prefSettings_ = project_.settings;
            snprintf(prefEmulatorPath_, sizeof(prefEmulatorPath_), "%s",
                     project_.emulatorPath.c_str());
            snprintf(prefPs2Ip_, sizeof(prefPs2Ip_), "%s", project_.ps2LinkIp.c_str());
            openPreferencesPopup_ = true;
        }
        if (!io.WantTextInput) {
            if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Z)) undo();
            if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Y)) redo();
            if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_C)) copyObject();
            if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_V)) pasteObject();
        }
    }
}

void App::drawMenuBar() {
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("New Project...", "Ctrl+N")) openNewProjectPopup_ = true;
            if (ImGui::MenuItem("Open Project...", "Ctrl+O")) openProjectDialog();
            if (ImGui::MenuItem("Save", "Ctrl+S", false, hasProject_)) saveAll("Saved");
            ImGui::Separator();
            if (ImGui::MenuItem("Exit")) glfwSetWindowShouldClose(window_, GLFW_TRUE);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Edit", hasProject_)) {
            const bool objectSelected =
                selectedObject_ >= 0 && selectedObject_ < (int)project_.objects().size();
            if (ImGui::MenuItem("Undo", "Ctrl+Z", false, history_.canUndo())) undo();
            if (ImGui::MenuItem("Redo", "Ctrl+Y", false, history_.canRedo())) redo();
            ImGui::Separator();
            if (ImGui::MenuItem("Copy object", "Ctrl+C", false, objectSelected)) copyObject();
            if (ImGui::MenuItem("Paste object", "Ctrl+V", false, hasClipboard_)) pasteObject();
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
                snprintf(prefEmulatorPath_, sizeof(prefEmulatorPath_), "%s",
                         project_.emulatorPath.c_str());
                snprintf(prefPs2Ip_, sizeof(prefPs2Ip_), "%s", project_.ps2LinkIp.c_str());
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
            if (ImGui::MenuItem("Run in PCSX2 (no build)", nullptr, false, !busy))
                runner_.runEmulatorOnly(project_);
            ImGui::Separator();
            const bool ps2Ready = !project_.ps2LinkIp.empty();
            if (ImGui::MenuItem("Build && Run on PS2", "F6", false, !busy && ps2Ready))
                runner_.buildAndRunPs2(project_, true);
            if (ImGui::MenuItem("Run on PS2 (no build)", nullptr, false, !busy && ps2Ready))
                runner_.buildAndRunPs2(project_, false);
            if (!ps2Ready && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                ImGui::SetTooltip("Set 'PS2 (ps2link) IP' in Project > Preferences first.");
            ImGui::Separator();
            if (ImGui::MenuItem("Clean", nullptr, false, !busy))
                runner_.clean(project_);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                ImGui::SetTooltip("Deletes bin\\ and the container build cache "
                                  "(obj) - the next build starts from scratch.");
            ImGui::EndMenu();
        }

        if (hasProject_ && ImGui::BeginMenu("Tools")) {
            if (ImGui::MenuItem("Menu Editor...")) showMenusEditor_ = true;
            ImGui::EndMenu();
        }

        if (!statusMessage_.empty()) {
            const float w = ImGui::CalcTextSize(statusMessage_.c_str()).x;
            ImGui::SameLine(ImGui::GetWindowWidth() - w - 16.0f);
            ImGui::TextDisabled("%s", statusMessage_.c_str());
        }
        ImGui::EndMainMenuBar();
    }
}

void App::drawViewportWindow() {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::Begin("Viewport");
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
        uint32_t tex =
            viewport_.render((int)avail.x, (int)avail.y, project_.objects(), selectedObject_);
        // Flip vertically: GL texture origin is bottom-left
        ImGui::Image((ImTextureID)(intptr_t)tex, avail, ImVec2(0, 1), ImVec2(1, 0));

        const ImVec2 imgPos = ImGui::GetItemRectMin();
        const bool imageHovered = ImGui::IsItemHovered();
        ImGuiIO& io = ImGui::GetIO();

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
                applyProjectToViewport();  // live mesh rebuild
                sculptStroke_ = true;
            }
        }
        if (sculptStroke_ && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            sculptStroke_ = false;
            project::saveHeights(project_);
            commitChange();  // one undo step per finished brush stroke
            statusMessage_ = "Terrain saved";
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

        // --- Transform gizmo on the selected object (disabled while sculpting) ---
        bool objectSelected = !sculptMode_ && selectedObject_ >= 0 &&
                              selectedObject_ < (int)project_.objects().size();
        if (objectSelected) {
            SceneObject& o = project_.objects()[selectedObject_];

            ImGuizmo::SetOrthographic(false);
            ImGuizmo::SetDrawlist();
            ImGuizmo::SetRect(imgPos.x, imgPos.y, avail.x, avail.y);

            const ImGuizmo::OPERATION ops[] = {ImGuizmo::TRANSLATE, ImGuizmo::ROTATE,
                                               ImGuizmo::SCALE};
            const ImGuizmo::OPERATION op = ops[gizmoOp_];
            const ImGuizmo::MODE mode =
                op == ImGuizmo::TRANSLATE ? ImGuizmo::WORLD : ImGuizmo::LOCAL;

            // Hold Ctrl to snap: 0.5 units / 15 degrees / 0.25 scale
            const float snapValues[3] = {op == ImGuizmo::ROTATE ? 15.0f
                                         : op == ImGuizmo::SCALE ? 0.25f
                                                                 : 0.5f,
                                         op == ImGuizmo::TRANSLATE ? 0.5f : 0.0f,
                                         op == ImGuizmo::TRANSLATE ? 0.5f : 0.0f};
            const float* snap = io.KeyCtrl ? snapValues : nullptr;

            // Same TRS composition as the viewport / PS2 code
            float model[16];
            ImGuizmo::RecomposeMatrixFromComponents(o.position, o.rotation, o.scale, model);
            if (ImGuizmo::Manipulate(viewport_.viewMatrix(), viewport_.projMatrix(), op, mode,
                                     model, nullptr, snap)) {
                ImGuizmo::DecomposeMatrixToComponents(model, o.position, o.rotation, o.scale);
                for (float& s : o.scale)
                    if (s < 0.01f) s = 0.01f;
            }
        }

        // Commit once per completed gizmo drag (not every frame)
        const bool usingGizmo = ImGuizmo::IsUsing();
        if (gizmoWasUsing_ && !usingGizmo) commitChange();
        gizmoWasUsing_ = usingGizmo;

        const bool gizmoBusy = usingGizmo || (objectSelected && ImGuizmo::IsOver());

        // --- Camera + selection input ---
        if (imageHovered && !gizmoBusy) {
            // In sculpt mode LMB paints, so only RMB orbits
            if ((!sculptMode_ && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) ||
                ImGui::IsMouseDragging(ImGuiMouseButton_Right)) {
                viewport_.orbit(io.MouseDelta.x, io.MouseDelta.y);
            }
            // Middle mouse: pan the camera target in the view plane
            if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle))
                viewport_.pan(io.MouseDelta.x, io.MouseDelta.y);
            if (io.MouseWheel != 0.0f) viewport_.zoom(io.MouseWheel);

            // Click (no drag) = pick object under cursor
            if (!sculptMode_ && ImGui::IsMouseReleased(ImGuiMouseButton_Left) &&
                io.MouseDragMaxDistanceSqr[ImGuiMouseButton_Left] < 9.0f) {
                const float u = (io.MousePos.x - imgPos.x) / avail.x;
                const float v = (io.MousePos.y - imgPos.y) / avail.y;
                selectedObject_ = viewport_.pick(u, v, project_.objects());
            }
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
        // hidden by default - toggle in the HUD section) ---
        if (showHudInEditor_ && !project_.hud.empty()) {
            ImDrawList* dl = ImGui::GetWindowDrawList();
            for (int i = 0; i < (int)project_.hud.size(); ++i) {
                const HudImage& hi = project_.hud[i];
                const float w = hi.size[0] / 512.0f * frameSize.x;
                const float h = hi.size[1] / 448.0f * frameSize.y;
                const ImVec2 c(frameMin.x + hi.pos[0] * frameSize.x,
                               frameMin.y + hi.pos[1] * frameSize.y);
                const ImVec2 pMin(c.x - w * 0.5f, c.y - h * 0.5f);
                const ImVec2 pMax(c.x + w * 0.5f, c.y + h * 0.5f);
                if (const HudTexture* t = hudTexture(hi.imagePath))
                    dl->AddImage((ImTextureID)(intptr_t)t->tex, pMin, pMax);
                else
                    dl->AddRect(pMin, pMax, IM_COL32(255, 100, 100, 200));
                if (i == selectedHud_)
                    dl->AddRect(pMin, pMax, IM_COL32(255, 160, 30, 255), 0.0f, 0, 2.0f);
            }
        }

        // --- Tool buttons overlay (top-left corner of the viewport) ---
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

        // View mode switch (persisted in the .tyra project file via saveAll)
        ImGui::SameLine(0.0f, 24.0f);
        const char* modeNames[] = {"Solid", "Wire", "Wire+Solid"};
        for (int i = 0; i < 3; ++i) {
            if (i) ImGui::SameLine();
            const bool active = (int)viewport_.viewMode() == i;
            if (active)
                ImGui::PushStyleColor(ImGuiCol_Button,
                                      ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
            if (ImGui::SmallButton(modeNames[i]) && !active) {
                viewport_.setViewMode((Viewport::ViewMode)i);
                saveAll("Saved");  // persist the view mode in the project file
            }
            if (active) ImGui::PopStyleColor();
        }

        // Terrain sculpting toggle + brush parameters
        ImGui::SameLine(0.0f, 24.0f);
        if (sculptMode_)
            ImGui::PushStyleColor(ImGuiCol_Button,
                                  ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
        if (ImGui::SmallButton("Sculpt (4)")) sculptMode_ = !sculptMode_;
        if (sculptMode_) ImGui::PopStyleColor();

        // TV frame toggles (PAL 4:3, NTSC slightly wider)
        ImGui::SameLine(0.0f, 24.0f);
        if (showPal_)
            ImGui::PushStyleColor(ImGuiCol_Button,
                                  ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
        if (ImGui::SmallButton("PAL")) showPal_ = !showPal_;
        if (showPal_) ImGui::PopStyleColor();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("4:3 frame - what a PAL TV shows.");
        ImGui::SameLine();
        if (showNtsc_)
            ImGui::PushStyleColor(ImGuiCol_Button,
                                  ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
        if (ImGui::SmallButton("NTSC")) showNtsc_ = !showNtsc_;
        if (showNtsc_) ImGui::PopStyleColor();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("NTSC has fewer active lines - the same 512x448\n"
                              "buffer looks slightly wider (~10:7) on screen.");

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

            // WASD: fly the camera over the terrain (tools live on 1-4)
            const float fwd = (ImGui::IsKeyDown(ImGuiKey_W) ? 1.0f : 0.0f) -
                              (ImGui::IsKeyDown(ImGuiKey_S) ? 1.0f : 0.0f);
            const float strafe = (ImGui::IsKeyDown(ImGuiKey_D) ? 1.0f : 0.0f) -
                                 (ImGui::IsKeyDown(ImGuiKey_A) ? 1.0f : 0.0f);
            viewport_.fly(fwd, strafe, io.DeltaTime);
            if (objectSelected && ImGui::IsKeyPressed(ImGuiKey_Delete)) {
                project_.objects().erase(project_.objects().begin() + selectedObject_);
                selectedObject_ = -1;
                commitChange();
            }
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
                selectedObject_ = -1;
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
    drawAssetsSection();
    drawHudSection();
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
    project_.viewMode = (int)viewport_.viewMode();
    // While a layout load is pending, the on-screen layout still belongs to
    // the previously shown project - keep the stored one instead of clobbering
    // the freshly opened project's docking with it.
    if (!layoutLoadPending_)
        project_.windowLayout = ImGui::SaveIniSettingsToMemory();  // clears WantSave
    if (auto err = project::save(project_); !err.empty())
        MessageBoxA(nullptr, err.c_str(), "Save Project", MB_ICONERROR | MB_OK);
}

void App::saveAll(const char* status) {
    saveProject();
    if (auto err = project::saveHistory(project_, history_); !err.empty())
        MessageBoxA(nullptr, err.c_str(), "Save History", MB_ICONERROR | MB_OK);
    statusMessage_ = status;
}

void App::commitChange() {
    history_.push({project_.scenes});
    saveAll("Saved");
}

void App::applySnapshot(const SceneSnapshot& s) {
    project_.scenes = s.scenes;
    if (project_.activeScene >= (int)project_.scenes.size()) project_.activeScene = 0;
    if (selectedObject_ >= (int)project_.objects().size()) selectedObject_ = -1;
    flowPositionsApplied_ = false;  // graphs live in objects - re-pin node positions
    project::saveHeights(project_);  // snapshots carry heightmaps (sculpt undo)
    applyProjectToViewport();  // terrain/lighting/heights may differ per snapshot
}

void App::undo() {
    if (!history_.canUndo()) return;
    applySnapshot(history_.undo());
    saveAll("Undo");
}

void App::redo() {
    if (!history_.canRedo()) return;
    applySnapshot(history_.redo());
    saveAll("Redo");
}

void App::copyObject() {
    if (selectedObject_ < 0 || selectedObject_ >= (int)project_.objects().size()) return;
    clipboard_ = project_.objects()[selectedObject_];
    hasClipboard_ = true;
    statusMessage_ = "Copied " + clipboard_.name;
}

void App::pasteObject() {
    if (!hasClipboard_) return;

    SceneObject o = clipboard_;
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
    selectedObject_ = (int)project_.objects().size() - 1;
    commitChange();
    statusMessage_ = "Pasted " + project_.objects().back().name;
}

void App::attachProject() {
    flowGraphObject_ = -1;
    flowPositionsApplied_ = false;
    history_.reset({project_.scenes});
    // The history file restores the undo stack when it is in sync with the
    // project file; otherwise we start fresh (and write a new one).
    if (auto err = project::loadHistory(project_, history_); !err.empty()) {
        history_.reset({project_.scenes});
        project::saveHistory(project_, history_);
    }
    // Editor-side state + window layout came in with the .tyra project file.
    selectedObject_ = project_.selectedObject;
    if (selectedObject_ >= (int)project_.objects().size()) selectedObject_ = -1;
    gizmoOp_ = (project_.gizmoOp >= 0 && project_.gizmoOp <= 2) ? project_.gizmoOp : 0;
    const int viewMode =
        (project_.viewMode >= 0 && project_.viewMode <= 2) ? project_.viewMode : 0;
    viewport_.setViewMode((Viewport::ViewMode)viewMode);
    // Loading ImGui settings mid-frame is unsupported (the dock nodes rebuild
    // while this frame's windows still reference the old ones, scattering the
    // layout) - defer to the frame boundary in run().
    layoutLoadPending_ = !project_.windowLayout.empty();
    // Layouts saved before the Properties window existed: dock it under the
    // Project panel once the deferred load has settled (drawUI waits for the
    // Project window's dock node to be valid again).
    dockPropertiesPending_ =
        layoutLoadPending_ &&
        project_.windowLayout.find("[Window][Properties]") == std::string::npos;
    flowPositionsApplied_ = false;
    statusMessage_.clear();
    wavIssueCache_.clear();
    modelInfoCache_.clear();
    // Pick up assets dropped into res/ by hand while the project was closed.
    rescanAssets(false);
}

void App::addEmitter(int kind) {
    addObject(PrimitiveType::Emitter);
    SceneObject& o = project_.objects().back();
    o.emitterKind = kind;
    // preset tints (particles are untextured color quads)
    const float presets[4][3] = {{1.0f, 0.6f, 0.2f},   // fire
                                 {0.5f, 0.5f, 0.5f},   // smoke
                                 {0.8f, 0.85f, 0.9f},  // fog
                                 {1.0f, 0.9f, 0.4f}};  // sparks
    for (int i = 0; i < 3; ++i) o.color[i] = presets[kind][i];
    if (kind == 2) {  // fog: wide footprint, big lazy puffs
        o.scale[0] = o.scale[2] = 8.0f;
        o.emitterCount = 16;
        o.emitterSize = 1.2f;
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
    if (type == PrimitiveType::SpawnPoint) {
        o.position[1] = 0.0f;  // marker sits on the ground
        o.color[0] = 0.15f, o.color[1] = 0.9f, o.color[2] = 0.9f;
    }
    if (type == PrimitiveType::Player) {
        o.position[1] = 0.0f;  // marker stands on the ground
        o.color[0] = 0.95f, o.color[1] = 0.75f, o.color[2] = 0.2f;
    }
    project_.objects().push_back(o);
    selectedObject_ = (int)project_.objects().size() - 1;
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
    selectedObject_ = (int)project_.objects().size() - 1;
    commitChange();
}

// Categorized object palette, shared by the Scene menu and the "+ Add"
// button in the Project panel.
void App::drawAddObjectMenu() {
    if (ImGui::BeginMenu("Object")) {
        if (ImGui::BeginMenu("Simple")) {
            if (ImGui::MenuItem("Box")) addObject(PrimitiveType::Box);
            if (ImGui::MenuItem("Sphere")) addObject(PrimitiveType::Sphere);
            if (ImGui::MenuItem("Cylinder")) addObject(PrimitiveType::Cylinder);
            if (ImGui::MenuItem("Cone")) addObject(PrimitiveType::Cone);
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
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Gameplay")) {
        if (ImGui::MenuItem("Player")) addObject(PrimitiveType::Player);
        if (ImGui::MenuItem("Spawn point")) addObject(PrimitiveType::SpawnPoint);
        if (ImGui::MenuItem("Save point")) addSavePoint();
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Effects")) {
        if (ImGui::MenuItem("Fire")) addEmitter(0);
        if (ImGui::MenuItem("Smoke")) addEmitter(1);
        if (ImGui::MenuItem("Fog")) addEmitter(2);
        if (ImGui::MenuItem("Sparks")) addEmitter(3);
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
        ImGui::BeginChild("##objects", ImVec2(0, 130), ImGuiChildFlags_Borders);
        for (int i = 0; i < (int)project_.objects().size(); ++i) {
            const SceneObject& o = project_.objects()[i];
            std::string label = o.name + "  (" + primitiveTypeName(o.type) + ")##obj" +
                                std::to_string(i);
            if (ImGui::Selectable(label.c_str(), selectedObject_ == i)) selectedObject_ = i;
        }
        ImGui::EndChild();
    }

    if (selectedObject_ >= 0 && selectedObject_ < (int)project_.objects().size())
        ImGui::TextDisabled("Edit the selection in the Properties window.");
}

// Display names for the Type field (primitiveTypeName() is the serialized
// lowercase form, e.g. "spawn-point").
static const char* typeLabel(PrimitiveType t) {
    switch (t) {
        case PrimitiveType::Box: return "Box";
        case PrimitiveType::Sphere: return "Sphere";
        case PrimitiveType::Cylinder: return "Cylinder";
        case PrimitiveType::Cone: return "Cone";
        case PrimitiveType::SpawnPoint: return "Spawn point";
        case PrimitiveType::Model: return "3D model";
        case PrimitiveType::Player: return "Player";
        case PrimitiveType::Emitter: return "Particle emitter";
        case PrimitiveType::SoundEmitter: return "Sound emitter";
        case PrimitiveType::PointLight: return "Point light";
        case PrimitiveType::SavePoint: return "Save point";
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
    SceneObject& o = project_.objects()[selectedObject_];

    // Edits apply live; a history snapshot is committed once per finished
    // interaction (slider released, text field defocused...).
    bool committed = false;

    // Real geometry in the game: rendered, collidable, texturable.
    const bool isShape =
        o.type == PrimitiveType::Box || o.type == PrimitiveType::Sphere ||
        o.type == PrimitiveType::Cylinder || o.type == PrimitiveType::Cone;
    const bool isSolid =
        isShape || o.type == PrimitiveType::Model || o.type == PrimitiveType::SavePoint;

    char nameBuf[128];
    std::snprintf(nameBuf, sizeof(nameBuf), "%s", o.name.c_str());
    if (ImGui::InputText("Name", nameBuf, sizeof(nameBuf))) o.name = nameBuf;
    committed |= ImGui::IsItemDeactivatedAfterEdit();

    if (isShape) {
        int typeIdx = (int)o.type;
        const char* typeNames[] = {"Box", "Sphere", "Cylinder", "Cone"};
        if (ImGui::Combo("Type", &typeIdx, typeNames, 4)) {
            o.type = (PrimitiveType)typeIdx;
            committed = true;
        }
    } else {
        ImGui::Text("Type:");
        ImGui::SameLine();
        ImGui::TextUnformatted(typeLabel(o.type));
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

    ImGui::DragFloat3("Position", o.position, 0.1f);
    committed |= ImGui::IsItemDeactivatedAfterEdit();
    if (isSolid) {
        ImGui::DragFloat3("Rotation", o.rotation, 1.0f, -360.0f, 360.0f, "%.0f deg");
        committed |= ImGui::IsItemDeactivatedAfterEdit();
    }
    if (isSolid || o.type == PrimitiveType::Emitter) {
        ImGui::DragFloat3("Scale", o.scale, 0.05f, 0.01f, 1000.0f);
        committed |= ImGui::IsItemDeactivatedAfterEdit();
    }
    // Color: mesh tint for solids, particle tint for emitters, light color
    // for point lights. Markers draw in fixed editor colors.
    if (isSolid || o.type == PrimitiveType::Emitter ||
        o.type == PrimitiveType::PointLight) {
        ImGui::ColorEdit3("Color", o.color);
        committed |= ImGui::IsItemDeactivatedAfterEdit();
    }

    const bool animatedModel =
        o.type == PrimitiveType::Model && isAnimatedModelPath(o.modelPath);
    if (isSolid) {
        // Material (.mtl asset): primitives take the file's first material
        // (Kd + map_Kd on their UVs, modulated by the object color), models
        // use it as an override replacing their own libraries. Animated .glb
        // models carry their own materials - no override.
        if (!animatedModel && drawMaterialCombo(o)) committed = true;
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

        if (ImGui::Checkbox("Physics (falls with gravity)", &o.physics)) committed = true;
        if (o.type == PrimitiveType::SavePoint) {
            ImGui::TextDisabled("Always usable - USE opens the save menu.");
        } else if (ImGui::Checkbox("Usable (USE prompt + On Used trigger)", &o.usable)) {
            committed = true;
        }
    }
    // Show/Hide Object nodes toggle emitters/sounds too - their on/off state
    // is worth saving; lights are baked at build, markers have no game state.
    if (isSolid || o.type == PrimitiveType::Emitter ||
        o.type == PrimitiveType::SoundEmitter) {
        if (ImGui::Checkbox("Save state (position/color/visibility in saves)",
                            &o.saveState))
            committed = true;
    }

    // Player collision. Solid geometry only - markers/emitters never collide.
    if (isSolid) {
        if (animatedModel) {
            // mesh collision is a static-model feature; animated models
            // collide as their baked frame-0 AABB or not at all
            bool solid = o.collisionMode != 2;
            if (ImGui::Checkbox("Collision (blocks the player, frame-0 AABB)",
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

    if (o.type == PrimitiveType::Emitter) {
        ImGui::SeparatorText("Particle emitter");
        const char* kinds[] = {"Fire", "Smoke", "Fog", "Sparks"};
        if (ImGui::Combo("Effect", &o.emitterKind, kinds, 4)) committed = true;
        if (ImGui::DragInt("Particles", &o.emitterCount, 1.0f, 1, 128)) {}
        committed |= ImGui::IsItemDeactivatedAfterEdit();
        ImGui::DragFloat("Particle size", &o.emitterSize, 0.02f, 0.05f, 8.0f, "%.2f");
        committed |= ImGui::IsItemDeactivatedAfterEdit();
        ImGui::TextDisabled("Color tints the particles; scale X/Z = spawn area.\n"
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

    if (o.type == PrimitiveType::Player) {
        ImGui::SeparatorText("Player");
        const char* modes[] = {"Walk (FPP)", "Noclip (fly)"};
        if (ImGui::Combo("Mode", &o.playerMode, modes, 2)) committed = true;
        ImGui::DragFloat("Walk speed", &o.playerWalkSpeed, 0.02f, 0.05f, 10.0f, "%.2f");
        committed |= ImGui::IsItemDeactivatedAfterEdit();
        ImGui::DragFloat("Look speed", &o.playerLookSpeed, 0.05f, 0.1f, 5.0f, "%.2f");
        committed |= ImGui::IsItemDeactivatedAfterEdit();
        ImGui::DragFloat("Eye height", &o.playerEyeHeight, 0.05f, 0.2f, 50.0f, "%.2f");
        committed |= ImGui::IsItemDeactivatedAfterEdit();
        committed |= ImGui::Checkbox("Can jump (X)", &o.playerCanJump);
        if (o.playerCanJump) {
            ImGui::DragFloat("Jump speed", &o.playerJumpSpeed, 0.1f, 0.0f, 50.0f, "%.1f");
            committed |= ImGui::IsItemDeactivatedAfterEdit();
        }
        ImGui::TextDisabled("First player in the scene drives the camera in the game.");
        ImGui::TextDisabled("Noclip: X up, Square down. Walk: X jumps.");
    }

    ImGui::Separator();
    if (ImGui::Button("Delete object")) {
        project_.objects().erase(project_.objects().begin() + selectedObject_);
        selectedObject_ = -1;
        committed = true;
    }

    if (committed) commitChange();
    ImGui::End();
}

std::vector<std::string> App::flowVarNames(const std::string& nodeType) const {
    // int / bool / position variables live in separate namespaces
    auto ns = [](const std::string& t) {
        if (t == "SetVarInt" || t == "VarAtLeast") return 0;
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

    SceneObject& owner = project_.objects()[flowGraphObject_];
    FlowGraph& fg = owner.flowGraph;
    bool changed = false;

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
    // Right-aligns a pin label to the node edge (Indent must be paired with
    // Unindent - it is window state, not per-attribute)
    auto rightLabel = [&](const char* txt, bool disabled) {
        const float indent = nodeWidth - ImGui::CalcTextSize(txt).x;
        if (indent > 0) ImGui::Indent(indent);
        if (disabled)
            ImGui::TextDisabled("%s", txt);
        else
            ImGui::TextUnformatted(txt);
        if (indent > 0) ImGui::Unindent(indent);
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
            const char* buttons[] = {"Cross", "Circle", "Square", "Triangle"};
            if (ImGui::BeginCombo("Button", n.str.empty() ? "Cross" : n.str.c_str())) {
                for (const char* b : buttons) {
                    if (ImGui::Selectable(b, n.str == b)) {
                        n.str = b;
                        changed = true;
                    }
                }
                ImGui::EndCombo();
            }
        } else if (t->strKind == FlowParamKind::Text) {
            char buf[128];
            std::snprintf(buf, sizeof(buf), "%s", n.str.c_str());
            if (ImGui::InputText("Text", buf, sizeof(buf))) n.str = buf;
            changed |= ImGui::IsItemDeactivatedAfterEdit();
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

        // numeric params
        if (posLinked && t->posIn && t->numCount == 3) {
            // X/Y/Z come from the position link, the node's own params rest
            ImGui::TextDisabled("Position: from link");
        } else if (n.type == "SetVarBool") {
            bool v = n.num[0] != 0.0f;
            if (ImGui::Checkbox("Value", &v)) {
                n.num[0] = v ? 1.0f : 0.0f;
                changed = true;
            }
        } else if (t->numKind == FlowParamKind::Color) {
            ImGui::ColorEdit3("Color", n.num, ImGuiColorEditFlags_NoInputs);
            changed |= ImGui::IsItemDeactivatedAfterEdit();
        } else {
            for (int a = 0; a < t->numCount; ++a) {
                const bool isLoop = std::strcmp(t->numLabels[a], "Loop") == 0;
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
                    if (ImGui::Checkbox("Loop", &loop)) {
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
        ImGui::PopItemWidth();
        ImGui::PopID();

        // pins: exec flow (round) + object id (square, amber) + position
        // (triangle, green) + bool value (circle, violet). Pure data nodes
        // have no exec pins; the bool-in pin accepts several links (folded).
        const unsigned idPinCol = IM_COL32(222, 170, 60, 255);
        const unsigned posPinCol = IM_COL32(110, 200, 120, 255);
        const unsigned boolPinCol = IM_COL32(180, 120, 220, 255);
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
        if (!t->pure) {
            if (t->trigger) {
                ImNodes::BeginOutputAttribute(flowOutPin(n.id));
                rightLabel("then >", false);
                ImNodes::EndOutputAttribute();
            } else {
                ImNodes::BeginInputAttribute(flowInPin(n.id));
                ImGui::TextUnformatted("> do");
                ImNodes::EndInputAttribute();
            }
        }
        if (t->idOut) {
            ImNodes::PushColorStyle(ImNodesCol_Pin, idPinCol);
            ImNodes::BeginOutputAttribute(flowIdOutPin(n.id), ImNodesPinShape_QuadFilled);
            rightLabel("object >", true);
            ImNodes::EndOutputAttribute();
            ImNodes::PopColorStyle();
        }
        if (t->posOut) {
            ImNodes::PushColorStyle(ImNodesCol_Pin, posPinCol);
            ImNodes::BeginOutputAttribute(flowPosOutPin(n.id),
                                          ImNodesPinShape_TriangleFilled);
            rightLabel("position >", true);
            ImNodes::EndOutputAttribute();
            ImNodes::PopColorStyle();
        }
        if (t->boolOut) {
            ImNodes::PushColorStyle(ImNodesCol_Pin, boolPinCol);
            ImNodes::BeginOutputAttribute(flowBoolOutPin(n.id),
                                          ImNodesPinShape_CircleFilled);
            rightLabel("bool >", true);
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

    // New link dragged between pins. Pin kinds by id (pin % 8): 0 = object
    // in, 1 = exec out, 2 = exec in, 3 = object out, 4 = position in,
    // 5 = position out, 6 = bool in, 7 = bool out; node = pin / 8.
    int startPin = 0, endPin = 0;
    if (ImNodes::IsLinkCreated(&startPin, &endPin)) {
        const int a = startPin % 8, b = endPin % 8;
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
        }
        if (outPin >= 0 && outPin / 8 != inPin / 8) {
            FlowLink l;
            l.fromNode = outPin / 8;
            l.toNode = inPin / 8;
            l.kind = kind;
            if (kind == FlowLinkObject || kind == FlowLinkPos) {
                // a node takes its object/position from at most one link
                // (bool-in pins fold over several links, so keep them all)
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
            for (const FlowNodeType& t : flowNodeTypes()) {
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
                    if (std::string(t.key) == "PlayAnimation") n.num[1] = 1.0f;  // speed
                    if (std::string(t.key) == "PlayMusic") {
                        n.num[0] = 80.0f;  // volume
                        n.num[1] = 1.0f;   // loop
                        if (!project_.music.empty()) n.str = project_.music.front();
                    }
                    if (std::string(t.key) == "SetMusicVolume") n.num[0] = 80.0f;
                    if (std::string(t.key) == "SetVarBool") n.num[0] = 1.0f;
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

void App::openInVSCode() {
    // `code` is a .cmd shim, so it has to go through cmd.exe
    std::string cmd = "cmd.exe /S /C \"code \"" + project_.dir + "\"\"";
    STARTUPINFOA si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    std::string mutableCmd = cmd;
    if (CreateProcessA(nullptr, mutableCmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                       nullptr, project_.dir.c_str(), &si, &pi)) {
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        statusMessage_ = "Opening in VS Code...";
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

void App::importHudImage() {
    const std::string src = pickPngFile();
    if (src.empty()) return;

    const std::filesystem::path srcPath(src);
    const std::string fileName = sanitizeAssetName(srcPath.filename().string());
    const std::filesystem::path destDir = std::filesystem::path(project_.dir) / "res" / "hud";
    std::error_code ec;
    std::filesystem::create_directories(destDir, ec);
    std::filesystem::copy_file(srcPath, destDir / fileName,
                               std::filesystem::copy_options::overwrite_existing, ec);
    if (ec) {
        statusMessage_ = "HUD image import failed: " + ec.message();
        return;
    }

    HudImage h;
    h.name = srcPath.stem().string();
    h.imagePath = "res/hud/" + fileName;
    hudTexCache_.erase(h.imagePath);  // reload if replaced
    if (const HudTexture* t = hudTexture(h.imagePath)) {
        h.size[0] = (float)t->w;
        h.size[1] = (float)t->h;
    }
    project_.hud.push_back(std::move(h));
    selectedHud_ = (int)project_.hud.size() - 1;
    saveAll("Saved");
}

void App::drawHudSection() {
    if (!ImGui::CollapsingHeader("HUD")) return;

    if (ImGui::SmallButton("Import image (PNG)...")) importHudImage();
    ImGui::SameLine();
    ImGui::Checkbox("Show in viewport", &showHudInEditor_);

    bool changed = false;
    for (int i = 0; i < (int)project_.hud.size(); ++i) {
        std::string label = project_.hud[i].name + "##hud" + std::to_string(i);
        if (ImGui::Selectable(label.c_str(), selectedHud_ == i)) selectedHud_ = i;
    }
    if (project_.hud.empty()) ImGui::TextDisabled("No HUD images.");

    if (selectedHud_ >= 0 && selectedHud_ < (int)project_.hud.size()) {
        HudImage& h = project_.hud[selectedHud_];
        ImGui::DragFloat2("Position##hud", h.pos, 0.005f, 0.0f, 1.0f, "%.3f");
        changed |= ImGui::IsItemDeactivatedAfterEdit();
        ImGui::DragFloat2("Size (px)##hud", h.size, 1.0f, 1.0f, 512.0f, "%.0f");
        changed |= ImGui::IsItemDeactivatedAfterEdit();
        if (ImGui::Button("Delete HUD image")) {
            project_.hud.erase(project_.hud.begin() + selectedHud_);
            selectedHud_ = -1;
            changed = true;
        }
    }
    if (changed) saveAll("Saved");
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
    // out-of-range rates) are converted in place after the copy.
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
        const int targetRate = (rate >= 11025 && rate <= 48000) ? rate : 22050;
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

    bool changed = false;
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
        if (ImGui::SmallButton("x")) {
            removeAudioTrack(project_, project_.music[i], true);
            project_.music.erase(project_.music.begin() + i);
            changed = true;
            ImGui::PopID();
            break;
        }
        ImGui::PopID();
    }
    if (project_.music.empty()) ImGui::TextDisabled("No music tracks.");
    if (changed) commitChange();  // graphs live in objects - undoable
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

    bool changed = false;
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
        if (ImGui::SmallButton("x")) {
            removeAudioTrack(project_, project_.sounds[i], false);
            project_.sounds.erase(project_.sounds.begin() + i);
            changed = true;
            ImGui::PopID();
            break;
        }
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
    if (changed) commitChange();
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
                         "Images list, fonts: Font combo) or the HUD section";
    } else if (skipped > 0) {
        statusMessage_ = "Drop: PNG images and TTF/OTF fonts are handled here";
    }
}

// Menu Editor window: menu list on the left, the selected menu's properties,
// entries and a live baked-panel preview (the exact pixels the PS2 will
// draw) on the right.
void App::drawMenusWindow() {
    if (!showMenusEditor_ || !hasProject_) return;

    ImGui::SetNextWindowSize(ImVec2(680, 540), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Menu Editor", &showMenusEditor_)) {
        ImGui::End();
        return;
    }

    bool changed = false;

    // --- left: menu list -------------------------------------------------
    ImGui::BeginChild("##menu_list", ImVec2(170, 0), ImGuiChildFlags_Borders);
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
    ImGui::SetNextItemWidth(180.0f);
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
    ImGui::SetNextItemWidth(180.0f);
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
    ImGui::SetNextItemWidth(180.0f);
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
        ImGui::SetNextItemWidth(100.0f);
        if (ImGui::Combo("Panel width", &wIdx, "128 px\000256 px\000512 px\000")) {
            m.panelW = wIdx == 0 ? 128 : wIdx == 2 ? 512 : 256;
            changed = true;
        }
    }
    ImGui::SetNextItemWidth(180.0f);
    ImGui::DragFloat2("Screen position", m.screenPos, 0.005f, 0.0f, 1.0f, "%.3f");
    changed |= ImGui::IsItemDeactivatedAfterEdit();
    if (ImGui::Checkbox("Show title", &m.showTitle)) changed = true;

    // Font: default chain / fonts imported into the project / a curated set
    // of stock Windows fonts (existence-checked) / import a new TTF.
    {
        std::string current = "Default (Consolas Bold)";
        if (!m.fontPath.empty())
            current = std::filesystem::path(m.fontPath).filename().string();
        ImGui::SetNextItemWidth(200.0f);
        if (ImGui::BeginCombo("Font", current.c_str())) {
            if (ImGui::Selectable("Default (Consolas Bold)", m.fontPath.empty())) {
                m.fontPath.clear();
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
                            m.fontPath == rel)) {
                        m.fontPath = rel;
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
                if (ImGui::Selectable(sf.label, m.fontPath == sf.file)) {
                    m.fontPath = sf.file;
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
                        m.fontPath = "res/fonts/" + fileName;
                        changed = true;
                    }
                }
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Rasterized into the panel at build time - any TTF\n"
                              "works, nothing ships to the PS2 but pixels.\n"
                              "Project fonts (res/fonts) travel with the project;\n"
                              "Windows fonts depend on this machine.");
    }
    {
        int sizes[2] = {m.titleSize, m.entrySize};
        ImGui::SetNextItemWidth(140.0f);
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
        ImGui::SameLine(190.0f);
        ImGui::SetNextItemWidth(120.0f);
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
            ImGui::Indent(46.0f);
            ImGui::SetNextItemWidth(90.0f);
            ImGui::DragFloat("scale##img", &img.scale, 0.02f, 0.05f, 4.0f, "%.2fx");
            changed |= ImGui::IsItemDeactivatedAfterEdit();
            ImGui::SameLine();
            ImGui::SetNextItemWidth(120.0f);
            ImGui::DragFloat2("offset##img", img.offset, 1.0f, -512.0f, 512.0f,
                              "%.0f px");
            changed |= ImGui::IsItemDeactivatedAfterEdit();
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(img.slot == MenuImage::Overlay
                                      ? "Top-left position inside the panel."
                                      : "Nudge from the centered flow position.");
            ImGui::Unindent(46.0f);
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
        "Set save value", "Add to save value", "Flow event"};
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
        ImGui::SetNextItemWidth(140.0f);
        if (ImGui::InputText("##label", labelBuf, sizeof(labelBuf))) en.label = labelBuf;
        changed |= ImGui::IsItemDeactivatedAfterEdit();
        ImGui::SameLine();
        ImGui::SetNextItemWidth(150.0f);
        if (ImGui::Combo("##action", &en.action, kActionNames, 7)) {
            en.param.clear();
            changed = true;
        }
        ImGui::SameLine();

        // Action target inline (scene / menu / value / event)
        auto paramCombo = [&](const char* comboId, const char* hint, auto&& items,
                              auto&& nameOf) {
            ImGui::SetNextItemWidth(120.0f);
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
            ImGui::SetNextItemWidth(70.0f);
            ImGui::DragFloat("##amount", &en.amount, 0.1f, 0.0f, 0.0f, "%.2f");
            changed |= ImGui::IsItemDeactivatedAfterEdit();
            ImGui::SameLine();
        } else if (en.action == MenuEntry::FlowEvent) {
            char eventBuf[64];
            std::snprintf(eventBuf, sizeof(eventBuf), "%s", en.param.c_str());
            ImGui::SetNextItemWidth(120.0f);
            if (ImGui::InputText("##event", eventBuf, sizeof(eventBuf)))
                en.param = eventBuf;
            changed |= ImGui::IsItemDeactivatedAfterEdit();
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Event name for the On Menu Event flow trigger.");
            ImGui::SameLine();
        }

        if (ImGui::SmallButton("x##delete")) {
            m.entries.erase(m.entries.begin() + e);
            changed = true;
            ImGui::PopID();
            break;
        }
        ImGui::PopID();
    }
    if ((int)m.entries.size() < menubake::kMaxEntries) {
        if (ImGui::SmallButton("+ Entry")) {
            m.entries.push_back(MenuEntry{});
            changed = true;
        }
    } else {
        ImGui::TextDisabled("Max %d entries per menu.", menubake::kMaxEntries);
    }

    // Live preview: the exact panel the build will bake, either 1:1 or
    // composited onto a mock TV screen (the 512x448 buffer stretched to the
    // PAL / NTSC display aspect, with the pause dim when it applies).
    ImGui::SeparatorText("Preview");
    ImGui::SetNextItemWidth(140.0f);
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
        for (const MenuEntry& en : m.entries)
            key += "\x1f" + en.label + "|" + std::to_string(en.action);
        if (key != menuPreviewKey_) {
            std::vector<unsigned char> rgba;
            int w = 0, h = 0;
            if (menubake::bakePanelRGBA(m, project_.dir, rgba, w, h)) {
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
            ImGui::Image((ImTextureID)(intptr_t)menuPreviewTex_,
                         ImVec2((float)menuPreviewW_, (float)menuPreviewContentH_),
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
            float sw = ImGui::GetContentRegionAvail().x - 8.0f;
            if (sw > 460.0f) sw = 460.0f;
            if (sw < 200.0f) sw = 200.0f;
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
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Open in VS Code")) openInVSCode();

    // List src/scripts/*.cpp (user scripts live there and are compiled
    // automatically by the project Makefile)
    const std::filesystem::path dir = std::filesystem::path(project_.dir) / "src" / "scripts";
    std::error_code ec;
    bool any = false;
    if (std::filesystem::exists(dir, ec)) {
        for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
            if (entry.path().extension() != ".cpp") continue;
            ImGui::BulletText("%s", entry.path().filename().string().c_str());
            any = true;
        }
    }
    if (!any) ImGui::TextDisabled("No scripts yet.");
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

    if (!newScriptError_.empty())
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", newScriptError_.c_str());

    ImGui::Separator();
    if (ImGui::Button("Create", ImVec2(120, 0))) {
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
                    statusMessage_ = "Created " + name + ".cpp";
                    ImGui::CloseCurrentPopup();
                } else {
                    newScriptError_ = "Cannot write " + path.string();
                }
            }
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(120, 0))) ImGui::CloseCurrentPopup();
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
    if (ImGui::Button("Delete", ImVec2(120, 0))) {
        project_.scenes.erase(project_.scenes.begin() + deleteScenePending_);
        if (project_.activeScene >= (int)project_.scenes.size() ||
            project_.activeScene == deleteScenePending_)
            project_.activeScene = 0;
        selectedObject_ = -1;
        flowGraphObject_ = -1;
        flowPositionsApplied_ = false;
        deleteScenePending_ = -1;
        commitChange();
        applyProjectToViewport();
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(120, 0))) {
        deleteScenePending_ = -1;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
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
    ImGui::DragInt("Terrain width", &newSceneWidth_, 1.0f, 8, 512, "%d units");
    ImGui::DragInt("Terrain depth", &newSceneDepth_, 1.0f, 8, 512, "%d units");
    if (!newSceneError_.empty())
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", newSceneError_.c_str());

    ImGui::Separator();
    if (ImGui::Button("Create", ImVec2(120, 0))) {
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
            selectedObject_ = -1;
            flowGraphObject_ = -1;
            flowPositionsApplied_ = false;
            applyProjectToViewport();  // builds the flat heightmap + mesh
            project::saveHeights(project_);
            commitChange();
            statusMessage_ = "Created scene " + name;
            ImGui::CloseCurrentPopup();
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(120, 0))) ImGui::CloseCurrentPopup();
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

    if (path.empty())
        ImGui::TextDisabled(
            debugLogSource_ == 0
                ? "No project open."
                : "Emulator not found. Set the path in Project > Preferences.");
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

    ImGui::SetNextWindowSize(ImVec2(980, 560), ImGuiCond_FirstUseEver);
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
    ImGui::SetNextItemWidth(150.0f);
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
    const float discPaneW = std::max(280.0f, ImGui::GetContentRegionAvail().x * 0.38f);
    ImGui::BeginChild("##discfiles",
                      ImVec2(ImGui::GetContentRegionAvail().x - discPaneW - 8.0f, 0));
    ImGui::TextDisabled("Drag rows to change the disc order (saved to iso-layout.txt).");
    if (ImGui::BeginTable("##disctable", 5,
                          ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
                              ImGuiTableFlags_ScrollY)) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, 28.0f);
        ImGui::TableSetupColumn("File", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Group", ImGuiTableColumnFlags_WidthFixed, 100.0f);
        ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed, 72.0f);
        ImGui::TableSetupColumn("LBA", ImGuiTableColumnFlags_WidthFixed, 64.0f);
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
                ImGui::SameLine(0.0f, 3.0f);
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
    const float side = std::min(avail.x, std::max(120.0f, avail.y - legendH));
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
    }

    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(560, 0), ImGuiCond_Appearing);

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
        if (ImGui::Button("Create", ImVec2(120, 0))) {
            Project p;
            TerrainConfig t{newWidth_, newDepth_};
            const char* preset = newTemplate_ == 1 ? "fpp" : "empty";
            std::string err = project::create(p, newName_, newLocation_, t, preset);
            if (err.empty()) {
                project_ = p;
                hasProject_ = true;
                applyProjectToViewport();
                attachProject();
                glfwSetWindowTitle(window_,
                                   ("Tyra Editor - " + project_.name).c_str());
                ImGui::CloseCurrentPopup();
            } else {
                newProjectError_ = err;
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

void App::applyProjectToViewport() {
    project::ensureHeightmap(project_);
    const SceneData& sc = project_.active();
    // Scene-visual settings resolve project defaults + this scene's overrides.
    const ProjectSettings rs = project::resolvedSettings(project_, sc);
    viewport_.setProjectDir(project_.dir);
    viewport_.setTerrainTexture(rs.terrainTexture, rs.terrainTexScale);
    viewport_.setTerrain(sc.terrain, project_.settings.terrainDetail, sc.heights, sc.hmW,
                         sc.hmD);
    viewport_.setSky(rs.skyColor, rs.skyTopColor, rs.skyDome);
    viewport_.setUsableHighlight(rs.highlightUsable, rs.highlightColor);
    viewport_.setLighting(rs.lightDir, rs.ambient, rs.diffuse, rs.lightColor, rs.brightness);
}

void App::drawPreferencesModal() {
    if (openPreferencesPopup_) {
        ImGui::OpenPopup("Project Preferences");
        openPreferencesPopup_ = false;
    }

    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(560, 0), ImGuiCond_Appearing);

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
    int profile = prefSettings_.buildProfile == "debug" ? 1 : 0;
    const char* profileNames[] = {"Release", "Debug"};
    if (ImGui::Combo("Profile", &profile, profileNames, 2))
        prefSettings_.buildProfile = profile == 1 ? "debug" : "release";
    ImGui::BeginDisabled(profile == 0);
    ImGui::Checkbox("Show FPS", &prefSettings_.showFps);
    ImGui::Checkbox("Show memory usage", &prefSettings_.showMemory);
    ImGui::EndDisabled();
    ImGui::TextDisabled(
        "Debug-profile overlays drawn in the top-left corner of the game:\n"
        "frames per second and free EE RAM. Stripped from release builds.");

    ImGui::SeparatorText("Terrain");
    ImGui::InputInt("Width (units)", &prefTerrain_.width);
    ImGui::InputInt("Depth (units)", &prefTerrain_.depth);
    prefTerrain_.width = prefTerrain_.width < 1 ? 1 : prefTerrain_.width > 4096 ? 4096
                                                                                : prefTerrain_.width;
    prefTerrain_.depth = prefTerrain_.depth < 1 ? 1 : prefTerrain_.depth > 4096 ? 4096
                                                                                : prefTerrain_.depth;
    ImGui::SliderInt("Detail (max grid cells)", &prefSettings_.terrainDetail, 4, 128);
    ImGui::TextDisabled("More cells = smaller triangles = fewer clipping artifacts,");
    ImGui::TextDisabled("but more geometry for the PS2 to push.");

    ImGui::SeparatorText("Rendering");
    int clipMode = prefSettings_.clipping == "fast" ? 1 : 0;
    const char* clipNames[] = {
        "Precise clipping (no holes at screen edges, costs EE time)",
        "Fast culling (fastest; big near triangles may vanish)"};
    if (ImGui::Combo("Triangles", &clipMode, clipNames, 2))
        prefSettings_.clipping = clipMode == 1 ? "fast" : "precise";

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

    ImGui::TextDisabled(
        "Terrain texture: %s",
        prefSettings_.terrainTexture.empty() ? "<none>" : prefSettings_.terrainTexture.c_str());
    ImGui::SameLine();
    pickProjectTexture("##pick_terrain_texture", prefSettings_.terrainTexture);
    if (!prefSettings_.terrainTexture.empty()) {
        ImGui::SameLine();
        if (ImGui::SmallButton("Clear##terrtex")) prefSettings_.terrainTexture.clear();
        ImGui::DragFloat("Texture tile (units)", &prefSettings_.terrainTexScale, 0.25f, 0.25f,
                         64.0f, "%.2f");
    }
    ImGui::ColorEdit3("Sky horizon color", prefSettings_.skyColor);
    ImGui::ColorEdit3("Sky zenith color", prefSettings_.skyTopColor);
    ImGui::Checkbox("Gradient sky dome", &prefSettings_.skyDome);

    ImGui::SeparatorText("Post effects");
    ImGui::SliderFloat("Bloom", &prefSettings_.bloom, 0.0f, 1.0f, "%.2f");
    ImGui::SliderFloat("Film grain", &prefSettings_.grain, 0.0f, 1.0f, "%.2f");
    ImGui::TextDisabled(
        "GS framebuffer tricks, applied in-game at the end of every frame.\n"
        "Bloom: quarter-res blur re-added over the frame (soft glow).\n"
        "Film grain: animated noise overlay. Subtle values work best.");

    ImGui::SeparatorText("Scenes");
    ImGui::Checkbox("Loading screen between scenes", &prefSettings_.loadingScreen);
    ImGui::TextDisabled(
        "Scene switches show res/hud/loading.png centered on black for ~0.7s.\n"
        "A placeholder is generated - replace the file to customize it.");

    ImGui::SeparatorText("Usable objects");
    ImGui::Checkbox("Highlight usable objects", &prefSettings_.highlightUsable);
    if (prefSettings_.highlightUsable) {
        ImGui::DragFloat("Proximity (units)", &prefSettings_.highlightDistance, 0.1f,
                         0.5f, 1000.0f, "%.1f");
        ImGui::ColorEdit3("Highlight color", prefSettings_.highlightColor);
        ImGui::DragFloat("Blur width (units)", &prefSettings_.highlightWidth, 0.01f,
                         0.05f, 2.0f, "%.2f");
        ImGui::SliderInt("Blur steps", &prefSettings_.highlightSteps, 1, 8);
        ImGui::TextDisabled(
            "Width = total rim size; steps = shells in the fade (1 = sharp edge).");
    }
    ImGui::TextDisabled(
        "In-game outline around objects marked 'Usable' while the player is\n"
        "within the proximity distance. The viewport marks them with a wire box.");

    ImGui::SeparatorText("Lighting");
    ImGui::DragFloat3("Light direction", prefSettings_.lightDir, 0.02f, -1.0f, 1.0f, "%.2f");
    ImGui::ColorEdit3("Light color", prefSettings_.lightColor);
    ImGui::SliderFloat("Brightness", &prefSettings_.brightness, 0.0f, 2.0f, "%.2f");
    ImGui::SliderFloat("Ambient", &prefSettings_.ambient, 0.0f, 1.0f, "%.2f");
    ImGui::SliderFloat("Diffuse", &prefSettings_.diffuse, 0.0f, 1.0f, "%.2f");

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

    ImGui::SeparatorText("Physics");
    ImGui::DragFloat("Gravity (units/s^2)", &prefSettings_.gravity, 0.1f, 0.0f, 100.0f,
                     "%.1f");
    if (prefTemplate_ == 1)
        ImGui::DragFloat("Jump speed (units/s)", &prefSettings_.jumpSpeed, 0.1f, 0.0f, 50.0f,
                         "%.1f");
    ImGui::TextDisabled("Objects with the 'Physics' flag fall; the FPP player jumps with X.");

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
        "IP of a PS2 on the LAN running PS2LINK.ELF. Enables Project > Build &&\n"
        "Run on PS2 (F6): the game boots on the console over ethernet with its\n"
        "assets served from this PC - no ISO, no SMB. Leave empty to disable.");

    ImGui::Separator();
    ImGui::TextDisabled(
        "These are project-wide defaults. Scenes inherit them unless a\n"
        "category is overridden in Scene > Scene Preferences.");
    if (ImGui::Button("OK", ImVec2(120, 0))) {
        project_.gameTemplate = prefTemplate_ == 1 ? "fpp" : "orbit";
        project_.settings = prefSettings_;
        project_.emulatorPath = prefEmulatorPath_;
        project_.ps2LinkIp = prefPs2Ip_;
        project_.active().terrain = prefTerrain_;
        applyProjectToViewport();  // scenes that inherit follow the new defaults
        commitChange();
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(120, 0))) ImGui::CloseCurrentPopup();
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
    openScenePrefsPopup_ = true;
}

void App::drawScenePreferencesModal() {
    if (openScenePrefsPopup_) {
        ImGui::OpenPopup("Scene Preferences");
        openScenePrefsPopup_ = false;
    }

    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(560, 0), ImGuiCond_Appearing);

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

    category("Lighting", ov.lighting, [&] {
        ImGui::DragFloat3("Light direction", s.lightDir, 0.02f, -1.0f, 1.0f, "%.2f");
        ImGui::ColorEdit3("Light color", s.lightColor);
        ImGui::SliderFloat("Brightness", &s.brightness, 0.0f, 2.0f, "%.2f");
        ImGui::SliderFloat("Ambient", &s.ambient, 0.0f, 1.0f, "%.2f");
        ImGui::SliderFloat("Diffuse", &s.diffuse, 0.0f, 1.0f, "%.2f");
    });

    category("Sky", ov.sky, [&] {
        ImGui::ColorEdit3("Sky horizon color", s.skyColor);
        ImGui::ColorEdit3("Sky zenith color", s.skyTopColor);
        ImGui::Checkbox("Gradient sky dome", &s.skyDome);
    });

    category("Clipping", ov.clipping, [&] {
        int clipMode = s.clipping == "fast" ? 1 : 0;
        const char* clipNames[] = {
            "Precise clipping (no holes at screen edges, costs EE time)",
            "Fast culling (fastest; big near triangles may vanish)"};
        if (ImGui::Combo("Triangles", &clipMode, clipNames, 2))
            s.clipping = clipMode == 1 ? "fast" : "precise";
    });

    category("Terrain texture", ov.terrainTex, [&] {
        ImGui::TextDisabled("Texture: %s",
                            s.terrainTexture.empty() ? "<none>" : s.terrainTexture.c_str());
        ImGui::SameLine();
        pickProjectTexture("##pick_scene_terrain_texture", s.terrainTexture);
        if (!s.terrainTexture.empty()) {
            ImGui::SameLine();
            if (ImGui::SmallButton("Clear")) s.terrainTexture.clear();
            ImGui::DragFloat("Texture tile (units)", &s.terrainTexScale, 0.25f, 0.25f, 64.0f,
                             "%.2f");
        }
    });

    category("Post effects", ov.postFx, [&] {
        ImGui::SliderFloat("Bloom", &s.bloom, 0.0f, 1.0f, "%.2f");
        ImGui::SliderFloat("Film grain", &s.grain, 0.0f, 1.0f, "%.2f");
    });

    category("Usable objects", ov.highlight, [&] {
        ImGui::Checkbox("Highlight usable objects", &s.highlightUsable);
        ImGui::DragFloat("Proximity (units)", &s.highlightDistance, 0.1f, 0.5f, 1000.0f, "%.1f");
        ImGui::ColorEdit3("Highlight color", s.highlightColor);
        ImGui::DragFloat("Blur width (units)", &s.highlightWidth, 0.01f, 0.05f, 2.0f, "%.2f");
        ImGui::SliderInt("Blur steps", &s.highlightSteps, 1, 8);
    });

    ImGui::Separator();
    if (ImGui::Button("OK", ImVec2(120, 0))) {
        SceneData& sc = project_.scenes[scenePrefScene_];
        sc.settings = scenePrefSettings_;
        sc.overrides = scenePrefOverrides_;
        applyProjectToViewport();
        commitChange();
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(120, 0))) ImGui::CloseCurrentPopup();
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
        attachProject();
        glfwSetWindowTitle(window_, ("Tyra Editor - " + project_.name).c_str());
    } else {
        runner_.clearLog();
        // Surface the error in the Output window via the runner log is hacky;
        // show a popup instead on next frame. Simple approach: message box.
        MessageBoxA(nullptr, err.c_str(), "Open Project", MB_ICONERROR | MB_OK);
    }
}
