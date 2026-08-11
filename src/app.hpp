#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <array>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include <imgui.h>  // ImGuiStyle baseStyle_ member (UI scaling)

#include "aichat.hpp"  // the AI Assistant window's conversation + tool table
#include "aigen.hpp"
#include "blss_ui.hpp"  // the Neural Upscaler window's job + parsed tables
#include "blsscorpus.hpp"  // blss::CoverageReport - the speed half of the answer
#include "audiopreview.hpp"
#include "camtake.hpp"
#include "dronegen.hpp"
#include "history.hpp"
#include "phonecam.hpp"
#include "gibake.hpp"
#include "procbake.hpp"
#include "matbake.hpp"
#include "menubake.hpp"  // CreditsLayout member (Credits Editor preview)
#include "menulayout.hpp"  // the Menu Preview's row geometry
#include "menustyle.hpp"  // the staged stylesheet the Style tab edits
#include "isoexport.hpp"
#include "elfsym.hpp"
#include "vucap.hpp"
#include "livedbg.hpp"
#include "livepad.hpp"
#include "logview.hpp"  // LogView members (the Output / Debug severity split)
#include "uiscript.hpp"
#include "livetime.hpp"
#include "livelogic.hpp"
#include "placement.hpp"
#include "prefab.hpp"
#include "project.hpp"
#include "vugen.hpp"  // vugen::Built - the VU panel keeps a live preview
#include "runner.hpp"
#include "session.hpp"
#include "theme.hpp"  // theme::Id theme_ member (interface theme)
#include "treegen.hpp"
#include "savebake.hpp"
#include "viewport.hpp"

