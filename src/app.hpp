#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <array>
#include <functional>
#include <future>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include <imgui.h>  // ImGuiStyle baseStyle_ member (UI scaling)

#include "aichat.hpp"  // the AI Assistant window's conversation + tool table
#include "aigen.hpp"
#include "audiopreview.hpp"
#include "camtake.hpp"
#include "dronegen.hpp"
#include "history.hpp"
#include "phonecam.hpp"
#include "gibake.hpp"
#include "litbake.hpp"
#include "shadowbake.hpp"
#include "procbake.hpp"
#include "matbake.hpp"
#include "modelao.hpp"  // the automatic model-AO baker + its per-asset plan
#include "menubake.hpp"  // CreditsLayout member (Credits Editor preview)
#include "menulayout.hpp"  // the Menu Preview's row geometry
#include "menustyle.hpp"  // the staged stylesheet the Style tab edits
#include "isoexport.hpp"
#include "elfsym.hpp"
#include "vucap.hpp"
#include "livedbg.hpp"
#include "livepad.hpp"
#include "livereplay.hpp"
#include "logview.hpp"  // LogView members (the Output / Debug severity split)
#include "uiscript.hpp"
#include "livetime.hpp"
#include "livelogic.hpp"
#include "placement.hpp"
#include "roadgen.hpp"  // roadgen::Surface - the test drive stands on roads
#include "prefab.hpp"
#include "vehbake.hpp"  // the import bake cached per vehicle definition
#include "modelimport.hpp"
#include "project.hpp"
#include "staticbatch.hpp"
#include "texatlas.hpp"
#include "vugen.hpp"  // vugen::Built - the VU panel keeps a live preview
#include "runner.hpp"
#include "session.hpp"
#include "theme.hpp"  // theme::Id theme_ member (interface theme)
#include "treegen.hpp"
#include "update.hpp"  // update::Release member (the update check's answer)
#include "savebake.hpp"
#include "viewport.hpp"

struct GLFWwindow;
namespace menuact {
struct Item;
}

// Viewport navigation preferences. These are a machine/muscle-memory property,
// not project data, so they live in the global editor config (editor.ini in
// the editor config dir), never in the per-project .tyra. See NavConfig persistence in
// app.cpp.
enum class NavScheme {
    Default = 0,  // tyra: LMB/RMB orbit, MMB pan
    Blender = 1,  // MMB orbit, Shift+MMB pan (LMB stays free for selection)
    Maya = 2,     // Alt+LMB orbit, Alt+MMB pan, Alt+RMB dolly
    Unity = 3,    // RMB orbit, MMB pan
};
enum class NavMoveKeys {
    WASD = 0,
    Arrows = 1,
};
struct NavConfig {
    NavScheme scheme = NavScheme::Default;
    NavMoveKeys moveKeys = NavMoveKeys::WASD;
    float orbitSensitivity = 1.0f;  // 0.2 .. 3.0, multiplies pixel deltas
    float panSensitivity = 1.0f;
    // The right+middle forward/back pan gets its own speed: it travels along the
    // VIEW direction, so a value that feels right for sideways panning is
    // usually too slow for crossing a scene or climbing to the sky.
    float dollySensitivity = 1.0f;
    float zoomSensitivity = 1.0f;
    bool invertX = false;  // reverse horizontal orbit direction
    bool invertY = false;  // reverse vertical orbit direction
    bool orbitAroundSelection = true;  // pivot snaps to the selected object
    // Trackpad navigation (no middle button): two-finger scroll PANS, Cmd/Ctrl
    // + scroll zooms, Option + scroll orbits. On by default on macOS, where
    // the built-in trackpad is the usual pointer; a mouse user turns it off in
    // View > Navigation controls to get wheel-zoom back.
#ifdef __APPLE__
    bool trackpad = true;
#else
    bool trackpad = false;
#endif
};

// One log panel's classified view of its text (docs/log-panels.md) - the state
// behind the *Output* and *Debug* windows' severity filter. logview::Line holds
// OFFSETS, so `text` is the buffer they index and has to be kept alive here
// rather than re-fetched per draw.
//
// The parse is incremental for one reason: a build appends lines continuously,
// and re-classifying a megabyte on every append measured far more than a frame's
// budget. `parsed`/`complete`/`state` are the resume point (see logview::parse);
// only a shrink or a rewrite - Clear, a new run recreating bin/log.txt, the
// Debug window's source combo - starts over.
struct LogView {
    std::string text;
    std::vector<logview::Line> lines;
    logview::State state;
    size_t parsed = 0;    // offset after the last COMPLETE line classified
    size_t complete = 0;  // lines.size() at that offset (the partial tail is dropped)
    // When each line ARRIVED, parallel to `lines`: seconds since local midnight,
    // -1 for no time (a continuation line, a blank one). A line carrying the
    // runner's own "HH:MM:SS " stamp keeps that time; a file source (the Debug
    // window) is stamped when logSetText first sees the line. A reset (Clear, a
    // new log, a moved 1 MB tail) restamps everything with the current time.
    std::vector<int> times;

    unsigned mask = logview::kAll;  // levels shown; persisted in editor.ini

    // The mouse selection over the coloured lines: an anchor and a caret, each a
    // line index into `lines` plus a byte offset into that line's MESSAGE (after
    // its stamp - the time column is not text). Line indices survive appends;
    // logSetText resets the pair when the text is rewritten.
    struct SelPos {
        size_t line = 0;
        size_t off = 0;
    };
    SelPos selA, selB;          // anchor, caret; empty when equal
    bool selDrag = false;       // a press is extending the caret (not a double-click)
    int focusFrame = -1;        // last frame the panel had focus (gates global Ctrl+C)

    // Derived from (lines, mask) - rebuilt when either changes, or when the font
    // size does (the width is measured in pixels).
    std::vector<int> visible;  // indices into `lines`
    int counts[logview::kLevelCount] = {0, 0, 0, 0};
    float width = 0.0f;        // widest visible line, px - the h-scroll range
    float widthFont = 0.0f;    // font size `width` was measured at
    bool dirty = true;
    size_t shown = 0;          // visible.size() last frame (autoscroll edge)
};

class App {
public:
    // initialProjectDir: optional project to open on startup (may be empty)
    int run(const std::string& initialProjectDir = "");

    /** Hands run() a UI script to drive itself with (docs/ui-scripting.md).
     * Called by --ui-script before run(); run() then returns non-zero if any
     * step failed, so a scripted GUI run gates a shell script like any test. */
    void setUiScript(const std::vector<uiscript::Step>& steps);

    /** The optional tool windows a layout can carry, which are also the keys
     * the AI Assistant's open_window tool accepts (docs/ai-chat.md). Public and
     * static because --chat-prompt prints the assistant's prompt without an
     * editor; defined next to the array it reads (app.cpp). */
    static std::vector<std::string> chatWindowKeys();

private:
    void drawUI();
    void drawMenuBar();
    // The logo at the top bar's left (mark + "TyraX" + caret) is one button,
    // "TyraX menu": it - or F10, or Alt tapped on its own - opens a popup whose
    // items are the ten menus (drawMainMenus) as submenus. shell_ui.cpp.
    void drawLogoMenu();
    void drawMainMenus();
    // drawMainMenus as data (menu_actions.hpp): every item it would draw is
    // appended to `out`; invoke >= 0 runs the item with that ordinal. Must be
    // called inside a window (the palette's): it opens an item-less child.
    void harvestMainMenus(std::vector<menuact::Item>* out, int invoke = -1);
    // Set by --ui-script (a step naming a top-level menu while the logo menu is
    // closed - devkit_ui.cpp) to open it on the next frame.
    bool logoMenuOpenRequest_ = false;
    bool logoMenuOpen_ = false;  // last frame's state, for the script shim
    bool logoMenuOpenOnPress_ = false;  // so a second click closes, not reopens
    bool altTapArmed_ = false;   // Alt went down with nothing else (yet)
    double altTapStart_ = 0.0;
    // Writes the editor's own framebuffer to <TYRAX_SHOT>/shotNN.png every
    // TYRAX_SHOT_EVERY seconds (default 2), and nothing at all when the
    // variable is unset. The only screenshot path that works under a compositor
    // that denies external capture - see the comment on the definition.
    void captureFrameIfRequested(int w, int h);
    // Icon toolbar drawn inline in the main menu bar (Save / Build / Run / Stop
    // + the live chips). Custom vector-drawn - the editor loads no icon font.
    void drawToolbar();
    // Merged title bar (platform.hpp, docs/editor-theme.md "Title bar"): the
    // room the top bar leaves for the OS at its left end (macOS traffic
    // lights) and for our own caption buttons at its right end (Windows), and
    // the per-frame part - drawing those buttons and telling the platform
    // layer where the bar's bare, draggable area is. All shell_ui.cpp.
    float titleBarInsetLeft() const;
    float titleBarInsetRight() const;
    void drawTitleBarControls();
    // The bar along the bottom: build state, live channels, scene counts.
    void drawStatusBar();
    // Make scene i the active one (clears the selection, reapplies terrain and
    // lighting). No-op for the active scene or an out-of-range index.
    void selectScene(int i);
    // The icon rail along the window's left edge: picks which page the
    // Project panel shows (leftPage_), plus AI Assistant / Project Preferences.
    void drawRail();
    // The app icon (resources/icon.png, the "Tx" mark) as a GL texture for the
    // top bar's logo button; created on first use, 0 if it cannot be decoded.
    unsigned int appIconTexture();
    unsigned int appIconTex_ = 0;
    bool appIconTried_ = false;
    // The Project panel's page: 0 Outliner, 1 Layers, 2 Assets, 3 Audio,
    // 4 Scripts (kLeftPageCount). View state, not saved.
    int leftPage_ = 0;
    // The side panels slid away: the Project panel (clicking the open page's
    // rail icon, Ctrl+B) and the Properties panel (Ctrl+Alt+B). A collapsed
    // panel is simply not submitted, so ImGui gives its dock node's width to
    // the neighbours and puts it back in the same place on reopen. Each window
    // layout carries its own pair (WindowLayout::projectCollapsed /
    // propertiesCollapsed), applied and captured with its open windows.
    bool leftCollapsed_ = false;
    bool rightCollapsed_ = false;
    // The command palette (palette_ui.cpp): Ctrl/Cmd+K or the top bar's
    // search button. Draws nothing while closed.
    void drawCommandPalette();
    bool paletteOpen_ = false;
    bool paletteFocus_ = false;  // put the caret in the query on the next frame
    char paletteQuery_[128] = "";
    int paletteSel_ = 0;
    // Put the AI Assistant's input caret in its box on the next frame (the
    // palette's Ctrl+Enter hands its query over as the draft).
    bool chatFocusInput_ = false;
    // Which machine the toolbar's Run/Stop pair drives: false = the emulator
    // (PCSX2), true = a real console over ps2link. Machine-global (editor.ini
    // `runOnPs2`) - which console is on this desk is not project data. The Play
    // glyph is green for the emulator and blue for the PS2, so the target is
    // readable without opening the dropdown. F5/Ctrl+F5 and F6/Ctrl+F6 stay
    // target-explicit and ignore this.
    bool runOnPs2_ = false;
    // Build && run (or run only) on the selected target - what the toolbar's
    // Play button and its dropdown entries call, so the two can never disagree.
    void runSelectedTarget(bool build);
    // Called by EVERY path that launches the game, immediately before it does:
    // opens the Debugger panel when the project is built with the debugger on
    // and the panel is closed. It has to be its own call precisely because the
    // paths do not share one: the F5/F6 chords reach runner_ directly (they are
    // target-explicit, so they cannot go through runSelectedTarget), and a
    // launch that misses this is a debug session with nowhere to watch it.
    void openDebuggerForLaunch();
    // UI (DPI) scaling. uiScaleUser_ == 0 means "auto" (follow the monitor's
    // content scale); a value > 0 is an explicit multiplier (1.0 == 100%).
    // applyUiScale() recomputes the effective scale and re-applies it to the
    // ImGui style + fonts; setUiScale() also persists the choice.
    void applyUiScale();
    void setUiScale(float userScale);
    // Interface theme (docs/editor-theme.md). applyTheme() rebuilds the
    // unscaled reference style from the theme and re-applies the UI scale over
    // it - the two are one operation, because baseStyle_ IS the themed style;
    // setTheme() also persists the choice to editor.ini.
    void applyTheme();
    void setTheme(theme::Id id);
    // Loads the interface font once at startup: the first face in
    // platform::uiFontFiles() this machine has. A machine with none keeps
    // ImGui's built-in bitmap font, and everything else still works.
    void loadUiFont();
    // Multiply a design-time pixel size (widget widths, child regions, window
    // sizes, hand-drawn previews) by the active UI scale, so code literals
    // track DPI/zoom the same way fonts and style spacing already do (see
    // applyUiScale: FontScaleMain + ScaleAllSizes). Any tool window that gives
    // a widget a literal pixel size should route it through this or the text
    // clips at high scale (a 180 px combo can't hold 2.5x-tall glyphs).
    float scaled(float px) const { return px * uiScaleApplied_; }
    void drawViewportWindow();
    // Switch the viewport camera projection (View menu, the viewport's "Proj:"
    // button, the axis gizmo, the numpad shortcuts). Editor state: it rides
    // into the .tyra on the next save like the gizmo mode, not a project edit.
    void setViewProjection(Viewport::Projection p);
    // Axis-view gizmo in the viewport's top-right corner (the Blender/Maya
    // navigation widget): the three world axes as labelled balls that rotate
    // with the camera; clicking one snaps to that orthographic axis view, and
    // clicking the hub toggles perspective. Drawn with ImDrawList over the
    // rendered image. Returns true while the cursor is over the widget, so the
    // caller can keep that click from also picking/deselecting objects.
    bool drawAxisGizmo(ImVec2 imgPos, ImVec2 avail);
    // Machine-global (editor.ini): the gizmo sits where HUD authoring wants
    // the corner, so it can be turned off (View > Projection > Axis gizmo).
    bool showAxisGizmo_ = true;

    // --- TV safe-area overlay (docs/safe-areas.md) --------------------------
    // Guides for framing something a real television will not crop: the picture
    // rectangle the console outputs, plus the classic action- and title-safe
    // insets. Machine-global (editor.ini) like the axis gizmo - a viewing aid,
    // not project data. All of it hides behind the viewport's gear so it cannot
    // clutter the image by default.
    struct SafeAreaCfg {
        bool frame = true;       // the 4:3 / 16:9 picture rectangle
        bool action = true;      // 90% - nothing important outside this
        bool title = true;       // 80% - text belongs inside this
        bool centre = false;     // centre cross + thirds
        bool bothRegions = false;  // NTSC's shorter picture inside PAL's
        // 0 = follow the project (its widescreen setting), 1 = force 4:3,
        // 2 = force 16:9. Forcing is for checking the other case without
        // touching the project.
        int aspect = 0;
        float opacity = 0.55f;
    };
    SafeAreaCfg safeArea_;
    bool showSafeArea_ = false;  // the master switch (the gear's first item)
    // PS2 output mode (docs/ps2-viewport.md), machine-global like the safe
    // areas. `ps2ViewportOutput` resolves the project's display settings into
    // the GS geometry the viewport renders at - the host twin of the engine's
    // RendererSettings::updateGeometry, so a new display mode is one entry in
    // both places.
    bool viewportPs2_ = false;
    // PS2 shading (per-vertex + flat triangles) and GS colour simulation
    // (0 = match project, 1 = full 32-bit, 2 = 16-bit, 3 = 16-bit + dither) -
    // docs/ps2-viewport.md. Machine-global like viewportPs2_, pushed to the
    // viewport each frame next to setPs2Output.
    bool viewportPs2Shade_ = false;
    int viewportGsColor_ = 0;
    Viewport::Ps2Output ps2ViewportOutput() const;
    // Draws the overlay over the viewport image. `pos`/`size` are the image rect.
    void drawSafeAreaOverlay(const ImVec2& pos, const ImVec2& size);
    // The gear button + its popup, drawn at the left end of the viewport's
    // bottom button row. Returns true when the cursor is over it, so a click
    // there does not fall through to the scene the way the axis gizmo's veto
    // works.
    bool drawViewportGear(const ImVec2& pos, const ImVec2& size);
    // Horizontal space the gear occupies, inset included: where the rest of the
    // bottom-left row starts. ONE definition because the gear and the row are
    // drawn by different code - they used to pick their corner independently
    // and the gear landed on top of "Center view".
    float viewportGearSpan() const;
    void drawProjectWindow();
    void drawPropertiesWindow();
    // Properties panel body when more than one object is selected: only the
    // fields common to every selected object, edited in one pass.
    void drawMultiProperties();
    void drawSceneSection();
    void drawLayersSection();
    bool isObjectHiddenInEditor(const SceneObject& o) const;
    void drawScriptsSection();
    int railScriptFilter_ = 0;  // All | Flow graphs | C++ | VU
    char railScriptSearch_[96] = "";
    void drawNewScriptModal();
    void drawNewSceneModal();
    void drawDeleteSceneModal();

    // --- Window layouts (Layout menu; project_.windowLayouts) --------------
    void drawLayoutMenu();     // the Layout top-level menu contents
    void drawLayoutModals();   // New / Rename Layout popups
    // Switch to layout `index`: fold the current on-screen arrangement into the
    // layout being left, then apply the target (open its windows + schedule an
    // ini load or a recipe rebuild). No-op without a project / out of range.
    void switchLayout(int index);
    // Arrange the dockspace from a built-in recipe (LayoutRecipe). Runs inside
    // drawUI (needs the dockspace id) before any panel window is submitted.
    void buildLayoutRecipe(int recipe, unsigned int dockspace);
    // Apply the active layout after a switch/open: set optional-window open
    // flags, then schedule the saved-ini load or the recipe rebuild.
    void applyActiveLayout();
    // Fold the live docking arrangement + open windows into the active layout.
    void captureActiveLayout();
    // Reset the active layout to its built-in recipe (drops manual edits). No-op
    // for a user layout that has no recipe.
    void resetActiveLayoutToRecipe();
    // Optional editor windows a layout can carry open, keyed by stable string.
    // showFlagForKey returns nullptr for an unknown key.
    bool* showFlagForKey(const std::string& key);
    void applyOpenWindows(const std::vector<std::string>& keys);  // set each flag = membership
    std::vector<std::string> captureOpenWindows() const;
    void drawFlowGraphWindow();
    // Names used by same-type "Variables" nodes across every scene's graphs
    // (the int / bool / position namespaces are separate).
    std::vector<std::string> flowVarNames(const std::string& nodeType) const;
    // Open the project in VS Code. A non-empty `file` (project-relative or
    // absolute) is also opened in that window - jump straight to a script /
    // custom node while keeping the whole project in context.
    void openInVSCode(const std::string& file = "");
    // Install/refresh the bundled TyraX VS Code extension (syntax highlighting +
    // validation for .flownode/.screenfx) into the user's ~/.vscode/extensions.
    // Best-effort and idempotent; called before openInVSCode and from the
    // "Install VS Code extension" menu item. Returns a human-readable status.
    std::string installVsCodeExtension();
    void drawOutputWindow();
    void drawDebugWindow();
    // The two log panels above share one body (docs/log-panels.md): a severity
    // filter row with per-level counts, then the lines themselves coloured by
    // level. logSetText() classifies new text (incrementally while a build
    // streams into it), logRefresh() rebuilds the filtered view once per frame,
    // and drawLogPanel() draws v.text through v's filter. drawLogHeader() ends
    // the panel's header row: the level chips right-aligned, after whatever
    // actions the caller put at the row's left (wrapping when the panel is
    // narrow). drawLogTabBadge() puts the error/warning count on the panel's
    // dock tab (see its definition for why not in the title).
    void logSetText(LogView& v, std::string&& text);
    void logRefresh(LogView& v);
    void drawLogHeader(LogView& v);
    void drawLogPanel(const char* id, LogView& v);
    void drawLogTabBadge(const LogView& v);
    void drawDiscLayoutWindow();
    void drawNewProjectModal();
    void drawPreferencesWindow();         // project-wide defaults (Project menu)
    void openProjectPreferences();        // raise it, seeding the grid scratch
    void drawEditorPreferencesModal();    // machine-global settings (Edit menu)
    void saveGlobalConfig();              // write editor.ini from the App members
    // devsession.hpp: "this editor has that project open, and here is what the
    // game is doing" - so nobody has to search the disk for the live project.
    void publishDevSession();
    double devSessionNext_ = 0.0;      // throttle
    long long devSessionStarted_ = 0;  // epoch seconds, first publish
    void drawNavigationModal();  // global viewport-navigation settings
    void drawScenePreferencesModal();
    void openScenePreferences();  // stage the active scene into scenePref* + open
    void openProjectDialog();
    void applyProjectToViewport();
    // Appends a default object of `type`, selects it and rests it on whatever
    // is under it. `commit` false leaves the commit to the caller: the preset
    // wrappers below tweak the fresh object AFTER this returns, and one insert
    // must be ONE undo step - committing here as well would make the first
    // undo roll back only the tweak.
    void addObject(PrimitiveType type, bool commit = true);
    void addEmitter(int kind);  // Effects menu presets (fire/smoke/fog/sparks)
    void addSoundEmitter();
    void addPointLight();
    void addSavePoint();
    void addEmpty();
    void addDecal();
    void addMirror();
    void addPortal();
    void addArea();
    void addScroller();
    // An editor note pinned into the scene (docs/comments.md). Selects it and
    // puts the keyboard in the Properties field, because a note with no text
    // is the one object that is useless the moment it is created.
    void addComment();
    void drawAddObjectMenu();
    // Area picker for a "catch area" reference (Mirror/Portal/feed Camera) or
    // a layer zone: a combo of this scene's Area objects plus <none>. Returns
    // true when the selection changed (the caller commits).
    bool areaCombo(const char* label, std::string& ref);
    // The catch-area block shared by the Mirror / Portal / feed Camera panels:
    // picker + "Update every frame" + the resolved counts. `verb` is how the
    // panel words what happens to a caught object ("re-drawn", "shown", ...).
    bool catchAreaControls(SceneObject& o, const char* verb);
    // --- The model importer (modelimport_ui.cpp, docs/model-import.md) ------
    // ONE window for every model format: pick a file, it is read on a worker
    // (modelimport::load), the window previews the planned result and offers
    // the options (split, pivot, size, clean-up, reduction, texture size,
    // static / animated), and Import writes it on the worker. The optional
    // target is the Asset Browser folder that asked; `forObject` is the
    // object waiting for a model (an entity being created from the model
    // picker), see ModelImportFor.
    void importModelAsset(const std::string& targetFolder = {});
    void importModelFile(const std::string& path, const std::string& targetFolder);
    void modelImportTick();
    void drawModelImportModal();
    void modelImportReplan();
    void modelImportPlace(const modelimport::Result& res);
    enum class ImportPhase { Idle, Loading, Ready, Writing };
    // Who is waiting on the import: nothing, a new Model / Animated model
    // entity (placed when the import lands), or an existing object whose
    // model is being replaced (modelPickObjectId_).
    // Borrow: the Animation Editor's "Borrow clips from a file > Import a
    // file..." - the imported .glb/.fbx becomes a clip-import row on
    // modelImportBorrowFor_ once the (asynchronous) import finishes.
    enum class ModelImportFor { Nothing, NewStatic, NewAnimated, Retarget, Borrow };
    std::string modelImportBorrowFor_;
    std::thread modelImportThread_;
    std::atomic<bool> modelImportDone_{false};
    ImportPhase modelImportPhase_ = ImportPhase::Idle;
    bool modelImporting_ = false;  // any phase but Idle
    bool modelImportOpen_ = false;
    ModelImportFor modelImportFor_ = ModelImportFor::Nothing;
    std::string modelImportFile_, modelImportTarget_, modelImportProjectDir_;
    std::string modelImportError_;
    std::vector<std::string> modelImportTexDirs_;
    std::unique_ptr<modelimport::Source> modelImportSrc_;
    modelimport::Options modelImportOpt_;
    std::vector<modelimport::Planned> modelImportPlan_;
    modelimport::Result modelImportResult_;
    std::string modelImportWriteError_;
    uint64_t modelImportVersion_ = 1;
    int modelImportSel_ = -1;  // highlighted planned model (-1 = none)
    bool modelImportScrollToSel_ = false;  // a preview click: bring the row into view
    float modelImportAngle_ = 40.0f, modelImportPitch_ = 18.0f;
    float modelImportZoom_ = 1.0f;
    float modelImportPanX_ = 0.0f, modelImportPanY_ = 0.0f;
    bool modelImportWire_ = false;
    // Clip playback in the import preview: the source file posed through the
    // Animation Editor's preview path (by absolute path - nothing is written).
    // Playing or a time past 0 shows the pose; 0 and paused = the planned model.
    int modelImportClip_ = 0;
    bool modelImportClipPlay_ = false;
    float modelImportClipTime_ = 0.0f;
    int modelImportUnit_ = 0;         // kUnitPresetMeters index, 3 = custom
    float modelImportMeters_ = 1.0f;   // meters per file unit
    bool modelImportAddToScene_ = false;
    bool modelImportLayout_ = true;    // split: place at the source layout
    char modelImportName_[96] = {};
    // Where the import writes, project-relative ("res/models/terrain/rocks");
    // a split import adds /<name>. Remembered between imports, so filing a
    // pack into a catalogue is one setting, not one per file.
    char modelImportFolder_[256] = "res/models";
    std::string modelImportLastFolder_ = "res/models";

    // --- The model picker (modelimport_ui.cpp) ------------------------------
    // What a Model / Animated model entity's "Model" field opens: a thumbnail
    // grid of the project's models of the right KIND (static: .obj, plus any
    // .glb/.fbx - placed through its static twin; animated: .glb/.fbx), with
    // a search box and "Import new...". objectId empty = create a new entity.
    void openModelPicker(bool animated, const std::string& objectId = {});
    void drawModelPicker();
    // Points `o` at a model asset as `animated` or static, the one place that
    // knows what a model change resets (clips, player clips, the material
    // that belonged to the previous model). False + status on failure.
    bool assignModel(SceneObject& o, const std::string& rel, bool animated);
    // Creates a Model / Animated model entity around an asset (the picker's
    // and the importer's "add to scene"). One undo step.
    void addModelEntity(const std::string& rel, bool animated);
    bool modelPickOpen_ = false;
    bool modelPickAnimated_ = false;
    std::string modelPickObjectId_;
    char modelPickFilter_[64] = {};
    std::vector<std::string> modelPickItems_;  // listed when the picker opens
    std::string modelPickFolder_;  // folder shown ("" = every model), remembered
    // "Real-world size" of a model asset (docs/world-scale.md): what one unit
    // of the file measures in meters, which combined with the project's world
    // scale is the scale objects made from it are inserted at. Opened right
    // after an import and from the Assets list; nothing about the file itself
    // is touched, only Project::modelUnitMeters.
    void beginModelSizing(const std::string& relPath);
    // Import already paid to parse/bake the model in the worker, so seed the
    // dialog from those bounds instead of synchronously loading it yet again.
    void beginModelSizing(const std::string& relPath, const float mn[3],
                          const float mx[3]);
    void drawModelSizeModal();
    bool modelSizeOpen_ = false;       // a sizing dialog is requested this frame
    std::string modelSizePath_;        // asset being sized ("" = none staged)
    float modelSizeSrc_[3] = {0.0f, 0.0f, 0.0f};  // authored size, file units
    bool modelSizeMeasured_ = false;   // the bounds above could be read
    int modelSizeUnit_ = 0;            // preset: 0 m, 1 cm, 2 inch, 3 custom
    float modelSizeMeters_ = 1.0f;     // meters per file unit (what is stored)
    bool modelSizeApplyExisting_ = false;  // also rescale objects already placed
    bool modelSizeFresh_ = false;      // opened straight after an import
    // Copies a picked PNG into res/textures (terrain tiling); "" on cancel.
    std::string importTextureAsset();
    // Copies a picked .mtl (with its map_Kd textures, references rewritten to
    // the sanitized names) into res/materials; "" when cancelled/failed.
    std::string importMaterialAsset();
    // All .mtl assets an object can use: res/materials + res/models, as
    // project-relative paths ("res/materials/walls.mtl")
    const std::vector<std::string>& listMaterialAssets();
    std::vector<std::string> materialAssetCache_;
    std::string materialAssetProject_;
    double materialAssetScanTime_ = -1.0;
    // Combo picking an .mtl for the object (primitives: surface; models:
    // override). Returns true when materialPath changed.
    bool drawMaterialCombo(SceneObject& o);
    // Creates res/materials/<model>.mtl seeded from a model object's built-in
    // materials (names + Kd + extracted textures, from the .glb/.fbx or .obj),
    // assigns it to o.materialPath and opens the Material Editor on the model.
    // Returns the new material's project-relative path, or "" on failure.
    std::string createMaterialForModel(SceneObject& o);
    // The .mtl a model wears without an override (its sibling <stem>.mtl,
    // else the .obj's first mtllib); "" when it has none.
    std::string modelOwnMaterial(const std::string& modelRel);
    // Friendly authored-forward selector for skeletal models and player
    // avatars. It writes SceneObject::modelYawOffset, including the common
    // "backwards" 180-degree correction. Returns true when committed.
    bool drawModelFacing(SceneObject& o);
    // Per-object LOD override rows. `animated` adds the animation-LOD row; a
    // static .obj model only takes the mesh-LOD distance. Returns true when a
    // value changed (caller commits).
    bool drawLodOverrides(SceneObject& o, bool animated = true);
    // The static model's distant version (billboard impostor / hull proxy):
    // one kind, one Bake, the switch distance. Returns true on a change.
    bool drawDistantVersion(SceneObject& o);
    // Roads (docs/roads.md): flatten the heightfield to the selected
    // road's interpolated line, smooth shoulder falloff, one undo step.
    void alignTerrainToRoad(int objIndex);
    // Road viewport editing (docs/roads.md): click empty ground = append a
    // point, click a point = drag it, click the line = insert there.
    bool roadEdit_ = false;
    int roadDragPoint_ = -1;
    // Junction overrides (docs/roads.md, "Junction overrides"). A junction is
    // not an object: it is selected by its identity - the road-id pair and
    // where it was - and re-found in the plan every frame, so a road edit
    // that moves the crossing a little keeps it selected. `override` >= 0
    // selects an ORPHANED override (its crossing is gone) by index instead.
    struct JunctionSel {
        bool active = false;
        std::string a, b;  // road ids, the crossing's A/B
        float x = 0.0f, z = 0.0f;
        int orphan = -1;   // index into SceneData::roadJunctions
        int scene = -1;    // a scene switch drops it
    };
    JunctionSel junctionSel_;
    // The active scene's crossings (markers, Properties): planCrossings
    // without the decals, cached on a signature of the roads + overrides.
    roadgen::CrossingPlan crossingPlan_;
    std::vector<roadgen::CrossingRoad> crossingRoadList_;
    std::vector<int> crossingRoadObj_;  // road k -> index in project_.objects()
    uint64_t crossingPlanSig_ = 0;
    const roadgen::CrossingPlan& sceneCrossings();
    // Crossing index the selection resolves to (-1: none / orphan).
    int selectedCrossing();
    void selectJunction(int crossing);
    void drawJunctionProperties();
    // Viewport diamonds on the crossings, shown while a road or a junction is
    // selected. Returns the crossing under `mouse` (or -1) when `hit` asks.
    int junctionMarkers(ImVec2 imgPos, ImVec2 avail, bool draw, ImVec2 mouse);
    // Retargets every BY-NAME reference to `renamed` after its name changed
    // from `from` (cutscene tracks and camera shots, mirror lists, scroller
    // members, camera feeds, portal links, texture feeds, and - for an Area -
    // catch areas, layer zones and In Area node params). Object references are
    // names, not ids, so a rename that skips this leaves them pointing at a name
    // nothing answers to. Two callers: the Properties name field and the AI
    // Assistant's set_object. Does NOT commit.
    void renameObjectRefs(SceneData& sc, const SceneObject& renamed,
                          const std::string& from);

