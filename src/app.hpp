#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include <imgui.h>  // ImGuiStyle baseStyle_ member (UI scaling)

#include "aigen.hpp"
#include "camtake.hpp"
#include "history.hpp"
#include "matbake.hpp"
#include "isoexport.hpp"
#include "project.hpp"
#include "runner.hpp"
#include "session.hpp"
#include "viewport.hpp"

struct GLFWwindow;

// Viewport navigation preferences. These are a machine/muscle-memory property,
// not project data, so they live in the global editor config (editor.ini in
// %LOCALAPPDATA%), never in the per-project .tyra. See NavConfig persistence in
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
    float zoomSensitivity = 1.0f;
    bool invertX = false;  // reverse horizontal orbit direction
    bool invertY = false;  // reverse vertical orbit direction
    bool orbitAroundSelection = true;  // pivot snaps to the selected object
};

class App {
public:
    // initialProjectDir: optional project to open on startup (may be empty)
    int run(const std::string& initialProjectDir = "");

private:
    void drawUI();
    void drawMenuBar();
    // Icon toolbar drawn inline in the main menu bar (Save / Run in PCSX2 /
    // Run on PS2 / Stop). Custom vector-drawn - the editor loads no icon font.
    void drawToolbar();
    // UI (DPI) scaling. uiScaleUser_ == 0 means "auto" (follow the monitor's
    // content scale); a value > 0 is an explicit multiplier (1.0 == 100%).
    // applyUiScale() recomputes the effective scale and re-applies it to the
    // ImGui style + fonts; setUiScale() also persists the choice.
    void applyUiScale();
    void setUiScale(float userScale);
    // Multiply a design-time pixel size (widget widths, child regions, window
    // sizes, hand-drawn previews) by the active UI scale, so code literals
    // track DPI/zoom the same way fonts and style spacing already do (see
    // applyUiScale: FontScaleMain + ScaleAllSizes). Any tool window that gives
    // a widget a literal pixel size should route it through this or the text
    // clips at high scale (a 180 px combo can't hold 2.5x-tall glyphs).
    float scaled(float px) const { return px * uiScaleApplied_; }
    void drawViewportWindow();
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
    void drawDiscLayoutWindow();
    void drawNewProjectModal();
    void drawPreferencesModal();          // project-wide defaults (Project menu)
    void drawEditorPreferencesModal();    // machine-global settings (Edit menu)
    void saveGlobalConfig();              // write editor.ini from the App members
    void drawNavigationModal();  // global viewport-navigation settings
    void drawScenePreferencesModal();
    void openScenePreferences();  // stage the active scene into scenePref* + open
    void openProjectDialog();
    void applyProjectToViewport();
    void addObject(PrimitiveType type);
    void addEmitter(int kind);  // Effects menu presets (fire/smoke/fog/sparks)
    void addSoundEmitter();
    void addPointLight();
    void addSavePoint();
    void addEmpty();
    void addDecal();
    void addMirror();
    void addPortal();
    void drawAddObjectMenu();
    // Copies a picked .obj (with its .mtl + textures, references rewritten to
    // the sanitized names) into res/models. Returns the project-relative path
    // of the model, or "" when cancelled/failed. Does NOT create an object.
    std::string importModelAsset();
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
    // Per-object animation/mesh LOD override rows (animated models + player
    // avatars). Returns true when a value changed (caller commits).
    bool drawLodOverrides(SceneObject& o);
    // Creates a scene object for a model already in res/models (no copying)
    void addModelObject(const std::string& relPath);
    // Project-panel section listing res/models + res/textures with the
    // Import... buttons (the object pickers only offer what is listed here)
    void drawAssetsSection();
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
    // A Font Manager entry pointing at `relPath` ("res/fonts/x.ttf"), creating
    // one named after the file stem if none exists. Returns the entry's name -
    // what a `font` reference stores. Used by the TTF import paths.
    std::string ensureFontForPath(const std::string& relPath);
    // Renames a font and follows the reference into every text, menu and
    // Display Text node, the way HUD text renames do.
    void renameFont(int index, const std::string& newName);
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
    void drawMenusWindow();
    void drawGradingWindow();
    void drawAmbienceWindow();
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
    void commitChange();
    void saveAll(const char* status);
    void applySnapshot(const SceneSnapshot& s);
    void undo();
    void redo();
    // Dirty tracking: unsaved model edits since the last save. setDirty keeps
    // the window title's "*" marker in sync.
    void setDirty(bool dirty);
    void updateWindowTitle();

    // Guarded actions that would discard unsaved edits (Exit / Open / New).
    // When the project is dirty they open a confirm modal instead of running
    // immediately; the modal's Save/Discard buttons then run the pending one.
    enum class PendingAction { None, Exit, Open, New, JoinSession };
    void requestExit();
    void requestOpenProject();
    void requestNewProject();
    void performPendingAction();
    void drawDiscardModal();

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
    // (empty = USERNAME) and the remote-project cache root (empty = default).
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
    // Set every frame by drawFlowGraphWindow(): the Flow Graph window (or one of
    // its children) has keyboard focus, so Ctrl+C/V copy nodes, not scene objects.
    bool flowGraphFocused_ = false;
    // Clipboard for flow-graph copy/paste: the copied nodes plus the links that
    // connect two of them (dangling links are dropped). nextId is unused.
    FlowGraph flowClipboard_;
    // Node-description tooltip (FlowNodeType::desc): node the mouse rests on
    // and since when - shown after a short delay, reset on hover change.
    int flowDescNode_ = -1;
    double flowDescSince_ = 0.0;

