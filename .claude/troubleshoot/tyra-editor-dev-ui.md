# tyra-editor-dev — panels, DPI, ImGui traps, Preferences window

From `### 5. Conventions`, `### 3` (the Preferences window shape), `app.cpp`.
See `docs/editor-theme.md`.

## Panel text is a wall of prose
**Cause/rule:** users come here to make games, not to read. A panel states facts:
numbers, names, one-line verdicts.
**Fix:** a multi-line `TextDisabled` paragraph in a panel body is a defect, not
documentation (the animation-import section shipped one and the user's verdict
was "wielka litania"). What needs explaining goes in a tooltip ON the thing it
explains — 1-3 short lines, no scene-setting, no restating the label. The long
story belongs in `docs/`, one click away through the AI Assistant. `prefHelp`'s
dimmed `(?)` is the idiom for a control whose label cannot carry it alone; prefer
no marker at all.

## A fixed pixel size clips or misaligns at high DPI
**Cause:** `applyUiScale()` scales fonts (`FontScaleMain`) and style spacing
(`ScaleAllSizes`) but NOT the pixel literals passed to ImGui.
**Fix:** wrap literal pixel sizes in `App::scaled(px)` (= `px * uiScaleApplied_`)
— `SetNextItemWidth(180)`, `BeginChild(ImVec2(170,0))`, absolute
`SameLine(190)`/`Indent(46)`, fixed button sizes, hand-drawn previews. Negative/
`-FLT_MIN`/fill widths and text-measured (`CalcTextSize`) sizes already track
scale; leave those alone. Free functions drawing fixed-size widgets take a
`scale` param (see `gradingWheel`). **`ScaleAllSizes` also does not reach a
third-party style struct** — `ImNodesStyle` (grid spacing, node padding, pin
radii, link thickness) is the editor's, so the Flow Graph scales it itself; a new
vendored widget library with its own style struct owes the same.

## A widget drawn on top of a `Selectable` is dead or fires the wrong action
**Cause:** an item owns its whole rect, so the `x` on each row of the AI
Assistant's chat-history popup hovered nothing and passed its click through to
the row underneath — which OPENED the chat you were trying to delete.
**Fix:** submit the `Selectable` with `ImGui::SetNextItemAllowOverlap()`. Lay
such a row out in COLUMNS, not by flowing `SameLine()` after each other's text —
a title one word longer otherwise pushes that row's trailing widgets right, and in
an auto-sized popup the widest row sets the width while narrower rows' buttons
float mid-row. Measure the widest label over the whole list first (`CalcTextSize`),
then place each row's items at fixed `SameLine(x)` offsets from the row's own
`GetCursorPosX()`.

## A zoomable canvas scales nothing
**Cause:** `SetWindowFontScale` writes `window->FontWindowScale`, and since ImGui
1.92 `UpdateCurrentFontSize()` reads that field for the CURRENT window only — a
per-window font scale does not reach anything inside a `BeginChild`, which is
where imnodes (and any canvas) puts its content. The call compiles and silently
scales nothing.
**Fix:** use `PushFont(nullptr, sizePx)`, which sets the context-level
`FontSizeBase` and children inherit it. Two consequences the Flow Graph's zoom is
built around (`flowgraph_ui.cpp`, PROGRESS 233): **derive every length from the
rounded font pixel size, not from the zoom** (ImGui rounds font sizes, so text
width is a staircase while a raw `zoom` multiplier is a straight line — snap the
zoom to a whole font pixel and the view stays self-similar); and a **stored node
position is a distance between nodes**, so it carries the SAME factor the node
contents do (UI scale included), or a 250% editor draws grown nodes at un-grown
spacing and they overlap.

## A tool is in the wrong menu or a submenu
**Cause/rule:** the Tools menu is ONE flat list under `SeparatorText` headers,
alphabetical inside each header. Not submenus: every doc page, tooltip and AI
prompt names a tool as `Tools > X`, and a submenu would put a word into all of
those paths (and into every `--ui-script` that clicks one).
**Fix:** headers answer "what am I working on" — *Assets*, *Scene*, *Lighting &
rendering*, *Screens & menus*, *Gameplay*, *Running game*, *AI* (last, its own
group). A new tool joins the header whose question it answers, at its alphabetical
place (Vehicle Editor belongs to Assets after Tree Generator; Global Illumination
follows Color Grading in Lighting & rendering). Appending at the bottom is how the
menu became a 29-entry pile.