    // Creates a scene object for a model already in res/models (no copying).
    // `at` (optional) is where the object lands before the placement snap - the
    // Asset Browser's drag & drop into the viewport passes the cursor's hit.
    // `commit` false leaves the commit to the caller, for the same reason
    // addObject above takes the flag: a caller that tweaks the fresh object
    // afterwards (the AI Assistant's add_object naming it) must still produce
    // ONE undo step.
    void addModelObject(const std::string& relPath, const float* at = nullptr,
                        bool commit = true);
    // Project-panel section listing res/models + res/textures with the
    // Import... buttons (the object pickers only offer what is listed here)
    void drawAssetsSection();
    // Assets page state (rail_ui.cpp): type chip, search, folder, Unused,
    // Grid | List and the selected file.
    int railAssetKind_ = 0;
    char railAssetSearch_[96] = "";
    std::string railAssetFolder_;
    bool railAssetUnused_ = false;
    bool railAssetList_ = false;
    std::string railAssetSel_;

    // --- Asset Browser (Tools > Asset Browser, docs/asset-browser.md) -------
    // A file manager over the project's res/ tree: folder tree, thumbnail grid,
    // type filters, search, and file operations that carry the project's
    // references with them. Everything below is implemented in assetbrowser.cpp.
    enum class AssetKind {
        Model,      // .obj - static geometry
        AnimModel,  // .glb/.fbx - skeletal model
        Material,   // .mtl material library
        Texture,    // PNG/JPG/TGA/BMP image
        Music,      // res/audio WAV (streamed)
        Sound,      // res/sfx WAV (ADPCM one-shot)
        Font,       // TTF/OTF source
        DronePatch, // .drone - a Drone Generator audio project
        Other,
    };
    static AssetKind assetKindOf(const std::string& rel);
    static const char* assetKindName(AssetKind k);
    struct AssetItem {
        std::string rel;   // project-relative ("res/models/props/tree.obj")
        std::string name;  // file name
        AssetKind kind = AssetKind::Other;
        unsigned long long bytes = 0;
        long long mtime = 0;
        // Written by the build (baked menu panels, text sprites, glyph
        // atlases, .tmdl meshes, paint-layer sidecars): shown only with "Show
        // generated", and never moved, renamed or deleted from here.
        bool generated = false;
    };
    struct AssetDir {
        std::string rel;     // "res/models/props" ("res" = the root)
        std::string name;    // "props" ("res" for the root)
        std::string parent;  // "res/models" ("" for the root)
        std::vector<std::string> children;  // sub-folder rel paths, sorted
        int files = 0;                      // files directly inside
        int filesDeep = 0;                  // files inside, sub-folders included
    };
    // Project-side reference census of one asset. Built for EVERY asset in one
    // pass over the model (rebuildAssetUsage) so the grid can badge unused
    // files for free; the Wavefront side (a .mtl naming a texture) is disk IO
    // and stays on demand in assetWavefrontUsers().
    struct AssetUsage {
        int objects = 0;  // scene objects
        int nodes = 0;    // flow-graph nodes
        int other = 0;    // HUD/menus/fonts/terrain/LOD chains/lists/clip edits
        std::vector<std::string> lines;               // readable "where" lines
        std::vector<std::pair<int, int>> objectRefs;  // (scene, object index)
        int total() const { return objects + nodes + other; }
    };
    void drawAssetBrowserWindow();
    // Details + per-type actions of the selected asset (the bottom strip).
    void drawAssetInspector(const std::string& rel);
    // Per-asset texture-quality override of Preferences > Textures, and the
    // artist-authored mesh LOD chain of a model. Shared by the browser's
    // inspector and the Project panel's asset summary.
    void drawAssetQualityCombo(const std::string& assetRel);
    void drawAssetLodButton(const std::string& assetRel);
    // "Size..." - the model's recorded real-world size (docs/world-scale.md),
    // which decides the scale objects made from it are inserted at.
    void drawAssetSizeButton(const std::string& assetRel);
    // Asset Browser inspector rows (assetbrowser.cpp): the per-asset texture
    // quality with its meaning spelled out, and a caption + field-like value
    // button (Real size, LOD).
    void drawAssetTexturesRow(const std::string& assetRel);
    void assetSettingRow(const char* caption, const char* value, const std::string& assetRel,
                         const std::function<void()>& onClick, const char* tip);
    // Rebuild assetItems_/assetDirs_ from disk. Cheap enough for a res/ tree
    // (a few hundred files); throttled by assetScanTime_ while the window is
    // open and forced by assetsChanged() after any file operation.
    void scanAssetTree();
    // Files moved/appeared/vanished on disk: rescan, drop the derived caches
    // (model/material summaries, thumbnails, WAV checks) and re-census.
    void assetsChanged();
    void rebuildAssetUsage();
    const AssetUsage* assetUsageFor(const std::string& rel);
    // Files under res/ that a .mtl/.obj names and must keep as siblings: an
    // .obj's material libraries plus the textures those libraries reference.
    std::vector<std::string> assetWavefrontDeps(const std::string& rel);
    // The reverse: .obj/.mtl files under res/ that name `rel` (including the
    // implicit "<stem>.mtl" sibling an .obj loads without a mtllib line).
    std::vector<std::string> assetWavefrontUsers(const std::string& rel);
    // Editor-side sidecars of an asset that must travel with it (paint layers,
    // replacement UVs). The baked "<stem>.tmdl" is NOT one: it is deleted
    // instead, since the next build re-bakes it in the new location.
    std::vector<std::string> assetSidecars(const std::string& rel);
    // Moves files (and the folder-internal dependencies they need) into
    // `destFolder`. Returns "" on success, or the reason it refused - a move
    // that would leave a .mtl looking for a texture in the wrong folder is
    // rejected rather than half-applied, because Wavefront references have to
    // stay bare sibling names for the PS2 to resolve them.
    std::string moveAssets(const std::vector<std::string>& rels,
                           const std::string& destFolder);
    // Moves a whole folder (with everything in it) into `destFolder`.
    std::string moveAssetFolder(const std::string& folderRel,
                                const std::string& destFolder);
    // Renames one file in place, rewriting the sibling Wavefront references
    // that name it (safe: they stay bare names in the same folder) and, for a
    // model, its exclusively-owned "<stem>.mtl" along with it.
    std::string renameAsset(const std::string& rel, const std::string& newName);
    std::string renameAssetFolder(const std::string& folderRel,
                                  const std::string& newName);
    std::string createAssetFolder(const std::string& parentRel,
                                  const std::string& name);
    // Copies a file next to itself under a free "<stem>-copy<n>" name.
    std::string duplicateAsset(const std::string& rel);
    // Every project reference to `from` becomes `to`. Covers object
    // model/material/sound paths, terrain materials and layers, HUD/menu/
    // splash/loading images, fonts, the music+sound lists and their build
    // options, per-asset texture quality, LOD chains, animation clip edits and
    // audio flow nodes. Returns how many references moved. A new field that
    // stores an asset path belongs in here, or renaming its file breaks it.
    int retargetAssetPath(const std::string& from, const std::string& to);
    // Rewrites the mtllib / map_Kd / refl statements of one Wavefront file that
    // name `oldName` to `newName` (bare file names, same folder). True when the
    // file changed.
    static bool rewriteWavefrontRef(const std::string& fileAbs,
                                    const std::string& oldName,
                                    const std::string& newName);
    // Stages the browser selection for deletion (one confirm dialog for the
    // whole set, reusing the per-asset reference warnings).
    void requestAssetSelectionDelete();
    void drawAssetBrowserModals();
    // Deletes one asset file, its sidecars and the stale baked .tmdl, and
    // clears what the project stored about it. Kinds the older per-asset dialog
    // already handles are routed through performAssetDelete so the two cleanup
    // paths cannot drift apart.
    void deleteAssetFile(const std::string& rel);
    // A model dragged from the browser onto the viewport: the object lands where
    // the cursor points (u, v are normalized image coords).
    void dropAssetIntoScene(const std::string& rel, float u, float v);
    // Adds the asset to the scene the way its type wants: a model becomes a
    // Model object, a material opens the Material Editor, an image opens the
    // texture pickers' owner. Returns false when the type has no action.
    bool activateAsset(const std::string& rel);
    // Absolute path of a project-relative asset.
    std::string assetAbs(const std::string& rel) const;

    bool showAssetBrowser_ = false;
    std::vector<AssetItem> assetItems_;         // every file under res/
    std::map<std::string, AssetDir> assetDirs_;  // by rel path, "res" included
    std::string assetFolder_ = "res";            // folder being listed
    std::vector<std::string> assetSelection_;    // selected files (rel paths)
    std::string assetAnchor_;                    // last click (shift-range end)
    std::string assetSearch_;                    // name filter (substring)
    int assetFilter_ = 0;                        // type chip, 0 = All
    // Smart view instead of a folder: 0 = the folder, 1 = Unused, 2 =
    // Recently changed, 3 = Generated by build (replaced the Generated box).
    int assetView_ = 0;
    bool assetRecursive_ = false;                // list sub-folders too
    bool assetShowGenerated_ = false;
    int assetSort_ = 0;         // 0 name, 1 type, 2 size, 3 newest first
    bool assetGridView_ = true;  // thumbnails (false = detail rows)
    float assetTileSize_ = 84.0f;
    double assetScanTime_ = -1.0;   // ImGui time of the last disk scan
    int assetThumbBudget_ = 0;      // thumbnails still allowed this frame
    // Asset Browser inspector preview orbit (view state; reset per asset).
    std::string assetOrbitRel_;
    float assetOrbitYaw_ = 35.0f, assetOrbitPitch_ = 22.0f, assetOrbitZoom_ = 1.0f;
    float assetOrbitPanX_ = 0.0f, assetOrbitPanY_ = 0.0f;  // framing radii
    uint64_t assetUsageSerial_ = ~0ull;  // edit serial the census was built at
    std::map<std::string, AssetUsage> assetUsage_;
    // Reverse Wavefront map (a texture / .mtl -> the .obj/.mtl files naming it),
    // parsed from disk on first use after a scan - the file operations need it,
    // the grid does not, so it is not part of scanAssetTree's cost.
    std::map<std::string, std::vector<std::string>> assetWfUsers_;
    bool assetWfUsersReady_ = false;
    std::set<std::string> assetTreeOpen_;  // expanded folder tree nodes
    // Pending in-place rename ("" = none): the file/folder and its edit buffer.
    std::string assetRenameRel_;
    bool assetRenameIsFolder_ = false;
    char assetRenameBuf_[128] = {};
    bool assetRenameFocus_ = false;
    std::string assetNewFolderParent_;  // "New folder" popup ("" = closed)
    char assetNewFolderBuf_[128] = {};
    // Result of the last operation: a refusal reason shown in the window and a
    // status line. Refusals stay visible until the next operation.
    std::string assetOpError_;
    // Multi-file delete staged by the browser (the single-asset dialog handles
    // one file at a time; this is the queue behind it).
    std::vector<std::string> assetDeleteBatch_;
    bool assetDeleteBatchActive_ = false;
    // Folder removed after its files (empty = the batch is a plain selection).
    std::string assetDeleteFolder_;
    // Files directly under res/<subdir> with the given extension (lowercase
    // compare), names only, sorted by the directory iteration order
    std::vector<std::string> listAssetFiles(const char* subdir, const char* ext);
    std::vector<std::string> listAnimatedModelFiles();
    // "Pick..." button + popup listing res/textures; true when path changed
    bool pickProjectTexture(const char* popupId, std::string& path);
    // Road surface picker: materials first, legacy direct PNGs second. A .mtl
    // resolves through its first map_Kd in both viewport and generated game.
    bool drawRoadSurfaceCombo(const char* label, const char* id,
                              std::string& surfacePath,
                              const char* noneLabel = nullptr);
    // Cached objparser summary of a model (for the properties panel)
    struct ModelInfo {
        bool ok = false;
        int tris = 0;
        // Model-space bounds from the parse that produced this summary. The
        // Asset Browser's Size dialog consumes these instead of loading a
        // large model again on the UI thread.
        float min[3] = {0.0f, 0.0f, 0.0f};
        float max[3] = {0.0f, 0.0f, 0.0f};
        // Two different vertex counts, and the difference is the point:
        // `verts` is what actually goes to VU1 (three per triangle, corners
        // split wherever a normal/UV/material does), `positions` is the obj
        // `v` count the modelling tool shows. A model whose verts are far
        // above 3x positions is paying for split corners.
        int verts = 0;
        int positions = 0;
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
        // Frame-zero baked bounds, retained for the Asset Browser's Size
        // dialog so clicking it never triggers a second animation bake.
        float min[3] = {0.0f, 0.0f, 0.0f};
        float max[3] = {0.0f, 0.0f, 0.0f};
        std::vector<std::string> warnings;
        // Baked materials (glTF), one per draw part: the color the game and
        // the viewport tint the mesh with. name + baseColorFactor; textured
        // parts also carry a texture flag.
        struct Material {
            std::string name;
            float color[3] = {1, 1, 1};
            bool textured = false;
        };
        std::vector<Material> materials;
    };
    std::map<std::string, GlbInfo> glbInfoCache_;
    const GlbInfo& glbInfo(const std::string& relPath);
    // glbInfo(relPath).clips with the Animation Editor's renames applied -
    // the names the game resolves and every reference stores.
    std::vector<std::string> effectiveClips(const std::string& relPath);
    // Summary of a standalone .mtl (material lines + missing-texture flags)
    const ModelInfo& materialInfo(const std::string& relPath);
    // Terrain-material picker (project-wide + per-scene overrides). Lists the
    // project's .mtl assets; returns true when the selection changed.
    bool drawTerrainMaterialCombo(const char* label, std::string& matPath);
    // Mirrors res/audio + res/sfx into the music/sounds lists (manual drops
    // are picked up, vanished files are dropped). announce: status even when
    // nothing changed.
    void rescanAssets(bool announce);
    // Cached format problem of a project WAV ("" = fine). sfx = adpenc rules
    // (mono PCM16/22050 + standard header); music = the song player rules.
    const std::string& wavIssue(const std::string& relPath, bool sfx);
    std::map<std::string, std::string> wavIssueCache_;
    void importHudImage();
    // Imports a PNG into res/hud/ as a HudImage appended to `target` (shared by
    // the UI Editor's HUD list and the Loading Screen editor). Returns the new
    // index, or -1 on failure.
    int importHudImageInto(std::vector<HudImage>& target);
    // The same import from a known file (an OS drop): copy into res/hud, append.
    int importHudImageFile(const std::string& src, std::vector<HudImage>& target);
    // Picks one of the project's fonts by name (menus, HUD texts, loading
    // texts). "" = the default entry. Returns true when the reference changed.
    bool fontCombo(std::string& fontRef);
    // Picks the TTF behind a Font Manager entry: default chain / fonts already
    // in the project / stock Windows fonts / import. Returns true when
    // fontPath changed. Only the Font Manager resolves real files.
    bool fontSourceCombo(std::string& fontPath);
    void drawFontManagerWindow();
    // Tools > UI Editor > Button icons: the {{name}} placeholders any text can
    // splice an image into (docs/text-icons.md). A modal, not a panel section:
    // it wants room for a preview grid.
    void drawTextIconsModal();
    // Tools > Input Map (docs/input-bindings.md): the named actions every
    // gameplay button goes through, plus the per-project binding presets.
    void drawInputMapWindow();
    // One action's pad / key / mouse pickers inside `preset`. Returns true when
    // something changed (the caller commits).
    bool inputBindingRow(InputPreset& preset, const InputAction& action);
    // Tools > Tree Generator: procedural low-poly tree authoring with a live
    // 3D turntable preview (treegen). "Add to scene" bakes the .obj/.mtl/PNGs
    // into res/models/trees and drops a Model object in - see treegen.hpp.
    void drawTreeGeneratorWindow();
    // Tools > Global Illumination: per-scene staleness + the bake itself
    // on gibake::Baker's worker thread (docs/global-illumination.md).
    void giBakerPoll();
    void drawGiBakeSection();
    // The "Baked lighting" tab of the Ambience Editor (docs/ambient-occlusion.md).
    // One home for the light that is computed on the host and shipped as
    // pixels; drawBakedLightingSection is a list of sections, drawModelAoSection
    // is the first of them.
    void modelAoPoll();
    void drawBakedLightingSection();
    void drawSceneAoSection();
    void drawModelAoSection();
    // "Pre-lit models" - the second section of that tab (docs/prelit-models.md,
    // "Managing pre-lit objects"): which objects of the active scene are
    // pre-lit, whether their textures still match the scene, and the batch bake.
    void drawPrelitSection();
    // "Baked shadows" - the fourth section (docs/shadows.md): the project-wide
    // quality, the per-scene staleness + budget readout and the bake itself.
    void drawShadowBakeSection();
    // The bakes those tabs start, shared with the Build menu's Bake items (and so
    // the command palette): one starter per button, never a second mechanism.
    void startGiBake(bool allScenes);
    void startShadowBake(bool allScenes);
    void startModelAoBake();
    void startPrelitBake(bool onlyStale);
    int prelitPendingCount();  // the "Bake pending (N)" count of the active scene
    // Pushes the CACHED shadow bake into the viewport (never bakes), and
    // drains shadowBaker_ so a finished one becomes visible.
    void updateShadowDecals();
    void shadowBakerPoll();
    // Drains litBaker_ and applies whatever it finished, as ONE undo step.
    // Polled every frame from drawUI and from nowhere else - the giBakerPoll
    // rule: a batch started from the tab has to land whether or not the tab (or
    // Properties, or the object's selection) is still there when it finishes.
    void litBakerPoll();
    // Puts one object back on the material it had before its first bake and
    // forgets it was ever pre-lit. One commitChange.
    void revertPrelit(int objIndex);
    // What the panels draw from. litbake::signature hashes every file the GI
    // bake reads, so it cannot be asked per row per frame: this is recomputed
    // only when the model, the scene or the bake parameters move.
    struct PrelitStatus {
        int index = -1;
        bool prelit = false;  // its texture carries light TODAY
        bool fresh = false;   // ...and still matches the scene
        bool movable = false;  // project::objectRuntimeMovable
        int texSize = 0;       // the baked image's own size, 0 = not on disk
    };
    const std::vector<PrelitStatus>& prelitStatuses();
    const PrelitStatus* prelitStatusFor(int objIndex);
    // (Re)builds the in-memory tree mesh + textures from treeParams_ and bumps
    // treePreviewVersion_ so the preview re-uploads. Called on any param edit.
    void rebuildTreePreview();
    // "Add to scene" / "Save to project": bakes the current tree's assets
    // into res/models/trees (+ the `<stem>.tree` parameter sidecar that lets
    // the generator re-open it) and, when `place`, inserts a Model object.
    void addTreeToScene(bool place = true);
    // Loads a tree saved by the generator (the `.tree` sidecar beside
    // `objRel`) back into the Tree Generator and opens it. False when the
    // model has no sidecar. The Asset Browser's "Edit in Tree Generator".
    bool openTreeInGenerator(const std::string& objRel);
    // Tools > Prefabs (docs/prefabs.md): reusable groups of scene objects,
    // captured from a selection and stamped back into the world - by hand, by
    // a procedural graph, or by the Spawn Prefab node at runtime. Lives in
    // prefab_ui.cpp (the assetbrowser.cpp precedent).
    void drawPrefabsWindow();
    // Tools > Particle Editor (docs/particles.md): the project's particle
    // library - effects defined once, linked from emitters and from a
    // vehicle's tyre smoke, with procedural smoke / flame / glow textures.
    // Lives in particle_ui.cpp (the prefab_ui.cpp precedent).
    void drawParticleEditorWindow();
    // Opens the window with `effect` selected ("" = keep the selection).
    void openParticleEditor(const std::string& effect);
    // Regenerates the effect's procedural texture into res/ and points its
    // material at it; returns false with a message in particleStatus_.
    bool particleBakeTexture(ParticleEffect& fx);

    // Tools > Vehicle Editor (docs/vehicles.md): define a car once - model,
    // wheels, driving - and place it in as many scenes as you like. Lives in
    // vehicle_ui.cpp (the prefab_ui.cpp precedent).
    void drawVehicleWindow();
    // Runs the import bake for one definition and caches the result. Called
    // when the model or a budget changes, never per frame - it parses a .fbx.
    void vehicleRefreshBake(int index, bool force);
    // Per-frame, from drawUI: keeps definitions baked so placed instances draw
    // even with the window shut (the giBakerPoll rule).
    void vehicleTick();
    // Test drive (docs/vehicles.md): runs vehiclesim::step on a placed vehicle
    // straight in the viewport, against the real scene's terrain. The whole
    // point of vehiclesim being host-only - tuning grip and acceleration in a
    // "slider, feel, slider" loop instead of "slider, four minutes, PCSX2".
    void vehicleDriveTick();
    void vehicleDriveStart(int objectIndex);
    void vehicleDriveStop();
    // Scene-object index being test-driven, -1 = nobody.
    int vehicleDriveObj_ = -1;
    vehiclesim::DriveState vehicleDriveState_;
    float vehicleDriveAccum_ = 0.0f;  // test drive: time not yet stepped (1/50 s steps)
    // The transform the object had before the drive. A test drive is a way of
    // LOOKING at a vehicle, never an edit - it must put the car back exactly
    // where the author left it (the procedural seed-sweep rule).
    float vehicleDriveHome_[6] = {0, 0, 0, 0, 0, 0};
    // The drawn road surface of the driven scene, built at vehicleDriveStart:
    // the test drive's ground is max(terrain, road), the generated runtime's
    // groundSurfaceAt (docs/vehicles.md, "Wheels on the road surface").
    roadgen::Surface vehicleDriveRoads_;
    // Panel-driven controls, alongside the keyboard. Not a testing hook: when
    // you are tuning grip you want the car to keep going while both hands are
    // on the sliders, and a held key cannot do that.
    bool vehicleDriveHoldThrottle_ = false;
    float vehicleDriveSteer_ = 0.0f;
    // Damage preview (docs/vehicles.md, "Damage"): a dented COPY of one
    // definition's baked body, pushed to the viewport in place of the real
    // one - by the Damage tab's test hits and by a test drive into a wall.
    // Like the drive it is a way of looking, never an edit: nothing reaches
    // the project, and a re-bake or Repair drops it.
    std::string vehDmgPreviewId_;      // definition id, "" = no preview
    tmdl::Model vehDmgPreviewBody_;
    float vehDmgPreviewDamage_ = 0.0f;
    int vehDmgPreviewSerial_ = 0;      // the drive's impactSerial last dented
    float vehDmgTestSpeed_ = 18.0f;    // the Damage tab's test-hit speed
    float vehDmgPreviewOver_ = 0.0f;   // the pending hit's speed past the threshold
    std::vector<float> vehDmgPreviewHp_;  // per bake piece, the runtime's hp
    std::vector<char> vehDmgPreviewGone_;
    std::string vehDmgPreviewLost_;       // what came off, for the readout
    // Dents one impact into the preview (starting it from the bake if none).
    void vehicleDamagePreviewHit(const VehicleDef& v, const vehiclesim::Impact& im);
    void vehicleDamagePreviewReset();
    // Renaming a definition retargets every instance in every scene. The
    // renameFont precedent: a reference stores the NAME, so it has to follow.
    void renameVehicleDef(int index, const std::string& newName);
    // Body bounds of a placed Vehicle, for placement/collision (the
    // ModelAabbFn a Vehicle takes - only the App knows the definitions).
    bool vehicleBodyBounds(const SceneObject& o, float* mn, float* mx);

    // The reduced-raster upscaler's settings (blss_ui.cpp), drawn by Project >
    // Preferences against the staged settings.
    //
    // How much GS VRAM the reduced render hands back on THIS project's raster,
    // and the honest answer for 1x2, which is "nothing".
    std::string blssVramLine(const ProjectSettings& s) const;
    // THE BUILD'S OWN INTERLOCK, mirrored live. blssClashes() in templates.cpp
    // emits an #error for each of these, so the dialog and the build must
    // answer alike; this is the ONE mirror.
    //
    // Each clash is a list of what caused it, and each entry can be selected
    // and framed.
    struct BlssClashRef {
        int scene = -1;
        int object = -1;  // -1 when the clash is a property of the scene itself
        std::string label;  // "scene > object", or just the scene name
    };
    struct BlssClash {
        std::vector<BlssClashRef> dof, dofNode, portals, split, extrapolation;
        bool any() const {
            return !dof.empty() || !dofNode.empty() || !portals.empty() ||
                   !split.empty() || !extrapolation.empty();
        }
    };
    // `assumeProjectDefaultOn` walks the scenes that INHERIT the project default
    // as though it were on, which is the question a reader of a switched-off
    // project is asking. False - the default - answers the BUILD's question
    // verbatim, and that is the one this mirrors.
    BlssClash blssClashesFor(const ProjectSettings& staged,
                             bool assumeProjectDefaultOn = false) const;
    // `informational` styles the block as a note rather than a warning, for the
    // case that matters most: someone EVALUATING whether to turn the feature on
    // has to be able to see what would stop them before they turn it on.
    void drawBlssClashWarning(const BlssClash&, bool informational);
    // Switch to that scene, select that object and put the camera pivot on it.
    void blssSelectClash(const BlssClashRef&);
    // Use the upscaler / Render scale / Adapt resolution + the frame
    // extrapolation block they exclude. Returns true when `s` changed.
    bool drawBlssSettings(ProjectSettings& s);

    // Tools > World Facts (facts_ui.cpp, docs/world-facts.md): the fact
    // catalog, the named queries over it, the reaction rules, the saved test
    // scenarios and the live World Blackboard. Its own TU (the prefab_ui.cpp
    // precedent) - it is a self-contained subsystem and app.cpp is already the
    // build's critical path.
    void drawWorldFactsWindow();
    void drawFactCatalogTab();
    void drawFactUsedBy(const facts::Fact& f);
    void factJumpTo(int tab, int index);  // a Used-by click: select + bring the tab forward
    void drawFactQueriesTab();
    void drawFactRulesTab();
    void drawFactScenariosTab();
    void drawFactBlackboardTab();
    // The recursive condition editor, shared by queries and rules. Returns
    // true when the tree changed. `depth` guards the nesting the UI draws.
    bool drawFactCondition(facts::Condition& c, int depth, const char* id);
    // A fact / query picker, the paramCombo idiom - used by the condition
    // editor, the rule actions, the scenarios AND the flow-graph node params,
    // so every place a fact is named offers the same list.
    bool factCombo(const char* id, std::string& value, bool positionsToo);
    bool factQueryCombo(const char* id, std::string& value);
    // The catalog's Name field plus its completion dropdown - the whole
    // widget in one place, because the popup and the field have to agree
    // about focus and about which candidate is highlighted.
    void drawFactNameField(facts::Fact& f);
    // The value widget for one fact, chosen from its declared type (a checkbox
    // for a yes/no, a combo of option names for a one-of-several). One
    // function so the catalog, the rules, the scenarios and the blackboard
    // cannot each invent their own idea of what a fact's value looks like.
    bool factValueWidget(const char* id, const facts::Fact& f, float* v3);
    // Live values for the Why? explanation and the blackboard: the running
    // game's, or the catalog defaults when nothing is attached.
    bool factLiveValue(const std::string& name, float* out3) const;
    void factPushOverrides();

