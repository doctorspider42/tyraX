// Windows that leave the main window - see multiviewport.hpp.
#define IMGUI_DEFINE_MATH_OPERATORS  // ImVec2 arithmetic, this file only
#include "multiviewport.hpp"

#include "gl_loader.h"
#include "platform.hpp"
#include "uiscript.hpp"

#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <stb_image_write.h>  // implementation lives in menubake.cpp

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <sstream>
#include <unordered_map>
#include <vector>

namespace mvp {

namespace {

GLFWwindow* gMain = nullptr;
GLFWdropfun gDrop = nullptr;
void (*gBackendCreate)(ImGuiViewport*) = nullptr;
void (*gBackendRender)(ImGuiViewport*, void*) = nullptr;
std::string gShotBase;  // "" = no capture pending
// Switching the preference off mid-session: ImGui folds every OS window back
// into the main viewport at its desktop position - usually past the main
// window's edge. The next frame brings them in.
bool gBringBackPending = false;

struct PendingUndock {
    std::string name;
    ImVec2 pos, size;
    int frames = 0;
};
std::vector<PendingUndock> gUndock;

// Where each panel was last docked in the main window's dock tree, so "Bring
// all windows back" can put it back there instead of leaving it floating. A
// lone panel dragged out takes its node with it (ImGui merges the emptied
// split away), so the split it lived in is remembered too: the parent node,
// which side, the axis and the share - the parent survives the merge, holding
// the sibling, and can be split again the same way.
struct Home {
    ImGuiID node = 0;
    ImGuiID parent = 0;
    int side = 0;  // 0 = first child (left / top), 1 = second
    ImGuiAxis axis = ImGuiAxis_X;
    float ratio = 0.5f;
};
std::unordered_map<ImGuiID, Home> gHome;

// The root of `node`'s tree, when that tree is the main window's dockspace.
ImGuiDockNode* mainTreeRoot(ImGuiDockNode* node) {
    if (!node) return nullptr;
    ImGuiDockNode* root = ImGui::DockNodeGetRootNode(node);
    if (!root || !root->IsDockSpace()) return nullptr;
    if (root->HostWindow && root->HostWindow->Viewport != ImGui::GetMainViewport()) return nullptr;
    return root;
}

void trackHomes() {
    ImGuiContext& g = *GImGui;
    for (ImGuiWindow* w : g.Windows) {
        if (w->RootWindow != w || !w->DockNode || (w->Flags & ImGuiWindowFlags_DockNodeHost)) continue;
        ImGuiDockNode* node = w->DockNode;
        if (!mainTreeRoot(node)) continue;
        Home h;
        h.node = node->ID;
        if (ImGuiDockNode* parent = node->ParentNode) {
            h.parent = parent->ID;
            h.side = parent->ChildNodes[1] == node ? 1 : 0;
            h.axis = parent->SplitAxis;
            const float total = parent->Size[h.axis];
            h.ratio = total > 0.0f ? ImClamp(node->Size[h.axis] / total, 0.1f, 0.9f) : 0.5f;
        }
        gHome[w->ID] = h;
    }
}

// The node a window goes back to, or 0 (no home: it has never been docked in
// the main window this session, or its whole neighbourhood is gone).
ImGuiID homeNodeFor(ImGuiWindow* w) {
    auto it = gHome.find(w->ID);
    if (it == gHome.end()) return 0;
    const Home& h = it->second;
    if (ImGuiDockNode* n = ImGui::DockBuilderGetNode(h.node))
        if (mainTreeRoot(n) && n->IsLeafNode()) return n->ID;
    // Its node merged away: split the parent again on the same side.
    if (ImGuiDockNode* parent = ImGui::DockBuilderGetNode(h.parent)) {
        if (!mainTreeRoot(parent) || !parent->IsLeafNode()) return 0;
        const ImGuiDir dir = h.axis == ImGuiAxis_X ? (h.side ? ImGuiDir_Right : ImGuiDir_Left)
                                                   : (h.side ? ImGuiDir_Down : ImGuiDir_Up);
        ImGuiID atDir = 0, rest = 0;
        ImGui::DockBuilderSplitNode(parent->ID, dir, h.ratio, &atDir, &rest);
        // The next panel with the same old home joins this new node.
        for (auto& [id, other] : gHome)
            if (other.node == h.node) other.node = atDir;
        return atDir;
    }
    return 0;
}

void createWindow(ImGuiViewport* vp) {
    gBackendCreate(vp);
    GLFWwindow* w = (GLFWwindow*)vp->PlatformHandle;
    if (!w || !gMain) return;
    // The drop callback reads the App from the user pointer; the backend keeps
    // its own per-window data elsewhere (a context map), so this is free.
    glfwSetWindowUserPointer(w, glfwGetWindowUserPointer(gMain));
    if (gDrop) glfwSetDropCallback(w, gDrop);
    // The parent ImGui chose (the main window by default - io.ConfigViewports-
    // NoDefaultParent is off - or the panel a popup was opened from).
    GLFWwindow* owner = gMain;
    if (vp->ParentViewport && vp->ParentViewport->PlatformHandle)
        owner = (GLFWwindow*)vp->ParentViewport->PlatformHandle;
    platform::ownToolWindow(owner, w);
}

// The name a shot file gets for an OS window: the window it shows (for a
// dock node, the visible tab), letters and digits only.
std::string shotName(ImGuiViewport* vp) {
    ImGuiWindow* w = ((ImGuiViewportP*)vp)->Window;
    if (w && w->DockNodeAsHost && w->DockNodeAsHost->VisibleWindow) w = w->DockNodeAsHost->VisibleWindow;
    std::string name = w ? w->Name : "window";
    const size_t hashes = name.find("##");
    if (hashes != std::string::npos && hashes > 0) name = name.substr(0, hashes);
    for (char& c : name)
        if (!std::isalnum((unsigned char)c)) c = '_';
    return name.empty() ? std::string("window") : name;
}

// Renderer_RenderWindow + (when a script asked) a read-back of what it drew,
// before the platform swaps: the window's own context is current here.
void renderWindow(ImGuiViewport* vp, void* arg) {
    gBackendRender(vp, arg);
    if (gShotBase.empty()) return;
    GLFWwindow* win = (GLFWwindow*)vp->PlatformHandle;
    int w = 0, h = 0;
    if (win) glfwGetFramebufferSize(win, &w, &h);
    if (w <= 0 || h <= 0) return;
    std::vector<unsigned char> px((size_t)w * h * 3), flipped((size_t)w * h * 3);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, px.data());
    for (int y = 0; y < h; ++y)
        std::memcpy(&flipped[(size_t)y * w * 3], &px[(size_t)(h - 1 - y) * w * 3], (size_t)w * 3);
    const std::filesystem::path base(gShotBase);
    const std::filesystem::path out =
        base.parent_path() / (base.stem().string() + "-" + shotName(vp) + base.extension().string());
    const bool ok = stbi_write_png(out.string().c_str(), w, h, 3, flipped.data(), w * 3) != 0;
    std::printf(ok ? "[shot] %s (%dx%d, own window at %.0f,%.0f)\n" : "[shot] failed to write %s (%dx%d)\n",
                out.string().c_str(), w, h, vp->Pos.x, vp->Pos.y);
    std::fflush(stdout);
}

ImRect workRect(const ImGuiPlatformMonitor& m) { return ImRect(m.WorkPos, m.WorkPos + m.WorkSize); }

}  // namespace

