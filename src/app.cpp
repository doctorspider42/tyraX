#include "app.hpp"
#include "app_internal.hpp"

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

#include "aisupport.hpp"
#include "animedit.hpp"
#include "decalproj.hpp"
#include "devsession.hpp"
#include "editorcfg.hpp"
#include "gl_loader.h"
#include "fbxparser.hpp"
#include "glbparser.hpp"
#include "json.hpp"
#include "menubake.hpp"
#include "objparser.hpp"
#include "pngquant.hpp"
#include "uvunwrap.hpp"
#include "savebake.hpp"
#include "stochtile.hpp"
#include "scrollsim.hpp"
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
#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <ImGuizmo.h>
#include <imnodes.h>

#include "icon_gen.hpp"  // kIconPng (built from resources/icon.png)

#include "platform.hpp"

// PickKind + the pick*() wrappers, fileSizeOr0, readTextFileTail,
// sanitizeAssetName, prefHelp and walkSpeedDrag now live in app_internal.hpp -
// the subsystem TUs split out of this file call them too.

// ---------------------------------------------------------------------------
// Global editor config. These are machine/muscle-memory properties (a 4K laptop
// wants a different UI scale than a 1080p desktop; navigation is personal
// preference; the emulator lives at a fixed path on this PC and the dev PS2 has
// a fixed LAN address), so they live outside the per-project .tyra - in
// editor.ini under platform::configDir(). Trivial key=value lines; the whole
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
    // Collaboration sessions: the name shown to other participants (empty =
    // the Windows user name) and where joined projects materialize (empty =
    // <editor config dir>/remote-cache).
    std::string displayName;
    std::string sessionCacheDir;
    // AI assistant backend for flow-graph generation (aigen.hpp). Machine
    // config, not project data: which CLIs/keys exist is a property of this
    // PC. The --ai-graph CLI reads the same keys (main.cpp).
    aigen::Config ai;
    // Material Editor: the preview panel's share of the window width
    // (0.25..0.75), set by dragging the splitter between the columns.
    float matEdSplit = 0.48f;
    // Credits Editor: the preview strip's share of the window HEIGHT
    // (0.15..0.75), set by dragging the splitter above it. Same reasoning as
    // matEdSplit - how much room a preview deserves is a property of the
    // monitor and of what you are doing, not of the project.
    float creditsSplit = 0.42f;
    // Lighting the Material / Animation Editor previews bake with: "" = the
    // scene's ambience, "*" = the neutral studio default, anything else = an
    // ambience preset name. A view preference of this machine, not project
    // data - which is also why a stale name silently falls back to the scene.
    std::string matEdLight;
    std::string animEdLight;
    // Collision-aware placement (docs/object-placement.md): inserted and
    // pasted objects rest on the surface under them instead of sinking into
    // it. A workflow preference like the navigation scheme, so it lives here
    // rather than in the .tyra.
    bool placementSnap = true;
    // The axis-view gizmo in the viewport's top-right corner. On by default;
    // it can be turned off because it sits where HUD authoring wants space.
    bool axisGizmo = true;
    // Phone camera link (docs/phone-camera.md). Which port is free and how
    // much the Wi-Fi here can carry are properties of this machine, so the
    // whole thing is machine config rather than project data. The pairing code
    // persists so a paired phone keeps working across editor restarts.
    phonecam::PreviewPrefs phoneCam;
    int phoneCamPort = (int)phonecam::kDefaultPort;
    std::string phoneCamCode;      // 6 digits; generated on first use
    bool phoneCamRequireCode = true;
    // TV safe-area guides (docs/safe-areas.md) - a viewing aid like axisGizmo,
    // so it belongs to the installation, not to the project.
    bool safeAreaOn = false;
    bool safeFrame = true, safeAction = true, safeTitle = true;
    bool safeCentre = false, safeBothRegions = false;
    int safeAspect = 0;  // 0 follow project, 1 = 4:3, 2 = 16:9
    float safeOpacity = 0.55f;
    // Time machine (docs/time-machine.md): how much RAM the capture history may
    // hold. Machine-global because it is a memory budget for THIS PC, not a
    // property of the game - and it is a ceiling, not an allocation.
    int timeMachineBudgetMb = 128;
    // Interface theme (docs/editor-theme.md), stored as theme::Info::key so a
    // reordered enum cannot repaint everyone's editor. An unknown key (a
    // config written by a newer build) falls back to the default theme.
    std::string theme;
    // Viewport output mode (docs/ps2-viewport.md): false = the editor's own
    // free-aspect image, true = the scene rasterized at the GS framebuffer
    // size of the project's display mode and shown the way a TV shows it.
    // A way of looking at the scene, like the safe areas and the axis gizmo,
    // so it belongs to the installation rather than to the project.
    bool viewportPs2 = false;
    // Toolbar run target: false = the emulator (PCSX2), true = a real console
    // over ps2link. Which machine is on the desk is a property of this PC, not
    // of the game, so it lives here rather than in the .tyra.
    bool runOnPs2 = false;
    // Log panels (docs/log-panels.md): which severity levels the Output and
    // Debug windows show, as a logview level bitmask, plus their selectable-text
    // toggles. Both default to everything visible, so the split changes nothing
    // until someone hides a level - and which noise a person wants hidden is a
    // property of how they work, not of the project.
    unsigned logMaskOutput = logview::kAll;
    unsigned logMaskDebug = logview::kAll;
    bool logSelectOutput = false;
    bool logSelectDebug = false;
    // Project folders opened most recently, most-recent first (the welcome
    // screen's list). Machine-global like everything else here: which projects
    // this PC has seen is a property of the PC, not of any one project.
    std::vector<std::string> recentProjects;
};

// How many entries the recent-projects list keeps. Ten fills the welcome
// screen without scrolling and is about as far back as "continue where I left
// off" is still useful.
static constexpr size_t kMaxRecentProjects = 10;

static std::filesystem::path editorConfigPath() {
    const std::filesystem::path base = platform::configDir();
    if (base.empty()) return {};
    return base / "editor.ini";
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
        else if (match("navDollySens", v)) cfg.nav.dollySensitivity = toF(v, 1.0f);
        else if (match("navZoomSens", v)) cfg.nav.zoomSensitivity = toF(v, 1.0f);
        else if (match("navInvertX", v)) cfg.nav.invertX = toI(v, 0) != 0;
        else if (match("navInvertY", v)) cfg.nav.invertY = toI(v, 0) != 0;
        else if (match("navOrbitSelection", v)) cfg.nav.orbitAroundSelection = toI(v, 1) != 0;
        else if (match("emulatorPath", v)) cfg.emulatorPath = v;
        else if (match("ps2LinkIp", v)) cfg.ps2LinkIp = v;
        else if (match("errorPopup", v)) cfg.errorPopup = toI(v, 1) != 0;
        else if (match("defaultProjectsDir", v)) cfg.defaultProjectsDir = v;
        else if (match("displayName", v)) cfg.displayName = v;
        else if (match("sessionCacheDir", v)) cfg.sessionCacheDir = v;
        else if (match("aiBackend", v)) cfg.ai.backend = v;
        else if (match("aiModel", v)) cfg.ai.model = v;
        else if (match("aiThinking", v)) cfg.ai.thinking = toI(v, 0) != 0;
        else if (match("matEdSplit", v)) cfg.matEdSplit = toF(v, cfg.matEdSplit);
        else if (match("creditsSplit", v))
            cfg.creditsSplit = toF(v, cfg.creditsSplit);
        else if (match("matEdLight", v)) cfg.matEdLight = v;
        else if (match("animEdLight", v)) cfg.animEdLight = v;
        else if (match("placementSnap", v)) cfg.placementSnap = toI(v, 1) != 0;
        else if (match("axisGizmo", v)) cfg.axisGizmo = toI(v, 1) != 0;
        else if (match("phoneCamPort", v)) cfg.phoneCamPort = toI(v, cfg.phoneCamPort);
        else if (match("phoneCamCode", v)) cfg.phoneCamCode = v;
        else if (match("phoneCamRequireCode", v)) cfg.phoneCamRequireCode = toI(v, 1) != 0;
        else if (match("phoneCamMaxW", v)) cfg.phoneCam.maxWidth = toI(v, cfg.phoneCam.maxWidth);
        else if (match("phoneCamMaxH", v)) cfg.phoneCam.maxHeight = toI(v, cfg.phoneCam.maxHeight);
        else if (match("phoneCamFps", v)) cfg.phoneCam.fps = toI(v, cfg.phoneCam.fps);
        else if (match("phoneCamQuality", v)) cfg.phoneCam.quality = toI(v, cfg.phoneCam.quality);
        else if (match("safeAreaOn", v)) cfg.safeAreaOn = toI(v, 0) != 0;
        else if (match("safeFrame", v)) cfg.safeFrame = toI(v, 1) != 0;
        else if (match("safeAction", v)) cfg.safeAction = toI(v, 1) != 0;
        else if (match("safeTitle", v)) cfg.safeTitle = toI(v, 1) != 0;
        else if (match("safeCentre", v)) cfg.safeCentre = toI(v, 0) != 0;
        else if (match("safeBothRegions", v)) cfg.safeBothRegions = toI(v, 0) != 0;
        else if (match("safeAspect", v)) cfg.safeAspect = toI(v, 0);
        else if (match("safeOpacity", v)) cfg.safeOpacity = toF(v, 0.55f);
        else if (match("theme", v)) cfg.theme = v;
        else if (match("viewportPs2", v)) cfg.viewportPs2 = toI(v, 0) != 0;
        else if (match("runOnPs2", v)) cfg.runOnPs2 = toI(v, 0) != 0;
        else if (match("timeMachineBudgetMb", v))
            cfg.timeMachineBudgetMb = toI(v, 128);
        else if (match("phoneCamSmoothing", v))
            cfg.phoneCam.smoothing = toI(v, cfg.phoneCam.smoothing);
        // An out-of-range mask (a config written by a newer editor with more
        // levels) is clamped to the levels this build has, never to zero - an
        // empty mask would look like an empty log.
        else if (match("logMaskOutput", v))
            cfg.logMaskOutput = (unsigned)toI(v, (int)logview::kAll) & logview::kAll;
        else if (match("logMaskDebug", v))
            cfg.logMaskDebug = (unsigned)toI(v, (int)logview::kAll) & logview::kAll;
        else if (match("logSelectOutput", v)) cfg.logSelectOutput = toI(v, 0) != 0;
        else if (match("logSelectDebug", v)) cfg.logSelectDebug = toI(v, 0) != 0;
        // One line per entry, written in list order (most recent first).
        else if (match("recentProject", v)) {
            if (!v.empty() && cfg.recentProjects.size() < kMaxRecentProjects)
                cfg.recentProjects.push_back(v);
        }
    }
    if (cfg.phoneCamPort < 1024 || cfg.phoneCamPort > 65535)
        cfg.phoneCamPort = (int)phonecam::kDefaultPort;
    if (cfg.ai.backend.empty()) cfg.ai.backend = "claude";
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
      << "navDollySens=" << n.dollySensitivity << "\n"
      << "navZoomSens=" << n.zoomSensitivity << "\n"
      << "navInvertX=" << (n.invertX ? 1 : 0) << "\n"
      << "navInvertY=" << (n.invertY ? 1 : 0) << "\n"
      << "navOrbitSelection=" << (n.orbitAroundSelection ? 1 : 0) << "\n"
      << "emulatorPath=" << cfg.emulatorPath << "\n"
      << "ps2LinkIp=" << cfg.ps2LinkIp << "\n"
      << "errorPopup=" << (cfg.errorPopup ? 1 : 0) << "\n"
      << "defaultProjectsDir=" << cfg.defaultProjectsDir << "\n"
      << "displayName=" << cfg.displayName << "\n"
      << "sessionCacheDir=" << cfg.sessionCacheDir << "\n"
      << "aiBackend=" << cfg.ai.backend << "\n"
      << "aiModel=" << cfg.ai.model << "\n"
      << "aiThinking=" << (cfg.ai.thinking ? 1 : 0) << "\n"
      << "matEdSplit=" << cfg.matEdSplit << "\n"
      << "creditsSplit=" << cfg.creditsSplit << "\n"
      << "matEdLight=" << cfg.matEdLight << "\n"
      << "animEdLight=" << cfg.animEdLight << "\n"
      << "placementSnap=" << (cfg.placementSnap ? 1 : 0) << "\n"
      << "axisGizmo=" << (cfg.axisGizmo ? 1 : 0) << "\n"
      << "phoneCamPort=" << cfg.phoneCamPort << "\n"
      << "phoneCamCode=" << cfg.phoneCamCode << "\n"
      << "phoneCamRequireCode=" << (cfg.phoneCamRequireCode ? 1 : 0) << "\n"
      << "phoneCamMaxW=" << cfg.phoneCam.maxWidth << "\n"
      << "phoneCamMaxH=" << cfg.phoneCam.maxHeight << "\n"
      << "phoneCamFps=" << cfg.phoneCam.fps << "\n"
      << "phoneCamQuality=" << cfg.phoneCam.quality << "\n"
      << "safeAreaOn=" << (cfg.safeAreaOn ? 1 : 0) << "\n"
      << "safeFrame=" << (cfg.safeFrame ? 1 : 0) << "\n"
      << "safeAction=" << (cfg.safeAction ? 1 : 0) << "\n"
      << "safeTitle=" << (cfg.safeTitle ? 1 : 0) << "\n"
      << "safeCentre=" << (cfg.safeCentre ? 1 : 0) << "\n"
      << "safeBothRegions=" << (cfg.safeBothRegions ? 1 : 0) << "\n"
      << "safeAspect=" << cfg.safeAspect << "\n"
      << "safeOpacity=" << cfg.safeOpacity << "\n"
      << "theme=" << cfg.theme << "\n"
      << "viewportPs2=" << (cfg.viewportPs2 ? 1 : 0) << "\n"
      << "runOnPs2=" << (cfg.runOnPs2 ? 1 : 0) << "\n"
      << "timeMachineBudgetMb=" << cfg.timeMachineBudgetMb << "\n"
      << "phoneCamSmoothing=" << cfg.phoneCam.smoothing << "\n"
      << "logMaskOutput=" << cfg.logMaskOutput << "\n"
      << "logMaskDebug=" << cfg.logMaskDebug << "\n"
      << "logSelectOutput=" << (cfg.logSelectOutput ? 1 : 0) << "\n"
      << "logSelectDebug=" << (cfg.logSelectDebug ? 1 : 0) << "\n";
    for (const std::string& dir : cfg.recentProjects) f << "recentProject=" << dir << "\n";
}

// The name a project folder shows under, and whether it is still a project at
// all: the <name>.tyra stem (project::load finds the manifest the same way),
// empty when the folder is gone or holds no manifest.
static std::string projectManifestName(const std::string& dir) {
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
        if (entry.is_regular_file(ec) && entry.path().extension() == ".tyra")
            return entry.path().stem().string();
    }
    return "";
}

// Dedupe key for a recent-projects entry: the same folder can arrive spelled
// two ways (the Open dialog and the New Project modal disagree on slashes and
// Windows does not care about case), and the list must not grow a second row
// for a project it already has.
static std::string recentProjectKey(const std::string& dir) {
    std::string k = std::filesystem::path(dir).lexically_normal().string();
    while (!k.empty() && (k.back() == '\\' || k.back() == '/')) k.pop_back();
    for (char& c : k) c = (char)tolower((unsigned char)c);
    return k;
}

// Default parent directory proposed for new projects: the configured global
// default (Edit > Preferences) if set, else ~/TyraProjects.
static std::string defaultNewProjectLocation(const std::string& configured) {
    if (!configured.empty()) return configured;
    const std::filesystem::path home = platform::homeDir();
    return home.empty() ? std::string() : (home / "TyraProjects").string();
}

// Publish this editor's session pointer (devsession.hpp) - what a person or a
// tool needs to answer "which project is live?" without searching the disk.
// Throttled: the contents change slowly, and the point of the heartbeat is
// liveness, not resolution.
void App::publishDevSession() {
    const double now = ImGui::GetTime();
    if (now < devSessionNext_) return;
    devSessionNext_ = now + 4.0;
    devsession::Info i;
    i.pid = devsession::selfPid();
    if (!devSessionStarted_)
        devSessionStarted_ = std::chrono::duration_cast<std::chrono::seconds>(
                                 std::chrono::system_clock::now().time_since_epoch())
                                 .count();
    i.started = devSessionStarted_;
    i.heartbeat = 0;  // devsession stamps it
    if (hasProject_) {
        i.project = project_.dir;
        i.name = project_.name;
        i.scene = project_.scenes.empty() ? "" : project_.active().name;
        i.profile = project_.settings.buildProfile;
        i.liveDebug = project_.settings.liveDebug;
        i.liveLink = project_.settings.liveLink;
        // How the game is reached. The runner drops bin/ps2link.run for a
        // console deploy and removes it for an emulator build, so the marker
        // is the honest answer - and it matters: over ps2link the host file
        // server is a child of THIS editor, so closing the editor freezes
        // every devkit file mid-session.
        std::error_code ec;
        i.transport = std::filesystem::exists(
                          std::filesystem::path(project_.dir) / "bin" / "ps2link.run", ec)
                          ? "ps2link"
                          : "pcsx2";
    }
    i.gameLive = dbgState_ == DbgState::Running || dbgState_ == DbgState::Halted;
    i.gameHalted = dbgState_ == DbgState::Halted;
    i.gameFrame = dbgSnap_.frame;
    devsession::publish(i);
}

// The three bits of editor.ini the CLI needs (editorcfg.hpp). Defined here so
// the parser above stays the only one that knows the file's shape.
namespace editorcfg {
std::string configPath() { return editorConfigPath().string(); }
std::vector<std::string> recentProjects() {
    return loadEditorConfig().recentProjects;
}
std::string defaultProjectsDir() {
    return defaultNewProjectLocation(loadEditorConfig().defaultProjectsDir);
}
}  // namespace editorcfg

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

// The editor's desktop identity. It is one string on purpose: the window's
// Wayland app id, its X11 WM_CLASS, and the basename of the .desktop file and
// the icon platform::installDesktopEntry writes all have to agree, or the
// desktop cannot connect the running window to its icon.
static constexpr const char* kAppId = "tyrax-editor";

// Window/taskbar icon from the baked-in resources/icon.png.
//
// Windows needs nothing here - GLFW's Win32 backend loads the GLFW_ICON
// resource out of the .exe into the window class (resources/app.rc). On X11
// this is what puts the icon on the window; on Wayland there is no protocol
// for it at all, so GLFW reports GLFW_FEATURE_UNAVAILABLE and the icon comes
// from the desktop entry instead - checking the platform first keeps that
// expected case out of the error callback.
static void applyWindowIcon(GLFWwindow* window) {
#ifndef _WIN32
    if (!window || glfwGetPlatform() == GLFW_PLATFORM_WAYLAND) return;
    int w = 0, h = 0, comp = 0;
    stbi_uc* pixels = stbi_load_from_memory(appicon::kIconPng,
                                            (int)appicon::kIconPngSize, &w, &h, &comp, 4);
    if (!pixels) return;
    const GLFWimage image{w, h, pixels};
    glfwSetWindowIcon(window, 1, &image);
    stbi_image_free(pixels);
#else
    (void)window;
#endif
}

// ---------------------------------------------------------------------------

int App::run(const std::string& initialProjectDir) {
    glfwSetErrorCallback([](int code, const char* msg) {
        std::fprintf(stderr, "GLFW error %d: %s\n", code, msg);
    });

    // The desktop entry has to exist before the window does: a Wayland
    // compositor resolves the icon by matching the surface's app id against
    // the installed .desktop files, and it looks exactly once, at map time.
    // No-op on Windows (the icon is a resource inside the .exe).
    platform::installDesktopEntry(
        kAppId, "TyraX", "Edit PS2 scenes and flow graphs, then build and run the game",
        appicon::kIconPng, appicon::kIconPngSize);

    if (!glfwInit()) return 1;

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_SAMPLES, 4);
    glfwWindowHint(GLFW_MAXIMIZED, GLFW_TRUE);
    // What the desktop matches the window against to find the entry above -
    // the app id on Wayland, the WM_CLASS pair on X11. Both hints are accepted
    // (and ignored) by the Win32 backend, so no #ifdef is needed.
    glfwWindowHintString(GLFW_WAYLAND_APP_ID, kAppId);
    glfwWindowHintString(GLFW_X11_CLASS_NAME, kAppId);
    glfwWindowHintString(GLFW_X11_INSTANCE_NAME, kAppId);

    // Size is the restore-size when the user un-maximizes.
    window_ = glfwCreateWindow(1600, 900, "TyraX", nullptr, nullptr);
    if (window_) platform::setDialogOwner(window_);
    if (!window_) {
        glfwTerminate();
        return 1;
    }
    applyWindowIcon(window_);
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
    // The interface font: a proportional face changes every frame height the
    // style metrics are authored against, so it is loaded before the theme.
    // The theme itself is applied further down, once the ImNodes context it
    // also tints exists and the editor config has been read.
    loadUiFont();

    ImGui_ImplGlfw_InitForOpenGL(window_, true);
    ImGui_ImplOpenGL3_Init("#version 330 core");
    ImNodes::CreateContext();
    // One EDITOR context per canvas. imnodes keeps panning/zoom/selection in
    // the editor context, so the Flow Graph and the Procedural graph would
    // otherwise shove each other around; and the context must never be left
    // unset - EditorContextSet(nullptr) makes the next BeginNodeEditor
    // dereference null (a startup crash when both windows are open, found the
    // hard way).
    flowEditorCtx_ = (void*)ImNodes::EditorContextCreate();
    procEditorCtx_ = (void*)ImNodes::EditorContextCreate();

    // Scale the UI for the display: the saved override if any, else auto-match
    // the monitor's content scale (a 4K laptop reports e.g. 2.0). Fonts are
    // rasterized dynamically in this ImGui, so scaling stays crisp.
    {
        const EditorConfig cfg = loadEditorConfig();
        theme_ = theme::fromKey(cfg.theme);
        uiScaleUser_ = cfg.uiScale;
        nav_ = cfg.nav;
        globalEmulatorPath_ = cfg.emulatorPath;
        globalPs2Ip_ = cfg.ps2LinkIp;
        errorPopupEnabled_ = cfg.errorPopup;
        globalDefaultProjectsDir_ = cfg.defaultProjectsDir;
        globalDisplayName_ = cfg.displayName;
        globalSessionCacheDir_ = cfg.sessionCacheDir;
        globalAi_ = cfg.ai;
        matEdSplit_ = cfg.matEdSplit;
        creditsSplit_ = cfg.creditsSplit;
        matEdLight_ = cfg.matEdLight;
        animEdLight_ = cfg.animEdLight;
        placementSnap_ = cfg.placementSnap;
        showAxisGizmo_ = cfg.axisGizmo;
        phoneCamPrefs_ = cfg.phoneCam;
        phoneCamPort_ = cfg.phoneCamPort;
        phoneCamCode_ = cfg.phoneCamCode;
        phoneCamRequireCode_ = cfg.phoneCamRequireCode;
        showSafeArea_ = cfg.safeAreaOn;
        safeArea_.frame = cfg.safeFrame;
        safeArea_.action = cfg.safeAction;
        safeArea_.title = cfg.safeTitle;
        safeArea_.centre = cfg.safeCentre;
        safeArea_.bothRegions = cfg.safeBothRegions;
        safeArea_.aspect = cfg.safeAspect;
        safeArea_.opacity = cfg.safeOpacity;
        viewportPs2_ = cfg.viewportPs2;
        runOnPs2_ = cfg.runOnPs2;
        timeBudgetMb_ = cfg.timeMachineBudgetMb;
        logOut_.mask = cfg.logMaskOutput;
        logDbg_.mask = cfg.logMaskDebug;
        logOut_.selectText = cfg.logSelectOutput;
        logDbg_.selectText = cfg.logSelectDebug;
        // Probe the recent projects once, here: the welcome screen draws this
        // list every frame and must not scan the disk to do it.
        for (const std::string& dir : cfg.recentProjects) {
            RecentProject r;
            r.dir = dir;
            probeRecentProject(r);
            recentProjects_.push_back(r);
        }
    }
    // Colours + style metrics + the scale over them, in that order (applyTheme
    // ends in applyUiScale).
    applyTheme();

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
        openProjectAt(dir);  // failure leaves the welcome screen up
    }

    // UI scripting (docs/ui-scripting.md): collect ImGui's item boxes so a
    // script can name widgets, and stop pacing to the monitor - an unattended
    // run has nobody watching, and every step costs frames.
    if (uiScriptActive_) {
        uiscript::setEnabled(true);
        glfwSwapInterval(0);
        std::printf("[ui] running %zu step(s)\n", uiScript_.size());
        std::fflush(stdout);
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
        // Between the backend and NewFrame on purpose: the script reads the item
        // map the last frame built and queues this frame's input, and a later
        // event overrides whatever the backend just fed from a real cursor.
        uiScriptTick();
        ImGui::NewFrame();

        drawUI();

        // No autosave: layout/docking changes fold into the .tyra only when
        // the user saves the project (Save / Ctrl+S). io.WantSaveIniSettings
        // is left for the next explicit saveProject() to pick up.

        publishDevSession();  // "this editor has that project open" (throttled)

        ImGui::Render();
        int w, h;
        glfwGetFramebufferSize(window_, &w, &h);
        glViewport(0, 0, w, h);
        glClearColor(0.08f, 0.08f, 0.10f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        captureFrameIfRequested(w, h);
        if (!uiShotPath_.empty()) {  // a script's shot step, after its frame drew
            captureFrameTo(uiShotPath_, w, h);
            uiShotPath_.clear();
        }

        glfwSwapBuffers(window_);
    }

    // No save on exit: the user chose to discard (or had nothing unsaved).

    // Let go of the Remote Pad. Closing the editor while a button was held used
    // to leave livepad.bin saying "attached, Cross down" forever - the game's
    // staleness watchdog covers a running game, but the leftover file also reads
    // as a held button to anything inspecting it, and that made a UI-script test
    // look like it had a stuck button for 12 seconds when the hold was 4.
    if (padAttached_) {
        showRemotePad_ = false;
        remotePadTick();  // the detach path: neutral state, attached flag cleared
    }

    // Audio first: the device callback holds the LiveSynth, and a running
    // render thread writes into droneRenderResult_. Both must be done before
    // anything they touch is destroyed.
    droneAudition(false);
    droneRenderCancel_.store(true);
    if (droneRenderThread_.joinable()) droneRenderThread_.join();

    devsession::retire(devsession::selfPid());  // stop claiming to be live
    viewport_.shutdown();
    if (flowEditorCtx_) ImNodes::EditorContextFree((ImNodesEditorContext*)flowEditorCtx_);
    if (procEditorCtx_) ImNodes::EditorContextFree((ImNodesEditorContext*)procEditorCtx_);
    flowEditorCtx_ = procEditorCtx_ = nullptr;
    ImNodes::DestroyContext();
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window_);
    glfwTerminate();
    return uiScriptFailed_ ? 1 : 0;  // a UI script gates a scripted run
}

// Framebuffer self-capture, enabled by the TYRAX_SHOT environment variable
// (a directory; TYRAX_SHOT_EVERY overrides the 2-second interval). The editor
// reads back its own window and writes <dir>/shotNN.png.
//
// This exists because it is the ONLY screenshot path that works everywhere: a
// Wayland compositor may refuse external capture outright (GNOME denies the
// org.gnome.Shell.Screenshot call to unsanctioned clients, and there is no X11
// window to grab when GLFW picks the Wayland backend), and it captures what the
// editor DREW rather than what the compositor presented - which also survives
// the AMD present quirk that leaves the window blank on some machines. Used to
// verify UI work without a human at the keyboard; see the tyra-testing skill.
void App::captureFrameIfRequested(int w, int h) {
    const char* dir = getenv("TYRAX_SHOT");
    if (!dir || !*dir || w <= 0 || h <= 0) return;
    static double next = 1.0;
    static int shotNo = 0;
    static double every = 0.0;
    if (every == 0.0) {
        const char* e = getenv("TYRAX_SHOT_EVERY");
        every = e ? atof(e) : 2.0;
        if (every < 0.1) every = 0.1;
    }
    if (glfwGetTime() < next) return;
    next = glfwGetTime() + every;

    char path[512];
    std::snprintf(path, sizeof(path), "%s/shot%02d.png", dir, shotNo++);
    captureFrameTo(path, w, h);
}

// The read-back itself, also used by a UI script's `shot` step.
bool App::captureFrameTo(const std::string& path, int w, int h) {
    if (w <= 0 || h <= 0) return false;
    std::vector<unsigned char> px((size_t)w * h * 3);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, px.data());
    // GL reads bottom-up; PNG is top-down.
    std::vector<unsigned char> flipped((size_t)w * h * 3);
    for (int y = 0; y < h; ++y)
        std::memcpy(&flipped[(size_t)y * w * 3], &px[(size_t)(h - 1 - y) * w * 3],
                    (size_t)w * 3);
    const bool ok =
        stbi_write_png(path.c_str(), w, h, 3, flipped.data(), w * 3) != 0;
    std::printf(ok ? "[shot] %s (%dx%d)\n" : "[shot] failed to write %s (%dx%d)\n",
                path.c_str(), w, h);
    std::fflush(stdout);
    return ok;
}

void App::drawUI() {
    ImGuizmo::BeginFrame();
    ImGuiID dockspace = ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport());

    // Terrain-layer edits (material/tint/Size/add/remove) rebuild the layer
    // passes once per frame, before the viewport renders below. Brush strokes
    // don't come through here - they rebuild only the chunks under the brush.
    if (splatPreviewDirty_ && hasProject_) {
        splatPreviewDirty_ = false;
        rebakeSplatPreview();
    }

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

    // Read what the running game reports about its flow graphs, and push
    // breakpoint / halt / step commands back to it (throttled).
    livedbgTick();
    livetimeTick();
    remotePadTick();  // the editor holds the controller (docs/remote-pad.md)

    // Hot-patch edited flow graphs into the running game (throttled; writes
    // only when the compiled program actually changed).
    liveLogicTick();

    // Drain collaboration-session events (peer joins/leaves, sync completion,
    // session end) - the only place session state meets project_/ImGui.
    sessionTick();

    // Drain the phone camera link (poses, phone-side buttons, connect/drop) and
    // re-bake a running recording. Same contract as sessionTick: the only place
    // link data meets project_/ImGui.
    phoneCamTick();

    drawMenuBar();
    drawViewportWindow();
    drawProjectWindow();
    drawPropertiesWindow();
    drawFlowGraphWindow();
    drawOutputWindow();
    drawDebugWindow();
    drawDiscLayoutWindow();
    drawMenusWindow();
    drawSaveEditorWindow();
    drawGradingWindow();
    drawAmbienceWindow();
    drawCutsceneWindow();
    drawMaterialEditorWindow();
    drawTerrainWindow();
    drawUiEditorWindow();
    drawFontManagerWindow();
    drawInputMapWindow();
    drawAssetBrowserWindow();
    drawTreeGeneratorWindow();
    drawProceduralWindow();
    drawPrefabsWindow();
    drawDroneGeneratorWindow();
    giBakerPoll();
    drawLoadingScreenWindow();
    drawCreditsWindow();
    drawAnimEditorWindow();
    drawDebuggerWindow();
    drawRemotePadWindow();
    drawSessionWindow();
    drawPhoneCamWindow();
    drawNewProjectModal();
    drawPreferencesModal();
    drawEditorPreferencesModal();
    drawAiGenerateModal();
    drawErrorModal();
    drawNavigationModal();
    drawScenePreferencesModal();
    drawNewScriptModal();
    drawNewSceneModal();
    drawDeleteSceneModal();
    drawDeleteAssetModal();
    drawModelSizeModal();
    drawDiscardModal();
    drawLayoutModals();
    drawHostSessionModal();
    drawJoinSessionModal();
    drawSessionEndedModal();

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
            runner_.buildAndRun(projectForBuild(), true);
        if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_F5))
            runner_.runEmulatorOnly(project_);
        if (ps2Ready && ImGui::IsKeyChordPressed(ImGuiKey_F6))
            runner_.buildAndRunPs2(projectForBuild(), true);
        if (ps2Ready && ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_F6))
            runner_.buildAndRunPs2(projectForBuild(), false);
        if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_B))
            runner_.buildAndRun(projectForBuild(), false);
    }
    // Debugger shortcuts (docs/live-debugger.md). F9 opens the panel; the
    // transport keys only mean something while a game is reporting, and they
    // are deliberately the ones every debugger uses: F5-family for run/pause
    // is taken by "Build && Run" here, so pause/step live on F10/F11.
    if (hasProject_) {
        if (ImGui::IsKeyChordPressed(ImGuiKey_F9)) showDebugger_ = true;
        if (dbgState_ == DbgState::Running || dbgState_ == DbgState::Halted) {
            if (ImGui::IsKeyChordPressed(ImGuiKey_F10)) {
                dbgCmd_.halt = dbgState_ != DbgState::Halted;
                dbgCmd_.stepFrames = 0;
                dbgCmd_.stepUntilFire = false;
                dbgCmdWritten_ = false;
                dbgScrub_ = -1;
            }
            if (ImGui::IsKeyChordPressed(ImGuiKey_F11)) {
                dbgCmd_.halt = false;
                dbgCmd_.stepFrames = 1;
                dbgCmd_.stepUntilFire = false;
                dbgCmdWritten_ = false;
                dbgScrub_ = -1;
            }
        }
    }
    if (hasProject_) {
        // Ctrl+S is host-only in a joined session (the client's disk copy is
        // the sync cache; the host owns persistence).
        if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_S) &&
            session_.role() != session::Session::Role::Client)
            saveAll("Saved");
        if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Comma)) {
            prefTerrain_ = project_.active().terrain;
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
    // ImGuiStyle's constructor leaves FontSizeBase at 0 ("ask the font atlas on
    // the first frame"), and baseStyle_ carries that zero - so restore the size
    // loadUiFont() picked. Left at 0 when no system face resolved, which is
    // exactly what makes ImGui fall back to the built-in font's own size.
    style.FontSizeBase = uiFontSize_;
    if (uiScaleApplied_ != scale) {
        // The Flow Graph canvas lays its nodes out in scaled grid space (see
        // drawFlowGraphWindow), so the positions it pushed into imnodes belong
        // to the old scale - re-push them or the graph draws at the new node
        // size with the old spacing.
        flowPositionsApplied_ = false;
    }
    uiScaleApplied_ = scale;
}

// Writes the whole global editor config (editor.ini) from the App members.
// All save sites funnel through here so no field is dropped - a bare
// {uiScaleUser_, nav_} would wipe the emulator path / PS2 IP on the next
// UI-scale or navigation change.
void App::saveGlobalConfig() {
    std::vector<std::string> recent;
    recent.reserve(recentProjects_.size());
    for (const RecentProject& r : recentProjects_) recent.push_back(r.dir);
    saveEditorConfig({uiScaleUser_, nav_, globalEmulatorPath_, globalPs2Ip_,
                      errorPopupEnabled_, globalDefaultProjectsDir_,
                      globalDisplayName_, globalSessionCacheDir_, globalAi_,
                      matEdSplit_, creditsSplit_, matEdLight_, animEdLight_,
                      placementSnap_, showAxisGizmo_,
                      phoneCamPrefs_, phoneCamPort_, phoneCamCode_,
                      phoneCamRequireCode_, showSafeArea_, safeArea_.frame,
                      safeArea_.action, safeArea_.title, safeArea_.centre,
                      safeArea_.bothRegions, safeArea_.aspect,
                      safeArea_.opacity, timeBudgetMb_,
                      theme::info(theme_).key, viewportPs2_, runOnPs2_,
                      logOut_.mask, logDbg_.mask, logOut_.selectText,
                      logDbg_.selectText, std::move(recent)});
}

void App::setUiScale(float userScale) {
    uiScaleUser_ = userScale;  // 0 == auto (follow the display DPI)
    applyUiScale();
    saveGlobalConfig();
}

// The interface font (docs/editor-theme.md). ImGui's built-in bitmap face is
// the loudest "this is a debug overlay" signal the editor gives off, so the
// first system UI font this machine has is loaded over it. Called once, before
// the first frame; fonts are rasterized dynamically in this ImGui, so the UI
// scale re-bakes the glyphs on its own and there is no atlas to rebuild here.
void App::loadUiFont() {
    // 15 px against ProggyClean's 13: a proportional face needs the extra pixel
    // to read as well at the same nominal size, and the whole UI is authored in
    // scaled() multiples of it rather than in absolute rows.
    constexpr float kUiFontSize = 15.0f;
    for (const platform::SystemFont& f : platform::uiFontFiles()) {
        const std::string path = platform::systemFontPath(f.file);
        if (path.empty()) continue;
        if (!ImGui::GetIO().Fonts->AddFontFromFileTTF(path.c_str(), kUiFontSize))
            continue;  // present but unreadable - try the next face
        uiFontSize_ = kUiFontSize;
        uiFontLabel_ = f.label;
        return;
    }
    // Nothing resolved: the built-in font stays, and uiFontSize_ 0 leaves
    // FontSizeBase alone so ImGui keeps that font's own size.
    std::printf("[ui] no system UI font found - using the built-in font\n");
}

// Colours + metrics + the DPI scale over them. One function because baseStyle_
// IS the themed style: the scale path resets to it every time, so a theme that
// only wrote ImGui::GetStyle() would be undone by the next zoom step.
void App::applyTheme() {
    ImGuiStyle themed;  // fresh ImGui defaults for everything the theme leaves
    theme::apply(theme_, themed);
    baseStyle_ = themed;
    theme::applyImNodes();  // the two node canvases follow the palette
    applyUiScale();
}

void App::setTheme(theme::Id id) {
    theme_ = id;
    applyTheme();
    saveGlobalConfig();
}

void App::drawMenuBar() {
    if (ImGui::BeginMainMenuBar()) {
        // Wordmark: the accent colour, once, where every application puts its
        // name. It is not a menu (no popup, no hover highlight) - it is what
        // turns a bare row of menus into the top of a product.
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + scaled(4.0f));
        ImGui::TextColored(theme::semantics().accent, "TyraX");
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + scaled(6.0f));
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("New Project...", "Ctrl+N")) requestNewProject();
            if (ImGui::MenuItem("Open Project...", "Ctrl+O")) requestOpenProject();
            drawRecentProjectsMenu();
            ImGui::Separator();
            const bool sessionClient =
                session_.role() == session::Session::Role::Client;
            if (ImGui::MenuItem("Save", "Ctrl+S", false, hasProject_ && !sessionClient))
                saveAll("Saved");
            if (sessionClient && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                ImGui::SetTooltip(
                    "You joined this project as a session participant -\n"
                    "the HOST owns saving and committing. Leave the session\n"
                    "to keep your local copy and save it yourself.");
            // No keyboard shortcut on purpose: Ctrl+W is one slip away from the
            // W that flies the viewport camera, and this throws the project out
            // of the editor.
            if (ImGui::MenuItem("Close Project", nullptr, false, hasProject_))
                requestCloseProject();
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
            // A pending paste is still following the cursor - the same command
            // settles it (see pasteObject).
            const char* pasteLabel = pastePending_ ? "Place paste"
                                     : clipboard_.size() > 1 ? "Paste objects"
                                                             : "Paste object";
            if (ImGui::MenuItem(copyLabel, "Ctrl+C", false, objectSelected)) copyObject();
            if (ImGui::MenuItem(pasteLabel, "Ctrl+V", false,
                                hasProject_ && (pastePending_ || !clipboard_.empty())))
                pasteObject();
            ImGui::Separator();
            if (ImGui::MenuItem("Preferences...")) {
                snprintf(prefEmulatorPath_, sizeof(prefEmulatorPath_), "%s",
                         globalEmulatorPath_.c_str());
                snprintf(prefPs2Ip_, sizeof(prefPs2Ip_), "%s", globalPs2Ip_.c_str());
                snprintf(prefDefaultProjectsDir_, sizeof(prefDefaultProjectsDir_), "%s",
                         globalDefaultProjectsDir_.c_str());
                snprintf(prefDisplayName_, sizeof(prefDisplayName_), "%s",
                         globalDisplayName_.c_str());
                snprintf(prefSessionCacheDir_, sizeof(prefSessionCacheDir_), "%s",
                         globalSessionCacheDir_.c_str());
                prefAiBackend_ = 0;
                {
                    const auto ids = aigen::backendIds();
                    for (int i = 0; i < (int)ids.size(); ++i)
                        if (globalAi_.backend == ids[i]) prefAiBackend_ = i;
                }
                snprintf(prefAiModel_, sizeof(prefAiModel_), "%s",
                         globalAi_.model.c_str());
                prefAiCustomModel_ = true;
                for (const char* m : aigen::modelPresets(globalAi_.backend))
                    if (globalAi_.model == m) prefAiCustomModel_ = false;
                prefAiThinking_ = globalAi_.thinking;
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
            // The one navigation setting worth reaching without opening a
            // modal: it changes what a click DOES to the camera, so it gets
            // toggled far more often than sensitivities are tuned. Same field
            // the Navigation controls popup edits - one setting, two doors.
            if (ImGui::MenuItem("Orbit around selected object", nullptr,
                                nav_.orbitAroundSelection)) {
                nav_.orbitAroundSelection = !nav_.orbitAroundSelection;
                saveGlobalConfig();
                navFocusedIndex_ = -1;  // re-snap next frame if something is selected
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(
                    "Selecting an object moves the camera pivot onto it, so a\n"
                    "right-drag orbits around that object instead of the terrain\n"
                    "centre. Pan, zoom and the forward drag still move freely.");
            if (ImGui::MenuItem("Navigation controls...")) openNavigationPopup_ = true;

            // Theme next to the interface scale: both are "how the editor
            // looks on THIS machine", both live in editor.ini, and a picker
            // buried in Preferences is one nobody finds. Also in
            // Edit > Preferences > Appearance.
            if (ImGui::BeginMenu("Theme")) {
                for (int i = 0; i < (int)theme::Id::Count; ++i) {
                    const theme::Info& ti = theme::info((theme::Id)i);
                    if (ImGui::MenuItem(ti.label, nullptr, theme_ == (theme::Id)i))
                        setTheme((theme::Id)i);
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", ti.desc);
                }
                ImGui::EndMenu();
            }

            ImGui::Separator();
            ImGui::TextDisabled("Viewport output");
            {
                // Two ways of looking at the same scene: the editor's own
                // image, which fills whatever shape the viewport is docked to,
                // or the console's - rasterized at the GS framebuffer size of
                // the project's display mode and fitted into the TV's picture.
                struct OutItem {
                    bool ps2;
                    const char* label;
                    const char* help;
                };
                static const OutItem outs[] = {
                    {false, "Editor",
                     "The viewport's own image: full resolution, and as wide as\n"
                     "you docked the panel."},
                    {true, "PS2 output (GS)",
                     "What the console draws: the scene rasterized at the GS\n"
                     "framebuffer size of Preferences > Display mode, then\n"
                     "fitted into the 4:3 / 16:9 picture a TV shows. Pixels are\n"
                     "the console's, and so is the framing."},
                };
                for (const OutItem& it : outs) {
                    if (ImGui::MenuItem(it.label, nullptr, viewportPs2_ == it.ps2,
                                        hasProject_) &&
                        viewportPs2_ != it.ps2) {
                        viewportPs2_ = it.ps2;
                        saveGlobalConfig();
                    }
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", it.help);
                }
                if (viewportPs2_ && hasProject_) {
                    const Viewport::Ps2Output o = ps2ViewportOutput();
                    ImGui::TextDisabled("  %dx%d, %s", o.bufW, o.bufH,
                                        project_.settings.widescreen ? "16:9" : "4:3");
                }
            }

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
            ImGui::TextDisabled("Projection");
            {
                // Perspective + the parallel views. The axis entries also aim
                // the camera down that world axis; orbiting afterwards drops
                // the lock and returns to whichever free mode was in use
                // before the axis view was picked.
                struct ProjItem {
                    Viewport::Projection p;
                    const char* label;
                    const char* shortcut;
                };
                static const ProjItem items[] = {
                    {Viewport::Projection::Perspective, "Perspective", "Num 5"},
                    {Viewport::Projection::Ortho, "Orthographic", "Num 5"},
                    {Viewport::Projection::OrthoTop, "Top (-Y)", "Num 7"},
                    {Viewport::Projection::OrthoBottom, "Bottom (+Y)", "Ctrl+Num 7"},
                    {Viewport::Projection::OrthoFront, "Front (-Z)", "Num 1"},
                    {Viewport::Projection::OrthoBack, "Back (+Z)", "Ctrl+Num 1"},
                    {Viewport::Projection::OrthoRight, "Right (-X)", "Num 3"},
                    {Viewport::Projection::OrthoLeft, "Left (+X)", "Ctrl+Num 3"},
                };
                for (const ProjItem& it : items) {
                    const bool active = viewport_.projection() == it.p;
                    if (ImGui::MenuItem(it.label, it.shortcut, active, hasProject_) &&
                        !active)
                        setViewProjection(it.p);
                }
                if (ImGui::MenuItem("Axis gizmo", nullptr, showAxisGizmo_)) {
                    showAxisGizmo_ = !showAxisGizmo_;
                    saveGlobalConfig();
                }
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                    ImGui::SetTooltip(
                        "The axis widget in the viewport's top-right corner:\n"
                        "click an axis ball to snap to that orthographic view,\n"
                        "click the hub to switch perspective/parallel.");
            }

            ImGui::Separator();
            ImGui::TextDisabled("Placement");
            if (ImGui::MenuItem("Snap to surface", nullptr, placementSnap_)) {
                placementSnap_ = !placementSnap_;
                saveGlobalConfig();
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                ImGui::SetTooltip(
                    "Inserted and pasted objects rest on the surface under "
                    "them\n(the terrain, or the top of the object below) "
                    "instead of\nsinking into it. A machine setting, not "
                    "project data.");

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
            if (ImGui::MenuItem("Procedural preview", nullptr, showProcPreview_,
                                hasProject_))
                showProcPreview_ = !showProcPreview_;
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                ImGui::SetTooltip(
                    "Draw what the procedural volumes generate. Turn it off to "
                    "work on\nwhat is underneath - a finished forest hides the "
                    "ground it grows on.\nThe graph keeps being evaluated, so "
                    "the budget readout and the seed\nsimulator stay live; only "
                    "the geometry goes.");
            if (ImGui::MenuItem("Scroller preview", nullptr, showScrollerPreview_,
                                hasProject_))
                showScrollerPreview_ = !showScrollerPreview_;
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                ImGui::SetTooltip(
                    "Draw the sliding ghost copies of EVERY endless scroller's\n"
                    "segments. Turn it off to work on the member objects the\n"
                    "copies are made of - a running belt fills its whole window\n"
                    "with them. One belt at a time is 'Show belt preview' in\n"
                    "that scroller's Properties. The belt markers stay either\n"
                    "way, and the clone count and warnings stay live.");

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
            if (ImGui::MenuItem("Drop to floor", "End", false, !selection_.empty()))
                dropSelectionToFloor();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                ImGui::SetTooltip(
                    "Rest every selected object on the first surface below it "
                    "-\nthe terrain or the top of another object.");
            ImGui::Separator();
            if (ImGui::MenuItem("Scene Preferences...")) openScenePreferences();
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Project", hasProject_)) {
            const bool busy = runner_.busy();
            if (ImGui::MenuItem("Preferences...", "Ctrl+,")) {
                prefTerrain_ = project_.active().terrain;
                prefSettings_ = project_.settings;
                openPreferencesPopup_ = true;
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Export PS2 ISO", nullptr, false, !busy))
                runner_.exportIso(projectForBuild());
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
                runner_.buildAndRun(projectForBuild(), false);
            if (ImGui::MenuItem("Rebuild", nullptr, false, !busy))
                runner_.buildAndRun(projectForBuild(), false, true);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                ImGui::SetTooltip(
                    "Full rebuild: recreates the build container, then compiles "
                    "the engine\n(VU1 microprograms included) and every game "
                    "source from scratch.\nSlow on purpose - it is the way out "
                    "when an incremental build\nmisbehaves. Clean also wipes "
                    "bin\\ on this machine; Rebuild does not.");
            if (ImGui::MenuItem("Build && Run in PCSX2", "F5", false, !busy))
                runner_.buildAndRun(projectForBuild(), true);
            if (ImGui::MenuItem("Run in PCSX2 (no build)", "Ctrl+F5", false, !busy))
                runner_.runEmulatorOnly(project_);
            ImGui::Separator();
            const bool ps2Ready = !project_.ps2LinkIp.empty();
            if (ImGui::MenuItem("Build && Run on PS2", "F6", false, !busy && ps2Ready))
                runner_.buildAndRunPs2(projectForBuild(), true);
            if (ImGui::MenuItem("Run on PS2 (no build)", "Ctrl+F6", false, !busy && ps2Ready))
                runner_.buildAndRunPs2(projectForBuild(), false);
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
            if (ImGui::MenuItem("Live Debugger", nullptr,
                                project_.settings.liveDebug)) {
                project_.settings.liveDebug = !project_.settings.liveDebug;
                commitChange();
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                ImGui::SetTooltip(
                    "Debug builds report every flow-graph node they run, and "
                    "take breakpoint,\n"
                    "pause/step and force-fire commands from "
                    "the editor (docs/live-debugger.md).\n"
                    "Open the panel with "
                    "Tools > Debugger (F9) or the DBG chip in the toolbar.");
            if (ImGui::MenuItem("Live Logic", nullptr,
                                project_.settings.liveLogic)) {
                project_.settings.liveLogic = !project_.settings.liveLogic;
                commitChange();
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                ImGui::SetTooltip(
                    "Edit a flow graph and the RUNNING game changes - no "
                    "rebuild.\nThe editor compiles the graph itself and a debug "
                    "build interprets it;\ngraphs the interpreter cannot "
                    "express are named in the Debugger's Logic tab\n"
                    "(docs/live-logic.md).");
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

        if (ImGui::BeginMenu("Session")) {
            const bool active = session_.active();
            if (ImGui::MenuItem("Host Session...", nullptr, false,
                                hasProject_ && !active))
                openHostSessionPopup_ = true;
            if (ImGui::MenuItem("Join Session...", nullptr, false, !active))
                requestJoinSession();
            ImGui::Separator();
            if (ImGui::MenuItem("Session Window", nullptr, showSessionWindow_, active))
                showSessionWindow_ = !showSessionWindow_;
            if (ImGui::MenuItem(session_.role() == session::Session::Role::Client
                                    ? "Leave Session"
                                    : "Close Session",
                                nullptr, false, active))
                closeSession();
            ImGui::EndMenu();
        }

        if (hasProject_ && ImGui::BeginMenu("Tools")) {
            if (ImGui::MenuItem("Asset Browser...")) {
                showAssetBrowser_ = true;
                scanAssetTree();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Material Editor...")) showMaterialEditor_ = true;
            if (ImGui::MenuItem("Terrain Editor...")) showTerrainEditor_ = true;
            if (ImGui::MenuItem("Menu Editor...")) showMenusEditor_ = true;
            if (ImGui::MenuItem("Save Editor...")) showSaveEditor_ = true;
            if (ImGui::MenuItem("Color Grading...")) showGradingEditor_ = true;
            if (ImGui::MenuItem("Ambience Editor...")) showAmbienceEditor_ = true;
            if (ImGui::MenuItem("Cutscene Director...")) showCutsceneEditor_ = true;
            if (ImGui::MenuItem("Animation Editor...")) showAnimEditor_ = true;
            if (ImGui::MenuItem("UI Editor...")) showUiEditor_ = true;
            if (ImGui::MenuItem("Font Manager...")) showFontManager_ = true;
            if (ImGui::MenuItem("Input Map...")) showInputMap_ = true;
            if (ImGui::MenuItem("Loading Screens...")) showLoadingEditor_ = true;
            if (ImGui::MenuItem("Credits Editor...")) showCreditsEditor_ = true;
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(
                    "End credits: headings, role/name pairs, images and page\n"
                    "breaks, imported from a text file if you like, scrolling\n"
                    "over music with a skip button and somewhere to go after.");
            ImGui::Separator();
            if (ImGui::MenuItem("Debugger...", "F9")) showDebugger_ = true;
            if (ImGui::MenuItem("Remote Pad...")) showRemotePad_ = true;
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(
                    "Hold the running game's controller from here - click the\n"
                    "buttons or drive it with the editor's keyboard. PCSX2 does\n"
                    "not need the focus, and the same channel is scriptable\n"
                    "(tyrax-editor --pad). Debug builds only.");
            ImGui::Separator();
            if (ImGui::MenuItem("Tree Generator...")) {
                showTreeGenerator_ = true;
                treePreviewDirty_ = true;
            }
            if (ImGui::MenuItem("Prefabs...")) showPrefabs_ = true;
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(
                    "Reusable groups of objects - a hut, a room, a lamp post\n"
                    "with its light and its script. Stamp them by hand, scatter\n"
                    "them with a procedural graph, or spawn them at runtime.");
            if (ImGui::MenuItem("Procedural...")) showProcedural_ = true;
            if (ImGui::MenuItem("Drone Generator...")) showDroneGenerator_ = true;
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(
                    "Ambient / drone music generator: audition a patch live,\n"
                    "render it into res/audio as a looping background track.");
            if (ImGui::MenuItem("Phone Camera...")) showPhoneCamWindow_ = true;
            ImGui::Separator();
            // Lives in the Ambience Editor now; the menu item still works
            // and simply opens that window on its GI tab.
            if (ImGui::MenuItem("Bake Global Illumination...")) {
                showAmbienceEditor_ = true;
                showGiBake_ = true;
            }
            ImGui::EndMenu();
        }

        drawToolbar();

        if (!statusMessage_.empty()) {
            const float w = ImGui::CalcTextSize(statusMessage_.c_str()).x;
            ImGui::SameLine(ImGui::GetWindowWidth() - w - 16.0f);
            ImGui::TextDisabled("%s", statusMessage_.c_str());
        }
        // A hairline along the bottom edge of the bar, in the accent colour.
        // The menu bar and the dockspace under it are both dark surfaces with
        // no border between them, so without this the whole top of the window
        // reads as one undifferentiated block.
        {
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const ImVec2 p = ImGui::GetWindowPos();
            const float y = p.y + ImGui::GetWindowHeight() - scaled(1.0f);
            dl->AddRectFilled(
                ImVec2(p.x, y),
                ImVec2(p.x + ImGui::GetWindowWidth(), y + scaled(1.0f)),
                ImGui::GetColorU32(theme::semantics().accentMuted));
        }
        ImGui::EndMainMenuBar();
    }
}

// Runs the toolbar's selected target (runOnPs2_). One place, so the Play
// button, its dropdown and anything else that grows later cannot disagree
// about what "Run" means.
void App::runSelectedTarget(bool build) {
    if (runOnPs2_) runner_.buildAndRunPs2(projectForBuild(), build);
    else if (build) runner_.buildAndRun(projectForBuild(), true);
    else runner_.runEmulatorOnly(project_);
}

// Icon toolbar drawn inline in the main menu bar, after the menus. Layout:
// Save, Build, then ONE run group - [Play, dropdown, Stop] - driving whichever
// target the dropdown selects (green Play = emulator, blue Play = real PS2),
// then the live chips. The Visual-Studio-style caret picks the target and
// carries the run variants (run without build, debug). Icons are vector-drawn
// on the menu-bar draw list (the editor loads no icon font) so they stay crisp
// at any UI scale, and they share one stroke weight so the set reads as a set.
// Spacing is explicit: a pair sits tight, groups get a wider gap.
void App::drawToolbar() {
    if (!hasProject_) return;

    const bool busy = runner_.busy();
    const bool ps2Ready = !project_.ps2LinkIp.empty();
    const ImU32 colDim = ImGui::GetColorU32(ImGuiCol_TextDisabled);
    const ImU32 colText = ImGui::GetColorU32(ImGuiCol_Text);
    // The chips say what a state MEANS, so they read their colours from the
    // theme (theme.hpp) instead of naming one: green/amber/red hardcoded here
    // is green/amber/red in a violet editor. colInfo is the accent - "the
    // other run target", "a live channel" - which every theme keeps distinct
    // from ok/warn/danger.
    const theme::Semantics& sem = theme::semantics();
    const ImU32 colOk = ImGui::GetColorU32(sem.ok);
    const ImU32 colWarn = ImGui::GetColorU32(sem.warn);
    const ImU32 colInfo = ImGui::GetColorU32(sem.accent);
    const ImU32 colStop = ImGui::GetColorU32(sem.danger);
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
        // The highlight fades in and out instead of snapping: sweeping the
        // cursor along a row of icons is the most common thing anyone does with
        // this bar, and a hard rectangle appearing under each one in turn is
        // the difference between a toolbar and a debug overlay. Held buttons
        // jump straight to the active fill - a press must feel immediate.
        const float hoverT =
            theme::hoverAnim(ImGui::GetItemID(), enabled && hovered, 12.0f);
        if (held || hoverT > 0.0f)
            dl->AddRectFilled(p, ImVec2(p.x + bw, p.y + h),
                              ImGui::GetColorU32(held ? ImGuiCol_ButtonActive
                                                      : ImGuiCol_ButtonHovered,
                                                 held ? 1.0f : hoverT),
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

    // The status chips (LIVE / DBG / LOGIC / SESSION) all draw their own dot +
    // label, so they need the icon buttons' faded highlight spelled out once
    // rather than four times. Call right after the chip's InvisibleButton.
    auto chipHover = [&](ImDrawList* dl, ImVec2 p, float chipW) {
        const float t =
            theme::hoverAnim(ImGui::GetItemID(), ImGui::IsItemHovered(), 12.0f);
        if (t > 0.0f)
            dl->AddRectFilled(p, ImVec2(p.x + chipW, p.y + h),
                              ImGui::GetColorU32(ImGuiCol_ButtonHovered, t),
                              round);
    };

    // One stroke weight for every outlined glyph - that, plus the shared glyph
    // rect above, is what makes the icons read as one set instead of five
    // drawings that happen to sit in a row.
    const float stroke = ImMax(1.5f, h * 0.075f);

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

    // Save: a floppy drawn as one closed outline with the classic clipped
    // top-right corner, plus a filled shutter and label. Normal when clean,
    // amber when there are unsaved edits. Enabled unless we joined a session as
    // a client (the host owns saving).
    const bool saveAllowed = session_.role() != session::Session::Role::Client;
    if (iconButton(
            "##tb_save", gapGroup, saveAllowed,
            !saveAllowed ? "The session host owns saving"
            : dirty_     ? "Save - unsaved changes (Ctrl+S)"
                         : "Save (Ctrl+S)",
            [&](ImDrawList* dl, ImVec2 a, ImVec2 b, bool en) {
                const ImU32 c = !en      ? colDim
                                : dirty_ ? colWarn
                                         : colText;
                const float w = b.x - a.x, hh = b.y - a.y;
                const float cut = w * 0.30f;  // clipped top-right corner
                const ImVec2 body[5] = {
                    ImVec2(a.x, a.y),         ImVec2(b.x - cut, a.y),
                    ImVec2(b.x, a.y + cut),   ImVec2(b.x, b.y),
                    ImVec2(a.x, b.y)};
                dl->AddPolyline(body, 5, c, ImDrawFlags_Closed, stroke);
                dl->AddRectFilled(ImVec2(a.x + w * 0.24f, a.y),  // shutter
                                  ImVec2(a.x + w * 0.58f, a.y + hh * 0.34f), c);
                dl->AddRectFilled(ImVec2(a.x + w * 0.24f, b.y - hh * 0.34f),
                                  ImVec2(b.x - w * 0.24f, b.y), c);  // label
            }))
        saveAll("Saved");

    // Build only (no run): an isometric box - the build's output - in the same
    // stroke as the floppy, so the pair reads as one family and neither is
    // confused with the filled Play/Stop shapes.
    if (iconButton("##tb_build", gapPair, !busy,
                   "Build (no run) (Ctrl+Shift+B)",
                   [&](ImDrawList* dl, ImVec2 a, ImVec2 b, bool en) {
                       const ImU32 c = en ? colText : colDim;
                       const float w = b.x - a.x, hh = b.y - a.y;
                       const float cx = a.x + w * 0.5f;
                       const float sh = hh * 0.26f;  // corner shoulder height
                       const ImVec2 top(cx, a.y), bot(cx, b.y);
                       const ImVec2 ul(a.x, a.y + sh), ur(b.x, a.y + sh);
                       const ImVec2 ll(a.x, b.y - sh), lr(b.x, b.y - sh);
                       const ImVec2 mid(cx, a.y + sh * 2.0f);
                       const ImVec2 hex[6] = {top, ur, lr, bot, ll, ul};
                       dl->AddPolyline(hex, 6, c, ImDrawFlags_Closed, stroke);
                       dl->AddLine(mid, ul, c, stroke);  // the three edges
                       dl->AddLine(mid, ur, c, stroke);  // meeting at the front
                       dl->AddLine(mid, bot, c, stroke); // corner
                   }))
        runner_.buildAndRun(projectForBuild(), false);

    // --- Run group: Play (+ target dropdown) + Debug + Stop ----------------
    // ONE pair for both targets; the dropdown picks which machine it drives and
    // the Play glyph's color says so (the theme's "ok" = the emulator, its
    // accent = the real console; every theme keeps those two apart).
    const bool targetReady = !runOnPs2_ || ps2Ready;
    const bool debugProfile = project_.settings.buildProfile == "debug";
    const ImU32 colTarget = runOnPs2_ ? colInfo : colOk;
    const char* noIpTip = "Set 'PS2 (ps2link) IP' in Edit > Preferences first.";
    // Debug is Run plus opening the Debugger panel. It needs the debug build
    // profile - Live Link, the Live Debugger and Live Logic exist nowhere else -
    // but it stays on the bar either way, dimmed, saying where to switch it on.
    const char* debugTip =
        !debugProfile ? "Debug needs the Debug build profile (Project > "
                        "Preferences > Build > Profile).\nLive Link, the Live "
                        "Debugger and Live Logic only exist in debug builds."
        : runOnPs2_   ? "Debug on PS2: build && run, and open the Debugger (F9)"
                      : "Debug in PCSX2: build && run, and open the Debugger (F9)";
    const bool debugEnabled = !busy && targetReady && debugProfile;
    if (iconButton("##tb_run", gapGroup, !busy && targetReady,
                   !targetReady    ? noIpTip
                   : runOnPs2_     ? "Build && Run on PS2 (F6)"
                                   : "Build && Run in PCSX2 (F5)",
                   paintPlay(!busy && targetReady ? colTarget : colDim)))
        runSelectedTarget(true);
    ImGui::SameLine(0.0f, 1.0f);
    const ImVec2 caretPos = ImGui::GetCursorScreenPos();
    const ImVec2 runMenuAnchor(caretPos.x, caretPos.y + h);
    if (button("##tb_run_more", 1.0f, h * 0.55f, true,
               "Run target and options...", paintCaret(colText)))
        ImGui::OpenPopup("run_menu");
    // Debug: the Play triangle in the target color with a breakpoint dot on its
    // lower-left vertex - "run, with the debugger attached" said in two symbols
    // the toolbar already uses. The dot sits ON the vertex so the two read as
    // one mark, and both survive being 10 px wide the way a drawn bug would not.
    if (iconButton("##tb_debug", gapPair, debugEnabled, debugTip,
                   [&](ImDrawList* dl, ImVec2 a, ImVec2 b, bool en) {
                       const float w = b.x - a.x, hh = b.y - a.y;
                       const float x0 = a.x + w * 0.16f, y1 = b.y - hh * 0.12f;
                       dl->AddTriangleFilled(
                           ImVec2(x0, a.y), ImVec2(x0, y1),
                           ImVec2(b.x, (a.y + y1) * 0.5f),
                           en ? colTarget : colDim);
                       dl->AddCircleFilled(ImVec2(x0, y1), w * 0.22f,
                                           en ? colStop : colDim);
                   })) {
        runSelectedTarget(true);
        showDebugger_ = true;
    }
    // Stop: cancels a running build, else stops the selected target - closes
    // PCSX2, or on the console kills the file server + resets ps2link. The
    // emulator side is always available (a stray PCSX2 can't be detected, and
    // the kill is a no-op when none runs).
    const bool stopEnabled = busy || targetReady;
    if (iconButton("##tb_stop", gapPair, stopEnabled,
                   busy            ? "Cancel build"
                   : !targetReady  ? noIpTip
                   : runOnPs2_     ? "Stop the game on the PS2"
                                   : "Stop PCSX2",
                   paintStop(stopEnabled ? colStop : colDim))) {
        if (busy) runner_.cancel();
        else if (runOnPs2_) runner_.stopPs2(project_);
        else runner_.stopEmulator();
    }

    // Live Link chip: a dot + label after the run group, ALSO the on/off
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
                    c = colOk;
                    label = "LIVE";
                    tip = "Live Link: object edits (move/rotate/scale/recolor,"
                          " add/delete) stream\ninto the running game. Click "
                          "to turn off (project setting).";
                    break;
                case LiveLinkState::RebuildNeeded:
                    c = colWarn;
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
        chipHover(dl, p, dotR * 2.0f + 4.0f + textW);
        dl->AddCircleFilled(ImVec2(p.x + dotR, p.y + h * 0.5f), dotR, c);
        dl->AddText(ImVec2(p.x + dotR * 2.0f + 4.0f,
                           p.y + (h - ImGui::GetTextLineHeight()) * 0.5f),
                    c, label);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tip);
    }

    // Live Debugger chip, next to LIVE and read the same way: green DBG = the
    // game is reporting what its graphs do, orange = stopped at a breakpoint
    // (with the frame it stopped on), amber = the running build predates the
    // current graphs, dim = nothing reporting yet. Clicking opens the panel
    // (the on/off switch is a project preference, in Build > Live Debugger).
    if (project_.settings.buildProfile == "debug" && project_.settings.liveDebug) {
        ImU32 c = colDim;
        char label[48] = "DBG";
        const char* tip =
            "Live Debugger: waiting for a game to report. Build & Run (F5) and "
            "the\n"
            "graph starts lighting up as it runs. Click to open the "
            "Debugger panel (F9).";
        switch (dbgState_) {
            case DbgState::Running:
                c = colOk;
                std::snprintf(label, sizeof(label), "DBG %.0f fps", dbgFps_);
                tip = "Live Debugger: the running game is reporting every "
                      "flow-graph node it\n"
                      "fires - watch the Flow Graph light "
                      "up. Click to open the Debugger (F9).";
                break;
            case DbgState::Halted:
                c = IM_COL32(245, 130, 90, 255);
                std::snprintf(label, sizeof(label), "DBG halted @ %u",
                              dbgSnap_.frame);
                tip = "Live Debugger: the game is stopped (breakpoint or "
                      "Pause). F10 continues,\n"
                      "F11 steps one frame. Click to "
                      "open the Debugger panel.";
                break;
            case DbgState::Stale:
                c = colWarn;
                std::snprintf(label, sizeof(label), "DBG (rebuild)");
                tip = "Live Debugger: the running game was built from "
                      "different graphs, so node\n"
                      "numbering no longer matches. "
                      "Build & Run (F5) to resync.";
                break;
            default: break;
        }
        ImGui::SameLine(0.0f, gapPair);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float dotR = h * 0.14f;
        const float textW = ImGui::CalcTextSize(label).x;
        const float chipW = dotR * 2.0f + 4.0f + textW;
        if (ImGui::InvisibleButton("##tb_livedbg", ImVec2(chipW, h)))
            showDebugger_ = true;
        chipHover(dl, p, chipW);
        dl->AddCircleFilled(ImVec2(p.x + dotR, p.y + h * 0.5f), dotR, c);
        dl->AddText(ImVec2(p.x + dotR * 2.0f + 4.0f,
                           p.y + (h - ImGui::GetTextLineHeight()) * 0.5f),
                    c, label);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tip);
    }

    // Live Logic chip: only interesting once graphs differ from the build -
    // blue "LOGIC (n)" = n graphs are running from the editor's patch instead
    // of their compiled C++, amber "LOGIC (rebuild)" = an edited graph uses
    // something the interpreter cannot run. Clicking opens the Debugger's
    // Logic tab.
    if (project_.settings.buildProfile == "debug" &&
        project_.settings.liveLogic &&
        (liveLogicState_ == LogicState::Patched ||
         liveLogicState_ == LogicState::Blocked)) {
        const bool blocked = liveLogicState_ == LogicState::Blocked;
        char label[48];
        if (blocked)
            std::snprintf(label, sizeof(label), "LOGIC (rebuild)");
        else
            std::snprintf(label, sizeof(label), "LOGIC (%d)",
                          liveLogicPatchCount_);
        const ImU32 c = blocked ? colWarn
                                : colInfo;
        const char* tip =
            blocked ? "Live Logic: an edited graph uses nodes the interpreter "
                      "cannot run\n(audio, AI, animation, spawning, runtime "
                      "text...) - Build & Run (F5).\nClick for the list."
                    : "Live Logic: these graphs were compiled by the editor and "
                      "are running\nin the game right now, with no rebuild. "
                      "Click to see them.";
        ImGui::SameLine(0.0f, gapPair);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float dotR = h * 0.14f;
        const float textW = ImGui::CalcTextSize(label).x;
        const float chipW = dotR * 2.0f + 4.0f + textW;
        if (ImGui::InvisibleButton("##tb_livelogic", ImVec2(chipW, h)))
            showDebugger_ = true;
        chipHover(dl, p, chipW);
        dl->AddCircleFilled(ImVec2(p.x + dotR, p.y + h * 0.5f), dotR, c);
        dl->AddText(ImVec2(p.x + dotR * 2.0f + 4.0f,
                           p.y + (h - ImGui::GetTextLineHeight()) * 0.5f),
                    c, label);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tip);
    }

    // Collaboration session chip (same pattern as the LIVE chip): hidden when
    // idle, green "SESSION (n)" while hosting, blue "JOINED" as a client,
    // amber while connecting/syncing. Click opens the Session window.
    {
        const auto st = session_.state();
        if (st != session::Session::State::Idle &&
            st != session::Session::State::Error) {
            char label[32] = "SESSION";
            ImU32 c = colWarn;
            const char* tip = "Connecting...";
            switch (st) {
                case session::Session::State::Listening: {
                    const int others = (int)sessionPeers_.size() - 1;
                    std::snprintf(label, sizeof(label), "SESSION (%d)",
                                  others < 0 ? 0 : others);
                    c = colOk;
                    tip = "Hosting a live session - click for peers / kick / "
                          "close.";
                    break;
                }
                case session::Session::State::Live:
                    std::snprintf(label, sizeof(label), "JOINED");
                    c = colInfo;
                    tip = "Joined a live session - the host owns saving. Click "
                          "for the Session window.";
                    break;
                default:  // Starting / Connecting / Syncing
                    std::snprintf(label, sizeof(label), "SYNC");
                    tip = "Session is starting up / transferring the project.";
                    break;
            }
            ImGui::SameLine(0.0f, gapGroup);
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const ImVec2 p = ImGui::GetCursorScreenPos();
            const float dotR = h * 0.14f;
            const float textW = ImGui::CalcTextSize(label).x;
            if (ImGui::InvisibleButton("##tb_session",
                                       ImVec2(dotR * 2.0f + 4.0f + textW, h)))
                showSessionWindow_ = true;
            chipHover(dl, p, dotR * 2.0f + 4.0f + textW);
            dl->AddCircleFilled(ImVec2(p.x + dotR, p.y + h * 0.5f), dotR, c);
            dl->AddText(ImVec2(p.x + dotR * 2.0f + 4.0f,
                               p.y + (h - ImGui::GetTextLineHeight()) * 0.5f),
                        c, label);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tip);
        }
    }

    // The Play caret's dropdown (anchored just under the caret): which machine
    // to run on, then the run variants for it. Debug needs the debug build
    // profile - Live Link, the Debugger and Live Logic only exist there - so
    // the entry stays visible but disabled and says where to turn it on.
    ImGui::SetNextWindowPos(runMenuAnchor);
    if (ImGui::BeginPopup("run_menu")) {
        if (ImGui::MenuItem("Emulator (PCSX2)", nullptr, !runOnPs2_)) {
            runOnPs2_ = false;
            saveGlobalConfig();
        }
        if (ImGui::MenuItem("PlayStation 2 (ps2link)", nullptr, runOnPs2_,
                            ps2Ready)) {
            runOnPs2_ = true;
            saveGlobalConfig();
        }
        if (!ps2Ready && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("%s", noIpTip);
        ImGui::Separator();
        const char* runKey = runOnPs2_ ? "F6" : "F5";
        const char* noBuildKey = runOnPs2_ ? "Ctrl+F6" : "Ctrl+F5";
        if (ImGui::MenuItem("Run", runKey, false, !busy && targetReady))
            runSelectedTarget(true);
        if (ImGui::MenuItem("Run without build", noBuildKey, false,
                            !busy && targetReady))
            runSelectedTarget(false);
        if (ImGui::MenuItem("Debug", nullptr, false, debugEnabled)) {
            runSelectedTarget(true);
            showDebugger_ = true;
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("%s", debugTip);
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
        mix(sc.terrain.enabled ? 1u : 0u);   // ...which a removed terrain isn't
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

// Terrain brush ranges scale with the map: a 64-unit garden and a 2000-unit
// world need very different maximums (fixed 30/0.5 caps made the brush useless
// on large maps). Sliders over these ranges are logarithmic, so small values
// keep their precision on any map size.
static float terrainDimOf(const Project& p) {
    const TerrainConfig& t = p.active().terrain;
    return (float)(t.width > t.depth ? t.width : t.depth);
}
static float brushMaxRadius(const Project& p) {
    const float r = terrainDimOf(p) * 0.5f;  // up to half the map per stroke
    return r > 30.0f ? r : 30.0f;
}
static float sculptMaxStrength(const Project& p) {
    const float s = terrainDimOf(p) / 100.0f;  // big maps = big landforms
    return s > 0.5f ? s : 0.5f;
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
    mix(sc.terrain.enabled ? 1u : 0u);  // no terrain = nothing walkable at all
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

// Fit a path into maxW pixels by dropping characters from the FRONT: the tail
// (the project's own folder) is what identifies it, the drive root is not.
// Binary search over the cut - the fit is monotonic, and this runs per row
// per frame.
static std::string ellipsizePathLeft(const std::string& s, float maxW) {
    if (ImGui::CalcTextSize(s.c_str()).x <= maxW) return s;
    size_t lo = 0, hi = s.size();  // smallest cut that fits
    while (lo < hi) {
        const size_t mid = (lo + hi) / 2;
        if (ImGui::CalcTextSize(("..." + s.substr(mid)).c_str()).x <= maxW)
            hi = mid;
        else
            lo = mid + 1;
    }
    // Never cut inside a UTF-8 sequence - a path can carry accented folder
    // names, and half a codepoint draws as garbage.
    while (lo < s.size() && ((unsigned char)s[lo] & 0xC0) == 0x80) ++lo;
    return "..." + s.substr(lo);
}

// The Viewport's content before anything is open: the two ways in, plus the
// recent-projects list so the usual next step - carry on with the project from
// last time - is one click instead of a file dialog. Entries whose folder is
// gone stay listed (greyed) rather than being swept: a project on an unplugged
// drive is not a mistake, and the x is right there when it really is.
void App::drawWelcomeScreen() {
    const ImGuiStyle& st = ImGui::GetStyle();
    ImGui::Dummy(ImVec2(0, scaled(24)));
    ImGui::Indent(scaled(30));

    ImGui::TextDisabled("No project open.");
    ImGui::Dummy(ImVec2(0, scaled(6)));
    const ImVec2 bsz(scaled(160), 0);
    if (ImGui::Button("New project...", bsz)) requestNewProject();
    ImGui::SameLine();
    if (ImGui::Button("Open project...", bsz)) requestOpenProject();
    ImGui::SameLine();
    ImGui::TextDisabled("Ctrl+N / Ctrl+O");

    ImGui::Dummy(ImVec2(0, scaled(12)));
    ImGui::SeparatorText("Recent projects");
    if (recentProjects_.empty()) {
        ImGui::TextDisabled("Empty - the projects you open show up here.");
        ImGui::Unindent(scaled(30));
        return;
    }

    // Resolved after the loop: opening a project (or dropping an entry)
    // rewrites recentProjects_, which we are iterating.
    int openIndex = -1, forgetIndex = -1;
    const float listW = ImMin(ImGui::GetContentRegionAvail().x, scaled(560));
    const float lineH = ImGui::GetTextLineHeight();
    const float rowH = lineH * 2.0f + st.FramePadding.y * 2.0f + scaled(4);
    const float textX = st.FramePadding.x + scaled(4);
    const ImU32 colName = ImGui::GetColorU32(ImGuiCol_Text);
    const ImU32 colDim = ImGui::GetColorU32(ImGuiCol_TextDisabled);
    ImDrawList* dl = ImGui::GetWindowDrawList();

    for (int i = 0; i < (int)recentProjects_.size(); ++i) {
        const RecentProject& r = recentProjects_[i];
        ImGui::PushID(i + 7100);
        const ImVec2 row = ImGui::GetCursorScreenPos();
        if (ImGui::Selectable("##recent", false, ImGuiSelectableFlags_AllowOverlap,
                              ImVec2(listW, rowH)))
            openIndex = i;
        // The row shows a shortened path, so the tooltip carries the full one.
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s%s", r.dir.c_str(),
                              r.valid ? "" : "\n(no .tyra project file here)");
        const ImVec2 after = ImGui::GetCursorScreenPos();

        // Name over path, both drawn straight into the draw list: as ImGui
        // items they would register their own (long) width and give the panel
        // a horizontal scrollbar. The clip rect keeps a deep path inside the
        // row instead of running under the x button.
        const float textW = listW - textX - scaled(34);  // up to the x button
        const ImVec4 clip(row.x + textX, row.y, row.x + textX + textW, row.y + rowH);
        const std::string title = r.valid ? r.name : r.name + "   (missing)";
        const std::string path = ellipsizePathLeft(r.dir, textW);
        dl->AddText(nullptr, 0.0f, ImVec2(row.x + textX, row.y + st.FramePadding.y),
                    r.valid ? colName : colDim, title.c_str(), nullptr, 0.0f, &clip);
        dl->AddText(nullptr, 0.0f,
                    ImVec2(row.x + textX, row.y + st.FramePadding.y + lineH), colDim,
                    path.c_str(), nullptr, 0.0f, &clip);

        // Remove from the list. Nothing on disk is touched - the entry is a
        // shortcut, not the project.
        ImGui::SetCursorScreenPos(
            ImVec2(row.x + listW - scaled(26), row.y + (rowH - lineH) * 0.5f));
        if (ImGui::SmallButton("x")) forgetIndex = i;
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Remove from this list (the project itself stays on disk)");
        ImGui::SetCursorScreenPos(after);
        ImGui::PopID();
    }

    ImGui::Dummy(ImVec2(0, scaled(4)));
    ImGui::TextDisabled("Click a project to open it. x only forgets the entry.");
    ImGui::Unindent(scaled(30));

    // An open wins over a drop in the same frame (they can't both be clicked,
    // but the drop would shift the index the open reads).
    if (openIndex < 0 && forgetIndex >= 0) forgetRecentProject(forgetIndex);
    // Nothing is open here, so there is never anything to discard - straight to
    // the shared open path (which reports a folder that vanished since it was
    // probed and re-marks the row).
    if (openIndex >= 0) openRecentProject(recentProjects_[openIndex].dir);
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
        drawWelcomeScreen();
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
                // A cycle owns the sky, the light and the fog colour at its
                // authored hour - exactly the overlay project::resolvedSettings
                // applies for the bake, so dragging the time slider previews
                // the light the build will actually use.
                ambience::Resolved d{};
                if (a.cycle.enabled) {
                    d = ambience::evaluate(a.cycle, a.cycle.time);
                    viewport_.setSky(d.skyColor, d.skyTopColor, a.skyDome,
                                     a.zenithSize);
                    viewport_.setLighting(d.lightDir, d.ambient, d.diffuse,
                                          d.lightColor, d.brightness);
                    viewport_.setFog(a.fogEnabled && showFog_, d.fogColor,
                                     a.fogStart, a.fogEnd);
                } else {
                    viewport_.setSky(a.skyColor, a.skyTopColor, a.skyDome, a.zenithSize);
                    viewport_.setLighting(a.lightDir, a.ambient, a.diffuse, a.lightColor,
                                          a.brightness);
                    viewport_.setFog(a.fogEnabled && showFog_, a.fogColor, a.fogStart, a.fogEnd);
                }
                viewport_.setAmbientOcclusion(a.aoEnabled, a.aoStrength, a.aoRadius);
                ambiencePreviewPushed_ = true;
                // With the runtime half on, the console also applies a drift
                // grade every frame (docs/day-night-cycle.md). The slider is how
                // you author that, so the preview has to include it - otherwise
                // the editor shows a brightly lit midnight the game will not.
                if (a.cycle.enabled && a.cycle.runtime && a.cycle.runtimeGrade) {
                    const ambience::Resolved now =
                        ambience::evaluate(a.cycle, a.cycle.time);
                    const ambience::Resolved baked =
                        ambience::evaluate(a.cycle, ambience::bakedHour(a.cycle));
                    const ambience::Grade g = ambience::driftGrade(now, baked);
                    ColorGradingPreset cg;
                    for (int i = 0; i < 3; ++i) {
                        cg.gain[i] = g.gain[i];
                        cg.lift[i] = g.lift[i];
                        cg.tint[i] = g.mixColor[i];
                    }
                    cg.tintAmount = g.mixAmount;
                    viewport_.setGrading(true, compileGrading(cg));
                    // Pre-compensate the sky the same way the game does, or the
                    // preview shows a night the console will not.
                    ambience::driftCompensation(g, skyBodyComp_);
                    float sky[3], top[3], fog[3];
                    for (int i = 0; i < 3; ++i) {
                        sky[i] = std::min(1.0f, d.skyColor[i] * skyBodyComp_[i]);
                        top[i] = std::min(1.0f, d.skyTopColor[i] * skyBodyComp_[i]);
                        fog[i] = std::min(1.0f, d.fogColor[i] * skyBodyComp_[i]);
                    }
                    viewport_.setSky(sky, top, a.skyDome, a.zenithSize);
                    viewport_.setFog(a.fogEnabled && showFog_, fog, a.fogStart,
                                     a.fogEnd);
                } else {
                    skyBodyComp_[0] = skyBodyComp_[1] = skyBodyComp_[2] = 1.0f;
                }
            } else if (ambiencePreviewPushed_) {
                ambiencePreviewPushed_ = false;
                applyProjectToViewport();  // restore the scene's own ambience
            }
            // The sun/moon discs follow whichever cycle is being shown: the
            // previewed preset while the editor is open, the scene's own
            // otherwise - so they are visible during ordinary scene work too.
            updateSkyBodyPreview(preview ? selectedAmbience_ : -1);
        }
        // Layer eye toggles: objects on hidden layers vanish from the render
        // and the click picking (mask indices parallel project_.objects()).
        {
            std::vector<char> hidden(project_.objects().size(), 0);
            for (size_t i = 0; i < project_.objects().size(); ++i)
                hidden[i] = isObjectHiddenInEditor(project_.objects()[i]) ? 1 : 0;
            viewport_.setHiddenMask(std::move(hidden));
        }
        // Scroller ghost belts: the View toggle covers every belt, the
        // per-object set covers the ones switched off in their own Properties.
        {
            std::vector<char> ghosts(project_.objects().size(), 1);
            for (size_t i = 0; i < project_.objects().size(); ++i) {
                const SceneObject& o = project_.objects()[i];
                if (o.type != PrimitiveType::Scroller) continue;
                ghosts[i] = (showScrollerPreview_ && !scrollGhostsOff_.count(o.id))
                                ? (char)1
                                : (char)0;
            }
            viewport_.setScrollerGhosts(std::move(ghosts));
        }
        // Non-destructive clip edits + the project's animation-fps ratio, so
        // the scene preview retimes and trims exactly like the build bakes.
        viewport_.setAnimEdits(project_.animClipEdits,
                               animedit::projectTimeScale(project_.settings));
        // Cutscene Director preview: pose the objects (and maybe fly the
        // camera) at the playhead. Returns the raw objects when not previewing.
        const std::vector<SceneObject>& posedObjects = cutscenePosedObjects();
        // Deferred paste: the staged copies are not in the scene yet, so they
        // ride along in a scratch list and render outlined like a selection -
        // that outline IS the "this is still being placed" feedback.
        const std::vector<SceneObject>* renderList = &posedObjects;
        std::vector<int> renderSel = selection_;
        int renderPrimary = selectedObject_;
        if (pastePending_ && !pasteStaged_.empty()) {
            pasteRenderScratch_ = posedObjects;
            renderSel.clear();
            for (const SceneObject& o : pasteStaged_) {
                renderSel.push_back((int)pasteRenderScratch_.size());
                pasteRenderScratch_.push_back(o);
            }
            renderPrimary = renderSel.back();
            renderList = &pasteRenderScratch_;
        }
        const std::vector<SceneObject>& renderObjects = *renderList;
        // Phone camera link: while a phone is streaming a pose and driving, IT
        // is the camera - ahead of the cutscene preview and the look-through
        // camera both, because the person holding it is framing the shot and a
        // playhead flying the same lane would fight them for it.
        phoneCamPushed_ = false;
        if (phoneDrive_ && phoneHasPose_ && phoneCam_.connected()) {
            viewport_.setCameraOverride(phoneEye_, phoneTarget_, phoneFov_,
                                        phoneRoll_);
            phoneCamPushed_ = true;
        }
        // Look-through camera ("View:" overlay / camera Properties): render
        // from the chosen Camera entity's pose + FOV. The cutscene camera
        // track wins while it previews; reading the POSED objects means a
        // dollied camera entity is followed live. A stale name (deleted
        // entity) falls back to the free orbit camera.
        if (!seqCameraPushed_ && !phoneCamPushed_) {
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
                // Look through it exactly as the game would, tilt included - a
                // rolled Camera entity should look rolled here too.
                float eu[3];
                seqCameraUpFromEuler(cam->rotation, eu);
                viewport_.setCameraOverride(cam->position, at, cam->cameraFov,
                                            seqRollFromUp(fwd, eu));
            } else {
                viewport_.clearCameraOverride();
            }
        }
        // Hide the camera(s) we are previewing through so their model doesn't
        // fill the frame: during a cutscene camera preview, every camera the
        // sequence films from; otherwise the single looked-through camera.
        {
            std::vector<std::string> hideCams;
            if (phoneCamPushed_) {
                // Recording into a Camera entity puts that entity exactly where
                // the view is, so it would fill the frame.
                if (!phoneRecTarget_.empty()) hideCams.push_back(phoneRecTarget_);
            } else if (seqCameraPushed_ && selectedSequence_ >= 0 &&
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
        updateProcPreview();
        // Pushed every frame rather than on change: the geometry follows the
        // project's display settings, which the Preferences dialog can change
        // under us, and resolving it is a handful of comparisons.
        viewport_.setPs2Output(ps2ViewportOutput());
        uint32_t tex = viewport_.render((int)avail.x, (int)avail.y, renderObjects,
                                        renderSel, renderPrimary);
        // Phone camera link: stream THIS frame to the connected device, so the
        // phone is a viewfinder onto the editor's own image rather than a
        // second, subtly different render.
        phoneCamPushPreview();
        // Flip vertically: GL texture origin is bottom-left
        ImGui::Image((ImTextureID)(intptr_t)tex, avail, ImVec2(0, 1), ImVec2(1, 0));

        const ImVec2 imgPos = ImGui::GetItemRectMin();
        const bool imageHovered = ImGui::IsItemHovered();
        ImGuiIO& io = ImGui::GetIO();

        // A model dragged out of the Asset Browser lands where the cursor points
        // (docs/asset-browser.md). Only the model payload is accepted, so
        // dragging a texture over the viewport shows no drop target at all.
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* pl =
                    ImGui::AcceptDragDropPayload("TYRAX_ASSET_MODEL")) {
                const std::string rel((const char*)pl->Data);
                dropAssetIntoScene(rel, (io.MousePos.x - imgPos.x) / avail.x,
                                   (io.MousePos.y - imgPos.y) / avail.y);
            }
            ImGui::EndDragDropTarget();
        }

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

        // Devkit object trails (docs/devkit.md): the path a watched object
        // actually took in the RUNNING game, drawn over the viewport image.
        // Projected here rather than in GL - the viewport hands out its view and
        // projection matrices, and an ImDrawList polyline is exact enough for a
        // debug trail (and costs no renderer changes).
        if (dbgShowTrails_ && !dbgObjWatch_.empty() &&
            (dbgState_ == DbgState::Running || dbgState_ == DbgState::Halted)) {
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const float* V = viewport_.viewMatrix();
            const float* P = viewport_.projMatrix();
            auto project = [&](const float* w, ImVec2& out) {
                // column-major 4x4, world -> clip
                float e[4] = {0, 0, 0, 0};
                for (int r = 0; r < 4; ++r) {
                    float v = 0.0f;
                    for (int c = 0; c < 3; ++c) v += V[c * 4 + r] * w[c];
                    e[r] = v + V[12 + r];
                }
                float c4[4] = {0, 0, 0, 0};
                for (int r = 0; r < 4; ++r) {
                    float v = 0.0f;
                    for (int c = 0; c < 4; ++c) v += P[c * 4 + r] * e[c];
                    c4[r] = v;
                }
                if (c4[3] <= 0.0001f) return false;  // behind the eye
                out = ImVec2(imgPos.x + (c4[0] / c4[3] * 0.5f + 0.5f) * avail.x,
                             imgPos.y + (0.5f - c4[1] / c4[3] * 0.5f) * avail.y);
                return true;
            };
            dl->PushClipRect(imgPos, ImVec2(imgPos.x + avail.x, imgPos.y + avail.y),
                             true);
            for (size_t ti = 0; ti < dbgObjWatch_.size(); ++ti) {
                const DbgObjTrack& t = dbgObjWatch_[ti];
                if (t.samples.size() < 2) continue;
                // A colour per watched object, stable across frames.
                static const ImU32 kCols[] = {
                    IM_COL32(90, 200, 255, 220),  IM_COL32(255, 170, 80, 220),
                    IM_COL32(150, 240, 130, 220), IM_COL32(240, 130, 220, 220),
                    IM_COL32(255, 230, 110, 220), IM_COL32(120, 160, 255, 220),
                    IM_COL32(255, 120, 120, 220), IM_COL32(170, 255, 230, 220)};
                const ImU32 col = kCols[ti % 8];
                ImVec2 prev;
                bool havePrev = false;
                for (const livedbg::ObjSample& sm : t.samples) {
                    ImVec2 cur;
                    if (!project(sm.pos, cur)) {
                        havePrev = false;
                        continue;
                    }
                    if (havePrev) dl->AddLine(prev, cur, col, scaled(1.5f));
                    prev = cur;
                    havePrev = true;
                }
                // The head of the trail: where it is right now.
                if (havePrev) {
                    dl->AddCircleFilled(prev, scaled(4.0f), col);
                    dl->AddCircle(prev, scaled(4.0f), IM_COL32(20, 20, 20, 180), 0,
                                  scaled(1.0f));
                }
            }
            dl->PopClipRect();
        }

        // TV safe-area guides, over the image like the cutscene bars.
        drawSafeAreaOverlay(imgPos, avail);

        // --- Axis view gizmo (top-right corner) ---
        // Drawn before the input handling so its hover can veto the click that
        // would otherwise fall through and change the selection.
        const bool overAxisGizmo = drawAxisGizmo(imgPos, avail) |
                                   drawViewportGear(imgPos, avail);

        // --- Terrain sculpting / painting brush (shared raycast + ring) ---
        const bool brushMode = sculptMode_ || paintMode_;
        bool brushHit = false;
        float brushX = 0.0f, brushZ = 0.0f;
        if (brushMode && imageHovered) {
            const float u = (io.MousePos.x - imgPos.x) / avail.x;
            const float v = (io.MousePos.y - imgPos.y) / avail.y;
            brushHit = viewport_.terrainRaycast(u, v, brushX, brushZ);

            if (brushHit && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                if (sculptMode_) {
                    if (sculptFlatten_) {
                        // level toward the target height; strength = lerp rate
                        project::flattenHeightmap(project_, brushX, brushZ, brushRadius_,
                                                  flattenHeight_, brushStrength_);
                    } else {
                        const float delta =
                            io.KeyShift ? -brushStrength_ : brushStrength_;
                        project::sculptHeightmap(project_, brushX, brushZ, brushRadius_,
                                                 delta);
                    }
                    // Live rebuild of just the chunks under the brush - a full
                    // applyProjectToViewport would rebuild the whole map per frame.
                    viewport_.updateTerrainRegion(project_.active().heights, brushX,
                                                  brushZ, brushRadius_);
                    sculptStroke_ = true;
                } else {  // paintMode_
                    const float delta = (io.KeyShift || paintErase_) ? -paintStrength_
                                                                     : paintStrength_;
                    project::paintSplat(project_, paintLayer_, brushX, brushZ,
                                        brushRadius_, delta);
                    // Live rebuild of just the layer passes under the brush -
                    // the paint twin of the sculpt region update above.
                    viewport_.updateSplatRegion(project_.active().splat, brushX,
                                                brushZ, brushRadius_);
                    paintStroke_ = true;
                }
            }
        }
        if (sculptStroke_ && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            sculptStroke_ = false;
            commitChange();  // one undo step per finished brush stroke
            statusMessage_ = "Terrain sculpted";
        }
        if (paintStroke_ && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            paintStroke_ = false;
            commitChange();  // one undo step per finished paint stroke
            statusMessage_ = "Terrain painted";
        }

        // brush ring projected onto the terrain
        if (brushMode && brushHit) {
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
                    dl->AddLine(prev, pt,
                                paintMode_ ? (paintErase_ ? IM_COL32(255, 90, 90, 220)
                                                          : IM_COL32(80, 220, 120, 220))
                                           : IM_COL32(255, 200, 40, 220),
                                2.0f);
                prev = pt;
                prevOk = ok;
            }
        }

        // --- Transform gizmo on the selection (disabled while sculpting;
        // objects on a hidden layer can't be grabbed either) ---
        bool objectSelected = !sculptMode_ && !paintMode_ && !measureMode_ &&
                              !pastePending_ &&
                              selectedObject_ >= 0 &&
                              selectedObject_ < (int)project_.objects().size() &&
                              !isObjectHiddenInEditor(project_.objects()[selectedObject_]);
        if (objectSelected) {
            SceneObject& o = project_.objects()[selectedObject_];

            ImGuizmo::SetOrthographic(viewport_.orthographic());
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
            // Right + middle together: pan the pivot FORWARD/BACK along the view
            // direction. Checked before the scheme switch because it has to beat
            // whatever those two buttons mean on their own - and it is scheme
            // independent, since every scheme leaves the pair unused.
            const bool doDollyPan = rmb && mmb;
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
            if (doDollyPan) {
                doOrbit = doPan = false;  // the pair is its own gesture
                // Dragging DOWN moves forward. That is the pan sense, not the
                // zoom sense: a pan drags the WORLD under the cursor, so pulling
                // the mouse toward you pulls the scene toward you.
                viewport_.dolly(io.MouseDelta.y * nav_.dollySensitivity);
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
            if (!sculptMode_ && !paintMode_ && !measureMode_ && !pastePending_ &&
                !lmbCamera && !overAxisGizmo &&
                ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                boxSelecting_ = true;
        }

        // --- Deferred paste: the staged copies follow the cursor until a
        // click (or another Ctrl+V) settles them; Esc drops them. ---
        if (pastePending_) {
            if (imageHovered && !gizmoBusy && !overAxisGizmo) {
                const float u = (io.MousePos.x - imgPos.x) / avail.x;
                const float v = (io.MousePos.y - imgPos.y) / avail.y;
                float point[3];
                if (viewport_.placementRaycast(u, v, project_.objects(),
                                               placementSkip(), point))
                    movePasteStaged(point);
                // A plain click (not an orbit drag) commits where they stand.
                if (ImGui::IsMouseReleased(ImGuiMouseButton_Left) &&
                    io.MouseDragMaxDistanceSqr[ImGuiMouseButton_Left] < 9.0f)
                    commitPastePlacement();
            }
            if (ImGui::IsKeyPressed(ImGuiKey_Escape)) cancelPastePlacement();
        }

        // --- Measuring tape: click a point, then a second one. The end
        // follows the cursor in between, so the readout updates live; a third
        // click starts a new measurement. Both points land on the same
        // surfaces a paste would rest on (object boxes + the heightfield), so
        // the tape measures the scene, not an arbitrary plane. ---
        // A paste in flight owns the click (and the same corner of the
        // screen), so the tape waits until it is placed or cancelled.
        if (measureMode_ && !pastePending_) {
            if (imageHovered && !overAxisGizmo) {
                const float u = (io.MousePos.x - imgPos.x) / avail.x;
                const float v = (io.MousePos.y - imgPos.y) / avail.y;
                float point[3];
                const bool hit = viewport_.placementRaycast(
                    u, v, project_.objects(), std::vector<char>(), point);
                if (hit && measurePoints_ == 1 && measureLive_)
                    for (int c = 0; c < 3; ++c) measureB_[c] = point[c];
                if (hit && ImGui::IsMouseReleased(ImGuiMouseButton_Left) &&
                    io.MouseDragMaxDistanceSqr[ImGuiMouseButton_Left] < 9.0f) {
                    if (measurePoints_ == 1) {
                        for (int c = 0; c < 3; ++c) measureB_[c] = point[c];
                        measurePoints_ = 2;
                        measureLive_ = false;
                    } else {
                        for (int c = 0; c < 3; ++c)
                            measureA_[c] = measureB_[c] = point[c];
                        measurePoints_ = 1;
                        measureLive_ = true;
                    }
                }
            }
            if (ImGui::IsKeyPressed(ImGuiKey_Escape)) measurePoints_ = 0;
            drawMeasureOverlay(imgPos, avail);
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

        // Procedural tools take the click while they are armed (Tools >
        // Procedural): editing a curve's control points, or picking an
        // instance for a manual override.
        bool procClick = false;
        if (imageHovered && !gizmoBusy && !sculptMode_ && !paintMode_ &&
            (procCurveNode_ != 0 || procOverrideMode_) &&
            ImGui::IsMouseReleased(ImGuiMouseButton_Left) &&
            io.MouseDragMaxDistanceSqr[ImGuiMouseButton_Left] < 9.0f) {
            const float u = (io.MousePos.x - imgPos.x) / avail.x;
            const float v = (io.MousePos.y - imgPos.y) / avail.y;
            if (procCurveNode_ != 0 && procVolume_ >= 0 &&
                procVolume_ < (int)project_.objects().size()) {
                float wx = 0.0f, wz = 0.0f;
                if (viewport_.terrainRaycast(u, v, wx, wz)) {
                    ProcNode* cn = procgraph::node(
                        project_.objects()[procVolume_].procGraph, procCurveNode_);
                    if (cn) {
                        const float wy = viewport_.terrainHeight(wx, wz);
                        if (procCurvePoint_ >= 0 &&
                            procCurvePoint_ < (int)cn->rows.size()) {
                            ProcRow& row = cn->rows[procCurvePoint_];
                            row.v[0] = wx;
                            row.v[1] = wy;
                            row.v[2] = wz;
                        } else {
                            ProcRow row;
                            row.v[0] = wx;
                            row.v[1] = wy;
                            row.v[2] = wz;
                            cn->rows.push_back(row);
                        }
                        commitChange();
                        procClick = true;
                    }
                }
            } else if (procOverrideMode_ && procVolume_ >= 0 &&
                       procVolume_ < (int)project_.objects().size()) {
                const int hit = pickProcInstance(u, v);
                ProcGraph& pg = project_.objects()[procVolume_].procGraph;
                if (hit >= 0) {
                    const uint64_t key = procResult_.instances[hit].key;
                    if (io.KeyCtrl) {  // ctrl+click deletes the instance
                        procOverrideFor(pg, key).removed = true;
                        procSelInstance_ = 0;
                        commitChange();
                    } else {
                        procSelInstance_ = key;
                    }
                    procClick = true;
                } else if (procSelInstance_) {
                    // Clicking the ground with an instance selected moves it
                    // there: the offset is stored relative to whatever the
                    // graph generated, so re-evaluation keeps the placement.
                    float wx = 0.0f, wz = 0.0f;
                    if (viewport_.terrainRaycast(u, v, wx, wz)) {
                        const procgen::Instance* sel = nullptr;
                        for (const procgen::Instance& inst : procResult_.instances)
                            if (inst.key == procSelInstance_) sel = &inst;
                        if (sel) {
                            ProcOverride& ov = procOverrideFor(pg, procSelInstance_);
                            ov.offset[0] += wx - sel->pos[0];
                            ov.offset[2] += wz - sel->pos[2];
                            ov.offset[1] += viewport_.terrainHeight(wx, wz) - sel->pos[1];
                            commitChange();
                            procClick = true;
                        }
                    }
                }
            }
        }

        // Click (no drag) = pick object under cursor. Ctrl toggles it in the
        // current selection; a plain click replaces (empty click clears).
        if (!procClick && imageHovered && !gizmoBusy && !sculptMode_ && !paintMode_ &&
            !measureMode_ && !pastePending_ && !overAxisGizmo &&
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
                // Text mode wins over the image and is sized by its own bake -
                // the same rule the build follows, so this preview matches.
                const HudTexture* t = nullptr;
                float size[2] = {up.size[0], up.size[1]};
                if (!project_.usePromptText.text.empty()) {
                    t = hudTextTexture(project_.usePromptText);
                    if (t) {
                        size[0] = (float)t->w;
                        size[1] = (float)t->h;
                    }
                } else {
                    t = up.imagePath.empty() ? builtinUseTexture()
                                             : hudTexture(up.imagePath);
                }
                ImVec2 pMin, pMax;
                screenRect(up.pos, size, pMin, pMax);
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

        // Terrain brushes stay with the tools (shortcuts 4/6). Grabbing either
        // one opens the Terrain Editor window - the tool's options live there.
        // Both are dead in a scene with no terrain (docs/terrain.md): there is
        // no ground to sculpt or paint, and the Terrain Editor is where it is
        // created again.
        const bool hasTerrain = project_.active().terrain.enabled;
        ImGui::SameLine(0.0f, 24.0f);
        ImGui::BeginDisabled(!hasTerrain);
        if (sculptMode_)
            ImGui::PushStyleColor(ImGuiCol_Button,
                                  ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
        if (ImGui::SmallButton("Sculpt (4)")) {
            sculptMode_ = !sculptMode_;
            if (sculptMode_) {
                paintMode_ = false;  // one terrain brush at a time
                showTerrainEditor_ = true;
            }
        }
        if (sculptMode_) ImGui::PopStyleColor();
        ImGui::SameLine();
        if (paintMode_)
            ImGui::PushStyleColor(ImGuiCol_Button,
                                  ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
        if (ImGui::SmallButton("Paint (6)")) {
            paintMode_ = !paintMode_;
            if (paintMode_) {
                sculptMode_ = false;
                showTerrainEditor_ = true;  // add layers there if none exist yet
            }
        }
        if (paintMode_) ImGui::PopStyleColor();
        ImGui::EndDisabled();
        if (!hasTerrain &&
            ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("This scene has no terrain - create it in the "
                              "Terrain Editor");

        // Surface snapping: inserted / pasted objects rest on what is under
        // them. A machine-global preference, mirrored in the View menu.
        ImGui::SameLine(0.0f, 24.0f);
        if (placementSnap_)
            ImGui::PushStyleColor(ImGuiCol_Button,
                                  ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
        if (ImGui::SmallButton("Surface snap")) {
            placementSnap_ = !placementSnap_;
            saveGlobalConfig();
        }
        if (placementSnap_) ImGui::PopStyleColor();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(
                "Inserted and pasted objects rest on the surface under them\n"
                "(the terrain, or the top of the object below) instead of\n"
                "sinking into it. End drops the selection to the floor.");

        // Measuring tape: how far apart two points in the scene actually are,
        // in units and (via the world scale) in meters.
        ImGui::SameLine();
        if (measureMode_)
            ImGui::PushStyleColor(ImGuiCol_Button,
                                  ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
        if (ImGui::SmallButton("Measure (7)")) {
            measureMode_ = !measureMode_;
            measurePoints_ = 0;
            if (measureMode_) sculptMode_ = paintMode_ = false;
        }
        if (measureMode_) ImGui::PopStyleColor();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(
                "Click a point, then a second one: the distance is shown in\n"
                "world units and in meters (Preferences > World > Units per\n"
                "meter). Click again to start over, Esc clears, the button\n"
                "or 7 leaves the tool.");

        // While a paste is in flight, say so where the eye already is.
        if (pastePending_) {
            ImGui::SetCursorScreenPos(ImVec2(imgPos.x + 8, imgPos.y + 32));
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f),
                               "Placing paste - click to drop, Ctrl+V to drop, "
                               "Esc to cancel");
        }

        // Geometry for the bottom-corner overlays. SmallButton keeps
        // FramePadding.x, so its width is the label plus twice that padding.
        auto smallBtnW = [](const char* s) {
            return ImGui::CalcTextSize(s).x + ImGui::GetStyle().FramePadding.x * 2.0f;
        };
        const float bottomY = imgPos.y + avail.y - ImGui::GetFrameHeight() - 8.0f;

        // --- Camera recenter (bottom-left, after the gear) ---
        ImGui::SetCursorScreenPos(ImVec2(imgPos.x + viewportGearSpan(), bottomY));
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

        // --- Camera projection (next to the recenter buttons) ---
        // Perspective / parallel + the six locked axis views. The numpad
        // shortcuts below do the same; this button is the discoverable half
        // (and the only one on a keyboard without a numpad).
        {
            ImGui::SameLine(0.0f, 16.0f);
            const std::string projLbl =
                std::string("Proj: ") + Viewport::projectionName(viewport_.projection());
            if (ImGui::SmallButton(projLbl.c_str())) ImGui::OpenPopup("##projection");
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(
                    "Camera projection. Orthographic drops the perspective\n"
                    "foreshortening; the axis views also aim the camera down\n"
                    "a world axis (Num 1/3/7, Ctrl for the opposite side,\n"
                    "Num 5 toggles perspective). Orbiting an axis view drops\n"
                    "the lock and goes back to the projection you came from.");
            if (ImGui::BeginPopup("##projection")) {
                for (int i = 0; i < Viewport::kProjectionCount; ++i) {
                    const Viewport::Projection p = (Viewport::Projection)i;
                    if (i == 2) ImGui::Separator();  // free modes | axis views
                    if (ImGui::MenuItem(Viewport::projectionName(p), nullptr,
                                        viewport_.projection() == p))
                        setViewProjection(p);
                }
                ImGui::EndPopup();
            }
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

        // Quick brush controls for both terrain modes - the same variables the
        // Terrain Editor window edits, so they never disagree. Ranges scale
        // with the map (see brushMaxRadius/sculptMaxStrength); logarithmic so
        // small maps keep fine control. [ and ] resize the brush from the keys.
        if (sculptMode_ || paintMode_) {
            ImGui::SetCursorScreenPos(ImVec2(imgPos.x + 8, imgPos.y + 32));
            ImGui::SetNextItemWidth(scaled(140));
            ImGui::SliderFloat("Radius", &brushRadius_, 1.0f,
                               brushMaxRadius(project_), "%.1f",
                               ImGuiSliderFlags_Logarithmic);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Brush radius ([ / ] to resize)");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(scaled(140));
            if (sculptMode_) {
                ImGui::SliderFloat("Strength", &brushStrength_, 0.01f,
                                   sculptMaxStrength(project_), "%.2f",
                                   ImGuiSliderFlags_Logarithmic);
                ImGui::SameLine();
                ImGui::Checkbox("Flatten", &sculptFlatten_);
                const float lvl = terrainDimOf(project_);
                if (sculptFlatten_) {
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(scaled(90));
                    ImGui::DragFloat("Level", &flattenHeight_, 0.1f, -lvl, lvl,
                                     "%.1f");
                    ImGui::SameLine();
                    ImGui::TextDisabled("LMB level to height, RMB orbit");
                } else {
                    ImGui::SameLine();
                    ImGui::TextDisabled("LMB raise, Shift+LMB lower, RMB orbit");
                }
            } else {
                ImGui::SliderFloat("Strength", &paintStrength_, 0.05f, 1.0f,
                                   "%.2f");
                ImGui::SameLine();
                ImGui::Checkbox("Erase", &paintErase_);
                ImGui::SameLine();
                const SceneData& psc = project_.active();
                const bool okLayer = paintLayer_ >= 0 &&
                                     paintLayer_ < (int)psc.terrainLayers.size();
                ImGui::TextDisabled(
                    okLayer ? "painting '%s' - LMB paint, Shift erase, RMB orbit"
                            : "no layer - add one in the Terrain Editor",
                    okLayer ? psc.terrainLayers[paintLayer_].name.c_str() : "");
            }
        }

        // --- Keyboard shortcuts (viewport hovered, not typing) ---
        if (ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows) && !io.WantTextInput) {
            if (ImGui::IsKeyPressed(ImGuiKey_1)) gizmoOp_ = 0;
            if (ImGui::IsKeyPressed(ImGuiKey_2)) gizmoOp_ = 1;
            if (ImGui::IsKeyPressed(ImGuiKey_3)) gizmoOp_ = 2;
            if (ImGui::IsKeyPressed(ImGuiKey_4) && hasTerrain) {
                sculptMode_ = !sculptMode_;
                if (sculptMode_) {
                    paintMode_ = false;
                    showTerrainEditor_ = true;
                }
            }
            if (ImGui::IsKeyPressed(ImGuiKey_5)) gizmoSpace_ = 1 - gizmoSpace_;
            // Camera projection on the numpad (CAD/Blender muscle memory):
            // 1/3/7 front/right/top, Ctrl for the opposite side, 5 toggles
            // perspective. The tool keys live on the number ROW, so nothing
            // collides.
            {
                const bool ctrl = io.KeyCtrl;
                if (ImGui::IsKeyPressed(ImGuiKey_Keypad7))
                    setViewProjection(ctrl ? Viewport::Projection::OrthoBottom
                                           : Viewport::Projection::OrthoTop);
                if (ImGui::IsKeyPressed(ImGuiKey_Keypad1))
                    setViewProjection(ctrl ? Viewport::Projection::OrthoBack
                                           : Viewport::Projection::OrthoFront);
                if (ImGui::IsKeyPressed(ImGuiKey_Keypad3))
                    setViewProjection(ctrl ? Viewport::Projection::OrthoLeft
                                           : Viewport::Projection::OrthoRight);
                if (ImGui::IsKeyPressed(ImGuiKey_Keypad5))
                    setViewProjection(viewport_.orthographic()
                                          ? Viewport::Projection::Perspective
                                          : Viewport::Projection::Ortho);
            }
            if (ImGui::IsKeyPressed(ImGuiKey_6) && hasTerrain) {
                paintMode_ = !paintMode_;
                if (paintMode_) {
                    sculptMode_ = false;
                    showTerrainEditor_ = true;
                }
            }
            if (ImGui::IsKeyPressed(ImGuiKey_7)) {
                measureMode_ = !measureMode_;
                measurePoints_ = 0;
                if (measureMode_) sculptMode_ = paintMode_ = false;
            }
            // Resize the brush without leaving the stroke ([ / ], 15% steps).
            if (sculptMode_ || paintMode_) {
                if (ImGui::IsKeyPressed(ImGuiKey_LeftBracket))
                    brushRadius_ = std::max(1.0f, brushRadius_ / 1.15f);
                if (ImGui::IsKeyPressed(ImGuiKey_RightBracket))
                    brushRadius_ =
                        std::min(brushMaxRadius(project_), brushRadius_ * 1.15f);
            }

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
            // End: rest the selection on the first surface below it.
            if (objectSelected && ImGui::IsKeyPressed(ImGuiKey_End))
                dropSelectionToFloor();
        }
    }
    ImGui::End();
}

// --- PS2 output geometry -----------------------------------------------------
// The GS geometry the project's display settings resolve to - the host twin of
// Tyra's RendererSettings::updateGeometry (renderer_settings.hpp) plus the
// scan-out choice in RendererCoreGS::presentFrameBuffer. Change one, change the
// other, or the viewport claims a picture the console does not draw.
//
// One resolution the console makes and the editor cannot: `videoSystem: auto`
// follows the region of whatever console boots the disc, and only that decides
// whether `palFullHeight` promotes the interlaced mode to the full 576i frame.
// The editor shows the PAL picture there, because `palFullHeight` is ONLY
// meaningful on a PAL console - a project that turns it on (new ones do) is a
// project authored for PAL, and the taller frame is the one whose extra 64
// lines need composing for. The shorter NTSC picture inside it is exactly what
// the safe-area overlay's "NTSC picture inside PAL" guide draws, and that guide
// enables in precisely this configuration. Only an explicit `ntsc` video system
// says the disc will never see a PAL console.
Viewport::Ps2Output App::ps2ViewportOutput() const {
    Viewport::Ps2Output o;
    o.on = viewportPs2_ && hasProject_;
    const ProjectSettings& s = project_.settings;
    std::string mode = s.displayMode;
    if (mode == "interlaced" && s.palFullHeight && s.videoSystem != "ntsc")
        mode = "pal576";

    int logicalH = 448;
    if (mode == "progressive") {
        o.bufW = 448;
        logicalH = 448;
    } else if (mode == "1080i") {
        o.bufW = 448;
        logicalH = 540;
    } else if (mode == "pal576") {
        o.bufW = 512;
        logicalH = 512;
    } else {  // interlaced and interlaced-field share the logical 512x448
        o.bufW = 512;
        logicalH = 448;
    }
    // True field rendering draws into a half-height buffer and the scan-out
    // spreads it over the full number of lines: the vertical resolution really
    // is halved, which is the whole point of seeing it here.
    o.bufH = mode == "interlaced-field" ? logicalH / 2 : logicalH;

    // Projection aspect, verbatim from the engine: the stock 512/448 is the
    // 4:3 baseline, scaled by the physical shape of the display window.
    float windowAspect = s.widescreen ? (16.0f / 9.0f) : (4.0f / 3.0f);
    if (mode == "1080i" && s.widescreen)
        windowAspect = (1792.0f / 1920.0f) * (16.0f / 9.0f);
    o.projAspect = (512.0f / 448.0f) * windowAspect / (4.0f / 3.0f);
    // windowAspect IS the physical shape of the window on the TV, so it is
    // also the rectangle the picture is fitted into here (1080i's pillarboxed
    // widescreen window included).
    o.tvAspect = windowAspect;
    // ps2sdk's flicker filter runs on the two stock interlaced scan-outs only.
    o.flicker = mode == "interlaced" || mode == "pal576";
    return o;
}

// --- TV safe areas -----------------------------------------------------------
// A CRT does not show the whole picture: the tube is scanned past the edges of
// the visible glass ("overscan"), and how much varies per set. The broadcast
// convention that survived it is two insets - action safe at 90% (nothing
// important outside) and title safe at 80% (text inside) - which is what these
// guides draw. PAL and NTSC share those fractions; where they genuinely differ
// is the PICTURE HEIGHT, and only when this project targets full-height PAL
// (see `bothRegions` below).
void App::drawSafeAreaOverlay(const ImVec2& pos, const ImVec2& size) {
    if (!showSafeArea_ || size.x < 32.0f || size.y < 32.0f) return;
    const SafeAreaCfg& c = safeArea_;
    if (!c.frame && !c.action && !c.title && !c.centre && !c.bothRegions) return;

    // The console outputs a fixed aspect regardless of how the viewport is
    // docked, so the guides must be drawn inside THAT rectangle - fitted into
    // the image like a TV picture inside a wider monitor.
    const bool wide = c.aspect == 0 ? project_.settings.widescreen : (c.aspect == 2);
    const float tvAspect = wide ? 16.0f / 9.0f : 4.0f / 3.0f;
    float w = size.x, h = size.x / tvAspect;
    if (h > size.y) {
        h = size.y;
        w = size.y * tvAspect;
    }
    const ImVec2 tl(pos.x + (size.x - w) * 0.5f, pos.y + (size.y - h) * 0.5f);
    const ImVec2 br(tl.x + w, tl.y + h);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float a = c.opacity < 0.0f ? 0.0f : (c.opacity > 1.0f ? 1.0f : c.opacity);
    auto col = [&](int r, int g, int b, float mul) {
        return IM_COL32(r, g, b, (int)(255.0f * a * mul));
    };
    auto inset = [&](float keep) {
        const float ix = w * (1.0f - keep) * 0.5f, iy = h * (1.0f - keep) * 0.5f;
        return std::pair<ImVec2, ImVec2>{ImVec2(tl.x + ix, tl.y + iy),
                                        ImVec2(br.x - ix, br.y - iy)};
    };

    // Everything outside the console's picture is not going to be on the TV at
    // all - dim it so the framing that matters reads clearly.
    if (c.frame && (w < size.x - 1.0f || h < size.y - 1.0f)) {
        const ImU32 shade = col(0, 0, 0, 0.55f);
        if (tl.y > pos.y) dl->AddRectFilled(pos, ImVec2(pos.x + size.x, tl.y), shade);
        if (br.y < pos.y + size.y)
            dl->AddRectFilled(ImVec2(pos.x, br.y), ImVec2(pos.x + size.x, pos.y + size.y), shade);
        if (tl.x > pos.x) dl->AddRectFilled(ImVec2(pos.x, tl.y), ImVec2(tl.x, br.y), shade);
        if (br.x < pos.x + size.x)
            dl->AddRectFilled(ImVec2(br.x, tl.y), ImVec2(pos.x + size.x, br.y), shade);
    }
    if (c.frame) {
        dl->AddRect(tl, br, col(255, 255, 255, 0.85f), 0.0f, 0, scaled(1.5f));
        const char* lbl = wide ? "16:9" : "4:3";
        dl->AddText(ImVec2(tl.x + scaled(5.0f), tl.y + scaled(3.0f)),
                    col(255, 255, 255, 0.8f), lbl);
    }
    // PAL's full-height frame shows 512 rendered lines where NTSC shows 448, so
    // the SAME scene reveals more at the top and bottom in Europe. Only draw the
    // difference when the project actually asks for it - otherwise both regions
    // get the identical letterboxed picture and a second rectangle would be a
    // lie. See ProjectSettings::palFullHeight.
    if (c.bothRegions && project_.settings.palFullHeight &&
        project_.settings.displayMode == "interlaced") {
        const float ntscFrac = 448.0f / 512.0f;
        const auto [nt, nb] = inset(1.0f);
        const float iy = h * (1.0f - ntscFrac) * 0.5f;
        dl->AddRect(ImVec2(nt.x, nt.y + iy), ImVec2(nb.x, nb.y - iy),
                    col(120, 200, 255, 0.9f), 0.0f, 0, scaled(1.5f));
        dl->AddText(ImVec2(nt.x + scaled(5.0f), nt.y + iy + scaled(3.0f)),
                    col(120, 200, 255, 0.9f), "NTSC picture");
    }
    if (c.action) {
        const auto [p0, p1] = inset(0.90f);
        dl->AddRect(p0, p1, col(255, 210, 90, 0.9f), 0.0f, 0, scaled(1.0f));
        dl->AddText(ImVec2(p0.x + scaled(4.0f), p0.y + scaled(2.0f)),
                    col(255, 210, 90, 0.85f), "action 90%");
    }
    if (c.title) {
        const auto [p0, p1] = inset(0.80f);
        dl->AddRect(p0, p1, col(120, 235, 140, 0.9f), 0.0f, 0, scaled(1.0f));
        dl->AddText(ImVec2(p0.x + scaled(4.0f), p0.y + scaled(2.0f)),
                    col(120, 235, 140, 0.85f), "title 80%");
    }
    if (c.centre) {
        const ImU32 gc = col(255, 255, 255, 0.35f);
        const float cx = (tl.x + br.x) * 0.5f, cy = (tl.y + br.y) * 0.5f;
        const float arm = scaled(11.0f);
        dl->AddLine(ImVec2(cx - arm, cy), ImVec2(cx + arm, cy), gc, scaled(1.0f));
        dl->AddLine(ImVec2(cx, cy - arm), ImVec2(cx, cy + arm), gc, scaled(1.0f));
        for (int i = 1; i <= 2; ++i) {  // rule of thirds
            const float x = tl.x + w * (i / 3.0f), y = tl.y + h * (i / 3.0f);
            dl->AddLine(ImVec2(x, tl.y), ImVec2(x, br.y), gc, scaled(1.0f));
            dl->AddLine(ImVec2(tl.x, y), ImVec2(br.x, y), gc, scaled(1.0f));
        }
    }
}

// The viewport's gear: overlay/guide switches live here rather than on the
// toolbar, so they cost no screen space until wanted.
// The gear is square and as tall as the bottom row's slot, and sits at the same
// baseline - see viewportGearSpan().
float App::viewportGearSpan() const {
    return 8.0f + ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.x;
}

bool App::drawViewportGear(const ImVec2& pos, const ImVec2& size) {
    // The inset and the height are the bottom row's, not the gear's own: the
    // two are one row and any disagreement here puts the gear back on top of
    // "Center view".
    const float pad = 8.0f;
    const float btn = ImGui::GetFrameHeight();
    if (size.x < btn * 4.0f || size.y < btn * 4.0f) return false;
    // The row's baseline, then centred on it: a SmallButton is only a font tall,
    // so a square gear left at the same top edge would hang below the row.
    const float rowTop = pos.y + size.y - btn - pad;
    const float top = rowTop + (ImGui::GetFontSize() - btn) * 0.5f;
    ImGui::SetCursorScreenPos(ImVec2(pos.x + pad, top));
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.0f, 0.0f, 0.35f));
    const bool clicked = ImGui::Button("##viewgear", ImVec2(btn, btn));
    ImGui::PopStyleColor();
    const bool hovered = ImGui::IsItemHovered();
    // A gear glyph is not in the default font, so draw one: a ring plus teeth.
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 c(pos.x + pad + btn * 0.5f, top + btn * 0.5f);
        const ImU32 col = IM_COL32(235, 235, 235, hovered ? 255 : 190);
        const float r = btn * 0.26f;
        dl->AddCircle(c, r, col, 12, scaled(1.6f));
        for (int i = 0; i < 6; ++i) {
            const float t = (float)i / 6.0f * 6.2831853f;
            const float cx = std::cos(t), sy = std::sin(t);
            dl->AddLine(ImVec2(c.x + cx * r, c.y + sy * r),
                        ImVec2(c.x + cx * (r + btn * 0.14f), c.y + sy * (r + btn * 0.14f)),
                        col, scaled(1.6f));
        }
    }
    if (hovered) ImGui::SetTooltip("Viewport output and guides");
    if (clicked) ImGui::OpenPopup("##viewguides");
    if (ImGui::BeginPopup("##viewguides")) {
        // The output mode sits above the guides because it draws the same
        // rectangle they do - only for real, by rendering into it.
        ImGui::SeparatorText("Output");
        if (ImGui::Checkbox("PS2 output (GS)", &viewportPs2_)) saveGlobalConfig();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Render the scene at the GS framebuffer size of\n"
                              "Preferences > Display mode and show it the way a\n"
                              "TV does: console pixels, console framing.\n"
                              "Off = the editor's own full-resolution image.");
        if (viewportPs2_ && hasProject_) {
            const Viewport::Ps2Output o = ps2ViewportOutput();
            ImGui::TextDisabled("%dx%d into %s", o.bufW, o.bufH,
                                project_.settings.widescreen ? "16:9" : "4:3");
        }

        ImGui::SeparatorText("TV safe areas");
        ImGui::Checkbox("Show guides", &showSafeArea_);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("A television crops the edges of the picture\n"
                              "(overscan). These are the broadcast insets that\n"
                              "survived it - keep anything important inside.");
        ImGui::BeginDisabled(!showSafeArea_);
        ImGui::Checkbox("Picture frame", &safeArea_.frame);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("The rectangle the console actually outputs, with\n"
                              "everything outside it dimmed. The viewport is\n"
                              "whatever shape you docked it; the TV is not.");
        ImGui::Checkbox("Action safe (90%)", &safeArea_.action);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Nothing important - a character, a pickup, the\n"
                              "crosshair - outside this.");
        ImGui::Checkbox("Title safe (80%)", &safeArea_.title);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Text and HUD readouts inside this, or a set with\n"
                              "heavy overscan clips them.");
        ImGui::Checkbox("Centre + thirds", &safeArea_.centre);
        static const char* kAspects[] = {"Follow project", "4:3", "16:9"};
        ImGui::SetNextItemWidth(scaled(140.0f));
        ImGui::Combo("Aspect", &safeArea_.aspect, kAspects, 3);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Follow project reads Preferences > Widescreen.\n"
                              "Force the other one to check how the same shot\n"
                              "frames there without changing the project.");
        // Only meaningful when the two regions really do show different amounts
        // of picture, which in this engine means region-following interlaced
        // output with the PAL-picture preference on.
        const bool palDiff = project_.settings.palFullHeight &&
                             project_.settings.displayMode == "interlaced";
        ImGui::BeginDisabled(!palDiff);
        ImGui::Checkbox("NTSC picture inside PAL", &safeArea_.bothRegions);
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip(
                palDiff ? "This project boots full-height PAL on a PAL console:\n"
                          "512 rendered lines against NTSC's 448, so Europe sees\n"
                          "MORE at the top and bottom. The inner box is what an\n"
                          "NTSC set shows."
                        : "Only differs when the project targets full-height PAL\n"
                          "(Preferences > PAL picture, with the region-following\n"
                          "interlaced mode). Otherwise both regions get the same\n"
                          "letterboxed picture and a second box would be a lie.");
        ImGui::SetNextItemWidth(scaled(140.0f));
        ImGui::SliderFloat("Opacity", &safeArea_.opacity, 0.1f, 1.0f, "%.2f");
        ImGui::EndDisabled();
        if (ImGui::Button("Close")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return true;
    }
    return hovered;
}

// The measuring tape's own drawing: the segment between the two points with
// its endpoints, and a readout at the midpoint. Everything is placed through
// Viewport::projectToImage, so it tracks the image under any projection.
void App::drawMeasureOverlay(ImVec2 imgPos, ImVec2 avail) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImU32 col = IM_COL32(255, 205, 90, 235);
    auto toScreen = [&](const float w[3], ImVec2& out) {
        float u, v;
        if (!viewport_.projectToImage(w, u, v)) return false;
        out = ImVec2(imgPos.x + u * avail.x, imgPos.y + v * avail.y);
        return true;
    };

    if (measurePoints_ == 0) {
        ImGui::SetCursorScreenPos(ImVec2(imgPos.x + 8, imgPos.y + 32));
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.35f, 1.0f),
                           "Measure: click the first point");
        return;
    }

    ImVec2 a, b;
    const bool aOk = toScreen(measureA_, a);
    const bool bOk = toScreen(measureB_, b);
    if (aOk) dl->AddCircleFilled(a, scaled(4.0f), col);
    if (bOk && measurePoints_ >= 1) dl->AddCircleFilled(b, scaled(4.0f), col);
    if (aOk && bOk) dl->AddLine(a, b, col, scaled(2.0f));

    const float d[3] = {measureB_[0] - measureA_[0], measureB_[1] - measureA_[1],
                        measureB_[2] - measureA_[2]};
    const float dist = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    const float ups = project_.settings.unitsPerMeter;
    char line1[96], line2[96];
    std::snprintf(line1, sizeof line1, "%.3f units  =  %.3f m", dist, dist / ups);
    // The per-axis split is what turns a measurement into a number you can
    // type into a scale or a position field.
    std::snprintf(line2, sizeof line2, "dx %.2f  dy %.2f  dz %.2f", d[0], d[1],
                  d[2]);

    if (aOk && bOk) {
        const ImVec2 mid((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f);
        const ImVec2 s1 = ImGui::CalcTextSize(line1);
        const ImVec2 s2 = ImGui::CalcTextSize(line2);
        const float w = std::max(s1.x, s2.x), h = s1.y + s2.y;
        const float pad = scaled(5.0f);
        const ImVec2 tl(mid.x - w * 0.5f - pad, mid.y - h - scaled(14.0f));
        dl->AddRectFilled(tl, ImVec2(tl.x + w + pad * 2, tl.y + h + pad * 2),
                          IM_COL32(20, 20, 24, 205), scaled(3.0f));
        dl->AddText(ImVec2(tl.x + pad, tl.y + pad), col, line1);
        dl->AddText(ImVec2(tl.x + pad, tl.y + pad + s1.y),
                    IM_COL32(210, 210, 215, 230), line2);
    }

    // Same numbers in the corner: the label above can end up off-screen when
    // one end of the tape is behind the camera.
    ImGui::SetCursorScreenPos(ImVec2(imgPos.x + 8, imgPos.y + 32));
    ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.35f, 1.0f), "Measure: %s   (%s)%s",
                       line1, line2,
                       measurePoints_ == 1 ? "  - click the second point" : "");
}

bool App::objectWorldSize(const SceneObject& o, float out[3]) {
    if (o.type == PrimitiveType::Model) {
        float mn[3], mx[3];
        if (!viewport_.modelLocalBounds(o, mn, mx)) return false;
        for (int c = 0; c < 3; ++c) out[c] = (mx[c] - mn[c]) * o.scale[c];
        return true;
    }
    // Every other drawn primitive is a unit shape scaled by the transform
    // (primmesh: a 1x1x1 shape around the origin), so the scale IS the size.
    // The flat ones have no thickness, and which axis is the flat one differs:
    // a Plane lies in XZ, the quads (decal/mirror/portal) stand in XY.
    switch (o.type) {
        case PrimitiveType::Box:
        case PrimitiveType::Sphere:
        case PrimitiveType::Cylinder:
        case PrimitiveType::Cone:
        case PrimitiveType::SavePoint:
            for (int c = 0; c < 3; ++c) out[c] = o.scale[c];
            return true;
        case PrimitiveType::Plane:
            out[0] = o.scale[0], out[1] = 0.0f, out[2] = o.scale[2];
            return true;
        case PrimitiveType::Decal:
        case PrimitiveType::Mirror:
        case PrimitiveType::Portal:
            out[0] = o.scale[0], out[1] = o.scale[1], out[2] = 0.0f;
            return true;
        default:
            return false;  // markers (spawn, player, light, camera, empty...)
    }
}

bool App::drawAxisGizmo(ImVec2 imgPos, ImVec2 avail) {
    if (!showAxisGizmo_) return false;
    const float radius = scaled(42.0f);   // hub -> axis tip
    const float ballR = scaled(8.5f);
    const float pad = scaled(14.0f);
    // A viewport docked down to a sliver has no room for it.
    if (avail.x < radius * 4.0f || avail.y < radius * 4.0f) return false;
    const ImVec2 hub(imgPos.x + avail.x - radius - ballR - pad,
                     imgPos.y + radius + ballR + pad);

    // World axis k in view space: the view matrix is column-major, so column k
    // (V[k*4 + r]) is where that axis points on screen. Screen Y grows down.
    const float* V = viewport_.viewMatrix();
    static const char* kLabel[3] = {"X", "Y", "Z"};
    static const ImU32 kColor[3] = {IM_COL32(226, 88, 96, 255),     // X red
                                    IM_COL32(126, 200, 78, 255),    // Y green
                                    IM_COL32(70, 145, 240, 255)};   // Z blue
    // Ball i (axis i/2, negative when odd) -> the view you get by standing on
    // that end of the axis and looking back at the scene.
    static const Viewport::Projection kProj[6] = {
        Viewport::Projection::OrthoRight, Viewport::Projection::OrthoLeft,
        Viewport::Projection::OrthoTop,   Viewport::Projection::OrthoBottom,
        Viewport::Projection::OrthoFront, Viewport::Projection::OrthoBack};
    static const char* kTip[6] = {"Right view", "Left view",  "Top view",
                                  "Bottom view", "Front view", "Back view"};

    struct Ball {
        int i = 0;
        ImVec2 pos;
        float depth = 0.0f;  // view-space z: bigger = nearer the camera
    };
    Ball balls[6];
    for (int i = 0; i < 6; ++i) {
        const int axis = i / 2;
        const float s = (i & 1) ? -1.0f : 1.0f;
        const float dx = V[axis * 4 + 0] * s, dy = V[axis * 4 + 1] * s,
                    dz = V[axis * 4 + 2] * s;
        balls[i].i = i;
        balls[i].pos = ImVec2(hub.x + dx * radius, hub.y - dy * radius);
        balls[i].depth = dz;
    }
    int order[6] = {0, 1, 2, 3, 4, 5};  // painter's order: far ones first
    std::sort(order, order + 6, [&](int a, int b) {
        return balls[a].depth < balls[b].depth;
    });

    ImGuiIO& io = ImGui::GetIO();
    const float dxm = io.MousePos.x - hub.x, dym = io.MousePos.y - hub.y;
    const float mouseDist2 = dxm * dxm + dym * dym;
    const float reach = radius + ballR + scaled(3.0f);
    // IsWindowHovered so an open modal (or another window over the corner)
    // makes the widget inert instead of eating clicks meant for it.
    const bool overWidget = mouseDist2 <= reach * reach &&
                            ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows);

    // Nearest ball under the cursor, front-most first so an axis pointing at
    // the camera wins over the one it covers.
    int hovered = -1;
    if (overWidget) {
        const float grab = ballR + scaled(3.0f);
        for (int k = 5; k >= 0 && hovered < 0; --k) {
            const Ball& b = balls[order[k]];
            const float bx = io.MousePos.x - b.pos.x, by = io.MousePos.y - b.pos.y;
            if (bx * bx + by * by <= grab * grab) hovered = b.i;
        }
    }
    const bool overHub =
        overWidget && hovered < 0 && mouseDist2 <= (ballR * 1.6f) * (ballR * 1.6f);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->AddCircleFilled(hub, reach,
                        IM_COL32(18, 20, 26, overWidget ? 120 : 45), 32);

    for (int k = 0; k < 6; ++k) {
        const Ball& b = balls[order[k]];
        const int axis = b.i / 2;
        const bool neg = (b.i & 1) != 0;
        const bool active = viewport_.projection() == kProj[b.i];
        const bool hot = hovered == b.i;
        const ImU32 col = kColor[axis];
        // Positive ends carry the stem and the letter; negative ends are
        // hollow until you hover them - the same reading as Blender's.
        if (!neg)
            dl->AddLine(hub, b.pos, col, scaled(2.0f));
        if (!neg || hot || active) {
            dl->AddCircleFilled(b.pos, ballR, col, 16);
        } else {
            dl->AddCircleFilled(b.pos, ballR, IM_COL32(30, 32, 38, 200), 16);
            dl->AddCircle(b.pos, ballR, col, 16, scaled(1.6f));
        }
        if (active || hot)
            dl->AddCircle(b.pos, ballR + scaled(2.5f),
                          IM_COL32(255, 255, 255, active ? 220 : 120), 16,
                          scaled(1.5f));
        if (!neg || hot) {
            const ImVec2 ts = ImGui::CalcTextSize(kLabel[axis]);
            dl->AddText(ImVec2(b.pos.x - ts.x * 0.5f, b.pos.y - ts.y * 0.5f),
                        IM_COL32(20, 20, 24, 255), kLabel[axis]);
        }
    }
    // Hub: the perspective/parallel switch (Blender has no such button, but
    // the widget is exactly where the hand already is).
    dl->AddCircleFilled(hub, ballR * (overHub ? 0.85f : 0.6f),
                        viewport_.orthographic() ? IM_COL32(235, 235, 240, 230)
                                                 : IM_COL32(130, 134, 145, 210),
                        16);

    if (hovered >= 0)
        ImGui::SetTooltip("%s - orthographic along the %s%s axis", kTip[hovered],
                          (hovered & 1) ? "-" : "+", kLabel[hovered / 2]);
    else if (overHub)
        ImGui::SetTooltip("%s (click to switch)", viewport_.orthographic()
                                                      ? "Orthographic"
                                                      : "Perspective");

    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        if (hovered >= 0) setViewProjection(kProj[hovered]);
        else if (overHub)
            setViewProjection(viewport_.orthographic()
                                  ? Viewport::Projection::Perspective
                                  : Viewport::Projection::Ortho);
    }
    return overWidget;
}

void App::setViewProjection(Viewport::Projection p) {
    viewport_.setProjection(p);
    // Editor state, not a model edit: saveProject() reads it back out of the
    // viewport, so it lands in the .tyra with the next save (like gizmoOp).
    project_.viewProjection = (int)p;
    statusMessage_ = std::string("View: ") + Viewport::projectionName(p);
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
    // Without a terrain the size is still the scene's world bounds, so it stays
    // on this row - what changes is that there is no ground (docs/terrain.md).
    ImGui::Text(project_.active().terrain.enabled
                    ? "%d x %d units (scene %s)"
                    : "none - %d x %d world (scene %s)",
                project_.active().terrain.width, project_.active().terrain.depth,
                project_.active().name.c_str());

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
                project_.scenes[i].name +
                (i == project_.startScene ? "  (start)" : "") + "##scene";
            if (ImGui::Selectable(label.c_str(), project_.activeScene == i,
                                  ImGuiSelectableFlags_AllowOverlap) &&
                project_.activeScene != i) {
                project_.activeScene = i;
                clearSelection();
                cancelPastePlacement();  // staged copies belong to the old scene
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
    project_.viewProjection = (int)viewport_.projection();
    // Fold the live docking arrangement + open windows into the active layout.
    // While a switch is still settling (load or rebuild pending) the on-screen
    // layout doesn't yet belong to the active layout - keep the stored one
    // instead of clobbering it.
    captureActiveLayout();
    if (auto err = project::save(project_); !err.empty())
        platform::errorBox("Save Project", err);
    // Terrain heightmaps live in separate <scene>.heights files (not the .tyra)
    // and, like the rest of the model, are persisted only on demand. They are
    // kept in memory (and in undo snapshots) during editing.
    if (auto err = project::saveHeights(project_); !err.empty())
        platform::errorBox("Save Terrain", err);
    // Terrain splatmaps (paint weights) live in <scene>.splat sidecars, same
    // deal as the heightmaps.
    if (auto err = project::saveSplat(project_); !err.empty())
        platform::errorBox("Save Terrain", err);
}

void App::saveAll(const char* status) {
    saveProject();
    if (auto err = project::saveHistory(project_, history_); !err.empty())
        platform::errorBox("Save History", err);
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
    if (key == "terrain") return &showTerrainEditor_;
    if (key == "ui") return &showUiEditor_;
    if (key == "fonts") return &showFontManager_;
    if (key == "input") return &showInputMap_;
    if (key == "menus") return &showMenusEditor_;
    if (key == "save") return &showSaveEditor_;
    if (key == "grading") return &showGradingEditor_;
    if (key == "ambience") return &showAmbienceEditor_;
    if (key == "loading") return &showLoadingEditor_;
    if (key == "credits") return &showCreditsEditor_;
    if (key == "disc") return &showDiscLayout_;
    if (key == "anim") return &showAnimEditor_;
    if (key == "tree") return &showTreeGenerator_;
    if (key == "proc") return &showProcedural_;
    if (key == "prefabs") return &showPrefabs_;
    if (key == "drone") return &showDroneGenerator_;
    if (key == "gibake") return &showGiBake_;
    if (key == "debugger") return &showDebugger_;
    if (key == "pad") return &showRemotePad_;
    if (key == "phonecam") return &showPhoneCamWindow_;
    if (key == "assets") return &showAssetBrowser_;
    return nullptr;
}

// The optional windows a layout can carry, in a stable order (also the capture
// order). Core windows (Viewport/Project/Properties/Flow Graph/Output/Debug)
// are always drawn and never listed here.
//
// This list and showFlagForKey above must agree: a key here that showFlagForKey
// does not know is ignored, and a window flag missing HERE is the worse half -
// layouts can then neither capture nor reset it, so it leaks across switches
// while every other optional window is deterministic ("input" was that way
// until PROGRESS 221). Adding a window? Add it to both. Layouts persist the
// keys by name (project.cpp writes them as a JSON string array), so the order
// is cosmetic - append rather than insert to keep saved files diff-friendly.
static const char* const kLayoutWindowKeys[] = {
    "cutscene", "material", "terrain",  "ui",       "fonts",  "menus",
    "grading",  "ambience", "loading",  "disc",     "anim",   "tree",
    "debugger", "phonecam", "assets",   "gibake",   "input",  "drone",
    "pad",      "proc",     "prefabs", "save"};

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
    case LayoutRecipe::Debugger: {
        // The debugging desk: the graph being watched fills the middle, the
        // Debugger (state, watch, timeline, breakpoints) owns the right column,
        // and the Viewport sits behind the graph so a glance at the scene is one
        // tab away. Output/Debug logs along the bottom - a crashed game shows up
        // there, not in the graph.
        ImGuiID right =
            ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.30f, nullptr, &center);
        ImGuiID bottom =
            ImGui::DockBuilderSplitNode(center, ImGuiDir_Down, 0.24f, nullptr, &center);
        ImGui::DockBuilderDockWindow("Debugger", right);
        ImGui::DockBuilderDockWindow("Properties", right);
        ImGui::DockBuilderDockWindow("Output", bottom);
        ImGui::DockBuilderDockWindow("Debug", bottom);
        ImGui::DockBuilderDockWindow("Viewport", center);
        ImGui::DockBuilderDockWindow("Flow Graph", center);
        pendingFocusWindow_ = "Flow Graph";
        break;
    }
    case LayoutRecipe::Procedural: {
        // Scattering desk. The graph and the viewport are used TOGETHER - you
        // drag a density slider and watch the world change - so neither may
        // hide the other: the graph takes the bottom half (it is wide and
        // short, like the dopesheet) and the viewport keeps the middle.
        // Properties gets the right column because a volume's own BOX is the
        // region the graph fills, which makes its transform a graph parameter
        // in everything but name.
        //
        // Prefabs is a bottom TAB rather than a side panel on purpose: it is
        // consulted (what does one instance cost, which members merge) rather
        // than watched, and its member table needs real width - in a 0.22 side
        // column every column of it truncates to three characters, while the
        // bottom dock hands it the whole window when you switch to it.
        ImGuiID left =
            ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, 0.18f, nullptr, &center);
        ImGuiID right =
            ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.20f, nullptr, &center);
        ImGuiID bottom =
            ImGui::DockBuilderSplitNode(center, ImGuiDir_Down, 0.46f, nullptr, &center);
        ImGui::DockBuilderDockWindow("Project", left);
        ImGui::DockBuilderDockWindow("Properties", right);
        ImGui::DockBuilderDockWindow("Output", bottom);
        ImGui::DockBuilderDockWindow("Debug", bottom);
        ImGui::DockBuilderDockWindow("Prefabs", bottom);
        ImGui::DockBuilderDockWindow("Procedural", bottom);
        ImGui::DockBuilderDockWindow("Flow Graph", center);
        ImGui::DockBuilderDockWindow("Viewport", center);
        pendingFocusWindow_ = "Procedural";
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
    ++modelEditSerial_;  // let the session diff pick up this edit (see sessionTick)
    // The undo snapshot only carries the SCENES, so push() returns false for an
    // edit to any project-wide collection - menus, the Input Map, gradings,
    // sequences, save values... Dirtying on push alone therefore left those
    // edits with a dark save icon and NO prompt on exit, i.e. quietly losable.
    // A commit is by definition an edit worth saving, so mark dirty either way;
    // push() still decides whether it becomes an undo step.
    history_.push({project_.scenes});
    setDirty(true);
}

void App::setDirty(bool dirty) {
    // Any dirtying edit advances the session serial - some model edits mark
    // dirty without going through commitChange (UI Editor, layouts). Done
    // BEFORE the no-op early-out below, which gates only the title refresh.
    if (dirty) ++modelEditSerial_;
    if (dirty_ == dirty) return;
    dirty_ = dirty;
    updateWindowTitle();
}

void App::updateWindowTitle() {
    if (!window_) return;
    const bool joined = session_.role() == session::Session::Role::Client;
    // Skip redundant GLFW calls: only push when the shown state changed.
    if (titleShowsDirty_ == dirty_ && titleName_ == project_.name &&
        titleShowsJoined_ == joined)
        return;
    titleShowsDirty_ = dirty_;
    titleShowsJoined_ = joined;
    titleName_ = project_.name;
    std::string title = "TyraX";
    if (hasProject_)
        title += " - " + project_.name + (joined ? " [joined]" : "") +
                 (dirty_ ? " *" : "");
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

// --- Recent projects -------------------------------------------------------

void App::probeRecentProject(RecentProject& r) {
    r.name = projectManifestName(r.dir);
    r.valid = !r.name.empty();
    // A vanished folder still shows its own name - an unreadable path alone
    // tells the user nothing about which project it was.
    if (!r.valid) r.name = std::filesystem::path(r.dir).filename().string();
    if (r.name.empty()) r.name = r.dir;
}

void App::rememberRecentProject(const std::string& dir) {
    if (dir.empty()) return;
    const std::string key = recentProjectKey(dir);
    for (size_t i = 0; i < recentProjects_.size();) {
        if (recentProjectKey(recentProjects_[i].dir) == key)
            recentProjects_.erase(recentProjects_.begin() + i);
        else
            ++i;
    }
    RecentProject r;
    r.dir = dir;
    probeRecentProject(r);
    recentProjects_.insert(recentProjects_.begin(), r);
    if (recentProjects_.size() > kMaxRecentProjects)
        recentProjects_.resize(kMaxRecentProjects);
    saveGlobalConfig();
}

void App::forgetRecentProject(int index) {
    if (index < 0 || index >= (int)recentProjects_.size()) return;
    recentProjects_.erase(recentProjects_.begin() + index);
    saveGlobalConfig();  // the list only ever lives in editor.ini
}

void App::openRecentProject(const std::string& dir) {
    const std::string err = openProjectAt(dir);
    if (err.empty()) return;
    // Moved or deleted since it was probed (the probe runs at startup, not per
    // frame): re-probe so the entry shows as missing, say what went wrong, and
    // KEEP it - whether a project on an unplugged drive is worth forgetting is
    // the user's call.
    const std::string key = recentProjectKey(dir);
    for (RecentProject& r : recentProjects_)
        if (recentProjectKey(r.dir) == key) probeRecentProject(r);
    platform::errorBox("Open Project", err);
}

void App::requestOpenRecent(const std::string& dir) {
    if (hasProject_ && dirty_) {
        pendingAction_ = PendingAction::OpenRecent;
        pendingRecentDir_ = dir;
        openDiscardPopup_ = true;
    } else {
        openRecentProject(dir);
    }
}

// File > Recent Projects: the same list the welcome screen draws, reachable
// while a project is open - "switch back to yesterday's project" is otherwise
// close-then-file-dialog. Entries stay clickable when they probed as missing:
// the probe is from startup, and the drive may well be back.
void App::drawRecentProjectsMenu() {
    if (!ImGui::BeginMenu("Recent Projects", !recentProjects_.empty())) return;

    // Resolved after the loop: opening (or clearing) rewrites recentProjects_,
    // which we are iterating.
    std::string pick;
    bool clear = false;
    for (int i = 0; i < (int)recentProjects_.size(); ++i) {
        const RecentProject& r = recentProjects_[i];
        ImGui::PushID(i);
        // Two projects can share a name, so the full folder rides in the
        // tooltip rather than in the label - a path in a menu item makes the
        // whole menu as wide as the deepest project on the machine.
        const std::string label = r.valid ? r.name : r.name + "  (missing)";
        if (ImGui::MenuItem(label.c_str())) pick = r.dir;
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s%s", r.dir.c_str(),
                              r.valid ? "" : "\n(no .tyra project file here)");
        ImGui::PopID();
    }
    ImGui::Separator();
    if (ImGui::MenuItem("Clear list")) clear = true;
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Forgets the shortcuts only - nothing on disk is touched");
    ImGui::EndMenu();

    if (!pick.empty()) requestOpenRecent(pick);
    else if (clear) {
        recentProjects_.clear();
        saveGlobalConfig();  // the list only ever lives in editor.ini
    }
}

std::string App::openProjectAt(const std::string& dir) {
    Project p;
    std::string err = project::load(p, dir);
    if (!err.empty()) return err;
    project_ = p;
    hasProject_ = true;
    applyProjectToViewport();
    attachProject();  // resets dirty + window title
    // project_.dir, not `dir`: load normalizes it (and accepts a .tyra path).
    rememberRecentProject(project_.dir);
    return {};
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

void App::requestCloseProject() {
    if (hasProject_ && dirty_) {
        pendingAction_ = PendingAction::Close;
        openDiscardPopup_ = true;
    } else {
        closeProject();
    }
}

// Back to the screen the editor boots on: the project is released and the
// Viewport draws the welcome screen (New / Open / recent projects) again. The
// unsaved-edit guard already ran in requestCloseProject().
//
// Most of the teardown is free: the per-frame channels (Live Link, the
// debugger, Live Logic, the Remote Pad) and every tool window stand down on
// their own the moment hasProject_ is false. What is left is the state that
// would otherwise outlive the project - worker threads still writing into it,
// and the disk-derived caches a DIFFERENT project must not inherit.
void App::closeProject() {
    if (!hasProject_) return;

    // Stopping the link also stops a recording in progress, which bakes its
    // keys into the sequence first - so a close never silently eats a take.
    if (phoneCam_.listening()) stopPhoneCam();
    if (giBaker_.running()) giBaker_.cancel();
    // A build has no UI left once the toolbar goes away (Stop lives there), so
    // it would run to completion with no way to cancel it.
    if (runner_.busy()) runner_.cancel();
    // The Drone Generator, in the same order the shutdown path uses (audio
    // first: the device callback holds the LiveSynth, and the render thread
    // writes into droneRenderResult_). The audition has to stop because its
    // window - and with it Stop - hides on !hasProject_, so a drone would play
    // on over the welcome screen with no control left. The offline render has
    // to be cancelled because droneTickRender() deliberately runs BEFORE that
    // same guard (so a render survives closing the window), and its completion
    // path appends the track to project_.music and calls saveAll() - after a
    // close that writes into an empty Project, or into whichever project is
    // opened next.
    if (droneAuditioning_) droneStop();
    if (droneRendering_) {
        droneRenderCancel_.store(true);
        if (droneRenderThread_.joinable()) droneRenderThread_.join();
        droneRendering_ = false;
        droneRenderDone_.store(false);
        droneRenderResult_ = dronegen::RenderResult();
        droneStatus_ = "Render cancelled - the project was closed.";
    }
    if (session_.active()) {
        session_.close();
        peerPresence_.clear();
        viewport_.setPeerSelections({});
    }

    project_ = Project();  // one empty scene - objects() stays safe to call
    hasProject_ = false;
    dirty_ = false;
    history_.reset({project_.scenes});
    clearSelection();
    pastePending_ = false;
    pasteStaged_.clear();
    flowGraphObject_ = -1;
    flowPositionsApplied_ = false;
    // Every one of these is keyed by a project-relative path.
    viewport_.invalidateAssets();
    layerRamCache_.clear();
    wavIssueCache_.clear();
    modelInfoCache_.clear();
    glbInfoCache_.clear();  // always invalidated with modelInfoCache_
    // The error catcher tails the open project's logs; the next open baselines
    // it again (attachProject). The runner log survives the close, so its size
    // has to stay honest or the next poll reads a shrink that never happened.
    errorSeenSig_.clear();
    errorGameLogSize_ = 0;
    errorRunnerLogSize_ = runner_.log().size();
    openErrorPopup_ = false;
    statusMessage_ = "Project closed";
    updateWindowTitle();
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
        case PendingAction::OpenRecent: {
            const std::string dir = std::move(pendingRecentDir_);
            pendingRecentDir_.clear();
            openRecentProject(dir);
            break;
        }
        case PendingAction::New:
            openNewProjectPopup_ = true;
            break;
        case PendingAction::Close:
            closeProject();
            break;
        case PendingAction::JoinSession:
            openJoinSessionPopup_ = true;
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

// --- Collaboration session -------------------------------------------------
// The Session (session.hpp) runs the network side on a worker thread; this
// block is the UI half: per-frame event drain, the Host/Join modals, the
// Session window and the project handoff when a join finishes syncing.

// Peer colors, indexed by PeerView::colorIdx (host = 0). Used by the Session
// window rows and (later) the viewport presence highlights.
static const ImU32 kPeerColors[session::kMaxPeers] = {
    IM_COL32(95, 200, 115, 255),   // 0 host - green
    IM_COL32(80, 160, 245, 255),   // blue
    IM_COL32(240, 175, 70, 255),   // amber
    IM_COL32(225, 100, 210, 255),  // magenta
    IM_COL32(90, 210, 205, 255),   // teal
    IM_COL32(245, 130, 100, 255),  // coral
    IM_COL32(180, 150, 250, 255),  // violet
    IM_COL32(215, 205, 95, 255),   // olive
};

void App::sessionTick() {
    const auto role = session_.role();
    const bool host = role == session::Session::Role::Host;
    bool appliedInbound = false;

    // Cheap when idle: one mutex-guarded empty drain + a small vector copy.
    for (auto& e : session_.drainEvents()) {
        switch (e.type) {
            case session::AppEvent::Type::PeerJoined:
                statusMessage_ = e.peer.name + " joined the session";
                break;
            case session::AppEvent::Type::PeerLeft:
                statusMessage_ = e.peer.name + (e.text == "kicked"  ? " was kicked"
                                                : e.text == "timeout"
                                                    ? " timed out"
                                                    : " left the session");
                peerPresence_.erase(e.peer.id);
                break;
            case session::AppEvent::Type::SyncDone:
                openRemoteProject(e.text);
                break;
            case session::AppEvent::Type::Refreshed:
                // Mid-session file refresh finished: assets on disk may have
                // changed under us - drop every disk-derived cache and rescan.
                viewport_.invalidateAssets();
                wavIssueCache_.clear();
                modelInfoCache_.clear();
                rescanAssets(false);
                applyProjectToViewport();
                statusMessage_ = "Project files refreshed from the host";
                break;
            case session::AppEvent::Type::Ended:
                // The worker is already winding down; join it and reset.
                session_.close();
                peerPresence_.clear();
                viewport_.setPeerSelections({});
                updateWindowTitle();  // drop the [joined] marker
                if (joinModalVisible_) {
                    sessionError_ = e.text;  // shown inline in the join modal
                } else {
                    sessionEndedText_ = e.text;
                    openSessionEndedPopup_ = true;
                }
                break;
            case session::AppEvent::Type::Frame:
                // An edit from a peer. Fold it into project_ AND the shadow
                // (so our own diff never re-emits it), then - on the host,
                // which is the total-order hub - rebroadcast to EVERY peer
                // including the origin so concurrent last-write-wins edits
                // converge (TCP preserves the host's order per client).
                if (session::applyEdit(project_, sessionShadow_, e.frame)) {
                    appliedInbound = true;
                    if (host) session_.broadcastFrame(e.frame, -1);
                    break;
                }
                // Not an edit: presence (selection sync). Update the sender's
                // entry; the host relays it to everyone else verbatim.
                {
                    json::Value msg;
                    if (!json::parse(e.frame.json, msg)) break;
                    const json::Value* t = msg.find("t");
                    if (!t || t->stringOr("") != "presence") break;
                    const json::Value* peerF = msg.find("peer");
                    const int peerId =
                        peerF ? (int)peerF->numberOr(-1) : e.peer.id;
                    if (peerId < 0 || peerId == session_.selfId()) break;
                    PeerPresence pp;
                    if (const auto* v = msg.find("scene"))
                        pp.scene = (int)v->numberOr(0);
                    if (const auto* sel = msg.find("sel");
                        sel && sel->type == json::Value::Type::Array)
                        for (const auto& id : sel->arr)
                            if (id.type == json::Value::Type::String)
                                pp.sel.push_back(id.str);
                    peerPresence_[peerId] = std::move(pp);
                    if (host) session_.broadcastFrame(e.frame, e.peer.id);
                }
                break;
        }
    }

    if (appliedInbound) {
        // Remote edits bypass commitChange; refresh the derived state its
        // callers rely on, and push one undo anchor per applied batch (so
        // local undo rewinds remote edits batch-by-batch instead of a
        // whole-session teleport; push() dedupes a no-op echo batch).
        pruneSelection();
        flowPositionsApplied_ = false;
        layerRamCache_.clear();
        applyProjectToViewport();
        history_.push({project_.scenes});
        if (host) {
            setDirty(true);  // the host owns saving; mark unsaved
            session_.setModelFiles(project::manifestFiles(project_));
        }
        // The inbound apply already advanced the shadow, so re-sync our
        // scan cursor: this batch must not diff back out as a local edit.
        sessionScannedSerial_ = modelEditSerial_;
    }

    // Outbound: diff our live model against the shadow whenever an edit moved
    // it past the last scan, and ship the delta (host -> all peers, client ->
    // host). Runs only while the session can carry edits.
    const auto st = session_.state();
    const bool canSend = (host && st == session::Session::State::Listening) ||
                         (!host && st == session::Session::State::Live);
    if (canSend && modelEditSerial_ != sessionScannedSerial_) {
        project::ensureObjectIds(project_);
        session::diffModel(project_, sessionShadow_, [&](wire::Frame f) {
            if (host) session_.broadcastFrame(f, -1);
            else session_.sendFrameToHost(f);
        });
        sessionScannedSerial_ = modelEditSerial_;
        // Keep the joiner snapshot current so a late peer gets these edits.
        if (host) session_.setModelFiles(project::manifestFiles(project_));
    }

    // Presence out: broadcast our selection (as stable object ids) whenever it
    // changed, throttled so a marquee drag doesn't spam frames.
    const double now = ImGui::GetTime();
    if (canSend && now >= presenceNextSend_) {
        std::vector<std::string> sel;
        for (int i : selection_)
            if (i >= 0 && i < (int)project_.objects().size())
                sel.push_back(project_.objects()[i].id);
        if (sel != presenceSentSel_ || project_.activeScene != presenceSentScene_) {
            std::string j = "{\"t\":\"presence\",\"peer\":" +
                            std::to_string(session_.selfId()) +
                            ",\"scene\":" + std::to_string(project_.activeScene) +
                            ",\"sel\":[";
            for (size_t i = 0; i < sel.size(); ++i)
                j += (i ? ",\"" : "\"") + sel[i] + "\"";
            j += "]}";
            wire::Frame f;
            f.json = std::move(j);
            if (host) session_.broadcastFrame(f, -1);
            else session_.sendFrameToHost(f);
            presenceSentSel_ = std::move(sel);
            presenceSentScene_ = project_.activeScene;
            presenceNextSend_ = now + 0.2;
        }
    }

    // Presence in -> viewport: resolve each peer's ids to indices in OUR
    // active scene (ids are the stable cross-editor key; indices shift).
    if (session_.active()) {
        std::vector<Viewport::PeerSel> sels;
        if (!peerPresence_.empty()) {
            std::map<std::string, int> idToIdx;
            for (int i = 0; i < (int)project_.objects().size(); ++i)
                idToIdx[project_.objects()[i].id] = i;
            for (const auto& [peerId, pp] : peerPresence_) {
                if (pp.scene != project_.activeScene || pp.sel.empty()) continue;
                int colorIdx = 0;
                for (const auto& pv : sessionPeers_)
                    if (pv.id == peerId) colorIdx = pv.colorIdx;
                const ImU32 c = kPeerColors[colorIdx % session::kMaxPeers];
                Viewport::PeerSel ps;
                ps.color[0] = ((c >> IM_COL32_R_SHIFT) & 0xFF) / 255.0f;
                ps.color[1] = ((c >> IM_COL32_G_SHIFT) & 0xFF) / 255.0f;
                ps.color[2] = ((c >> IM_COL32_B_SHIFT) & 0xFF) / 255.0f;
                for (const auto& id : pp.sel) {
                    auto it = idToIdx.find(id);
                    if (it != idToIdx.end()) ps.indices.push_back(it->second);
                }
                if (!ps.indices.empty()) sels.push_back(std::move(ps));
            }
        }
        viewport_.setPeerSelections(std::move(sels));
    }

    sessionPeers_ = session_.peers();
}

void App::requestJoinSession() {
    if (hasProject_ && dirty_) {
        pendingAction_ = PendingAction::JoinSession;
        openDiscardPopup_ = true;
    } else {
        openJoinSessionPopup_ = true;
    }
}

void App::startHostSession() {
    // Pre-projectId projects get their id now (persists on the next save);
    // fresh pastes get object ids so the model snapshot is complete.
    project::ensureProjectId(project_);
    project::ensureObjectIds(project_);
    session::HostConfig cfg;
    cfg.port = (uint16_t)sessionPort_;
    cfg.joinCode = sessionCode_;
    cfg.displayName = sessionName_;
    cfg.projectDir = project_.dir;
    cfg.projectId = project_.projectId;
    cfg.projectName = project_.name;
    cfg.modelFiles = project::manifestFiles(project_);
    // Remember the typed name for future sessions (editor.ini).
    if (globalDisplayName_ != sessionName_) {
        globalDisplayName_ = sessionName_;
        saveGlobalConfig();
    }
    session_.host(std::move(cfg));
    // Baseline the diff against the exact model we're serving, so nothing
    // re-emits as a "change" until the user actually edits.
    sessionShadow_ = session::makeShadow(project_);
    sessionScannedSerial_ = modelEditSerial_;
    presenceSentSel_.clear();
    presenceSentScene_ = -1;  // resend presence into the fresh session
    showSessionWindow_ = true;
    statusMessage_ = "Hosting session";
}

void App::closeSession() {
    const bool wasClient = session_.role() == session::Session::Role::Client;
    session_.close();
    showSessionWindow_ = false;
    peerPresence_.clear();
    viewport_.setPeerSelections({});
    updateWindowTitle();  // drop the [joined] marker
    // A client keeps the synced project open as a local snapshot.
    statusMessage_ = wasClient ? "Left the session" : "Session closed";
}

void App::openRemoteProject(const std::string& dir) {
    Project p;
    const std::string err = project::load(p, dir);
    if (!err.empty()) {
        session_.close();
        sessionEndedText_ = "Failed to open the synced project: " + err;
        openSessionEndedPopup_ = true;
        return;
    }
    project_ = p;
    hasProject_ = true;
    applyProjectToViewport();
    sessionAttachKeep_ = true;  // attachProject must not close the session
    attachProject();
    sessionAttachKeep_ = false;
    // Baseline the live-sync shadow against the just-synced model.
    sessionShadow_ = session::makeShadow(project_);
    sessionScannedSerial_ = modelEditSerial_;
    presenceSentSel_.clear();
    presenceSentScene_ = -1;  // resend presence into the fresh session
    showSessionWindow_ = true;
    statusMessage_ = "Joined session: " + project_.name;
}

// Seed the display-name field once per modal open: the configured name
// (Edit > Preferences), else the OS user name - both beat an empty box.
static void seedSessionName(char* buf, size_t n, const std::string& configured) {
    if (buf[0]) return;
    if (!configured.empty()) {
        std::snprintf(buf, n, "%s", configured.c_str());
        return;
    }
    const std::string u = platform::userName();
    std::snprintf(buf, n, "%s", u.empty() ? "user" : u.c_str());
}

void App::drawHostSessionModal() {
    if (openHostSessionPopup_) {
        ImGui::OpenPopup("Host Session");
        openHostSessionPopup_ = false;
        seedSessionName(sessionName_, sizeof(sessionName_), globalDisplayName_);
        if (!sessionCode_[0])
            std::snprintf(sessionCode_, sizeof(sessionCode_), "%s",
                          session::newJoinCode().c_str());
    }
    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Host Session", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted(
            ("Host \"" + project_.name + "\" as a live session.").c_str());
        ImGui::Spacing();
        ImGui::SetNextItemWidth(scaled(220));
        ImGui::InputText("Your name", sessionName_, sizeof(sessionName_));
        ImGui::SetNextItemWidth(scaled(110));
        ImGui::InputInt("Port", &sessionPort_, 0, 0);
        sessionPort_ = ImClamp(sessionPort_, 1024, 65535);
        ImGui::AlignTextToFramePadding();
        ImGui::Text("Join code: %s", sessionCode_);
        ImGui::SameLine();
        if (ImGui::SmallButton("New code"))
            std::snprintf(sessionCode_, sizeof(sessionCode_), "%s",
                          session::newJoinCode().c_str());
        ImGui::Spacing();
        const auto ips = wire::localIPv4();
        if (!ips.empty()) {
            ImGui::TextDisabled("Peers connect to:");
            for (const auto& ip : ips)
                ImGui::TextDisabled("  %s:%d", ip.c_str(), sessionPort_);
        }
        ImGui::TextDisabled(
            "Windows Firewall may ask for permission on the first start.");
        ImGui::Spacing();
        if (ImGui::Button("Start hosting", ImVec2(scaled(120), 0))) {
            startHostSession();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

void App::drawJoinSessionModal() {
    if (openJoinSessionPopup_) {
        ImGui::OpenPopup("Join Session");
        openJoinSessionPopup_ = false;
        seedSessionName(sessionName_, sizeof(sessionName_), globalDisplayName_);
        sessionError_.clear();
    }
    joinModalVisible_ = false;
    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Join Session", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        joinModalVisible_ = true;
        const auto st = session_.state();
        const bool busy = st == session::Session::State::Connecting ||
                          st == session::Session::State::Syncing ||
                          st == session::Session::State::Starting;
        if (busy) {
            const auto prog = session_.syncProgress();
            if (st == session::Session::State::Syncing && prog.filesTotal > 0) {
                ImGui::Text("Transferring project: %d / %d files", prog.filesDone,
                            prog.filesTotal);
                const float frac =
                    prog.bytesTotal
                        ? (float)((double)prog.bytesDone / (double)prog.bytesTotal)
                        : 1.0f;
                char overlay[64];
                std::snprintf(overlay, sizeof(overlay), "%.1f / %.1f MB",
                              prog.bytesDone / 1048576.0, prog.bytesTotal / 1048576.0);
                ImGui::ProgressBar(frac, ImVec2(scaled(320), 0), overlay);
                if (!prog.currentFile.empty())
                    ImGui::TextDisabled("%s", prog.currentFile.c_str());
            } else {
                ImGui::TextUnformatted(st == session::Session::State::Connecting
                                           ? "Connecting..."
                                           : "Preparing transfer...");
            }
            ImGui::Spacing();
            if (ImGui::Button("Cancel")) {
                session_.close();
                ImGui::CloseCurrentPopup();
            }
        } else if (st == session::Session::State::Live) {
            // Sync finished; sessionTick opened the project - close quietly.
            ImGui::CloseCurrentPopup();
        } else {
            ImGui::SetNextItemWidth(scaled(220));
            ImGui::InputText("Host address", sessionAddr_, sizeof(sessionAddr_));
            ImGui::SetNextItemWidth(scaled(110));
            ImGui::InputInt("Port", &sessionPort_, 0, 0);
            sessionPort_ = ImClamp(sessionPort_, 1024, 65535);
            ImGui::SetNextItemWidth(scaled(110));
            ImGui::InputText("Join code", sessionCode_, sizeof(sessionCode_));
            ImGui::SetNextItemWidth(scaled(220));
            ImGui::InputText("Your name", sessionName_, sizeof(sessionName_));
            if (!sessionError_.empty())
                ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.4f, 1.0f), "%s",
                                   sessionError_.c_str());
            ImGui::Spacing();
            if (ImGui::Button("Join", ImVec2(scaled(120), 0))) {
                sessionError_.clear();
                session::JoinConfig cfg;
                cfg.address = sessionAddr_;
                cfg.port = (uint16_t)sessionPort_;
                cfg.joinCode = sessionCode_;
                cfg.displayName = sessionName_;
                cfg.cacheRoot = globalSessionCacheDir_;
                // Remember the typed name for future sessions (editor.ini).
                if (globalDisplayName_ != sessionName_) {
                    globalDisplayName_ = sessionName_;
                    saveGlobalConfig();
                }
                session_.join(std::move(cfg));
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape))
                ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void App::drawSessionEndedModal() {
    if (openSessionEndedPopup_) {
        ImGui::OpenPopup("Session");
        openSessionEndedPopup_ = false;
    }
    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Session", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted(sessionEndedText_.c_str());
        if (session_.role() == session::Session::Role::None && hasProject_)
            ImGui::TextDisabled(
                "The project stays open as a local copy on this machine.");
        ImGui::Spacing();
        if (ImGui::Button("OK", ImVec2(scaled(90), 0)) ||
            ImGui::IsKeyPressed(ImGuiKey_Escape))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

void App::drawSessionWindow() {
    if (!showSessionWindow_) return;
    if (!session_.active()) {
        showSessionWindow_ = false;
        return;
    }
    ImGui::SetNextWindowSize(ImVec2(scaled(340), scaled(260)), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Session", &showSessionWindow_)) {
        ImGui::End();
        return;
    }
    const bool hosting = session_.role() == session::Session::Role::Host;
    if (hosting) {
        ImGui::Text("Hosting \"%s\"", project_.name.c_str());
        ImGui::Text("Port %d - join code %s", sessionPort_, sessionCode_);
        for (const auto& ip : wire::localIPv4())
            ImGui::TextDisabled("  %s:%d", ip.c_str(), sessionPort_);
    } else {
        ImGui::Text("Joined \"%s\" @ %s", project_.name.c_str(), sessionAddr_);
        ImGui::TextDisabled("The host owns saving and committing.");
        if (ImGui::SmallButton("Refresh project files")) session_.requestRefresh();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(
                "Re-syncs assets/sources from the host (fetches files the "
                "host\nimported since you joined; unchanged files are "
                "skipped).\nScene edits stream live and never need this.");
        const auto prog = session_.syncProgress();
        if (prog.filesTotal > prog.filesDone)
            ImGui::TextDisabled("  fetching %d / %d files...", prog.filesDone,
                                prog.filesTotal);
    }
    ImGui::Separator();
    ImGui::TextDisabled("Participants");
    for (const auto& peer : sessionPeers_) {
        ImGui::PushID(peer.id);
        const ImU32 c = kPeerColors[peer.colorIdx % session::kMaxPeers];
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float r = ImGui::GetTextLineHeight() * 0.32f;
        dl->AddCircleFilled(ImVec2(p.x + r, p.y + ImGui::GetTextLineHeight() * 0.55f),
                            r, c);
        ImGui::Dummy(ImVec2(r * 2.0f + 4.0f, 0));
        ImGui::SameLine();
        ImGui::TextUnformatted(peer.name.c_str());
        if (peer.id == 0) {
            ImGui::SameLine();
            ImGui::TextDisabled("(host)");
        }
        // What they're editing (from presence): the scene name.
        if (auto it = peerPresence_.find(peer.id); it != peerPresence_.end() &&
                                                   it->second.scene >= 0 &&
                                                   it->second.scene <
                                                       (int)project_.scenes.size()) {
            ImGui::SameLine();
            ImGui::TextDisabled("- %s", project_.scenes[it->second.scene].name.c_str());
        }
        if (hosting && peer.id != 0) {
            if (!peer.address.empty()) {
                ImGui::SameLine();
                ImGui::TextDisabled("%s", peer.address.c_str());
            }
            ImGui::SameLine(ImGui::GetContentRegionAvail().x - scaled(30));
            if (ImGui::SmallButton("Kick")) session_.kickPeer(peer.id);
        }
        ImGui::PopID();
    }
    ImGui::Separator();
    if (ImGui::Button(hosting ? "Close session" : "Leave session")) closeSession();
    ImGui::End();
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
    // A scatter volume owns its baked chunk objects and their mesh files, so
    // deleting one must take those with it. That erases objects, which shifts
    // every index in the selection - so when a volume is involved the whole
    // deletion switches to ids (stable by construction) instead of indices.
    std::vector<std::string> volumeIds, deleteIds;
    bool byId = true;
    for (int i : selection_) {
        if (i < 0 || i >= (int)project_.objects().size()) continue;
        const SceneObject& o = project_.objects()[i];
        if (o.id.empty()) byId = false;
        deleteIds.push_back(o.id);
        if (o.type == PrimitiveType::Scatter) volumeIds.push_back(o.id);
    }
    if (!volumeIds.empty() && byId) {
        for (const std::string& v : volumeIds)
            procbake::clearVolume(project_, project_.active(), v);
        std::vector<SceneObject>& objs = project_.objects();
        objs.erase(std::remove_if(objs.begin(), objs.end(),
                                  [&](const SceneObject& o) {
                                      return std::find(deleteIds.begin(),
                                                       deleteIds.end(),
                                                       o.id) != deleteIds.end();
                                  }),
                   objs.end());
        clearSelection();
        commitChange();
        return;
    }
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
    // A pending paste is settled by the next Ctrl+V (the keyboard twin of
    // clicking it down); only then does a fresh one start.
    if (pastePending_) {
        commitPastePlacement();
        return;
    }
    if (clipboard_.empty()) return;
    beginPastePlacement();
}

// --- Collision-aware placement -------------------------------------------

aobake::ModelAabbFn App::placementModelAabb() {
    return [this](const SceneObject& o, float mn[3], float mx[3]) {
        return viewport_.modelLocalBounds(o, mn, mx);
    };
}

placement::HeightFn App::placementHeight() const {
    // No terrain, no ground sampler: placement then rests objects on other
    // objects only, and an object over nothing keeps its height (placement.cpp
    // treats an empty HeightFn as "no terrain under the footprint"). Handing it
    // the viewport's y = 0 fallback instead would snap props onto a floor that
    // does not exist (docs/terrain.md).
    if (!project_.active().terrain.enabled) return {};
    return [this](float x, float z) { return viewport_.terrainHeight(x, z); };
}

std::vector<char> App::placementSkip(const std::vector<int>& extra) const {
    std::vector<char> skip(project_.objects().size(), 0);
    for (size_t i = 0; i < project_.objects().size(); ++i)
        if (isObjectHiddenInEditor(project_.objects()[i])) skip[i] = 1;
    for (int i : extra)
        if (i >= 0 && i < (int)skip.size()) skip[i] = 1;
    return skip;
}

void App::snapInsertedObject() {
    if (!placementSnap_ || project_.objects().empty()) return;
    const int last = (int)project_.objects().size() - 1;
    SceneObject& o = project_.objects()[last];
    // No ceiling: a fresh object rests on whatever stands under it, however
    // tall - inserting into an occupied spot stacks instead of intersecting.
    o.position[1] += placement::restOffsetY(o, project_.objects(),
                                            placementSkip({last}),
                                            placementModelAabb(),
                                            placementHeight(), FLT_MAX);
}

void App::dropSelectionToFloor() {
    if (selection_.empty()) return;
    const aobake::ModelAabbFn aabb = placementModelAabb();
    const placement::HeightFn height = placementHeight();
    // Every selected object is its own drop: the ceiling is its own underside,
    // so only surfaces BELOW it can catch it (lift an object over a shelf,
    // press End, it lands on the shelf).
    const std::vector<char> skip = placementSkip(selection_);
    int moved = 0;
    for (int i : selection_) {
        if (i < 0 || i >= (int)project_.objects().size()) continue;
        SceneObject& o = project_.objects()[i];
        const placement::Aabb box = placement::worldAabb(o, aabb);
        const float dy = placement::restOffsetY(o, project_.objects(), skip, aabb,
                                                height, box.mn[1] + 0.001f);
        if (dy != 0.0f) ++moved;
        o.position[1] += dy;
    }
    if (!moved) {
        statusMessage_ = "Nothing to drop onto";
        return;
    }
    commitChange();
    statusMessage_ = moved == 1 ? "Dropped to floor"
                                : "Dropped " + std::to_string(moved) + " objects";
}

// --- Deferred paste -------------------------------------------------------

void App::beginPastePlacement() {
    pasteStaged_.clear();
    for (const SceneObject& src : clipboard_) {
        SceneObject o = src;
        o.id.clear();  // a paste is a new object - it must get its own id
        std::string name = o.name + "-copy";
        for (int n = 2;; ++n) {
            bool taken = false;
            for (const auto& other : project_.objects()) taken |= (other.name == name);
            for (const auto& other : pasteStaged_) taken |= (other.name == name);
            if (!taken) break;
            name = o.name + "-copy" + std::to_string(n);
        }
        o.name = name;
        pasteStaged_.push_back(std::move(o));
    }
    if (pasteStaged_.empty()) return;
    pastePending_ = true;
    pasteMoved_ = false;
    sculptMode_ = paintMode_ = false;  // one viewport tool at a time
    // Nothing is selected while a paste is in flight: the gizmo would fight
    // the cursor for the drag, and the staged copies aren't in the scene yet.
    clearSelection();
    statusMessage_ = "Click to place the paste (Ctrl+V places, Esc cancels)";
}

void App::movePasteStaged(const float point[3]) {
    if (pasteStaged_.empty()) return;
    // The first copy is the anchor: the group keeps its arrangement and lands
    // with that copy's origin on the cursor point.
    const float* anchor = pasteStaged_.front().position;
    const float d[3] = {point[0] - anchor[0], point[1] - anchor[1],
                        point[2] - anchor[2]};
    for (SceneObject& o : pasteStaged_)
        for (int k = 0; k < 3; ++k) o.position[k] += d[k];
    if (placementSnap_) {
        // ONE offset for the whole group (the largest any member needs), so
        // a pasted stack keeps its shape instead of collapsing onto the floor.
        const float dy = placement::restOffsetYGroup(
            pasteStaged_, project_.objects(), placementSkip(),
            placementModelAabb(), placementHeight(), FLT_MAX);
        for (SceneObject& o : pasteStaged_) o.position[1] += dy;
    }
    pasteMoved_ = true;
}

void App::commitPastePlacement() {
    if (!pastePending_) return;
    pastePending_ = false;
    selection_.clear();
    // Settled without ever passing over the viewport (Ctrl+V twice with the
    // cursor elsewhere): fall back to the classic diagonal offset so the copy
    // doesn't land exactly inside the original.
    if (!pasteMoved_)
        for (SceneObject& o : pasteStaged_) {
            o.position[0] += 1.0f;
            o.position[2] += 1.0f;
        }
    for (SceneObject& o : pasteStaged_) {
        project_.objects().push_back(std::move(o));
        selection_.push_back((int)project_.objects().size() - 1);
    }
    const size_t count = pasteStaged_.size();
    pasteStaged_.clear();
    if (selection_.empty()) return;
    selectedObject_ = selection_.back();
    commitChange();
    statusMessage_ = count == 1 ? "Pasted " + project_.objects().back().name
                                : "Pasted " + std::to_string(count) + " objects";
}

void App::cancelPastePlacement() {
    if (!pastePending_) return;
    pastePending_ = false;
    pasteStaged_.clear();
    statusMessage_ = "Paste cancelled";
}

void App::attachProject() {
    // A project switch ends any live session - the session is bound to the
    // project it was started on. The join flow is the one exception: it
    // attaches the freshly synced remote project with the session kept alive
    // (openRemoteProject sets sessionAttachKeep_ around this call).
    if (!sessionAttachKeep_ && session_.active()) {
        session_.close();
        statusMessage_ = "Session closed";
    }

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
    // Same for the Live Debugger: symbols, history and heat belong to the
    // project that was open, and the command file of the new one must be
    // (re)written from scratch.
    dbgState_ = DbgState::Off;
    dbgSyms_ = livedbg::Symbols();
    dbgSnap_ = livedbg::Snapshot();
    dbgTimeline_.clear();
    dbgCmd_ = livedbg::Command();
    // The seq is seeded from the clock for the same reason Live Link's is: a
    // game left running would ignore a command whose seq it has already seen.
    dbgCmd_.seq = (uint32_t)std::time(nullptr);
    dbgCmdWritten_ = false;
    dbgPrevHits_.clear();
    dbgHeat_.clear();
    dbgFireQueue_.clear();
    dbgScrub_ = -1;
    dbgFps_ = 0.0f;
    dbgSnapTime_ = dbgSnapPrevTime_ = 0.0;
    dbgSnapPrevFrame_ = 0;
    dbgNextTick_ = dbgSymNextRead_ = 0.0;
    dbgCrash_ = DbgCrash();
    dbgCrashSize_ = 0;
    dbgVuCap_ = vucap::Capture();
    dbgVuCapSize_ = 0;
    dbgVuCapStamp_ = 0;
    dbgVuCapTorn_ = 0;
    dbgVuCapWaiting_ = false;
    dbgLostGame_ = false;
    dbgCrashNextRead_ = 0.0;
    // Live Logic is per project too: the built-graph list, the last patch and
    // the sequence counter all belong to the project that was open.
    liveLogicState_ = LogicState::Off;
    liveLogicBuilt_ = livelogic::BuiltList();
    liveLogicLastPayload_.clear();
    liveLogicBlocked_.clear();
    liveLogicPatchCount_ = 0;
    liveLogicSeq_ = (uint32_t)std::time(nullptr);
    liveLogicNextTick_ = liveLogicBuiltNextRead_ = 0.0;
    history_.reset({project_.scenes});
    // The history file restores the undo stack when it is in sync with the
    // project file; otherwise we start fresh (and write a new one).
    if (auto err = project::loadHistory(project_, history_); !err.empty()) {
        history_.reset({project_.scenes});
        project::saveHistory(project_, history_);
    }
    // Editor-side state + window layout came in with the .tyra project file.
    // Only the primary selection persists; seed the (transient) set from it.
    pastePending_ = false;  // a staged paste never survives a project switch
    pasteStaged_.clear();
    selectOnly(project_.selectedObject);
    gizmoOp_ = (project_.gizmoOp >= 0 && project_.gizmoOp <= 2) ? project_.gizmoOp : 0;
    gizmoSpace_ = project_.gizmoSpace == 1 ? 1 : 0;
    const int viewMode =
        (project_.viewMode >= 0 && project_.viewMode <= 2) ? project_.viewMode : 0;
    viewport_.setViewMode((Viewport::ViewMode)viewMode);
    const int viewProj = (project_.viewProjection >= 0 &&
                          project_.viewProjection < Viewport::kProjectionCount)
                             ? project_.viewProjection
                             : 0;
    viewport_.setProjection((Viewport::Projection)viewProj);
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
void App::addScroller() {
    addObject(PrimitiveType::Scroller);
    SceneObject& o = project_.objects().back();
    // an invisible belt marker; a bright arrow gizmo shows the scroll axis
    o.position[1] = 1.0f;
    o.color[0] = 0.2f, o.color[1] = 0.85f, o.color[2] = 1.0f;
    o.collisionMode = 2;   // pure marker - never blocks the player
    o.castShadow = false;  // no geometry - nothing to occlude with
    saveAll("Saved");
}
void App::addPortal() {
    addObject(PrimitiveType::Portal);
    SceneObject& o = project_.objects().back();
    // a door-sized upright frame at standing height, warm energy tint
    o.position[1] = 1.2f;
    o.scale[0] = 1.6f, o.scale[1] = 2.4f, o.scale[2] = 1.0f;
    o.color[0] = 0.95f, o.color[1] = 0.55f, o.color[2] = 0.2f;
    o.collisionMode = 2;  // walk-through surface - the teleport is the "wall"
    saveAll("Saved");
}
void App::addArea() {
    addObject(PrimitiveType::Area);
    SceneObject& o = project_.objects().back();
    // A room-sized box resting on the ground, cool green so the wireframe
    // reads as "volume", not "prop".
    o.position[1] = 2.0f;
    o.scale[0] = 8.0f, o.scale[1] = 4.0f, o.scale[2] = 8.0f;
    o.color[0] = 0.3f, o.color[1] = 0.95f, o.color[2] = 0.5f;
    o.collisionMode = 2;  // a volume, never a wall
    o.castShadow = false;  // no geometry - nothing to occlude with
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
    snapInsertedObject();  // re-snap: the pillar's real height is set here
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
    // Rest it on whatever is under the spawn spot instead of spawning inside
    // it (the marker types have no volume, so this is a no-op for them).
    snapInsertedObject();
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

    // Animated models (.glb/.fbx): copy, then a validation bake for early
    // feedback. The .tskl the game loads is serialized from the copy on
    // every build. A .glb is self-contained; an .fbx may reference textures
    // as separate files, so those are copied next to it.
    if (isAnimatedModelPath(fileName)) {
        std::filesystem::copy_file(srcPath, destDir / fileName,
                                   std::filesystem::copy_options::overwrite_existing, ec);
        if (ec) {
            statusMessage_ = "Model import failed: " + ec.message();
            return "";
        }
        if (fileName.size() > 4 && fileName.compare(fileName.size() - 4, 4, ".fbx") == 0)
            fbxparser::copyExternalTextures(srcPath.string(), destDir.string());
        glbparser::Baked baked;
        std::string error;
        if (!animimport::bake((destDir / fileName).string(), 12.0f, baked, error)) {
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
        const size_t bytes = animimport::parseSkel((destDir / fileName).string(),
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
        beginModelSizing("res/models/" + fileName);
        modelSizeFresh_ = true;  // ask how big it is in the real world
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
        for (const std::string& tex : {s.texture, s.refl})
            if (!tex.empty() && tex != "@sky")  // @sky = dynamic env map, no file
                textureNames.emplace(
                    tex,
                    sanitizeAssetName(std::filesystem::path(tex).filename().string()));
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
            if (tag == "map_Kd" || tag == "refl") {
                // last token = filename, remapped to its flattened name; the
                // refl options (-type/-mm, the strength) are preserved.
                std::vector<std::string> toks;
                for (std::string t; ss >> t;) toks.push_back(t);
                std::string last = toks.empty() ? "" : toks.back();
                for (char& c : last)
                    if (c == '\\') c = '/';
                auto it = textureNames.find(last);
                out << tag;
                if (tag == "refl")
                    for (size_t ti = 0; ti + 1 < toks.size(); ++ti)
                        out << " " << toks[ti];
                out << " " << (it != textureNames.end() ? it->second : last)
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

    // The cache is keyed "<model>|<material override>", so erasing the bare
    // path missed every entry of a re-imported model (its summary stayed
    // stale until a project reload) - drop the whole map, it is cheap to
    // refill and the Assets panel refills it the same frame.
    modelInfoCache_.clear();
    statusMessage_ = "Imported " + fileName;
    if (!parseOk)
        statusMessage_ += " (unparseable - it will render as a placeholder box)";
    else if (!mtlNames.empty())
        statusMessage_ += " + " + std::to_string(mtlNames.size()) + " mtl, " +
                          std::to_string(textureNames.size()) + " texture(s)";
    if (missing > 0)
        statusMessage_ += " - " + std::to_string(missing) +
                          " referenced file(s) missing next to the .obj";
    if (parseOk) {
        beginModelSizing("res/models/" + fileName);
        modelSizeFresh_ = true;  // an .obj carries no unit at all - ask
    }
    return "res/models/" + fileName;
}

// --- model real-world size (docs/world-scale.md) ---------------------------

namespace {
// Meters per file unit for the unit presets the dialog offers.
constexpr float kUnitPresetMeters[3] = {1.0f, 0.01f, 0.0254f};
// Which preset a stored size matches (3 = none of them, "Custom").
int unitPresetIndex(float meters) {
    for (int i = 0; i < 3; ++i)
        if (std::fabs(meters - kUnitPresetMeters[i]) < 1e-6f) return i;
    return 3;
}
}  // namespace

void App::beginModelSizing(const std::string& relPath) {
    modelSizePath_ = relPath;
    modelSizeApplyExisting_ = false;
    modelSizeFresh_ = false;  // the import path raises it after this call
    // Re-importing over an existing file keeps its name, so the viewport's
    // path-keyed caches would hand back the OLD geometry's bounds.
    viewport_.invalidateAssets();
    // What the file is authored as. modelLocalBounds covers both formats
    // (static .obj bounds and an animated model's baked pose bounds) and is
    // the same measurement the placement snapping uses.
    SceneObject probe;
    probe.type = PrimitiveType::Model;
    probe.modelPath = relPath;
    float mn[3] = {0.0f, 0.0f, 0.0f}, mx[3] = {0.0f, 0.0f, 0.0f};
    modelSizeMeasured_ = viewport_.modelLocalBounds(probe, mn, mx);
    for (int c = 0; c < 3; ++c)
        modelSizeSrc_[c] = modelSizeMeasured_ ? mx[c] - mn[c] : 0.0f;
    auto it = project_.modelUnitMeters.find(relPath);
    modelSizeMeters_ = it != project_.modelUnitMeters.end() ? it->second : 1.0f;
    modelSizeUnit_ = unitPresetIndex(modelSizeMeters_);
    modelSizeOpen_ = true;
}

void App::drawModelSizeModal() {
    if (modelSizeOpen_) {
        ImGui::OpenPopup("Model size");
        modelSizeOpen_ = false;
    }
    const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (!ImGui::BeginPopupModal("Model size", nullptr,
                                ImGuiWindowFlags_AlwaysAutoResize))
        return;

    const float ups = project_.settings.unitsPerMeter;
    const float srcH = modelSizeSrc_[1];
    ImGui::TextUnformatted(
        std::filesystem::path(modelSizePath_).filename().string().c_str());
    if (modelSizeFresh_)
        ImGui::TextDisabled("Imported. How big is it in the real world?");
    ImGui::Separator();

    if (!modelSizeMeasured_) {
        ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.3f, 1.0f),
                           "The model could not be measured - it will be\n"
                           "inserted at scale 1 like before.");
    } else {
        ImGui::Text("Authored size: %.3f x %.3f x %.3f file units",
                    modelSizeSrc_[0], modelSizeSrc_[1], modelSizeSrc_[2]);
    }

    ImGui::SetNextItemWidth(scaled(160));
    const char* unitNames[] = {"Meters", "Centimeters", "Inches", "Custom"};
    if (ImGui::Combo("Source units", &modelSizeUnit_, unitNames, 4) &&
        modelSizeUnit_ < 3)
        modelSizeMeters_ = kUnitPresetMeters[modelSizeUnit_];
    prefHelp(
        ".glb and .fbx always arrive in meters (the importer normalizes\n"
        "them), so those only need this when the source itself was modeled\n"
        "at the wrong size. An .obj carries no unit at all - this is where\n"
        "you say what it was authored in.");

    // The same number from the other end: type the height you want the thing
    // to have in reality. Guarded - a flat model has no height to divide by.
    if (modelSizeMeasured_ && srcH > 1e-6f) {
        float realH = srcH * modelSizeMeters_;
        ImGui::SetNextItemWidth(scaled(160));
        if (ImGui::DragFloat("Real height (m)", &realH, 0.01f, 0.001f, 10000.0f,
                             "%.3f m")) {
            if (realH > 0.0001f) {
                modelSizeMeters_ = realH / srcH;
                modelSizeUnit_ = unitPresetIndex(modelSizeMeters_);
            }
        }
        prefHelp("An adult human is about 1.7 m, a door 2.0 m, a car 1.5 m tall.");
    }
    if (modelSizeUnit_ == 3) {
        ImGui::SetNextItemWidth(scaled(160));
        if (ImGui::DragFloat("Meters per file unit", &modelSizeMeters_, 0.001f,
                             0.00001f, 10000.0f, "%.5f",
                             ImGuiSliderFlags_Logarithmic))
            modelSizeUnit_ = unitPresetIndex(modelSizeMeters_);
    }

    const float insertScale = modelSizeMeters_ * ups;
    ImGui::Separator();
    ImGui::Text("World scale: %.3f units per meter", ups);
    if (ups == 1.0f)
        ImGui::TextDisabled("(Project Preferences > World - set it to what your\n"
                            "own content already uses.)");
    if (modelSizeMeasured_)
        ImGui::Text("In the scene: %.2f x %.2f x %.2f units, object scale %.3f",
                    modelSizeSrc_[0] * insertScale, srcH * insertScale,
                    modelSizeSrc_[2] * insertScale, insertScale);
    else
        ImGui::Text("Object scale: %.3f", insertScale);

    // Objects already placed keep the scale they were inserted with - resizing
    // an asset is not supposed to move a scene under the user. Offer it, but
    // only when it would actually change something.
    int users = 0;
    for (const SceneData& sc : project_.scenes)
        for (const SceneObject& o : sc.objects)
            if (o.type == PrimitiveType::Model && o.modelPath == modelSizePath_)
                ++users;
    if (users > 0) {
        ImGui::Checkbox(("Also rescale " + std::to_string(users) +
                         " object(s) already using this model")
                            .c_str(),
                        &modelSizeApplyExisting_);
        prefHelp("Overwrites their scale with the one above - any per-object\n"
                 "resizing you did by hand is lost.");
    }

    ImGui::Separator();
    if (ImGui::Button("OK", ImVec2(scaled(120), 0))) {
        project_.modelUnitMeters[modelSizePath_] = modelSizeMeters_;
        if (modelSizeApplyExisting_) {
            for (SceneData& sc : project_.scenes)
                for (SceneObject& o : sc.objects)
                    if (o.type == PrimitiveType::Model &&
                        o.modelPath == modelSizePath_)
                        o.scale[0] = o.scale[1] = o.scale[2] = insertScale;
            commitChange();  // object scales are an undoable scene edit
        }
        // The asset size itself lives in the manifest, not in a scene, so an
        // undo snapshot would not carry it - write it out like the LOD and
        // texture-quality overrides do. setDirty first: that is what advances
        // the session serial, so a peer sees the section change too.
        setDirty(true);
        char msg[192];
        std::snprintf(msg, sizeof msg, "%s: 1 unit = %g m, inserted at scale %g",
                      std::filesystem::path(modelSizePath_).filename().string().c_str(),
                      modelSizeMeters_, insertScale);
        saveAll("Saved");
        statusMessage_ = msg;
        modelSizePath_.clear();
        modelSizeFresh_ = false;
        ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(scaled(120), 0))) {
        modelSizePath_.clear();
        modelSizeFresh_ = false;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
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
        for (const std::string& tex : {m.texture, m.refl})
            if (!tex.empty() && tex != "@sky")  // @sky = dynamic env map, no file
                textureNames.emplace(
                    tex,
                    sanitizeAssetName(std::filesystem::path(tex).filename().string()));

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
            if (tag == "map_Kd" || tag == "refl") {
                // last token = filename, remapped to its flattened name; the
                // refl options (-type/-mm, the strength) are preserved.
                std::vector<std::string> toks;
                for (std::string t; ss >> t;) toks.push_back(t);
                std::string last = toks.empty() ? "" : toks.back();
                for (char& c : last)
                    if (c == '\\') c = '/';
                auto it = textureNames.find(last);
                out << tag;
                if (tag == "refl")
                    for (size_t ti = 0; ti + 1 < toks.size(); ++ti)
                        out << " " << toks[ti];
                out << " " << (it != textureNames.end() ? it->second : last)
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

// Every animated-model asset regardless of container (.glb + .fbx), sorted -
// the combos that offer "animated" models all go through this.
std::vector<std::string> App::listAnimatedModelFiles() {
    std::vector<std::string> files = listAssetFiles("models", ".glb");
    const std::vector<std::string> fbx = listAssetFiles("models", ".fbx");
    files.insert(files.end(), fbx.begin(), fbx.end());
    std::sort(files.begin(), files.end());
    return files;
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
        info.verts = model.vertexCount();
        info.tris = info.verts / 3;
        info.positions = model.positionCount;
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
// The clip names of a model as the GAME sees them: the Animation Editor's
// renames applied over the file's own names. Every clip name the UI shows or
// stores in a reference is an effective name, so the pickers below can compare
// straight against SceneObject::animClip and friends. Cheap (a handful of
// clips); the GlbInfo cache stays keyed on the file alone.
std::vector<std::string> App::effectiveClips(const std::string& relPath) {
    std::vector<std::string> out;
    for (const std::string& c : glbInfo(relPath).clips)
        out.push_back(animedit::effectiveName(project_, relPath, c));
    return out;
}

const App::GlbInfo& App::glbInfo(const std::string& relPath) {
    auto it = glbInfoCache_.find(relPath);
    if (it != glbInfoCache_.end()) return it->second;

    GlbInfo info;
    glbparser::Baked baked;
    const std::filesystem::path full = std::filesystem::path(project_.dir) / relPath;
    if (animimport::bake(full.string(), 12.0f, baked, info.error)) {
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
        // Models: seed a new editable .mtl from the model's own built-in
        // materials (the only way to give an animated .glb/.fbx a material it
        // can preview/paint on - it has no sibling .mtl to assign).
        if (isModel && !o.modelPath.empty()) {
            ImGui::Separator();
            if (ImGui::Selectable("+ New material from this model...")) {
                if (!createMaterialForModel(o).empty()) changed = true;
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(
                    "Extracts the model's built-in materials (names, colors,\n"
                    "textures) into a new res/materials/*.mtl, assigns it as the\n"
                    "override and opens it in the Material Editor - previewed on\n"
                    "this model.");
        }
        ImGui::EndCombo();
    }

    // Live texture feed: override the surface with a feed camera's view
    // (CCTV) or a raytraced mirror's traced image (docs/texture-feeds.md).
    // Mirrors/portals have dedicated surfaces - no feed on those.
    if (o.type != PrimitiveType::Mirror && o.type != PrimitiveType::Portal) {
        std::string cur = "<none>";
        if (o.textureFeed.rfind("camera:", 0) == 0)
            cur = "camera: " + o.textureFeed.substr(7);
        else if (o.textureFeed.rfind("mirror:", 0) == 0)
            cur = "mirror: " + o.textureFeed.substr(7);
        if (ImGui::BeginCombo("Texture feed", cur.c_str())) {
            if (ImGui::Selectable("<none>", o.textureFeed.empty()) &&
                !o.textureFeed.empty()) {
                o.textureFeed.clear();
                changed = true;
            }
            for (const SceneObject& t : project_.objects()) {
                if (t.type == PrimitiveType::Camera && t.camFeed) {
                    const std::string ref = "camera:" + t.name;
                    if (ImGui::Selectable(("camera: " + t.name).c_str(),
                                          o.textureFeed == ref) &&
                        o.textureFeed != ref) {
                        o.textureFeed = ref;
                        changed = true;
                    }
                }
                if (t.type == PrimitiveType::Mirror && t.mirrorRaytraced) {
                    const std::string ref = "mirror:" + t.name;
                    if (ImGui::Selectable(("mirror: " + t.name).c_str(),
                                          o.textureFeed == ref) &&
                        o.textureFeed != ref) {
                        o.textureFeed = ref;
                        changed = true;
                    }
                }
            }
            ImGui::EndCombo();
        }
        if (!o.textureFeed.empty() && ImGui::IsItemHovered())
            ImGui::SetTooltip(
                "Live 128x128 feed drawn flat (emissive) over this object's\n"
                "UVs, tinted by the object color. PS2-only - the editor\n"
                "viewport shows the base material.");
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

// Per-asset texture-quality override of Preferences > Textures. Textures
// shared by several assets take the highest requested quality. Drawn by the
// Asset Browser's inspector (and reachable wherever an asset row needs it).
void App::drawAssetQualityCombo(const std::string& assetRel) {
    auto it = project_.textureQuality.find(assetRel);
    int cur = it == project_.textureQuality.end() ? 0
              : it->second == "none"              ? 1
              : it->second == "8bit"              ? 2
                                                  : 3;
    const char* labels[] = {"(project)", "Full", "8-bit", "4-bit"};
    ImGui::SetNextItemWidth(scaled(90));
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
}

// Artist-authored mesh LOD tiers of one model: each level is another .obj
// in the project with the same materials and fewer triangles. Empty = the
// build decimates automatically (docs/model-pipeline.md). The popup keeps
// the list dense - clearing a level drops the ones after it, since a tier
// chain with a hole has no meaning.
void App::drawAssetLodButton(const std::string& assetRel) {
    const std::string name = std::filesystem::path(assetRel).filename().string();
    const std::string pid = "lodpop" + assetRel;
    if (ImGui::SmallButton(("LOD...##" + assetRel).c_str()))
        ImGui::OpenPopup(pid.c_str());
    auto it = project_.modelLods.find(assetRel);
    const bool custom = it != project_.modelLods.end() && !it->second.empty();
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip(custom ? "Custom LOD meshes assigned - click to change"
                                 : "Distance LOD meshes for this model\n"
                                   "(default: the build decimates automatically)");
    if (custom) {
        ImGui::SameLine();
        ImGui::TextDisabled("[%d custom LOD]", (int)it->second.size());
    }
    if (!ImGui::BeginPopup(pid.c_str())) return;
    ImGui::TextUnformatted(("Mesh LOD levels of " + name).c_str());
    ImGui::TextDisabled(
        "Level 1 shows past the mesh LOD distance, level 2 past twice it.\n"
        "A level must use the same materials (same usemtl names, same\n"
        "order) and fewer triangles, or the build decimates instead.\n"
        "Leave both on (auto) to let the build do it.");
    ImGui::Separator();
    std::vector<std::string> tiers =
        it != project_.modelLods.end() ? it->second : std::vector<std::string>();
    const std::vector<std::string> models = listAssetFiles("models", ".obj");
    bool changed = false;
    for (int level = 0; level < 2; ++level) {
        const std::string cur =
            level < (int)tiers.size() ? tiers[level] : std::string();
        const std::string label = "Level " + std::to_string(level + 1);
        ImGui::SetNextItemWidth(scaled(260));
        if (ImGui::BeginCombo((label + "##lod" + assetRel).c_str(),
                              cur.empty() ? "(auto - decimate)"
                                          : cur.substr(cur.rfind('/') + 1).c_str())) {
            if (ImGui::Selectable("(auto - decimate)", cur.empty())) {
                tiers.resize((size_t)level);  // drops the coarser levels too
                changed = true;
            }
            for (const std::string& cand : models) {
                const std::string candRel = "res/models/" + cand;
                if (candRel == assetRel) continue;  // not itself
                if (ImGui::Selectable(cand.c_str(), candRel == cur)) {
                    if ((int)tiers.size() <= level) tiers.resize((size_t)level + 1);
                    tiers[level] = candRel;
                    changed = true;
                }
                const ModelInfo& ci = modelInfo(candRel);
                if (ci.ok) {
                    ImGui::SameLine();
                    ImGui::TextDisabled("(%d tris)", ci.tris);
                }
            }
            ImGui::EndCombo();
        }
        if (level == 0 && tiers.empty()) break;  // level 2 needs level 1
    }
    if (changed) {
        // Drop trailing empties so "auto" never round-trips as a hole.
        while (!tiers.empty() && tiers.back().empty()) tiers.pop_back();
        if (tiers.empty())
            project_.modelLods.erase(assetRel);
        else
            project_.modelLods[assetRel] = tiers;
        saveAll("Saved");
    }
    ImGui::EndPopup();
}

// Real-world size of a model (docs/world-scale.md). Asked once at import; this
// is where it gets corrected afterwards - and the only place that says what
// scale the model will be dropped into a scene at. Drawn by the Asset Browser's
// inspector, next to the texture-quality and LOD controls.
void App::drawAssetSizeButton(const std::string& assetRel) {
    if (ImGui::SmallButton(("Size...##" + assetRel).c_str()))
        beginModelSizing(assetRel);
    auto it = project_.modelUnitMeters.find(assetRel);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip(it != project_.modelUnitMeters.end()
                              ? "Real-world size of this model - click to change"
                              : "Real-world size of this model is not recorded:\n"
                                "objects are inserted at scale 1 (click to set it)");
    if (it != project_.modelUnitMeters.end()) {
        const float ins = project_.modelInsertScale(assetRel);
        if (ins != 1.0f) {
            ImGui::SameLine();
            ImGui::TextDisabled("[x%.2f]", ins);
        }
    }
}

// Assets used to be a flat bullet list of res/models + res/materials with the
// per-asset controls crammed onto each row. They live in the Asset Browser now
// (Tools > Asset Browser, docs/asset-browser.md) - folders, thumbnails, type
// filters, reference-safe moves. What stays here is the summary an artist wants
// while working in the Project panel, plus the import entry points.
void App::drawAssetsSection() {
    if (!ImGui::CollapsingHeader("Assets", ImGuiTreeNodeFlags_DefaultOpen)) return;

    if (ImGui::Button("Browse assets...")) {
        showAssetBrowser_ = true;
        scanAssetTree();
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Asset Browser: folders, thumbnails, filters,\n"
                          "drag & drop into the scene, safe move/rename/delete.");
    ImGui::SameLine();
    if (ImGui::SmallButton("Import model...")) {
        importModelAsset();
        assetsChanged();
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip(".obj = static geometry (+ .mtl/textures)\n"
                          ".glb/.fbx = animated model (Blender/Maya/Max export;\n"
                          "clips play on the PS2 skeletal runtime)");
    ImGui::SameLine();
    if (ImGui::SmallButton("New material...")) {
        showMaterialEditor_ = true;
        openNewMaterialPopup_ = true;
        matEdNewError_.clear();
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Create a material in the Material Editor\n"
                          "(color, brightness, texture - live preview).");

    // One line per type, counted straight off the res/ tree.
    if (assetScanTime_ < 0.0) scanAssetTree();
    int models = 0, anims = 0, materials = 0, textures = 0, other = 0;
    for (const AssetItem& item : assetItems_) {
        if (item.generated) continue;
        switch (item.kind) {
            case AssetKind::Model: ++models; break;
            case AssetKind::AnimModel: ++anims; break;
            case AssetKind::Material: ++materials; break;
            case AssetKind::Texture: ++textures; break;
            case AssetKind::Music:
            case AssetKind::Sound:
            case AssetKind::Font: break;  // their own panel sections below
            default: ++other; break;
        }
    }
    ImGui::TextDisabled("%d model(s), %d animated, %d material(s), %d texture(s)",
                        models, anims, materials, textures);
    if (models + anims + materials + textures + other == 0)
        ImGui::TextDisabled("res/ is empty - import a model or drop files into it.");
}

// Creates a scene object for a model that is already inside the project
// (res/models/...) - the "no-copy" path used by the From-project menu and
// after an import has placed the files.
void App::addModelObject(const std::string& relPath, const float* at) {
    SceneObject o;
    o.type = PrimitiveType::Model;
    o.modelPath = relPath;
    o.color[0] = o.color[1] = o.color[2] = 0.85f;
    o.position[1] = 0.0f;
    if (at)
        for (int i = 0; i < 3; ++i) o.position[i] = at[i];
    // Models whose real-world size is known come in at the size the project's
    // world scale says they should be (docs/world-scale.md); everything else
    // keeps the plain 1:1 insert it always had.
    const float s = project_.modelInsertScale(relPath);
    o.scale[0] = o.scale[1] = o.scale[2] = s;

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
    snapInsertedObject();  // stand the model on the surface, not inside it
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
            const std::vector<std::string> anim = listAnimatedModelFiles();
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
        // Endless conveyor: tiles named segments of scene objects forever
        // along its axis (the train-window level generator).
        if (ImGui::MenuItem("Scroller (endless)")) addScroller();
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Gameplay")) {
        if (ImGui::MenuItem("Player")) addObject(PrimitiveType::Player);
        // Linked pair of surfaces: a live view through to the target portal
        // plus a walk-through teleport that carries speed and view angle.
        if (ImGui::MenuItem("Portal")) addPortal();
        if (ImGui::MenuItem("Spawn point")) addObject(PrimitiveType::SpawnPoint);
        if (ImGui::MenuItem("Save point")) addSavePoint();
        // Cutscene Director shot marker (bind camera-track keys to it)
        if (ImGui::MenuItem("Camera")) addObject(PrimitiveType::Camera);
        // Invisible volume: layer streaming zones, mirror/portal/feed target
        // sets, In Area triggers (docs/areas.md).
        if (ImGui::MenuItem("Area")) addArea();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(
                "Invisible box - a wireframe here, nothing in the game.\n"
                "Point a streaming layer's zone, a mirror/portal/camera-feed\n"
                "target list or an In Area trigger at it instead of typing\n"
                "distances.");
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
    // Procedural authoring region: the box a node graph fills - by scattering,
    // by exact repetition, or both - which the build bakes into ordinary static
    // chunk meshes. This menu item and Tools > Procedural > New volume are the
    // same verb; both land in the graph editor with the volume selected.
    if (ImGui::MenuItem("Procedural volume...")) addScatterVolume();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
        ImGui::SetTooltip(
            "A region filled by a node graph (Tools > Procedural): forests,\n"
            "rock fields, a colonnade around a plaza. Adding one opens the\n"
            "graph editor - the object itself is just the box it works in.");
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

        // --- filters: name search + object type ------------------------------
        // A scene outgrows a 130 px list long before it outgrows a PS2, so the
        // list carries the Asset Browser's two filters. Both are pure view
        // state - nothing below edits the model, and the selection is left
        // alone (a filtered-out object stays selected and stays editable in
        // Properties).
        ImGui::SetNextItemWidth(-scaled(112.0f));
        char search[128];
        std::snprintf(search, sizeof(search), "%s", sceneFilterName_.c_str());
        if (ImGui::InputTextWithHint("##objsearch", "Search name...", search,
                                     sizeof(search)))
            sceneFilterName_ = search;
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-FLT_MIN);
        // Only the types the scene actually contains are offered, each with its
        // count - a filter that can only come back empty is not worth a row.
        // The names are the ones the object rows print, so the two agree.
        const char* typePreview =
            sceneFilterType_ < 0 ? "All types"
                                 : primitiveTypeName((PrimitiveType)sceneFilterType_);
        if (ImGui::BeginCombo("##objtype", typePreview)) {
            int counts[kPrimitiveTypeCount] = {};
            for (const SceneObject& o : sc.objects) {
                const int t = (int)o.type;
                if (t >= 0 && t < kPrimitiveTypeCount) ++counts[t];
            }
            if (ImGui::Selectable("All types", sceneFilterType_ < 0))
                sceneFilterType_ = -1;
            for (int t = 0; t < kPrimitiveTypeCount; ++t) {
                if (!counts[t]) continue;
                const std::string lbl =
                    std::string(primitiveTypeName((PrimitiveType)t)) + "  (" +
                    std::to_string(counts[t]) + ")##objtype" + std::to_string(t);
                if (ImGui::Selectable(lbl.c_str(), sceneFilterType_ == t))
                    sceneFilterType_ = t;
            }
            ImGui::EndCombo();
        }

        const bool filtering = !sceneFilterName_.empty() || sceneFilterType_ >= 0;
        auto lowered = [](std::string s) {
            for (char& c : s) c = (char)tolower((unsigned char)c);
            return s;
        };
        const std::string needle = lowered(sceneFilterName_);
        auto passesFilter = [&](const SceneObject& o) {
            if (sceneFilterType_ >= 0 && (int)o.type != sceneFilterType_) return false;
            return needle.empty() ||
                   lowered(o.name).find(needle) != std::string::npos;
        };

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
        auto objectRow = [&](int i, bool inPrefabGroup = false) {
            const SceneObject& o = project_.objects()[i];
            const bool hidden = isObjectHiddenInEditor(o);
            // Inside a prefab group the provenance is already stated by the
            // group header; outside one (a member dragged onto another layer,
            // or a copy pasted somewhere else) the row has to say it itself.
            const bool tag = !o.prefabSource.empty() && !inPrefabGroup;
            std::string label = o.name + "  (" + primitiveTypeName(o.type) + ")" +
                                (tag ? "  [" + o.prefabSource + "]" : "") +
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
            // Session presence: a colored dot per participant who has this
            // object selected (their peer color), right-aligned on the row.
            if (!peerPresence_.empty()) {
                float dotX = ImGui::GetItemRectMax().x - scaled(10.0f);
                const float dotY =
                    (ImGui::GetItemRectMin().y + ImGui::GetItemRectMax().y) * 0.5f;
                for (const auto& [peerId, pp] : peerPresence_) {
                    if (pp.scene != project_.activeScene) continue;
                    if (std::find(pp.sel.begin(), pp.sel.end(), o.id) == pp.sel.end())
                        continue;
                    int colorIdx = 0;
                    for (const auto& pv : sessionPeers_)
                        if (pv.id == peerId) colorIdx = pv.colorIdx;
                    ImGui::GetWindowDrawList()->AddCircleFilled(
                        ImVec2(dotX, dotY), scaled(3.5f),
                        kPeerColors[colorIdx % session::kMaxPeers]);
                    dotX -= scaled(9.0f);
                }
            }
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

        // Objects stamped from a prefab collapse into one node per prefab -
        // the same shape as the layer groups above them, and for the same
        // reason: twenty slabs that arrived together are one thing in the
        // author's head and twenty rows in a flat list. The group sits at the
        // position of its FIRST member, so the list keeps scene order instead
        // of quietly sorting itself, and it starts CLOSED (collapsing them is
        // the point - a scene of 27 prefab rooms is otherwise 500 rows). Click
        // the label to select the whole instance, the arrow to open it.
        auto selectAll = [&](const std::vector<int>& ms) {
            clearSelection();
            for (int j : ms) toggleSelect(j);
        };
        auto emitRows = [&](const std::vector<int>& idxs) {
            std::vector<std::string> done;
            for (int i : idxs) {
                const std::string src = project_.objects()[i].prefabSource;
                if (src.empty()) {
                    objectRow(i);
                    continue;
                }
                if (std::find(done.begin(), done.end(), src) != done.end()) continue;
                done.push_back(src);
                std::vector<int> members;
                for (int j : idxs)
                    if (project_.objects()[j].prefabSource == src) members.push_back(j);
                bool allSel = true;
                for (int j : members) allSel &= isSelected(j);
                ImGuiTreeNodeFlags pflags = ImGuiTreeNodeFlags_SpanAvailWidth |
                                            ImGuiTreeNodeFlags_OpenOnArrow |
                                            ImGuiTreeNodeFlags_OpenOnDoubleClick;
                if (allSel) pflags |= ImGuiTreeNodeFlags_Selected;
                const std::string header = src + "  (" +
                                           std::to_string(members.size()) +
                                           ")##pfgrp" + std::to_string(i);
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.62f, 0.80f, 0.96f, 1.0f));
                const bool open = ImGui::TreeNodeEx(header.c_str(), pflags);
                ImGui::PopStyleColor();
                if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) {
                    if (ImGui::GetIO().KeyCtrl || ImGui::GetIO().KeyShift)
                        for (int j : members) { if (!isSelected(j)) toggleSelect(j); }
                    else
                        selectAll(members);
                }
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
                    ImGui::SetTooltip(
                        "Inserted from the prefab \"%s\" (Tools > Prefabs).\n"
                        "These are ordinary objects - editing the prefab does not\n"
                        "reach back into them.\n"
                        "Click to select all %d, the arrow to list them.",
                        src.c_str(), (int)members.size());
                // A closed group would otherwise make its members undraggable,
                // so the header is a drag source for the whole instance.
                if (grouped && ImGui::BeginDragDropSource()) {
                    if (!allSel) selectAll(members);
                    const int first = members.front();
                    ImGui::SetDragDropPayload("SCENE_OBJECT", &first, sizeof(int));
                    ImGui::Text("Move %d objects (%s)", (int)members.size(),
                                src.c_str());
                    ImGui::EndDragDropSource();
                }
                if (open) {
                    for (int j : members) objectRow(j, true);
                    ImGui::TreePop();
                }
            }
        };

        // Indices of the scene's objects, in order, that pass `keep` AND the
        // search/type filters - one place, so every group is filtered the same
        // way and the counts below cannot disagree with the rows.
        auto indicesWhere = [&](auto keep) {
            std::vector<int> out;
            for (int i = 0; i < (int)project_.objects().size(); ++i) {
                const SceneObject& o = project_.objects()[i];
                if (passesFilter(o) && keep(o)) out.push_back(i);
            }
            return out;
        };

        ImGui::BeginChild("##objects", ImVec2(0, grouped ? 220 : 130),
                          ImGuiChildFlags_Borders);
        if (!grouped) {
            const std::vector<int> rows =
                indicesWhere([](const SceneObject&) { return true; });
            if (rows.empty()) ImGui::TextDisabled("No object matches the filter.");
            emitRows(rows);
        } else {
            const ImGuiTreeNodeFlags gflags = ImGuiTreeNodeFlags_DefaultOpen |
                                              ImGuiTreeNodeFlags_SpanAvailWidth;
            for (int li = 0; li < (int)sc.layers.size(); ++li) {
                const SceneLayer& l = sc.layers[li];
                // The count follows the filter, so an open group and its header
                // tell the same story. Empty groups stay listed: they are still
                // drop targets for a layer reassignment.
                int count = 0;
                for (const SceneObject& o : sc.objects)
                    if (o.layer == l.name && passesFilter(o)) ++count;
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
                    emitRows(indicesWhere([&](const SceneObject& o) {
                        return o.layer == l.name;
                    }));
                    ImGui::TreePop();
                }
            }

            // Unassigned: no layer, or a stale name left by a deleted layer.
            int count = 0;
            for (const SceneObject& o : sc.objects)
                if ((o.layer.empty() || !layerExists(o.layer)) && passesFilter(o))
                    ++count;
            std::string header =
                "Unassigned  (" + std::to_string(count) + ")##layergrp_none";
            const bool open = ImGui::TreeNodeEx(header.c_str(), gflags);
            dropTarget("");
            if (open) {
                emitRows(indicesWhere([&](const SceneObject& o) {
                    return o.layer.empty() || !layerExists(o.layer);
                }));
                ImGui::TreePop();
            }
        }
        ImGui::EndChild();

        // What a filter hides has to be stated, or a scene reads as smaller
        // than it is - with the way back out on the same line.
        if (filtering) {
            const int shown =
                (int)indicesWhere([](const SceneObject&) { return true; }).size();
            ImGui::TextDisabled("%d of %d objects shown", shown,
                                (int)sc.objects.size());
            ImGui::SameLine();
            if (ImGui::SmallButton("Clear filters")) {
                sceneFilterName_.clear();
                sceneFilterType_ = -1;
            }
        }

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
            // Zone shape: an Area object's box, or the circle below it
            // (docs/areas.md). The area also bounds Y, so a zone can be one
            // floor of a building.
            ImGui::SameLine();
            ImGui::SetNextItemWidth(scaled(150));
            if (areaCombo("##zonearea", l.streamArea)) committed = true;
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(
                    "Zone shape: an Area object's box (bounds height too, and\n"
                    "follows the area if it moves). <none> = the circle below.");
            if (l.streamArea.empty()) {
                ImGui::SameLine();
                float center[2] = {l.streamX, l.streamZ};
                ImGui::SetNextItemWidth(scaled(110));
                if (ImGui::DragFloat2("##zonexz", center, 0.5f, 0.0f, 0.0f, "%.0f")) {
                    l.streamX = center[0];
                    l.streamZ = center[1];
                }
                if (ImGui::IsItemDeactivatedAfterEdit()) committed = true;
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Zone center (world X, Z)");
                ImGui::SameLine();
                ImGui::SetNextItemWidth(scaled(70));
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

std::string App::installVsCodeExtension() {
    // The extension ships prebuilt as a .vsix next to the exe (dev tree:
    // <exe>/../tools/vscode-tyrax/*.vsix), resolved the same exe-relative way as
    // the generated c_cpp_properties.json. It MUST be installed through the
    // `code` CLI: modern VS Code (>=1.74) loads only what its own manifest cache
    // lists, so an extension folder merely copied into ~/.vscode/extensions is
    // silently ignored - which is why the earlier folder-copy install did
    // nothing and printed nothing. `code --install-extension <vsix>` registers
    // it properly.
    const std::string exePath = platform::exePath();
    if (exePath.empty()) return "Could not locate the editor executable";
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

    // Run it synchronously so we can report the real outcome; --force
    // reinstalls in place, so this is idempotent. The command goes through the
    // platform shell, which always starts - so a missing CLI is detected up
    // front rather than read off a spawn failure that cannot happen.
    if (!platform::commandExists("code"))
        return "Could not run VS Code's 'code' CLI - is it on PATH?";
    auto proc = platform::Process::start("code --install-extension " +
                                         platform::shellArg(vsix.string()) + " --force");
    if (!proc || proc->wait() != 0)
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

    std::string abs;
    if (!file.empty()) {
        std::filesystem::path fp(file);
        abs = fp.is_absolute() ? fp.string()
                               : (std::filesystem::path(project_.dir) / fp).string();
    }
    if (const std::string err = platform::openInVSCode(project_.dir, abs); !err.empty()) {
        statusMessage_ = err;
    } else {
        statusMessage_ = "Opening in VS Code...";
        // Append the extension-install outcome so it is visible (a failure here
        // is the difference between highlighting working or not).
        if (!vsCodeExtStatus_.empty()) statusMessage_ += "  [" + vsCodeExtStatus_ + "]";
    }
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
    // A Drone Generator patch is worthless without its track, so it goes too
    // (the Asset Browser deletes sidecars itself before routing here - removing
    // an already-removed file is a no-op).
    std::filesystem::path patch = std::filesystem::path(p.dir) / relPath;
    patch.replace_extension(".drone");
    ec.clear();
    std::filesystem::remove(patch, ec);
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
    if (ImGui::SmallButton("Generate...##music")) showDroneGenerator_ = true;
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Tools > Drone Generator: build an ambient/drone track\n"
                          "in the editor instead of importing one.");
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
        // A track the Drone Generator made kept its patch next to it, so it can
        // be reopened and re-rendered instead of replaced by hand.
        {
            std::filesystem::path patch =
                std::filesystem::path(project_.dir) / project_.music[i];
            patch.replace_extension(".drone");
            std::error_code pec;
            if (std::filesystem::exists(patch, pec)) {
                ImGui::SameLine();
                if (ImGui::SmallButton("Edit")) {
                    std::filesystem::path rel(project_.music[i]);
                    rel.replace_extension(".drone");
                    droneLoadPatch(rel.generic_string());
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Open this track's patch in the Drone "
                                      "Generator (Tools > Drone Generator).");
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

// Save Editor (Tools > Save Editor): everything about memory card saves in
// one place - how the save presents in the PS2 browser (title + icon, baked
// to res/save/ by refreshGenerated), an exact byte breakdown of what one
// slot stores (the same fixed payload the in-RAM checkpoint buffer holds),
// and the custom save values/texts (drawSaveDataSection below).
//
// The icon preview is rendered at the card icon's own texture resolution and
// shown smaller, so the model's edges stay clean on a HiDPI panel.
static constexpr int kSaveIconPreviewPx = 128;
static constexpr double kSaveIconLoopSeconds = 2.0;

// The spinner sheet as the game will use it: one cell at a time, cycled at the
// console's own rate. Whatever spinnerInfo settled on is what is drawn - so a
// rejected custom sheet previews as the built-in, exactly like it will ship.
void App::drawSaveSpinnerPreview(const savebake::SpinnerInfo& spin) {
    namespace fs = std::filesystem;
    const std::string key = project_.dir + "|" + spin.resPath + "|" +
                            std::to_string(spin.frames);
    if (saveSpinnerPreviewKey_ != key || !saveSpinnerPreviewTex_) {
        const fs::path full = fs::path(project_.dir) / spin.resPath;
        int w = 0, h = 0, comp = 0;
        unsigned char* px = stbi_load(full.string().c_str(), &w, &h, &comp, 4);
        if (px) {
            if (!saveSpinnerPreviewTex_) glGenTextures(1, &saveSpinnerPreviewTex_);
            glBindTexture(GL_TEXTURE_2D, saveSpinnerPreviewTex_);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA,
                         GL_UNSIGNED_BYTE, px);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            stbi_image_free(px);
            saveSpinnerPreviewW_ = w;
            saveSpinnerPreviewH_ = h;
            saveSpinnerPreviewKey_ = key;
        } else {
            saveSpinnerPreviewW_ = saveSpinnerPreviewH_ = 0;
        }
    }
    if (!saveSpinnerPreviewTex_ || saveSpinnerPreviewW_ <= 0) return;
    // SAVE_SPINNER_HOLD frames a cell at 50 Hz - the same cadence the game
    // uses, so what you see here is the speed that ships.
    const double cellsPerSecond = 50.0 / 4.0;
    const int cell =
        (int)((long long)(ImGui::GetTime() * cellsPerSecond) % spin.frames);
    const float u0 = (float)(cell * spin.cellW) / (float)saveSpinnerPreviewW_;
    const float u1 =
        (float)((cell + 1) * spin.cellW) / (float)saveSpinnerPreviewW_;
    const float side = scaled(48.0f);
    const float aspect = spin.cellH > 0 ? (float)spin.cellH / (float)spin.cellW
                                        : 1.0f;
    ImGui::Image((ImTextureID)(intptr_t)saveSpinnerPreviewTex_,
                 ImVec2(side, side * aspect), ImVec2(u0, 0.0f),
                 ImVec2(u1, 1.0f));
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("cell %d/%d", cell + 1, spin.frames);
}

void App::drawSaveEditorWindow() {
    if (!showSaveEditor_ || !hasProject_) return;
    ImGui::SetNextWindowSize(ImVec2(scaled(460.0f), scaled(760.0f)),
                             ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Save Editor", &showSaveEditor_)) {
        ImGui::End();
        return;
    }

    // --- Memory card appearance --------------------------------------------
    ImGui::SeparatorText("Memory card appearance");
    char titleBuf[68];
    std::snprintf(titleBuf, sizeof(titleBuf), "%s", project_.saveTitle.c_str());
    ImGui::SetNextItemWidth(scaled(240.0f));
    if (ImGui::InputTextWithHint("Save title", project_.name.c_str(), titleBuf,
                                 sizeof(titleBuf)))
        project_.saveTitle = titleBuf;
    if (ImGui::IsItemDeactivatedAfterEdit()) saveAll("Saved");
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("The name the PS2 browser shows for this game's\n"
                          "saves (icon.sys). '|' breaks it onto a second\n"
                          "line; empty = the project name. ASCII only.");

    // Geometry: the flat image quad, or a 3D icon from a project model -
    // .obj (static, gently swaying) / .glb (a clip sampled into morph
    // shapes: a real animated icon, like retail games).
    namespace fs = std::filesystem;
    if (ImGui::BeginCombo("Icon model",
                          project_.saveIconModel.empty()
                              ? "(flat image quad)"
                              : project_.saveIconModel.c_str())) {
        if (ImGui::Selectable("(flat image quad)",
                              project_.saveIconModel.empty()) &&
            !project_.saveIconModel.empty()) {
            project_.saveIconModel.clear();
            saveAll("Saved");
        }
        std::error_code ec;
        const fs::path models = fs::path(project_.dir) / "res" / "models";
        for (fs::directory_iterator it(models, ec), end; it != end;
             it.increment(ec)) {
            if (ec) break;
            if (!it->is_regular_file(ec)) continue;
            std::string ext = it->path().extension().string();
            for (char& c : ext) c = (char)tolower((unsigned char)c);
            if (ext != ".obj" && ext != ".glb") continue;
            const std::string rel =
                "res/models/" + it->path().filename().generic_string();
            if (ImGui::Selectable(rel.c_str(), rel == project_.saveIconModel)) {
                project_.saveIconModel = rel;
                saveAll("Saved");
            }
        }
        ImGui::EndCombo();
    }

    std::string modelExt =
        fs::path(project_.saveIconModel).extension().string();
    for (char& c : modelExt) c = (char)tolower((unsigned char)c);
    if (modelExt == ".glb") {
        // Clip picker: parse the model's clip list once per picked file.
        if (saveIconClipsModel_ != project_.saveIconModel) {
            saveIconClips_.clear();
            glbparser::Baked baked;
            std::string err;
            if (glbparser::bake((fs::path(project_.dir) /
                                 project_.saveIconModel).string(),
                                12.0f, baked, err))
                for (const glbparser::Clip& c : baked.clips)
                    saveIconClips_.push_back(c.name);
            saveIconClipsModel_ = project_.saveIconModel;
        }
        const char* shown = project_.saveIconClip.empty()
                                ? "(first clip)"
                                : project_.saveIconClip.c_str();
        if (ImGui::BeginCombo("Clip", shown)) {
            if (ImGui::Selectable("(first clip)",
                                  project_.saveIconClip.empty()) &&
                !project_.saveIconClip.empty()) {
                project_.saveIconClip.clear();
                saveAll("Saved");
            }
            for (const std::string& c : saveIconClips_)
                if (ImGui::Selectable(c.c_str(), c == project_.saveIconClip)) {
                    project_.saveIconClip = c;
                    saveAll("Saved");
                }
            ImGui::EndCombo();
        }
    }
    // A .glb with a clip plays that clip, so the idle motion below would mean
    // nothing for it. saveIconClips_ is already the cached clip list.
    const bool clipDriven = modelExt == ".glb" && !saveIconClips_.empty();

    ImGui::SetNextItemWidth(scaled(120.0f));
    ImGui::SliderInt("Frames", &project_.saveIconFrames, 1,
                     savebake::kMaxIconShapes);
    if (ImGui::IsItemDeactivatedAfterEdit()) saveAll("Saved");
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip(
            "Animation shapes baked into the icon. A .glb clip is sampled\n"
            "into this many morph frames; everything else gets this many\n"
            "steps of the Motion below. More frames = smoother motion but\n"
            "a bigger icon file (each shape is another copy of every vertex).");

    // Idle motion: what a source with no animation of its own does.
    {
        const std::vector<savebake::IconMotion>& motions = savebake::iconMotions();
        const int cur = savebake::iconMotionIndex(project_.saveIconMotion);
        ImGui::BeginDisabled(clipDriven);
        if (ImGui::BeginCombo("Motion", motions[cur].label)) {
            for (size_t i = 0; i < motions.size(); ++i) {
                if (ImGui::Selectable(motions[i].label, (int)i == cur) &&
                    (int)i != cur) {
                    project_.saveIconMotion = motions[i].key;
                    saveAll("Saved");
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("%s", motions[i].desc);
            }
            ImGui::EndCombo();
        }
        if (savebake::iconMotionIndex(project_.saveIconMotion) != 5) {
            ImGui::SetNextItemWidth(scaled(120.0f));
            ImGui::SliderFloat("Amount", &project_.saveIconMotionAmount, 0.25f,
                               2.0f, "%.2fx");
            if (ImGui::IsItemDeactivatedAfterEdit()) saveAll("Saved");
        }
        ImGui::EndDisabled();
        // TextWrapped, not TextDisabled: these descriptions are a sentence or
        // two and the panel is narrow, so an unwrapped one is simply cut off.
        ImGui::PushStyleColor(ImGuiCol_Text,
                              ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
        ImGui::TextWrapped("%s", clipDriven
                                     ? "The .glb clip drives this icon's motion."
                                     : motions[cur].desc);
        ImGui::PopStyleColor();
    }

    // Icon image: any res/ PNG/JPG, resampled to the 128x128 icon texture
    // (a model with its own texture ships that instead).
    if (ImGui::BeginCombo("Icon image",
                          project_.saveIcon.empty() ? "(built-in placeholder)"
                                                    : project_.saveIcon.c_str())) {
        if (ImGui::Selectable("(built-in placeholder)",
                              project_.saveIcon.empty()) &&
            !project_.saveIcon.empty()) {
            project_.saveIcon.clear();
            saveAll("Saved");
        }
        std::error_code ec;
        const fs::path res = fs::path(project_.dir) / "res";
        for (fs::recursive_directory_iterator it(res, ec), end; it != end;
             it.increment(ec)) {
            if (ec) break;
            if (!it->is_regular_file(ec)) continue;
            std::string ext = it->path().extension().string();
            for (char& c : ext) c = (char)tolower((unsigned char)c);
            if (ext != ".png" && ext != ".jpg" && ext != ".jpeg") continue;
            const std::string rel =
                "res/" + fs::relative(it->path(), res, ec).generic_string();
            if (ImGui::Selectable(rel.c_str(), rel == project_.saveIcon)) {
                project_.saveIcon = rel;
                saveAll("Saved");
            }
        }
        ImGui::EndCombo();
    }

    // Preview: the baked GEOMETRY rendered the way the browser draws it
    // (texture x vertex colour, shaded), one image per animation shape. The
    // same bake fills the stats line below, so the picture and the numbers can
    // never describe different icons.
    // project_.dir is in the key because two projects can carry identical icon
    // settings ("" everywhere is the default), and without it opening the
    // second one would keep showing the first one's render.
    const std::string previewKey = project_.dir + "|" + project_.saveIcon +
                                   "|" + project_.saveIconModel + "|" +
                                   project_.saveIconClip + "|" +
                                   std::to_string(project_.saveIconFrames) +
                                   "|" + project_.saveIconMotion + "|" +
                                   std::to_string(project_.saveIconMotionAmount);
    if (saveIconPreviewKey_ != previewKey || saveIconPreviewTex_.empty()) {
        saveIconInfo_ = savebake::iconInfo(project_);  // stats line, same bake
        const std::vector<std::vector<unsigned char>> frames =
            savebake::iconPreviewFrames(project_, kSaveIconPreviewPx);
        if (!saveIconPreviewTex_.empty()) {
            glDeleteTextures((GLsizei)saveIconPreviewTex_.size(),
                             saveIconPreviewTex_.data());
            saveIconPreviewTex_.clear();
        }
        for (const std::vector<unsigned char>& f : frames) {
            unsigned tex = 0;
            glGenTextures(1, &tex);
            glBindTexture(GL_TEXTURE_2D, tex);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, kSaveIconPreviewPx,
                         kSaveIconPreviewPx, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                         f.data());
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            saveIconPreviewTex_.push_back(tex);
        }
        saveIconPreviewKey_ = previewKey;
    }
    if (!saveIconPreviewTex_.empty()) {
        // One loop every kSaveIconLoopSeconds, however many shapes there are -
        // so raising Frames makes the SAME motion smoother rather than faster,
        // which is what the slider means.
        const size_t n = saveIconPreviewTex_.size();
        const double phase =
            ImGui::GetTime() / kSaveIconLoopSeconds * (double)n;
        const size_t f = (size_t)((long long)phase % (long long)n);
        ImGui::Image((ImTextureID)(intptr_t)saveIconPreviewTex_[f],
                     ImVec2(scaled(96.0f), scaled(96.0f)));
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(
                "The baked icon, rendered the way the PS2 browser draws it:\n"
                "the model's triangles with texture x vertex colour, cycling\n"
                "through the %d animation shape%s that ship in list.icn.\n"
                "The browser's own lighting and camera differ slightly.",
                (int)n, n == 1 ? "" : "s");
    }
    ImGui::SameLine();
    ImGui::BeginGroup();
    std::string title = savebake::displayTitle(project_);
    for (char& c : title)
        if (c == '|') c = '\n';
    ImGui::TextUnformatted(title.c_str());
    ImGui::TextDisabled("Card folder: %s",
                        templates::saveDirName(project_).c_str());
    ImGui::TextDisabled("%s - %d tris, %d shape%s, %.1f KB",
                        saveIconInfo_.source.c_str(), saveIconInfo_.triangles,
                        saveIconInfo_.shapes, saveIconInfo_.shapes == 1 ? "" : "s",
                        saveIconInfo_.bytes / 1024.0);
    if (!saveIconInfo_.warning.empty())
        ImGui::TextColored(theme::semantics().warn, "%s",
                           saveIconInfo_.warning.c_str());
    ImGui::TextDisabled("Written to the card with the first save.");
    ImGui::EndGroup();

    // --- How a save behaves --------------------------------------------------
    ImGui::SeparatorText("Saving");
    {
        // Settled up front: the Size readout, the preview and the warning all
        // have to describe the sheet that will actually ship.
        const savebake::SpinnerInfo spin = savebake::spinnerInfo(project_);
        const char* srcLabel = project_.saveMenuWritesCheckpoint
                                   ? "the last checkpoint"
                                   : "a live snapshot";
        if (ImGui::BeginCombo("Save menu writes", srcLabel)) {
            if (ImGui::Selectable("a live snapshot",
                                  !project_.saveMenuWritesCheckpoint) &&
                project_.saveMenuWritesCheckpoint) {
                project_.saveMenuWritesCheckpoint = false;
                saveAll("Saved");
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(
                    "The slot records where the player is standing right now.");
            if (ImGui::Selectable("the last checkpoint",
                                  project_.saveMenuWritesCheckpoint) &&
                !project_.saveMenuWritesCheckpoint) {
                project_.saveMenuWritesCheckpoint = true;
                saveAll("Saved");
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(
                    "The slot records the last Save Checkpoint instead - the\n"
                    "\"you resume from the shrine, not from here\" model.\n"
                    "Before the first checkpoint it writes a live snapshot, so\n"
                    "the menu is never dead at the start of a game.");
            ImGui::EndCombo();
        }

        if (ImGui::Checkbox("Write in the background", &project_.saveAsync))
            saveAll("Saved");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(
                "The card transfer is stepped one call per frame while the\n"
                "game keeps running: no pause, and no \"do not remove the\n"
                "memory card\" overlay. Best for Commit Checkpoint, which is\n"
                "meant to be unobtrusive. LOADING still blocks - the world is\n"
                "being replaced, so there is nothing to keep playing.");

        ImGui::BeginDisabled(!project_.saveAsync);
        if (ImGui::Checkbox("Show a spinner while writing", &project_.saveSpinner))
            saveAll("Saved");
        ImGui::BeginDisabled(!project_.saveSpinner);
        const char* kCorners[] = {"Top left", "Top right", "Bottom left",
                                  "Bottom right"};
        int corner = project_.saveSpinnerCorner;
        if (corner < 0 || corner > 3) corner = 3;
        ImGui::SetNextItemWidth(scaled(150.0f));
        if (ImGui::Combo("Corner", &corner, kCorners, 4)) {
            project_.saveSpinnerCorner = corner;
            saveAll("Saved");
        }
        ImGui::SetNextItemWidth(scaled(120.0f));
        ImGui::SliderFloat("Margin", &project_.saveSpinnerMargin, 0.0f, 96.0f,
                           "%.0f px");
        if (ImGui::IsItemDeactivatedAfterEdit()) saveAll("Saved");
        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(
                "Distance from the screen edges, in the console's 512x448\n"
                "pixels. Keep it clear of the TV-safe area if the game is\n"
                "meant for a CRT (docs/safe-areas.md).");
        ImGui::SetNextItemWidth(scaled(120.0f));
        ImGui::SliderFloat("Size", &project_.saveSpinnerScale, 0.4f, 3.0f,
                           "%.2fx");
        if (ImGui::IsItemDeactivatedAfterEdit()) saveAll("Saved");
        ImGui::SameLine();
        ImGui::TextDisabled("%.0fx%.0f px", spin.cellW * project_.saveSpinnerScale,
                            spin.cellH * project_.saveSpinnerScale);
        // The sheet itself: any project image laid out as a horizontal strip.
        if (ImGui::BeginCombo("Spinner sheet",
                              project_.saveSpinnerImage.empty()
                                  ? "(built-in)"
                                  : project_.saveSpinnerImage.c_str())) {
            if (ImGui::Selectable("(built-in)",
                                  project_.saveSpinnerImage.empty()) &&
                !project_.saveSpinnerImage.empty()) {
                project_.saveSpinnerImage.clear();
                saveAll("Saved");
            }
            std::error_code ec;
            const fs::path res = fs::path(project_.dir) / "res";
            for (fs::recursive_directory_iterator it(res, ec), end; it != end;
                 it.increment(ec)) {
                if (ec) break;
                if (!it->is_regular_file(ec)) continue;
                std::string ext = it->path().extension().string();
                for (char& c : ext) c = (char)tolower((unsigned char)c);
                if (ext != ".png") continue;
                const std::string rel =
                    "res/" + fs::relative(it->path(), res, ec).generic_string();
                if (ImGui::Selectable(rel.c_str(),
                                      rel == project_.saveSpinnerImage)) {
                    project_.saveSpinnerImage = rel;
                    saveAll("Saved");
                }
            }
            ImGui::EndCombo();
        }
        if (!project_.saveSpinnerImage.empty()) {
            ImGui::SetNextItemWidth(scaled(120.0f));
            ImGui::SliderInt("Cells", &project_.saveSpinnerFrames, 1, 32);
            if (ImGui::IsItemDeactivatedAfterEdit()) saveAll("Saved");
            ImGui::SameLine();
            ImGui::TextDisabled("(?)");
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(
                    "How many animation cells the strip is cut into. The\n"
                    "sheet is one ROW, so a cell is (width / cells) x height.");
        }
        drawSaveSpinnerPreview(spin);
        ImGui::EndDisabled();
        ImGui::EndDisabled();
        if (!spin.warning.empty()) {
            ImGui::PushStyleColor(ImGuiCol_Text, theme::semantics().warn);
            ImGui::TextWrapped("Using the built-in instead: %s.",
                               spin.warning.c_str());
            ImGui::PopStyleColor();
        } else {
            ImGui::PushStyleColor(
                ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
            ImGui::TextWrapped(
                "%s - %dx%d, %d cell%s of %dx%d. Any project PNG works as long "
                "as both its sides are 8/16/32/64/128/256/512.",
                spin.custom ? spin.resPath.c_str()
                            : "Built-in (res/hud/save-spinner.png)",
                spin.sheetW, spin.sheetH, spin.frames,
                spin.frames == 1 ? "" : "s", spin.cellW, spin.cellH);
            ImGui::PopStyleColor();
        }
    }

    // --- What lands in a save slot ------------------------------------------
    ImGui::SeparatorText("What a save slot stores");
    const templates::SaveSizeInfo sz = templates::saveSizeInfo(project_);
    if (ImGui::BeginTable("savesize", 2, ImGuiTableFlags_SizingStretchProp)) {
        auto row = [](const char* label, const std::string& value) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(label);
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(value.c_str());
        };
        auto bytes = [](int b) {
            char buf[32];
            if (b >= 1024)
                std::snprintf(buf, sizeof(buf), "%.1f KB", b / 1024.0);
            else
                std::snprintf(buf, sizeof(buf), "%d B", b);
            return std::string(buf);
        };
        row("Header (scene, player position, facing)", bytes(sz.headerBytes));
        row(("Save values (" + std::to_string(sz.values) + ")").c_str(),
            bytes(sz.valuesBytes));
        row(("Save texts (" + std::to_string(sz.texts) + " x 32 B)").c_str(),
            bytes(sz.textsBytes));
        row(("Object states (" + std::to_string(sz.objectSlots) +
             " slots x 32 B)")
                .c_str(),
            bytes(sz.objectsBytes));
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::Text("Save slot file (64-byte aligned)");
        ImGui::TableNextColumn();
        ImGui::Text("%s", bytes(sz.payloadBytes).c_str());
        row("Card icon (icon.sys + list.icn, once)", bytes(sz.iconBytes));
        row("All data (3 slots + icon, raw bytes)",
            bytes(sz.payloadBytes * templates::kSaveSlots + sz.iconBytes));
        // What the card actually loses, which is the number that matters and
        // is always bigger: files are allocated in whole 1 KB clusters and
        // the save directory costs one of its own.
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::Text("Card space used (1 KB clusters)");
        ImGui::TableNextColumn();
        // The number to quote, so it gets the theme's one bright colour -
        // emphasis, not a warning (nothing here is wrong).
        ImGui::TextColored(theme::semantics().accent, "%s",
                           bytes(sz.cardFootprintBytes).c_str());
        ImGui::EndTable();
    }
    ImGui::TextDisabled(
        "A PS2 card allocates whole %d-byte clusters and no two files share\n"
        "one, so each of the %d slots costs at least one cluster however small\n"
        "it is - plus one for icon.sys, one or more for list.icn, and one for\n"
        "the save's own directory. That rounding is why the two rows differ.",
        sz.cardClusterBytes, templates::kSaveSlots);
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip(
            "A slot stores the scene index, the player position/facing,\n"
            "every save value and text below, and position/color/visibility\n"
            "of objects with \"Save state\" enabled (sized by the largest\n"
            "per-scene count of such objects). The in-game checkpoint\n"
            "(Save Checkpoint flow node) keeps exactly ONE copy of this\n"
            "payload in RAM until it is committed to the card.");
    ImGui::Text("Checkpoint buffer in game RAM: %d KB",
                (sz.payloadBytes + 1023) / 1024);

    // Save-flagged objects, per scene - "what is actually in my save?"
    if (ImGui::TreeNode("Save-flagged objects")) {
        for (const SceneData& sc : project_.scenes) {
            int flagged = 0;
            for (const SceneObject& o : sc.objects)
                if (o.saveState) ++flagged;
            if (ImGui::TreeNode(sc.name.c_str(), "%s (%d)", sc.name.c_str(),
                                flagged)) {
                for (const SceneObject& o : sc.objects)
                    if (o.saveState) ImGui::BulletText("%s", o.name.c_str());
                if (flagged == 0)
                    ImGui::TextDisabled("None - enable \"Save state\" in an\n"
                                        "object's properties to persist it.");
                ImGui::TreePop();
            }
        }
        ImGui::TreePop();
    }

    // --- Save data -----------------------------------------------------------
    ImGui::SeparatorText("Save data");
    drawSaveDataSection();
    ImGui::End();
}

// Custom values persisted in memory card save slots. Flow graph "Save"
// nodes (Set/Add/Value At Least) reference them by name; the defaults are
// the fresh-game state. Drawn inside the Save Editor window.
void App::drawSaveDataSection() {
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
            const std::string ref = ensureFontForPath("res/fonts/" + fileName);
            if (menuTarget) project_.menus[selectedMenu_].font = ref;
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

    // Two jobs, one window: authoring a scene's mood, and baking the light it
    // implies. showGiBake_ is no longer a window flag - it is "show me the GI
    // tab", set by the Tools menu item and by a saved layout that had the old
    // standalone window open.
    const bool wantGi = showGiBake_;
    showGiBake_ = false;
    bool changed = false;

    if (ImGui::BeginTabBar("##ambience_tabs")) {
        if (ImGui::BeginTabItem("Presets")) {
            drawAmbiencePresets(changed);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Day / night")) {
            drawAmbienceDayCycle(changed);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Global illumination", nullptr,
                                wantGi ? ImGuiTabItemFlags_SetSelected : 0)) {
            drawGiBakeSection();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ImGui::End();
    if (changed) commitChange();
}

// The preset half of the Ambience Editor (see drawAmbienceWindow).
void App::drawAmbiencePresets(bool& changed) {
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

    ImGui::SeparatorText("Ambient occlusion");
    ImGui::Checkbox("Bake ambient occlusion", &a.aoEnabled);
    changed |= ImGui::IsItemDeactivatedAfterEdit();
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip(
            "Soft contact shadows where geometry meets: terrain\n"
            "self-shadowing (ravines, foot of hills) and darkening where\n"
            "objects touch the ground and each other - baked into per-pixel\n"
            "AO textures at build (a terrain map + a primitive lightmap\n"
            "atlas), drawn as extra blended passes. Which objects cast is\n"
            "per object: Properties > Cast shadow. Imported and animated\n"
            "models cast but don't receive.");
    if (a.aoEnabled) {
        ImGui::SliderFloat("AO strength", &a.aoStrength, 0.0f, 1.0f, "%.2f");
        changed |= ImGui::IsItemDeactivatedAfterEdit();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("How dark full occlusion gets (0 = invisible).");
        ImGui::DragFloat("AO radius", &a.aoRadius, 0.05f, 0.1f, 50.0f, "%.2f");
        changed |= ImGui::IsItemDeactivatedAfterEdit();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(
                "World units the contact darkening reaches from an\n"
                "occluder. Terrain self-shadowing scans 3x this.");
        if (a.aoRadius < 0.1f) a.aoRadius = 0.1f;
        ImGui::TextDisabled("Static bake: moved objects re-shade themselves at "
                            "runtime, but\nthe shadow they cast stays where the "
                            "scene was built.");
    }

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
}

// The day/night half of the Ambience Editor (docs/day-night-cycle.md). Edits
// the SELECTED preset's cycle: a preset is a scene's mood bundle, so "which
// preset" doubles as "which time of day", and a scene picks one already.
void App::drawAmbienceDayCycle(bool& changed) {
    if (selectedAmbience_ < 0 ||
        selectedAmbience_ >= (int)project_.ambiencePresets.size()) {
        ImGui::TextDisabled("Select a preset on the Presets tab first.");
        ImGui::TextDisabled(
            "\nA day/night cycle belongs to a preset: the sun and moon arcs,\n"
            "the colours through the day, and the hour the scene is built at.");
        return;
    }
    AmbiencePreset& a = project_.ambiencePresets[selectedAmbience_];
    DayCycle& c = a.cycle;

    ImGui::Text("Preset: %s", a.name.c_str());
    // Which preset the ACTIVE SCENE actually resolves to. Without this line the
    // tab is genuinely misleading: the viewport previews whatever preset is
    // selected here, and closing the window puts the SCENE's preset back - which
    // reads exactly like the edit having been thrown away when the two differ.
    {
        const int sceneIdx = project::ambienceIndexFor(project_, project_.active());
        if (sceneIdx != selectedAmbience_) {
            ImGui::SameLine(0.0f, scaled(12.0f));
            ImGui::TextColored(theme::semantics().warn, "(not this scene's)");
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(
                    "Scene \"%s\" is lit by %s, so the viewport goes back to that\n"
                    "when this window closes. Edits here are NOT lost - the title\n"
                    "bar shows the project unsaved until you save - they just are\n"
                    "not what this scene uses.",
                    project_.active().name.c_str(),
                    sceneIdx >= 0 && sceneIdx < (int)project_.ambiencePresets.size()
                        ? project_.ambiencePresets[sceneIdx].name.c_str()
                        : "no preset");
            ImGui::SameLine();
            if (ImGui::SmallButton("Use for this scene")) {
                project_.active().ambiencePreset = a.name;
                changed = true;
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Point scene \"%s\" at this preset, so what you\n"
                                  "author here is what it is lit by.",
                                  project_.active().name.c_str());
        }
    }
    ImGui::SameLine(0.0f, scaled(24.0f));
    if (ImGui::Checkbox("Enable day/night cycle", &c.enabled)) {
        // First enable with nothing authored would resolve every hour to one
        // default key, i.e. a flat sky - seed a real day instead.
        if (c.enabled && c.keys.empty()) c.keys = ambience::defaultKeys();
        changed = true;
    }
    prefHelp(
        "Replaces this preset's sky, light direction/colour and fog colour with\n"
        "whatever the cycle resolves to at the hour below.\n\n"
        "It is not a screen tint: the resolved direction is what the vertex\n"
        "shading, the AO bake, the GI bake and the runtime projected shadows,\n"
        "lens flare and god rays are all built from.");
    if (!c.enabled) {
        ImGui::TextDisabled(
            "\nOff - the preset's own sky/lighting/fog on the Presets tab is used.");
        return;
    }

    // --- the slider --------------------------------------------------------
    ImGui::SeparatorText("Time of day");
    const ambience::Resolved now = ambience::evaluate(c, c.time);
    {
        char label[32];
        std::snprintf(label, sizeof(label), "%02d:%02d", (int)c.time,
                      (int)((c.time - (float)(int)c.time) * 60.0f));
        ImGui::SetNextItemWidth(-scaled(120.0f));
        ImGui::SliderFloat("##tod", &c.time, 0.0f, 24.0f, label);
        changed |= ImGui::IsItemDeactivatedAfterEdit();
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Noon")) { c.time = 12.0f; changed = true; }
    ImGui::SameLine();
    if (ImGui::SmallButton("Midnight")) { c.time = 0.0f; changed = true; }

    // A 24-hour strip of the horizon colour, with the current hour marked. It
    // is the fastest way to see that a key list has a hole in it.
    {
        const ImVec2 p0 = ImGui::GetCursorScreenPos();
        const float w = ImGui::GetContentRegionAvail().x;
        const float h = scaled(22.0f);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const int steps = 96;
        for (int i = 0; i < steps; ++i) {
            const float t0 = (float)i / steps, t1 = (float)(i + 1) / steps;
            const DayKey k = ambience::sampleKeys(c, t0 * 24.0f);
            dl->AddRectFilled(
                ImVec2(p0.x + w * t0, p0.y), ImVec2(p0.x + w * t1 + 1.0f, p0.y + h),
                IM_COL32((int)(k.skyColor[0] * 255), (int)(k.skyColor[1] * 255),
                         (int)(k.skyColor[2] * 255), 255));
        }
        const theme::Semantics& sem = theme::semantics();
        for (const DayKey& k : c.keys) {
            const float x = p0.x + w * (k.hour / 24.0f);
            dl->AddLine(ImVec2(x, p0.y), ImVec2(x, p0.y + h * 0.35f),
                        ImGui::GetColorU32(sem.textDim), 1.0f);
        }
        const float sx = p0.x + w * (ambience::wrap24(c.time) / 24.0f);
        dl->AddLine(ImVec2(sx, p0.y), ImVec2(sx, p0.y + h),
                    ImGui::GetColorU32(sem.accent), scaled(2.0f));
        ImGui::Dummy(ImVec2(w, h));
    }
    ImGui::TextDisabled(
        "Sun %+.0f deg, moon %+.0f deg above the horizon - %s is lighting the "
        "scene.",
        now.sunElevation, now.moonElevation,
        now.sunElevation >= now.moonElevation ? "the sun" : "the moon");
    prefHelp(
        "The baked light direction never dips below +5 degrees, whichever body\n"
        "is up: a light at the horizon gives flat ground no diffuse at all, and\n"
        "one below it lights the world from underneath. Night gets dark from\n"
        "the key colours, not from aiming the sun into the floor.");

    // --- sun ---------------------------------------------------------------
    ImGui::SeparatorText("Sun");
    ImGui::SliderFloat("Rises at (bearing)", &c.sunAzimuth, 0.0f, 360.0f, "%.0f deg");
    changed |= ImGui::IsItemDeactivatedAfterEdit();
    prefHelp("Compass bearing of sunrise: 0 = +Z, 90 = +X.");
    ImGui::SliderFloat("Arc tilt", &c.sunTilt, -89.0f, 89.0f, "%.0f deg");
    changed |= ImGui::IsItemDeactivatedAfterEdit();
    prefHelp("How far the arc leans off the zenith. 0 puts the sun straight\n"
             "overhead at midday; 60 is a low winter sun with long shadows.");
    ImGui::SliderFloat("Sunrise", &c.sunrise, 0.0f, 24.0f, "%.1f h");
    changed |= ImGui::IsItemDeactivatedAfterEdit();
    ImGui::SliderFloat("Sunset", &c.sunset, 0.0f, 24.0f, "%.1f h");
    changed |= ImGui::IsItemDeactivatedAfterEdit();
    ImGui::SliderFloat("Sun size", &c.sunSize, 0.25f, 30.0f, "%.1f deg");
    changed |= ImGui::IsItemDeactivatedAfterEdit();
    prefHelp("Apparent diameter of the disc. The real sun is about 0.5 deg,\n"
             "which is almost invisible on a 512x448 frame - 3 reads better.");

    // --- moon --------------------------------------------------------------
    ImGui::SeparatorText("Moon");
    ImGui::SliderFloat("Moon bearing", &c.moonAzimuth, 0.0f, 360.0f, "%.0f deg");
    changed |= ImGui::IsItemDeactivatedAfterEdit();
    ImGui::SliderFloat("Moon arc tilt", &c.moonTilt, -89.0f, 89.0f, "%.0f deg");
    changed |= ImGui::IsItemDeactivatedAfterEdit();
    ImGui::SliderFloat("Hours behind the sun", &c.moonOffset, 0.0f, 24.0f, "%.1f h");
    changed |= ImGui::IsItemDeactivatedAfterEdit();
    prefHelp("12 puts the moon up exactly while the sun is down, which is what\n"
             "you want unless you are after a moon visible in daylight.");
    ImGui::SliderFloat("Moon size", &c.moonSize, 0.25f, 30.0f, "%.1f deg");
    changed |= ImGui::IsItemDeactivatedAfterEdit();
    ImGui::SliderFloat("Phase", &c.moonPhase, 0.0f, 1.0f, "%.2f");
    changed |= ImGui::IsItemDeactivatedAfterEdit();
    ImGui::SliderFloat("Opacity", &c.moonOpacity, 0.0f, 1.0f, "%.2f");
    changed |= ImGui::IsItemDeactivatedAfterEdit();
    prefHelp(
        "How solid the disc is. Applied when it is DRAWN, not baked into the\n"
        "texture, so dragging this needs no re-bake and one texture serves\n"
        "every value.\n\n"
        "Below 1 the sky shows through - which is what a moon behind thin cloud\n"
        "or a daytime moon actually looks like. 0 draws nothing at all.");
    prefHelp("0 = new, 0.5 = full, 1 = new again. Baked into the disc as a\n"
             "terminator, so it costs the console nothing.");
    {
        ImGui::Text("Moon texture: %s",
                    c.moonTexture.empty() ? "built-in (NASA LRO colour map)"
                                          : c.moonTexture.c_str());
        if (ImGui::Button("Import moon texture...")) {
            // Same import shape as the HUD image pickers: copy into res/ and
            // store the project-relative path.
            const std::string src = pickPngFile();
            if (!src.empty()) {
                const std::filesystem::path srcPath(src);
                const std::string fileName =
                    sanitizeAssetName(srcPath.filename().string());
                const std::filesystem::path destDir =
                    std::filesystem::path(project_.dir) / "res" / "textures";
                std::error_code ec;
                std::filesystem::create_directories(destDir, ec);
                std::filesystem::copy_file(
                    srcPath, destDir / fileName,
                    std::filesystem::copy_options::overwrite_existing, ec);
                if (!ec) {
                    c.moonTexture = "res/textures/" + fileName;
                    skyBodyMoonSig_.clear();  // force a re-bake
                    changed = true;
                } else {
                    statusMessage_ = "Moon texture import failed: " + ec.message();
                }
            }
        }
        if (!c.moonTexture.empty()) {
            ImGui::SameLine();
            if (ImGui::Button("Reset to built-in")) {
                c.moonTexture.clear();
                skyBodyMoonSig_.clear();
                changed = true;
            }
        }
        prefHelp(
            "Empty = NASA's Lunar Reconnaissance Orbiter colour map, built into\n"
            "the editor. A 2:1 image is treated as an equirectangular map of the\n"
            "sphere and projected; anything else is used as the disc face.\n\n"
            "The map itself never ships - only the small baked disc does.");
    }
    // The disc as it will be baked, at the size it is baked (the preview and
    // the shipped texture are the same pixels - see menubake::bakeMoonRGBA).
    if (uint32_t t = viewport_.moonDiscTexture())
        ImGui::Image((ImTextureID)(intptr_t)t, ImVec2(scaled(96), scaled(96)));

    // --- the runtime half ----------------------------------------------------
    ImGui::SeparatorText("Let the clock run (hybrid)");
    if (ImGui::Checkbox("Advance the time in game", &c.runtime)) changed = true;
    prefHelp(
        "The half of the cycle that costs nothing per frame follows a live\n"
        "clock: the sun and moon move, the sky dome and the fog retint, the\n"
        "projected shadows swing, the flare and god rays track the sun and the\n"
        "stars fade in.\n\n"
        "What does NOT follow is the BAKED half - vertex shading, the AO\n"
        "lightmap, any GI. Those stay at the hour above, because re-baking them\n"
        "is ~170 ms of EE work. That is what the colour grade below is for.");
    if (c.runtime) {
        ImGui::SliderFloat("Day length", &c.dayLength, 8.0f, 1800.0f, "%.0f s");
        changed |= ImGui::IsItemDeactivatedAfterEdit();
        prefHelp("Real seconds for a whole 24 hours. The time above is where\n"
                 "the clock STARTS (and still the hour everything bakes at).");
        ImGui::TextDisabled("%.1f s per in-game hour; sunrise to sunset takes %.0f s.",
                            c.dayLength / 24.0f,
                            c.dayLength * ambience::wrap24(c.sunset - c.sunrise) / 24.0f);
        if (ImGui::Checkbox("Drift a colour grade with the clock", &c.runtimeGrade))
            changed = true;
        prefHelp(
            "Without this, geometry baked at noon stays brightly lit under a\n"
            "midnight sky. The grade carries the world's overall level and\n"
            "warmth per frame - one postFx call, no re-bake - and is identity\n"
            "when the clock sits on the hour the scene was baked at.\n\n"
            "Turn it off if the project drives its own grading and you would\n"
            "rather bake several times of day as separate scenes.");
        ImGui::SliderFloat("Bake lighting at", &c.bakeHour, 0.0f, 24.0f, "%.1f h");
        changed |= ImGui::IsItemDeactivatedAfterEdit();
        prefHelp(
            "The hour shadows, AO and GI are baked at - a separate question from\n"
            "where the clock starts, once it moves. Noon is usually the right\n"
            "answer: it is the most neutral light for a lightmap, and the drift\n"
            "grade measures every other hour against it.");
        ImGui::TextDisabled("Clock starts %02d:%02d, geometry baked %02d:%02d.",
                            (int)c.time,
                            (int)((c.time - (float)(int)c.time) * 60.0f),
                            (int)c.bakeHour,
                            (int)((c.bakeHour - (float)(int)c.bakeHour) * 60.0f));
    }

    // --- stars ---------------------------------------------------------------
    ImGui::SeparatorText("Stars");
    if (ImGui::Checkbox("Night sky", &c.starsEnabled)) changed = true;
    prefHelp(
        "Real glowing points, not a sky texture: the field is drawn through\n"
        "ADDITIVE bags, so each star ADDS its colour to the sky behind it and a\n"
        "bright one saturates and blooms.\n\n"
        "One bag per magnitude tier = 3 submits for the whole sky, and the\n"
        "brightness rides the bags' additive FIX - so fading the stars in at\n"
        "dusk costs three bytes a frame, not a rebuild. How bright they get at\n"
        "each hour is the Stars column in the key table below.");
    if (c.starsEnabled) {
        starfield::Params& sp = c.starField;
        ImGui::SetNextItemWidth(scaled(120.0f));
        if (ImGui::InputInt("Seed", &sp.seed)) changed = true;
        ImGui::SameLine();
        if (ImGui::SmallButton("Reroll")) {
            sp.seed = (int)(ImGui::GetTime() * 1000.0) & 0x7fffffff;
            changed = true;
        }
        prefHelp("The same seed is the same sky, always - so nudging a slider\n"
                 "adjusts the sky instead of reshuffling it.");
        ImGui::SliderInt("Star count", &sp.count, 0, starfield::kMaxStars);
        changed |= ImGui::IsItemDeactivatedAfterEdit();
        ImGui::SliderFloat("Magnitude spread", &sp.magnitudeSpread, 0.0f, 1.0f, "%.2f");
        changed |= ImGui::IsItemDeactivatedAfterEdit();
        prefHelp("0 = every star equally bright; 1 = a realistic long tail of\n"
                 "faint stars under a handful of bright ones.");
        ImGui::SliderFloat("Milky Way", &sp.milkyWay, 0.0f, 1.0f, "%.2f");
        changed |= ImGui::IsItemDeactivatedAfterEdit();
        prefHelp("Density along a band. Without it a random sky reads as\n"
                 "uniform noise rather than as a sky.");
        ImGui::SliderFloat("Band tilt", &sp.milkyWayTilt, -89.0f, 89.0f, "%.0f deg");
        changed |= ImGui::IsItemDeactivatedAfterEdit();
        ImGui::SliderFloat("Star size", &sp.sizeScale, 0.25f, 4.0f, "%.2f");
        changed |= ImGui::IsItemDeactivatedAfterEdit();
        ImGui::SliderFloat("Twinkle", &c.starTwinkle, 0.0f, 1.0f, "%.2f");
        changed |= ImGui::IsItemDeactivatedAfterEdit();
        prefHelp("Each magnitude tier shimmers at its own rate. Per BAG, not\n"
                 "per star - three multiplies a frame for the whole sky.");
        ImGui::TextDisabled("%d stars = %d vertices in %d submits (the sky dome "
                            "next to it is 504).",
                            sp.count, sp.count * 6, starfield::kTiers);
    }

    // --- keys --------------------------------------------------------------
    ImGui::SeparatorText("Colours through the day");
    if (ImGui::Button("Seed a default day")) {
        c.keys = ambience::defaultKeys();
        selectedDayKey_ = -1;
        changed = true;
    }
    prefHelp("Replaces the key list with a night/dawn/day/dusk/night set.");
    ImGui::SameLine();
    if (ImGui::Button("+ Key at current time")) {
        DayKey k = ambience::sampleKeys(c, c.time);  // continue the curve
        k.hour = ambience::wrap24(c.time);
        c.keys.push_back(k);
        project::sortDayKeys(c);
        selectedDayKey_ = -1;
        changed = true;
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(selectedDayKey_ < 0 || selectedDayKey_ >= (int)c.keys.size());
    if (ImGui::Button("Remove key")) {
        c.keys.erase(c.keys.begin() + selectedDayKey_);
        selectedDayKey_ = -1;
        changed = true;
    }
    ImGui::EndDisabled();

    if (c.keys.empty()) {
        ImGui::TextDisabled("No keys - every hour resolves to the same neutral sky.");
        return;
    }
    if (ImGui::BeginTable("##daykeys", 8,
                          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                              ImGuiTableFlags_SizingFixedFit |
                              ImGuiTableFlags_ScrollY,
                          ImVec2(0, scaled(200)))) {
        ImGui::TableSetupColumn("Hour");
        ImGui::TableSetupColumn("Horizon");
        ImGui::TableSetupColumn("Zenith");
        ImGui::TableSetupColumn("Light");
        ImGui::TableSetupColumn("Fog");
        ImGui::TableSetupColumn("Amb");
        ImGui::TableSetupColumn("Diff");
        ImGui::TableSetupColumn("Stars");
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableHeadersRow();
        bool resort = false;
        for (int i = 0; i < (int)c.keys.size(); ++i) {
            DayKey& k = c.keys[i];
            ImGui::PushID(i);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(scaled(64));
            if (ImGui::DragFloat("##h", &k.hour, 0.05f, 0.0f, 24.0f, "%.2f"))
                selectedDayKey_ = i;
            if (ImGui::IsItemDeactivatedAfterEdit()) {
                resort = true;
                changed = true;
            }
            if (ImGui::IsItemActivated()) selectedDayKey_ = i;
            const ImGuiColorEditFlags cf =
                ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel;
            auto colCell = [&](const char* id, float* rgb) {
                ImGui::TableNextColumn();
                if (ImGui::ColorEdit3(id, rgb, cf)) selectedDayKey_ = i;
                changed |= ImGui::IsItemDeactivatedAfterEdit();
            };
            colCell("##sky", k.skyColor);
            colCell("##top", k.skyTopColor);
            colCell("##lit", k.lightColor);
            colCell("##fog", k.fogColor);
            auto numCell = [&](const char* id, float* v, float hi) {
                ImGui::TableNextColumn();
                ImGui::SetNextItemWidth(scaled(52));
                if (ImGui::DragFloat(id, v, 0.01f, 0.0f, hi, "%.2f"))
                    selectedDayKey_ = i;
                changed |= ImGui::IsItemDeactivatedAfterEdit();
            };
            numCell("##amb", &k.ambient, 1.0f);
            numCell("##dif", &k.diffuse, 1.0f);
            numCell("##str", &k.stars, 1.0f);
            ImGui::PopID();
        }
        ImGui::EndTable();
        if (resort) {
            project::sortDayKeys(c);
            selectedDayKey_ = -1;
        }
    }
    ImGui::TextDisabled(
        "Keys interpolate cyclically, so the last key of the day carries "
        "through\nmidnight into the first. Two keys holding the same colour "
        "are how you\nkeep night looking like night instead of ramping into "
        "dawn from 00:00.");

    if (changed) project::clampDayCycle(c);
}

// Pushes the sun/moon discs into the viewport, re-baking the moon only when its
// inputs moved. `presetIndex` >= 0 = preview that preset (the Ambience Editor is
// showing it); -1 = whatever the active scene resolves to.
void App::updateSkyBodyPreview(int presetIndex) {
    const DayCycle* c = nullptr;
    if (presetIndex >= 0 && presetIndex < (int)project_.ambiencePresets.size()) {
        const DayCycle& pc = project_.ambiencePresets[presetIndex].cycle;
        if (pc.enabled) c = &pc;
    } else {
        c = templates::sceneDayCycle(project_, project_.active());
    }
    if (!c) {
        viewport_.setSkyBodies(Viewport::SkyBodies{});
        viewport_.setStarField({}, 0.0f, 0.0f, 0.0f);
        return;
    }

    // The sun sprite is fixed, so it uploads once per session.
    if (!skyBodySunUploaded_) {
        std::vector<unsigned char> rgba;
        menubake::bakeSunRGBA(rgba);
        viewport_.setSkyBodyTexture(Viewport::SkySun, menubake::kSunDiscSize,
                                    menubake::kSunDiscSize, rgba.data());
        // The star dot rides along - it is fixed too, and it is what keeps a
        // star from drawing as a hard little square.
        menubake::bakeFlareRGBA(2, rgba);
        viewport_.setSkyBodyTexture(Viewport::SkyStarDot,
                                    menubake::kFlareSpriteSize,
                                    menubake::kFlareSpriteSize, rgba.data());
        skyBodySunUploaded_ = true;
    }
    // The moon is a real image projection - only re-bake when its inputs move.
    {
        char sig[64];
        std::snprintf(sig, sizeof(sig), "%.4f|", c->moonPhase);
        const std::string want = std::string(sig) + c->moonTexture;
        if (want != skyBodyMoonSig_) {
            std::vector<unsigned char> rgba;
            const std::string srcAbs = c->moonTexture.empty()
                                           ? std::string()
                                           : project_.filePath(c->moonTexture);
            if (menubake::bakeMoonRGBA(c->moonPhase, srcAbs, rgba)) {
                viewport_.setSkyBodyTexture(Viewport::SkyMoon,
                                            menubake::kMoonDiscSize,
                                            menubake::kMoonDiscSize, rgba.data());
                skyBodyMoonSig_ = want;
            }
        }
    }

    // The night sky. Regenerated only when its Params change - generate() is
    // deterministic, so the same seed is always the same sky and there is
    // nothing to gain from re-rolling it every frame.
    if (c->starsEnabled && c->starField.count > 0) {
        if (c->starField != skyBodyStarParams_) {
            skyBodyStarParams_ = c->starField;
            skyBodyStars_ = starfield::generate(c->starField);
        }
    } else if (!skyBodyStars_.empty()) {
        skyBodyStars_.clear();
        skyBodyStarParams_ = starfield::Params{};
    }

    const ambience::Resolved d = ambience::evaluate(*c, c->time);
    Viewport::SkyBodies b;
    b.enabled = true;
    for (int i = 0; i < 3; ++i) {
        b.sunDir[i] = d.sunDir[i];
        b.moonDir[i] = d.moonDir[i];
        b.sunColor[i] = d.lightColor[i];
    }
    // Same "keep drawing a little below the horizon" rule codegen bakes, so a
    // setting body slides out of view here too instead of vanishing whole.
    const float kDeg = 3.14159265f / 180.0f;
    b.sunRadius =
        d.sunElevation < -10.0f ? 0.0f : std::tan(c->sunSize * 0.5f * kDeg);
    b.moonRadius =
        d.moonElevation < -10.0f ? 0.0f : std::tan(c->moonSize * 0.5f * kDeg);
    b.moonRoll = d.moonUpAngle;
    b.moonOpacity = c->moonOpacity;
    for (int i = 0; i < 3; ++i) b.compensation[i] = skyBodyComp_[i];
    viewport_.setStarField(skyBodyStars_,
                           d.stars * (skyBodyComp_[0] + skyBodyComp_[1] +
                                      skyBodyComp_[2]) / 3.0f,
                           c->starTwinkle, (float)ImGui::GetTime());
    viewport_.setSkyBodies(b);
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
    // BindDisplayMode only: the engine mode per option (-1 = the project-
    // default boot mode). Empty on the other binds (positional mapping).
    std::vector<int> optionModes;
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
     // DEFAULT = the project's boot mode on the player's console (region +
     // the PAL-picture preference); the rest are explicit overrides.
     {"DEFAULT", "480p", "1080i"},
     {-1, 1, 2}},
    {"ASPECT", MenuEntry::Toggle, "opt_widescreen", 0.0f, MenuEntry::BindWidescreen,
     {"4:3", "16:9"}},
    {"PLAYERS", MenuEntry::Choice, "opt_players", 0.0f, MenuEntry::BindPlayerCount,
     {"1 Player", "2 Players"}},
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
    // Display rows map options to engine modes explicitly (the preset
    // carries the table); the other binds map by position.
    if (s.bind == MenuEntry::BindDisplayMode) {
        en.optionModes = s.optionModes;
        for (size_t o = en.optionModes.size(); o < en.options.size(); ++o)
            en.optionModes.push_back((int)(o < 4 ? o : 4));
    }
    m.entries.push_back(std::move(en));
}

// A save value by name, created with `def` when the project has none yet.
// Returns the name, so a caller can assign it straight into MenuEntry::param.
std::string ensureSaveValue(Project& p, const std::string& name, float def) {
    for (const SaveValue& sv : p.saveValues)
        if (sv.name == name) return name;
    SaveValue sv;
    sv.name = name;
    sv.value = def;
    p.saveValues.push_back(std::move(sv));
    return name;
}

// Scaffold a CONTROLS page: one "Rebind key" row per rebindable input action,
// plus a preset picker when the project has more than one preset. This is the
// whole in-game key-assignment story wired up in one click
// (docs/input-bindings.md).
void addRebindRows(Project& p, GameMenu& m) {
    if (p.input.presets.size() > 1) {
        MenuEntry pr;
        pr.label = "PRESET";
        pr.action = MenuEntry::Choice;
        pr.settingBind = MenuEntry::BindInputPreset;
        pr.param = ensureSaveValue(p, "input-preset",
                                   (float)p.input.activePreset);
        for (const InputPreset& e : p.input.presets) pr.options.push_back(e.name);
        m.entries.push_back(std::move(pr));
    }
    for (const InputAction& a : p.input.actions) {
        if (!a.rebindable) continue;
        if ((int)m.entries.size() >= menubake::kMaxEntries) break;
        MenuEntry en;
        en.label = a.label.empty() ? a.name : a.label;
        en.action = MenuEntry::RebindKey;
        en.bindAction = a.name;
        en.param = ensureSaveValue(p, "bind-" + a.name, 0.0f);
        m.entries.push_back(std::move(en));
    }
}

// The plain "APPLY" action row that commits a display-mode row's staged
// selection (MenuEntry::ApplyVideo) - inserted next to the DISPLAY block.
MenuEntry makeApplyVideoEntry() {
    MenuEntry en;
    en.label = "APPLY";
    en.action = MenuEntry::ApplyVideo;
    return en;
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
    auto makeSub = [&](const char* base, const char* title, int b0, int b1,
                       bool applyVideo = false) {
        GameMenu sub;
        sub.name = uniqueName(base);
        sub.title = title;
        addOptionBlock(p, sub, b0);
        addOptionBlock(p, sub, b1);
        // The APPLY row makes the display-mode row stage-then-commit: the
        // player browses the modes freely, the switch fires on APPLY.
        if (applyVideo) sub.entries.push_back(makeApplyVideoEntry());
        p.menus.push_back(std::move(sub));
        return p.menus.back().name;
    };
    const std::string audio = makeSub("options-audio", "AUDIO", 0, 1);
    // CONTROLS carries the stick settings only. Key rebinding is deliberately
    // NOT scaffolded: it needs one save value per action and most projects want
    // a fixed control scheme, so it stays an explicit choice
    // (+ Option block > Key bindings). See docs/input-bindings.md.
    const std::string controls = makeSub("options-controls", "CONTROLS", 2, 3);
    const std::string display = makeSub("options-display", "DISPLAY", 4, 5, true);
    GameMenu root;
    root.name = uniqueName("options");
    root.title = "OPTIONS";
    // Opens at game start, so the scaffold is something you can see immediately
    // instead of a menu nothing reaches yet. Skipped when another menu already
    // claims the title screen - codegen takes the FIRST one, so setting it twice
    // would silently pick a winner.
    bool titleTaken = false;
    for (const GameMenu& o : p.menus) titleTaken |= o.titleScreen;
    root.titleScreen = !titleTaken;
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
            "display mode + an APPLY row that commits it, aspect).\n"
            "The root opens at game start (unless another menu already\n"
            "does). Style and edit them like any menu. Key rebinding is\n"
            "NOT included - add it deliberately with + Option block >\n"
            "Key bindings.");
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
    changed |= textTokenPicker("titletok", m.title);
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
    changed |= fontCombo(m.font);
    {
        ImGui::SetNextItemWidth(scaled(110.0f));
        if (ImGui::DragInt("Title size", &m.titleSize, 0.2f, 10, 48, "%d px"))
            changed = true;
        ImGui::SameLine();
        ImGui::SetNextItemWidth(scaled(110.0f));
        if (ImGui::DragInt("Row size", &m.entrySize, 0.2f, 8, 32, "%d px"))
            changed = true;
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(
                "Entry text height, and with it the row pitch and the cursor.\n"
                "Any {{glyph}} in a label scales with it too.");
        if (m.titleSize < 10) m.titleSize = 10;
        if (m.titleSize > 48) m.titleSize = 48;
        if (m.entrySize < 8) m.entrySize = 8;
        if (m.entrySize > 32) m.entrySize = 32;
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
        "Choice",         "Apply video mode",  "Rebind key",     "Play credits"};
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
        changed |= textTokenPicker("labeltok", en.label);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(scaled(150.0f));
        if (ImGui::Combo("##action", &en.action, kActionNames,
                         IM_ARRAYSIZE(kActionNames))) {
            en.param.clear();
            en.optionModes.clear();
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
        } else if (en.action == MenuEntry::PlayCredits) {
            paramCombo("##credits", "<roll>", project_.credits,
                       [](const CreditsRoll& r) -> const std::string& { return r.name; });
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(
                    "Credits roll to play (Tools > Credits Editor). The menu\n"
                    "closes first; where the player ends up afterwards is the\n"
                    "roll's own finish action.");
        } else if (en.action == MenuEntry::RebindKey) {
            // Two references: WHICH action to rebind, and the save value the
            // player's override is persisted in (docs/input-bindings.md).
            ImGui::SetNextItemWidth(scaled(110.0f));
            if (ImGui::BeginCombo("##rebindaction", en.bindAction.empty()
                                                        ? "<action>"
                                                        : en.bindAction.c_str())) {
                for (const InputAction& a : project_.input.actions) {
                    if (!a.rebindable) continue;  // not offered to the player
                    if (ImGui::Selectable(a.name.c_str(), a.name == en.bindAction)) {
                        en.bindAction = a.name;
                        if (en.label == "New entry" || en.label.empty())
                            en.label = a.label;
                        // Give the row its backing save value straight away -
                        // without one the override cannot persist.
                        if (en.param.empty())
                            en.param =
                                ensureSaveValue(project_, "bind-" + a.name, 0.0f);
                        changed = true;
                    }
                }
                ImGui::EndCombo();
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(
                    "Input Map action this row rebinds. Only actions marked\n"
                    "\"Player may rebind it in-game\" are listed.\n"
                    "The row covers the PAD button only - keyboard/mouse keys\n"
                    "stay as the Input Map authored them (that support is\n"
                    "experimental and gets its own menu later).");
            ImGui::SameLine();
            paramCombo("##rebindvalue", "<value>", project_.saveValues,
                       [](const SaveValue& v) -> const std::string& { return v.name; });
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(
                    "Save value the override is stored in (an inputCodes\n"
                    "index; 0 = the project's preset binding). Persists in\n"
                    "memory card saves like every other menu state.");
        } else if (en.action == MenuEntry::ApplyVideo) {
            ImGui::TextDisabled("(?)");
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(
                    "Commits the display-mode row's staged selection (any\n"
                    "menu's). While a menu has this row, the display-mode\n"
                    "row only cycles its value - the screen switches when\n"
                    "the player picks APPLY (with the keep-or-revert\n"
                    "prompt). Without one, the display row switches on\n"
                    "every change, closing the menu each time.");
            ImGui::SameLine();
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
            if (en.settingBind == MenuEntry::BindDisplayMode) {
                // Display-mode rows: each option picks an engine scan mode
                // from a dropdown, with a free-text label next to it (rename
                // "480i" to "576i" for a PAL release). The mode drives the
                // generated game (MenuEntryData::optModes); the label is
                // only what the row draws. Index 0 is the -1 sentinel: the
                // project-default mode, resolved at boot on the player's
                // console (region + the PAL-picture preference).
                static const char* kDispModeNames[] = {"DEFAULT", "480i",
                                                       "480p",    "1080i",
                                                       "480i FIELD", "576i"};
                static const char* kDispModeDescs[] = {
                    "Default (project) - the mode the game boots in",
                    "480i - interlaced (stock, letterboxed 576i on PAL)",
                    "480p - progressive (component, 60 Hz)",
                    "1080i - HD (component, 60 Hz)",
                    "480i FIELD - field rendering (half VRAM)",
                    "576i - full-height PAL (always 50 Hz)"};
                if (en.options.empty()) {
                    en.options = {"DEFAULT", "480p"};
                    en.optionModes = {-1, 1};
                }
                if (en.optionModes.size() != en.options.size()) {
                    en.optionModes.resize(en.options.size());
                    for (size_t o = 0; o < en.optionModes.size(); ++o)
                        en.optionModes[o] = (int)(o < 4 ? o : 4);
                }
                for (int o = 0; o < (int)en.options.size(); ++o) {
                    ImGui::PushID(o);
                    int mode = en.optionModes[o];  // dropdown index = mode + 1
                    if (mode < -1) mode = -1;
                    if (mode > 4) mode = 4;
                    ImGui::SetNextItemWidth(scaled(230.0f));
                    if (ImGui::BeginCombo("##optmode",
                                          kDispModeDescs[mode + 1])) {
                        for (int mo = -1; mo < 5; ++mo)
                            if (ImGui::Selectable(kDispModeDescs[mo + 1],
                                                  mo == mode)) {
                                en.optionModes[o] = mo;
                                en.options[o] = kDispModeNames[mo + 1];
                                changed = true;
                            }
                        ImGui::EndCombo();
                    }
                    ImGui::SameLine();
                    char optBuf[32];
                    std::snprintf(optBuf, sizeof(optBuf), "%s",
                                  en.options[o].c_str());
                    ImGui::SetNextItemWidth(scaled(110.0f));
                    if (ImGui::InputText("##opt", optBuf, sizeof(optBuf)))
                        en.options[o] = optBuf;
                    changed |= ImGui::IsItemDeactivatedAfterEdit();
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip(
                            "Label drawn on the row (free text) - the\n"
                            "dropdown decides the actual mode.");
                    ImGui::SameLine();
                    ImGui::BeginDisabled((int)en.options.size() <= 1);
                    if (ImGui::SmallButton("x##optdel")) {
                        en.options.erase(en.options.begin() + o);
                        en.optionModes.erase(en.optionModes.begin() + o);
                        changed = true;
                        ImGui::EndDisabled();
                        ImGui::PopID();
                        break;
                    }
                    ImGui::EndDisabled();
                    ImGui::PopID();
                }
                if ((int)en.options.size() < menubake::kMaxOptions) {
                    if (ImGui::SmallButton("+##optadd")) {
                        int mode = 0;  // first mode this row doesn't offer yet
                        for (int mo = -1; mo < 5; ++mo) {
                            bool used = false;
                            for (int v : en.optionModes) used |= (v == mo);
                            if (!used) {
                                mode = mo;
                                break;
                            }
                        }
                        en.options.push_back(kDispModeNames[mode + 1]);
                        en.optionModes.push_back(mode);
                        changed = true;
                    }
                }
            } else if (en.action == MenuEntry::Toggle) {
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
                             "Stick curve\0Display mode\0Widescreen\0"
                             "Player count\0Input preset\0")) {
                // Display rows carry an explicit option->mode table (edited
                // above); every other bind maps by option position.
                if (en.settingBind == MenuEntry::BindDisplayMode) {
                    en.optionModes.resize(en.options.size());
                    for (size_t o = 0; o < en.optionModes.size(); ++o)
                        en.optionModes[o] = (int)(o < 4 ? o : 4);
                } else {
                    en.optionModes.clear();
                }
                // An input-preset row's options ARE the presets, in order -
                // the option index is the preset index at runtime.
                if (en.settingBind == MenuEntry::BindInputPreset) {
                    en.options.clear();
                    for (const InputPreset& pr : project_.input.presets)
                        en.options.push_back(pr.name);
                }
                changed = true;
            }
            ImGui::SameLine();
            ImGui::TextDisabled("(?)");
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(
                    "Drives a built-in setting from this row's option index,\n"
                    "spread evenly across the options: volume 0-100%%, deadzone\n"
                    "0-0.4, aim curve 1-3, display DEFAULT (the project boot\n"
                    "mode)/480i/480p/1080i/480i FIELD/576i (each option picks\n"
                    "its mode from a dropdown;\n"
                    "add an Apply video mode row so switching waits for APPLY),\n"
                    "aspect 4:3/16:9, player count 1P/2P (needs a Multiplayer\n"
                    "mode + a second Player object), input preset (the Tools >\n"
                    "Input Map presets, in order). None = a plain save-value\n"
                    "row (flow graphs react).");
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
                "aspect ratio, player count (1P/2P, two-player modes).\n"
                "Restyle and relabel it like any other entry.");
        if (ImGui::BeginPopup("##optblock")) {
            static const char* kBlockMenu[] = {
                "Music volume", "Sound volume", "Controller deadzone",
                "Aim response curve", "Display mode", "Widescreen (aspect)",
                "Player count (1P/2P)"};
            for (int b = 0; b < kOptionBlockCount; ++b)
                if (ImGui::Selectable(kBlockMenu[b])) {
                    addOptionBlock(project_, m, b);
                    changed = true;
                }
            ImGui::Separator();
            if (ImGui::Selectable("Key bindings (all rebindable actions)")) {
                // One Rebind key row per action + a preset picker: the whole
                // controls page (docs/input-bindings.md).
                addRebindRows(project_, m);
                changed = true;
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(
                    "Adds a \"Rebind key\" row for every Input Map action\n"
                    "marked \"Player may rebind it in-game\", each backed by\n"
                    "its own save value, plus a preset picker when the project\n"
                    "has more than one preset.");
            ImGui::Separator();
            if (ImGui::Selectable("Apply video mode (row)")) {
                m.entries.push_back(makeApplyVideoEntry());
                changed = true;
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(
                    "Commits the Display mode row's selection. With this row\n"
                    "anywhere in the project, cycling the display option only\n"
                    "stages it - the screen switches when the player picks\n"
                    "APPLY (keep-or-revert prompt included).");
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
                          std::to_string(m.showTitle) + "\x1f" + m.font +
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
            if (menubake::bakePanelRGBA(m, project_, rgba, w, h)) {
                // Composite each Toggle/Choice row's initial option label
                // where the game draws the value strip cell.
                std::vector<int> current(m.entries.size(), 0);
                for (size_t e = 0; e < m.entries.size(); ++e)
                    for (const SaveValue& sv : project_.saveValues)
                        if (sv.name == m.entries[e].param)
                            current[e] = (int)sv.value;
                menubake::overlayValuePreview(m, project_, current, rgba, w, h);
                if (!menuPreviewTex_) glGenTextures(1, &menuPreviewTex_);
                glBindTexture(GL_TEXTURE_2D, menuPreviewTex_);
                glUploadTexRgba(w, h, rgba.data());
                menuPreviewW_ = w;
                menuPreviewH_ = h;
                const menubake::PanelLayout lay =
                    menubake::panelLayout(m, project_);
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
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip(
            "Object scripts (TYRA_OBJECT_SCRIPT) run when attached to objects:\n"
            "Properties > Scripts. Plain TYRA_SCRIPT classes run globally every\n"
            "frame. Subfolders are compiled too; generated code lives in src\\gen.");

    // List user scripts as a folder tree: every .cpp under src/scripts,
    // subfolders included - that directory is exclusively the user's. Engine-
    // generated sources live in src/gen and never show here (any stray
    // *.gen.cpp left by an old editor version is filtered out for good
    // measure). Click a file to open it in VS Code in the project context.
    const std::filesystem::path dir = std::filesystem::path(project_.dir) / "src" / "scripts";
    std::error_code ec;

    struct ScriptNode {
        std::map<std::string, ScriptNode> folders;  // sorted subfolders
        std::vector<std::string> files;             // leaf filenames
    };
    ScriptNode root;
    bool any = false;
    if (std::filesystem::exists(dir, ec)) {
        for (std::filesystem::recursive_directory_iterator it(dir, ec), end;
             it != end && !ec; it.increment(ec)) {
            if (!it->is_regular_file(ec)) continue;
            if (it->path().extension() != ".cpp") continue;
            const std::string fname = it->path().filename().string();
            if (fname.size() > 8 &&
                fname.compare(fname.size() - 8, 8, ".gen.cpp") == 0)
                continue;
            // Slot the relative path into the tree: walk/create a folder node
            // per path segment, drop the filename in the final node's files.
            const std::string rel =
                std::filesystem::relative(it->path(), dir, ec).string();
            ScriptNode* node = &root;
            for (size_t start = 0;;) {
                size_t sep = rel.find_first_of("/\\", start);
                if (sep == std::string::npos) {
                    node->files.push_back(rel.substr(start));
                    break;
                }
                node = &node->folders[rel.substr(start, sep - start)];
                start = sep + 1;
            }
            any = true;
        }
    }

    if (!any) {
        ImGui::TextDisabled("No scripts yet.");
        return;
    }

    // Render folders first (open by default), then files. TreeNodeEx pushes an
    // ID scope per folder, so same-named files in different folders don't
    // collide. prefix carries the src/scripts-relative path for opening -
    // forward slashes, because it is handed on as a path, not as display text.
    std::function<void(const ScriptNode&, const std::string&)> drawNode =
        [&](const ScriptNode& n, const std::string& prefix) {
            for (const auto& [name, child] : n.folders) {
                if (ImGui::TreeNodeEx(name.c_str(),
                                      ImGuiTreeNodeFlags_DefaultOpen |
                                          ImGuiTreeNodeFlags_SpanAvailWidth)) {
                    drawNode(child, prefix + name + "/");
                    ImGui::TreePop();
                }
            }
            std::vector<std::string> files = n.files;
            std::sort(files.begin(), files.end());
            for (const std::string& f : files) {
                ImGui::Bullet();
                ImGui::SameLine();
                if (ImGui::Selectable(f.c_str()))
                    openInVSCode("src/scripts/" + prefix + f);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Open in VS Code");
            }
        };
    drawNode(root, "");
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
    ImGui::TextDisabled("Creates src/scripts/%s.cpp (subfolders allowed: ai/guard)",
                        newScriptName_);
    if (newScriptAttachTo_ >= 0 && newScriptAttachTo_ < (int)project_.objects().size())
        ImGui::TextDisabled("Attaches it to \"%s\".",
                            project_.objects()[newScriptAttachTo_].name.c_str());

    if (!newScriptError_.empty())
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", newScriptError_.c_str());

    ImGui::Separator();
    if (ImGui::Button("Create", ImVec2(scaled(120), 0))) {
        std::string name = newScriptName_;
        for (char& c : name)
            if (c == '\\') c = '/';  // one separator form for path + display
        // Segments of letters/digits/_/- split by '/': "ai/guard" makes the
        // subfolder. Empty segments (leading//trailing slash) are invalid;
        // ".." is impossible since '.' is rejected.
        bool valid = !name.empty() && name.front() != '/' && name.back() != '/';
        for (char c : name)
            if (!isalnum((unsigned char)c) && c != '_' && c != '-' && c != '/')
                valid = false;
        if (name.find("//") != std::string::npos) valid = false;

        if (!valid) {
            newScriptError_ =
                "Name may contain only letters, digits, '_', '-' and '/'";
        } else {
            // File name -> C++ class name (ai/my-script -> My_script)
            std::string base = name;
            if (const size_t slash = base.find_last_of('/');
                slash != std::string::npos)
                base = base.substr(slash + 1);
            std::string className;
            for (char c : base) className += (isalnum((unsigned char)c) ? c : '_');
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
    if (deleteScenePending_ == project_.startScene)
        ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.3f, 1.0f),
                           "This is the start scene - the first one takes its place.");

    ImGui::Separator();
    if (ImGui::Button("Delete", ImVec2(scaled(120), 0))) {
        project_.scenes.erase(project_.scenes.begin() + deleteScenePending_);
        if (project_.activeScene >= (int)project_.scenes.size() ||
            project_.activeScene == deleteScenePending_)
            project_.activeScene = 0;
        // Deleting shifts every later index down by one, so the start scene
        // follows its scene rather than staying on a number. Deleting the start
        // scene itself falls back to the first one.
        if (project_.startScene == deleteScenePending_)
            project_.startScene = 0;
        else if (project_.startScene > deleteScenePending_)
            --project_.startScene;
        project::clampStartScene(project_);
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
            // Its own custom LOD list goes, and so does any reference to it as
            // another model's LOD level (a chain with a missing level would
            // just warn at every build).
            project_.modelLods.erase(d.relPath);
            for (auto it = project_.modelLods.begin();
                 it != project_.modelLods.end();) {
                std::vector<std::string>& tiers = it->second;
                for (size_t i = 0; i < tiers.size(); ++i)
                    if (tiers[i] == d.relPath) {
                        tiers.resize(i);  // and every coarser level after it
                        break;
                    }
                it = tiers.empty() ? project_.modelLods.erase(it) : std::next(it);
            }
            // Its recorded real-world size goes with it - a re-import asks
            // again rather than inheriting the deleted file's answer.
            project_.modelUnitMeters.erase(d.relPath);
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
    ImGui::Checkbox("Create terrain", &newSceneTerrain_);
    prefHelp(
        "Off starts the scene with no ground at all - nothing is drawn and the\n"
        "game has no floor here either, so its floors are the geometry you\n"
        "place (docs/terrain.md). The size below stays the world bounds, and\n"
        "the terrain can be created later in the Terrain Editor.");
    ImGui::DragInt(newSceneTerrain_ ? "Terrain width" : "World width",
                   &newSceneWidth_, 1.0f, 8, 4096, "%d units");
    ImGui::DragInt(newSceneTerrain_ ? "Terrain depth" : "World depth",
                   &newSceneDepth_, 1.0f, 8, 4096, "%d units");
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
            sc.terrain.enabled = newSceneTerrain_;
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

// ---------------------------------------------------------------------------
// The log panels (docs/log-panels.md). Output (the Runner's build/run stream)
// and Debug (the game's own log.txt / PCSX2's emulog.txt) are the same problem:
// a growing pile of lines whose interesting part is a handful of warnings and
// errors buried in tool chatter. logview.cpp classifies every line into
// error / warning / info / verbose; the two functions below draw that
// classification - a filter chip per level with its count, then the lines
// coloured by level - and both panels share the body so they cannot drift.
// ---------------------------------------------------------------------------

// A level's colour. Meanings, not literals (docs/editor-theme.md): an error is
// the theme's danger colour, ordinary tool output its dim text.
static ImVec4 logLevelColor(logview::Level l) {
    const theme::Semantics& s = theme::semantics();
    switch (l) {
        case logview::Level::Error: return s.danger;
        case logview::Level::Warning: return s.warn;
        case logview::Level::Info: return s.text;
        default: return s.textDim;
    }
}

// One filter chip - "3 errors", in its level's colour, quiet while the level is
// hidden. Returns true when clicked; the caller flips the mask bit.
static bool logLevelChip(logview::Level l, int count, bool on) {
    ImVec4 col = logLevelColor(l);
    if (!on) col.w *= 0.40f;  // hidden, but still readable enough to click back
    char label[64];
    // The count is part of the label, so the id has to be pinned separately -
    // otherwise every appended line gives the chip a new id and drops its hover.
    snprintf(label, sizeof(label), "%d %s##logchip%d", count,
             logview::label(l, count != 1), (int)l);
    ImGui::PushStyleColor(ImGuiCol_Text, col);
    ImGui::PushStyleColor(ImGuiCol_Button,
                          ImGui::GetStyleColorVec4(on ? ImGuiCol_FrameBgActive
                                                      : ImGuiCol_FrameBg));
    const bool clicked = ImGui::SmallButton(label);
    ImGui::PopStyleColor(2);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip(on ? "Showing these lines - click to hide them"
                             : "These lines are hidden - click to show them");
    return clicked;
}

// Hands a panel its current text and classifies what is new in it.
void App::logSetText(LogView& v, std::string&& text) {
    // Append-only is the common case - a build streaming lines in - so keep the
    // classified prefix and look only at what arrived: re-classifying a
    // megabyte on every appended line costs far more than a frame. Anything
    // else (Clear, a new run recreating bin/log.txt, the Debug window's source
    // combo, a 1 MB tail whose start has moved) starts over.
    const bool appended =
        text.size() >= v.text.size() && text.compare(0, v.text.size(), v.text) == 0;
    if (appended && text.size() == v.text.size()) return;  // nothing new
    if (!appended) {
        v.lines.clear();
        v.parsed = 0;
        v.complete = 0;
        v.state = logview::State{};
    }
    v.text = std::move(text);
    v.lines.resize(v.complete);  // drop last frame's provisional tail line
    v.parsed = logview::parse(v.text, v.parsed, v.state, v.lines);
    v.complete = v.lines.size();
    logview::appendPartial(v.text, v.parsed, v.state, v.lines);
    v.dirty = true;
}

// Rebuilds what the draw needs from (lines, mask). Call once per frame per
// panel, before the panel's own buttons - they read the counts and the filtered
// text. Cheap while nothing changed.
void App::logRefresh(LogView& v) {
    const float font = ImGui::GetFontSize();
    if (!v.dirty && v.widthFont == font) return;

    v.visible.clear();
    for (int i = 0; i < logview::kLevelCount; ++i) v.counts[i] = 0;
    size_t widest = 0, widestLen = 0;
    for (size_t i = 0; i < v.lines.size(); ++i) {
        const logview::Line& ln = v.lines[i];
        // The chips count ENTRIES, not lines: a compiler error plus its source
        // snippet is one problem, and "4 errors" for it would be a lie.
        if (!ln.cont) v.counts[(int)ln.level]++;
        if (!(v.mask & logview::bit(ln.level))) continue;
        v.visible.push_back((int)i);
        if (ln.end - ln.begin > widestLen) {
            widestLen = ln.end - ln.begin;
            widest = i;
        }
    }
    // The horizontal scroll range. A clipper only submits the lines on screen,
    // so the child cannot measure its own content width - and measuring every
    // line of a megabyte log is not affordable per rebuild. The longest line in
    // BYTES stands in for the widest in pixels (a log's lines are all the same
    // typeface), and one CalcTextSize on it is free.
    v.width = 0.0f;
    if (widestLen) {
        const logview::Line& ln = v.lines[widest];
        v.width = ImGui::CalcTextSize(v.text.c_str() + ln.begin, v.text.c_str() + ln.end).x;
    }
    v.widthFont = font;
    // Only the selectable-text mode needs the lines as one string; Copy builds
    // its own on demand, so an ordinary streaming build pays no joins.
    if (v.selectText)
        v.joined = logview::join(v.text, v.lines, v.mask);
    else
        v.joined.clear();
    v.dirty = false;
}

void App::drawLogPanel(const char* id, LogView& v) {
    // Here rather than in the callers: a Clear / source switch above this point
    // has just replaced v.text and cleared v.lines, and `visible` still indexes
    // the text that is gone. A no-op when nothing changed.
    logRefresh(v);

    // Most severe first - that is the order a reader scans a log console in.
    for (int i = logview::kLevelCount - 1; i >= 0; --i) {
        const logview::Level l = (logview::Level)i;
        if (i != logview::kLevelCount - 1) ImGui::SameLine();
        if (logLevelChip(l, v.counts[i], (v.mask & logview::bit(l)) != 0)) {
            v.mask ^= logview::bit(l);
            v.dirty = true;
            saveGlobalConfig();
        }
    }
    ImGui::SameLine();
    if (ImGui::Checkbox("Select text", &v.selectText)) {
        v.dirty = true;
        v.scrollBottom = true;  // the two modes measure their content differently
        saveGlobalConfig();
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip(
            "Draw the filtered log in a read-only text box so it can be\n"
            "selected with the mouse. ImGui's editable text is one colour,\n"
            "so this trades the per-level colouring for selection - the\n"
            "filter above keeps working either way.");
    ImGui::Separator();

    // Own scrolling child so the scroll can be driven directly (see the
    // autoscroll note below).
    if (!v.selectText)
        ImGui::SetNextWindowContentSize(
            ImVec2(v.width + ImGui::GetStyle().FramePadding.x * 2.0f, 0.0f));
    ImGui::BeginChild(id, ImVec2(0.0f, 0.0f), ImGuiChildFlags_None,
                      ImGuiWindowFlags_HorizontalScrollbar);

    if (v.selectText) {
        // Sizing the input to its own content means this child (not the input)
        // owns the scrollbars, so GetScrollY/SetScrollHereY refer to what we see.
        const ImVec2 pad = ImGui::GetStyle().FramePadding;
        const ImVec2 textSize = ImGui::CalcTextSize(v.joined.c_str(), nullptr, false);
        const ImVec2 avail = ImGui::GetContentRegionAvail();
        const ImVec2 inputSize(ImMax(textSize.x + pad.x * 2.0f, avail.x),
                               ImMax(textSize.y + pad.y * 2.0f, avail.y));
        ImGui::InputTextMultiline("##logtext", const_cast<char*>(v.joined.c_str()),
                                  v.joined.size() + 1, inputSize,
                                  ImGuiInputTextFlags_ReadOnly);
    } else if (v.visible.empty()) {
        // Say WHY it is empty: an all-levels-off filter otherwise reads exactly
        // like a log that has nothing in it.
        if (v.lines.empty())
            ImGui::TextDisabled("(nothing logged yet)");
        else
            ImGui::TextDisabled("%d line%s hidden by the filter above.",
                                (int)v.lines.size(), v.lines.size() == 1 ? "" : "s");
    } else {
        ImGuiListClipper clipper;
        clipper.Begin((int)v.visible.size());
        while (clipper.Step()) {
            for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
                const logview::Line& ln = v.lines[v.visible[i]];
                ImGui::PushStyleColor(ImGuiCol_Text, logLevelColor(ln.level));
                ImGui::TextUnformatted(v.text.c_str() + ln.begin, v.text.c_str() + ln.end);
                ImGui::PopStyleColor();
            }
        }
    }

    // Stick to the bottom while new lines arrive, but only when the user is
    // already at the bottom (scrolling up to read or select holds position).
    // GetScrollMaxY() lags one frame behind the content just appended, so when
    // we were pinned last frame Scroll.y still equals it here and the test
    // passes; once the user scrolls up it no longer does and we let go.
    if (v.scrollBottom ||
        (v.visible.size() != v.shown && ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 1.0f))
        ImGui::SetScrollHereY(1.0f);
    v.scrollBottom = false;
    v.shown = v.visible.size();

    ImGui::EndChild();
}

void App::drawOutputWindow() {
    ImGui::Begin("Output");
    logSetText(logOut_, runner_.log());

    if (ImGui::SmallButton("Clear")) runner_.clearLog();
    ImGui::SameLine();
    if (ImGui::SmallButton("Copy"))
        ImGui::SetClipboardText(logview::join(logOut_.text, logOut_.lines, logOut_.mask).c_str());
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Copies the lines currently shown - the filter applies.");
    ImGui::SameLine();
    drawLogPanel("##logscroll", logOut_);
    ImGui::End();
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

    // The source file is resolved and READ first, because the buttons below
    // report on the classified lines (the level counts, Copy) - so the Reload
    // button arms the next frame's read rather than this one's.
    std::string path;
    if (hasProject_) {
        if (debugLogSource_ == 0)
            path = (std::filesystem::path(project_.dir) / "bin" / "log.txt").string();
        else
            path = runner_.emulatorLogPath(project_);
    }
    // Refresh from disk on demand, and while Auto is on, at most twice a second
    // (per-frame file reads would be wasteful for a possibly large log).
    const double now = ImGui::GetTime();
    if (!path.empty() && (debugReloadNow_ || (debugAutoReload_ && now >= debugNextReload_))) {
        logSetText(logDbg_, readTextFileTail(path, 1u << 20));  // last 1 MB
        debugNextReload_ = now + 0.5;
    }
    debugReloadNow_ = false;

    // Game log = the game's own TYRA_LOG output (bin/log.txt, written on the
    // host fs by the running ELF); Emulator log = PCSX2's console (emulog.txt).
    const char* sources[] = {"Game log", "Emulator log"};
    ImGui::SetNextItemWidth(scaled(140));
    if (ImGui::Combo("Source", &debugLogSource_, sources, 2)) {
        logSetText(logDbg_, std::string());  // don't show the other source's content
        debugNextReload_ = 0.0;              // reload immediately from the new source
    }

    ImGui::SameLine();
    if (ImGui::SmallButton("Reload")) debugReloadNow_ = true;
    ImGui::SameLine();
    ImGui::Checkbox("Auto", &debugAutoReload_);
    ImGui::SameLine();
    if (ImGui::SmallButton("Copy"))
        ImGui::SetClipboardText(logview::join(logDbg_.text, logDbg_.lines, logDbg_.mask).c_str());
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Copies the lines currently shown - the filter applies.");
    ImGui::SameLine();
    if (ImGui::SmallButton("Clear log") && !path.empty()) {
        std::ofstream(path, std::ios::trunc);  // best effort; may be held open
        logSetText(logDbg_, std::string());
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

    drawLogPanel("##debugscroll", logDbg_);
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
    if (ImGui::Button("Export ISO")) runner_.exportIso(projectForBuild());
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

// The starting presets New Project offers. The chosen one becomes
// Project::gameTemplate for the project's LIFE - Project > Preferences shows it
// read-only - because the game sources it generates are user-ownable: switching
// afterwards would either overwrite the user's work or leave an owned file no
// longer matching what the project builds. FPP and Third person generate the
// same sources and differ in the seeded Player object's mode.
struct NewProjectPreset {
    const char* label;     // the New Project combo
    const char* preset;    // project::create() argument
    const char* tmpl;      // the resulting Project::gameTemplate
};
static const NewProjectPreset kNewPresets[] = {
    // Short labels - the combo is one field wide and the "(?)" next to it
    // carries the explanation.
    {"FPP (first person)", "fpp", "fpp"},
    {"Third person", "thirdperson", "thirdperson"},
    {"Empty (no objects)", "empty", "orbit"},
};
static const int kNewPresetCount = (int)(sizeof(kNewPresets) / sizeof(kNewPresets[0]));

// Which preset a project was created from, by its stored game template. Never
// fails: project::load clamps unknown template strings to "orbit".
static int presetIndexOf(const std::string& tmpl) {
    for (int i = 0; i < kNewPresetCount; ++i)
        if (tmpl == kNewPresets[i].tmpl) return i;
    return kNewPresetCount - 1;  // "orbit" / anything unknown = Empty
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

        // World scale first: the terrain size below reads in meters through it,
        // and it is the one setting that is genuinely hard to change later (it
        // rescales nothing by design, so a world built at the wrong scale stays
        // that size). See docs/world-scale.md.
        ImGui::SeparatorText("World scale");
        struct UnitsPreset {
            const char* label;
            float unitsPerMeter;  // 0 = Custom
        };
        static const UnitsPreset kUnitsPresets[] = {
            {"1 unit = 1 meter (metric)", 1.0f},
            {"1 unit = 10 cm (10 units/m)", 10.0f},
            {"1 unit = 1 cm (100 units/m)", 100.0f},
            {"1 unit = 10 m (0.1 units/m)", 0.1f},
            {"Custom...", 0.0f},
        };
        const int kUnitsPresetCount = (int)(sizeof(kUnitsPresets) / sizeof(kUnitsPresets[0]));
        {
            const char* items[kUnitsPresetCount];
            for (int i = 0; i < kUnitsPresetCount; ++i) items[i] = kUnitsPresets[i].label;
            if (ImGui::Combo("Scale", &newUnitsPreset_, items, kUnitsPresetCount) &&
                kUnitsPresets[newUnitsPreset_].unitsPerMeter > 0.0f)
                newUnitsPerMeter_ = kUnitsPresets[newUnitsPreset_].unitsPerMeter;
        }
        prefHelp(
            "How many world units a real-world meter is. Everything imported\n"
            "from reality (a model, a phone camera take) converts through it,\n"
            "and the FPP preset's player is person-sized at whatever you pick.\n"
            "The engine itself has no units - changeable later in Project >\n"
            "Preferences > World, but it never rescales existing content.\n"
            "See docs/world-scale.md.");
        if (kUnitsPresets[newUnitsPreset_].unitsPerMeter <= 0.0f) {
            ImGui::DragFloat("Units per meter", &newUnitsPerMeter_, 0.05f, 0.001f, 1000.0f,
                             "%.3f", ImGuiSliderFlags_Logarithmic);
            if (newUnitsPerMeter_ < 0.001f) newUnitsPerMeter_ = 0.001f;
        }

        ImGui::SeparatorText(newTerrain_ ? "Terrain (flat)" : "World size");
        ImGui::Checkbox("Create terrain", &newTerrain_);
        prefHelp(
            "On: the scene starts with a flat ground plane you can sculpt and\n"
            "paint (Terrain Editor).\n"
            "Off: no ground at all - nothing is drawn and the game has no floor\n"
            "either, so the player and the physics stand on the geometry you\n"
            "place and fall through the void everywhere else. For interiors,\n"
            "platformers and cutscene-only projects. The size below stays the\n"
            "world bounds every walker is clamped to, and the terrain can be\n"
            "created (or removed) later in the Terrain Editor.");
        ImGui::InputInt(newTerrain_ ? "Width (units)" : "World width (units)",
                        &newWidth_);
        ImGui::InputInt(newTerrain_ ? "Depth (units)" : "World depth (units)",
                        &newDepth_);
        if (newWidth_ < 1) newWidth_ = 1;
        if (newDepth_ < 1) newDepth_ = 1;
        if (newWidth_ > 4096) newWidth_ = 4096;
        if (newDepth_ > 4096) newDepth_ = 4096;
        if (newUnitsPerMeter_ != 1.0f)
            ImGui::TextDisabled("= %.1f x %.1f m", (float)newWidth_ / newUnitsPerMeter_,
                                (float)newDepth_ / newUnitsPerMeter_);

        ImGui::SeparatorText("Preset");
        const char* presetNames[kNewPresetCount];
        for (int i = 0; i < kNewPresetCount; ++i) presetNames[i] = kNewPresets[i].label;
        ImGui::Combo("Preset", &newTemplate_, presetNames, kNewPresetCount);
        prefHelp(
            "What the project starts as - and what its generated game sources\n"
            "are, permanently: the preset is fixed at creation and Project >\n"
            "Preferences only shows it. Those sources are user-ownable, so\n"
            "switching later would either overwrite your work or leave an\n"
            "owned file no longer matching what the project builds.\n"
            "- FPP: walk/look through the player's eyes.\n"
            "- Third person: camera on a boom behind the player; the avatar\n"
            "  is the Player object's own animated model (.glb/.fbx),\n"
            "  assigned in Properties - the rig works without one.\n"
            "- Empty: nothing in the scene and a camera that does not move\n"
            "  on its own - add a player, or drive it from your own code,\n"
            "  a Camera object or a cutscene.");

        ImGui::SeparatorText("AI support");
        ImGui::Checkbox("Claude Code", &newAiClaude_);
        ImGui::SameLine();
        ImGui::Checkbox("GitHub Copilot", &newAiCopilot_);
        prefHelp(
            "Copies assistant guides into the project (.claude/skills/ +\n"
            "CLAUDE.md, .github/copilot-instructions.md): how the project is\n"
            "structured, flow graphs, custom scripts and the editor's CLI -\n"
            "so an AI assistant opened in the project knows what it is doing.\n"
            "Can also be added later in Project > Preferences.");

        ImGui::TextDisabled("Creates: %s\\%s", newLocation_, newName_);
        ImGui::TextDisabled(
            newTerrain_ ? "Default scene \"main\" with a flat %d x %d terrain.%s"
                        : "Default scene \"main\", %d x %d world, no terrain.%s",
            newWidth_, newDepth_,
            newTemplate_ == 0   ? " Adds a player entity (FPP)."
            : newTemplate_ == 1 ? " Adds a player entity (third person)."
                                : "");
        // A player with no terrain has nothing to stand on until the first
        // floor is placed - say so here rather than let it be discovered by
        // falling through an empty world on the first build.
        if (!newTerrain_ && newTemplate_ != 2)
            ImGui::TextColored(ImVec4(0.9f, 0.75f, 0.35f, 1.0f),
                               "The player starts in mid-air - place a floor "
                               "(a Box, a model) under it.");
        // The build defaults a fresh project starts with - one line, because
        // nobody should have to discover why the FPS overlay is there or the
        // keyboard is not; the reasoning is in the tooltip.
        ImGui::TextDisabled("Build: debug + Live Link, keyboard & mouse off.");
        prefHelp(
            "A fresh project is set up for authoring, all three in Project >\n"
            "Preferences > Build:\n"
            "- Debug profile: the on-screen FPS / memory / profiler overlays\n"
            "  are available. Switch to release for the disc - it strips them.\n"
            "- Live Link: edit the running game (move an object in the editor\n"
            "  and it moves on the console). Debug builds only.\n"
            "- USB keyboard & mouse: off, so a pad game does not load drivers\n"
            "  it never uses. Turn it on for a keyboard/mouse game.");

        if (!newProjectError_.empty())
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", newProjectError_.c_str());

        ImGui::Separator();
        if (ImGui::Button("Create", ImVec2(scaled(120), 0))) {
            Project p;
            TerrainConfig t{newWidth_, newDepth_, newTerrain_};
            if (newTemplate_ < 0 || newTemplate_ >= kNewPresetCount) newTemplate_ = 0;
            const char* preset = kNewPresets[newTemplate_].preset;
            std::string err =
                project::create(p, newName_, newLocation_, t, preset, newUnitsPerMeter_);
            if (err.empty()) {
                if (newAiClaude_ || newAiCopilot_)
                    statusMessage_ =
                        aisupport::install(p.dir, newAiClaude_, newAiCopilot_);
                project_ = p;
                hasProject_ = true;
                applyProjectToViewport();
                attachProject();  // resets dirty + window title
                rememberRecentProject(project_.dir);
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
    project::ensureSplatmap(project_);  // weights track the same render grid
    const SceneData& sc = project_.active();
    // Scene-visual settings resolve project defaults + this scene's overrides.
    const ProjectSettings rs = project::resolvedSettings(project_, sc);
    viewport_.setProjectDir(project_.dir);
    const project::TerrainMaterial tm =
        project::resolveTerrainMaterial(project_, rs.terrainMaterial);
    // Stochastic base: preview the baked supertile (same pixels the build
    // bakes), tiled at 1/factor so the source keeps its world size.
    if (sc.terrainBaseStochastic && !tm.texture.empty()) {
        float sf = 1.0f;
        const std::string key = uploadStochPreview(tm.texture, sf);
        const float tile[2] = {tm.tile[0] / sf, tm.tile[1] / sf};
        viewport_.setTerrainMaterial(key.empty() ? tm.texture : key, tm.kd,
                                     tm.present, tile);
    } else {
        viewport_.setTerrainMaterial(tm.texture, tm.kd, tm.present, tm.tile);
    }
    viewport_.setTerrain(sc.terrain, project_.settings.terrainDetail, sc.heights, sc.hmW,
                         sc.hmD);
    // Macro ground variation rides the vertex shade - set it before the layer
    // meshes build so one rebuild covers both.
    viewport_.setTerrainTint(sc.terrainTintVariation, sc.terrainTintScale);
    // Painted terrain layers: push the resolved layer set + weights (empty
    // layers = the plain single-material terrain above).
    rebakeSplatPreview();
    viewport_.setSky(rs.skyColor, rs.skyTopColor, rs.skyDome, rs.zenithSize);
    viewport_.setUsableHighlight(rs.highlightUsable, rs.highlightColor);
    viewport_.setLighting(rs.lightDir, rs.ambient, rs.diffuse, rs.lightColor, rs.brightness);
    viewport_.setAmbientOcclusion(rs.aoEnabled, rs.aoStrength, rs.aoRadius);
    // Baked global illumination (docs/global-illumination.md): the viewport
    // shows what the console will, so it reads the SAME cache codegen and
    // texbake read. Reloaded only when the scene, the bake or the model
    // changes - gibake::load hashes the whole scene, which is not a
    // per-frame cost.
    if (giViewScene_ != project_.activeScene ||
        giViewSerial_ != modelEditSerial_ ||
        giViewVersion_ != giBaker_.version()) {
        giViewScene_ = project_.activeScene;
        giViewSerial_ = modelEditSerial_;
        giViewVersion_ = giBaker_.version();
        const gibake::Bake b = gibake::load(project_, project_.activeScene);
        viewport_.setGiProbes(b.valid ? b.probes : gibake::ProbeGrid());
        // The ground takes the baked terrain lightmap instead of the probes -
        // the same split the console makes (see Viewport::setGiTerrain).
        viewport_.setGiTerrain(b.valid ? b.terrain : aobake::AoImage());
    }
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

void App::drawTerrainWindow() {
    if (!showTerrainEditor_) return;
    if (!hasProject_) {
        showTerrainEditor_ = false;
        return;
    }
    ImGui::SetNextWindowSize(ImVec2(scaled(380), scaled(540)), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Terrain Editor", &showTerrainEditor_)) {
        ImGui::End();
        return;
    }

    SceneData& sc = project_.active();

    // Does this scene HAVE a terrain (docs/terrain.md)? This is the one control
    // that stays live with the terrain removed - everything below edits a
    // ground that would not exist. Removing keeps the heightmap, the paint and
    // the layers in the project, so creating it again brings the scene back
    // exactly as it was (and it is one undo step either way).
    {
        bool on = sc.terrain.enabled;
        if (ImGui::Checkbox("Terrain in this scene", &on)) {
            sc.terrain.enabled = on;
            if (!on) sculptMode_ = paintMode_ = false;
            applyProjectToViewport();
            commitChange();
            statusMessage_ = on ? "Created the terrain" : "Removed the terrain";
        }
        prefHelp(
            "Off removes the ground completely: nothing is drawn here and the\n"
            "generated game has no floor either - the player, the physics and\n"
            "the AI stand on the geometry you place and fall through the void\n"
            "everywhere else. The scene's size stays the world bounds every\n"
            "walker is clamped to. The heightmap and the paint are kept, so\n"
            "turning it back on restores the terrain you had.");
    }
    if (!sc.terrain.enabled) {
        ImGui::Separator();
        ImGui::TextWrapped(
            "This scene has no terrain. Sculpting, painting, the terrain "
            "material and the ground bakes (AO, GI, navigation) are all off; "
            "floors are whatever geometry you place.");
        ImGui::End();
        return;
    }
    const bool canPaint = !sc.terrainLayers.empty();
    if (!canPaint) paintMode_ = false;

    // --- Tool row: Sculpt / Paint, one brush in hand at a time. The same
    // toggles as the viewport toolbar (4 / 6) - shared state, never disagree.
    {
        const float bw =
            (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) *
            0.5f;
        auto toolButton = [&](const char* label, bool& mode, bool& other,
                              bool enabled) {
            ImGui::BeginDisabled(!enabled);
            if (mode)
                ImGui::PushStyleColor(
                    ImGuiCol_Button,
                    ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
            if (ImGui::Button(label, ImVec2(bw, scaled(28)))) {
                mode = !mode;
                if (mode) other = false;
            }
            if (mode) ImGui::PopStyleColor();
            ImGui::EndDisabled();
        };
        toolButton("Sculpt (4)", sculptMode_, paintMode_, true);
        ImGui::SameLine();
        toolButton("Paint (6)", paintMode_, sculptMode_, canPaint);
        if (!canPaint && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("Add a terrain layer below first");

        if (sculptMode_)
            ImGui::TextDisabled(
                "Drag on the terrain: LMB raise, Shift+LMB lower. [ ] resize.");
        else if (paintMode_)
            ImGui::TextDisabled(
                "Drag on the terrain: LMB paint, Shift+LMB erase. [ ] resize.");
        else
            ImGui::TextDisabled(
                "Pick a tool - or manage the layers and bake below.");
    }

    // --- Brush (the active tool's settings; radius is shared) ---
    ImGui::SeparatorText("Brush");
    if (sculptMode_ || paintMode_) {
        ImGui::SetNextItemWidth(scaled(200));
        ImGui::SliderFloat("Radius", &brushRadius_, 1.0f,
                           brushMaxRadius(project_), "%.1f",
                           ImGuiSliderFlags_Logarithmic);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Brush radius in world units, up to half the map."
                              "\n[ and ] resize it over the viewport.");
        if (sculptMode_) {
            ImGui::SetNextItemWidth(scaled(200));
            ImGui::SliderFloat("Strength", &brushStrength_, 0.01f,
                               sculptMaxStrength(project_), "%.2f",
                               ImGuiSliderFlags_Logarithmic);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(
                    "Units raised per frame at the brush center - the range\n"
                    "grows with the map, so large worlds sculpt fast too.\n"
                    "Flatten mode: level rate (values above 1 act as 1).");
            ImGui::Checkbox("Flatten to level", &sculptFlatten_);
            if (sculptFlatten_) {
                ImGui::SameLine();
                const float lvl = terrainDimOf(project_);
                ImGui::SetNextItemWidth(scaled(90));
                ImGui::DragFloat("##level", &flattenHeight_, 0.1f, -lvl, lvl,
                                 "%.1f");
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Target height (world units)");
            }
        } else {
            ImGui::SetNextItemWidth(scaled(200));
            ImGui::SliderFloat("Strength", &paintStrength_, 0.05f, 1.0f, "%.2f");
            ImGui::Checkbox("Erase (reveal layers below)", &paintErase_);
        }
    } else {
        ImGui::TextDisabled("Grab Sculpt or Paint to brush the terrain.");
    }

    // --- Layers ---
    // Shown as a Photoshop-style stack: the TOP row draws over everything
    // below it, new layers land on top, and the base sits at the bottom.
    // Storage order is unchanged (higher index = drawn later = higher in the
    // stack); only the presentation is reversed.
    ImGui::SeparatorText("Layers");

    int removeIdx = -1, moveIdx = -1, moveDir = 0;
    bool layersChanged = false;

    if (ImGui::SmallButton("+ Add layer")) {
        project::addTerrainLayer(project_, "Layer", "");
        paintLayer_ = (int)sc.terrainLayers.size() - 1;  // new = top of the stack
        // Adding a layer means you're about to paint it - put the brush in
        // hand (unless the sculpt tool is deliberately held).
        if (!sculptMode_) paintMode_ = true;
        layersChanged = true;
    }
    ImGui::SameLine();
    ImGui::TextDisabled("top layer paints over the ones below");

    for (int i = (int)sc.terrainLayers.size() - 1; i >= 0; --i) {
        ImGui::PushID(i);
        TerrainLayer& L = sc.terrainLayers[i];

        if (ImGui::RadioButton("##active", paintLayer_ == i)) paintLayer_ = i;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Paint this layer");
        ImGui::SameLine();

        char nameBuf[64];
        std::snprintf(nameBuf, sizeof(nameBuf), "%s", L.name.c_str());
        ImGui::SetNextItemWidth(scaled(90));
        if (ImGui::InputText("##name", nameBuf, sizeof(nameBuf)))
            L.name = nameBuf;
        if (ImGui::IsItemDeactivatedAfterEdit()) layersChanged = true;

        ImGui::SameLine();
        ImGui::SetNextItemWidth(scaled(120));
        if (drawTerrainMaterialCombo("##mat", L.material)) layersChanged = true;

        // Up = raise in the stack = drawn later = HIGHER storage index.
        ImGui::SameLine();
        ImGui::BeginDisabled(i == (int)sc.terrainLayers.size() - 1);
        if (ImGui::ArrowButton("##up", ImGuiDir_Up)) {
            moveIdx = i;
            moveDir = 1;
        }
        ImGui::EndDisabled();
        ImGui::SameLine(0.0f, scaled(2));
        ImGui::BeginDisabled(i == 0);
        if (ImGui::ArrowButton("##down", ImGuiDir_Down)) {
            moveIdx = i;
            moveDir = -1;
        }
        ImGui::EndDisabled();
        ImGui::SameLine(0.0f, scaled(6));
        if (ImGui::SmallButton("X")) removeIdx = i;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Remove layer");

        // Texture size: how large this layer's pattern appears on the ground
        // (bigger = fewer repeats). Only meaningful for a textured layer.
        ImGui::Indent(scaled(22));
        ImGui::SetNextItemWidth(scaled(110));
        if (ImGui::DragFloat("Size", &L.scale, 0.02f, 0.1f, 20.0f, "%.2fx"))
            splatPreviewDirty_ = true;  // live preview follows the drag
        if (ImGui::IsItemDeactivatedAfterEdit()) layersChanged = true;
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(
                "How large this layer's texture looks on the ground\n"
                "(bigger = larger pattern). No effect on a flat layer.");
        ImGui::SameLine(0.0f, scaled(12));
        const bool layerHasTex =
            !project::resolveTerrainMaterial(project_, L.material).texture.empty();
        ImGui::BeginDisabled(!layerHasTex);
        if (ImGui::Checkbox("Stochastic", &L.stochastic)) layersChanged = true;
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip(
                layerHasTex
                    ? "Break the tiled-grid repetition by baking a larger\n"
                      "non-repeating supertile at build (zero runtime cost).\n"
                      "Best on organic textures; leave off for bricks/tiles."
                    : "Pick a material with a texture first -\nstochastic tiling "
                      "scrambles the texture, so a\nflat color has nothing to work "
                      "on.");
        ImGui::Unindent(scaled(22));

        ImGui::PopID();
    }

    // The base is the bottom of the stack - everything above blends over it.
    // Edit its material right here: the scene's own when it overrides the
    // project default, otherwise the project default (so a single-scene
    // project just sets its terrain material without leaving this window).
    {
        std::string& baseMat = sc.overrides.terrainMat
                                   ? sc.settings.terrainMaterial
                                   : project_.settings.terrainMaterial;
        ImGui::BulletText("Base");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(scaled(150));
        if (drawTerrainMaterialCombo("##basemat", baseMat)) {
            commitChange();
            applyProjectToViewport();
        }
        const bool baseHasTex =
            !project::resolveTerrainMaterial(project_, baseMat).texture.empty();
        ImGui::Indent(scaled(22));
        ImGui::BeginDisabled(!baseHasTex);
        if (ImGui::Checkbox("Stochastic tiling##base", &sc.terrainBaseStochastic)) {
            commitChange();
            applyProjectToViewport();  // base texture path lives on setTerrainMaterial
        }
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip(
                baseHasTex
                    ? "Break the tiled-grid repetition of the base texture by "
                      "baking\na larger non-repeating supertile at build (zero "
                      "runtime cost).\nBest on organic textures; leave off for "
                      "bricks/tiles."
                    : "Assign a base material with a texture first -\nstochastic "
                      "tiling scrambles the texture, so a flat\ncolor has nothing "
                      "to work on.");
        ImGui::Unindent(scaled(22));
    }

    // Apply deferred structural edits (one at a time), then commit + refresh.
    if (removeIdx >= 0) {
        project::removeTerrainLayer(project_, removeIdx);
        if (paintLayer_ >= (int)sc.terrainLayers.size())
            paintLayer_ = (int)sc.terrainLayers.size() - 1;
        if (paintLayer_ < 0) paintLayer_ = 0;
        layersChanged = true;
    } else if (moveIdx >= 0) {
        project::moveTerrainLayer(project_, moveIdx, moveDir);
        if (paintLayer_ == moveIdx) paintLayer_ = moveIdx + moveDir;
        else if (paintLayer_ == moveIdx + moveDir) paintLayer_ = moveIdx;
        layersChanged = true;
    }

    // --- Macro ground variation ---
    ImGui::SeparatorText("Variation");
    {
        ImGui::SetNextItemWidth(scaled(200));
        if (ImGui::SliderFloat("Amount", &sc.terrainTintVariation, 0.0f, 1.0f,
                               "%.2f"))
            viewport_.setTerrainTint(sc.terrainTintVariation, sc.terrainTintScale);
        if (ImGui::IsItemDeactivatedAfterEdit()) commitChange();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip(
                "Large soft patches of lighter/darker ground - breaks the\n"
                "uniform 'carpet' look at zero runtime cost. Tints the base\n"
                "and the painted layers together, like real ground lighting.");
        ImGui::SetNextItemWidth(scaled(200));
        if (ImGui::SliderFloat("Patch size", &sc.terrainTintScale, 4.0f, 200.0f,
                               "%.0f units", ImGuiSliderFlags_Logarithmic))
            viewport_.setTerrainTint(sc.terrainTintVariation, sc.terrainTintScale);
        if (ImGui::IsItemDeactivatedAfterEdit()) commitChange();
    }

    // --- How it ships ---
    ImGui::SeparatorText("Quality");
    {
        int vw = 0, vd = 0;
        project::terrainGridDims(project_, vw, vd);
        ImGui::TextWrapped(
            "Layer textures stay tiled at full resolution; the blend follows "
            "the terrain grid (%dx%d vertices - raise Terrain detail in "
            "Preferences for finer blend edges). Painted chunks draw one extra "
            "pass per layer on the PS2.",
            vw, vd);
    }

    if (layersChanged) {
        commitChange();
        if (sc.terrainLayers.empty())
            applyProjectToViewport();  // revert the preview to the terrain material
        else
            splatPreviewDirty_ = true;
    }

    ImGui::End();
}

// Generates a stochastic supertile for a terrain texture and uploads it into
// the viewport's texture cache under a synthetic key, returning that key (and
// the tiling factor). Same pixels the build bakes (stochtile is the shared
// source of truth), so the preview matches the game. "" on failure.
std::string App::uploadStochPreview(const std::string& srcRel, float& factor) {
    factor = 1.0f;
    if (srcRel.empty()) return "";
    const std::string full =
        (std::filesystem::path(project_.dir) / srcRel).string();
    int w = 0, h = 0, f = 1;
    std::vector<uint8_t> px = stochtile::generate(full, srcRel, w, h, f);
    if (px.empty()) return "";
    factor = (float)f;
    const std::string key = "@stoch:" + srcRel;
    viewport_.updateTexturePixels(key, w, h, px.data());
    return key;
}

void App::rebakeSplatPreview() {
    // Two-pass splatting preview: hand the viewport each layer resolved to
    // what it draws (texture / tint / tiling incl. the layer Size and any
    // stochastic supertile) plus the per-vertex weights - the same inputs the
    // PS2 runtime gets from codegen.
    const SceneData& sc = project_.active();
    std::vector<Viewport::TerrainLayerDraw> draws;
    draws.reserve(sc.terrainLayers.size());
    for (const TerrainLayer& tl : sc.terrainLayers) {
        Viewport::TerrainLayerDraw d;
        const project::TerrainMaterial m =
            project::resolveTerrainMaterial(project_, tl.material);
        const float s = tl.scale > 0.0f ? tl.scale : 1.0f;
        if (m.present) {
            for (int i = 0; i < 3; ++i) d.kd[i] = m.kd[i];
            float sf = 1.0f;
            std::string key;
            if (tl.stochastic && !m.texture.empty())
                key = uploadStochPreview(m.texture, sf);
            d.texture = key.empty() ? m.texture : key;
            d.tile[0] = m.tile[0] / (s * sf);
            d.tile[1] = m.tile[1] / (s * sf);
        }  // else: the neutral-gray defaults (codegen emits the same)
        draws.push_back(std::move(d));
    }
    viewport_.setTerrainLayers(draws, sc.splat);
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
    // Read-only on purpose: the preset is picked once, in New Project. It
    // decides which game-template sources are generated, and those are
    // user-ownable - switching here would either overwrite work or leave an
    // owned source no longer matching what the project builds.
    ImGui::BeginDisabled();
    {
        int shown = presetIndexOf(project_.gameTemplate);
        const char* names[kNewPresetCount];
        for (int i = 0; i < kNewPresetCount; ++i) names[i] = kNewPresets[i].label;
        ImGui::Combo("Preset", &shown, names, kNewPresetCount);
    }
    ImGui::EndDisabled();
    prefHelp(
        "Fixed when the project was created and not changeable here: it picks\n"
        "the generated game sources (files with the editor marker), which you\n"
        "may have taken ownership of. Start a new project to use another\n"
        "preset. The player's own camera settings stay editable - select the\n"
        "Player object and see Properties.");

    ImGui::SeparatorText("Build");
    int videoSys = prefSettings_.videoSystem == "pal"    ? 2
                   : prefSettings_.videoSystem == "ntsc" ? 1
                                                         : 0;
    const char* videoSysNames[] = {"Auto (console region)", "NTSC (60 Hz)",
                                   "PAL (50 Hz)"};
    if (ImGui::Combo("Target system", &videoSys, videoSysNames, 3))
        prefSettings_.videoSystem = videoSys == 2 ? "pal" : videoSys == 1 ? "ntsc" : "auto";
    prefHelp(
        "Video signal of the built game (also on exported ISOs). Auto follows\n"
        "the console region. Game speed is normalized - PAL (50 Hz) and NTSC\n"
        "(60 Hz) play at the same wall-clock speed.");
    int dispMode = prefSettings_.displayMode == "pal576"             ? 4
                   : prefSettings_.displayMode == "1080i"            ? 3
                   : prefSettings_.displayMode == "progressive"      ? 2
                   : prefSettings_.displayMode == "interlaced-field" ? 1
                                                                     : 0;
    const char* dispModeNames[] = {"Interlaced (480i/576i)",
                                   "Interlaced, field rendering (480i/576i)",
                                   "Progressive scan (480p)", "1080i (HD)",
                                   "PAL 576i (full-height, 50 Hz)"};
    if (ImGui::Combo("Display mode", &dispMode, dispModeNames, 5))
        prefSettings_.displayMode = dispMode == 4   ? "pal576"
                                    : dispMode == 3 ? "1080i"
                                    : dispMode == 2 ? "progressive"
                                    : dispMode == 1 ? "interlaced-field"
                                                    : "interlaced";
    prefHelp(
        "Scan mode of the built game. Interlaced is the stock TV signal and\n"
        "follows Target system; it renders full 512x448 frames and each TV\n"
        "field shows half the lines of the newest one. Field rendering\n"
        "sends the same 480i/576i signal but renders a fresh half-height\n"
        "(512x224) image for EVERY field - 50/60 distinct pictures per\n"
        "second at full speed, for about half the fill and VRAM cost\n"
        "(slightly softer static picture). Progressive (flicker-free 480p)\n"
        "and 1080i always run at 60 Hz and need component (YPbPr) cables on\n"
        "a real console - PCSX2 displays every mode. 1080i renders a\n"
        "448x540 frame (sharper vertically) and leaves less VRAM for\n"
        "textures. PAL 576i is the full-height PAL frame (true 576i: a\n"
        "512x512 render, 14% more lines than the NTSC-sized picture) -\n"
        "always a 50 Hz PAL signal regardless of Target system, and it\n"
        "also leaves less VRAM for textures. All can also be switched at\n"
        "runtime with the Set Display Mode flow node, which shows a\n"
        "keep-or-revert prompt with an automatic rollback.");
    if (prefSettings_.displayMode == "interlaced") {
        int palPic = prefSettings_.palFullHeight ? 1 : 0;
        const char* palPicNames[] = {"Letterbox (NTSC-size picture)",
                                     "Full-height 576i"};
        if (ImGui::Combo("PAL picture", &palPic, palPicNames, 2))
            prefSettings_.palFullHeight = palPic == 1;
        prefHelp(
            "How the region-following interlaced mode looks on a PAL\n"
            "console (or with Target system forced to PAL): Letterbox\n"
            "keeps the NTSC-size 448-line picture in the 576i raster (the\n"
            "classic port look), Full-height boots the true 512-line PAL\n"
            "frame (PAL 576i - costs ~380 KB of GS VRAM). NTSC consoles\n"
            "always get 480i. A menu display row's DEFAULT option maps to\n"
            "whatever this resolves to on the player's console. (Field\n"
            "rendering has no full-height variant yet.)");
    }
    ImGui::Checkbox("Widescreen (16:9)", &prefSettings_.widescreen);
    prefHelp(
        "Widens the projection so proportions are correct on a 16:9 TV\n"
        "(anamorphic - on a 4:3 set the picture looks squeezed). In 1080i\n"
        "the picture also fills more of the screen. HUD sprites stretch\n"
        "with the screen. Runtime switch: the Set Widescreen flow node.");
    int profile = prefSettings_.buildProfile == "debug" ? 1 : 0;
    const char* profileNames[] = {"Release", "Debug"};
    if (ImGui::Combo("Profile", &profile, profileNames, 2))
        prefSettings_.buildProfile = profile == 1 ? "debug" : "release";
    ImGui::Checkbox("Keyboard && mouse controls", &prefSettings_.keyboardMouse);
    prefHelp(
        "The game loads the USB keyboard/mouse drivers: WASD walks, the\n"
        "mouse looks, E uses, Space jumps, Esc pauses, arrows + Enter drive\n"
        "menus (bindings live in inc/controls.hpp). Works in PCSX2 (the\n"
        "editor sets its emulated USB devices automatically) and with real\n"
        "USB devices on a console. Skipped on ps2link deploys.");
    ImGui::BeginDisabled(!prefSettings_.keyboardMouse);
    ImGui::Indent(scaled(16));
    ImGui::Checkbox("Also over ps2link", &prefSettings_.keyboardMousePs2Link);
    prefHelp(
        "Keeps keyboard/mouse working on a Run on PS2 (ps2link) deploy. The\n"
        "console runs the TyraX ps2link (tools/ps2link - its boot screen says\n"
        "so), which bakes usbd + ps2kbd + ps2mouse into its own boot, and the\n"
        "engine reuses that resident stack instead of loading its own. The\n"
        "driver logs show up live in Output / ps2client. Untick only if you\n"
        "deliberately boot a stock ps2link, which has no USB stack to reuse:\n"
        "the drivers then just report \"not ready\". See docs/ps2link-setup.md\n"
        "and docs/keyboard-mouse.md.");
    ImGui::Unindent(scaled(16));
    ImGui::EndDisabled();
    ImGui::Checkbox("Disable VSync (experimental)", &prefSettings_.disableVsync);
    prefHelp(
        "Skips the vsync wait before the buffer flip. The frame rate becomes\n"
        "continuous instead of snapping between 50 and 25 (PAL), at the cost\n"
        "of screen tearing. Gameplay speed is unaffected either way.");
    ImGui::BeginDisabled(profile == 0);
    ImGui::Checkbox("Show FPS", &prefSettings_.showFps);
    ImGui::Checkbox("Show memory usage", &prefSettings_.showMemory);
    ImGui::Checkbox("Show frame profiler", &prefSettings_.showProfiler);
    ImGui::EndDisabled();
    prefHelp(
        "Debug-profile overlays drawn in the top-left corner of the game:\n"
        "frames per second, free EE RAM, and a per-phase EE-time breakdown\n"
        "(whole frame / scene / usable-highlight / particles, avg ms over\n"
        "~1s). Stripped from release builds.");
    ImGui::BeginDisabled(profile == 0);
    ImGui::Checkbox("Show areas", &prefSettings_.showAreas);
    ImGui::EndDisabled();
    prefHelp(
        "Draws every Area object (docs/areas.md) in the GAME as a wireframe\n"
        "box, the way the editor viewport shows it. Areas have no geometry on\n"
        "the console by design, which is exactly why \"why did that layer not\n"
        "unload\" or \"why is that crate not reflecting\" is hard to see - this\n"
        "puts the volume back on screen. Stripped from release builds.");
    ImGui::BeginDisabled(profile == 0);
    ImGui::Checkbox("Live Link", &prefSettings_.liveLink);
    ImGui::EndDisabled();
    ImGui::BeginDisabled(profile == 0);
    ImGui::Checkbox("Live Debugger", &prefSettings_.liveDebug);
    ImGui::EndDisabled();
    prefHelp(
        "Debug builds report what their flow graphs run - every trigger and\n"
        "action, the flow variables, the save values - so the editor can show\n"
        "the graph executing live, set breakpoints, stop and step the game,\n"
        "and fire a trigger on demand (docs/live-debugger.md). Costs a\n"
        "counter bump per fired node plus one small file write every few\n"
        "frames; release builds carry none of it. Tools > Debugger (F9).");
    ImGui::BeginDisabled(profile == 0);
    ImGui::Checkbox("Live Logic", &prefSettings_.liveLogic);
    ImGui::EndDisabled();
    ImGui::BeginDisabled(profile == 0);
    ImGui::Checkbox("Time machine", &prefSettings_.timeMachine);
    ImGui::EndDisabled();
    prefHelp(
        "The game captures everything it mutates - object transforms,\n"
        "physics, animation, flow variables, save values, where the player\n"
        "stands - a few times a second, and the editor keeps a history of\n"
        "those captures in memory. The Debugger's Rewind tab puts the RUNNING\n"
        "game back into any of them, with no rebuild and no reboot; with Live\n"
        "Logic on you can fix a graph first and watch the new logic play out\n"
        "on the situation that just broke. Costs one small file written next\n"
        "to the ELF every few frames, and nothing at all in a release build.\n"
        "See docs/time-machine.md.");
    ImGui::BeginDisabled(profile == 0);
    ImGui::Checkbox("Remote Pad", &prefSettings_.remotePad);
    ImGui::EndDisabled();
    prefHelp(
        "Lets the EDITOR hold the controller: Tools > Remote Pad draws a pad\n"
        "you can click (or drive with the editor's own keyboard), and the game\n"
        "reads it out of one small file next to the ELF. Nothing needs the\n"
        "keyboard focus, so PCSX2 can sit in the background - and the same\n"
        "channel is scriptable from the command line\n"
        "(tyrax-editor --pad <project> \"hold up; wait 2\"), which is what makes\n"
        "an unattended input test possible. Works on real hardware over\n"
        "ps2link too (polled less often - it is a network round-trip there).\n"
        "Release builds carry none of it. See docs/remote-pad.md.");
    ImGui::BeginDisabled(profile == 0);
    ImGui::Checkbox("EE crash handler", &prefSettings_.eeCrashHandler);
    ImGui::EndDisabled();
    prefHelp(
        "A real CPU exception (bad pointer, address error, reserved\n"
        ""
        "instruction) is not a TYRAX assertion: nothing prints it and the game\n"
        ""
        "just stops. With this on, a debug build installs the engine's crash\n"
        ""
        "handler, which writes bin/crash.txt - decoded cause, registers,\n"
        ""
        "backtrace - and the editor resolves those addresses to functions and\n"
        ""
        "source lines. The crash also takes the screen, so a dead game says so\n"
        ""
        "instead of looking like a hang. Debug builds only - release carries\n"
        ""
        "none of it. Off by default; note PCSX2 cannot produce EE exceptions\n"
        ""
        "at all, so a report only ever appears on a real console.\n"
        ""
        "See docs/devkit.md.");
    prefHelp(
        "Debug builds carry a small flow-graph interpreter, so editing a graph\n"
        "changes the RUNNING game with no rebuild: the editor compiles the\n"
        "graph itself and streams the instructions next to the ELF\n"
        "(docs/live-logic.md). Graphs using nodes the interpreter does not\n"
        "implement (audio, AI, animation, spawning, runtime text) still need a\n"
        "build - the Debugger's Logic tab names them. Release builds compile\n"
        "every graph to native C++ and carry no interpreter.");
    prefHelp(
        "Debug builds poll livelink.bin next to the ELF so the editor can\n"
        "stream scene edits into the running game (docs/live-link.md). Turn\n"
        "off if you don't want your game patched from outside; release\n"
        "builds never carry the poller. Also toggled by the LIVE chip in\n"
        "the toolbar and Build > Live Link.");

    ImGui::SeparatorText("World");
    ImGui::DragFloat("Units per meter", &prefSettings_.unitsPerMeter, 0.05f, 0.001f,
                     1000.0f, "%.3f", ImGuiSliderFlags_Logarithmic);
    if (prefSettings_.unitsPerMeter < 0.001f) prefSettings_.unitsPerMeter = 0.001f;
    prefHelp(
        "How big a real-world meter is in this project's units. The engine\n"
        "has no opinion about units, but everything imported from reality\n"
        "does: a phone camera take records meters and a model carries them\n"
        "too. Set this to what your own content already uses and imports\n"
        "land at the right size instead of needing a guessed scale every\n"
        "time. 1.0 = one unit is one meter (nothing changes). It is an\n"
        "authoring reference only - it never reaches the game, and setting\n"
        "it does not move anything already in a scene.\n"
        "See docs/world-scale.md.");
    {
        // The same numbers in meters, which is what makes an inconsistent
        // project visible: a metric gravity next to a walk speed the size of
        // a highway means the world was built at a different scale than the
        // settings assume.
        const float ups = prefSettings_.unitsPerMeter;
        ImGui::TextDisabled("At this scale:");
        ImGui::SameLine();
        ImGui::TextDisabled("eye %.2f m, walk %.1f m/s, gravity %.1f m/s^2",
                            prefSettings_.eyeHeight / ups,
                            prefSettings_.walkSpeed * 50.0f / ups,
                            prefSettings_.gravity / ups);
        prefHelp(
            "Walk speed is units per 1/50 s, so 0.4 means 20 units/s.\n"
            "For reference: a person is ~1.7 m tall, walks at 1.4 m/s,\n"
            "sprints at ~6 m/s, and gravity is 9.81 m/s^2.");
    }

    ImGui::SeparatorText("Terrain");
    ImGui::InputInt("Width (units)", &prefTerrain_.width);
    ImGui::InputInt("Depth (units)", &prefTerrain_.depth);
    prefTerrain_.width = prefTerrain_.width < 1 ? 1 : prefTerrain_.width > 4096 ? 4096
                                                                                : prefTerrain_.width;
    prefTerrain_.depth = prefTerrain_.depth < 1 ? 1 : prefTerrain_.depth > 4096 ? 4096
                                                                                : prefTerrain_.depth;
    if (prefSettings_.unitsPerMeter != 1.0f)
        ImGui::TextDisabled("= %.1f x %.1f m",
                            (float)prefTerrain_.width / prefSettings_.unitsPerMeter,
                            (float)prefTerrain_.depth / prefSettings_.unitsPerMeter);
    ImGui::SliderInt("Detail (max grid cells)", &prefSettings_.terrainDetail, 4, 512);
    prefHelp("More cells = smaller triangles = fewer clipping artifacts,\n"
             "but more geometry for the PS2 to push.");

    ImGui::DragFloat("View distance", &prefSettings_.terrainViewDistance, 1.0f, 0.0f,
                     2000.0f,
                     prefSettings_.terrainViewDistance > 0.0f ? "%.0f units"
                                                              : "off (whole map)");
    if (prefSettings_.terrainViewDistance < 0.0f)
        prefSettings_.terrainViewDistance = 0.0f;
    prefHelp(
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
    int clipMode = prefSettings_.clipping == "fast"      ? 2
                   : prefSettings_.clipping == "precise" ? 1
                                                         : 0;
    const char* clipNames[] = {
        "Precise clipping on VU1 (no holes, no EE cost - default)",
        "Precise clipping on EE (legacy; costs EE time)",
        "Fast culling (fastest; big near triangles may vanish)"};
    if (ImGui::Combo("Triangles", &clipMode, clipNames, 3))
        prefSettings_.clipping =
            clipMode == 2 ? "fast" : clipMode == 1 ? "precise" : "vu1";

    // Animation frame rate. glTF/FBX carry keyframe times in seconds and no
    // fps at all, so a clip exported from a scene running at one rate but
    // authored for another is silently the wrong length. These two numbers
    // are the ratio the build applies to every clip.
    {
        ImGui::SetNextItemWidth(scaled(90));
        ImGui::DragFloat("##animSrcFps", &prefSettings_.animSourceFps, 0.25f,
                         1.0f, 240.0f, "%.0f fps");
        ImGui::SameLine();
        ImGui::TextUnformatted("exported ->");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(scaled(90));
        ImGui::DragFloat("##animPlayFps", &prefSettings_.animPlayFps, 0.25f,
                         1.0f, 240.0f, "%.0f fps");
        ImGui::SameLine();
        ImGui::TextUnformatted("Animation fps");
        prefSettings_.animSourceFps =
            std::clamp(prefSettings_.animSourceFps, 1.0f, 240.0f);
        prefSettings_.animPlayFps =
            std::clamp(prefSettings_.animPlayFps, 1.0f, 240.0f);
        const float ratio =
            prefSettings_.animPlayFps / prefSettings_.animSourceFps;
        if (std::fabs(ratio - 1.0f) > 0.001f)
            ImGui::TextDisabled("    every clip plays %.3fx %s", ratio,
                                ratio > 1.0f ? "faster" : "slower");
        else
            ImGui::TextDisabled("    clips play at their authored length");
        prefHelp(
            "glTF and FBX store keyframe times in SECONDS, never an fps - so\n"
            "a clip animated for 30 fps but exported from a 24 fps Blender\n"
            "scene arrives 25% too long and plays visibly too slow.\n"
            "Left: the fps the clips were exported at. Right: the fps they\n"
            "should play at. The build scales every clip's keyframe times by\n"
            "the ratio; the source files are never modified. Equal values\n"
            "(the default) change nothing. Per-clip overrides live in\n"
            "Tools > Animation Editor.");
    }

    ImGui::DragFloat("Animation LOD distance", &prefSettings_.animLodDistance,
                     0.5f, 0.0f, 2000.0f,
                     prefSettings_.animLodDistance > 0.0f ? "%.0f units" : "off");
    if (prefSettings_.animLodDistance < 0.0f) prefSettings_.animLodDistance = 0.0f;
    prefHelp(
        "Animated models farther than this refresh their pose every 2nd frame\n"
        "(every 4th beyond twice the distance). Playback time is unaffected.\n"
        "Cuts the per-instance EE cost of distant animated crowds.");

    ImGui::DragFloat("Mesh LOD distance", &prefSettings_.meshLodDistance,
                     0.5f, 0.0f, 2000.0f,
                     prefSettings_.meshLodDistance > 0.0f ? "%.0f units" : "off");
    if (prefSettings_.meshLodDistance < 0.0f) prefSettings_.meshLodDistance = 0.0f;
    prefHelp(
        "The build bakes ~50% and ~25%-vertex variants of every model -\n"
        "animated ones into the .tskl, static ones into the .tmdl; objects\n"
        "farther than this render the reduced meshes. A static model can use\n"
        "your own LOD meshes instead (the Asset Browser's LOD... button).\n"
        "Costs RAM and file size; the editor viewport shows the full mesh.");

    ImGui::Checkbox("Reflection probe: aim along the reflected ray",
                    &prefSettings_.envProbeReflected);
    prefHelp(
        "Dynamic reflections (@sky materials): instead of the classic\n"
        "level-forward aim, a ray from the camera hits the reflective\n"
        "object under the crosshair and the probe renders from the hit\n"
        "point along the REFLECTED ray (smoothed) - what that surface\n"
        "actually mirrors. Best on large curved chrome at mid distance;\n"
        "the single shared probe still approximates every other surface.");

    ImGui::Checkbox("Static object batching", &prefSettings_.staticBatching);
    prefHelp(
        "Merges non-moving primitives sharing a material into combined\n"
        "draw bags at scene load - each separate object costs ~1 ms of\n"
        "fixed submit overhead per frame on real hardware, batches pay it\n"
        "once. Objects with physics, scripts, flow-graph references,\n"
        "save-state or a streaming layer always stay individual.");

    // Texture quantization - the PS2-native "compression" (palettized
    // PSMT8/PSMT4 textures). Applied at build time into .res-baked; per
    // model/material overrides live in the Asset Browser's inspector.
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
    prefHelp(
        "Quantized at build (sources in res/ stay untouched). Override per\n"
        "model/material in the Asset Browser - e.g. keep the hero's textures\n"
        "full color while everything else goes 4-bit.");

    ImGui::Checkbox("Texture atlasing", &prefSettings_.textureAtlas);
    prefHelp(
        "Packs small (<=128) clamp-safe material textures into shared 256x256\n"
        "pages at build: one GS VRAM allocation (+~8 KB overhead) per page\n"
        "instead of per texture, fewer texture switches. Conservative - tiled\n"
        "terrain textures, emitters, decals, sphere maps and textures whose\n"
        "model UVs leave 0..1 keep their own files. Palettized projects share\n"
        "one 256-color palette per page (the era-authentic trade). The boot\n"
        "log prints what was packed.");

    drawTerrainMaterialCombo("Terrain material", prefSettings_.terrainMaterial);
    prefHelp("The material's color tints the terrain; its texture (map_Kd),\n"
             "if any, tiles across it - set the tiling on the material's\n"
             "texture in the Material Editor. Import .mtl in Project > Assets.");

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
    prefHelp(
        "The NPC nav grid, baked at build time from the terrain slope and\n"
        "blocking objects (grid capped at 128x128 cells - big maps get\n"
        "bigger cells). Agent radius widens every obstacle so NPCs keep\n"
        "their distance from walls. Used by the AI flow nodes (Patrol /\n"
        "Chase / Flee); preview with View > Nav Mesh Overlay. Scenes whose\n"
        "graphs use no AI nodes carry no nav data at all.");

    ImGui::SeparatorText("Ambience (sky, lighting, fog)");
    if (ImGui::Button("Open Ambience Editor")) showAmbienceEditor_ = true;
    prefHelp(
        "Sky gradient, baked lighting and distance fog now live in presets.\n"
        "Author them in Tools > Ambience Editor; each scene picks a preset in\n"
        "Scene > Preferences (or uses the default).");

    ImGui::SeparatorText("Scenes");
    ImGui::Checkbox("Loading screen between scenes", &prefSettings_.loadingScreen);
    prefHelp(
        "Master switch: shows a loading screen while a scene loads (also at\n"
        "boot). Design them in Tools > Loading Screens - each scene picks one\n"
        "(Scene > Preferences), or a project default is used; with none, a\n"
        "built-in loading.png on black is shown.");
    if (ImGui::Button("Open Loading Screens editor")) {
        showLoadingEditor_ = true;
        ImGui::CloseCurrentPopup();
    }

    ImGui::SeparatorText("Shadows");
    ImGui::Checkbox("Blob shadows under moving objects", &prefSettings_.blobShadows);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip(
            "A soft dark quad on the terrain under the third-person avatar,\n"
            "animated models and physics objects, fading as they rise -\n"
            "grounds them visually for one quad each. Project-wide.");

    ImGui::SeparatorText("Usable objects");
    ImGui::Checkbox("Highlight usable objects", &prefSettings_.highlightUsable);
    prefHelp(
        "In-game outline around objects marked 'Usable' while the player is\n"
        "within the proximity distance. The viewport marks them with a wire box.");
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
        prefHelp(
            "Width = total rim size; steps = shells in the fade (1 = sharp edge).\n"
            "Opacity = transparency of the strongest shell (the rest fade from it).\n"
            "Draw over object = a colored glow ON the surface fading outward,\n"
            "instead of only a rim behind the silhouette.");
    }

    // Player defaults - shared by both player presets (the height is the eye in
    // FPP, the body in third person, the same way a Player object labels it).
    if (project_.hasPlayerTemplate()) {
        const bool third = project_.gameTemplate == "thirdperson";
        ImGui::SeparatorText(third ? "Third-person camera" : "FPP camera");
        ImGui::DragFloat(third ? "Body height" : "Eye height", &prefSettings_.eyeHeight,
                         0.05f, 0.2f, 50.0f, "%.2f");
        walkSpeedDrag("Walk speed", prefSettings_.walkSpeed,
                      prefSettings_.unitsPerMeter);
        ImGui::DragFloat("Look speed", &prefSettings_.lookSpeed, 0.05f, 0.1f, 5.0f, "%.2f");
    } else {
        ImGui::SeparatorText("Camera");
        ImGui::DragFloat("Orbit speed", &prefSettings_.orbitSpeed, 0.05f, 0.0f, 10.0f, "%.2f");
        prefHelp(
            "How fast the camera circles the terrain in a scene that has no\n"
            "Player object. 0 - what the Empty preset starts at - parks it at\n"
            "a fixed vantage point looking at the origin, so the project shows\n"
            "what you built instead of a turntable; drive the camera from a\n"
            "script, a Camera object or a cutscene. A scene WITH a Player\n"
            "object ignores this - the player owns the camera.");
    }

    ImGui::SeparatorText("Multiplayer");
    {
        int mpMode = prefSettings_.multiplayer == "shared"  ? 1
                     : prefSettings_.multiplayer == "split" ? 2
                                                            : 0;
        const char* mpNames[] = {"Off (single player)", "Shared screen",
                                 "Split screen (top / bottom)"};
        if (ImGui::Combo("Two players", &mpMode, mpNames, 3))
            prefSettings_.multiplayer =
                mpMode == 1 ? "shared" : mpMode == 2 ? "split" : "off";
        if (mpMode != 0) {
            ImGui::Checkbox("Player 2 joins with Start on pad 2",
                            &prefSettings_.p2JoinOnStart);
            prefHelp(
                "Player 2 exists in scenes that contain a SECOND Player object\n"
                "(the first is P1, the second P2). Shared screen frames both\n"
                "with one camera; split screen renders each player's own view.\n"
                "A menu Toggle bound to 'Player count' can also switch 1P/2P\n"
                "mid-game (Menu Editor > + Option block).");
        }
    }

    ImGui::SeparatorText("Input");
    ImGui::DragFloat("Sprint speed", &prefSettings_.sprintMultiplier, 0.02f, 1.0f,
                     4.0f, "%.2fx");
    prefHelp(
        "Walk-speed multiplier while the \"sprint\" input action is held\n"
        "(Tools > Input Map - pad R2 / Left Shift by default). 1.00x turns\n"
        "sprinting off without unbinding the button. A third-person avatar\n"
        "crosses its run threshold while sprinting, so it plays the run clip.");
    ImGui::SliderFloat("Left stick deadzone", &prefSettings_.stickDeadzoneL, 0.0f, 0.9f,
                       "%.2f");
    ImGui::SliderFloat("Right stick deadzone", &prefSettings_.stickDeadzoneR, 0.0f, 0.9f,
                       "%.2f");
    prefHelp(
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
    prefHelp(
        "Exponential is gentle near center and snappy at the edge (aiming);\n"
        "S-Curve eases in and out. Higher exponent = more pronounced.");

    ImGui::SeparatorText("Physics");
    ImGui::DragFloat("Gravity (units/s^2)", &prefSettings_.gravity, 0.1f, 0.0f, 100.0f,
                     "%.1f");
    if (project_.hasPlayerTemplate())
        ImGui::DragFloat("Jump speed (units/s)", &prefSettings_.jumpSpeed, 0.1f, 0.0f, 50.0f,
                         "%.1f");
    prefHelp("Objects with the 'Physics' flag fall; the player jumps with X.");

    ImGui::SeparatorText("AI support");
    {
        const bool haveClaude = aisupport::installed(project_.dir, "claude");
        const bool haveCopilot = aisupport::installed(project_.dir, "copilot");
        if (ImGui::Button(haveClaude ? "Refresh Claude Code files"
                                     : "Add Claude Code support"))
            statusMessage_ = aisupport::install(project_.dir, true, false);
        ImGui::SameLine();
        if (ImGui::Button(haveCopilot ? "Refresh Copilot files"
                                      : "Add Copilot support"))
            statusMessage_ = aisupport::install(project_.dir, false, true);
        prefHelp(
            "Copies assistant guides into the project (.claude/skills/ + CLAUDE.md\n"
            "for Claude Code, .github/copilot-instructions.md for Copilot): the\n"
            "project structure, flow-graph format, custom scripting and the\n"
            "editor's headless CLI. Installing again refreshes the files unless\n"
            "you took ownership (deleted their marker line). Applied immediately\n"
            "- these are files on disk, not project settings.");
        if (haveClaude || haveCopilot)
            ImGui::TextDisabled("Installed:%s%s", haveClaude ? " Claude Code" : "",
                                haveCopilot ? " Copilot" : "");
    }

    ImGui::Separator();
    ImGui::TextDisabled(
        "These are project-wide defaults. Scenes inherit them unless a\n"
        "category is overridden in Scene > Scene Preferences. The emulator\n"
        "path and dev-PS2 IP are machine-global - set them in Edit > Preferences.");
    if (ImGui::Button("OK", ImVec2(scaled(120), 0))) {
        // gameTemplate is deliberately NOT written back - the preset is fixed
        // at creation and this dialog only shows it.
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
        "stored outside the .tyra file (in editor.ini, next to the other machine "
        "settings).");

    // Appearance applies IMMEDIATELY and saves itself, unlike the staged text
    // fields below: a theme you cannot see until you press Save is a theme you
    // cannot choose. Same reasoning as the Navigation modal.
    ImGui::SeparatorText("Appearance");
    if (ImGui::BeginCombo("Theme", theme::info(theme_).label)) {
        for (int i = 0; i < (int)theme::Id::Count; ++i) {
            const theme::Info& ti = theme::info((theme::Id)i);
            if (ImGui::Selectable(ti.label, theme_ == (theme::Id)i))
                setTheme((theme::Id)i);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", ti.desc);
        }
        ImGui::EndCombo();
    }
    ImGui::TextDisabled("%s", theme::info(theme_).desc);
    ImGui::TextDisabled(
        "Interface font: %s (the first UI face found on this machine).\n"
        "Scale lives in View > Interface scale - currently %d%%%s.",
        uiFontLabel_.empty() ? "built-in bitmap font" : uiFontLabel_.c_str(),
        (int)std::lround(uiScaleApplied_ * 100.0f),
        uiScaleUser_ == 0.0f ? " (auto)" : "");

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

    ImGui::SeparatorText("Collaboration sessions");
    ImGui::InputText("Display name", prefDisplayName_, sizeof(prefDisplayName_));
    ImGui::TextDisabled(
        "The name other participants see in a live session. Leave empty to\n"
        "use your Windows user name.");
    ImGui::InputText("Joined-project cache", prefSessionCacheDir_,
                     sizeof(prefSessionCacheDir_));
    ImGui::SameLine();
    if (ImGui::SmallButton("Browse...##sesscache")) {
        const std::string dir = pickFolder();
        if (!dir.empty())
            snprintf(prefSessionCacheDir_, sizeof(prefSessionCacheDir_), "%s",
                     dir.c_str());
    }
    ImGui::TextDisabled(
        "Where projects you JOIN materialize (re-joins reuse it, so unchanged\n"
        "files never transfer twice). Leave empty for\n"
        "the editor config folder (remote-cache).");

    ImGui::SeparatorText("AI assistant");
    {
        const auto ids = aigen::backendIds();
        if (prefAiBackend_ < 0 || prefAiBackend_ >= (int)ids.size())
            prefAiBackend_ = 0;
        if (ImGui::BeginCombo("Backend", aigen::backendLabel(ids[prefAiBackend_]))) {
            for (int i = 0; i < (int)ids.size(); ++i)
                if (ImGui::Selectable(aigen::backendLabel(ids[i]),
                                      prefAiBackend_ == i))
                    prefAiBackend_ = i;
            ImGui::EndCombo();
        }
        // Model: a dropdown of the backend's known models plus "Custom..." -
        // picking Custom opens a free-text field, so brand-new models work
        // the day they ship. "" = the backend's default model.
        const auto models = aigen::modelPresets(ids[prefAiBackend_]);
        auto modelLabel = [](const char* m) {
            return *m ? m : "Backend default";
        };
        // A staged model the list doesn't know (hand-typed earlier, or the
        // backend just changed) can only be shown as Custom.
        bool listed = false;
        for (const char* m : models) listed |= (prefAiModel_ == std::string(m));
        if (!listed) prefAiCustomModel_ = true;
        if (ImGui::BeginCombo("Model", prefAiCustomModel_
                                           ? "Custom..."
                                           : modelLabel(prefAiModel_))) {
            for (const char* m : models) {
                const bool sel =
                    !prefAiCustomModel_ && prefAiModel_ == std::string(m);
                if (ImGui::Selectable(modelLabel(m), sel)) {
                    snprintf(prefAiModel_, sizeof(prefAiModel_), "%s", m);
                    prefAiCustomModel_ = false;
                }
            }
            if (ImGui::Selectable("Custom...", prefAiCustomModel_))
                prefAiCustomModel_ = true;
            ImGui::EndCombo();
        }
        if (prefAiCustomModel_)
            ImGui::InputTextWithHint("Model id", "as the backend expects it",
                                     prefAiModel_, sizeof(prefAiModel_));
        ImGui::Checkbox("Thinking", &prefAiThinking_);
        ImGui::TextDisabled(
            "Backend used by Flow Graph > Generate with AI (and the --ai-graph\n"
            "CLI). Claude CLI needs 'claude' on PATH, Copilot CLI 'copilot';\n"
            "the OpenAI API needs the OPENAI_API_KEY environment variable and\n"
            "uses curl. Thinking = extended reasoning where the backend\n"
            "supports it (slower, better on tricky logic).");
    }

    ImGui::Separator();
    if (ImGui::Button("Save", ImVec2(scaled(120), 0))) {
        globalEmulatorPath_ = prefEmulatorPath_;
        globalPs2Ip_ = prefPs2Ip_;
        globalDefaultProjectsDir_ = prefDefaultProjectsDir_;
        globalDisplayName_ = prefDisplayName_;
        globalSessionCacheDir_ = prefSessionCacheDir_;
        globalAi_.backend = aigen::backendIds()[prefAiBackend_];
        globalAi_.model = prefAiModel_;
        globalAi_.thinking = prefAiThinking_;
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

// "Generate with AI" (Flow Graph window). The Generator runs the backend on a
// worker thread; this modal polls it each frame - spinner + Cancel while busy,
// then the parsed graph is applied through commitChange (one undo step, so a
// bad generation is a Ctrl+Z away).
void App::drawAiGenerateModal() {
    if (openAiGeneratePopup_) {
        ImGui::OpenPopup("Generate Flow Graph with AI");
        openAiGeneratePopup_ = false;
    }

    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(scaled(620), 0), ImGuiCond_Appearing);
    if (!ImGui::BeginPopupModal("Generate Flow Graph with AI", nullptr,
                                ImGuiWindowFlags_AlwaysAutoResize))
        return;

    // The target can vanish mid-request (undo, scene switch, delete).
    const bool targetOk = hasProject_ && aiGenTargetObject_ >= 0 &&
                          aiGenTargetObject_ < (int)project_.objects().size();
    if (!targetOk) {
        if (aiGen_.busy()) aiGen_.cancel();
        aiGenInFlight_ = false;
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }
    SceneObject& owner = project_.objects()[aiGenTargetObject_];

    // Consume a finished request exactly once.
    if (aiGenInFlight_ && !aiGen_.busy()) {
        aiGenInFlight_ = false;
        if (aiGen_.state() == aigen::Generator::State::Success) {
            FlowGraph fg;
            aiGenWarnings_.clear();
            const std::string err =
                aigen::parseGraph(aiGen_.reply(), fg, &aiGenWarnings_);
            if (!err.empty()) {
                aiGenError_ = err;
            } else {
                // The reply is always the complete resulting graph - with an
                // existing graph the model saw it in the prompt and returned
                // the updated whole (edits, additions and fresh starts all
                // land the same way).
                owner.flowGraph = fg;
                commitChange();
                // Show the result: focus this object's graph and push the new
                // node positions into imnodes.
                flowGraphObject_ = aiGenTargetObject_;
                flowPositionsApplied_ = false;
                statusMessage_ = "AI graph: " + std::to_string(fg.nodes.size()) +
                                 " nodes, " + std::to_string(fg.links.size()) +
                                 " links -> " + owner.name +
                                 (aiGenWarnings_.empty()
                                      ? ""
                                      : "  [" + aiGenWarnings_ + "]");
                ImGui::CloseCurrentPopup();
                ImGui::EndPopup();
                return;
            }
        } else {
            aiGenError_ = aiGen_.error();
        }
    }

    const bool busy = aiGen_.busy();
    const bool hasGraph = !owner.flowGraph.empty();
    ImGui::Text("Graph of: %s", owner.name.c_str());
    ImGui::TextDisabled(
        "%s%s%s%s - change in Edit > Preferences > AI assistant.",
        aigen::backendLabel(globalAi_.backend),
        globalAi_.model.empty() ? "" : ", model ",
        globalAi_.model.c_str(), globalAi_.thinking ? ", thinking" : "");

    ImGui::BeginDisabled(busy);
    ImGui::TextUnformatted(hasGraph ? "Describe new logic or a change:"
                                    : "Describe the logic you want:");
    ImGui::InputTextMultiline("##aiprompt", aiPromptBuf_, sizeof(aiPromptBuf_),
                              ImVec2(-FLT_MIN, scaled(110)));
    if (hasGraph)
        ImGui::TextDisabled(
            "The AI sees the current graph and decides from your request\n"
            "whether to change it, extend it, or rebuild it.");
    ImGui::EndDisabled();

    if (!aiGenError_.empty() && !busy) {
        ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + scaled(590));
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s",
                           aiGenError_.c_str());
        ImGui::PopTextWrapPos();
    }

    ImGui::Separator();
    if (busy) {
        // Spinner: an arc revolving with time, next to the status text.
        const float r = scaled(8.0f), thick = scaled(3.0f);
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const float t = (float)ImGui::GetTime() * 6.0f;
        dl->PathArcTo(ImVec2(pos.x + r + thick, pos.y + r + thick), r, t,
                      t + 4.4f, 24);
        dl->PathStroke(ImGui::GetColorU32(ImGuiCol_ButtonHovered), 0, thick);
        ImGui::Dummy(ImVec2((r + thick) * 2.0f, (r + thick) * 2.0f));
        ImGui::SameLine();
        ImGui::Text("Generating...");
        ImGui::SameLine(0.0f, scaled(20.0f));
        if (ImGui::Button("Cancel", ImVec2(scaled(120), 0))) aiGen_.cancel();
    } else {
        const bool emptyPrompt = aiPromptBuf_[0] == '\0';
        ImGui::BeginDisabled(emptyPrompt);
        if (ImGui::Button("Generate", ImVec2(scaled(120), 0))) {
            aiGenError_.clear();
            aiGenWarnings_.clear();
            aiGen_.start(globalAi_,
                         aigen::systemPrompt(project_, aiGenTargetObject_,
                                             hasGraph ? &owner.flowGraph
                                                      : nullptr),
                         aiPromptBuf_);
            aiGenInFlight_ = true;
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Close", ImVec2(scaled(120), 0)))
            ImGui::CloseCurrentPopup();
    }
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
    // Wider than the others on purpose: this one crosses whole scenes, and 3x
    // was not enough on a large terrain.
    changed |= ImGui::SliderFloat("Forward pan", &nav_.dollySensitivity, 0.1f, 8.0f,
                                  "%.2fx");
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip(
            "Speed of the right+middle drag, which pans forward and back along\n"
            "the view direction (drag down = forward). It scales with zoom like\n"
            "the other moves, so this is a multiplier on top of that.");
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
    scenePrefStart_ = project_.startScene == project_.activeScene;
    openScenePrefsPopup_ = true;
}

void App::drawScenePreferencesModal() {
    if (openScenePrefsPopup_) {
        ImGui::OpenPopup("Scene Preferences");
        openScenePrefsPopup_ = false;
    }

    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    // Every category is drawn whether it is overridden or not, so the content is
    // ~1000 px tall: with AlwaysAutoResize the modal grew past the bottom of a
    // 900 px screen and OK/Cancel could not be reached at all. An explicit
    // height capped to the viewport keeps the footer on screen and lets the
    // categories scroll (the window is still user-resizable from its corner).
    ImGui::SetNextWindowSize(
        ImVec2(scaled(560),
               std::min(scaled(1040), ImGui::GetMainViewport()->WorkSize.y * 0.9f)),
        ImGuiCond_Appearing);

    if (!ImGui::BeginPopupModal("Scene Preferences", nullptr, 0))
        return;
    if (scenePrefScene_ < 0 || scenePrefScene_ >= (int)project_.scenes.size()) {
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }

    ProjectSettings& s = scenePrefSettings_;
    SceneOverrides& ov = scenePrefOverrides_;
    ImGui::Text("Scene: %s", project_.scenes[scenePrefScene_].name.c_str());
    prefHelp(
        "Each category inherits Project > Preferences until you tick\n"
        "\"Override project settings\" for it.");
    // -reserve for the footer (separator + button row) below.
    ImGui::BeginChild("##sceneprefs_scroll",
                      ImVec2(0, -(ImGui::GetFrameHeightWithSpacing() +
                                  ImGui::GetStyle().ItemSpacing.y * 2.0f)));

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

    // Which scene the game boots into. Project-wide (Project::startScene), but
    // it reads as a property OF a scene, so it is authored from here rather
    // than from Project > Preferences.
    ImGui::SeparatorText("Startup");
    {
        const bool isStart = project_.startScene == scenePrefScene_;
        const char* startName =
            project_.startScene >= 0 &&
                    project_.startScene < (int)project_.scenes.size()
                ? project_.scenes[project_.startScene].name.c_str()
                : "?";
        // Something must always be the start scene, so the tick can only be
        // MOVED here, never cleared - untickable once it is this scene.
        ImGui::BeginDisabled(isStart);
        ImGui::Checkbox("Boot into this scene", &scenePrefStart_);
        ImGui::EndDisabled();
        const std::string tip =
            isStart ? std::string(
                          "The game boots into this scene. Some scene always "
                          "has to, so the tick can only be MOVED: open another "
                          "scene's preferences and tick it there.")
                    : std::string("The game currently boots into \"") +
                          startName +
                          "\". Ticking this moves the start here on OK - the "
                          "Project panel marks it (start).";
        prefHelp(tip.c_str());
    }

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
        int clipMode = s.clipping == "fast"      ? 2
                       : s.clipping == "precise" ? 1
                                                 : 0;
        const char* clipNames[] = {
            "Precise clipping on VU1 (no holes, no EE cost - default)",
            "Precise clipping on EE (legacy; costs EE time)",
            "Fast culling (fastest; big near triangles may vanish)"};
        if (ImGui::Combo("Triangles", &clipMode, clipNames, 3))
            s.clipping =
                clipMode == 2 ? "fast" : clipMode == 1 ? "precise" : "vu1";
    });

    category("Terrain material", ov.terrainMat, [&] {
        drawTerrainMaterialCombo("Material", s.terrainMaterial);
    });

    category("Post effects", ov.postFx, [&] {
        ImGui::SliderFloat("Bloom", &s.bloom, 0.0f, 2.0f, "%.2f");
        ImGui::SliderFloat("Bloom threshold", &s.bloomThreshold, 0.0f, 1.0f,
                           s.bloomThreshold <= 0.0f ? "off - whole frame"
                                                    : "%.2f");
        ImGui::SliderFloat("Bloom spread", &s.bloomSpread, 0.0f, 1.0f, "%.2f");
        ImGui::SliderFloat("Film grain", &s.grain, 0.0f, 1.0f, "%.2f");
        ImGui::SliderFloat("DoF amount", &s.dofAmount, 0.0f, 1.0f, "%.2f");
        ImGui::DragFloat("DoF focus", &s.dofFocus, 0.5f, 0.5f, 500.0f, "%.1f");
        ImGui::DragFloat("DoF range", &s.dofRange, 0.5f, 0.1f, 500.0f, "%.1f");
        ImGui::SliderFloat("Lens flare", &s.flare, 0.0f, 1.0f, "%.2f");
        ImGui::SliderFloat("God rays", &s.godRays, 0.0f, 1.0f, "%.2f");
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

    ImGui::EndChild();
    ImGui::Separator();
    if (ImGui::Button("OK", ImVec2(scaled(120), 0))) {
        SceneData& sc = project_.scenes[scenePrefScene_];
        sc.settings = scenePrefSettings_;
        sc.overrides = scenePrefOverrides_;
        sc.ambiencePreset = scenePrefAmbience_;
        sc.loadingScreen = scenePrefLoading_;
        if (scenePrefStart_) project_.startScene = scenePrefScene_;
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
    const std::string err = openProjectAt(dir);
    if (!err.empty()) {
        runner_.clearLog();
        // Surface the error in the Output window via the runner log is hacky;
        // show a popup instead on next frame. Simple approach: message box.
        platform::errorBox("Open Project", err);
    }
}