    // Viewport overlays: TV frames (PAL 4:3 and NTSC, which shows a
    // slightly wider slice of the same 512x448 buffer)
    bool showPal_ = false;
    bool showNtsc_ = false;
    bool showHudInEditor_ = false;  // HUD preview overlay (default hidden)
    // Distance-fog preview in the viewport (View menu). On by default; when off
    // the scene's fog is suppressed in the editor so distant geometry stays
    // visible. Editor-only preview toggle - does not touch the generated game.
    bool showFog_ = true;

    // UI Editor (Tools > UI Editor): selected screen-stack entry - a HUD image
    // (uiFxSel_ == 0, index in selectedHud_), an effect layer (uiFxSel_ 1 =
    // bloom + color grading, 2 = film grain), the pinned USE prompt (3), a
    // HUD text (4, index in selectedText_) or a custom screen effect placement
    // (5, index in selectedFx_ into project_.screenFx).
    bool showUiEditor_ = false;
    bool showFontManager_ = false;
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

    // Menus editing (Menu Editor window): selected menu + a live preview of
    // the baked panel (re-baked whenever the menu's content changes)
    bool showMenusEditor_ = false;
    int selectedMenu_ = -1;
    unsigned menuPreviewTex_ = 0;
    int menuPreviewW_ = 0, menuPreviewH_ = 0;
    int menuPreviewContentH_ = 0;  // drawn part (layout cached at bake time)
    bool menuPreviewClipped_ = false;  // content hit the 512px texture cap
    int menuPreviewMode_ = 0;      // 0 = panel 1:1, 1 = TV PAL, 2 = TV NTSC
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

    // Loading Screens (Tools > Loading Screens): selected screen + selected
    // element within it (lsSelKind_: 0 image / 1 text / 2 bar; lsSelIdx_ into
    // that element vector) + the preview's simulated load fraction.
    bool showLoadingEditor_ = false;
    int selectedLoadingScreen_ = -1;
    int lsSelKind_ = 0;
    int lsSelIdx_ = -1;
    float lsPreviewProgress_ = 0.65f;
    int selectedSplash_ = -1;  // boot splash screen being edited (-1 = none)

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
    // Applies the loaded take (seqTake_/seqTakeMap_/seqTakeTarget_) to sequence
    // s: free shots -> camera lane (replace or append); a Camera entity target
    // -> its transform track + FOV + a bound camera key. Returns the first key
    // time (for the playhead), or -1 on no-op.
    float applyCamTake(Sequence& s, bool replace);
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
        std::vector<std::string> extra;  // unrecognized lines, preserved verbatim
    };
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
    // Texture-bake controls (pow2 size + quantization) shared by HUD images
    // and the USE prompt in the UI Editor. Returns true on change.
    bool hudBakeControls(HudImage& h);
    // The embedded built-in USE prompt sprite (viewport overlay preview).
    const HudTexture* builtinUseTexture();
    HudTexture builtinUseTex_;
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
    navmesh::NavGrid navGrid_;
    uint64_t navOverlaySig_ = 0;
    uint64_t navOverlayVersion_ = 0;
    void updateNavOverlay();

    // "New project" modal state
    bool openNewProjectPopup_ = false;
    char newName_[128] = "my-game";
    char newLocation_[512] = "";
    int newWidth_ = 64;
    int newDepth_ = 64;
    int newTemplate_ = 0;  // 0 = empty, 1 = fpp
    // "Add AI support": install the assistant skill files (aisupport.hpp)
    // into the fresh project. Also available later in Project Preferences.
    bool newAiClaude_ = false;
    bool newAiCopilot_ = false;
    std::string newProjectError_;

    // "New script" modal state. newScriptAttachTo_ >= 0 = attach the created
    // script to that object of the active scene (Properties > Scripts flow).
    bool openNewScriptPopup_ = false;
    char newScriptName_[64] = "my_script";
    std::string newScriptError_;
    int newScriptAttachTo_ = -1;

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

    // "Project Preferences" modal staging (applied on OK). Edits project-wide
    // defaults only (project_.settings + terrain + game template).
    bool openPreferencesPopup_ = false;
    TerrainConfig prefTerrain_;
    int prefTemplate_ = 0;
    ProjectSettings prefSettings_;

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

    // "Debug" window: tails a log from disk (reloaded, throttled). Source 0 is
    // the game's own log (bin/log.txt, written by TYRA_LOG); source 1 is the
    // emulator's console log (PCSX2 emulog.txt, boot progress + asserts).
    std::string debugLog_;
    int debugLogSource_ = 0;
    bool debugAutoReload_ = true;
    double debugNextReload_ = 0.0;  // ImGui::GetTime() gate for the next read

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

    // "Scene Preferences" modal staging (applied on OK): the active scene's
    // per-category overrides of the project defaults.
    bool openScenePrefsPopup_ = false;
    int scenePrefScene_ = -1;  // scene index the staging belongs to
    ProjectSettings scenePrefSettings_;
    SceneOverrides scenePrefOverrides_;
    std::string scenePrefAmbience_;  // staged SceneData::ambiencePreset
    std::string scenePrefLoading_;   // staged SceneData::loadingScreen

    std::string statusMessage_;

    // Install the bundled VS Code extension once per session (from openInVSCode);
    // vsCodeExtStatus_ keeps the last outcome so it can be shown to the user.
    bool vsCodeExtInstallTried_ = false;
    std::string vsCodeExtStatus_;
};