void install(GLFWwindow* mainWindow, GLFWdropfun drop) {
    gMain = mainWindow;
    gDrop = drop;
    ImGuiPlatformIO& pio = ImGui::GetPlatformIO();
    // createWindow honours viewport->ParentViewport (platform::ownToolWindow),
    // which the stock GLFW backend does not: telling ImGui so makes its z-order
    // reasoning right - every panel's OS window is above its parent - which
    // the TYRAX popup patch in imgui.cpp relies on (a menu that such a window
    // would cover gets an OS window of its own, on top).
    if (pio.Platform_CreateWindow) ImGui::GetIO().BackendFlags |= ImGuiBackendFlags_HasParentViewport;
    if (!gBackendCreate && pio.Platform_CreateWindow) {
        gBackendCreate = pio.Platform_CreateWindow;
        pio.Platform_CreateWindow = createWindow;
    }
    if (!gBackendRender && pio.Renderer_RenderWindow) {
        gBackendRender = pio.Renderer_RenderWindow;
        pio.Renderer_RenderWindow = renderWindow;
    }
}

bool supported() {
    const ImGuiIO& io = ImGui::GetIO();
    return (io.BackendFlags & ImGuiBackendFlags_PlatformHasViewports) &&
           (io.BackendFlags & ImGuiBackendFlags_RendererHasViewports);
}