    // Tools > VU Programs (vu_ui.cpp, docs/vu-authoring.md).
    void drawVuProgramsWindow();
    void drawVuStageList(std::vector<VuStage>& stages, bool kernel);
    void vuRebuildPreview();
    int vuObjectsInClass(unsigned classBit) const;
    void vuSimulate();
    void drawVuLook(int look);    // the centre column for one look
    void drawVuCheck(int look);   // host run + generated .vclpp beside it
    void drawVuKernel();
    void drawVuMicroMemory(unsigned mask, unsigned needed, int fam);

    // Tools > Procedural (docs/procedural-generation.md): the scatter-graph
    // editor. One window drives every Scatter volume in the active scene -
    // graph editing, the live budget, per-instance overrides and the bake.
    void drawProceduralWindow();
    // Re-evaluates the Scatter volumes of the active scene when anything they
    // read changed, and pushes the result to the viewport. Called once per
    // frame from the viewport draw, like updateNavOverlay/updateProjectedDecals.
    void updateProcPreview();
    // Indices of the active scene's Scatter volumes, in scene order.
    std::vector<int> procVolumes() const;
    // Inserts a Scatter volume covering most of the terrain, with the starter
    // graph, and opens the Procedural window on it.
    void addScatterVolume(bool commit = true);
    // Bakes one volume (index into the active scene) or every stale volume in
    // the project, then commits. Returns the report for the status line.
    procbake::Report bakeProcVolume(int objectIndex);
    procbake::Report bakeStaleProcVolumes();
    // The project every build/export path passes to the Runner: stale scatter
    // volumes are baked first, so the console always runs the current graph.
    Project& projectForBuild();
    // The instance nearest to the camera ray through image coords (u, v), or
    // -1: the per-instance override editor's picker. Index into procResult_.
    int pickProcInstance(float u, float v) const;
    // The override row for a point key, creating it on first touch.
    ProcOverride& procOverrideFor(ProcGraph& g, uint64_t key);
    // Drops overrides that no longer change anything, so an undone tweak
    // leaves nothing behind in the .tyra.
    void pruneProcOverrides(ProcGraph& g);
    // Runs the edited volume's graph on `count` seeds and fills
    // procSeedTrials_. Synchronous - one trial is one full evaluation, which is
    // the number the readout already shows in milliseconds, so the caller can
    // tell the user what it is about to spend.
    void runProcSeedSweep(int objectIndex, int count);
    // Tools > Drone Generator (docs/drone-generator.md) - the ambient/drone
    // music tool. All of these live in droneui.cpp (the assetbrowser.cpp
    // precedent: a self-contained subsystem gets its own TU).
    void drawDroneGeneratorWindow();
    // Starts/stops live audition. Opening the device also creates the
    // LiveSynth; a machine with no sound card just gets droneAudioError_.
    void droneAudition(bool on);
    // Transport verbs. dronePlay starts at the playhead (rewinding first when it
    // is already at the end, like every DAW); droneStop parks the playhead where
    // playback actually got to.
    void dronePlay(bool record);
    void droneStop();
    // Pushes droneParams_ into the running LiveSynth. Every knob edit calls it,
    // so what you hear is always the current patch.
    void dronePushParams();
    // Kicks off the offline render on a worker thread; droneTickRender() polls
    // it each frame and writes the WAV (+ .drone sidecar) when it finishes.
    void droneStartRender(bool confirmedOverwrite = false);
    void droneTickRender();
    // Loads a .drone patch (a rendered track's sidecar, or any hand-written
    // one) into the tool. Returns false and sets droneStatus_ on a bad file.
    bool droneLoadPatch(const std::string& relOrAbs);
    // Writes the patch to a project-relative path. Returns false and stages a
    // confirmation when that file exists and is not the one already open.
    bool droneSavePatch(const std::string& rel, bool confirmed = false);
    // Back to defaults (asks first when the open patch has unsaved edits).
    void droneNewPatch();
    // Every .drone in the project, for the Open picker.
    std::vector<std::string> dronePatchList() const;
    // Rebuilds the min/max envelope the waveform strip draws from the last
    // render, so the display does not walk a million samples per frame.
    void droneBuildWaveOverview();
    // Records `value` as a keyframe at the playhead for the parameter that lives
    // at `offset` inside droneParams_. Called from the knob hook, which is what
    // makes EVERY knob automatable without its call site knowing about lanes.
    void droneWriteAuto(size_t offset, float value);
    // Whether the field at `offset` has a timeline lane - knobs draw a marker
    // and say so in their tooltip, so a value that springs back explains itself.
    bool droneIsAutomated(size_t offset) const;
    // The transport strip (position bar with ticks, playhead, click/drag seek)
    // and the Timeline tab (one editable lane per automated parameter).
    void drawDroneTimelineBar();
    void drawDroneTimelineTab();
    // The Simple view (drone_simple_ui.cpp, the default; Detailed is the rest
    // of drawDroneGeneratorWindow). Edits set `dirty` like the Detailed knobs;
    // a track switch that needs the unsaved-changes question sets openNewAsk.
    void drawDroneSimple(bool& dirty, bool& openNewAsk);
    void drawDroneDetailed(bool& dirty, bool& openNewAsk);
    // Simple | Detailed (##drone_view), drawn with its right end at `right`
    // (window coords) on row `y`. Returns its width; draw = false only
    // measures.
    float drawDroneViewSwitch(float right, float y, bool draw = true);
    // Adds On Start -> Play Music(<wav>) to a scene (Simple > In the game).
    // `replaceNode` >= 0 retargets that existing Play Music instead. One
    // commitChange.
    void droneUseInScene(int scene, int object, int replaceNode, bool createObject);
    // The playhead: the live transport while auditioning, the scrubbed position
    // otherwise. droneSeek moves both.
    double droneHeadTime() const;
    // `settle` re-establishes the sound at that position (see
    // dronegen::Synth::setTime); the intermediate frames of a drag pass false
    // so scrubbing stays cheap, and the release settles once.
    void droneSeek(double sec, bool settle = true);

    // Tools > Animation Editor (docs/animated-models.md). Non-destructive:
    // every control writes an AnimClipEdit, never the source .glb/.fbx.
    void drawAnimEditorWindow();
    // The inspector of the import row selected under "Borrowed from other
    // files" (animEdImport_, docs/animation-import.md). Returns true when it changed
    // something, which the caller turns into a commit + a cache drop - an
    // import alters what clips a model HAS, so every parse of it is stale.
    bool drawAnimImportSection(const std::string& modelRel);
    // Everything derived from a model's parse: the GlbInfo summary, the
    // viewport's baked draw and the material preview. Called after an import
    // change, since those caches all hold a clip list. With `modelRel` given
    // only THAT model's entries drop - an Apply on one character must not
    // re-bake every animated model in the project.
    void invalidateAnimCaches(const std::string& modelRel = std::string());
    // Staged donor pick for the import block; "" = nothing chosen yet.
    std::string animImpSource_;
    // Clip-list name filter (Animation Editor, left pane).
    char animEdFilter_[48] = {};
    // Everything the import panel needs to know about a (target, source) pair.
    // Answering it means parsing two models, which a panel body must not do
    // every frame - so it is computed once per pair and dropped by
    // invalidateAnimCaches() with every other parse-derived cache.
    struct AnimImportProbe {
        bool ok = false;
        std::string error;
        int clipCount = 0;
        float match = 0.0f;   // 0..1, fraction of animated bones with a home
        int bonesTotal = 0;   // donor skinning bones
        int bonesMapped = 0;  // ...that resolve to a target bone
        animmerge::RetargetInfo retarget;  // which path, gap, facing
    };
    std::map<std::string, AnimImportProbe> animProbeCache_;
    // Parsed skeletons, shared by the probe, the bone mapper and glbInfo -
    // one parse per FILE instead of one per consumer (the concrete stalls:
    // open, Add clips, Map bones). Revalidates by size+mtime, so it survives
    // invalidateAnimCaches() and still notices a re-imported asset.
    animmerge::SkelCache skelCache_;
    // `boneMap` participates in the cache key, so a row with hand-made pairs
    // reads its own numbers and a mapping commit refreshes them.
    const AnimImportProbe& animImportProbe(
        const std::string& modelRel, const std::string& sourceRel,
        const std::vector<std::pair<std::string, std::string>>& boneMap);

    // --- the bone-mapping editor (Map bones... on an import row) -----------
    // Staged entirely on the App: the two skeletons are parsed ONCE when the
    // window opens and the pair list is a working copy, committed on Apply.
    int animMapRow_ = -1;  // index into project_.animImports; -1 = closed
    bool animMapFocusNext_ = false;  // bring Map bones to the front on its next Begin
    bool animMapParsed_ = false;
    glbparser::Skel animMapTarget_, animMapDonor_;
    std::vector<float> animMapTPos_, animMapDPos_;  // bind-pose globals
    std::vector<std::pair<std::string, std::string>> animMapPairs_;  // staged
    std::vector<animmerge::BoneSuggestion> animMapSugg_;
    int animMapSelDonor_ = -1;  // selected donor node, -1 = none
    // The pair list's hover, read by the canvas NEXT frame (the list is laid
    // out after the canvas but must highlight into it - the standard one-frame
    // ImGui trick). Donor/target node indices, -1 = none.
    int animMapHiD_ = -1, animMapHiT_ = -1;
    // Canvas view: wheel zooms to the cursor, middle-drag pans - without it
    // finger and toe joints of a real rig are unclickable. Reset on open.
    float animMapZoom_ = 1.0f, animMapPanX_ = 0.0f, animMapPanY_ = 0.0f;
    // Projection of the target rig: 0 side (z, y), 1 front (x, y) - picked
    // at open by which plane shows the rig wider; the source rig always
    // gets its own widest plane (a chain seen end-on was one dot).
    int animMapTargetView_ = 0;
    // Test pose: both rigs posed through the CURRENT mapping - the check
    // that beats every percentage (docs/animation-import.md).
    int animMapPoseClip_ = -1;  // donor clip index, -1 = off
    float animMapPoseT_ = 0.0f;
    bool animMapPosePlay_ = false;
    std::vector<float> animMapDPosed_, animMapTPosed_;
    // One affix rule covering many unmatched bones (recomputed with the
    // suggestions), and the AI assist (aigen backend, one-shot).
    animmerge::AffixRule animMapAffix_;
    bool animMapAffixOk_ = false;
    std::unique_ptr<aigen::Generator> animMapAiGen_;
    std::vector<std::pair<std::string, std::string>> animMapAiSugg_;
    std::string animMapAiErr_;
    // The user's accepted pairs, machine-global (<configDir>/bone-aliases.ini)
    // - once "Oyayubi1" was mapped onto "Thumb1", the next file from the same
    // pack suggests itself. Written on Apply, loaded lazily.
    std::map<std::string, std::string> boneAliases_;
    bool boneAliasesLoaded_ = false;
    void loadBoneAliases();
    void saveBoneAliases();
    // Suggestions are recomputed only when the staged pairs change - they
    // were per-frame, which is wasted work and made the window read as busy.
    std::vector<std::pair<std::string, std::string>> animMapSuggFor_;
    bool animMapSuggValid_ = false;
    void openAnimBoneMap(int importRow);
    // Draws the window when open; returns true on an applied change.
    bool drawAnimBoneMapWindow();
    // Staged text of the row being typed into, so a Name prefix edit costs a
    // re-merge once on commit instead of once per keystroke.
    int animImpEditRow_ = -1;
    char animImpPrefix_[64] = {};
    // Preview lighting shared by the Material and Animation Editors.
    // `sel` is the stored selection (see matEdLight_): resolves it into the
    // override the viewport bakes with, and draws the combo that picks it
    // ("Scene ambience" / "Neutral studio" / every ambience preset). The combo
    // returns true when the selection changed (persist editor.ini then).
    Viewport::PreviewLight previewLight(const std::string& sel) const;
    bool previewLightCombo(const char* label, std::string& sel);
    // The edit row for (model, source clip), creating it on first touch.
    AnimClipEdit& animEditFor(const std::string& model, const std::string& clip);
    // Drops entries that no longer change anything (isDefault) so an undone
    // edit leaves no trace in the .tyra. Call after every edit commit.
    void pruneAnimEdits();
    // Retargets every reference to a clip of `model` after a rename:
    // SceneObject::animClip, the Player locomotion clips and the Animation /
    // On Animation Finished flow-node params. References store EFFECTIVE
    // names, so a rename that did not remap would silently break playback.
    void renameAnimClipRefs(const std::string& model, const std::string& from,
                            const std::string& to);
    // A Font Manager entry pointing at `relPath` ("res/fonts/x.ttf"), creating
    // one named after the file stem if none exists. Returns the entry's name -
    // what a `font` reference stores. Used by the TTF import paths.
    std::string ensureFontForPath(const std::string& relPath);
    // Renames a font and follows the reference into every text, menu and
    // Display Text node, the way HUD text renames do.
    void renameFont(int index, const std::string& newName);
    // Same for input actions / binding presets: the name is the reference key
    // (preset bindings, On Action nodes, menu rebind rows, Set Input Preset).
    void renameInputAction(int index, const std::string& newName);
    void renameInputPreset(int index, const std::string& newName);
    // The Project panel's Audio page (rail_ui.cpp): music + sounds, play,
    // the SPU2 budget, build knobs on the selected track.
    void drawAudioPage();
    std::unique_ptr<audiopreview::ClipPlayer> clipPlayer_;
    std::string railAudioSel_;
    void importMusicTrack();
    void importSoundEffect();

    // --- Asset deletion (Assets / HUD / Music / Sounds sections) -----------
    // Removes an asset from the project instead of the user hand-deleting the
    // file: a "x" button stages the asset here, drawDeleteAssetModal() asks for
    // confirmation (spelling out what still references it) and then deletes the
    // file from res/ and clears any dangling references.
    struct PendingAssetDelete {
        enum Kind { Model, Material, Music, Sound, Hud };
        Kind kind = Model;
        std::string relPath;  // project-relative file ("res/models/tree.obj")
        std::string label;    // display name shown in the dialog
        int hudIndex = -1;    // Hud only: project_.hud entry to erase
    };
    bool assetDeleteActive_ = false;      // a deletion is awaiting confirmation
    PendingAssetDelete assetDeletePending_;
    void requestAssetDelete(PendingAssetDelete::Kind kind, const std::string& relPath,
                            const std::string& label, int hudIndex = -1);
    void drawDeleteAssetModal();
    // Deletes the staged asset file from res/ and clears the project references
    // the dialog warned about (object model/material/sound paths, audio flow
    // nodes, list entries). Called on confirm.
    void performAssetDelete(const PendingAssetDelete& d);
    // Objects (across all scenes) and flow-graph nodes still pointing at the
    // staged asset - filled by countAssetUsers for the dialog, cleared on
    // confirm. objectUsers: scene objects; nodeUsers: flow-graph audio nodes.
    void countAssetUsers(const PendingAssetDelete& d, int& objectUsers,
                         int& nodeUsers) const;
    void drawSaveDataSection();
    void drawSaveDataAddButtons();
    void drawSaveEditorWindow();
    void drawMenusWindow();
    void drawGradingWindow();
    void drawAmbienceWindow();
    void drawAmbienceSkyTime(bool& changed);  // Sky & time: presets | sky | inspector
    void drawAmbienceSkyHero(AmbiencePreset& a, bool& changed);
    void drawAmbienceSimple(bool& changed);  // ambience_simple_ui.cpp
    // The "..." menu of a preset (rename, default, duplicate, delete); a delete
    // is only requested through deleteIdx and applied after the panes.
    void drawAmbiencePresetMenu(AmbiencePreset& a, int& deleteIdx, bool& changed);
    void deleteAmbiencePreset(int idx);
    void drawAmbienceInspector(AmbiencePreset& a, bool& changed);
    // Pushes the sun/moon discs at `presetIndex`'s cycle into the viewport
    // (-1 = whatever the active scene resolves to). Re-bakes the moon only when
    // its phase or texture changed.
    void updateSkyBodyPreview(int presetIndex);
    void drawCutsceneWindow();
    // Poses a copy of the active scene's objects at the Cutscene Director
    // playhead (the same interpolation the PS2 runtime uses) so the viewport
    // previews the cutscene live. Returns the raw objects unchanged when no
    // sequence preview is active. May also drive the viewport camera.
    const std::vector<SceneObject>& cutscenePosedObjects();
    // UI Editor (Tools > UI Editor): the screen stack - HUD images plus the
    // full-screen effect layers (bloom, grain, motion blur), reorderable so
    // effects can sit under the crosshair/text instead of blurring them.
    void drawUiEditorWindow();
    // One HUD element's on-screen box as the UI Editor preview hit-tests it;
    // `sel` uses the uiFxSel_ codes (0 image, 3 USE prompt, 4 text, 9 bar).
    struct HudHit {
        int sel = 0;
        int index = 0;
        ImVec2 min, max;
    };
    // Draws the HUD (images in stack order, then bars, the USE prompt and the
    // texts - the game's order) into the 512x448 frame at fMin/fSize. The
    // viewport overlay and the UI Editor preview share it. `editing` also
    // draws hidden-at-start elements (dimmed), the selected bar at its preview
    // fill and the selected element's transition preview; `clock` drives the
    // looped animations; `hits` (may be null) receives every box drawn.
    void drawHudFrame(ImDrawList* dl, ImVec2 fMin, ImVec2 fSize, bool editing,
                      float clock, std::vector<HudHit>* hits);
    // Loading Screens (Tools > Loading Screens): the boot sequence strip
    // (logo -> splashes -> loading screen -> start scene), named loading
    // screens (bg color + images + baked texts + progress bars) assignable
    // per scene, edited in place on a 2D preview driven by a load fraction.
    void drawLoadingScreenWindow();
    // Draws a loading screen into the 512x448-space frame at p0/size (bars and
    // progress animations at `fraction`, spinners on `clock` seconds);
    // `hits` (may be null) receives each element's box (HudHit sel 0 image /
    // 1 text / 2 bar / 5 animation / 6 the continue prompt). `waiting` draws
    // the press-to-continue state the game holds on after the load.
    void drawLoadingFrame(ImDrawList* dl, const LoadingScreenDef& ls, float fraction, ImVec2 p0,
                          ImVec2 size, std::vector<HudHit>* hits, bool waiting = false,
                          float clock = 0.0f);
    // Property editor for a single progress bar (returns true on change).
    bool loadingBarControls(LoadingBar& b);
    // Property editors for an animated element and for the continue prompt.
    bool loadingAnimControls(LoadingAnim& a);
    bool loadingWaitControls(LoadingScreenDef& ls);
    bool drawLoadingAssignments();
    // Cuts a sprite sheet into `count` frame PNGs (cols x rows, row-major) in
    // res/hud and appends them to `a` (docs/loading-screens.md, "Animations").
    bool importLoadingSheet(LoadingAnim& a, const std::string& src, int cols, int rows, int count);
    // Material Editor (Tools > Material Editor): authors the .mtl files the
    // whole pipeline already consumes (newmtl/Kd/map_Kd) with a live preview.
    void drawMaterialEditorWindow();
    // The unified terrain tool (Tools > Terrain Editor): Sculpt and Paint as
    // switchable modes over one shared, map-size-aware brush.
    void drawTerrainWindow();
    // Push the active scene's resolved terrain layers + per-vertex weights to
    // the viewport (two-pass splatting preview). Empty layers clear the passes.
    void rebakeSplatPreview();
    // Generate + upload a stochastic-tiling supertile for a terrain texture to
    // the viewport's cache; returns the synthetic texture key + tiling factor.
    std::string uploadStochPreview(const std::string& srcRel, float& factor);
    // load + show; modelHint (a res/models .obj) switches the preview to that
    // model - passed by the Edit... button of Model objects
    void openMaterialEditor(const std::string& relPath,
                            const std::string& modelHint = "");
    bool loadMaterialFile(const std::string& relPath);    // disk -> matEd* staging
    void saveMaterialFile();  // matEd* staging -> disk + cache invalidation
    // Copies the open .mtl (and every texture it references, so the copy is
    // paint-safe) under a fresh name and opens the duplicate.
    void duplicateMaterialAsset();
    // Texture painting on the preview mesh (see drawMaterialEditorWindow).
    bool matEdLoadPaintTarget(const std::string& texRel);  // PNG -> CPU pixels
    void matEdSavePaintTarget();  // CPU pixels -> the PNG on disk (the "bake")
    void matEdStamp(float u, float v);      // one brush splat at a surface UV
    void matEdPaintTo(float u, float v);    // stamp + gap fill from the last UV
    // OS file drop. (x, y) = where it landed in ImGui screen coords (the GLFW
    // cursor, or a --ui-script `drop` target); negative = unknown.
    void handleFileDrop(int count, const char** paths, float x = -1.0f, float y = -1.0f);
    void saveProject();

    // Editing model: mutate project_ freely, then commitChange() once per
    // logical action - it pushes an undo snapshot and marks the project dirty.
    // The project is written to disk only on demand (Save / Ctrl+S / the
    // toolbar button); there is no autosave.
    //
    // commitChange() is the ONE verb for a model edit, project-wide data
    // included. The undo snapshot only carries project_.scenes, so for a
    // project-wide collection - menus, credits, loading screens, the Input
    // Map, save values, per-asset overrides - history_.push() returns false
    // and NO undo step appears; the commit still marks the project dirty and
    // advances modelEditSerial_, which is what the collaboration / Live Link
    // diff watches. That is why committing per widget costs nothing and why a
    // panel must not reach for saveAll() instead: writing on every slider
    // release rewrites the whole project AND the history file, clears the
    // dirty flag so the toolbar save icon never lights, skips the serial bump,
    // and silently persists whatever else the user had pending.
    //
    // saveAll() is for an explicit save COMMAND (Ctrl+S, File > Save, the
    // toolbar button, the discard modal) or for an action whose file-system
    // side effect the model must match on disk (asset import). Not for a
    // widget. A `bool changed` flag accumulated over a window body is the
    // usual trigger, but it is set by hand and the next widget added forgets
    // it - so a window that owns a section pairs it with a comparison of
    // project::sectionJson() across the whole body, which cannot be forgotten.
    // EVERY project-wide panel carries that guard now (Save Editor, Menus,
    // Credits, Loading Screens, UI Editor, icons, Fonts, Input Map, Animation
    // Editor, Grading, Ambience, Cutscene Director, Prefabs) - copy the
    // nearest one rather than inventing a third answer. The only saveAll()
    // sites left are the five save commands, the three asset imports and the
    // Drone Generator's render.
    //
    // View state is NOT an edit: the render mode, the projection and the
    // active scene are read off the viewport by saveProject() and neither
    // dirty the project nor write to disk (see setViewProjection). Editor
    // state that IS stored in the .tyra but has no undo meaning - window
    // layouts, debugger breakpoints - marks dirty directly with setDirty().
    void commitChange();
    void saveAll(const char* status);
    void applySnapshot(const SceneSnapshot& s);
    void undo();
    void redo();
    // Dirty tracking: unsaved model edits since the last save. setDirty keeps
    // the window title's "*" marker in sync.
    void setDirty(bool dirty);
    void updateWindowTitle();

    // Guarded actions that would discard unsaved edits (Exit / Open / New /
    // Close). When the project is dirty they open a confirm modal instead of
    // running immediately; the modal's Save/Discard buttons then run the
    // pending one. OpenRecent carries its target in pendingRecentDir_.
    enum class PendingAction { None, Exit, Open, OpenRecent, New, Close, JoinSession };
    void requestExit();
    void requestOpenProject();
    void requestNewProject();
    void requestCloseProject();
    void performPendingAction();
    void drawDiscardModal();
    // Release the open project and go back to the state the editor boots in
    // (the Viewport becomes the welcome screen again). Never asks anything -
    // requestCloseProject() owns the unsaved-edit guard.
    void closeProject();

    // --- Recent projects ----------------------------------------------------
    // The list the welcome screen offers before any project is open, so the
    // usual next step ("carry on with what I had") is one click instead of a
    // file dialog. Machine-global state: the folders live in editor.ini, the
    // name/validity next to each is probed from disk (once per entry, at
    // startup and when the list changes - not per frame).
    struct RecentProject {
        std::string dir;     // project folder, what gets opened
        std::string name;    // display name (manifest stem, else the folder's)
        bool valid = false;  // the folder still holds a <name>.tyra manifest
        long long savedAt = 0;  // the manifest's last write (unix s), 0 = unknown
    };
    std::vector<RecentProject> recentProjects_;
    void probeRecentProject(RecentProject& r);  // fill name + valid from disk
    void rememberRecentProject(const std::string& dir);  // to the front + save
    void forgetRecentProject(int index);                 // drop it + save
    // Open one list entry, reporting a folder that moved/vanished since it was
    // probed instead of failing silently. The welcome screen's rows and the
    // File > Recent Projects menu both go through it.
    void openRecentProject(const std::string& dir);
    void requestOpenRecent(const std::string& dir);  // dirty-guarded
    void drawRecentProjectsMenu();  // the File menu's submenu
    std::string pendingRecentDir_;  // target of PendingAction::OpenRecent
    // Stage loading `dir` (a project folder, not the .tyra) across frames. The
    // first frame presents the loading cover; later stages read and attach on
    // the main thread because project::load replaces project-wide registries.
    // Single funnel for CLI argument, Open dialog and recent list.
    void openProjectAt(const std::string& dir, bool fromRecent = false);
    void projectLoadTick();
    void drawProjectLoadingScreen();
    bool projectLoading_ = false;
    bool projectLoadFromRecent_ = false;
    double projectLoadShowUntil_ = 0.0;
    std::string projectLoadDir_;
    std::string projectLoadName_;
    // openExampleCopy: the bundled example the stage-0 worker copies into
    // projectLoadDir_ before reading it, so the card is up the moment the
    // example is clicked instead of after a multi-second copy.
    std::string projectLoadCopyFrom_;
    // Overwrite an existing copy: the worker deletes projectLoadDir_ first.
    bool projectLoadReplace_ = false;
    std::atomic<bool> projectLoadCopying_{false};
    int projectLoadStage_ = 0;      // 0 = read the files, 1 = migrate + attach
    int projectLoadDrawn_ = 0;      // card frames presented since the stage began
    bool projectLoadCancel_ = false;  // honoured at the next stage boundary
    std::string projectLoadError_;  // non-empty = the card shows the failure
    std::unique_ptr<Project> projectLoadPending_;  // stage 0's result
    // Stage 0 runs project::load on this thread (projectLoadTick); the flag
    // stops it between object files. projectLoadJoin waits for it and, after
    // a cancel, restores the registries the abandoned read replaced.
    std::thread projectLoadThread_;
    std::atomic<bool> projectLoadAbort_{false};
    std::atomic<bool> projectLoadReady_{false};
    project::LoadProgress projectLoadProgress_;  // the phase list's live state (written by the thread)
    bool projectLoadResultTaken_ = true;
    std::unique_ptr<Project> projectLoadResult_;  // written by the thread
    std::string projectLoadResultErr_;            // written by the thread
    void projectLoadJoin();
    int projectLoadObjects_ = 0, projectLoadScenes_ = 0, projectLoadTerrains_ = 0;
    double projectLoadStarted_ = 0.0;
    // The start page: drawn INSTEAD of the docked layout while nothing is open
    // (drawUI keeps the dockspace alive but hidden). Recent projects on the
    // left, the bundled examples on the right, setup chips at the bottom.
    void drawWelcomeScreen();
    struct WelcomeExample {
        std::string folder;   // examples/<folder>
        std::string dir;      // absolute source folder
        std::string title;    // README H1, shortened
        std::string blurb;    // README's first sentence
        std::string tag;      // category chip
        std::string preview;  // README's first image inside the folder, or ""
        unsigned int tex = 0;
        int texW = 0, texH = 0;
        bool texTried = false;
    };
    std::vector<WelcomeExample> welcomeExamples_;
    bool welcomeExamplesScanned_ = false;
    int welcomeExampleTab_ = 0;          // 0 = Featured, 1 = All, then tags
    char welcomeRecentFilter_[96] = {};
    double welcomeProbeAt_ = -1.0;       // setup chips, re-probed every few s
    std::string welcomePcsx2_;           // auto-detected or configured, "" = none
    bool welcomeToolchainReady_ = false;
    void scanWelcomeExamples();
    // mode 0 = ask when the projects folder already has this example (the
    // "You already have it" dialog), 1 = a copy under a free name, 2 =
    // replace the existing copy.
    void openExampleCopy(const WelcomeExample& ex, int mode = 0);
    void drawExampleExistsModal();
    bool exampleExistsAsk_ = false;      // open the dialog next frame
    WelcomeExample exampleExists_;       // the example it is about
    std::string exampleExistsDir_;       // the copy already there

    // --- Collaboration session (docs/collaboration.md) ----------------------
    // Live LAN sessions: this editor hosts its open project or joins another
    // editor's. Session (session.hpp) runs the network side on a worker
    // thread; sessionTick() drains its events once per frame on the UI thread
    // and is the ONLY place session state meets project_/ImGui.
    void sessionTick();
    void requestJoinSession();  // dirty-guarded like Open/New
    void startHostSession();    // reads the session* input fields
    void closeSession();        // host: ends for everyone; client: leaves
    // Open the freshly synced remote project from its cache directory,
    // keeping the session alive across attachProject().
    void openRemoteProject(const std::string& dir);
    void drawHostSessionModal();
    void drawJoinSessionModal();
    void drawSessionEndedModal();
    void drawSessionWindow();
    void copyObject();
    void pasteObject();