struct GLFWwindow;

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

    unsigned mask = logview::kAll;  // levels shown; persisted in editor.ini
    bool selectText = false;        // swap the colours for a selectable text box

    // Derived from (lines, mask) - rebuilt when either changes, or when the font
    // size does (the width is measured in pixels).
    std::vector<int> visible;  // indices into `lines`
    int counts[logview::kLevelCount] = {0, 0, 0, 0};
    float width = 0.0f;        // widest visible line, px - the h-scroll range
    float widthFont = 0.0f;    // font size `width` was measured at
    bool dirty = true;
    size_t shown = 0;          // visible.size() last frame (autoscroll edge)
    bool scrollBottom = false; // jump to the tail once (the two modes scroll
                               // differently, so a switch would land at the top)
    std::string joined;        // the kept lines as text (Copy / selectable mode)
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
    // Writes the editor's own framebuffer to <TYRAX_SHOT>/shotNN.png every
    // TYRAX_SHOT_EVERY seconds (default 2), and nothing at all when the
    // variable is unset. The only screenshot path that works under a compositor
    // that denies external capture - see the comment on the definition.
    void captureFrameIfRequested(int w, int h);
    // Icon toolbar drawn inline in the main menu bar (Save / Build / Run / Stop
    // + the live chips). Custom vector-drawn - the editor loads no icon font.
    void drawToolbar();
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
    // and drawLogPanel() draws v.text through v's filter.
    void logSetText(LogView& v, std::string&& text);
    void logRefresh(LogView& v);
    void drawLogPanel(const char* id, LogView& v);
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
    void drawAddObjectMenu();
    // Area picker for a "catch area" reference (Mirror/Portal/feed Camera) or
    // a layer zone: a combo of this scene's Area objects plus <none>. Returns
    // true when the selection changed (the caller commits).
    bool areaCombo(const char* label, std::string& ref);
    // The catch-area block shared by the Mirror / Portal / feed Camera panels:
    // picker + "Update every frame" + the resolved counts. `verb` is how the
    // panel words what happens to a caught object ("re-drawn", "shown", ...).
    bool catchAreaControls(SceneObject& o, const char* verb);
    // Picks a model, then copies and validates it on a worker thread. The
    // optional target is the Asset Browser folder that requested the import;
    // completion moves the finished dependency group there on the UI thread.
    // Does NOT create a scene object.
    void importModelAsset(const std::string& targetFolder = {});
    void modelImportWorker(std::string source, std::string projectDir,
                           std::string targetFolder);
    void modelImportTick();
    void drawModelImportModal();
    struct ModelImportResult {
        std::string projectDir;
        std::string targetFolder;
        std::string relPath;
        std::string status;
        std::array<float, 6> bounds{};  // min xyz, max xyz
        bool installed = false;
        bool measured = false;
        bool animated = false;
    };
    std::thread modelImportThread_;
    std::atomic<float> modelImportProgress_{0.0f};
    std::atomic<int> modelImportStage_{0};
    std::atomic<bool> modelImportDone_{false};
    bool modelImporting_ = false;
    bool modelImportOpen_ = false;
    bool modelImportClose_ = false;
    std::string modelImportName_;
    ModelImportResult modelImportResult_;
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
    std::vector<std::string> listMaterialAssets();
    // Combo picking an .mtl for the object (primitives: surface; models:
    // override). Returns true when materialPath changed.
    bool drawMaterialCombo(SceneObject& o);
    // Creates res/materials/<model>.mtl seeded from a model object's built-in
    // materials (names + Kd + extracted textures, from the .glb/.fbx or .obj),
    // assigns it to o.materialPath and opens the Material Editor on the model.
    // Returns the new material's project-relative path, or "" on failure.
    std::string createMaterialForModel(SceneObject& o);
    // Friendly authored-forward selector for skeletal models and player
    // avatars. It writes SceneObject::modelYawOffset, including the common
    // "backwards" 180-degree correction. Returns true when committed.
    bool drawModelFacing(SceneObject& o);
    // Per-object LOD override rows. `animated` adds the animation-LOD row; a
    // static .obj model only takes the mesh-LOD distance. Returns true when a
    // value changed (caller commits).
    bool drawLodOverrides(SceneObject& o, bool animated = true);
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
    bool assetRecursive_ = false;                // list sub-folders too
    bool assetShowGenerated_ = false;
    int assetSort_ = 0;         // 0 name, 1 type, 2 size, 3 newest first
    bool assetGridView_ = true;  // thumbnails (false = detail rows)
    float assetTileSize_ = 84.0f;
    double assetScanTime_ = -1.0;   // ImGui time of the last disk scan
    int assetThumbBudget_ = 0;      // thumbnails still allowed this frame
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
    // (16-bit PCM 22050 Hz); music = the song player rules.
    const std::string& wavIssue(const std::string& relPath, bool sfx);
    std::map<std::string, std::string> wavIssueCache_;
    void importHudImage();
    // Imports a PNG into res/hud/ as a HudImage appended to `target` (shared by
    // the UI Editor's HUD list and the Loading Screen editor). Returns the new
    // index, or -1 on failure.
    int importHudImageInto(std::vector<HudImage>& target);
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
    // Tools > Bake Global Illumination: per-scene staleness + the bake itself
    // on gibake::Baker's worker thread (docs/global-illumination.md).
    void giBakerPoll();
    void drawGiBakeSection();
    // (Re)builds the in-memory tree mesh + textures from treeParams_ and bumps
    // treePreviewVersion_ so the preview re-uploads. Called on any param edit.
    void rebuildTreePreview();
    // "Add to scene": bakes the current tree's assets into res/models/trees
    // and inserts a Model object pointing at them.
    void addTreeToScene();
    // Tools > Prefabs (docs/prefabs.md): reusable groups of scene objects,
    // captured from a selection and stamped back into the world - by hand, by
    // a procedural graph, or by the Spawn Prefab node at runtime. Lives in
    // prefab_ui.cpp (the assetbrowser.cpp precedent).
    void drawPrefabsWindow();

    // Tools > Neural Upscaler (BLSS) - blss_ui.cpp, docs/neural-upscaler.md.
    // Training, evaluation, cross-validation, the input-channel report, the
    // comparison images and the emit step, all of which used to be CLI-only.
    // Every run is a subprocess of the editor's own binary (blss_ui.hpp says
    // why) and every number drawn is parsed out of its output.
    void drawBlssWindow();
    void drawBlssHeader();
    // The corpus switch (project vs the built-in bestiary) and the one-line
    // reminder of which one a table came from. Drawn in the header, read by
    // every verb through blssCommonArgs().
    void drawBlssCorpusChoice();
    void drawBlssCorpusReminder();
    // THE HAPPY PATH, and it is above the tabs because it is the only question
    // most people have: one button that runs the NET-FREE `--blss-eval` on this
    // project and answers "will this scene benefit" in plain language. It needs
    // no trained network, which is the whole point - the window used to tell
    // people to run Evaluate first, and Evaluate could not run without a net
    // that only Train could produce.
    void drawBlssHappyPath();
    // THE OTHER HALF OF THE HAPPY PATH: will the scene get FASTER. BLSS trades
    // GS fill for EE work at a measured price, so the answer is entirely a
    // question about overdraw - and blss::measureCoverage() counts a project's
    // own overdraw in about a second. Unlike every other verb in this window
    // this runs IN-PROCESS on a worker thread rather than as a subprocess:
    // there is no CLI verb behind it, it touches no file, and a second is not
    // worth a process. `blssCoverageTick` is the poll (called from blssPoll, so
    // a run that ends while the window is shut still lands).
    void blssStartCoverage();
    void blssCoverageTick();
    // ONE ANSWER, not two tables: the quality ceiling and the speed estimate
    // combined, in the window's own voice, including the case that occurs on
    // most scenes - no headroom AND below the break-even, so leave it off.
    void drawBlssAnswer();
    // The per-shot coverage breakdown, behind a collapsing header. A project
    // whose mean is 15 because one scene is 30 and another is 1 has been told
    // something useful only if it can see that.
    void drawBlssCoverageDetail();
    // The net's recorded command line against the project's CURRENT settings.
    // A blss.net stores no settings at all, so the `.args` sidecar is the only
    // place "this was trained with --sharpen 0.5 and the project now says 0.80"
    // can be noticed - everywhere else the mismatch is silent and just worse.
    void drawBlssProvenanceDrift();
    // WHICH OF THE TWO NETS THIS PROJECT SHIPS, as a control rather than a
    // status line. codegen's order is the project's own `blss.net` else the net
    // embedded in the editor, so the choice is a fact about ONE file - and the
    // switch is therefore a reversible RENAME (`blss.net.off`), never a delete.
    // It replaces the line that used to read "none - the game will be built
    // with RANDOM weights", which stopped being reachable the moment a default
    // shipped and was this feature's worst footgun while it was.
    void drawBlssNetSource();
    void blssSetNetAside(bool aside);
    // "Will this scene benefit at all?" - the answer --blss-eval already
    // contains and used to bury in a table. Reads the oracle row, which is the
    // scene's own ceiling: on examples/showcase it is +0.00 dB, and no network
    // can beat a bound of zero. `compact` drops the explanatory paragraphs for
    // the header, where the same verdict has to sit above six tabs.
    void drawBlssVerdict(const blssui::EvalSummary& sum, bool compact);
    // bilinear | BLSS | the amplified difference, click-through to Compare.
    void drawBlssVerdictThumbs();
    void drawBlssOutput(float height);
    void drawBlssTrainTab();
    void drawBlssEvalTab();
    void drawBlssCvTab();
    void drawBlssImagesTab();
    void drawBlssFeaturesTab();
    // WHAT THE CORPUS IS ALLOWED TO SEE (Project::blssShots). The six automatic
    // moves are a guess derived from the scene's bounds; this is where the
    // author says which of them survive and adds vantages of their own, aimed
    // where the player will actually stand. The plan is .tyra data, so the
    // trainer reads it out of the project rather than off a command line.
    void drawBlssShotsTab();
    // The shot list the corpus will shoot, as the window understands it -
    // resolved through project::blssResolveShot, which is also what the corpus
    // loader calls, so the preview cannot describe a different frame from the
    // one that gets rendered.
    void drawBlssShotPlanPreview();
    // The DISTRIBUTION PROBE as a workflow instead of a chore: turn on debug
    // view 2, run the game, and let the editor find the BLSSFEAT line in the
    // project's own log rather than making somebody grep for it and paste it
    // into a CLI. Under ps2link the game writes NO bin/log.txt at all
    // (templates.cpp sets writeLogsToFile = !ps2link), so the runner's own
    // [ps2] stream is the second source and the panel says which it used.
    void drawBlssProbeTab();
    // Fills blssProbeLine_ from the freshest source that has one. Returns the
    // human description of where it came from, or "" when nothing was found.
    std::string blssFindFeatLine();
    void blssRunProbe();
    // "Is this corpus good enough to train on" - the third verdict, next to
    // "will the picture improve" and "will the frame get faster". `compact`
    // drops the per-channel findings for the header block.
    void drawBlssHealth(bool compact);
    // Picks up a finished run. Called every frame from drawUI, NOT from the
    // window body, so a training run that ends while the tab is shut still
    // lands - the giBakerPoll rule.
    void blssPoll();
    void blssStart(blssui::Kind kind, const std::vector<std::string>& args, int epochs);
    // Pressed from the Train tab AND from the verdict, so the argument list is
    // built once - two copies is how one of them stops passing --all-shots.
    void blssStartTraining();
    void blssRestoreTrainDefaults();
    // The four tabs each carry their own frame count and are only comparable
    // when they agree; this says so when they have drifted.
    void drawBlssFrameDrift(int mine);
    std::vector<std::string> blssCommonArgs() const;
    std::string blssNetPath() const;
    void blssRefreshNetStatus();
    static void blssWriteNetSidecar(const std::string& netPath, const std::string& command);
    void blssReloadImages();
    void blssReleaseImages();
    // The amplified |A-B| view of the Compare tab, built on the CPU from the
    // two loaded PNGs. A 0.4 dB gap is invisible side by side and obvious at
    // 8x, which is why this is the one view worth adding.
    void blssRebuildDiff();
    // How many camera moves the corpus is expected to have - 13 for the
    // bestiary, six per scene for a project. Only the cross-validation cost
    // estimate needs it (its fold count defaults to one per shot).
    int blssExpectedShots() const;
    // How much GS VRAM the reduced render hands back on THIS project's raster,
    // and the honest answer for 1x2, which is "nothing".
    std::string blssVramLine(const ProjectSettings& s) const;
    // THE BUILD'S OWN INTERLOCK, mirrored live. blssClashes() in templates.cpp
    // emits an #error for each of these, so the dialog and the build must
    // answer alike; this is the ONE mirror, called by both Project >
    // Preferences (staged settings) and the BLSS window (live ones).
    //
    // It carries the NAMES now, not four bools. "A Set Depth Of Field flow node
    // turns it on at runtime" is not actionable on a ten-scene project - the
    // walk in blssClashesFor() has the scene and the object in hand and used to
    // throw both away - so each clash is a list of what caused it, and each
    // entry can be selected and framed.
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
    // HOW MUCH OF THE UPSCALER GOES ON SCREEN, and it is the whole shape of
    // this feature's UI (docs/neural-upscaler.md, "Two layers"). ONE definition
    // of the settings, one copy of every tooltip, one mirror of the build
    // interlock - and a parameter deciding how far down the block runs, the
    // drawBlssVerdict(compact) / drawBlssHealth(compact) idiom.
    //
    // `Essentials` is what somebody deciding whether to switch the feature ON
    // has to answer - use it, which reconstruction, which raster - plus the one
    // line of verdict that says whether this project is on the right side of
    // the break-even. `Everything` adds the four knobs that only mean anything
    // to somebody FITTING a network (sharpen, temporal, jitter, the debug view)
    // and the long-form prose, and it is what the window's Project settings tab
    // draws.
    //
    // The split is recent and it is a consequence of plain mode: until that
    // landed, BLSS MEANT "fit a network to your scene" and the whole window was
    // the feature. It is not the mainstream path any more - plain mode's
    // break-even is 2.6 coverages against the neural path's 13.1, a trained net
    // ships embedded, and on every project measured that net chooses nothing -
    // so the ordinary interaction is a checkbox, a mode and one line to read.
    enum class BlssDetail { Essentials, Everything };
    bool drawBlssSettings(ProjectSettings& s, BlssDetail detail = BlssDetail::Everything);
    // The speed estimate priced for `s` rather than for the committed project.
    // One function, because the Preferences dialog must re-price live when the
    // reader flips the reconstruction (the two modes' break-evens are more than
    // four times apart) and blssCoverageTick() must not answer differently.
    // A pure function of blssCov_ and the mode, so it cannot shimmer.
    blssui::SpeedEstimate blssSpeedFor(const ProjectSettings& s) const;
    // The one-line verdict for the Essentials layer: the button that measures,
    // the headline and the two facts behind it. Carries the honesty rules
    // wholesale by going through blssui::recommend() rather than deciding
    // anything of its own - a simpler UI must not become a more confident one.
    void drawBlssSpeedAnswer(const ProjectSettings& staged);

    // Tools > World Facts (facts_ui.cpp, docs/world-facts.md): the fact
    // catalog, the named queries over it, the reaction rules, the saved test
    // scenarios and the live World Blackboard. Its own TU (the prefab_ui.cpp
    // precedent) - it is a self-contained subsystem and app.cpp is already the
    // build's critical path.
    void drawWorldFactsWindow();
    void drawFactCatalogTab();
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
    void addScatterVolume();
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
    void drawMusicSection();
    void importMusicTrack();
    void drawSoundsSection();
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
    void drawSaveEditorWindow();
    void drawMenusWindow();
    void drawGradingWindow();
    void drawAmbienceWindow();
    void drawAmbiencePresets(bool& changed);
    void drawAmbienceDayCycle(bool& changed);
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
    // full-screen effects layer (bloom/grain), reorderable so effects can sit
    // under the crosshair/text instead of blurring them.
    void drawUiEditorWindow();
    // Loading Screens (Tools > Loading Screens): named loading screens (bg
    // color + images + baked texts + progress bars) assignable per scene, with
    // a 2D preview driven by a simulated load fraction.
    void drawLoadingScreenWindow();
    // Collapsing "Boot splash screens" section at the top of the Loading
    // Screens window: a list of images shown at startup, each for a set time.
    // Returns true when something changed (the caller saves).
    bool drawSplashSection();
    // Draws the 512x448-space loading-screen preview into the current ImGui
    // window (background, images, texts, bars honoring `fraction`).
    void drawLoadingPreview(const LoadingScreenDef& ls, float fraction);
    // Property editor for a single progress bar (returns true on change).
    bool loadingBarControls(LoadingBar& b);
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
    void handleFileDrop(int count, const char** paths);
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
    void drawWelcomeScreen();  // the Viewport's content while nothing is open

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
    void selectOnly(int i);     // replace the selection with {i} (i<0 clears)
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
    // Selection index the orbit pivot was last snapped to; -1 = none. Lets
    // "orbit around selection" re-center only when the selection changes.
    int navFocusedIndex_ = -1;

    Project project_;
    bool hasProject_ = false;
    // Unsaved model edits since the last save (drives the title "*" and the
    // discard-confirmation guard). Layout/docking changes do not set this -
    // they fold into the .tyra only when the user saves the project.
    bool dirty_ = false;
    bool titleShowsDirty_ = false;   // last title state pushed to GLFW
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
    int sessionPort_ = 7797;
    char sessionCode_[16] = "";
    std::string sessionError_;      // inline error in the join modal
    std::string sessionEndedText_;  // reason shown by the session-ended popup
    std::vector<session::PeerView> sessionPeers_;
    // Presence: what each OTHER participant has selected (object ids + the
    // scene index they are editing), keyed by peer id. Rendered as per-peer
    // outlines in the viewport and dots in the object list. Our own selection
    // is broadcast throttled whenever it changes.
    struct PeerPresence {
        int scene = 0;
        std::vector<std::string> sel;  // object ids
    };
    std::map<int, PeerPresence> peerPresence_;
    std::vector<std::string> presenceSentSel_;
    int presenceSentScene_ = -1;
    double presenceNextSend_ = 0.0;
    // Set by attachProject()/switchLayout(): load the active layout's saved ini
    // at the next frame boundary (ImGui cannot reload settings between NewFrame
    // and EndFrame). Recipe-built layouts use recipeRebuildPending_ instead.
    bool layoutLoadPending_ = false;
    int selectedObject_ = -1;
    // Full multi-selection (indices into the active scene's objects, in click
    // order). selectedObject_ == (selection_.empty() ? -1 : selection_.back()).
    std::vector<int> selection_;
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
    // the stack rather than starting a new pick.
    int viewportPick(float u, float v, ImVec2 mouse, bool* cycled);
    // Scene-objects list filters (view state, per session - a filter that
    // outlived a restart would hide objects nobody remembers hiding).
    // sceneFilterType_ holds a PrimitiveType value, or -1 for "every type".
    std::string sceneFilterName_;
    int sceneFilterType_ = -1;

    // Layouts saved before the Properties window existed lack a slot for it;
    // when set, the next frame docks it under the Project panel.
    bool dockPropertiesPending_ = false;

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
    // "Graph of" picker: list only objects that already own nodes (the ones
    // the list marks with a *). View state, per session - like the Scene
    // panel's filters above.
    bool flowOnlyWithNodes_ = false;
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
    // Tools > World Facts (docs/world-facts.md). Project-wide like Prefabs, so
    // the window is a tabbed list with an index and nothing about it is per
    // scene.
    bool showWorldFacts_ = false;
    int factSel_ = -1;        // selected catalog row
    int factQuerySel_ = -1;   // selected query
    int factRuleSel_ = -1;    // selected rule
    int factScenarioSel_ = -1;
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
    // bloom + color grading, 2 = film grain), the pinned USE prompt (3), a
    // HUD text (4, index in selectedText_) or a custom screen effect placement
    // (5, index in selectedFx_ into project_.screenFx).
    bool showUiEditor_ = false;
    bool showFontManager_ = false;
    bool showInputMap_ = false;
    int inputActionSel_ = 0;  // row selected in the Input Map action list
    int inputPresetSel_ = 0;  // preset tab being edited

    // Live Debugger panel (Tools > Debugger, the DBG toolbar chip, or the
    // built-in "Debugger" window layout).
    bool showDebugger_ = false;

    // Tree Generator (Tools > Tree Generator). The preview mesh + textures are
    // rebuilt into these on any param change; treePreviewVersion_ tells the
    // viewport when to re-upload. treeName_ is the asset base name.
    // Tools > Bake Global Illumination (docs/global-illumination.md). The bake
    // is EXPLICIT - never part of a build - so this window is where a project
    // learns that its lighting is stale, and the one place that fixes it.
    bool showGiBake_ = false;
    gibake::Baker giBaker_;
    // The probe grid currently uploaded to the viewport: reloaded when the
    // scene changes, the model is edited (which can stale the bake) or a bake
    // finishes.
    int giViewScene_ = -1;
    uint64_t giViewSerial_ = ~0ull;
    uint64_t giViewVersion_ = ~0ull;
    uint64_t giBakerSeen_ = 0;  // last Baker version pushed to the viewport

    // Tools > Neural Upscaler (BLSS) - blss_ui.cpp, docs/neural-upscaler.md.
    // One job at a time: every verb here saturates the machine, so a second
    // would only make both slower.
    bool showBlss_ = false;
    blssui::Job blssJob_;
    uint64_t blssJobSeen_ = 0;
    std::string blssPendingNet_;  // the net a running --blss-train will write
    std::string blssLastError_;
    // Negative so the very first frame the window is drawn refreshes rather
    // than waiting out the throttle - "no network" is the one thing this
    // window must never say wrongly.
    double blssStatusChecked_ = -1.0e9;
    float blssOutputH_ = 150.0f;
    // What the project's network is, and where it came from. A blss.net is a
    // bare list of floats and records nothing about how it was made, so the
    // provenance is a sidecar this editor writes next to the net it trained;
    // a net newer than its sidecar reports "unknown" rather than a stale one.
    bool blssNetPresent_ = false;
    // ...and whether a net this window set ASIDE is waiting to come back. The
    // net-source radio is the only writer of `blss.net.off`; without this the
    // "project's own net" option would go permanently dead the moment the
    // shipped default was selected.
    bool blssNetAside_ = false;
    bool blssNetArgsStale_ = false;
    size_t blssNetBytes_ = 0;
    std::string blssNetWhen_, blssNetArgs_;
    char blssNetName_[128] = "blss.net";
    char blssAssets_[512] = {};
    // WHICH CORPUS EVERY VERB IN THIS WINDOW RUNS ON, and it defaults to the
    // project because a measurement says so: a net fitted to the built-in
    // procedural bestiary scored -0.40 dB on examples/procedural, i.e. WORSE
    // than not reconstructing at all, while the same code fitted to that
    // project's own scenes scored +0.06 (commit 29b4de60). The bestiary is the
    // fallback for a project with nothing to draw, not the thing to ship.
    //
    // One switch for all five verbs rather than one per tab: "which corpus
    // produced this table" must have a single answer visible above every table
    // in the window, and a per-tab copy is how a Train on the project gets
    // evaluated against the bestiary without anyone noticing.
    bool blssCorpusProject_ = true;
    // Train
    int blssTrainFrames_ = 156, blssTrainEpochs_ = 400;
    unsigned blssTrainSeed_ = 0xB1557u;
    float blssTrainDecay_ = 1e-4f, blssTrainFill_ = 16.0f, blssTrainFlicker_ = 0.0f;
    // --all-shots ON by default, and it follows the corpus: the console runs
    // the frames the net was fitted on, so for a PROJECT corpus withholding a
    // third of it buys an honest held-out column nobody can act on and costs
    // the shipped net real quality.
    bool blssTrainAllShots_ = true, blssTrainStandardise_ = false;
    // Evaluate
    int blssEvalFrames_ = 156;
    float blssEvalDeadzone_ = 8.0f;
    bool blssEvalDump_ = true;
    std::string blssDumpDir_;
    // Cross-validate
    int blssCvFrames_ = 156, blssCvEpochs_ = 400, blssCvSeeds_ = 1, blssCvFolds_ = 0;
    bool blssCvSweep_ = false;
    char blssCvSweepList_[128] = "0,1,2,3,4,6,8,12,16,24";
    // Inputs
    int blssFeatFrames_ = 156;
    // Parsed out of the last run of each kind - never computed here.
    blssui::EvalTable blssEval_;
    // THE VERDICT'S NUMBERS, from the last Evaluate or "will this scene
    // benefit" run. Taken from the tool's own machine-readable `[blss] verdict`
    // line when it is there and re-derived from the parsed table when it is
    // not, so an older binary still answers.
    blssui::EvalSummary blssSummary_;
    // WHICH NET PRODUCED THE TABLE ON SCREEN, from the run's own announce line
    // rather than from the file system. With a default shipping, "I evaluated
    // my project" and "I evaluated the editor's net on my project" are one
    // keystroke apart, and until the line existed they looked identical.
    blssui::NetSource blssNetSource_;
    blssui::CvTable blssCv_;
    blssui::FeatureTable blssFeat_;
    // The third verdict - "is this corpus good enough to train on" - derived
    // once from blssFeat_ when a report lands, not per frame, for the same
    // reason blssSpeed_ is: a sentence a reader is going to quote must not
    // shimmer between two roundings. Unknown until a report exists, and
    // deliberately not reassuring while it is.
    blssui::CorpusHealth blssHealth_;
    // A CONSOLE frame placed in that corpus. Parsed from the same
    // `--features` run (the tool prints the probe table under the channel
    // table), so a run without --probe leaves it empty, which is correct.
    blssui::ProbeTable blssProbe_;
    blssui::ProbeVerdict blssProbeVerdict_;
    // The BLSSFEAT line being probed, and where it came from - the project's
    // bin/log.txt, the runner's [ps2] stream, or the user's own paste.
    char blssProbeLine_[2048] = {};
    std::string blssProbeSource_, blssProbeNote_;
    // What the corpus loader SAID it found, per scene, out of the last run's
    // output. It is how an author tells "the trainer honoured my shot plan"
    // from "the trainer is still shooting its six defaults", which is otherwise
    // invisible: a plan the tool ignores looks exactly like a plan it obeys.
    std::vector<blssui::CorpusScene> blssScenes_;
    // Training shots: which row of Project::blssShots is being edited.
    int blssShotSel_ = -1;
    // "Look through this shot" - the editor camera parked at a training
    // vantage. Held as state rather than pushed once, because drawUI CLEARS the
    // viewport's camera override on every frame nothing claims it (app.cpp,
    // the look-through camera branch), so a one-shot push lasts one frame.
    bool blssLookThrough_ = false;
    float blssLookEye_[3] = {0.0f, 0.0f, 0.0f};
    float blssLookAt_[3] = {0.0f, 0.0f, 1.0f};
    float blssLookFov_ = 60.0f;
    // THE SPEED HALF. `blssCov_` is the UI's copy and is only ever written by
    // blssCoverageTick after the worker has finished (the version bump is the
    // handover, the Runner/giBaker idiom); `blssCovOut_` is the worker's own
    // slot and must not be read while it runs.
    blss::CoverageReport blssCov_, blssCovOut_;
    std::thread blssCovThread_;
    std::atomic<bool> blssCovRunning_{false};
    std::atomic<bool> blssCovCancel_{false};
    std::atomic<uint64_t> blssCovVersion_{0};
    uint64_t blssCovSeen_ = 0;
    double blssCovStarted_ = 0.0, blssCovSeconds_ = 0.0;
    // The estimate, derived from blssCov_ once per finished run rather than per
    // frame - blssui::speedFrom is pure and cheap, but the verdict text is what
    // a reader quotes and it must not flicker between two roundings.
    blssui::SpeedEstimate blssSpeed_;
    // Which tab to force open next frame (-1 = leave it alone). The verdict's
    // thumbnails are click-through to Compare, and a strip that showed the
    // pictures but could not get you to them would be decoration.
    int blssTabSelect_ = -1;
    // The comparison PNGs --blss-eval --dump wrote, as GL textures. The PIXELS
    // are kept as well, because the difference view is computed from them on
    // the CPU - ~900 KB each at 512x448, nine of them.
    struct BlssImage {
        std::string label, tip, path;
        unsigned tex = 0;
        int w = 0, h = 0;
        std::vector<unsigned char> px;  // RGBA, w*h*4
    };
    std::vector<BlssImage> blssImages_;
    bool blssImagesDirty_ = true;
    int blssImgA_ = 0, blssImgB_ = 0, blssImgMode_ = 0;
    float blssWipe_ = 0.5f, blssZoom_ = 1.0f;
    // |A-B| amplified. Rebuilt when the pair or the amplification changes, and
    // never per frame - it is a full-image CPU pass.
    unsigned blssDiffTex_ = 0;
    int blssDiffW_ = 0, blssDiffH_ = 0;
    int blssDiffA_ = -1, blssDiffB_ = -1;
    float blssDiffAmp_ = 8.0f, blssDiffAmpBuilt_ = -1.0f;
    double blssDiffPeak_ = 0.0, blssDiffMean_ = 0.0;  // the honest scale of the gap

    bool showTreeGenerator_ = false;
    treegen::Params treeParams_;
    int treePreset_ = 0;
    treegen::Mesh treeMesh_;
    treegen::Image treeBarkTex_, treeLeafTex_;
    uint64_t treePreviewVersion_ = 0;
    bool treePreviewDirty_ = true;
    char treeName_[64] = "tree";
    float treeGenAngle_ = 40.0f, treeGenPitch_ = 18.0f, treeGenZoom_ = 1.0f;
    bool treeGenSpin_ = true;
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

    int selectedHud_ = -1;
    int uiFxSel_ = 0;
    int selectedText_ = -1;
    // Font Manager selection (index into Project::fonts).
    int fontSel_ = 0;
    // Cached atlas footprint line: measuring it walks all 95 glyphs, so it is
    // recomputed only when a bake-affecting field changes (see fontAtlasKey_).
    std::string fontAtlasKey_;
    std::string fontAtlasInfo_;
    bool fontAtlasClipped_ = false;
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
    std::string animEdLight_;  // preview lighting, see matEdLight_
    float animEdYaw_ = 40.0f, animEdPitch_ = 15.0f, animEdZoom_ = 1.0f;
    float animEdPanX_ = 0.0f, animEdPanY_ = 0.0f;  // radius-relative target
    double animEdClock_ = 0.0;  // wall clock of the previous frame
    char animEdRename_[64] = {};  // rename field buffer for the selected clip

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
    void drawMenuPreview(const GameMenu& m);
    // The preview in three parts, because it is drawn in two windows and must
    // not become two previews: refresh owns the bake, controls the mode picker
    // and the simulated cursor, draw the image. The texture is shared - a
    // display mode changes presentation, not what is baked.
    bool menuPreviewRowUsable(const GameMenu& m, const menulayout::Layout& L,
                              int row) const;
    void menuPreviewStep(const GameMenu& m, const menulayout::Layout& L, int dir);
    void menuPreviewRefresh(const GameMenu& m);
    void menuPreviewControls(const GameMenu& m, int& mode);
    void menuPreviewDraw(const GameMenu& m, int mode, float zoom);
    void drawMenuPreviewWindow();
    void drawMenuStyleTab(GameMenu& m, bool& projectChanged);
    void drawMenuStyleText(GameMenu& m, bool& projectChanged);
    void drawMenuCost(const GameMenu& m);
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
    int menuPreviewMode_ = 0;      // 0 = panel 1:1, 1 = TV PAL, 2 = TV NTSC
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

    // Loading Screens (Tools > Loading Screens): selected screen + selected
    // element within it (lsSelKind_: 0 image / 1 text / 2 bar; lsSelIdx_ into
    // that element vector) + the preview's simulated load fraction.
    bool showLoadingEditor_ = false;
    int selectedLoadingScreen_ = -1;
    int lsSelKind_ = 0;
    int lsSelIdx_ = -1;
    float lsPreviewProgress_ = 0.65f;
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
    // The preview strip's share of the window height, dragged on the splitter
    // above it and persisted in editor.ini (machine setting, like matEdSplit_).
    float creditsSplit_ = 0.42f;
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
    bool seqPreview_ = true;      // pose the viewport at the playhead
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
    std::map<std::string, uint32_t> liveTexGen_;  // game-relative -> generation
    uint32_t liveTexSeq_ = 0;
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
    bool newAiClaude_ = false;
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

    // "Project Preferences" - an ordinary WINDOW, and it applies live like
    // every other panel in this editor (see drawPreferencesWindow for why the
    // staged OK/Cancel it used to have could not survive being non-modal).
    // `prefSettings_` is a ONE-FRAME copy, not a staging buffer: re-seeded from
    // project_.settings at the top of the body and written back at the bottom.
    // The game template is not copied - it is fixed at creation and the window
    // only displays it.
    bool showProjectPrefs_ = false;
    bool focusProjectPrefs_ = false;  // menu/shortcut re-open raises the window
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

    // "Editor Preferences" modal staging (Edit > Preferences, applied on Save).
    // Machine-global settings, mirror of globalEmulatorPath_ / globalPs2Ip_.
    bool openEditorPrefsPopup_ = false;
    char prefEmulatorPath_[512] = "";  // PCSX2 exe path (auto-detect if empty)
    char prefPs2Ip_[64] = "";          // ps2link IP for Run on PS2
    char prefDefaultProjectsDir_[512] = "";  // default parent folder for new projects
    char prefDisplayName_[48] = "";          // session display name (editor.ini)
    char prefSessionCacheDir_[512] = "";     // remote-project cache root override
    int prefAiBackend_ = 0;            // index into aigen::backendIds()
    char prefAiModel_[128] = "";       // "" = the backend's default model
    // Model combo shows "Custom..." + a free-text field when the staged model
    // is not one of the backend's listed models (or the user picked Custom).
    bool prefAiCustomModel_ = false;
    bool prefAiThinking_ = false;

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
    // project preference (also in Build > Live Link and Preferences > Build).
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
    livedbg::Snapshot dbgSnap_;     // newest snapshot the game wrote
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
    float dbgFps_ = 0.0f;           // measured against the editor's wall clock
    int dbgScrub_ = -1;             // timeline index being inspected (-1 = live)
    std::string dbgWatchFilter_;    // Watch tab search box (name or kind)
    void livedbgTick();
    void drawDebuggerWindow();

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
    void drawTimeMachinePanel();

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
    int dbgVuMesh_ = 0;  // which position stream of the flush the preview draws
    bool dbgVuPinFlush_ = false;  // re-grab one draw instead of walking them
    int dbgVuFlushWanted_ = 0;    // ...which one
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
        Blocked     // an edited graph cannot be hot-patched (rebuild needed)
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