void setEnabled(bool on) {
    ImGuiIO& io = ImGui::GetIO();
    if (on && supported()) {
        io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
    } else {
        if (enabled() && ImGui::GetFrameCount() > 0) gBringBackPending = true;
        io.ConfigFlags &= ~ImGuiConfigFlags_ViewportsEnable;
    }
}

bool enabled() { return (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable) != 0; }

void renderPlatformWindows(GLFWwindow* mainWindow) {
    // Called every frame: UpdatePlatformWindows also retires the OS windows of
    // a session that just switched the preference off (it returns at once
    // while the flag is clear, and the unused viewports are destroyed - with
    // their windows - two frames later in NewFrame).
    ImGui::UpdatePlatformWindows();
    if (enabled()) {
        ImGui::RenderPlatformWindowsDefault();
        // Every OS window has its own GL context (sharing objects with the main
        // one); the editor's own GL work all assumes the main one is current.
        glfwMakeContextCurrent(mainWindow);
    }
    gShotBase.clear();
}

void requestPlatformShots(const std::string& mainShotPath) { gShotBase = mainShotPath; }

void newFrame() {
    trackHomes();
    if (gBringBackPending) {
        gBringBackPending = false;
        bringWindowsBack();
    }
    for (size_t i = 0; i < gUndock.size();) {
        PendingUndock& p = gUndock[i];
        ImGuiWindow* w = ImGui::FindWindowByName(p.name.c_str());
        // Undocking is queued by the step and done inside NewFrame; the window
        // is moved once it no longer belongs to a node.
        if (w && w->DockNode && ++p.frames < 10) {
            ++i;
            continue;
        }
        if (w && !w->DockNode) {
            ImGui::SetWindowPos(w, p.pos);
            if (p.size.x > 0.0f && p.size.y > 0.0f) ImGui::SetWindowSize(w, p.size);
            ImGui::FocusWindow(w);
        }
        gUndock.erase(gUndock.begin() + (std::ptrdiff_t)i);
    }
}