    // --- Phone camera link (docs/phone-camera.md) ---------------------------
    // The phone as a viewfinder: it shows a live JPEG stream of what the
    // editor camera sees and its ARKit pose drives that camera; the Cutscene
    // Director records the move into camera keyframes. phonecam::Link owns the
    // network side on a worker thread, phoneCamTick() drains it once per frame
    // and is the ONLY place link data meets project_/ImGui - the same contract
    // sessionTick() follows.
    void phoneCamTick();
    void startPhoneCam();
    void stopPhoneCam();
    void drawPhoneCamWindow();
    // Streams the frame the viewport just rendered. Called from
    // drawViewportWindow right after render(), so the phone sees exactly the
    // editor's image (grading included) with no second scene pass.
    void phoneCamPushPreview();
    // Anchors the mapping on the CURRENT pose and aims the path along the
    // editor view - the take importer's "From view", live. Nothing else may
    // move the anchor: it is what keeps the phone's motion relative.
    void phoneCamRecenter();
    // The Camera entity the phone views from and records into (nullptr = free
    // shots, started from the editor's own viewpoint). ONE selection for both:
    // "the view from cam-1" and "the recording into cam-1" are one intent.
    const SceneObject* phoneStartCamera() const;
    void selectPhoneCamera(const std::string& name);
    // Slides the mapping's start point along the current view basis (a delta in
    // right/up/forward, scene units) - the phone's "fly the start point" mode.
    void movePhoneStart(const float delta[3]);
    // Recording into the selected sequence. Returns false (and says why in the
    // Output panel) when there is nothing to record into.
    bool startPhoneRecording();
    void stopPhoneRecording();
    // (Re)bakes the recorded buffer into the target sequence. Called live while
    // recording (throttled) so the dopesheet fills up as you move, and once
    // more on stop - which is also the only call that commits an undo step.
    void bakePhoneRecording();
    // Frames per second the built game will run at (60 NTSC / 50 PAL, from the
    // project's video system + display mode) - the "sync with project" option
    // for the keyframe density.
    float projectFrameRate() const;

    // --- Collision-aware placement (docs/object-placement.md) --------------
    // Inserted and pasted objects rest ON the surface under them (terrain or
    // another object's top) instead of sinking into it. placementSnap_ is a
    // machine-global editor setting (editor.ini) like the navigation scheme -
    // a workflow preference, not project data.
    bool placementSnap_ = true;
    // The two callbacks the placement math needs. Both read the viewport's
    // caches: it owns the parsed models and the terrain heightfield, and it
    // samples heights with the same bilinear filter the game does.
    aobake::ModelAabbFn placementModelAabb();
    placement::HeightFn placementHeight() const;
    // Objects the placement math must ignore: everything on a hidden layer,
    // plus any explicitly listed index (the object being placed itself).
    std::vector<char> placementSkip(const std::vector<int>& extra = {}) const;
    // Rests the just-appended object (project_.objects().back()) on the
    // surface under it. No-op when the snap preference is off.
    void snapInsertedObject();
    // "Drop to floor" (End): rest every selected object on the first surface
    // BELOW it, whatever the insert-snap preference says. One undo step.
    void dropSelectionToFloor();

    // --- Deferred paste (docs/object-placement.md) -------------------------
    // Ctrl+V does not drop the copies where they lie: it stages them here and
    // they follow the cursor across the viewport (snapped onto whatever is
    // under it) until a left click - or a second Ctrl+V - commits them. Esc
    // cancels. pastePending_ is what makes the staged objects render.
    bool pastePending_ = false;
    std::vector<SceneObject> pasteStaged_;  // the copies being positioned
    bool pasteMoved_ = false;               // the cursor has positioned them
    // Scene objects + the staged copies, the list the viewport renders while
    // a paste is pending (member so it isn't reallocated every frame).
    std::vector<SceneObject> pasteRenderScratch_;
    // Fill pasteStaged_ from the clipboard and start following the cursor.
    void beginPastePlacement();
    // Move the staged copies so their anchor sits at `point`, snapping the
    // group onto the surface under it when the preference is on.
    void movePasteStaged(const float point[3]);
    void commitPastePlacement();  // insert them into the scene
    void cancelPastePlacement();

    // Selection set helpers. selectedObject_ stays the "primary" (anchor) of
    // the set - always selection_.back() (or -1) - so the many single-select
    // reads keep working; selection_ carries the full multi-selection.
    void expandSelectionGroups();
    void groupSelection();
    void ungroupSelection();
    std::string selectedGroup() const;
    void selectOnly(int i, bool expandGroups = true);     // replace the selection with {i} (i<0 clears)
    void toggleSelect(int i);   // add/remove i (no-op for i<0)
    void clearSelection();
    bool isSelected(int i) const;
    void pruneSelection();      // drop indices past the object count (after undo/load)
    // Select every object whose screen bounds overlap the marquee rect (image
    // corners a..b). add = extend the current selection instead of replacing.
    void selectObjectsInBox(ImVec2 a, ImVec2 b, ImVec2 imgPos, ImVec2 avail, bool add);
    void deleteSelectedObjects();  // erase every selected object (one undo step)
    void attachProject();  // post-open: history + solution state

    GLFWwindow* window_ = nullptr;

    // UI scaling (machine-level, stored in a small global editor config file,
    // NOT in the per-project .tyra). baseStyle_ is the unscaled reference
    // captured once at init; every scale change resets to it and re-scales so
    // repeated changes never compound.
    float uiScaleUser_ = 0.0f;     // 0 == auto (match display DPI)
    float uiScaleApplied_ = 1.0f;  // effective scale currently in effect
    ImGuiStyle baseStyle_;

    // Interface theme + font (editor.ini, like the UI scale - what an editor
    // looks like is a property of the installation, not of any project).
    theme::Id theme_ = theme::kDefault;
    // The size the interface font was loaded at, or 0 when the built-in bitmap
    // font is in use. applyUiScale() restores it after resetting the style,
    // because ImGuiStyle's constructor zeroes FontSizeBase.
    float uiFontSize_ = 0.0f;
    // Which face loadUiFont() found, for the Preferences readout. Empty = the
    // built-in font (no system face resolved).
    std::string uiFontLabel_;
    // The fixed-width face for numbers and logs (ui::monoFont); empty = none
    // resolved, and those fall back to the interface font.
    std::string monoFontLabel_;

    // Viewport navigation (global editor config, see editor.ini persistence).
    NavConfig nav_;
    bool openNavigationPopup_ = false;

    // Machine-global emulator path + dev-PS2 IP (editor.ini, NOT the per-project
    // .tyra): the emulator lives at a fixed path on this PC and the dev PS2 has
    // a fixed LAN address, independent of which project is open. Edited in
    // Edit > Preferences; fed into project_ (the Runner's runtime transport) on
    // every project attach. Empty emulator path = auto-detect; empty IP disables
    // the "Run on PS2" actions.
    std::string globalEmulatorPath_;
    std::string globalPs2Ip_;
    // Docker image the game compiles in (Edit > Preferences > Build, editor.ini
    // `toolchainImage`). Empty = leave the choice to the generated compose file
    // and the project's own .env, i.e. exactly the behaviour before this setting
    // existed. See docs/toolchain-image.md.
    std::string globalToolchainImage_;
    std::string globalBuildBackend_ = "native";
    // Parent folder proposed as the location for new projects (Edit >
    // Preferences). Empty = fall back to ~/TyraProjects.
    std::string globalDefaultProjectsDir_;
    // Collaboration (editor.ini): the name other session participants see
    // (empty = the OS user name) and the remote-project cache root (empty = default).
    std::string globalDisplayName_;
    std::string globalSessionCacheDir_;
    // AI assistant backend for flow-graph generation (editor.ini; Edit >
    // Preferences > AI assistant). Model "" = the backend's default.
    aigen::Config globalAi_;

    // --- Update check (docs/updates.md, update.cpp + update_ui.cpp) ---------
    // Whether the editor asks GitHub for a newer release when it starts
    // (editor.ini, Edit > Preferences; Help > Check for updates asks
    // regardless), and one version the user has told it to stop mentioning.
    // Both are machine-global: which build is installed on this PC is not a
    // property of any project.
    bool globalUpdateCheck_ = true;
    // "Use the native title bar" (editor.ini nativeTitleBar, Edit >
    // Preferences > Appearance): off = the OS title bar is merged into our top
    // bar where platform::titleBarMergeSupported(). Read once at startup -
    // titleBarMerged_ is what THIS session actually did, so flipping the
    // preference changes nothing until the next start.
    bool nativeTitleBar_ = false;
    bool titleBarMerged_ = false;
    // "Windows can leave the main window" (editor.ini multiViewports, Edit >
    // Preferences > Appearance; multiviewport.hpp). Applied live, between
    // frames, by run().
    bool multiViewports_ = true;
    // The console session log (sessionlog.hpp): EditorConfig::consoleLogLines /
    // consoleLogFiles, and their Preferences working copies.
    int globalConsoleLogLines_ = 20000;
    int globalConsoleLogFiles_ = 10;
    int prefConsoleLogLines_ = 20000;
    int prefConsoleLogFiles_ = 10;
    std::string globalUpdateSkip_;
    // ONE worker for both jobs (the check and the download), because they are
    // never both wanted and the UI is a single modal. Everything below it is
    // written by that thread and read by the UI thread only after `done` flips
    // - the Runner/aigen idiom, no mutex.
    std::thread updateThread_;
    std::atomic<bool> updateDone_{false};
    std::atomic<bool> updateBusy_{false};
    update::Release updateRelease_;
    std::string updateError_;
    std::filesystem::path updateFile_;  // the downloaded installer
    enum class UpdateJob { None, Check, Download };
    UpdateJob updateJob_ = UpdateJob::None;
    bool updateManual_ = false;      // asked for from the menu: say so either way
    bool updateChecking_ = false;    // a check is in flight (menu item greys out)
    bool updateDownloading_ = false;
    bool updateDownloadCancelled_ = false;  // Cancel download pressed (not a failure)
    double updateRateAt_ = 0.0, updateRate_ = 0.0;  // the download's MB/s readout
    long long updateRateBytes_ = 0;
    bool openUpdatePopup_ = false;   // request to open the modal next frame
    std::string updateStatus_;       // one line under the menu item / in the modal
    // Started at startup (when the preference is on) and from Help > Check for
    // updates; updateTick() collects the answer each frame from drawUI.
    void startUpdateCheck(bool manual);
    void updateTick();
    void drawUpdateModal();
    // Downloads the installer, then closes the editor and lets it run.
    void updateDownload();
    void updateJoinWorker();
    // Selection index the orbit pivot was last snapped to; -1 = none. Lets
    // "orbit around selection" re-center only when the selection changes.
    int navFocusedIndex_ = -1;

    Project project_;
    bool hasProject_ = false;
    // Unsaved model edits since the last save (drives the title "*" and the
    // discard-confirmation guard). Layout/docking changes do not set this -
    // they fold into the .tyra only when the user saves the project.
    bool dirty_ = false;
    // How many edits the top bar's "N unsaved" counts: the undo steps between
    // the current state and the saved one (History::stepsFromSaved) plus the
    // commits to project-wide data, which make no undo step. Those are counted
    // once per widget activation, so a dragged slider is one edit, not sixty.
    // Reset (with History::markSaved) by saveAll and on open/close.
    int unsavedEditCount() const;
    void markEditsSaved();
    int projectEditsUnsaved_ = 0;
    unsigned projectEditActiveId_ = 0;  // the ImGui item the last one came from
    bool titleShowsDirty_ = false;  // last title state pushed to GLFW
    bool titleShowsJoined_ = false;  // last session-client marker in the title
    std::string titleName_;          // project name currently in the title
    // Discard guard (Exit/Open/New while dirty). pendingAction_ is what runs
    // once the user resolves the modal; exitConfirmed_ lets the main loop close
    // after a confirmed Exit without re-prompting.
    PendingAction pendingAction_ = PendingAction::None;
    bool openDiscardPopup_ = false;
    bool exitConfirmed_ = false;

    // Collaboration session state (see the method block above). The Session
    // owns the worker thread; these are the UI-side latches and input buffers.
    session::Session session_;
    // Live model sync. modelEditSerial_ is bumped by every mutation path
    // (commitChange / applySnapshot / setDirty-true); sessionTick diffs the
    // model against sessionShadow_ whenever it moves past what it last
    // scanned, and mirrors inbound edits into the same shadow (so an echo of
    // our own edit re-diffs to nothing). Seeded when a session goes live.
    session::ModelShadow sessionShadow_;
    uint64_t modelEditSerial_ = 0;
    uint64_t sessionScannedSerial_ = 0;
    bool showSessionWindow_ = false;
    bool openHostSessionPopup_ = false;
    bool openJoinSessionPopup_ = false;
    bool openSessionEndedPopup_ = false;
    bool joinModalVisible_ = false;   // Ended errors go inline while it shows
    bool sessionAttachKeep_ = false;  // openRemoteProject: don't close on attach
    char sessionName_[48] = "";
    char sessionAddr_[128] = "";
    int sessionPort_ = 7797;      // the port this editor HOSTS on
    int sessionJoinPort_ = 7797;  // the port of the host it joins (Join segment)
    char sessionCode_[16] = "";      // the code this editor hosts with
    char sessionJoinCode_[16] = "";  // the code typed / pasted to join
    std::string sessionError_;      // inline error in the join modal
    int sessionTab_ = 0;            // the Live session modal: 0 Host, 1 Join
    char sessionInvite_[256] = "";  // pasted invite / host:port (parsed live)
    double sessionStartedAt_ = 0.0; // the panel's elapsed time
    double sessionCloseArmedUntil_ = 0.0;  // "Close session" asks for a 2nd click
    std::string sessionEndedText_;  // reason shown by the session-ended popup
    std::vector<session::PeerView> sessionPeers_;
    // Presence: what each OTHER participant has selected (object ids + the
    // scene index they are editing), keyed by peer id. Rendered as per-peer
    // outlines in the viewport and dots in the object list. Our own selection
    // is broadcast throttled whenever it changes.
    struct PeerPresence {
        int scene = 0;
        std::vector<std::string> sel;  // object ids
        // Their editor camera (presence "cam", optional - an older editor
        // sends none): yaw, pitch, distance, target xyz (Viewport::camState).
        bool hasCam = false;
        float cam[6] = {0, 0, 0, 0, 0, 0};
    };
    std::map<int, PeerPresence> peerPresence_;
    std::vector<std::string> presenceSentSel_;
    int presenceSentScene_ = -1;
    float presenceSentCam_[6] = {0, 0, -1, 0, 0, 0};
    double presenceRefreshAt_ = 0.0;  // next unchanged re-send (late joiners)
    // Session panel "Follow": the peer whose camera ours rides (-1 = none),
    // and the camera sessionFollowTick last set (to notice the user moving).
    int followPeer_ = -1;
    bool followApplied_ = false;
    float followCam_[6] = {0, 0, 0, 0, 0, 0};
    void sessionFollowTick();
    static bool sameSessionCam(const float a[6], const float b[6]);
    double presenceNextSend_ = 0.0;
    // Set by attachProject()/switchLayout(): load the active layout's saved ini
    // at the next frame boundary (ImGui cannot reload settings between NewFrame
    // and EndFrame). Recipe-built layouts use recipeRebuildPending_ instead.
    bool layoutLoadPending_ = false;
    int selectedObject_ = -1;
    // Full multi-selection (indices into the active scene's objects, in click
    // order). selectedObject_ == (selection_.empty() ? -1 : selection_.back()).
    std::vector<int> selection_;
    bool selectionGroupMember_ = false;  // explicit child-row inspection
    // Rubber-band box select in progress (anchor = io.MouseClickedPos[0]).
    bool boxSelecting_ = false;
    // Click cycling: clicking the same spot again walks the objects stacked
    // under the cursor (Viewport::pickAll order) instead of re-selecting the
    // front one, which is the only way to reach something enclosed by or
    // hidden behind another with the mouse alone. The candidate list is
    // captured when the cycle STARTS and then walked - selecting something
    // moves the orbit pivot (View > Orbit around selected object), so
    // re-picking on the second click would ask a different camera a different
    // question. pickCycleLast_ is what the previous click at that spot chose;
    // the selection is not the anchor, because it may have been changed from
    // the outliner in between.
    ImVec2 pickCyclePos_{-1e9f, -1e9f};
    std::vector<int> pickCycle_;
    int pickCycleLast_ = -1;
    // Resolves a viewport click into an object index (-1 = empty space),
    // advancing the cycle. `cycled` reports that this click stepped through
    // the stack rather than starting a new pick. The image rect is passed in
    // because a comment icon is hit-tested in SCREEN space (it is drawn there
    // too, and a note far from the camera has a 3D box smaller than its icon).
    int viewportPick(float u, float v, ImVec2 mouse, ImVec2 imgPos, ImVec2 avail,
                     bool* cycled);
    // The status-bar line after a pick: the object, its place in the stack
    // under the cursor and what the next click there would select.
    std::string pickStackStatus(int hit) const;
    // The right-click menu's candidates (Viewport::pickAll order), captured
    // when the menu opens so the rows stay stable while it is up.
    std::vector<int> pickMenu_;
    // Scene-objects list filters (view state, per session - a filter that
    // outlived a restart would hide objects nobody remembers hiding).
    // sceneFilterCat_ is one of the outliner's filter-chip categories (Models,
    // Primitives, Lights... - outlinerCategory() in app.cpp), or -1 for All.
    std::string sceneFilterName_;
    int sceneFilterCat_ = -1;

    // Layouts saved before the Properties window existed lack a slot for it;
    // when set, the next frame docks it under the Project panel.
    bool dockPropertiesPending_ = false;

    // Default dock for the world tools (Terrain Editor, Ambience Editor, Color
    // Grading, Disc Layout, Project Preferences): a tool window that has no
    // place in the current layout yet is docked BESIDE the Viewport instead of
    // floating over it - Terrain into the Properties column, the wide ones
    // into one node split off the Viewport's right side. Runs in drawUI before
    // any window is submitted (dockToolWindows); a window the user has placed
    // keeps its place (ImGui settings exist). toolWasOpen_ remembers last
    // frame's flags so a window that just opened is brought to the front of
    // its node (takeToolFocus) - a shared node otherwise shows whichever
    // window is submitted first.
    void dockToolWindows(unsigned int dockspace);
    bool takeToolFocus(const char* window);
    bool toolDockReady(const char* window);
    std::set<std::string> toolFocusPending_;
    std::map<std::string, bool> toolWasOpen_;

    // Window layouts (project_.windowLayouts). A switch/rebuild is applied at a
    // frame boundary: recipeRebuildPending_ rebuilds the active layout from its
    // built-in DockBuilder recipe (empty ini) in drawUI; layoutLoadPending_
    // (above) loads a saved ini dump in the run() loop. After either, focus
    // pendingFocusWindow_ once it exists so the layout's headline panel is on top.
    bool recipeRebuildPending_ = false;
    int recipeRebuildId_ = -1;
    std::string pendingFocusWindow_;
    // New / Rename Layout modal state (name buffer shared; error under the field).
    bool openNewLayoutPopup_ = false;
    bool openRenameLayoutPopup_ = false;
    char layoutNameBuf_[64] = {0};
    std::string layoutNameError_;

    // Transform gizmo: 0 = move, 1 = rotate, 2 = scale
    int gizmoOp_ = 0;
    // Gizmo axes: 0 = absolute (world), 1 = relative to the camera
    int gizmoSpace_ = 0;
    // Object scale when a camera-space scale drag started (ImGuizmo reports
    // scale deltas cumulatively over the whole drag, not per frame)
    float gizmoDragScale0_[3] = {1.0f, 1.0f, 1.0f};
    bool gizmoWasUsing_ = false;
    // Whether the current gizmo drag has changed the anchor's transform at
    // all. A press released without motion has not, and is treated as a click
    // (it picks) rather than as an empty edit (it used to dirty the project).
    bool gizmoEdited_ = false;

    // Measuring tape (docs/world-scale.md): click two points on the scene and
    // read the distance between them, in world units and in meters. A pure
    // viewport overlay - it never touches the project.
    bool measureMode_ = false;
    int measurePoints_ = 0;  // 0 = nothing placed, 1 = start placed, 2 = frozen
    float measureA_[3] = {0.0f, 0.0f, 0.0f};
    float measureB_[3] = {0.0f, 0.0f, 0.0f};
    bool measureLive_ = false;  // the end point is following the cursor
    // Draws the tape over the viewport image (line, endpoints, readout).
    void drawMeasureOverlay(ImVec2 imgPos, ImVec2 avail);

    // --- Comments (docs/comments.md) ---------------------------------------
    // Editor notes pinned into the scene. They have no geometry: the viewport
    // skips PrimitiveType::Comment entirely and the app draws a message icon
    // over the finished image instead, which is why the icon is the same size
    // at any distance and never hides what the note is about.
    //
    // ONE function computes where those icons are (screenIcons); the overlay
    // draws them and the picker hit-tests them, so what you see is exactly
    // what a click selects - the axis-gizmo arrangement.
    struct ScreenIcon {
        int index = -1;      // into project_.objects()
        ImVec2 center{0, 0};  // screen-space centre of the bubble
        float w = 0.0f, h = 0.0f;
        ImVec2 anchor{0, 0};  // the object's own point, where the tail lands
        float depth = 0.0f;   // distance along the view axis, for ordering
        bool emitter = false;  // a particle emitter's badge (docs/particles.md)
    };
    std::vector<ScreenIcon> screenIcons(ImVec2 imgPos, ImVec2 avail);
    void drawScreenIconOverlay(ImVec2 imgPos, ImVec2 avail);
    // View > Comments. Machine-global (editor.ini), not project data: icons
    // always remain visible and clickable; this only chooses whether every
    // note's text is expanded or only the selected one's. Off by default.
    bool showCommentText_ = false;
    // Set by addComment(): the Properties note field takes the keyboard on the
    // next frame it is drawn, so a fresh note is typed rather than hunted for.
    bool commentFocus_ = false;
    // World-space size of an object as drawn: the unit primitive or the
    // model's own bounds, times its scale. False for types with no extent
    // worth quoting (markers, lights). Used by the Properties readout.
    bool objectWorldSize(const SceneObject& o, float out[3]);

    // Terrain sculpting brush
    bool sculptMode_ = false;
    float brushRadius_ = 5.0f;
    float brushStrength_ = 0.08f;  // units per frame at the brush center
    bool sculptStroke_ = false;    // an LMB stroke is in progress
    bool sculptFlatten_ = false;   // level toward flattenHeight_ instead of raise
    float flattenHeight_ = 0.0f;   // flatten target height (world units)
    // The viewport's brush card (bottom centre) as last drawn: a press inside it
    // is a click on the card, never a stroke on the ground under it.
    ImVec2 brushCardMin_{0, 0}, brushCardMax_{0, 0};

    // Terrain splat painting (docs/terrain-painting.md). Paints the active layer
    // into SceneData::splat with the same brush raycast as sculpting (mutually
    // exclusive with it). splatPreviewDirty_ requests a live composite re-bake.
    bool paintMode_ = false;
    int paintLayer_ = 0;            // active additional-layer index
    float paintStrength_ = 0.5f;    // per-stroke-step weight, 0..1
    bool paintErase_ = false;       // subtract the active layer instead of add
    bool paintStroke_ = false;      // an LMB paint stroke is in progress
    bool splatPreviewDirty_ = false;

    History history_;
    std::vector<SceneObject> clipboard_;  // copy/paste a whole selection at once

    // Flow graph editor state
    int flowGraphObject_ = -1;           // object whose graph is open in the editor
    bool flowPositionsApplied_ = false;  // node positions pushed to imnodes per graph
    float flowZoom_ = 1.0f;              // canvas zoom (imnodes emulation, 0.4-1.8)
    // The graph follows the selected object (and, while the Live Debugger is
    // halted, the graph that stopped). Remembers what it last followed so a
    // graph picked by hand stays until the selection actually changes.
    bool flowFollowSel_ = true;
    int flowFollowedSel_ = -2;
    int flowFollowedBreak_ = -1;
    // Left list "Graphs in <scene>": its width, filter text, and whether the
    // objects without logic are listed too. View state, per session.
    float flowListW_ = 0.0f;
    float flowInspW_ = 0.0f;
    std::string flowListFilter_;
    std::string flowSwitchFilter_;  // the graph switcher popup's own filter
    bool flowListAll_ = false;
    // Inspector tab while the Live Debugger is reporting (0 node, 1 live).
    int flowInspTab_ = 0;
    bool flowInspWasLive_ = false;
    // Add-node palette (Space, right-click, or a wire dropped on empty canvas):
    // the query, the category chip (-1 all), the highlighted row, where the
    // node lands, and the pin a dropped wire started at (-1 none) - the
    // palette then lists only nodes that can take it and wires the new one.
    std::string flowPalQuery_;
    int flowPalCat_ = -1;
    int flowPalSel_ = 0;
    bool flowPalOpen_ = false;
    bool flowPalFocus_ = false;
    bool flowPalMoreCats_ = false;
    ImVec2 flowPalPos_ = ImVec2(0, 0);
    int flowPalFromPin_ = -1;
    std::vector<std::string> flowRecent_;  // node type keys, newest first
    // Frame-all / select requests carried to the frame that can do them
    // (node sizes are known only after the canvas has been drawn once).
    int flowFrameAllReq_ = 0;  // frames to wait; 0 = none
    int flowSelectReq_ = -1;   // node id to select next frame
    // Live Logic per-graph capability, cached per (graph, edit serial).
    int flowCapObj_ = -1;
    uint64_t flowCapSerial_ = ~0ull;
    bool flowCapPatchable_ = true;
    std::vector<std::string> flowCapReasons_;
    // Set every frame by drawFlowGraphWindow(): the Flow Graph window (or one of
    // its children) has keyboard focus, so Ctrl+C/V copy nodes, not scene objects.
    bool flowGraphFocused_ = false;
    // Clipboard for flow-graph copy/paste: the copied nodes plus the links that
    // connect two of them (dangling links are dropped). nextId is unused.
    FlowGraph flowClipboard_;
    // imnodes editor context of the Flow Graph canvas (the Procedural graph has
    // its own - see procEditorCtx_). Panning/zoom/selection live in the editor
    // context, so two canvases need two of them.
    void* flowEditorCtx_ = nullptr;
    // Node-description tooltip (FlowNodeType::desc): node the mouse rests on
    // and since when - shown after a short delay, reset on hover change.
    int flowDescNode_ = -1;
    double flowDescSince_ = 0.0;
    // Node the per-node context menu was opened on (Live Debugger actions:
    // breakpoint, force-fire).
    int flowCtxNode_ = -1;

