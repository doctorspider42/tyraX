#pragma once

#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include <imgui.h>  // ImGuiStyle baseStyle_ member (UI scaling)

#include "camtake.hpp"
#include "history.hpp"
#include "isoexport.hpp"
#include "project.hpp"
#include "runner.hpp"
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
    void drawFlowGraphWindow();
    // Names used by same-type "Variables" nodes across every scene's graphs
    // (the int / bool / position namespaces are separate).
    std::vector<std::string> flowVarNames(const std::string& nodeType) const;
    // Open the project in VS Code. A non-empty `file` (project-relative or
    // absolute) is also opened in that window - jump straight to a script /
    // custom node while keeping the whole project in context.
    void openInVSCode(const std::string& file = "");
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
    // Creates a scene object for a model already in res/models (no copying)
    void addModelObject(const std::string& relPath);
    // Project-panel section listing res/models + res/textures with the
    // Import... buttons (the object pickers only offer what is listed here)
    void drawAssetsSection();
    // Files directly under res/<subdir> with the given extension (lowercase
    // compare), names only, sorted by the directory iteration order
    std::vector<std::string> listAssetFiles(const char* subdir, const char* ext);
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
    // Shared TTF picker (menus, HUD texts): default chain / project fonts /
    // stock Windows fonts / import. Returns true when fontPath changed.
    bool fontCombo(std::string& fontPath);
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
    // Material Editor (Tools > Material Editor): authors the .mtl files the
    // whole pipeline already consumes (newmtl/Kd/map_Kd) with a live preview.
    void drawMaterialEditorWindow();
    void openMaterialEditor(const std::string& relPath);  // load + show
    bool loadMaterialFile(const std::string& relPath);    // disk -> matEd* staging
    void saveMaterialFile();  // matEd* staging -> disk + cache invalidation
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
    enum class PendingAction { None, Exit, Open, New };
    void requestExit();
    void requestOpenProject();
    void requestNewProject();
    void performPendingAction();
    void drawDiscardModal();
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
    // Selection index the orbit pivot was last snapped to; -1 = none. Lets
    // "orbit around selection" re-center only when the selection changes.
    int navFocusedIndex_ = -1;

    Project project_;
    bool hasProject_ = false;
    // Unsaved model edits since the last save (drives the title "*" and the
    // discard-confirmation guard). Layout/docking changes do not set this -
    // they fold into the .tyra only when the user saves the project.
    bool dirty_ = false;
    bool titleShowsDirty_ = false;  // last title state pushed to GLFW
    std::string titleName_;         // project name currently in the title
    // Discard guard (Exit/Open/New while dirty). pendingAction_ is what runs
    // once the user resolves the modal; exitConfirmed_ lets the main loop close
    // after a confirmed Exit without re-prompting.
    PendingAction pendingAction_ = PendingAction::None;
    bool openDiscardPopup_ = false;
    bool exitConfirmed_ = false;
    // Set by attachProject(): apply project_.windowLayout at the next frame
    // boundary (ImGui cannot reload settings between NewFrame and EndFrame).
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
    int selectedHud_ = -1;
    int uiFxSel_ = 0;
    int selectedText_ = -1;
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
    std::string matEdPath_;  // project-relative path of the open .mtl ("" = none)
    struct MatEdEntry {
        std::string name;
        float color[3] = {1.0f, 1.0f, 1.0f};
        float brightness = 1.0f;  // folded into Kd on save (see saveMaterialFile)
        std::string texture;      // map_Kd, relative to the .mtl dir ("" = none)
        float tile = 1.0f;        // map_Kd -s: texture repeats per world unit
                                  // (terrain only; objects have baked UVs)
        std::vector<std::string> extra;  // unrecognized lines, preserved verbatim
    };
    std::vector<MatEdEntry> matEdMats_;
    int matEdSel_ = 0;         // selected entry within the file
    int matEdShape_ = 1;       // preview: 0 box, 1 sphere, 2 cylinder, 3 cone
    bool matEdSpin_ = true;    // turntable
    float matEdAngle_ = 40.0f;
    bool openNewMaterialPopup_ = false;
    char matEdNewName_[64] = "my-material";
    std::string matEdNewError_;
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

    // "New project" modal state
    bool openNewProjectPopup_ = false;
    char newName_[128] = "my-game";
    char newLocation_[512] = "";
    int newWidth_ = 64;
    int newDepth_ = 64;
    int newTemplate_ = 0;  // 0 = empty, 1 = fpp
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

    // "Debug" window: tails a log from disk (reloaded, throttled). Source 0 is
    // the game's own log (bin/log.txt, written by TYRA_LOG); source 1 is the
    // emulator's console log (PCSX2 emulog.txt, boot progress + asserts).
    std::string debugLog_;
    int debugLogSource_ = 0;
    bool debugAutoReload_ = true;
    double debugNextReload_ = 0.0;  // ImGui::GetTime() gate for the next read

    // "Scene Preferences" modal staging (applied on OK): the active scene's
    // per-category overrides of the project defaults.
    bool openScenePrefsPopup_ = false;
    int scenePrefScene_ = -1;  // scene index the staging belongs to
    ProjectSettings scenePrefSettings_;
    SceneOverrides scenePrefOverrides_;
    std::string scenePrefAmbience_;  // staged SceneData::ambiencePreset

    std::string statusMessage_;
};