int bringWindowsBack() {
    ImGuiContext& g = *GImGui;
    ImGuiViewportP* mainVp = (ImGuiViewportP*)(void*)ImGui::GetMainViewport();
    const ImRect work(mainVp->WorkPos, mainVp->WorkPos + mainVp->WorkSize);
    if (work.GetWidth() <= 0.0f || work.GetHeight() <= 0.0f) return 0;
    const float cascade = ImGui::GetFrameHeight() * 1.5f;
    int moved = 0;
    // Where a window goes: one that was already in the main window keeps its
    // place, pushed inside; one from another OS window is cascaded from the
    // work area's top-left. Never bigger than 90% of the work area, so the
    // whole window - title bar and resize grip - is reachable.
    auto place = [&](ImVec2 pos, ImVec2 size, bool inMain, ImVec2& outPos, ImVec2& outSize) {
        outSize = ImMin(size, ImVec2(work.GetWidth() * 0.9f, work.GetHeight() * 0.9f));
        if (!inMain) pos = work.Min + ImVec2(cascade * (float)(moved + 1), cascade * (float)(moved + 1));
        outPos = ImClamp(pos, work.Min, work.Max - outSize);
    };
    // A panel that has a home in the main window's dock tree is docked back
    // there; one that never had one (a tool window opened floating) is moved
    // inside and stays floating.
    auto redock = [&](ImGuiWindow* w) {
        const ImGuiID target = homeNodeFor(w);
        if (!target) return false;
        ImGui::DockBuilderDockWindow(w->Name, target);
        return true;
    };
    // Floating dock nodes (two or more panels in one floating card): each
    // panel goes home; the rest of the card keeps the node, which owns the
    // position (its host window follows).
    for (int n = 0; n < g.DockContext.Nodes.Data.Size; n++) {
        ImGuiDockNode* node = (ImGuiDockNode*)g.DockContext.Nodes.Data[n].val_p;
        if (!node || !node->IsFloatingNode() || !node->HostWindow || !node->IsVisible) continue;
        const bool inMain = node->HostWindow->Viewport == mainVp;
        if (inMain && work.Contains(ImRect(node->Pos, node->Pos + node->Size))) continue;
        std::vector<ImGuiWindow*> panels;
        for (ImGuiWindow* w : g.Windows)
            if (w->RootWindow == w && w->DockNode && ImGui::DockNodeGetRootNode(w->DockNode) == node &&
                !(w->Flags & ImGuiWindowFlags_DockNodeHost))
                panels.push_back(w);
        int home = 0;
        for (ImGuiWindow* w : panels) home += redock(w) ? 1 : 0;
        moved += home;
        if (home == (int)panels.size() && home > 0) continue;
        ImVec2 p, s;
        place(node->Pos, node->Size, inMain, p, s);
        ImGui::DockBuilderSetNodePos(node->ID, p);
        ImGui::DockBuilderSetNodeSize(node->ID, s);
        ++moved;
    }
    // Plain floating windows. NoMove ones are the editor's own frame (the
    // dockspace host, the start page, the bars, the palette).
    for (ImGuiWindow* w : g.Windows) {
        if (w->RootWindow != w || w->DockNode || w->DockNodeAsHost) continue;
        if (w->Flags & (ImGuiWindowFlags_ChildWindow | ImGuiWindowFlags_Popup | ImGuiWindowFlags_Tooltip |
                        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_DockNodeHost))
            continue;
        if (!w->WasActive || w->Hidden) continue;
        const bool inMain = w->Viewport == mainVp;
        if (inMain && work.Contains(w->Rect())) continue;
        if (redock(w)) {
            ++moved;
            continue;
        }
        ImVec2 p, s;
        place(w->Pos, w->SizeFull, inMain, p, s);
        ImGui::SetWindowPos(w, p);
        ImGui::SetWindowSize(w, s);
        ++moved;
    }
    return moved;
}