    // Prefabs (Tools > Prefabs). Project-wide, so the window is a plain list
    // with an index - nothing about it is per scene.
    bool showPrefabs_ = false;
    bool showParticles_ = false;
    // docs/particles.md: what viewport_.setEmitterLayers was last fed from.
    uint64_t emitterLayersSerial_ = ~0ull;
    int emitterLayersScene_ = -1;
    int particleSel_ = -1;             // selected library entry
    std::string particleStatus_;       // last bake / rename message
    int particleLayerSel_ = 0;         // 0 = the effect's main layer
    // GL previews of each layer's generated texture, one per flipbook frame.
    struct ParticleTexCache {
        ParticleTexGen recipe;
        std::vector<unsigned int> ids;
        bool valid = false;
    };
    std::vector<ParticleTexCache> particleTex_;
    std::string particleRenameFrom_;   // name while the Name field is edited
    // The window's animated 2D preview: a handful of billboards simulated with
    // the effect's own knobs (an approximation - the viewport shows the exact
    // per-kind formulas on a placed emitter).
    struct ParticlePreviewDot { float x, y, vx, vy, life, maxLife, spin; };
    std::vector<std::vector<ParticlePreviewDot>> particlePreview_;  // per layer
    unsigned particlePreviewRng_ = 1u;
    float particlePreviewAcc_ = 0.0f;
    // Preview view state (not edits): solo the selected layer, pause, layers
    // hidden by their eye (indexed like particlePreview_), inspector tab.
    bool particleSolo_ = false;
    bool particlePaused_ = false;
    std::vector<char> particleHidden_;
    int particleTab_ = 0;  // 0 Behaviour, 1 Texture, 2 Placement
    bool showVehicles_ = false;
    int vehicleSel_ = -1;  // selected definition in the Vehicle Editor
    void drawVehiclePreview(const VehicleDef& v, int modelIndex);
    std::unique_ptr<Viewport> vehiclePreview_;
    std::unique_ptr<audiopreview::EngineLoop> vehicleEnginePreview_;
    std::string vehiclePreviewKey_, vehicleAudioKey_, vehiclePreviewModel_;
    float vehiclePreviewSpeed_ = 0, vehiclePreviewSteer_ = 0, vehiclePreviewSpin_ = 0;
    float vehiclePreviewRevs_ = 0;
    float vehiclePreviewLimiter_ = 0;  // rev limiter bounce phase (vehiclesim::revLimiterStep)
    bool vehiclePreviewPlay_ = false, vehiclePreviewSound_ = false;
    bool vehiclePreviewFast_ = false;
    bool vehicleBudgetEdit_ = false;
    // Vehicle Editor view state: inspector tab (vehicle_ui.cpp's VehTab:
    // Handling, Engine & sound, Camera & HUD, Damage, Look, Model & cost), the
    // "Wheels & measures" overlay, and the wheel row the pointer is over in
    // the Model & cost tab (-1 none) - the preview rings it.
    int vehicleTab_ = 0;
    // The vehicle the "Editing: Defaults | <vehicle>" switch goes back to
    // from Defaults, and the "Show: Own only" filter.
    int vehicleScopeVehicle_ = -1;
    bool vehicleOwnOnly_ = false;
    bool vehicleShowMeasures_ = true;
    // Vehicle Editor preview: Near (tier 0) or Far (the far tier the console
    // swaps to past farDistance). View state.
    bool vehiclePreviewFar_ = false;
    // Ambience Editor > Global illumination > "Show probes in viewport":
    // the baked probe grid drawn over the scene (Viewport::setGiProbeView).
    // View state, session only.
    bool giShowProbes_ = false;
    int vehicleHoverWheel_ = -1;
    // Cached import bakes, one per definition, keyed by what the bake depends
    // on. A bake parses a .glb/.fbx and decimates it - far too slow for a
    // frame - so the window shows the last result and re-runs it only when
    // that key changes or the author asks.
    struct VehicleBakeCache {
        std::string key;          // model path + budgets + merge flag
        bool ok = false;
        std::string error;
        vehbake::Result result;
    };
    std::map<std::string, VehicleBakeCache> vehicleBakes_;  // by definition id
    // The Vehicle Editor's own undo stack. Definitions are project-wide, so
    // commitChange() dirties without pushing a step (History carries the
    // scenes alone) - and a window full of sliders needs an undo of its own.
    // The Material Editor and the Menu Editor's Style tab made the same call.
    std::vector<std::vector<VehicleDef>> vehicleUndo_;
    int vehicleUndoAt_ = -1;
    // Tools > World Facts (docs/world-facts.md). Project-wide like Prefabs, so
    // the window is a tabbed list with an index and nothing about it is per
    // scene.
    bool showWorldFacts_ = false;
    int factSel_ = -1;        // selected catalog row
    int factQuerySel_ = -1;   // selected query
    int factRuleSel_ = -1;    // selected rule
    int factScenarioSel_ = -1;
    int factFocusFrames_ = 0;  // >0: bring the window forward (Tools menu)
    int factTab_ = 0;         // Catalog / Queries / Rules / Scenarios / Blackboard
    int factTabJump_ = -1;     // a Used-by click selects that tab next frame
    int factKeepFilter_ = -1;  // catalog chip: -1 all, a Persist, 100 = computed
    float factListW_ = 0.0f;   // catalog list | detail splitter (0 = default share)
    float factSideW_ = 0.0f;   // Queries/Rules/Scenarios list | detail splitter
    std::string factSideFilter_;   // their list filter (one, shared by the three)
    std::string factBoardFilter_;  // Blackboard filter
    std::string factFilter_;  // catalog search box
    // The name a rename is being typed over, so renameFactRefs can retarget
    // every reference when the field commits - the objRenameFrom_ idiom.
    std::string factRenameFrom_, factQueryRenameFrom_;
    // Name-completion dropdown state. It lives here rather than in the draw
    // function because every part of it is read a FRAME LATER than it is
    // written: the InputText eats Enter itself, so the accept has to be
    // decided before the field is submitted, from what was true last frame.
    std::vector<std::string> factNameSuggest_;  // full strings to complete to
    int factNameSuggestSel_ = -1;    // highlighted row, -1 = none
    bool factNameSuggestOpen_ = false;
    bool factNameSuggestHover_ = false;  // the list was hovered last frame
    bool factNameActive_ = false;    // the field had keyboard focus last frame
    bool factNameCaretEnd_ = false;  // put the caret past the accepted text
    bool factNameRefocus_ = false;   // hand the keyboard back after an accept
    // Manual blackboard overrides, keyed by fact name. Held here rather than
    // in the Command so the list survives a game restart and can be edited
    // while nothing is running.
    std::map<std::string, std::array<float, 3>> factOverrides_;
    bool showVuPrograms_ = false;
    // Rebuilt every frame the VU window is open (milliseconds), so the
    // listing, the budget bar and the simulation are one answer rather than
    // three that can drift.
    std::vector<vugen::Built> vuPreview_;
    std::vector<std::string> vuPreviewLabel_;
    std::vector<std::string> vuPreviewErrors_;
    int vuPreviewSel_ = 0;
    float vuSimParams_[4] = {1.0f, 0.0f, 0.0f, 0.0f};
    float vuSimTime_ = 0.0f;
    std::string vuSimOut_;
    std::vector<int> vuSimRows_;  // per vertex: x y z (values), x y z (zero), same
    std::string vuSimErr_;
    int vuSimQ_ = 0;
    bool vuSimRan_ = false, vuSimSame_ = true;
    int vuSelKind_ = 0;  // the list: 0 a look, 1 the VU0 kernel, 2 Micro memory
    int vuSelLook_ = 0;
    int prefabSelected_ = 0;
    // The Notes field: a wrapped paragraph at rest, a multiline editor while it
    // is being typed in (ImGui's multiline InputText does not word-wrap, so the
    // editor cannot also be the reading view - see drawPrefabsWindow).
    char prefabNotesBuf_[1024] = {};
    bool prefabNotesEditing_ = false;
    bool prefabNotesFocus_ = false;
    // The last "Bake to model" result, kept on screen: what could NOT be baked
    // is the half worth reading, and a status line scrolls away. Keyed by the
    // prefab's id, because one global report drawn under every prefab reads as
    // "the last bake applied to all of them".
    prefab::BakeReport prefabBakeReport_;
    std::string prefabBakeFor_;
    // The selected prefab's bake ON DISK ("" = not baked) - what makes the
    // baked/not-baked readout survive a restart. Cached because the answer is
    // a file read: recomputed when the (id, name) key changes and after a
    // bake / Delete bake, never per frame.
    std::string prefabBakeDiskKey_;
    std::string prefabBakeDiskPath_;
    // Panel view state (not edits): the name/notes filter over the tile grid
    // and whether the member list is expanded past its first rows.
    char prefabSearch_[96] = {};
    bool prefabMembersAll_ = false;

    // Procedural scatter (Tools > Procedural). The graph editor mirrors the
    // flow-graph editor's imnodes setup (own editor context, so panning and
    // selection do not fight over one canvas).
    bool showProcedural_ = false;
    // The edited volume is addressed by ID and the index is re-resolved every
    // frame: baking and clearing insert/erase chunk objects, so an index is
    // stale the moment either runs.
    std::string procVolumeId_;
    int procVolume_ = -1;                // resolved index of procVolumeId_
    // Range of procResult_.instances that belongs to the edited volume - the
    // override picker must not hand out a point key from another volume's
    // graph (it would be stored in the wrong graph and never match again).
    int procOwnFirst_ = 0, procOwnCount_ = 0;
    bool procPositionsApplied_ = false;  // node positions pushed to imnodes
    float procZoom_ = 1.0f;
    int procPreviewNode_ = 0;  // isolate this node's output (0 = the Output node)
    int procDescNode_ = -1;    // node-description tooltip target + since when
    double procDescSince_ = 0.0;
    // The node the right-click menu belongs to. Deliberately NOT procDescNode_:
    // that one is a HOVER tracker reset to -1 on every frame the cursor is not
    // over a node, and an open popup is exactly such a frame - sharing them made
    // the menu close on the frame after it opened, i.e. right-click did nothing.
    int procCtxNode_ = -1;
    int procInspectNode_ = 0;     // the node the inspector edits (selected on the canvas)
    int procFramePending_ = 0;    // >0: frame all nodes (fit zoom, then centre)
    std::string procFrameVolume_; // the volume last framed (a switch frames again)
    ImVec2 procAddAt_ = ImVec2(0, 0);  // where the Add node menu puts its node
    std::map<std::string, int> procVolState_;      // per volume: 0 baked 1 stale 2 frozen 3 runtime
    std::map<std::string, double> procStaleSince_; // ImGui time a volume went stale (session)
    bool drawProcNodeParams(ProcNode& n, const ProcNodeType& t, const std::vector<std::string>& models);
    int drawProcInspector(SceneObject& vol, ProcGraph& g, bool stale, bool& changed,
                          const std::vector<std::string>& models);
    void procAutoArrange(ProcGraph& g, float canvasW);
    ProcGraph procClipboard_;
    void* procEditorCtx_ = nullptr;  // ImNodesEditorContext (own canvas state)
    // Evaluation. One cache per volume id (keyed so switching volumes keeps
    // both warm); procPreviewSerial_ is the modelEditSerial_ the current
    // preview was computed from - the whole invalidation rule.
    std::map<std::string, procgen::Cache> procCaches_;
    procgen::Result procResult_;   // merged preview of the active scene
    uint64_t procPreviewSerial_ = 0;
    bool procPreviewValid_ = false;
    uint64_t procPreviewVersion_ = 1;  // bumped per rebuild (viewport overlays)
    double procLastMs_ = 0.0;          // last full evaluation cost
    float procFraction_ = 1.0f;        // progressive density fraction in use
    int procInstances_ = 0, procCandidates_ = 0, procNodesRun_ = 0;
    procbake::Report procBudget_;      // estimate of the active volume's bake
    std::vector<std::string> procModels_;  // res/models .obj list (asset pickers)
    double procModelsAt_ = -1.0;           // when it was last scanned
    // Cached "is the bake stale" answer for the edited volume, recomputed only
    // when the model changes (the check hashes the terrain + every object).
    bool procStale_ = false, procStaleValid_ = false;
    uint64_t procStaleSerial_ = 0;
    std::string procStatus_;
    // Viewport interaction modes (mutually exclusive, off by default):
    // curve editing (click the ground to append/move a control point) and
    // instance overrides (click an instance, then nudge/delete it).
    int procCurveNode_ = 0;    // Curve node being edited (0 = none)
    int procCurvePoint_ = -1;  // its selected control point (-1 = append mode)
    bool procOverrideMode_ = false;
    uint64_t procSelInstance_ = 0;  // selected instance key (0 = none)
    // Seed simulator (docs/procedural-runtime.md). A runtime volume set to
    // "New world every run" builds a different world on every boot, so the one
    // seed in the graph says nothing about what a player will get. The
    // simulator runs the graph on several seeds HERE and shows what each would
    // produce; picking one previews it in the viewport.
    struct ProcSeedTrial {
        uint32_t seed = 0;
        int instances = 0;
        int triangles = 0;
        int chunks = 0;
        int warnings = 0;
        double millis = 0.0;
    };
    std::vector<ProcSeedTrial> procSeedTrials_;
    std::string procSeedTrialsFor_;  // volume id the trials belong to
    int procSeedCount_ = 8;
    // 0 = show the authored seed (the normal state). Non-zero replaces it in
    // the preview only - the graph is never edited by looking at it.
    uint32_t procSeedPreview_ = 0;

    // Viewport overlays: TV frames (PAL 4:3 and NTSC, which shows a
    // slightly wider slice of the same 512x448 buffer)
    bool showPal_ = false;
    bool showNtsc_ = false;
    bool showHudInEditor_ = false;  // HUD preview overlay (default hidden)
    // Distance-fog preview in the viewport (View menu). On by default; when off
    // the scene's fog is suppressed in the editor so distant geometry stays
    // visible. Editor-only preview toggle - does not touch the generated game.
    bool showFog_ = true;
    // Procedural preview in the viewport (View menu, and the Procedural
    // window's own tool row). On by default - a volume you cannot see is a
    // volume you cannot author - but a finished forest sits on top of whatever
    // you are editing under it, so it has to be hideable. The graph is still
    // EVALUATED while hidden: the budget readout, the warnings and the seed
    // simulator are the reason the window is open, and silently freezing them
    // would be a worse lie than the geometry being in the way.
    bool showProcPreview_ = true;

    // Endless-scroller ghost belts, hideable at two scopes. Same reason as the
    // procedural preview: a belt fills its whole ahead/behind window with
    // semi-transparent copies of its members, which is exactly what you cannot
    // see past while editing those members.
    //
    // `showScrollerPreview_` is View > Scroller preview - every belt at once.
    // `scrollGhostsOff_` holds the OBJECT IDS of individual belts turned off
    // from their own Properties, which is the scope that panel implies (a
    // checkbox inside one belt's properties that silenced every belt in the
    // scene reads as a bug, and was reported as one). Ids, not indices: the
    // object list is reordered by inserts, deletes and the procedural bake.
    //
    // Both are session state like the rest of the Preview group - the belt's
    // origin markers, the layout maths and the clone-count readout are
    // untouched either way, and nothing here reaches the .tyra or the game.
    bool showScrollerPreview_ = true;
    std::set<std::string> scrollGhostsOff_;

    // UI Editor (Tools > UI Editor): selected screen-stack entry - a HUD image
    // (uiFxSel_ == 0, index in selectedHud_), an effect layer (uiFxSel_ 1 =
    // bloom + color grading, 2 = film grain, 10 = motion blur), the pinned USE
    // prompt (3), a HUD text (4, index in selectedText_), a HUD bar (9, index
    // in selectedBar_) or a custom screen effect placement (5, index in
    // selectedFx_ into project_.screenFx). The pinned-under-the-stack effects
    // take 6 (depth of field), 7 (lens flare) and 8 (god rays). Every kind
    // needs its OWN id: the inspector is an if/else chain on uiFxSel_, so two
    // kinds sharing one opens the wrong panel.
    bool showUiEditor_ = false;
    // Where the UI Editor was last drawn, for OS file drops (handleFileDrop):
    // window and 512x448 preview frame rects (min x/y, max x/y) and the frame.
    float uiEdDropWin_[4] = {}, uiEdDropFrame_[4] = {};
    int uiEdDropSeen_ = -10;
    bool showFontManager_ = false;
    bool showInputMap_ = false;
    int inputActionSel_ = 0;  // row selected in the Input Map action list
    int inputPresetSel_ = 0;  // preset tab being edited
    // Input Map session state: the find field, the controller diagram's
    // context (0 on foot, 1 menus, 2 vehicle), and "press to set" capture
    // (0 off, 1 pad button from a host gamepad, 2 keyboard key).
    char inputFind_[64] = "";
    int inputDiagramCtx_ = 0;
    int inputCapture_ = 0;
    bool inputCaptureNoPad_ = false;
    bool inputCapturePrev_[16] = {};

    // Live Debugger panel (Tools > Debugger, the DBG toolbar chip, or the
    // built-in "Debugger" window layout).
    bool showDebugger_ = false;

    // Tree Generator (Tools > Tree Generator). The preview mesh + textures are
    // rebuilt into these on any param change; treePreviewVersion_ tells the
    // viewport when to re-upload. treeName_ is the asset base name.
    // Tools > Global Illumination (docs/global-illumination.md). The bake
    // is EXPLICIT - never part of a build - so this window is where a project
    // learns that its lighting is stale, and the one place that fixes it.
    bool showGiBake_ = false;
    // Tools > Texture Atlas (docs/texture-atlasing.md, src/atlas_ui.cpp): what
    // the packer merged with what, why a texture was refused, and the VRAM
    // arithmetic. The plan reads every candidate image off disk, so it is
    // cached and recomputed only when something that feeds it changes.
    // Tools > Static Batches (docs/static-batching.md, src/batch_ui.cpp): what
    // merged with what, what it costs in VU1 packages, and why every object
    // that is not batched is not batched. The grouping comes from
    // staticbatch::compute - the host twin of the generated
    // buildStaticBatchList - and is cached because it reads every baked
    // .tmdl in the scene.
    bool showStaticBatches_ = false;
    bool showBatchOverlay_ = false;  // View > Static batches
    bool showBatchCells_ = false;    // the selected batch's grouping cell
    bool batchDirty_ = true;
    int batchSelected_ = -1;  // focuses the overlay on one batch, -1 = all
    staticbatch::Result batchResult_;
    std::vector<std::string> batchWarnings_;
    double batchComputedAt_ = -1.0;  // ImGui time of the last compute ("updated N s ago")
    int batchListMode_ = 0;          // 0 = Batches, 1 = Drawn solo
    float batchListW_ = 0.0f;        // list | detail splitter (0 = default share)
    char batchFilter_[96] = {};      // texture or object name, substring
    void drawStaticBatchesWindow();
    void refreshStaticBatches();
    void updateBatchOverlay();
    void drawBatchExcludeCheckbox(int objectIndex);
    void drawBatchDetail();      // the selected batch: cost, members, solo neighbours
    void drawBatchSoloList();    // Drawn solo, grouped by reason, fix per group
    void drawBatchSoloRow(int objectIndex, bool compact);

    bool showTextureAtlas_ = false;
    bool atlasPlanDirty_ = true;
    texatlas::Plan atlasPlan_;
    texatlas::VramEstimate atlasVram_;
    void drawTextureAtlasWindow();
    // Page previews are composited from the plan rather than read back from
    // the bake (which lags every edit) - the map lives beside HudTexture,
    // which is declared further down.
    void rebuildAtlasPreviews();
    // The window's view state (session only): the page shown as the hero, the
    // one texture the inspector edits (a member or a refused texture, by its
    // res path), Baked/Slots, the member filter. atlasFreeSlots_ is derived
    // with the plan: per page, the free slots of its most common member size
    // still open on it (x, y, w, h in page pixels, gutter included) - the
    // "how many more fit before a second page" answer.
    int atlasPageSel_ = 0;
    std::string atlasSel_;
    std::string atlasHover_;  // the member row hovered last frame (lights its slot)
    int atlasView_ = 0;  // 0 baked, 1 slots
    char atlasFilter_[64] = {};
    std::vector<std::vector<std::array<int, 4>>> atlasFreeSlots_;
    gibake::Baker giBaker_;
    // Baked shadow decals (docs/shadows.md). Async like the two bakers around
    // it; unlike them it writes only to disk, so there is nothing to apply
    // back onto the model - the poll exists to refresh the viewport preview
    // and the staleness table.
    shadowbake::Baker shadowBaker_;
    uint64_t shadowBakeVersion_ = ~0ull;
    // What the viewport was last given: the key of everything that could have
    // changed it, and the version the viewport rebuilds its GL meshes on.
    uint64_t shadowPreviewKey_ = 0;
    uint64_t shadowPreviewVersion_ = 0;
    // Background re-bake (docs/shadows.md, "Re-baked while you edit"): with
    // the auto-bake switch on, the active scene re-bakes itself a moment after
    // the last edit. Keyed on (scene, modelEditSerial_); an edit during an
    // AUTO bake cancels it - a manual one is left to finish.
    uint64_t shadowAutoKey_ = ~0ull;
    double shadowAutoEditTime_ = 0.0;
    bool shadowAutoPending_ = false;
    bool shadowAutoRunning_ = false;
    // Pre-lit models (docs/prelit-models.md): the scene's light baked into ONE
    // object's texture, from the button in Properties. Async because the bounce
    // solve is the expensive half; the result is applied on the UI thread, so
    // the object edit goes through commitChange like every other edit.
    litbake::Baker litBaker_;
    litbake::Params litBakeParams_;
    // The active scene's pre-lit status table + the key it was computed for
    // (scene, modelEditSerial_, the bake parameters). ~0 = recompute.
    std::vector<PrelitStatus> prelitStatus_;
    uint64_t prelitStatusKey_ = ~0ull;
    // The probe grid currently uploaded to the viewport: reloaded when the
    // scene changes, the model is edited (which can stale the bake) or a bake
    // finishes.
    int giViewScene_ = -1;
    uint64_t giViewSerial_ = ~0ull;
    uint64_t giViewVersion_ = ~0ull;
    // ...and when the GI switch itself moves. Without it the preference was
    // the one GI setting the viewport ignored: gibake::load already refuses
    // to answer while GI is off, but nothing asked it again, so unticking the
    // box left the baked light on screen until the scene changed.
    int giViewEnabled_ = -1;
    uint64_t giBakerSeen_ = 0;  // last Baker version pushed to the viewport

    // Automatic model AO (docs/ambient-occlusion.md, "Model AO"). The bake is
    // a property of an ASSET, not of a scene, so it has no per-scene staleness
    // readout - it just has to be there when the viewport uploads a texture.
    // showBakedLighting_ is not a window flag: it is "show me that tab",
    // exactly like showGiBake_.
    bool showBakedLighting_ = false;
    modelao::Baker modelAoBaker_;
    uint64_t modelAoSeen_ = 0;  // last Baker version pushed to the viewport
    // What the last run was started FOR: the settings plus the per-asset
    // overrides. Re-scanning every frame would mean parsing every .obj in the
    // project every frame, so the poll starts a run when this changes and at
    // no other time (plus the panel's explicit Re-scan).
    uint64_t modelAoIntent_ = 0;

    bool showTreeGenerator_ = false;
    treegen::Params treeParams_;
    int treePreset_ = 0;
    treegen::Mesh treeMesh_;
    treegen::Image treeBarkTex_, treeLeafTex_;
    uint64_t treePreviewVersion_ = 0;
    bool treePreviewDirty_ = true;
    char treeName_[64] = "oak";
    // The name still follows the preset (oak, birch...) until it is typed in.
    bool treeNameTouched_ = false;
    int treeTab_ = 0;  // Trunk / Branches / Leaves / Look / Detail
    // Near / Impostor preview: the distant card captured on the CPU (the
    // same capture the bake runs) into treeImpTex_, redone when the tree or
    // the view count changes.
    bool treeShowImpostor_ = false;
    unsigned treeImpTex_ = 0;
    uint64_t treeImpVersion_ = 0;
    int treeImpViewsBuilt_ = 0;
    int treeImpSize_ = 128;
    float treeGenAngle_ = 40.0f, treeGenPitch_ = 18.0f, treeGenZoom_ = 1.0f;
    float treeGenPanX_ = 0.0f, treeGenPanY_ = 0.0f;  // framing radii
    bool treeGenSpin_ = true;
    bool treeGenImpostor_ = true;
    int treeImpostorViews_ = 8;
    // Properties > Distance and LOD > Distant version, staged per object until
    // its Bake button runs: the kind (0 none, 1 billboard, 2 hull proxy) and
    // the billboard's view count.
    int modelImpostorViews_ = 8;
    int modelDistantKind_ = 0;
    std::string modelImpostorObject_;
    int treeGenDisplayMode_ = 0;
    // Drone Generator (Tools > Drone Generator, docs/drone-generator.md).
    // droneParams_ is the whole patch; the LiveSynth and the audio device are
    // created lazily on the first Audition, so a session that never opens the
    // tool never touches the sound card. The render runs on droneRenderThread_
    // and hands its result over through droneRenderDone_ (Runner idiom: the UI
    // thread only ever reads the result after that flag is set).
    bool showDroneGenerator_ = false;
    dronegen::Params droneParams_;
    int dronePreset_ = 0;
    int droneTab_ = 0;
    int droneLayerTab_ = 0;  // Layers tab: the layer being edited (pill tabs)
    char droneTrackName_[64] = "ambient";
    std::unique_ptr<dronegen::LiveSynth> droneLive_;
    std::unique_ptr<audiopreview::Device> droneDevice_;
    bool droneAuditioning_ = false;
    // Transport. Generate mode is free-running sound design (it plays until you
    // stop it); Record mode is bound to the timeline - it starts at the playhead,
    // stops itself at the end of the piece, and Rec writes keyframes while it
    // runs. droneRecording_ is only true in the second case.
    int droneMode_ = 0;  // 0 = Generate, 1 = Record
    bool droneRecording_ = false;
    std::string droneAudioError_;
    std::string droneStatus_;
    std::string dronePatchTitle_;
    // The open patch, as a document: its project-relative path (empty = never
    // saved) and whether it has edits the file does not have yet.
    std::string dronePatchRel_;
    bool droneDirty_ = false;
    // Overwrite guards: the render (and a Save onto someone else's file) stage
    // their target here and raise a confirmation instead of clobbering it.
    std::string droneAskWav_, droneAskPatch_;
    std::thread droneRenderThread_;
    std::atomic<float> droneRenderProgress_{0.0f};
    std::atomic<bool> droneRenderCancel_{false};
    std::atomic<bool> droneRenderDone_{false};
    bool droneRendering_ = false;
    dronegen::RenderResult droneRenderResult_;
    dronegen::Params droneRenderedWith_;
    std::string droneRenderTarget_;  // res-relative WAV path being written
    int droneLiveRate_ = 0;          // rate the LiveSynth was built for
    std::string droneRenderAbs_;     // absolute destination, fixed at start
    // Waveform overview of the last render + the analyzer bands, both display
    // caches (never the source of truth for anything).
    std::vector<float> droneWaveMin_, droneWaveMax_;
    float droneBands_[32] = {};
    // Timeline. droneHeadSec_ is the ONE playhead truth: the position bar, the
    // waveform marker, the lane editors and the automated values the knobs
    // display all read it. droneWriteArmed_ turns any knob edit into a keyframe.
    double droneHeadSec_ = 0.0;
    bool droneWriteArmed_ = false;
    std::string droneWriteMsg_;    // "Filter cutoff @ 0:12.4" - write feedback
    char droneLaneFilter_[48] = "";  // search box of the "add lane" picker
    // The left rail's "in this project" list: every .drone under res/, its
    // title, whether its WAV exists and whether the patch is newer than the
    // WAV ("changed since last render"). Rescanned every couple of seconds
    // and right after a save/render (droneTrackScanAt_ < 0).
    struct DroneTrackRow {
        std::string rel, title;
        bool hasWav = false, newerThanWav = false;
    };
    std::vector<DroneTrackRow> droneTracks_;
    double droneTrackScanAt_ = -1.0;
    // Opening the tool on a project that already has a patch opens that
    // patch (once per project, and only over the untouched default).
    std::string droneAutoOpenFor_;
    std::string dronePendingLoad_;  // a rail click waiting on the unsaved-patch question
    int droneChordSel_ = 0;         // Harmony: the chord card being edited
    char droneTitleBuf_[96] = "";
    // The Simple view (editor.ini droneDetailed flips to the Detailed one -
    // how much machinery a person wants to see is theirs, not the project's).
    // Nothing below is saved: the mood a patch came from (session only), the
    // one-step undo a mood pick / Another take offers, a slider drag's
    // snapshot (every frame rewrites from it, so a drag never accumulates),
    // the live waveform history and the In the game questions.
    bool droneDetailed_ = false;
    int droneMoodFrom_ = -1;
    struct DroneMoodUndo {
        bool valid = false;
        dronegen::Params params;
        std::string title, what;
        int moodFrom = -1;
        bool wasDirty = false;
        double until = 0.0;
        uint64_t serial = 0;
    } droneMoodUndo_;
    uint64_t droneEditSerial_ = 0;  // bumped by every edit of the patch
    int droneDragSlider_ = -1;
    dronegen::Params droneDragP0_;
    float droneDragR0_ = 0.0f;
    float droneLoadR_[6] = {};      // slider values at the last load (double-click)
    std::string droneLoadRFor_;     // which document droneLoadR_ belongs to
    std::vector<float> droneLiveHist_;  // peak per column while playing
    bool droneScrollToGame_ = false;
    struct DroneUseAsk {
        int scene = -1, object = -1, replaceNode = -1;
        bool create = false;
        std::string text;
    } droneUseAsk_;
    void droneScanTracks();
    std::string droneUniqueTrackName(const std::string& base) const;

    int selectedHud_ = -1;
    int uiFxSel_ = 0;
    int selectedText_ = -1;
    // UI Editor > Bars (uiFxSel_ 9, index into Project::hudBars). The preview
    // fraction is editor-only: what the viewport overlay fills the selected bar
    // to, so a bar can be judged at 30% without running the game (-1 = start).
    int selectedBar_ = -1;
    float hudBarPreview_ = -1.0f;
    // UI Editor preview (session state, never saved): background (0 = the
    // viewport's last frame, 1 = checker, 2 = black), looped animations
    // running, the 90% safe-area guide, the in-preview drag (0 none, 1 move,
    // 2..5 a corner handle) and the start of a Play show / hide transition
    // preview (< 0 = none).
    int uiPrevBg_ = 0;
    bool uiPrevAnimate_ = true;
    bool uiPrevSafe_ = true;
    int uiDragMode_ = 0;
    double uiTransPlayT0_ = -1.0;
    // Font Manager selection (index into Project::fonts).
    int fontSel_ = 0;
    // The specimen and the atlas card: backdrop (0 on the scene, 1 on black,
    // 2 as PS2 = drawn glyph by glyph from the baked atlas), the editable
    // sample line, and the baked atlas itself as a GL texture. Baking walks
    // all 95 glyphs, so it reruns only when fontAtlasTexKey_ (project dir +
    // the bake-affecting fields) changes.
    int fontPreviewMode_ = 0;
    char fontSample_[96] = "Press X to look through the lens";
    unsigned fontAtlasTex_ = 0;
    std::string fontAtlasTexKey_;
    menubake::AtlasLayout fontAtlasLayout_;
    int fontAtlasKb_ = 0;
    int selectedFx_ = -1;
    // Baked preview of the selected HUD text (menubake::bakeTextRGBA),
    // re-rasterized whenever its content changes.
    unsigned textPreviewTex_ = 0;
    int textPreviewW_ = 0, textPreviewH_ = 0;
    std::string textPreviewKey_;