## A new preference's apply is destructive or commits silently
**Cause/rule:** the *Project* Preferences dialog is a WINDOW
(`drawPreferencesWindow`, key `projectprefs`) that applies LIVE, not a modal
staging into `prefSettings_`. Staging could not survive a non-modal window (the
project can be edited underneath — undo, a session peer, the AI Assistant — so an
OK afterwards would overwrite it with minutes-old values, and `prefTerrain_` stages
the ACTIVE SCENE's terrain, so a scene switch would write scene A's size onto
scene B).
**Fix:** `prefSettings_` is a ONE-FRAME copy re-seeded from `project_.settings` at
the top of the body, compared and written back at the bottom (covers a widget
added to any tab by construction — the sectionJson-guard reasoning over a struct
with an `operator==`). Five tabs split by the QUESTION: *Display*, *Rendering*,
*World*, *Player*, *Build*. A control whose apply is destructive (the terrain grid
width/depth/detail cap, scratch `prefTerrain_`/`prefGridDetail_`) is written back
only on `IsItemDeactivatedAfterEdit`, re-seeded from the model on any frame the
widget is neither active nor just-committed (`project::ensureHeightmap` answers a
size change with a NEAREST-NEIGHBOUR RESAMPLE; `Viewport::setTerrain` re-centres
the camera). The footer is pinned OUTSIDE the scrolling region (every tab body is
a `BeginChild` with `-footerH` reserved). A button that opens another window just
opens it (settings are live, so the two cannot disagree; a `--ui-script` click on
a covered Preferences item lands on the window in front — assert those with
`expect-checked`, which does not click).

## A checkbox edit silently never commits
**Cause:** **a checkbox never reports `IsItemDeactivatedAfterEdit()`** — it
activates on mouse-down and both edits and deactivates on mouse-up, so the "was
edited while active in a PREVIOUS frame" test can never be true. Three shipped
checkboxes were asking (`Fog enabled`, `Gradient sky dome`, a VU stage's
`Enabled`).
**Fix:** use the return value (`if (ImGui::Checkbox(...)) commitChange();`).
`IsItemDeactivatedAfterEdit` is for widgets you DRAG (sliders, drags, colour
edits, text fields); anything that commits on the click itself reports through its
return value.

## A preference that changes the picture does nothing until the scene changes
**Cause:** `commitChange()` does not touch the viewport — it pushes undo and marks
dirty.
**Fix:** a setting that changes the PICTURE must call `applyProjectToViewport()`
itself (the GI switch did not, so unticking it left the baked light on screen).
Settings that only change what a future bake produces must NOT call it. The
refresh is deferred: the body sets `prefsViewportDirty_` and the refresh runs on
the first frame with `!ImGui::IsAnyItemActive()`, BEFORE the commit (it also
resamples the heightmap after a grid change).

## `prefHelp` chains onto the wrong widget
**Cause:** it is `SameLine` + a dimmed `(?)`, so a help marker written a few
widgets later chains onto whatever came before (Live Link and Live Logic had
theirs stranded at the bottom of the section).
**Fix:** `prefHelp` belongs to the widget it FOLLOWS. And ImGui does no
accelerator parsing — `&&` in a label draws two ampersands (*Keyboard && mouse
controls* did).

## A cutscene restoring the HUD switches it back on
**Cause/rule:** **Hide HUD is `ScriptContext::hudSuppressed`, a SECOND flag beside
`hudVisible`** — the Set HUD Visible node owns that one, and a cutscene restoring
it to true on release would switch a HUD back on the game had deliberately hidden.
It covers more than `hudVisible` does (the stack, the bars, the baked texts, the
USE prompt and `updateUseTarget` itself) and deliberately not runtime text, which
is where subtitles live.
**Fix:** keep the two flags separate; do not collapse them.

## A skippable cutscene is unskippable in a project with a pause menu
**Cause:** the director used to test the raw Start button in its own `update()` —
the press opened the menu, the open menu paused the scripts, and the director
never ran to see it.
**Fix:** **a skippable cutscene OWNS the `menu` action**, intercepted at the top of
`updateGameMenu` BEFORE the pause toggle; that ordering is the whole fix. The
confirmation screen is one more per-project menu ROLE (`GameMenu::skipMenu`, the
`saveMenu` precedent) plus one more `MenuEntry::Action`; declining needs no action
of its own, so a Close row is the whole "no".

## A log panel draws from text that moved mid-frame
**Cause/rule:** the Output and Debug log panels share ONE body (`logSetText`/
`logRefresh`/`drawLogPanel` over a `LogView`, classification in logview.cpp) — a
new log surface calls those three rather than growing a second answer.
`LogView::visible` holds INDICES into the text `LogView::text` owns, so anything
that replaces the text mid-frame (Clear log, the source combo) must be followed by
a `logRefresh` before the body draws (which is why `drawLogPanel` calls it
itself).
**Fix:** the Debug window READS its file before submitting its widgets, because
the buttons report on the classified lines — hence `debugReloadNow_` arming the
next frame's read.