std::string fitLayoutIni(const std::string& ini, float mainX, float mainY, float mainW, float mainH) {
    if (ini.empty() || mainW <= 0.0f || mainH <= 0.0f) return ini;
    const bool viewports = enabled();
    const ImGuiPlatformIO& pio = ImGui::GetPlatformIO();
    const ImRect mainAbs(ImVec2(mainX, mainY), ImVec2(mainX + mainW, mainY + mainH));
    // Reachable = enough of the top edge (where the title bar / tab strip is)
    // on some screen to grab it.
    const float grabW = 48.0f, grabH = 16.0f;
    auto reachable = [&](const ImRect& abs) {
        const ImRect top(abs.Min, ImVec2(abs.Max.x, abs.Min.y + grabH));
        auto onto = [&](const ImRect& screen) {
            ImRect r = top;
            r.ClipWithFull(screen);
            return r.GetWidth() >= std::min(grabW, top.GetWidth()) && r.GetHeight() >= grabH * 0.5f;
        };
        if (viewports && !pio.Monitors.empty()) {
            for (const ImGuiPlatformMonitor& m : pio.Monitors)
                if (onto(workRect(m))) return true;
            return false;
        }
        return onto(mainAbs);
    };
    // Into the main window, keeping the size if it fits. Returns the new
    // MAIN-RELATIVE position.
    auto fitInMain = [&](ImVec2 relPos, ImVec2& size) {
        size = ImMin(size, ImVec2(mainW * 0.9f, mainH * 0.9f));
        return ImClamp(relPos, ImVec2(0, 0), ImVec2(mainW, mainH) - size);
    };
    auto parsePair = [](const std::string& v, int& a, int& b) {
        return std::sscanf(v.c_str(), "%d,%d", &a, &b) == 2;
    };

    std::vector<std::string> lines;
    {
        std::istringstream in(ini);
        std::string l;
        while (std::getline(in, l)) lines.push_back(l);
    }
    // [Window] sections: one pass collecting the keys of each, a fix at its end.
    struct Win {
        int pos = -1, size = -1, vpPos = -1, vpId = -1, dock = -1, header = -1;
    };
    auto fixWindow = [&](const Win& w) {
        if (w.header < 0 || w.dock >= 0 || w.size < 0) return;  // docked: its node decides
        int px = 0, py = 0, sx = 0, sy = 0, vx = 0, vy = 0;
        if (w.pos >= 0) parsePair(lines[(size_t)w.pos].substr(4), px, py);
        parsePair(lines[(size_t)w.size].substr(5), sx, sy);
        const bool ownWindow = w.vpPos >= 0 && w.vpId >= 0 && parsePair(lines[(size_t)w.vpPos].substr(12), vx, vy);
        ImVec2 size((float)sx, (float)sy);
        ImVec2 rel;
        if (ownWindow) {
            const ImVec2 abs((float)(vx + px), (float)(vy + py));
            if (viewports && reachable(ImRect(abs, abs + size))) return;
            rel = ImVec2(abs.x - mainX, abs.y - mainY);
        } else {
            rel = ImVec2((float)px, (float)py);
            const ImRect relRect(rel, rel + size);
            if (ImRect(ImVec2(0, 0), ImVec2(mainW, mainH)).Contains(relRect)) return;
            // Partly outside the main window: ImGui keeps a sliver of it on
            // screen by itself, and with viewports on gives it its own OS
            // window - only one that cannot be grabbed is moved.
            if (reachable(ImRect(rel + ImVec2(mainX, mainY), rel + ImVec2(mainX, mainY) + size))) return;
        }
        const ImVec2 p = fitInMain(rel, size);
        lines[(size_t)w.size] = "Size=" + std::to_string((int)size.x) + "," + std::to_string((int)size.y);
        const std::string pos = "Pos=" + std::to_string((int)p.x) + "," + std::to_string((int)p.y);
        if (w.pos >= 0)
            lines[(size_t)w.pos] = pos;
        else
            lines[(size_t)w.size] = pos + "\n" + lines[(size_t)w.size];
        if (ownWindow) {  // back in the main window: no viewport of its own
            lines[(size_t)w.vpPos] = "\x01";
            lines[(size_t)w.vpId] = "\x01";
        }
    };

    Win cur;
    bool inWindow = false, inDocking = false;
    for (size_t i = 0; i < lines.size(); ++i) {
        const std::string& l = lines[i];
        if (!l.empty() && l[0] == '[') {
            if (inWindow) fixWindow(cur);
            cur = Win();
            inWindow = l.rfind("[Window][", 0) == 0;
            inDocking = l.rfind("[Docking][", 0) == 0;
            if (inWindow) cur.header = (int)i;
            continue;
        }
        if (inWindow) {
            if (l.rfind("Pos=", 0) == 0) cur.pos = (int)i;
            else if (l.rfind("Size=", 0) == 0) cur.size = (int)i;
            else if (l.rfind("ViewportPos=", 0) == 0) cur.vpPos = (int)i;
            else if (l.rfind("ViewportId=", 0) == 0) cur.vpId = (int)i;
            else if (l.rfind("DockId=", 0) == 0) cur.dock = (int)i;
        } else if (inDocking) {
            // A floating node: a top-level "DockNode" line (a DockSpace is the
            // main window's own; a child node has Parent=). Its Pos is desktop
            // space with viewports on, main-window space with them off.
            const size_t first = l.find_first_not_of(' ');
            if (first != 0 || l.rfind("DockNode", 0) != 0 || l.find(" Parent=") != std::string::npos) continue;
            const size_t pp = l.find(" Pos="), sp = l.find(" Size=");
            if (pp == std::string::npos || sp == std::string::npos) continue;
            int px = 0, py = 0, sx = 0, sy = 0;
            if (!parsePair(l.substr(pp + 5), px, py) || !parsePair(l.substr(sp + 6), sx, sy)) continue;
            ImVec2 size((float)sx, (float)sy);
            const ImVec2 base = viewports ? ImVec2(0, 0) : ImVec2(mainX, mainY);
            const ImVec2 abs = ImVec2((float)px, (float)py) + base;
            if (reachable(ImRect(abs, abs + size))) continue;
            const ImVec2 rel = fitInMain(ImVec2(abs.x - mainX, abs.y - mainY), size);
            const ImVec2 out = viewports ? rel + ImVec2(mainX, mainY) : rel;
            const size_t pe = l.find(' ', pp + 1), se = l.find(' ', sp + 1);
            std::string fixed = l.substr(0, pp) + " Pos=" + std::to_string((int)out.x) + "," + std::to_string((int)out.y);
            fixed += (pe == std::string::npos ? std::string() : l.substr(pe, sp - pe));
            fixed += " Size=" + std::to_string((int)size.x) + "," + std::to_string((int)size.y);
            fixed += (se == std::string::npos ? std::string() : l.substr(se));
            lines[i] = fixed;
        }
    }
    if (inWindow) fixWindow(cur);

    std::string out;
    out.reserve(ini.size());
    for (const std::string& l : lines) {
        if (l == "\x01") continue;
        out += l;
        out += '\n';
    }
    return out;
}