    // Animation Editor (Tools > Animation Editor): non-destructive per-clip
    // retiming/trim/rename of an animated model's clips. animSel* address the
    // model + SOURCE clip being edited; the staged values are read straight
    // out of project_.animClipEdits (edits commit through saveAll like the
    // other project-wide editors), and the panel owns its own playhead so
    // the preview keeps running while a field is being dragged.
    bool showAnimEditor_ = false;
    std::string animEdModel_;   // project-relative .glb/.fbx being edited
    std::string animEdClip_;    // SOURCE clip name ("" = none selected)
    float animEdTime_ = 0.0f;   // playhead, seconds into the TRIMMED clip
    bool animEdPlaying_ = true;
    bool animEdWireframe_ = false;
    bool animEdBones_ = false;  // skeleton overlay on the preview (view state)
    std::string animEdLight_;  // preview lighting, see matEdLight_
    float animEdYaw_ = 40.0f, animEdPitch_ = 15.0f, animEdZoom_ = 1.0f;
    float animEdPanX_ = 0.0f, animEdPanY_ = 0.0f;  // radius-relative target
    double animEdClock_ = 0.0;  // wall clock of the previous frame
    char animEdRename_[64] = {};  // rename field buffer for the selected clip
    // The inspector shows the selected clip, or (>= 0) the import row picked
    // under "Borrowed from other files" - index into Project::animImports.
    int animEdImport_ = -1;
    bool animEdLoopPreview_ = true;  // preview wraps (off: stops at the end)
    int animEdSpeed_ = 1;            // preview speed: 0 = 0.5x, 1 = 1x, 2 = 2x
    int animEdTimelineDrag_ = 0;     // 1 trim start, 2 trim end, 3 playhead

    // A "used by / started by / handled by" link the screen editors show: a
    // line of text and where a click goes - a menu (Menu Editor) or one
    // object's flow graph.
    struct ToolRef {
        std::string text;
        int menu = -1;
        int scene = -1, object = -1;
    };

    // Menus editing (Menu Editor window): selected menu + a live preview of
    // the baked panel (re-baked whenever the menu's content changes)
    bool showMenusEditor_ = false;
    int selectedMenu_ = -1;

    // Save Editor (Tools > Save Editor): live preview of the memory card
    // icon texture + baked-icon stats (rebuilt when any icon setting
    // changes), and the cached clip list of the picked .glb icon model.
    bool showSaveEditor_ = false;
    // ONE GL texture per animation shape of the baked icon, cycled on a timer
    // so the panel previews the motion the PS2 browser will play, not a still.
    // Rebuilt (and the old textures deleted) whenever saveIconPreviewKey_ -
    // every input the bake reads - changes.
    std::vector<unsigned> saveIconPreviewTex_;
    std::string saveIconPreviewKey_;
    // The spinner sheet, uploaded whole; the preview picks a cell with UVs and
    // cycles them, so swapping a sheet in the picker is a visible change
    // rather than something you find out about after a build.
    void drawSaveSpinnerPreview(const savebake::SpinnerInfo& spin);
    unsigned saveSpinnerPreviewTex_ = 0;
    int saveSpinnerPreviewW_ = 0, saveSpinnerPreviewH_ = 0;
    std::string saveSpinnerPreviewKey_;
    savebake::IconInfo saveIconInfo_;
    std::string saveIconClipsModel_;
    std::vector<std::string> saveIconClips_;
    // --- the Menu Editor's Style tab (docs/menu-styles.md, menustyle_ui.cpp) --
    // The staged stylesheet is what the widgets edit and what the preview bakes
    // from; the file on disk is only written by Save. Style edits get their OWN
    // undo stack - a stylesheet is not project data, so commitChange() must not
    // see them (the Material Editor made the same call).
    // The preview in three parts, because it is drawn in two windows and must
    // not become two previews: refresh owns the bake, controls the mode picker
    // and the simulated cursor, draw the image. The texture is shared - a
    // display mode changes presentation, not what is baked.
    bool menuPreviewRowUsable(const GameMenu& m, const menulayout::Layout& L,
                              int row) const;
    void menuPreviewStep(const GameMenu& m, const menulayout::Layout& L, int dir);
    void menuPreviewRefresh(const GameMenu& m);
    void drawMenuPreviewWindow();
    void drawMenuStyleTab(GameMenu& m, bool& projectChanged);
    void drawMenuStyleText(GameMenu& m, bool& projectChanged);
    void drawMenuCostCard(const GameMenu& m);
    // The preview's building blocks (menustyle_ui.cpp): one clock tick per
    // frame, this frame's transition/sheen, the panel at its texels, the
    // panel on a mock TV of one display mode (with its fit verdict).
    struct MenuMotion {
        float ofsX = 0.0f, ofsY = 0.0f, tint = 1.0f;
        const menustyle::Animation* sheen = nullptr;
    };
    struct MenuFit {
        bool fits = true;
        float panelW = 0.0f, panelH = 0.0f;  // framebuffer px in that mode
        float screenH = 0.0f;                // drawn TV height
    };
    void menuPreviewTick();
    MenuMotion menuPreviewMotion(const GameMenu& m);
    void menuPreviewSheen(const MenuMotion& mo, ImDrawList* dl, ImVec2 m0, ImVec2 m1,
                          float pxPerTexel);
    void menuPreviewPanel(const GameMenu& m, ImVec2 p0, float scale, bool outline);
    MenuFit menuPreviewTV(const GameMenu& m, const std::string& modeKey, bool wide, ImVec2 p0,
                          float sw, bool outline);
    void menuPreviewStepRow(const GameMenu& m, int dir);
    int menuPreviewTickFrame_ = -1;
    float menuPreviewCloseT_ = -1.0f;  // >= 0 = replaying the close transition
    int menuPreviewView_ = 0;  // Menu Preview: every resolution / one, large / texels
    void drawMenuStyleDeleteModal();
    std::string menuStyleDeleteKey_;  // the sheet the confirm is about
    void menuStyleSync(const GameMenu& m);
    void menuStylePush();
    void menuStyleEdited();
    menustyle::Rule& menuStyleRule(const std::string& menuScope,
                                   menustyle::Elem elem, const std::string& cls,
                                   int state);
    bool menuStyleProp(const GameMenu& m, menustyle::Elem elem,
                       const std::string& cls, int state, menustyle::Prop prop);
    bool menuStyleFileExists(const std::string& key) const;
    std::string importMenuImage(const std::string& srcPath);
    menustyle::Sheet menuStyleStaged_;
    std::string menuStyleKey_;
    bool menuStyleLoaded_ = false;
    bool menuStyleDirty_ = false;
    std::vector<menustyle::Sheet> menuStyleUndo_;
    size_t menuStyleUndoAt_ = 0;
    std::string menuStyleText_;             // canonical text of the staged sheet
    std::vector<char> menuStyleTextBuf_;    // the raw tab's edit buffer
    bool menuStyleScoped_ = false;          // write into a menu#<name> block
    std::string menuStyleClass_;            // which row class the widgets edit
    int menuPreviewRow_ = 0;                // simulated cursor row
    int menuPreviewScroll_ = 0;             // first visible row of a long list
    bool showMenuPreview_ = false;          // the standalone Menu Preview window
    // The preview PLAYS the sheet's motion, with the same formulas the runtime
    // uses - a transition you cannot see while authoring it is a transition you
    // tune by rebuilding the game.
    bool menuPreviewPlay_ = true;
    float menuPreviewClock_ = 0.0f;
    float menuPreviewOpenT_ = 1e9f;  // large = the open transition has finished
    int menuPreviewWinMode_ = 0;            // its own display mode
    float menuPreviewZoom_ = 2.0f;
    unsigned menuPreviewTex_ = 0;
    int menuPreviewW_ = 0, menuPreviewH_ = 0;
    int menuPreviewContentH_ = 0;  // drawn part (layout cached at bake time)
    bool menuPreviewClipped_ = false;  // content hit the 512px texture cap
    // Menu Editor preview: 0 = panel texels, 1.. = that supported display
    // mode (in game frame); -1 = not chosen yet (opens on the boot mode).
    int menuPreviewMode_ = -1;
    int menuPreviewLastMode_ = 0;  // the frame's mode while showing texels
    double menuPreviewBakedAt_ = -1.0;  // ImGui time of the last preview bake
    int menuEdTab_ = 0;               // Rows / Opens / Layout / Style / Stylesheet
    bool menuEdTabForce_ = true;
    std::vector<ToolRef> menuEventHandlers(const std::string& event) const;
    std::vector<ToolRef> menuOpenedBy(const std::string& menu) const;
    void drawMenuRowsTab(GameMenu& m, bool& changed);
    void drawMenuRowOptions(MenuEntry& en, bool& changed);
    void drawMenuLayoutTab(GameMenu& m, bool& changed);
    // Preview aspect: 0 = follow the project (Preferences > Widescreen), 1 =
    // force 4:3, 2 = force 16:9 - the safe-area overlay's Aspect control, for
    // the same reason. Widescreen is anamorphic, so it changes what a baked
    // panel looks like without changing a single pixel of it, and checking the
    // other case must not mean editing the project. Shared by both preview
    // surfaces: it is a question about the menu, not about the window.
    int menuPreviewAspect_ = 0;
    std::string menuPreviewKey_;  // serialized menu the texture was baked from

    // Color grading (Tools > Color Grading): selected preset + whether the
    // viewport previews grading (the edited preset wins over the default)
    bool showGradingEditor_ = false;
    int selectedGrading_ = -1;
    bool gradingPreview_ = true;
    int gradingView_ = 0;          // Color Grading preview: 0 split, 1 after, 2 before
    float gradingSplit_ = 0.5f;    // where the before/after split sits, 0..1

    // Ambience Editor (Tools > Ambience Editor): selected sky/light/fog preset
    // and whether the viewport previews the edited preset over the scene's.
    bool showAmbienceEditor_ = false;
    int selectedAmbience_ = -1;
    bool ambiencePreview_ = true;
    bool ambiencePreviewPushed_ = false;  // preset pushed to the viewport?
    // Day/night cycle discs (docs/day-night-cycle.md). The moon bake is a
    // real image projection, so it is re-run only when its inputs change -
    // this is the signature of what is currently uploaded ("" = nothing yet).
    std::string skyBodyMoonSig_;
    bool skyBodySunUploaded_ = false;
    // The generated night sky, cached against the Params it came from -
    // starfield::generate is deterministic, so re-rolling it per frame would
    // buy nothing and cost a mesh rebuild.
    std::vector<starfield::Star> skyBodyStars_;
    starfield::Params skyBodyStarParams_;
    // 1/gain of the runtime drift grade, so the previewed sky/discs/stars cancel
    // it exactly as the console does (ambience::driftCompensation).
    float skyBodyComp_[3] = {1.0f, 1.0f, 1.0f};
    // Which key row the cycle tab has selected (-1 = none).
    int selectedDayKey_ = -1;
    // Ambience Editor view state: the window tab (0 Sky & time, 1 Global
    // illumination, 2 Baked lighting), the inspector tab, and the hero's
    // Play-the-day hour (-1 = not playing; it never edits the preset).
    int ambienceTab_ = 0;
    // Sky & time shows the Simple view unless this is on (editor.ini
    // ambienceDetailed - how much of the machinery a person wants to see is
    // theirs, not the project's).
    bool ambienceDetailed_ = false;
    int ambienceInspTab_ = 0;
    float ambiencePlayHour_ = -1.0f;
    // Per-scene GI cache state (refreshGiStates, hud_ui.cpp): the GI tab's
    // scene list and the "stale" mark on its window tab. Throttled - reading a
    // cache parses the file and the signature walks the scene.
    struct GiSceneState {
        bool present = false, fresh = false;
        int atlas = 0, ground = 0, dim[3] = {0, 0, 0};
    };
    std::vector<GiSceneState> giStates_;
    double giStatesAt_ = -10.0;
    uint64_t giStatesSerial_ = ~0ull, giStatesVersion_ = ~0ull;
    void refreshGiStates(bool force);
    double giBakeStartedAt_ = 0.0;  // for the running bake's elapsed time
    float giLastBakeSecs_ = 0.0f;
    bool giWasRunning_ = false;

    // Loading Screens (Tools > Loading Screens): selected screen + selected
    // element within it (lsSelKind_: 0 image / 1 text / 2 bar / 3 the screen
    // itself / 4 the boot splash selectedSplash_ / 5 an animation / 6 the
    // continue prompt; lsSelIdx_ into that element vector) + the preview's
    // simulated load fraction, and whether it sits on "waiting for START".
    bool showLoadingEditor_ = false;
    int selectedLoadingScreen_ = -1;
    int lsSelKind_ = 3;
    int lsSelIdx_ = -1;
    float lsPreviewProgress_ = 0.65f;
    double lsPlayT0_ = -1.0;  // simulated load running since (< 0 = not)
    int lsDragMode_ = 0;      // preview drag: 0 none, 1 move, 2..5 a corner
    bool lsWaitPreview_ = false;  // Preview ended on the continue prompt
    int lsTab_ = 0;               // 0 Screens, 1 Assignments
    // Sprite-sheet import dialog state (Add > Animation from a sprite sheet).
    std::string lsSheetPath_;
    int lsSheetCols_ = 4, lsSheetRows_ = 2, lsSheetCount_ = 8;
    bool lsSheetOpen_ = false;
    int selectedSplash_ = -1;  // boot splash screen being edited (-1 = none)

    // Credits Editor (Tools > Credits Editor, docs/credits.md): the selected
    // roll and block, the preview's playhead, and the page textures the preview
    // draws - the SAME baked pages the console gets, so what scrolls here is
    // what scrolls there. crPreviewTex_ is keyed by a bake signature so an edit
    // re-uploads and nothing else does.
    bool showCreditsEditor_ = false;
    int selectedCredits_ = -1;
    int crSelBlock_ = -1;
    float crPreviewTime_ = 0.0f;
    bool crPreviewPlaying_ = false;
    // The block list's share of the window width, dragged on the splitter
    // beside it and persisted in editor.ini (machine setting, like matEdSplit_).
    float creditsSplit_ = 0.3f;
    std::vector<unsigned int> crPreviewTex_;  // one GL texture per page
    // What those textures were baked FROM: a copy of the roll (compared with
    // its operator==, so a new field can never be forgotten here the way a
    // hand-built signature string forgets one) plus the TTF paths behind its
    // fonts - repointing a Font Manager entry changes the bake without touching
    // the roll.
    CreditsRoll crPreviewRoll_;
    std::string crPreviewFonts_;
    bool crPreviewValid_ = false;
    int crPreviewPageW_ = 0, crPreviewPageH_ = 0;
    menubake::CreditsLayout crLayout_;  // geometry of the previewed roll
    // Rebuilds crPreviewTex_ from the current roll when its bake signature
    // changed; returns false when the roll cannot be baked (no usable font).
    bool creditsPreviewRefresh();
    void creditsPreviewDrop();  // frees the page textures (roll switch / close)
    // The roll's total running time in seconds, as the generated player will
    // pace it - what the window reports next to the page count.
    float creditsDuration(const CreditsRoll& r) const;
    // When block `i` of the previewed roll is centred on screen (crLayout_).
    float creditsBlockTime(const CreditsRoll& r, int i) const;
    int crTab_ = 1;               // Look / Playback / Block
    bool crTabForce_ = true;      // crTab_ changed in code: select it this frame
    bool crPreviewSeek_ = true;   // move the playhead to the first content
    bool crImportRequest_ = false;  // open the import picker outside the popup
    std::vector<ToolRef> creditsStartedBy(const std::string& roll) const;
    // Shows that object's graph in the Flow Graph window (active scene only;
    // anything else says where it is in the status bar).
    void openGraphOf(int scene, int object);
    void drawCreditsWindow();
    // Import a text file (docs/credits.md markup) into `r`, replacing its
    // blocks. Returns an error message, or "" on success.
    std::string creditsImportFile(CreditsRoll& r, const std::string& file);

    // Snapshots the track target's current static pose into a key at `time`
    // (replacing a key within 1/60 s). Used by the dopesheet buttons,
    // double-click drops and auto-key. Returns false if the target is gone.
    bool cutsceneSnapshotObjectKey(SeqTrack& tr, float time);
    // Auto-key: a finished gizmo drag drops keys at the playhead for every
    // selected object tracked by the selected sequence (seqAutoKey_).
    void cutsceneAutoKey();

    // Cutscene Director (Tools > Cutscene Director): the keyframe timeline.
    bool showCutsceneEditor_ = false;
    int selectedSequence_ = -1;   // index into project_.sequences
    int selectedSeqTrack_ = -1;   // dopesheet lane of the selected key
                                  // (-1 = camera lane, >= 0 = object track)
    int selectedSeqKey_ = -1;     // selected key within that lane (-1 = none)
    float seqPlayhead_ = 0.0f;    // scrub time in seconds
    // Opening the Director (or picking a sequence) parks the playhead on the
    // first frame with something to see - t = 0 is black inside the fade-in.
    // Display only: the saved sequence is untouched.
    bool seqOpenSeek_ = true;
    bool seqWasOpen_ = false;
    // The sequence camera (and its bars/fade) also drives the main viewport.
    // Off by default: the Director shows the shot in its own preview.
    bool seqPreview_ = false;
    bool seqPlaying_ = false;     // auto-advance the playhead (preview playback)
    bool seqAutoKey_ = false;     // gizmo release drops a key at the playhead
    float seqZoom_ = 1.0f;        // dopesheet horizontal zoom (1 = fit duration)
    bool seqCameraPushed_ = false;  // camera override handed to the viewport?
    std::vector<SceneObject> seqPosed_;  // scratch: objects posed at the playhead
    // Widescreen bars + fade preview, computed at the playhead by
    // cutscenePosedObjects() and overlaid on the viewport image.
    int seqBarsStyleNow_ = 0;     // kSeqBars* while previewing, else 0
    float seqBarsNow_ = 0.0f;     // bars slide-in envelope 0..1
    float seqFadeNow_ = 0.0f;     // fade-to-black overlay alpha 0..1
    // The shot at the playhead, resolved by cutscenePosedObjects() whether or
    // not it also drives the viewport (seqPreview_): the Director renders it
    // into its own preview (Viewport::renderShot).
    bool seqShotValid_ = false;
    float seqShotEye_[3] = {0.0f, 0.0f, 0.0f};
    float seqShotAt_[3] = {0.0f, 0.0f, -1.0f};
    float seqShotFov_ = 60.0f, seqShotRoll_ = 0.0f, seqShotShake_ = 0.0f;
    std::string seqShotCam_;      // Camera entity filming at the playhead ("" free)
    // Director layout: inspector tab (0 Key, 1 Sequence), the preview band's
    // height and the inspector's width. View state.
    int seqInspTab_ = 0;
    bool seqInspTabForce_ = false;
    float seqTopH_ = 0.0f;
    float seqInspW_ = 0.0f;
    // "Import take..." modal (camera lane): a phone-recorded 6DoF camera take
    // (src/camtake.hpp) staged for baking into free camera shots. The bake
    // preview is cached and recomputed only when a mapping control changes.
    bool seqTakeOpen_ = false;        // open the modal this frame
    CamTake seqTake_;                 // the loaded take
    std::string seqTakePath_;         // file it was loaded from
    std::string seqTakeError_;        // loader error shown in the modal
    CamTakeMapping seqTakeMap_;       // scale / yaw / origin / time / tolerance
    bool seqTakeAtPlayhead_ = false;  // key times start at the playhead
    bool seqTakeReplace_ = true;      // replace the camera track (else append)
    bool seqTakeDirty_ = true;        // re-bake the cached preview
    std::vector<SeqCameraKey> seqTakeBaked_;  // cached bake result
    CamTakeBakeStats seqTakeStats_;
    // Import target: "" = free camera shots on the camera lane; otherwise the
    // name of a Camera entity - the take bakes into that entity's transform
    // track (position + rotation) so a bound shot dollies along the path, and
    // the entity's FOV is set from the take. Two cameras in one scene each
    // take their own recording this way.
    std::string seqTakeTarget_;
    // "Adjust imported take": after an import the take + mapping stay loaded so
    // the whole path can be re-positioned / re-oriented (origin + yaw + scale)
    // in place without re-importing. Valid while it matches the open sequence.
    bool seqTakeActive_ = false;      // a re-bakeable last import exists
    int seqTakeSeqIdx_ = -1;          // sequence it was imported into
    // Applies a take to sequence s: free shots -> camera lane (replace or
    // append); a Camera entity target -> its transform track + FOV + a bound
    // camera key. Returns the first key time (for the playhead), or -1 on
    // no-op. The one-argument form uses the take-import members; the live
    // phone recording passes its own buffer through the same code, so a
    // recorded move and an imported file land identically.
    float applyCamTake(Sequence& s, bool replace, const CamTake& take,
                       const CamTakeMapping& map, const std::string& target,
                       CamTakeBakeStats& stats);
    float applyCamTake(Sequence& s, bool replace);

    // --- Phone camera link state (see the method block above) ---------------
    phonecam::Link phoneCam_;
    bool showPhoneCamWindow_ = false;
    // Machine-global settings (editor.ini).
    phonecam::PreviewPrefs phoneCamPrefs_;
    int phoneCamPort_ = (int)phonecam::kDefaultPort;
    std::string phoneCamCode_;
    bool phoneCamRequireCode_ = true;
    // The newest pose and where it maps to. phoneMap_ is a full CamTakeMapping
    // so the live view and the baked keys run through identical math
    // (mapCamSample); its `anchor` is pinned by phoneCamRecenter().
    CamTakeMapping phoneMap_;
    CamTakeSample phonePose_;
    bool phoneHasPose_ = false;
    double phonePoseAt_ = 0.0;  // ImGui time of the newest pose (staleness)
    bool phoneDrive_ = true;    // the pose drives the viewport camera
    float phoneEye_[3] = {0.0f, 0.0f, 0.0f};
    float phoneTarget_[3] = {0.0f, 0.0f, -1.0f};
    float phoneFov_ = 60.0f;
    float phoneRoll_ = 0.0f;  // live Dutch angle, degrees (see phoneMap_)
    bool phoneCamPushed_ = false;  // camera override handed to the viewport?
    double phonePreviewAt_ = 0.0;  // ImGui time of the last streamed frame
    // For the window's device row: poses per second (counted over ~1 s) and
    // metres walked since the phone connected.
    float phonePoseHz_ = 0.0f;
    double phonePoseRateAt_ = 0.0;
    int phonePoseRateN_ = 0;
    float phoneWalked_ = 0.0f;
    float phoneLastPos_[3] = {0.0f, 0.0f, 0.0f};
    // Recording. The buffer is a plain CamTake, so everything downstream is the
    // file importer's code.
    bool phoneRec_ = false;
    CamTake phoneTake_;
    int phoneRecSeq_ = -1;           // sequence index being recorded into
    std::string phoneRecTarget_;     // Camera entity ("" = free camera shots)
    double phoneRecBakedAt_ = 0.0;   // ImGui time of the last live re-bake
    CamTakeBakeStats phoneRecStats_;
    // The sequence's camera lane and duration as they were when recording
    // started. Every live re-bake restores these first and appends on top, so
    // growing the buffer never compounds keys or ratchets the duration - and
    // shots authored before the recording survive it.
    std::vector<SeqCameraKey> phoneRecBaseCam_;
    float phoneRecBaseDuration_ = 5.0f;
    float phoneRecPlayhead_ = 0.0f;  // time the first recorded key sits at
    // Keyframe density. 0 = the project's frame rate (every game frame gets a
    // key - exact, but the biggest table), 1 = a custom rate, 2 = no fixed
    // rate at all: decimate by the take importer's error tolerance instead.
    int phoneDensityMode_ = 1;
    float phoneDensity_ = 10.0f;   // keys per second (mode 1)
    float phoneTolerance_ = 0.05f; // world-unit error bound (mode 2)
    bool phoneRecAtPlayhead_ = false;  // keys start at the playhead, not at 0
    // "From view" for the take import: set the mapping origin to the preview
    // camera's position AND the mapping yaw so the take's first sample looks
    // where the editor camera looks (aim the recorded path along the view).
    void takeOriginAimFromView();

    // Look-through camera: the viewport renders from this Camera entity's
    // pose + FOV ("" = free orbit camera). Editor-side state, not persisted;
    // the Cutscene Director camera-track preview takes precedence.
    std::string lookThroughCam_;

    // Material Editor (Tools > Material Editor). Materials are the project's
    // .mtl asset files, edited in place: matEdMats_ stages the open file's
    // entries (color/brightness split out of Kd for the UI), every committed
    // edit rewrites the file and invalidates the caches - the scene viewport
    // updates live. Not project data, so no undo history (same as imports).
    bool showMaterialEditor_ = false;
    bool showTerrainEditor_ = false;  // the unified Sculpt + Paint terrain tool
    // Terrain Editor layer coverage: the share of the ground each layer
    // VISIBLY covers (top of the stack wins), from the splat weights.
    // Recomputed at most twice a second - a 512x512 four-layer map is a
    // million bytes to walk.
    std::vector<float> terrainCoverage_;
    double terrainCoverageAt_ = -1.0;
    int terrainCoverageScene_ = -1;
    bool terrainBaseOpen_ = false;  // the Base row's "Change" popup asked to open
    ImGuiTextFilter matEdFilter_;
    bool matEdFocusNext_ = false;
    std::string matEdInfoPath_;
    std::filesystem::file_time_type matEdInfoTime_{};
    int matEdInfoW_ = 0, matEdInfoH_ = 0;
    bool matEdInfoOk_ = false;
    std::string matEdPath_;  // project-relative path of the open .mtl ("" = none)
    struct MatEdEntry {
        std::string name;
        float color[3] = {1.0f, 1.0f, 1.0f};
        float brightness = 1.0f;  // folded into Kd on save (see saveMaterialFile)
        std::string texture;      // map_Kd, relative to the .mtl dir ("" = none)
        float tile = 1.0f;        // map_Kd -s: texture repeats per world unit
                                  // (terrain only; objects have baked UVs)
        std::string refl;           // refl sphere map, relative to the .mtl dir
                                    // ("" = not reflective)
        float reflStrength = 0.5f;  // refl -mm gain operand, 0..1
        bool reflRounded = false;   // refl -rounded: centroid-radial env
                                    // normals (flat faces get a gradient)
        // Emission (docs/emissive-materials.md): the surface never renders
        // darker than the emission color, so it stays lit in total darkness.
        // Saved as a standard "Ke r g b" = the RESOLVED emission (see
        // matEdKe); the authored controls ride in a "# tyra-glow <strength>
        // <r> <g> <b> <white>" hint so the split round-trips exactly, the way
        // "# tyra-brightness" does for Kd.
        float glow = 0.0f;  // 0 = matte; 1 = fully self-lit; up to 2 overbright
        float glowColor[3] = {1.0f, 1.0f, 1.0f};
        // White-hot core: added to every channel, so the surface desaturates
        // toward white the way an overexposed emitter does on camera. The ONLY
        // way an untextured emissive surface can read brighter - it is already
        // at the framebuffer maximum in its own hue at glow 1.
        float glowWhite = 0.0f;
        // "Lights up surroundings" (docs/emissive-materials.md step 2): the
        // emitter shape is baked into the light of the geometry around it.
        // 0 = lights nothing.
        float glowRange = 0.0f;
        float glowLight = 1.0f;
        std::vector<std::string> extra;  // unrecognized lines, preserved verbatim
    };
    // The resolved emission of one entry: glowColor x glow with the white-hot
    // core added on every channel, capped at the 1.99 the PS2 color byte can
    // carry. This is what lands in "Ke" and what every renderer (game bake,
    // viewport, material preview) consumes - one definition, so the file and
    // the previews can never disagree.
    static void matEdKe(const MatEdEntry& e, float out[3]);
    std::vector<MatEdEntry> matEdMats_;
    int matEdSel_ = 0;         // selected entry within the file
    int matEdShape_ = 1;       // preview: 0 box, 1 sphere, 2 cylinder, 3 cone,
                               // 4 = the .obj in matEdModel_
    std::string matEdModel_;   // res/models .obj shown when matEdShape_ == 4
    bool matEdSpin_ = true;    // turntable; an orbit drag unchecks it (the
                               // hand wins - re-tick to resume spinning)
    float matEdAngle_ = 40.0f;
    float matEdPitch_ = 30.0f;  // camera elevation (drag up/down on preview)
    float matEdZoom_ = 1.0f;    // mouse-wheel dolly on the preview
    float matEdPanX_ = 0.0f, matEdPanY_ = 0.0f;  // middle-drag pan, framing radii
    // Preview panel's share of the window width (the draggable splitter
    // between the property column and the preview; editor.ini, machine
    // setting like uiScale).
    float matEdSplit_ = 0.48f;
    // Lighting the preview bakes with: "" = the scene's ambience (default),
    // "*" = the neutral studio light, anything else = an ambience preset name.
    // A dark scene otherwise makes its own previews unreadable. editor.ini,
    // machine setting like matEdSplit_. See previewLight/previewLightCombo.
    std::string matEdLight_;
    bool openNewMaterialPopup_ = false;
    char matEdNewName_[64] = "my-material";
    std::string matEdNewError_;