## Reference facts (upstream `editor-ui-and-conventions.md`)
- Shell/TUs: `app.cpp` (~17.2k); `app.hpp` (~3.9k) declares the WHOLE `App` class; `app_internal.hpp` (~300) shared helpers (`PickKind`, `sanitizeAssetName`, `fileSizeOr0`, `readTextFileTail`, `prefHelp`, `walkSpeedDrag`). `props_ui.cpp` (~3.7k): `drawPropertiesWindow`/`drawMultiProperties`/`drawLodOverrides`/`drawDistantVersion`. Subsystem TUs (props, flowgraph, hud, procui, cutscene, credits, menustyle, mateditor, devkit, chat, facts, prefab, particle, batch, atlas, junction, vehicle, vu, modelimport, update, blss_window, droneui, assetbrowser).
- Preferences `drawPreferencesWindow` (key `projectprefs`): non-modal live window; `prefSettings_` one-frame copy; destructive controls keep a scratch (`prefTerrain_`/`prefGridDetail_`); a checkbox commits on its RETURN VALUE; deferred `prefsViewportDirty_` -> `applyProjectToViewport()`; footer pinned outside the scroll; five tabs by question (Display/Rendering/World/Player/Build); `prefHelp` follows its widget.
- editor.ini (`EditorConfig` + `loadEditorConfig`/`saveEditorConfig` + `saveGlobalConfig()` + `drawEditorPreferencesModal`); `editorcfg.hpp` (`configPath()`/`recentProjects()`/`defaultProjectsDir()`); `NavConfig` field is four places; `openInVSCode`/`errorPopup`/`giGpuBake`.
- Window layouts: `Project::windowLayouts` (`WindowLayout{name, ini, recipe, openWindows}`) + `activeLayout`; `App::buildLayoutRecipe`/`LayoutRecipe`/`seedBuiltinLayouts`/`hasRecipe`/`switchLayout`/`applyActiveLayout`/`captureActiveLayout`; new optional window registered in `showFlagForKey` AND `kLayoutWindowKeys`; `pendingFocusWindow_`.
- Project lifetime: `attachProject()`/`closeProject()` pair; `openProjectAt`/`openRemoteProject`/`projectLoadTick`/`drawProjectLoadingScreen`; `requestCloseProject`/`requestOpenProject` + `PendingAction`; `!hasProject_` guard; `droneTickRender()`; `droneStop()`; `viewport_.invalidateAssets()`.
- Log panels `logview.cpp/.hpp`: `logSetText`/`logRefresh`/`drawLogPanel`/`LogView`; `Line::cont`; `appendPartial`/`parse`. Theme `theme.cpp/.hpp`: `Palette`/`ImGuiCol_`/`theme::semantics()`/`Semantics`/`applyImNodes()`/`hoverAnim()`/`baseStyle_`/`applyUiScale()`/`ImGuiStyle`/`uiFontSize_`; `platform::uiFontFiles`.
- ImGui traps: `App::scaled(px)`; `PushFont(nullptr, sizePx)` not `SetWindowFontScale`; `SetNextItemAllowOverlap()`; an ImGui label IS its ID (`##suffix`, `ImHashStr`, `###`); popup IDs (`OpenPopup`/`IsPopupOpen`/`BeginPopupModal` same string); tooltip after `IsItem*`; clipped widgets (`ItemInfo` skipped); `--ui-script` hover.
- UI scripting `uiscript.cpp/.hpp`: `IMGUI_ENABLE_TEST_ENGINE` + `ImGuiTestEngineHook_ItemAdd`/`ItemInfo`/`Log`/`ImGuiTestEngine_FindItemDebugLabel`; `PUBLIC` on the imgui target; `TestEngineHookItems`; `parseScript` -> `Step`s by `App::uiScriptTick` injecting `io.AddMousePosEvent`/`AddMouseButtonEvent`/`AddKeyEvent`; `rightclick`; double-quote tokenizer; optional `<dx>,<dy>` offset; `captureActiveLayout`/`activeLayout`.

## UI identifiers (upstream)
- `NavConfig`: widget in `drawNavigationModal`; `orbitAroundSelection` has a second door in the View menu; both reset `navFocusedIndex_`. `setViewProjection` is the reference for "view state is not an edit". Engine log prefixes `LOG:`/`==WARN:`/`====ERR:` are exact (vendor/tyra debug.hpp). `ImGui::NewFrame` timing; `ImGuiContext` layout; `ImGuiStorage` (by `GetItemID()`); the UI-script tick must stay BETWEEN the GLFW backend's NewFrame and `ImGui::NewFrame`.
