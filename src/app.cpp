#include "app.hpp"

#include <cfloat>
#include <cstdio>
#include <cstring>

#include <filesystem>
#include <fstream>

#include "gl_loader.h"
#include "templates.hpp"

#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <ImGuizmo.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shobjidl.h>

// ---------------------------------------------------------------------------
// Native pickers (IFileOpenDialog). pickFolder: FOS_PICKFOLDERS;
// pickSolutionFile: file dialog filtered to *.tyra solution files.
// ---------------------------------------------------------------------------
static std::string pickPath(bool folder) {
    std::string result;
    HRESULT hrInit = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    IFileOpenDialog* dialog = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_ALL,
                                   IID_PPV_ARGS(&dialog)))) {
        DWORD options = 0;
        dialog->GetOptions(&options);
        dialog->SetOptions(options | (folder ? FOS_PICKFOLDERS : 0) | FOS_FORCEFILESYSTEM);
        if (!folder) {
            static const COMDLG_FILTERSPEC filters[] = {
                {L"Tyra project (*.tyra, project.json)", L"*.tyra;project.json"},
                {L"All files (*.*)", L"*.*"},
            };
            dialog->SetFileTypes(2, filters);
            dialog->SetTitle(L"Open Tyra project");
        }
        if (SUCCEEDED(dialog->Show(nullptr))) {
            IShellItem* item = nullptr;
            if (SUCCEEDED(dialog->GetResult(&item))) {
                PWSTR path = nullptr;
                if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
                    int len = WideCharToMultiByte(CP_UTF8, 0, path, -1, nullptr, 0, nullptr,
                                                  nullptr);
                    if (len > 1) {
                        result.resize(len - 1);
                        WideCharToMultiByte(CP_UTF8, 0, path, -1, result.data(), len, nullptr,
                                            nullptr);
                    }
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

static std::string pickFolder() { return pickPath(true); }
static std::string pickSolutionFile() { return pickPath(false); }

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

    window_ = glfwCreateWindow(1600, 900, "Tyra Editor", nullptr, nullptr);
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
    ImGui::StyleColorsDark();

    ImGui_ImplGlfw_InitForOpenGL(window_, true);
    ImGui_ImplOpenGL3_Init("#version 330 core");

    if (!viewport_.init()) {
        std::fprintf(stderr, "Failed to init viewport renderer\n");
        return 1;
    }

    // Default location for new projects: the user's home dir
    if (const char* home = getenv("USERPROFILE"))
        std::snprintf(newLocation_, sizeof(newLocation_), "%s\\TyraProjects", home);

    if (!initialProjectDir.empty()) {
        // Accept both a project directory and a <name>.tyra solution file
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

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        drawUI();

        ImGui::Render();
        int w, h;
        glfwGetFramebufferSize(window_, &w, &h);
        glViewport(0, 0, w, h);
        glClearColor(0.08f, 0.08f, 0.10f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(window_);
    }

    viewport_.shutdown();
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
            ImGuiID bottom = ImGui::DockBuilderSplitNode(center, ImGuiDir_Down, 0.26f, nullptr,
                                                         &center);
            ImGui::DockBuilderDockWindow("Project", left);
            ImGui::DockBuilderDockWindow("Output", bottom);
            ImGui::DockBuilderDockWindow("Viewport", center);
            ImGui::DockBuilderFinish(dockspace);
        }
    }

    drawMenuBar();
    drawViewportWindow();
    drawProjectWindow();
    drawOutputWindow();
    drawNewProjectModal();
    drawPreferencesModal();
    drawNewScriptModal();

    // Keyboard shortcuts
    ImGuiIO& io = ImGui::GetIO();
    if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_N)) openNewProjectPopup_ = true;
    if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_O)) openProjectDialog();
    if (hasProject_ && !runner_.busy() && ImGui::IsKeyPressed(ImGuiKey_F5))
        runner_.buildAndRun(project_, true);
    if (hasProject_) {
        if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_S)) saveAll("Saved");
        if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Comma)) {
            prefTerrain_ = project_.terrain;
            prefTemplate_ = project_.gameTemplate == "fpp" ? 1 : 0;
            prefSettings_ = project_.settings;
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
                selectedObject_ >= 0 && selectedObject_ < (int)project_.objects.size();
            if (ImGui::MenuItem("Undo", "Ctrl+Z", false, history_.canUndo())) undo();
            if (ImGui::MenuItem("Redo", "Ctrl+Y", false, history_.canRedo())) redo();
            ImGui::Separator();
            if (ImGui::MenuItem("Copy object", "Ctrl+C", false, objectSelected)) copyObject();
            if (ImGui::MenuItem("Paste object", "Ctrl+V", false, hasClipboard_)) pasteObject();
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Scene", hasProject_)) {
            if (ImGui::MenuItem("Add Box")) addObject(PrimitiveType::Box);
            if (ImGui::MenuItem("Add Sphere")) addObject(PrimitiveType::Sphere);
            if (ImGui::MenuItem("Add Cylinder")) addObject(PrimitiveType::Cylinder);
            if (ImGui::MenuItem("Add Cone")) addObject(PrimitiveType::Cone);
            ImGui::Separator();
            if (ImGui::MenuItem("Add Spawn Point")) addObject(PrimitiveType::SpawnPoint);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Project", hasProject_)) {
            const bool busy = runner_.busy();
            if (ImGui::MenuItem("Preferences...", "Ctrl+,")) {
                prefTerrain_ = project_.terrain;
                prefTemplate_ = project_.gameTemplate == "fpp" ? 1 : 0;
                prefSettings_ = project_.settings;
                openPreferencesPopup_ = true;
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Build", nullptr, false, !busy))
                runner_.buildAndRun(project_, false);
            if (ImGui::MenuItem("Build && Run in PCSX2", "F5", false, !busy))
                runner_.buildAndRun(project_, true);
            if (ImGui::MenuItem("Run in PCSX2 (no build)", nullptr, false, !busy))
                runner_.runEmulatorOnly(project_);
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
            viewport_.render((int)avail.x, (int)avail.y, project_.objects, selectedObject_);
        // Flip vertically: GL texture origin is bottom-left
        ImGui::Image((ImTextureID)(intptr_t)tex, avail, ImVec2(0, 1), ImVec2(1, 0));

        const ImVec2 imgPos = ImGui::GetItemRectMin();
        const bool imageHovered = ImGui::IsItemHovered();
        ImGuiIO& io = ImGui::GetIO();

        // --- Transform gizmo on the selected object ---
        bool objectSelected =
            selectedObject_ >= 0 && selectedObject_ < (int)project_.objects.size();
        if (objectSelected) {
            SceneObject& o = project_.objects[selectedObject_];

            ImGuizmo::SetOrthographic(false);
            ImGuizmo::SetDrawlist();
            ImGuizmo::SetRect(imgPos.x, imgPos.y, avail.x, avail.y);

            const ImGuizmo::OPERATION ops[] = {ImGuizmo::TRANSLATE, ImGuizmo::ROTATE,
                                               ImGuizmo::SCALE};
            const ImGuizmo::OPERATION op = ops[gizmoOp_];
            const ImGuizmo::MODE mode =
                op == ImGuizmo::TRANSLATE ? ImGuizmo::WORLD : ImGuizmo::LOCAL;

            // Same TRS composition as the viewport / PS2 code
            float model[16];
            ImGuizmo::RecomposeMatrixFromComponents(o.position, o.rotation, o.scale, model);
            if (ImGuizmo::Manipulate(viewport_.viewMatrix(), viewport_.projMatrix(), op, mode,
                                     model)) {
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
            if (ImGui::IsMouseDragging(ImGuiMouseButton_Left) ||
                ImGui::IsMouseDragging(ImGuiMouseButton_Right)) {
                viewport_.orbit(io.MouseDelta.x, io.MouseDelta.y);
            }
            if (io.MouseWheel != 0.0f) viewport_.zoom(io.MouseWheel);

            // Click (no drag) = pick object under cursor
            if (ImGui::IsMouseReleased(ImGuiMouseButton_Left) &&
                io.MouseDragMaxDistanceSqr[ImGuiMouseButton_Left] < 9.0f) {
                const float u = (io.MousePos.x - imgPos.x) / avail.x;
                const float v = (io.MousePos.y - imgPos.y) / avail.y;
                selectedObject_ = viewport_.pick(u, v, project_.objects);
            }
        }

        // --- Tool buttons overlay (top-left corner of the viewport) ---
        ImGui::SetCursorScreenPos(ImVec2(imgPos.x + 8, imgPos.y + 8));
        const char* toolNames[] = {"Move (W)", "Rotate (E)", "Scale (R)"};
        for (int i = 0; i < 3; ++i) {
            if (i) ImGui::SameLine();
            const bool active = gizmoOp_ == i;
            if (active)
                ImGui::PushStyleColor(ImGuiCol_Button,
                                      ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
            if (ImGui::SmallButton(toolNames[i])) gizmoOp_ = i;
            if (active) ImGui::PopStyleColor();
        }

        // View mode switch (persisted in the solution file via saveAll)
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
                saveAll("Saved");  // persist the view mode in the solution file
            }
            if (active) ImGui::PopStyleColor();
        }

        // --- Keyboard shortcuts (viewport hovered, not typing) ---
        if (ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) && !io.WantTextInput) {
            if (ImGui::IsKeyPressed(ImGuiKey_W)) gizmoOp_ = 0;
            if (ImGui::IsKeyPressed(ImGuiKey_E)) gizmoOp_ = 1;
            if (ImGui::IsKeyPressed(ImGuiKey_R)) gizmoOp_ = 2;
            if (objectSelected && ImGui::IsKeyPressed(ImGuiKey_Delete)) {
                project_.objects.erase(project_.objects.begin() + selectedObject_);
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
    ImGui::Text("%d x %d units (flat)", project_.terrain.width, project_.terrain.depth);

    ImGui::Text("Target:");
    ImGui::SameLine(110);
    ImGui::TextUnformatted(project_.elfName().c_str());

    ImGui::SeparatorText("Scenes");
    for (const auto& s : project_.scenes) {
        ImGui::BulletText("%s%s", s.c_str(), s == "main" ? " (default)" : "");
    }

    drawSceneSection();
    drawScriptsSection();

    ImGui::SeparatorText("Build");
    const bool busy = runner_.busy();
    ImGui::BeginDisabled(busy);
    if (ImGui::Button("Build", ImVec2(100, 0))) runner_.buildAndRun(project_, false);
    ImGui::SameLine();
    if (ImGui::Button("Build & Run", ImVec2(120, 0))) runner_.buildAndRun(project_, true);
    ImGui::SameLine();
    if (ImGui::Button("Run", ImVec2(80, 0))) runner_.runEmulatorOnly(project_);
    ImGui::EndDisabled();

    if (busy) {
        ImGui::SameLine();
        ImGui::Text("Working... %c", "|/-\\"[(int)(ImGui::GetTime() * 8) & 3]);
    } else if (runner_.state() == Runner::State::Failed) {
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Last build failed - see Output.");
    } else if (runner_.state() == Runner::State::Success) {
        ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "Done.");
    }

    ImGui::End();
}

void App::saveProject() {
    if (auto err = project::save(project_); !err.empty())
        MessageBoxA(nullptr, err.c_str(), "Save Project", MB_ICONERROR | MB_OK);
}

void App::saveAll(const char* status) {
    saveProject();
    if (auto err = project::saveSolution(project_, history_, selectedObject_, gizmoOp_,
                                         (int)viewport_.viewMode());
        !err.empty())
        MessageBoxA(nullptr, err.c_str(), "Save Solution", MB_ICONERROR | MB_OK);
    statusMessage_ = status;
}

void App::commitChange() {
    history_.push({project_.terrain, project_.objects});
    saveAll("Saved");
}

void App::applySnapshot(const SceneSnapshot& s) {
    const bool terrainChanged = s.terrain.width != project_.terrain.width ||
                                s.terrain.depth != project_.terrain.depth;
    project_.terrain = s.terrain;
    project_.objects = s.objects;
    if (selectedObject_ >= (int)project_.objects.size()) selectedObject_ = -1;
    if (terrainChanged) applyProjectToViewport();
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
    if (selectedObject_ < 0 || selectedObject_ >= (int)project_.objects.size()) return;
    clipboard_ = project_.objects[selectedObject_];
    hasClipboard_ = true;
    statusMessage_ = "Copied " + clipboard_.name;
}

void App::pasteObject() {
    if (!hasClipboard_) return;

    SceneObject o = clipboard_;
    std::string name = o.name + "-copy";
    for (int n = 2;; ++n) {
        bool taken = false;
        for (const auto& other : project_.objects) taken |= (other.name == name);
        if (!taken) break;
        name = o.name + "-copy" + std::to_string(n);
    }
    o.name = name;
    o.position[0] += 1.0f;  // offset so the copy is visible next to the original
    o.position[2] += 1.0f;

    project_.objects.push_back(std::move(o));
    selectedObject_ = (int)project_.objects.size() - 1;
    commitChange();
    statusMessage_ = "Pasted " + project_.objects.back().name;
}

void App::attachProject() {
    selectedObject_ = -1;
    history_.reset({project_.terrain, project_.objects});
    // Solution file restores undo history + editor state when it is in sync
    // with project.json; otherwise we start fresh (and write a new one).
    int viewMode = 0;
    if (auto err =
            project::loadSolution(project_, history_, selectedObject_, gizmoOp_, viewMode);
        !err.empty()) {
        history_.reset({project_.terrain, project_.objects});
        project::saveSolution(project_, history_, selectedObject_, gizmoOp_, viewMode);
    }
    viewport_.setViewMode((Viewport::ViewMode)viewMode);
    statusMessage_.clear();
}

void App::addObject(PrimitiveType type) {
    // Unique default name: box-1, box-2, ...
    int counter = 0;
    std::string name;
    for (;;) {
        name = std::string(primitiveTypeName(type)) + "-" + std::to_string(++counter);
        bool taken = false;
        for (const auto& o : project_.objects) taken |= (o.name == name);
        if (!taken) break;
    }

    SceneObject o;
    o.name = name;
    o.type = type;
    if (type == PrimitiveType::SpawnPoint) {
        o.position[1] = 0.0f;  // marker sits on the ground
        o.color[0] = 0.15f, o.color[1] = 0.9f, o.color[2] = 0.9f;
    }
    project_.objects.push_back(o);
    selectedObject_ = (int)project_.objects.size() - 1;
    commitChange();
}

void App::drawSceneSection() {
    ImGui::SeparatorText("Scene objects");

    if (ImGui::SmallButton("+ Box")) addObject(PrimitiveType::Box);
    ImGui::SameLine();
    if (ImGui::SmallButton("+ Sphere")) addObject(PrimitiveType::Sphere);
    ImGui::SameLine();
    if (ImGui::SmallButton("+ Cylinder")) addObject(PrimitiveType::Cylinder);
    ImGui::SameLine();
    if (ImGui::SmallButton("+ Cone")) addObject(PrimitiveType::Cone);
    ImGui::SameLine();
    if (ImGui::SmallButton("+ Spawn")) addObject(PrimitiveType::SpawnPoint);

    if (project_.objects.empty()) {
        ImGui::TextDisabled("No objects - add a primitive above.");
    } else {
        ImGui::BeginChild("##objects", ImVec2(0, 130), ImGuiChildFlags_Borders);
        for (int i = 0; i < (int)project_.objects.size(); ++i) {
            const SceneObject& o = project_.objects[i];
            std::string label = o.name + "  (" + primitiveTypeName(o.type) + ")##obj" +
                                std::to_string(i);
            if (ImGui::Selectable(label.c_str(), selectedObject_ == i)) selectedObject_ = i;
        }
        ImGui::EndChild();
    }

    if (selectedObject_ < 0 || selectedObject_ >= (int)project_.objects.size()) return;
    SceneObject& o = project_.objects[selectedObject_];

    // Edits apply live; a history snapshot is committed once per finished
    // interaction (slider released, text field defocused...).
    bool committed = false;

    char nameBuf[128];
    std::snprintf(nameBuf, sizeof(nameBuf), "%s", o.name.c_str());
    if (ImGui::InputText("Name", nameBuf, sizeof(nameBuf))) o.name = nameBuf;
    committed |= ImGui::IsItemDeactivatedAfterEdit();

    int typeIdx = (int)o.type;
    const char* typeNames[] = {"Box", "Sphere", "Cylinder", "Cone", "Spawn point"};
    if (ImGui::Combo("Type", &typeIdx, typeNames, 5)) {
        o.type = (PrimitiveType)typeIdx;
        committed = true;
    }

    ImGui::DragFloat3("Position", o.position, 0.1f);
    committed |= ImGui::IsItemDeactivatedAfterEdit();
    ImGui::DragFloat3("Rotation", o.rotation, 1.0f, -360.0f, 360.0f, "%.0f deg");
    committed |= ImGui::IsItemDeactivatedAfterEdit();
    ImGui::DragFloat3("Scale", o.scale, 0.05f, 0.01f, 1000.0f);
    committed |= ImGui::IsItemDeactivatedAfterEdit();
    ImGui::ColorEdit3("Color", o.color);
    committed |= ImGui::IsItemDeactivatedAfterEdit();

    if (ImGui::Button("Delete object")) {
        project_.objects.erase(project_.objects.begin() + selectedObject_);
        selectedObject_ = -1;
        committed = true;
    }

    if (committed) commitChange();
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

void App::drawScriptsSection() {
    ImGui::SeparatorText("Scripts");

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

void App::drawOutputWindow() {
    ImGui::Begin("Output");
    const std::string log = runner_.log();

    if (ImGui::SmallButton("Clear")) runner_.clearLog();
    ImGui::SameLine();
    if (ImGui::SmallButton("Copy all")) ImGui::SetClipboardText(log.c_str());
    ImGui::Separator();

    // Read-only multiline input: text is selectable / copyable with the mouse.
    ImGui::InputTextMultiline("##log", const_cast<char*>(log.c_str()), log.size() + 1,
                              ImVec2(-FLT_MIN, -FLT_MIN), ImGuiInputTextFlags_ReadOnly);

    // Stick to the bottom while new lines arrive, but stop when the user
    // scrolls up (e.g. to select something).
    static size_t lastLogSize = 0;
    static bool stickToBottom = true;
    char childName[128];
    ImFormatString(childName, sizeof(childName), "%s/%s_%08X",
                   ImGui::GetCurrentWindow()->Name, "##log", ImGui::GetID("##log"));
    if (ImGuiWindow* child = ImGui::FindWindowByName(childName)) {
        const bool atBottom = child->Scroll.y >= child->ScrollMax.y - 4.0f;
        if (log.size() == lastLogSize)
            stickToBottom = atBottom;
        else if (stickToBottom)
            ImGui::SetScrollY(child, child->ScrollMax.y);
    }
    lastLogSize = log.size();

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

        ImGui::SeparatorText("Game template");
        const char* templateNames[] = {"Terrain orbit (camera circles the terrain)",
                                       "FPP walkthrough (left stick walk, right stick look)"};
        ImGui::Combo("Template", &newTemplate_, templateNames, 2);

        ImGui::TextDisabled("Creates: %s\\%s", newLocation_, newName_);
        ImGui::TextDisabled("Default scene \"main\" with a flat %d x %d terrain.%s", newWidth_,
                            newDepth_,
                            newTemplate_ == 1 ? " Includes a spawn point." : "");

        if (!newProjectError_.empty())
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", newProjectError_.c_str());

        ImGui::Separator();
        if (ImGui::Button("Create", ImVec2(120, 0))) {
            Project p;
            TerrainConfig t{newWidth_, newDepth_};
            std::string err = project::create(p, newName_, newLocation_, t,
                                              newTemplate_ == 1 ? "fpp" : "orbit");
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
    viewport_.setTerrain(project_.terrain, project_.settings.terrainDetail);
    viewport_.setSkyColor(project_.settings.skyColor);
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
    ImGui::ColorEdit3("Sky color", prefSettings_.skyColor);

    if (prefTemplate_ == 1) {
        ImGui::SeparatorText("FPP camera");
        ImGui::DragFloat("Eye height", &prefSettings_.eyeHeight, 0.05f, 0.2f, 50.0f, "%.2f");
        ImGui::DragFloat("Walk speed", &prefSettings_.walkSpeed, 0.02f, 0.05f, 10.0f, "%.2f");
        ImGui::DragFloat("Look speed", &prefSettings_.lookSpeed, 0.05f, 0.1f, 5.0f, "%.2f");
    } else {
        ImGui::SeparatorText("Orbit camera");
        ImGui::DragFloat("Orbit speed", &prefSettings_.orbitSpeed, 0.05f, 0.0f, 10.0f, "%.2f");
    }

    ImGui::Separator();
    if (ImGui::Button("OK", ImVec2(120, 0))) {
        project_.terrain = prefTerrain_;
        project_.gameTemplate = prefTemplate_ == 1 ? "fpp" : "orbit";
        project_.settings = prefSettings_;
        applyProjectToViewport();
        commitChange();
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(120, 0))) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
}

void App::openProjectDialog() {
    // Projects are opened through their solution file (<name>.tyra).
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