    // Texture painting (Material Editor preview). Strokes splat into a CPU
    // RGBA copy of the selected entry's texture, live-uploaded into the
    // shared GL texture each frame; releasing the mouse writes the PNG back
    // to disk - painting IS the bake, the flat texture is what ships (texbake
    // still quantizes at build like any other PNG). Asset edits, so no
    // project undo - a small per-stroke snapshot stack covers mistakes.
    bool matEdPaint_ = false;          // paint mode toggle
    // The inspector's tab: 0 Look, 1 Paint (arms the brush - matEdPaint_
    // follows it), 2 Bake.
    int matEdTab_ = 0;
    // The material list's second line per .mtl (its first texture, or the
    // entry count), keyed by path and refreshed when the file's time moves.
    std::map<std::string, std::pair<long long, std::string>> matEdListInfo_;
    int matEdBrushMode_ = 0;           // 0 = color, 1 = brush image, 2 = eraser
    float matEdBrushColor_[3] = {0.8f, 0.2f, 0.15f};
    float matEdBrushSize_ = 24.0f;     // radius in texture pixels
    float matEdBrushOpacity_ = 1.0f;
    // Random per-dab opacity variation, 0-100%: each dab's opacity is
    // reduced by up to this fraction (organic, hand-worn strokes).
    float matEdBrushOpacityVary_ = 0.0f;
    std::string matEdBrush_;           // active brush: res/brushes/<x>.png
    // Stamp spacing, % of the brush diameter between stamps along a stroke
    // (GIMP semantics): low = a continuous line, >=100 = separate stamps.
    float matEdBrushSpacing_ = 25.0f;
    float matEdBrushAngle_ = 0.0f;     // dab rotation, degrees (brush mode)
    bool matEdBrushRandomRot_ = false; // re-roll the rotation per dab
    unsigned matEdRng_ = 22695477u;    // per-dab random rotation state
    // Live dab preview: each hovered frame composites one UNCOMMITTED dab
    // under the cursor (the active layer is backed up and restored right
    // after), so you see where and how the stamp lands before clicking.
    bool matEdGhostOn_ = true;
    bool matEdGhostShown_ = false;  // composite currently holds a ghost dab
    bool matEdGhostPass_ = false;   // stamping the ghost: fixed angle, no roll
    std::string matEdPaintTexRel_;     // project-relative path of the loaded target
    // Composite of the layer stack = the pixels the PNG on disk holds (what
    // the PS2 loads). Layers are editor-side: painted strokes land on the
    // ACTIVE layer, the composite is rebuilt after every change and the
    // stack persists in a `<texture>.layers/` sidecar next to the PNG
    // (skipped by texbake, so it never ships). One Background layer =
    // no sidecar - untouched textures stay plain files.
    std::vector<unsigned char> matEdPaintPixels_;  // RGBA composite, W*H*4
    int matEdPaintW_ = 0, matEdPaintH_ = 0;
    struct MatEdLayer {
        std::string name = "Layer";
        int blend = 0;         // 0 normal, 1 multiply, 2 add, 3 overlay
        float opacity = 1.0f;
        bool visible = true;
        std::vector<unsigned char> pixels;  // RGBA, W*H*4 (straight alpha)
        // Smart mask (docs/material-baking.md): when genOn, the pixels are
        // GENERATED - genColor filled through a matbake::generateMask alpha
        // driven by the baked maps. Regenerated live as the bake refines and
        // whenever a parameter changes; hand-painting on such a layer is
        // overwritten by the next regeneration. Params persist in the
        // layers.json sidecar and in .matpreset files.
        bool genOn = false;
        matbake::MaskParams gen;
        float genColor[3] = {0.24f, 0.16f, 0.10f};
    };
    std::vector<MatEdLayer> matEdLayers_;  // bottom-up; [0] = Background
    int matEdActiveLayer_ = 0;
    // layers -> matEdPaintPixels_ + live GL upload of the shared texture
    void matEdComposite();
    // sidecar write: `<tex>.layers/layers.json` + layer PNGs; a single
    // Background layer removes the sidecar instead (keep projects clean)
    void matEdSaveLayers();
    std::filesystem::path matEdLayersDirAbs() const;
    bool matEdStroke_ = false;         // LMB stroke in progress
    float matEdLastUV_[2] = {0, 0};    // previous sample point on the surface
    bool matEdHaveLastUV_ = false;
    float matEdStampResidual_ = 0.0f;  // px travelled since the last stamp
    // The Material Editor's own undo (Ctrl+Z while the window is focused, or
    // the Undo button): one stack of paint strokes, layer add/remove and
    // committed property edits, in order. Separate from the project history -
    // materials are assets, their edits go straight to disk.
    struct MatEdUndoStep {
        enum class Kind { Paint, Props, LayerAdd, LayerRemove };
        Kind kind = Kind::Paint;
        std::string texRel;  // paint target the step belongs to (paint/layer)
        int layer = 0;       // Paint: painted layer; LayerAdd/Remove: index
        std::vector<unsigned char> pixels;  // Paint: layer texels before it
        MatEdLayer layerData;               // LayerRemove: the removed layer
        std::vector<MatEdEntry> mats;       // Props: entries before the edit
        int sel = 0;
    };
    std::vector<MatEdUndoStep> matEdUndo_;
    std::vector<MatEdEntry> matEdPrevMats_;  // entries as of the last save/undo push
    bool matEdFocused_ = false;  // window focus last frame (routes Ctrl+Z)
    // removed: the layer being deleted (LayerRemove steps only)
    void matEdPushUndo(MatEdUndoStep::Kind kind, int layer = 0,
                       const MatEdLayer* removed = nullptr);
    void matEdUndoLast();
    std::vector<unsigned char> matEdPatternPixels_;  // decoded pattern cache
    int matEdPatternW_ = 0, matEdPatternH_ = 0;
    std::string matEdPatternLoaded_;   // path matEdPatternPixels_ came from

    // Map baking (docs/material-baking.md): matbake's progressive UV-space
    // raytraced bake of the preview mesh - AO, bent normals, thickness,
    // curvature, position, OS normals in one pass. Previewed live on the
    // material (AO multiplied over the composite at GL-upload time only -
    // the saved PNG never contains the preview) or as a raw map view, and
    // applied as a "Baked AO" multiply layer on the entry's texture. Params
    // persist per .mtl via a "# tyra-bake" hint line.
    matbake::Baker matBaker_;
    int matBakePreviewMode_ = 0;  // 0 off, 1 AO over material, 2 raw map view
    int matBakeMapView_ = 0;      // map view: 0 ao 1 curvature 2 thickness
                                  // 3 bent 4 normal 5 position
    int matBakeSizeIdx_ = 2;      // bake resolution: 64 << idx
    int matBakeRays_ = 64;        // rays per texel at full quality
    float matBakeMaxDist_ = 0.0f; // occlusion reach (0 = auto: half the AABB
                                  // diagonal)
    int matBakeSSIdx_ = 1;        // supersample grid: 1 << idx per axis
    bool matBakeBackface_ = true; // back-side hits occlude (thin geometry)
    int matBakePadding_ = 4;      // dilate ring, texels
    int matBakeSeed_ = 1;
    std::string matBakeHigh_;     // high-poly .obj, project-relative ("" = none)
    float matBakeCage_ = 0.0f;    // cage offset (0 = auto: 2% of the diagonal)
    uint64_t matBakeStartedSig_ = 0;  // input signature of the running bake
    uint64_t matBakeSeenVersion_ = 0;
    matbake::Maps matBakeMaps_;       // latest snapshot
    bool matBakeApplyWhenDone_ = false;  // "Bake & add layer" pending
    std::string matBakeApplyEntry_;   // entry the pending apply was armed
                                      // for - switching entries cancels it
    bool matBakeRunOnce_ = false;  // smart masks asked for maps (no preview)
    // cached mesh inputs (rebuilding objparser loads per slider tick would
    // thrash disk; keys carry the file mtimes so external edits re-bake)
    matbake::MeshInput matBakeMeshLow_, matBakeMeshHigh_;
    std::string matBakeMeshKey_, matBakeHighKey_;
    std::string matBakeMeshError_;
    void matBakeResetParams();    // defaults (new/awaiting-hint files)
    matbake::Params matBakeParams() const;
    bool matBakeBuildMeshes(const std::string& entryName);
    void matBakeTick(const std::string& entryName, const std::string& texRel);
    void matBakeUploadSolo();     // selected raw map -> "@matbake-view"
    void matBakeApplyLayer();     // AO -> "Baked AO" multiply layer + save
    void matBakeSaveMaps(const std::filesystem::path& mtlDirAbs,
                         const std::string& entryName);
    void matEdBakeSection(const std::string& entryName,
                          const std::string& texRel);
    // composite -> GL upload; multiplies the AO preview in at upload time
    void matEdUploadComposite();
    // Auto-creates a paintable texture for the selected entry when it has
    // none ("<entry>-tex.png", 256^2 white, next to the .mtl), assigns it,
    // saves the file and loads it as the paint target. The one-click
    // enabler for masks/presets/bakes on a fresh multi-part model. Returns
    // true when a loaded paint target exists afterwards.
    bool matEdEnsurePaintTexture();
    // Drops the loaded paint target (pixels, layers, stroke/ghost state).
    // Called when the selected entry has NO texture - a stale target from
    // the previous entry must never receive bake previews or layers.
    void matEdUnloadPaintTarget();

    // UV inspection (docs/material-painting.md): preview display mode and
    // the 2D UV-layout panel with two-way hover sync (hover a face in 3D -
    // its texture region lights up in the panel; hover the panel - the
    // triangle is outlined on the mesh). The panel reuses the bake's cached
    // mesh input (matBakeMeshLow_).
    int matEdDisplayMode_ = 0;  // 0 solid, 1 +wireframe, 2 UV checker
    bool matEdUvView_ = false;  // UV layout panel under the 3D preview
    float matEdUvZoom_ = 1.0f;
    float matEdUvPan_[2] = {0.0f, 0.0f};
    int matEdUvHoverTri_ = -1;          // panel-hovered triangle (last frame)
    bool matEd3dHoverValid_ = false;    // 3D cursor rests on a paintable face
    float matEd3dHoverUV_[2] = {0, 0};  // its surface UV (marked in the panel)
    // Highlighted triangles (UV validator): outlined red in both views.
    std::vector<int> matEdUvIssueTris_;
    void drawMatEdUvPanel(const std::string& entryName,
                          const std::string& texRel, const ImVec2& size);

    // UV validator: overlaps, out-of-range, flipped/degenerate triangles,
    // texel-density outliers over the preview mesh's paintable UVs. Results
    // are pinned to the mesh key they were computed for; clicking a finding
    // highlights its triangle(s) in the UV panel and on the mesh.
    std::vector<matbake::UvIssue> matEdUvIssues_;
    std::string matEdUvIssuesKey_;  // matBakeMeshKey_ at validation time
    int matEdUvIssueSel_ = -1;
    void matEdUvValidateSection(const std::string& entryName);

    // Automatic UV unwrap (uvunwrap.hpp): rewrites the preview .obj with
    // smart-project charts. Static .obj models only; the modal warns that
    // the previous UVs are replaced (projects are git repos - revert there).
    bool openUnwrapPopup_ = false;
    float unwrapAngle_ = 55.0f;  // chart-growing angle threshold
    int unwrapMargin_ = 4;       // chart spacing, px at the bake resolution

    // "PS2 CLUT" display mode (matEdDisplayMode_ == 3): the composite is
    // palette-quantized through the same median-cut quantizer texbake ships
    // and uploaded in place of the texture (GL-only, like the AO preview -
    // the PNG on disk never changes). Palette size follows the shipped
    // policy by default; dithering is selectable for comparison. The last
    // palette is kept for the swatch strip.
    int matEdPs2Mode_ = 0;    // 0 follow policy, 1 = 16, 2 = 256, 3 = full
    int matEdPs2Dither_ = 0;  // pngquant::Dither
    std::vector<unsigned char> matEdPs2Palette_;
    // colors the preview quantizes to (0 = full color): the explicit combo
    // choice, or the .mtl's per-asset override / project default
    int matEdPs2Colors() const;
    // "128x128 4-bit = 8 KB + 64 B palette" for the budget line
    std::string matEdBudgetLine(int tw, int th) const;

    // Smart masks + presets (docs/material-baking.md). Regeneration fills a
    // gen layer's pixels from the current bake maps; regen-all runs after
    // every bake snapshot and after a paint-target load, so the masks track
    // the bake live.
    void matEdRegenLayer(MatEdLayer& l);
    void matEdRegenMasks();  // all genOn layers + composite (no disk write)
    bool matEdAnyGenLayer() const;
    void matEdGenControls();  // generator UI for the active layer
    // a layer row's thumbnail: its pixels over a checker, `s` px square
    void matEdLayerThumb(ImDrawList* dl, const MatEdLayer& L, ImVec2 at, float s) const;
    // Presets: the gen-enabled layers' parameters as a reusable JSON under
    // <project>/material-presets/ (project dir, never ships).
    void matEdSavePreset(const std::string& name);
    bool matEdApplyPreset(const std::string& relName);
    bool openSavePresetPopup_ = false;
    char matEdPresetName_[64] = "worn-metal";
    std::string matEdPresetError_;
    // preview-mesh stats line ("12,345 tris - 6,789 verts - UVs ok"),
    // recomputed only when the cached bake mesh changes
    std::string matEdStatsLine_;
    std::string matEdStatsKey_;
    bool matEdStatsWarn_ = false;  // mesh has no usable UVs

    // Texture hot reload (docs/live-link.md): every saved paint target is
    // re-baked into bin/<rel> in the format the build shipped and announced
    // via bin/livetex.bin; the generated live_tex poller re-uploads the
    // pixels into the running game's existing GS VRAM allocation. Paint in
    // the editor, watch the texture change on the console.
    // One paint may have SEVERAL shipped paths (an animated model's .tskl
    // renames its textures), so each record carries the paint it belongs to -
    // the game reports a failed reload per group, not per path.
    struct LiveTexRec {
        uint32_t gen = 0;    // grows per repaint; the poller applies unseen
        uint32_t group = 0;  // the paint that announced it
    };
    std::map<std::string, LiveTexRec> liveTexGen_;  // game-relative -> record
    uint32_t liveTexSeq_ = 0;
    uint32_t liveTexGroup_ = 0;
    void liveTexNotify(const std::string& texResRel);
    // "New texture" modal (paintable blank PNG next to the .mtl)
    bool openNewTexturePopup_ = false;
    char matEdNewTexName_[64] = "";
    int matEdNewTexSize_ = 2;          // index: 64/128/256/512
    float matEdNewTexColor_[3] = {1.0f, 1.0f, 1.0f};
    std::string matEdNewTexError_;
    struct HudTexture {
        unsigned tex = 0;
        int w = 0, h = 0;
    };
    std::map<std::string, HudTexture> hudTexCache_;
    const HudTexture* hudTexture(const std::string& relPath);
    // Texture Atlas page previews, composited from the plan (see
    // rebuildAtlasPreviews): keyed by page index, rebuilt with the plan.
    std::map<int, HudTexture> atlasPagePreview_;
    // The generated drawing of a built-in text icon as a GL texture. Lets the
    // Button icons manager preview an icon whose PNG the project has not baked
    // yet, and show what "restore default" gives back. Null for a name that is
    // not one of the built-ins.
    const HudTexture* builtinIconTexture(const std::string& iconName);
    // Puts one icon back to its built-in state: default path + scale, and the
    // generated PNG deleted so the next build (and the preview) redraws it.
    void restoreDefaultTextIcon(TextIcon& icon);
    // A "{{ }}" button next to a text field: opens the list of placeholders this
    // project understands (actions first - they follow the binding - then the
    // icons), each with its glyph, and appends the chosen one. This is the
    // legend for the placeholder syntax as much as it is an insert helper.
    // Returns true when it changed `text`.
    bool textTokenPicker(const char* id, std::string& text);
    // Texture-bake controls (pow2 size + quantization) shared by HUD images
    // and the USE prompt in the UI Editor. Returns true on change.
    bool hudBakeControls(HudImage& h);
    // The shared "Motion" block (loop + show/hide transition) every HUD
    // element's property panel ends with, and the optional-image picker a bar
    // uses twice (fill, frame). Both return true on a change.
    bool hudMotionControls(HudAnim& anim, HudTransition& trans, bool* visibleAtStart);
    bool hudBarImageControls(const char* id, const char* title, HudImage& img,
                             bool withSize);
    // Renames a HUD element's name in every flow node that references it by
    // that kind (Set HUD Element Visible / Play HUD Effect / Set HUD Bar).
    void renameHudElementRefs(const std::string& from, const std::string& to,
                              bool isBar);
    // The embedded built-in USE prompt sprite (viewport overlay preview).
    const HudTexture* builtinUseTexture();
    HudTexture builtinUseTex_;
    // Built-in text-icon drawings as GL textures, so the Button icons manager
    // previews an icon before its PNG exists (they are generated at the first
    // build). Keyed by icon name; cleared with the decoded-icon cache.
    std::map<std::string, HudTexture> builtinIconTex_;
    // Viewport overlay textures of the HUD texts, re-baked on content change.
    struct TextTexture {
        unsigned tex = 0;
        std::string key;  // content the texture was baked from
        HudTexture hud;
    };
    std::map<std::string, TextTexture> textTexCache_;
    const HudTexture* hudTextTexture(const HudText& t);


    Viewport viewport_;
    Runner runner_;

    // Projected-decal preview: world-space conforming meshes (decalproj) for the
    // active scene's projecting decals, keyed by object id (pos3+uv2 per vertex).
    // Recomputed only when a cheap signature of the scene changes, then pushed to
    // the viewport with a bumped version (see updateProjectedDecals).
    std::map<std::string, std::vector<float>> projectedDecals_;
    uint64_t projectedDecalsSig_ = 0;
    uint64_t projectedDecalsVersion_ = 0;
    void updateProjectedDecals();

    // Nav-mesh overlay (View > Nav Mesh Overlay): the active scene's baked
    // walkable grid, recomputed only when its inputs change (same signature
    // trick as the projected decals). Session state, not persisted.
    bool showNavOverlay_ = false;
    // Collision-box overlay (View > Collision boxes, docs/collision-boxes.md):
    // the volume the game blocks the player and the camera boom with. Session
    // state like the other view toggles - the viewport is told each frame.
    bool showCollisionBoxes_ = false;
    navmesh::NavGrid navGrid_;
    uint64_t navOverlaySig_ = 0;
    uint64_t navOverlayVersion_ = 0;
    void updateNavOverlay();

    // "New project" modal state
    bool openNewProjectPopup_ = false;
    char newName_[128] = "my-game";
    char newLocation_[512] = "";
    int newWidth_ = 100;
    int newDepth_ = 100;
    bool newSizeCustom_ = false;  // the size segment's "Custom" (free width/depth)
    // "Create terrain" (docs/terrain.md): off starts the scene with NO ground
    // at all - no mesh, no floor in the game - for a project whose floors are
    // placed geometry (an interior, a platformer). Default on: a ground plane
    // is what "a new project" has always meant, and it is what the presets
    // below stand on.
    bool newTerrain_ = true;
    // Index into kNewPresets (app.cpp): FPP / Third person / Empty. Starts on
    // FPP - the walk-around-a-world preset is what most projects want first,
    // and an empty scene is a deliberate choice rather than a default.
    int newTemplate_ = 0;
    // World scale (docs/world-scale.md), picked while the project is created -
    // afterwards it is a setting that deliberately rescales nothing, so the
    // honest moment to ask is before there is any content. Index into the
    // preset list in drawNewProjectModal; the last entry is Custom, which is
    // when newUnitsPerMeter_ is edited directly.
    int newUnitsPreset_ = 0;  // 0 = 1 unit = 1 m
    float newUnitsPerMeter_ = 1.0f;
    // "Add AI support": install the assistant skill files (aisupport.hpp)
    // into the fresh project. Also available later in Project Preferences.
    bool newAiClaude_ = true;  // pre-selected, as designed (NewProject.dc.html)
    bool newAiCopilot_ = false;
    bool newAiCodex_ = false;
    std::string newProjectError_;

    // "New script" modal state. newScriptAttachTo_ >= 0 = attach the created
    // script to that object of the active scene (Properties > Scripts flow).
    bool openNewScriptPopup_ = false;
    char newScriptName_[64] = "my_script";
    std::string newScriptError_;
    int newScriptAttachTo_ = -1;
    // A VU program instead of an EE script: src/vu/<name>.cpp, compiled and
    // run on the HOST at build time (docs/vu-authoring.md). Cannot be attached
    // to an object - it replaces a material class, not a behaviour.
    // 0 = a game script on the EE, 1 = a VU1 program, 2 = a VU0 kernel. Three
    // destinations (src/scripts, src/vu, src/vu0) and three stubs, so this
    // stopped being a bool the moment the VU0 half became authorable.
    int newScriptKind_ = 0;

    // Object script classes registered in src/scripts/*.cpp with
    // TYRA_OBJECT_SCRIPT(Name), for the Properties attach UI. Per-file cache
    // keyed by write time - directories are cheap to walk every frame, file
    // reads are not.
    struct ScriptFileScan {
        std::filesystem::file_time_type mtime;
        std::vector<std::string> names;
    };
    std::map<std::string, ScriptFileScan> scriptScanCache_;
    std::vector<std::string> objectScriptNames();

    // Layer rename-in-place: the name captured when the field gained focus,
    // so object and flow-node references remap from it when editing ends.
    std::string layerRenameFrom_;
    int layerRenameIdx_ = -1;

    // Object rename-in-place: Cutscene Director tracks and camera-shot
    // bindings reference objects by name; they remap from this captured name
    // when the Properties name edit ends.
    std::string objRenameFrom_;
    int objRenameIdx_ = -1;
    // Properties tab of a solid object (0 Object, 1 Material, 2 Physics):
    // remembered for the session, so selecting another object - or a light,
    // which has no tabs - keeps the tab you were working in.
    int propsTab_ = 0;
    int propsTabFrame_ = -1;  // last frame the tab bar was drawn

    // Layers panel RAM readout: estimated resident bytes per layer name
    // ("" = the always-resident unassigned group). Parsing models/materials
    // for texture references is too slow per frame - cached until the next
    // commitChange()/attachProject() clears it.
    std::map<std::string, double> layerRamCache_;
    double layerAssetMB(const std::string& layerName);

    // "New scene" modal state
    int deleteScenePending_ = -1;  // scene index awaiting delete confirmation
    bool openNewScenePopup_ = false;
    char newSceneName_[64] = "scene-2";
    int newSceneWidth_ = 64, newSceneDepth_ = 64;
    // Same question the New Project dialog asks (docs/terrain.md): a scene may
    // start with no ground at all. Default on, like a scene always had.
    bool newSceneTerrain_ = true;
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
    std::set<std::string> discCollapsed_;  // load groups folded in the file list
    double discElfAge_ = -1.0;    // seconds since the planned ELF was written (-1 = none)
    double discElfAgeAt_ = 0.0;   // ImGui time discElfAge_ was measured

    // "Project Preferences" - an ordinary WINDOW, and it applies live like
    // every other panel in this editor (see drawPreferencesWindow for why the
    // staged OK/Cancel it used to have could not survive being non-modal).
    // `prefSettings_` is a ONE-FRAME copy, not a staging buffer: re-seeded from
    // project_.settings at the top of the body and written back at the bottom.
    // The game template is not copied - it is fixed at creation and the window
    // only displays it.
    bool showProjectPrefs_ = false;
    // A tab name for the NEXT frame of Project Preferences to select (see the
    // beginTab lambda there); empty = leave whichever tab the author left on.
    // One-shot: honoured once and cleared.
    std::string prefsFocusTab_;
    // Project Preferences navigation: the selected category (the old tab
    // names), a pending section jump, the search text, and the sections the
    // last drawn category had (the sidebar's index, one frame behind).
    std::string prefsCat_;
    std::string prefsScrollTo_;
    // One setting to bring into view and ring briefly (accent outline that
    // fades over ~1.5 s), by its kPrefIndex label - "Texture atlasing" from
    // the Texture Atlas, a search hit, a "what I changed" entry. Its category's
    // Advanced fold opens for it when it lives there.
    std::string prefsScrollToItem_;
    std::string prefsFlashItem_;
    double prefsFlashStart_ = 0.0;
    bool prefsScrollTop_ = false;
    char prefsSearch_[96] = {};
    std::vector<std::string> prefsSections_;
    // Categories whose Advanced fold is open (closed by default, per session),
    // the sidebar's "Show what I changed" list, and the new-project settings
    // "changed" is measured against (sectionJson text, rebuilt when the world
    // scale or the preset differs from the key it was built for).
    std::vector<std::string> prefsAdvOpen_;
    bool prefsShowChanged_ = false;
    std::string prefsDefaultsJson_, prefsDefaultsKey_;
    bool focusProjectPrefs_ = false;  // menu/shortcut re-open raises the window
    // Set by a FLOATING tool window that sends the user to a preference (the
    // Texture Atlas's "Turn it on in Rendering prefs"): Project Preferences
    // then opens floating, centred over that window and focused - docked as a
    // tab it would sit behind the very window that asked for it, and the click
    // would look like it did nothing. Holds that window's rect.
    bool prefsOverFloating_ = false;
    ImVec2 prefsOverPos_{0, 0}, prefsOverSize_{0, 0};
    TerrainConfig prefTerrain_;       // width/depth scratch - see prefGridDetail_
    ProjectSettings prefSettings_;
    // THE TERRAIN GRID IS THE ONE THING THAT MAY NOT APPLY PER FRAME.
    // Changing width, depth or the detail cap changes the heightmap's
    // dimensions, and project::ensureHeightmap answers that with a
    // NEAREST-NEIGHBOUR RESAMPLE - so live-applying "64" -> "128" keystroke by
    // keystroke would pass through 1 and 12 and flatten a sculpted map on the
    // way, and dragging the detail slider down and back would do the same. Also
    // Viewport::setTerrain re-centres the camera on any size change. These
    // three therefore write back only on IsItemDeactivatedAfterEdit.
    int prefGridDetail_ = 64;
    // The viewport refresh is deferred to the end of an interaction:
    // applyProjectToViewport() rebuilds the whole terrain mesh, which is not a
    // per-frame cost while a slider is being dragged.
    bool prefsViewportDirty_ = false;

    // "Editor Preferences" modal edit buffers (Edit > Preferences). Every
    // change is applied and saved at once - the buffers only hold typing.
    // Machine-global settings, mirror of globalEmulatorPath_ / globalPs2Ip_.
    bool openEditorPrefsPopup_ = false;
    char prefEmulatorPath_[512] = "";  // PCSX2 exe path (auto-detect if empty)
    char prefPs2Ip_[64] = "";          // ps2link IP for Run on PS2
    // Edit > Preferences > ps2link "Test connection": a TCP connect to
    // tcp/18193 on a worker (wire::probeTcp), never a deploy. The verdict is
    // kept with the IP it was for, so editing the field drops it.
    std::future<std::string> ps2TestJob_;
    std::string ps2TestIp_;      // the address the running/last test asked
    std::string ps2TestResult_;  // "" none yet; else the verdict line
    int ps2TestKind_ = 3;        // StatusLine kind of the verdict
    double ps2TestStarted_ = 0.0;
    char prefToolchainImage_[256] = "";  // Docker image games compile in ("" = compose default)
    int prefBuildBackend_ = 0;             // 0 native, 1 Docker fallback
    char prefDefaultProjectsDir_[512] = "";  // default parent folder for new projects
    char prefDisplayName_[48] = "";          // session display name (editor.ini)
    char prefSessionCacheDir_[512] = "";     // remote-project cache root override
    int prefAiBackend_ = 0;            // index into aigen::backendIds()
    char prefAiModel_[128] = "";       // "" = the backend's default model
    // Model combo shows "Custom..." + a free-text field when the staged model
    // is not one of the backend's listed models (or the user picked Custom).
    bool prefAiCustomModel_ = false;
    bool prefAiThinking_ = false;
    int prefCategory_ = 0;          // the sidebar's page (Appearance...)
    char prefSearch_[64] = "";      // filters rows across every page
    std::string prefDetectedPcsx2_; // what an empty PCSX2 path resolves to
    bool prefSwatchesReady_ = false;
    ImVec4 prefSwatches_[4][5];     // the theme cards' colours, read once

    // "Generate with AI" modal (Flow Graph window). The Generator runs on its
    // own worker thread; the modal polls it every frame, shows a spinner and a
    // Cancel button while busy, and applies the parsed graph via commitChange
    // on success. aiGenTargetObject_ pins the graph owner at request time so a
    // changed selection can't retarget an in-flight reply.
    bool openAiGeneratePopup_ = false;
    char aiPromptBuf_[2048] = "";
    int aiGenTargetObject_ = -1;
    bool aiGenInFlight_ = false;    // a started request's result is unconsumed
    aigen::Generator aiGen_;
    std::string aiGenError_;        // backend/parse failure shown in the modal
    std::string aiGenWarnings_;     // non-fatal parse notes (dropped links...)
    void drawAiGenerateModal();