void routeInjectedMouse() {
    ImGuiContext& g = *GImGui;
    if (!(g.IO.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)) return;
    const ImGuiInputEvent* last = nullptr;
    for (const ImGuiInputEvent& e : g.InputEventsQueue)
        if (e.Type == ImGuiInputEventType_MousePos) last = &e;
    // No event = the cursor did not move (ImGui drops a repeated position), so
    // it is still where it was - but the OS window under it may be new (a
    // popup's), and the backend reports none for a cursor that is not real.
    const ImVec2 p = last ? ImVec2(last->MousePos.PosX, last->MousePos.PosY) : g.IO.MousePos;
    if (!ImGui::IsMousePosValid(&p)) return;
    // The OS window under the point: a panel's own window is above the main
    // one (platform::ownToolWindow), and among panels the last focused wins.
    ImGuiViewportP* best = nullptr;
    for (ImGuiViewportP* vp : g.Viewports) {
        if (vp->Flags & (ImGuiViewportFlags_NoInputs | ImGuiViewportFlags_IsMinimized)) continue;
        // Alive last frame is enough: a popup's OS window is only created at
        // the END of its first frame, and the script presses a frame later.
        if (vp->Idx > 0 && vp->LastFrameActive < g.FrameCount - 1) continue;
        if (!vp->GetMainRect().Contains(p)) continue;
        if (!best || (best->Idx == 0 && vp->Idx > 0) ||
            (vp->Idx > 0 && vp->LastFocusedStampCount > best->LastFocusedStampCount))
            best = vp;
    }
    if (best) g.IO.AddMouseViewportEvent(best->ID);
}

std::string scriptStep(const uiscript::Step& s) {
    if (s.kind == uiscript::Step::MainWindow) {
        if (!gMain) return "no main window";
        if (s.w < 200.0f || s.h < 150.0f) return "mainwindow size must be at least 200,150";
        glfwRestoreWindow(gMain);
        glfwSetWindowPos(gMain, (int)s.dx, (int)s.dy);
        glfwSetWindowSize(gMain, (int)s.w, (int)s.h);
        return "";
    }
    ImGuiWindow* w = ImGui::FindWindowByName(s.arg.c_str());
    if (!w) return "no window named '" + s.arg + "' (window names are exact)";
    if (s.kind == uiscript::Step::Undock) {
        const ImGuiViewport* mainVp = ImGui::GetMainViewport();
        PendingUndock p;
        p.name = w->Name;
        p.pos = ImVec2(mainVp->Pos.x + s.dx, mainVp->Pos.y + s.dy);
        p.size = ImVec2(s.w, s.h);
        if (w->DockNode) ImGui::DockContextQueueUndockWindow(GImGui, w);
        gUndock.push_back(p);
        return "";
    }
    // Dock: into the node that holds `path`'s window.
    ImGuiWindow* into = ImGui::FindWindowByName(s.path.c_str());
    if (!into) return "no window named '" + s.path + "' to dock into";
    if (!into->DockNode) return "'" + s.path + "' is not docked anywhere";
    ImGui::DockBuilderDockWindow(w->Name, into->DockNode->ID);
    return "";
}

}  // namespace mvp