    // --- AI Assistant window (Tools > AI Assistant, docs/ai-chat.md) --------
    // A chat with the same configured backend that ANSWERS questions about the
    // editor (from the embedded docs/ pages) and CARRIES OUT operations in the
    // project (the aichat tool table). Everything below is implemented in
    // chat_ui.cpp; the prompt, the parser and the read-only tools live in
    // aichat.cpp, which knows nothing about ImGui.
    //
    // The loop: aiChatSend appends the user's message and starts a request;
    // aiChatTick (called every frame from drawUI) consumes the reply, runs the
    // tool calls it carries and starts the NEXT request with their results, up
    // to kChatMaxSteps rounds per user turn. The tick is the only place chat
    // data meets project_ - so a request still in flight when a project is
    // closed cannot write into the next one (every tool refuses without
    // hasProject_).
    static constexpr int kChatMaxSteps = 8;
    // How many saved chats a project keeps. Old ones are dropped oldest-first
    // when a new one is saved, and the drop is reported in the status line.
    static constexpr int kChatHistoryKeep = 100;
    bool showAiChat_ = false;
    aichat::Conversation chat_;
    aigen::Generator chatGen_;
    bool chatInFlight_ = false;   // a started request's reply is unconsumed
    int chatStep_ = 0;            // tool rounds spent in the current user turn
    // Machine-global (editor.ini): whether the assistant may run the EDIT and
    // COMMAND tools at all. On by default - every edit is one undo step and
    // nothing reaches disk without a save - but "a chat that can only read" is
    // a legitimate way to want to work.
    bool chatAllowEdits_ = true;
    char chatInputBuf_[4096] = "";
    std::string chatError_;        // backend failure / cancellation
    bool chatScrollPending_ = false;  // stick the transcript to the bottom
    // Which message was last copied, and when. A Copy button on every message
    // turned the transcript into a column of chrome, so the message itself is
    // the click target and this is the acknowledgement: for a moment after the
    // click, hovering that message says "Copied" instead of "Click to copy".
    // Keyed by the ImGui id of the hit box rather than by message index,
    // because one Tool message draws several of them (0 = nothing copied yet).
    unsigned chatCopiedId_ = 0;
    double chatCopiedAt_ = 0.0;
    // History (docs/ai-chat.md): the conversation is written to its own file
    // next to editor.ini after every turn, so closing the window, switching
    // chats or restarting the editor does not lose it. chatFile_ is the file the
    // CURRENT conversation owns - empty until its first save, then reused, so a
    // chat does not multiply as it grows. The list is re-scanned when the popup
    // opens, never per frame.
    std::string chatFile_;
    std::vector<aichat::ChatRecord> chatHistory_;
    bool chatHistoryScanned_ = false;
    // --- context accounting and compaction (docs/ai-chat.md) --------------
    // What the session has actually cost, from the BACKEND's own numbers where
    // it reports them (the Claude CLI and the OpenAI API do; the Copilot CLI
    // does not, and then these stay at zero while the window shows the estimate
    // instead - a made-up number presented as a measured one is worse than no
    // number).
    long long chatTokensIn_ = 0;
    long long chatTokensOut_ = 0;
    double chatCostUsd_ = 0.0;
    bool chatUsageReal_ = false;   // any request reported real numbers
    // Compaction: the conversation's older half is replaced by the model's own
    // recap when the transcript outgrows its budget, so a long chat costs a
    // bounded amount instead of growing until the oldest turns fall off the
    // front. It is one extra backend request, so it happens on a send that is
    // over budget (or when the user asks for it) - never speculatively.
    bool chatCompactPending_ = false;   // the next start() compacts
    bool chatCompactThenSend_ = false;  // ...and then answers the user
    bool chatCompacting_ = false;       // the in-flight request is a compaction
    // build_game: the Runner takes minutes and runs on its own thread, so the
    // chat PARKS instead of answering - the tool's result is filled in when the
    // build settles and the loop resumes with it. That is what lets the
    // assistant see its own compile errors. Gated by a machine setting because
    // it spends a Docker container and several minutes.
    bool chatAllowBuild_ = false;
    bool chatBuildWaiting_ = false;  // a tool started a build; the loop is parked
    bool chatPadWaiting_ = false;    // ...or a pad script; same parking
    // A `run` build is not finished when the build is: PCSX2 takes tens of
    // seconds to boot, and a tool that came back the moment the ELF linked had
    // the assistant pressing buttons at a black screen. So the turn keeps
    // waiting until the game's own debug channel appears - that is the first
    // moment anything in there is true.
    bool chatBuildWasRun_ = false;
    bool chatGameWaiting_ = false;
    double chatGameDeadline_ = 0.0;
    long long chatGameMark_ = 0;       // the liveness signal before the launch
    long long chatGameSignal() const;  // newest mtime of the game's own files
    size_t chatPadLogMark_ = 0;      // bin/log.txt size when the script started
    size_t chatCompactCount_ = 0;       // messages being folded
    std::string chatCompactNote_;       // what happened, for the window
    // Per user turn (keyed by the User message's index; session-only, never
    // saved): the mode it was asked in and the undo history's index before
    // and after its tools ran, so the turn's change card can offer "Undo".
    struct ChatTurnInfo {
        int mode = 1;        // 0 ask only, 1 edit project, 2 edit + build & run
        int undoBase = -1;   // history_.index() when the turn started
        int undoEnd = -1;    // ... after its last tool round
        bool undone = false;
    };
    std::map<size_t, ChatTurnInfo> chatTurns_;
    size_t chatTurnUser_ = 0;           // the User message the running turn answers
    bool chatWasOpen_ = false;          // to bring a just-opened, docked window forward
    void aiChatRekeyTurns();            // after a compaction renumbers messages
    void drawAiChatTurn(size_t userIdx, size_t from, size_t to, bool live);
    void aiChatPersist();                        // save the current conversation
    void aiChatOpen(const std::string& file);    // load a saved one
    void drawAiChatHistory();                    // the History popup body
    void drawAiChatWindow();
    void aiChatTick();
    void aiChatSend(const std::string& text);
    void aiChatStart();
    std::string aiChatSystemPrompt();  // what a request carries (also measured)
    void aiChatReset();
    // Runs one tool call against the editor, filling c.failed; returns the text
    // the model gets back. Read tools are delegated to aichat::runReadTool.
    std::string runChatTool(aichat::ToolCall& c);
    // One set_object property onto an object. False = no branch for that key
    // (reported as unhandled); err non-empty = the value was refused.
    bool applyChatObjectProp(SceneData& sc, SceneObject& o,
                             const std::string& key, const json::Value& v,
                             std::string& err);

    // "Debug" window: tails a log from disk (reloaded, throttled). Source 0 is
    // the game's own log (bin/log.txt, written by TYRA_LOG); source 1 is the
    // emulator's console log (PCSX2 emulog.txt, boot progress + asserts).
    // (the tail itself lives in logDbg_.text - the classified view owns the
    // buffer its line offsets point into)
    int debugLogSource_ = 0;
    bool debugAutoReload_ = true;
    double debugNextReload_ = 0.0;  // ImGui::GetTime() gate for the next read
    // The Reload button is drawn BELOW the read (the buttons report on the
    // classified lines), so a press arms the next frame's read.
    bool debugReloadNow_ = false;

    // The classified views behind the two panels. Their filter masks and the
    // selectable-text toggles are machine-global (editor.ini): which noise a
    // person wants hidden is a property of how they work, not of the project.
    LogView logOut_, logDbg_;

    // Game error catcher: polls the game's log (bin/log.txt over host:, or the
    // networked [ps2] console output in the runner log) for a TYRA assertion
    // dump and raises a copyable error dialog, so a failed assert (e.g. a
    // missing texture) surfaces in the editor instead of only freezing the
    // game. errorPopupEnabled_ (persisted in editor.ini) gates the dialog; when
    // off, errors go only to the console / Debug window. errorSeenSig_ is the
    // last assertion block already handled, so each distinct error pops once.
    // It is forgotten when a source shrinks (a new run recreates bin/log.txt;
    // Output "Clear" empties the runner log) so an *identical* new error - the
    // same missing file at the same line, re-triggered on the next run - pops
    // again instead of being deduped against the stale log. The size fields
    // track that; they are baselined on project attach so opening a project
    // with a stale dump in its log neither pops it nor looks like a shrink.
    bool errorPopupEnabled_ = true;
    // "Use the GPU for bakes" - ONE machine-wide switch (editor.ini gpuBakes,
    // Edit > Preferences > Build) for the GI gather and the impostor capture;
    // the Ambience Editor shows which engine the next GI bake uses and links
    // here instead of keeping a switch of its own.
    bool gpuBakes_ = true;
    std::string errorSeenSig_;
    std::string errorModalText_;      // block shown in the open dialog
    bool openErrorPopup_ = false;     // request to open the modal next frame
    size_t errorGameLogSize_ = 0;     // last-seen bin/log.txt size (shrink = new run)
    size_t errorRunnerLogSize_ = 0;   // last-seen runner-log size (shrink = cleared)
    double errorNextPoll_ = 0.0;      // ImGui::GetTime() gate for the next scan
    // Reads the game log tail + runner log and, on a newly seen assertion,
    // raises the error dialog (throttled). Called each frame from drawUI().
    void pollGameError();
    // The freshest assertion dump the editor can see (game log.txt + runner
    // log). "" when there is none.
    std::string latestGameAssert() const;
    void drawErrorModal();

    // Live Link: mirrors scene edits into the running game without a rebuild.
    // Debug-profile builds with the project's "Live Link" preference on
    // compile a poller (live_link.gen.cpp) that re-reads bin/livelink.bin over
    // host: - the same file channel PCSX2's Host Filesystem and the ps2link
    // file server already serve assets through, so one mechanism covers the
    // emulator AND a real console. liveLinkTick() (each frame from drawUI,
    // self-throttled to ~10 Hz) snapshots the active scene - one 64-byte
    // record per object, addressed by project::liveLinkIdHash - and writes it
    // atomically (tmp + rename) with a bumped sequence number whenever it
    // changed; a gizmo drag streams to the console as it happens. The game
    // patches matching objects, SPAWNS newly added ones (each record carries
    // the as-built index of an equal-recipe template to clone) and hides ones
    // missing from the snapshot (deleted; undo restores). Streaming happens
    // only while the live project is representable against bin/livelink.sig
    // (the as-built record the Runner stamps at build start, parsed into
    // liveLinkBuilt_): a changed recipe on a built object, a new object with
    // no template (or carrying logic / baked lights / projected decals /
    // mirrors), or layer-table drift flips the toolbar chip to "rebuild"
    // instead of mis-patching. The chip doubles as the on/off switch for the
    // project preference (also in Build > Live Link and Preferences > Build & debug).
    enum class LiveLinkState {
        Off,           // preference off, no project, or release build profile
        NoBuild,       // no bin/livelink.sig yet - run a debug build first
        Live,          // streaming; toolbar shows the green LIVE dot
        RebuildNeeded  // the session can't represent the edit - F5 to resync
    };
    // bin/livelink.sig parsed: per scene the as-built (idHash, recipeHash)
    // list in authored order (line position = the spawn-template index the
    // records reference), plus the cross-object context hash.
    struct LiveLinkBuilt {
        bool loaded = false;
        uint64_t ctxHash = 0;
        std::vector<std::vector<std::pair<uint64_t, uint64_t>>> scenes;
    };
    LiveLinkState liveLinkState_ = LiveLinkState::Off;
    LiveLinkBuilt liveLinkBuilt_;
    uint32_t liveLinkSeq_ = 0;         // last written sequence number
    std::vector<unsigned char> liveLinkLastPayload_;  // last written body
    double liveLinkSigNextRead_ = 0.0; // ImGui::GetTime() gate for sig re-read
    double liveLinkNextTick_ = 0.0;    // ImGui::GetTime() gate for the ticker
    void liveLinkTick();

    // Live Debugger (docs/live-debugger.md): Live Link's opposite direction.
    // A debug build with the "Live Debugger" preference reports what its flow
    // graphs are doing - every trigger and action it runs, the flow variables,
    // the save values - into bin/livedbg.bin, and reads breakpoints, halt/step
    // and force-fire requests back from bin/livedbg.cmd. Both files ride the
    // same host: channel as Live Link, so PCSX2 and a real console over
    // ps2link work identically. livedbgTick() (each frame from drawUI,
    // self-throttled to ~20 Hz) reads the snapshot, folds it into the timeline
    // and writes the command file whenever the desired state changed (or the
    // Runner deleted it for a fresh run). The Debugger window and the Flow
    // Graph overlay draw from dbgSnap_/dbgSyms_/dbgTimeline_ only.
    enum class DbgState {
        Off,       // release build, preference off, or no project
        NoBuild,   // no symbol table yet - refresh/build once
        Waiting,   // symbols known, no game reporting
        Stale,     // the running ELF was built from different graphs
        Running,   // the game is reporting and moving
        Halted     // stopped at a breakpoint / by Pause
    };
    DbgState dbgState_ = DbgState::Off;
    livedbg::Symbols dbgSyms_;      // src/gen/livedbg.sym (as generated)
    livedbg::Snapshot dbgSnap_;  // newest snapshot the game wrote
    livedbg::RenderCost dbgRenderCost_, dbgRenderBaseline_;
    // Show the Obj_* rows (one object's own pipeline bill, docs/profiling.md).
    bool dbgRenderCostDetail_ = false;
    uint32_t dbgRenderCostSeq_ = 0;
    bool dbgRenderCostWaiting_ = false;
    double dbgRenderCostPoll_ = 0;
    std::string dbgRenderCostProject_;
    livedbg::Timeline dbgTimeline_;  // per-frame fire history (the scrub)
    livedbg::Command dbgCmd_;       // last command written (state + seq)
    bool dbgCmdWritten_ = false;    // has the current dbgCmd_ reached the game?
    std::vector<uint32_t> dbgPrevHits_;  // previous snapshot's counters
    std::vector<double> dbgHeat_;   // per key: ImGui::GetTime() of its last fire
    std::vector<uint16_t> dbgFireQueue_;  // keys the user asked to force-fire
    double dbgNextTick_ = 0.0;      // ImGui::GetTime() gate for the ticker
    double dbgSymNextRead_ = 0.0;   // gate for re-reading the symbol table
    double dbgSnapTime_ = 0.0;      // when the newest snapshot arrived
    double dbgSnapPrevTime_ = 0.0;  // and the one before it (for the FPS)
    uint32_t dbgSnapPrevFrame_ = 0;
    // What bin/livedbg.bin looks like ON DISK, independent of whether it is
    // still MOVING. A dead channel (the file server went away, the console
    // kept running) leaves a perfectly valid snapshot frozen at its last
    // write, and that is indistinguishable from "no data yet" unless the age
    // is carried across - which is what cost a whole evening once. Seconds
    // since the file was last written; < 0 means there is no file at all.
    double dbgSnapFileAge_ = -1.0;
    double dbgSnapFileNextStat_ = 0.0;  // gate for the stat (it is per tick)
    /** One sentence naming why no live stats are on screen, plus what to do
     * about it. Empty while the game is reporting normally. Shared by the
     * window's state block and the Stats tab so the two cannot disagree. */
    std::string dbgSilenceReason() const;
    /** PCSX2 is the target and is not running this ELF: a frozen snapshot is
     *  a closed game's, not a stale transport (devkit_ui.cpp). */
    bool dbgGameClosed() const;
    /** The paragraph dbgSilenceReason() no longer prints inline: which file is
     * silent and how a running console ends up with nowhere to write. For the
     * (?) hover next to it; only meaningful when the reason is non-empty. */
    const char* dbgSilenceDetail() const;
    float dbgFps_ = 0.0f;           // measured against the editor's wall clock
    int dbgScrub_ = -1;             // timeline index being inspected (-1 = live)
    std::string dbgWatchFilter_;    // Watch tab search box (name or kind)
    void livedbgTick();
    void drawDebuggerWindow();
    // The window's parts (devkit_ui.cpp): a status header with the transport,
    // a section rail instead of a tab strip, one page, a right column (where
    // the game stopped, or why it is not running) and one time strip.
    enum class DbgPage {
        Watch, Nodes, Breakpoints, Patches,  // Logic
        Objects, Screen,                     // World
        Stats, RenderCost, Hardware, Vu      // Performance
    };
    DbgPage dbgPage_ = DbgPage::Watch;
    bool dbgTimeOpen_ = true;       // the time strip expanded (else one row)
    int dbgWatchKind_ = 0;          // 0 all, 1 facts, 2 save values, 3 flow vars, 4 just changed
    // Watch "Changed" column: each row's frame of the report that last changed
    // it (0 = no change seen), diffed in livedbgTick from consecutive reports.
    std::vector<float> dbgPrevVars_;
    std::vector<uint32_t> dbgVarChanged_;
    // Stats chart: the game's rendered rate per report, as ms a frame.
    std::vector<float> dbgFrameMs_;
    static constexpr size_t kDbgFrameMsKeep = 150;
    std::string dbgNodeLabel(const livedbg::NodeSym& n) const;
    void dbgRevealNode(const livedbg::NodeSym& n);
    void dbgSetTransport(int what);  // 0 pause/continue, 1 step frame, 2 step node
    void dbgBuildAndRun();
    void dbgDrawHeader();
    void dbgDrawRail(bool compact, bool horizontal = false);
    void dbgDrawChecklist();
    void dbgDrawStoppedAt(bool showPath);
    void dbgDrawBreakpointList(bool compactRows);
    void dbgDrawCrashBanner();
    void dbgDrawTimeStrip(bool open);
    void dbgDrawWatchPage();
    void dbgDrawNodesPage();
    void dbgDrawBreakpointsPage();
    void dbgDrawPatchesPage();
    void dbgDrawObjectsPage();
    void dbgDrawScreenPage();
    void dbgDrawStatsPage();
    void dbgDrawRenderCostPage();
    void dbgDrawVuPage();

    // The time machine (docs/time-machine.md): the third direction of the same
    // host: channel. The game captures everything it mutates into
    // bin/livetime.bin every few frames; livetimeTick() (each frame from
    // drawUI, self-throttled) folds those captures into a RAM history and
    // timeMachineRewind() writes one back to bin/livetime.rst, which puts the
    // running game where it was. The history is deliberately not persisted -
    // see the budget note on livetime::History.
    int timeBudgetMb_ = 128;        // EditorConfig::timeMachineBudgetMb
    livetime::History timeHistory_;
    livetime::Snapshot timeLast_;   // newest capture seen
    bool timeHaveLast_ = false;
    double timeNextTick_ = 0.0;     // ImGui::GetTime() gate for the reader
    double timeLastSeen_ = 0.0;     // when a capture last arrived (staleness)
    int timeScrub_ = -1;            // history index being inspected (-1 = live)
    uint32_t timeRestoreSeq_ = 0;   // sequence of the last restore we pushed
    std::string timeStatus_;        // last action, shown in the panel
    void livetimeTick();
    /** Pushes history entry `index` back into the running game. */
    void timeMachineRewind(int index);

    // The input recorder (docs/input-replay.md): the fifth direction of the
    // same host: channel, and the only one that reproduces a whole SESSION.
    // The mode is chosen for the NEXT run and staged into the Runner, which
    // does the file work before the launch - so nothing here talks to a
    // running game except replayTick(), which reads the status the game
    // writes into bin/replay.st (~4 Hz, the livetimeTick shape).
    enum class ReplayArm { None, Record, Play };
    ReplayArm replayArm_ = ReplayArm::None;
    std::string replayFile_;        // recordings/<name>.tyrarep for Play
    livereplay::Status replayStatus_;
    bool replayHaveStatus_ = false;
    double replayNextTick_ = 0.0;   // ImGui::GetTime() gate for the reader
    std::string replayMsg_;         // last action, shown in the panel
    std::string replaySaveName_;    // the Save field's contents
    std::vector<std::string> replayFiles_;  // recordings/*.tyrarep, cached
    double replayScanAt_ = 0.0;     // when that list was last rebuilt
    void replayTick();
    void drawReplayPanel();
    /** Asks the running game to finish its recording, waits for the terminal
     * chunk, and canonicalizes bin/replay.out into recordings/<name>.tyrarep.
     * Returns "" or an error. */
    std::string replayStopAndSave(const std::string& name);
    /** Replay files in recordings/, refreshed at most a few times a second. */
    void replayRescan(bool force);

    // Remote Pad (docs/remote-pad.md): the fourth direction of the same host:
    // channel, and the only one carrying INPUT. While the window is open the
    // editor IS the controller - remotePadTick() rewrites bin/livepad.bin at
    // ~25 Hz (the game treats a `seq` that stopped moving as "the driver went
    // away", so a held button has to be re-announced), and writes one detached
    // state when the window closes so nothing is left held.
    bool showRemotePad_ = false;
    livepad::State padState_;
    uint32_t padSeq_ = 0;
    bool padAttached_ = false;      // is the editor currently driving?
    bool padKeyboard_ = false;      // fold the EDITOR's own keyboard onto it
    int padTarget_ = 0;             // which connector the panel edits (0/1)
    double padNextWrite_ = 0.0;     // ImGui::GetTime() gate for the rewrite
    // Per pad, per button: hold it at least until this time. A click shorter
    // than the write interval would otherwise fall between two snapshots and
    // never be announced at all - "I clicked Cross and nothing happened".
    double padLatch_[livepad::kPads][16] = {};
    // The drawn controller: shift-click latches a button until clicked again
    // (when padShiftHold_ is on); a stick is dragged in 2-D and springs back
    // on release unless Shift is held then.
    bool padShiftHold_ = false;
    bool padSticky_[livepad::kPads][16] = {};
    bool padStickDragged_[2] = {false, false};
    std::string padStatus_;
    // A pad SCRIPT being played (docs/remote-pad.md's own language, parsed by
    // the same livepad::parseScript the --pad CLI uses). While one runs the
    // editor drives the pad whether or not the panel is open - that is what
    // lets the AI Assistant walk the player into the thing it just built - and
    // the chat parks until the script ends. The state is cleared when it does:
    // a script that left a direction held would look exactly like a stuck pad.
    std::vector<livepad::Step> padScript_;
    size_t padScriptStep_ = 0;
    double padScriptUntil_ = 0.0;
    bool padScriptRunning_ = false;
    void padScriptTick();  // advance it; called from remotePadTick
    void remotePadTick();
    void drawRemotePadWindow();

    // UI scripting (docs/ui-scripting.md): --ui-script drives the editor itself
    // by injecting into ImGui's own event queue, resolving targets by widget
    // NAME through uiscript's item registry. Nothing reaches the OS, so the
    // window needs no focus and a script is independent of DPI and ui scale.
    // uiScriptTick() runs once per frame, immediately BEFORE ImGui::NewFrame():
    // it reads the item map the LAST frame built, then injects for this one.
    std::vector<uiscript::Step> uiScript_;
    size_t uiStepIndex_ = 0;
    int uiStepPhase_ = 0;        // sub-frame progress within the current step
    double uiStepStarted_ = 0.0;  // ImGui::GetTime() the step began
    double uiStepUntil_ = 0.0;   // for wait / hold
    int uiStepFrames_ = 0;       // for `frames`
    float uiTargetX_ = 0, uiTargetY_ = 0;  // resolved click point
    bool uiScriptActive_ = false;
    bool uiScriptFailed_ = false;
    std::string uiShotPath_;     // a `shot` step: written after this frame renders
    void uiScriptTick();
    /** Reads the window back into a PNG. Shared with the TYRAX_SHOT capture. */
    bool captureFrameTo(const std::string& path, int w, int h);

    // Crash reporting (docs/devkit.md). A real EE exception is not a
    // TYRA_ASSERT: with the engine's crash handler installed the game writes
    // bin/crash.txt (decoded cause, registers, backtrace candidates) and halts;
    // this is that report, parsed, plus the names the PS2 toolchain resolves for
    // its addresses on demand.
    struct DbgCrash {
        bool present = false;
        std::string raw;      // the whole report, for Copy
        std::string cause;    // decoded name
        uint32_t epc = 0, badvaddr = 0, frame = 0;
        int scene = -1;
        std::vector<uint32_t> trace;
        std::vector<elfsym::Location> names;  // resolved on demand
        std::string namesError;
        bool resolving = false;
    };
    DbgCrash dbgCrash_;
    size_t dbgCrashSize_ = 0;   // last seen size of crash.txt (change = new)
    double dbgCrashNextRead_ = 0.0;
    // VU1 packet capture (docs/devkit.md): "show me what the EE actually fed
    // VU1 for one draw". Armed from the Debugger's VU tab; the game answers with
    // bin/vucap.bin, decoded by src/vucap.hpp.
    vucap::Capture dbgVuCap_;
    size_t dbgVuCapSize_ = 0;
    long long dbgVuCapStamp_ = 0;  // last_write_time of the file we decoded
    int dbgVuCapTorn_ = 0;         // consecutive incomplete reads of that file
    bool dbgVuCapWaiting_ = false;  // a capture was asked for, none arrived yet
    float dbgVuYaw_ = 0.6f, dbgVuPitch_ = 0.35f, dbgVuZoom_ = 1.0f;
    ImVec2 dbgVuPan_{0.0f, 0.0f};  // middle-drag pan, pixels
    int dbgVuMesh_ = 0;  // which position stream of the flush the preview draws
    bool dbgVuPinFlush_ = false;  // re-grab one draw instead of walking them
    int dbgVuFlushWanted_ = 0;    // ...which one
    // The game's own screenshot (docs/devkit.md): the running game reads its
    // last finished frame out of GS VRAM and writes bin/frame.tga, which this
    // decodes into a GL texture for the Debugger's Screen tab. The only capture
    // path that works on real hardware - and the only one that survives a
    // locked desktop, where PCSX2's F8 and every host-side grab go blind.
    unsigned int dbgShotTex_ = 0;   // GL texture, or 0 when nothing decoded
    int dbgShotW_ = 0, dbgShotH_ = 0;
    long long dbgShotStamp_ = 0;    // last_write_time of the file we decoded
    size_t dbgShotSize_ = 0;
    int dbgShotTorn_ = 0;           // consecutive polls that saw NO progress
    size_t dbgShotPartial_ = 0;     // size of the last short read - see below
    bool dbgShotWaiting_ = false;   // asked for one, none arrived yet
    std::string dbgShotFile_;       // the PNG this capture was kept as
    std::vector<unsigned char> dbgShotPixels_;  // RGBA, for Copy image
    std::string dbgShotError_;
    void dbgReadFrameShot();
    void dbgReadVuCapture();
    void dbgReadCrashReport();
    void dbgResolveCrashNames();
    // "The game stopped reporting": the devkit heartbeat died without a crash
    // report or an assert - a hang, or an exception nobody caught.
    bool dbgLostGame_ = false;
    uint32_t dbgLostAtFrame_ = 0;

    // Live Logic (docs/live-logic.md): flow-graph HOT PATCHING. The editor
    // compiles every graph that differs from what the running ELF was built
    // with (src/gen/livelogic.built, emitted by codegen) into the pre-resolved
    // instruction list in livelogic.hpp and writes bin/livelogic.bin; the
    // game's interpreter runs those graphs instead of their compiled C++.
    // liveLogicTick() (each frame from drawUI, self-throttled) recompiles when
    // the project changed and rewrites the patch only when its bytes change.
    // Graphs the IR cannot express (audio, AI, animation, spawning, text...)
    // are listed per graph in liveLogicBlocked_ and still need a rebuild.
    enum class LogicState {
        Off,        // release build, preference off, or no project
        NoBuild,    // no built-graph list yet (build once)
        InSync,     // nothing differs from the build - nothing to patch
        Patched,    // N graphs are running from the editor's patch
        Blocked,    // an edited graph cannot be hot-patched (rebuild needed)
        OffStale    // graphs differ from the build and Live Logic is OFF
    };
    LogicState liveLogicState_ = LogicState::Off;
    livelogic::BuiltList liveLogicBuilt_;
    std::vector<unsigned char> liveLogicLastPayload_;
    uint32_t liveLogicSeq_ = 0;
    int liveLogicPatchCount_ = 0;
    // Per unpatchable graph: "object name" -> why (node titles, deduped).
    std::vector<std::pair<std::string, std::string>> liveLogicBlocked_;
    double liveLogicNextTick_ = 0.0;
    double liveLogicBuiltNextRead_ = 0.0;
    void liveLogicTick();
    // Breakpoints are stored as "<objectId>:<nodeId>" in the project (editor
    // state), and resolved to the game's integer keys through dbgSyms_.
    std::string dbgBreakpointKey(const std::string& objectId, int nodeId) const;
    bool dbgHasBreakpoint(const std::string& objectId, int nodeId) const;
    void dbgToggleBreakpoint(const std::string& objectId, int nodeId);
    int dbgKeyFor(int scene, const std::string& objectId, int nodeId) const;
    // Seconds since a node last fired (FLT_MAX = not since the game started).
    float dbgNodeHeat(int key) const;
    // Fires the whole branch under a trigger in the running game, once.
    // `andRun` resumes the game afterwards instead of running a single frame -
    // needed whenever the branch only ARMS something (a Delay, a glide), which
    // then needs frames to finish.
    void dbgFireNode(int key, bool andRun = false);
    // Frames left on an armed countdown for this node key (-1 = not armed).
    int dbgTimerFrames(int key) const;

    // Object watch (docs/devkit.md): the editor names up to
    // livedbg::kMaxWatchObjects runtime objects and the game samples them every
    // frame; the samples accumulate here into a per-object history the panel
    // plots and the viewport draws as a trail. Session state (indices belong to
    // the running build), not saved with the project.
    struct DbgObjTrack {
        int index = -1;               // runtime object index
        std::string name;             // for display when the object is renamed
        std::vector<livedbg::ObjSample> samples;  // oldest first, capped
    };
    std::vector<DbgObjTrack> dbgObjWatch_;
    bool dbgShowTrails_ = true;       // draw watched paths in the viewport
    void dbgToggleObjectWatch(int objectIndex);
    bool dbgIsWatched(int objectIndex) const;
    static constexpr size_t kDbgTrackSamples = 1500;  // ~30 s at 50 Hz

    // "Scene Preferences" modal staging (applied on OK): the active scene's
    // per-category overrides of the project defaults.
    bool openScenePrefsPopup_ = false;
    int scenePrefScene_ = -1;  // scene index the staging belongs to
    ProjectSettings scenePrefSettings_;
    SceneOverrides scenePrefOverrides_;
    std::string scenePrefAmbience_;  // staged SceneData::ambiencePreset
    std::string scenePrefLoading_;   // staged SceneData::loadingScreen
    bool scenePrefStart_ = false;    // staged "this is Project::startScene"

    std::string statusMessage_;

    // Install the bundled VS Code extension once per session (from openInVSCode);
    // vsCodeExtStatus_ keeps the last outcome so it can be shown to the user.
    bool vsCodeExtInstallTried_ = false;
    std::string vsCodeExtStatus_;
};
