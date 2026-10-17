# Driving the editor: `--ui-script` (docs/ui-scripting.md)

Read when: you verify editor UI (panels, dialogs, toggles, node canvases, viewport clicks) unattended, or a UI script "succeeds" but nothing happens.

```powershell
build\tyrax-editor.exe --ui-script <projectDir> "frames 30; wait 1; click 'TyraX menu'; click Tools; click 'Remote Pad'; shot panel.png; quit"
TYRAX --ui-script [projectDir] --file <script.ui>    # or --stdin
```
The editor runs for real and holds its own mouse/keyboard, naming WIDGETS. No focus on either OS. Exit 0 only if every step passed.

## Steps (src/uiscript.cpp)

| step | form |
|---|---|
| pointing | `click`, `shiftclick`, `rightclick` (`right-click`), `doubleclick`, `hover` `<target> [<dx>,<dy>]` |
| asserts | `expect`, `expect-not` (`expectnot`), `expect-checked`, `expect-unchecked` `<target>` (no offset, never touch the mouse) |
| hold | `hold <target> [seconds]` (default 0.5) |
| drag | `drag <target> <dx> <dy> [left\|right\|middle]` (default left) |
| wheel | `wheel <target> <notches>` (alias `scroll`) |
| files | `drop <target> <file> [dx,dy]` - an OS file drop on the target (`handleFileDrop` with the point; UI Editor PNGs, Asset Browser imports) |
| input | `key <chord>` (e.g. `ctrl+s`, `enter`, `escape`/`esc`), `text <string>` (alias `type`) |
| timing | `wait <s>` (`sleep`), `frames <n>` |
| output | `shot <file.png>`, `dump`, `log <text>`, `quit` (`exit`) |

- **Start with `dump`** (`--ui-script <dir> "frames 20; dump"`): every widget with rect AND checked/open state.
- A step naming a target WAITS for it (no sleeps for menus); a timeout prints what was on screen.
- **The ten menus live behind the logo button `TyraX menu`** (top-left; F10 / lone Alt tap too). A step naming a top-level menu — bare `File`/`Edit`/`View`/`Layout`/`Build`/`Session`/`Tools`/`Help`, or any of the ten as `'##MainMenuBar/<menu>'` — opens it first when closed (`uiscript::namesMainMenu` + `logoMenuOpenRequest_`), so old `click Tools; click 'Remote Pad'` scripts still run. The popup's items report under `##MainMenuBar` (`uiscript::aliasCurrentWindow`); a submenu's entries under `Tools###Menu_00` etc. Start with `frames 30; wait 1`: a focus change while the layout settles closes the popup under an early click.
- Target = `"Window/Label"` or `"Label"`, case-insensitive, prefix match (`"Remote/Cross"`, menu entries without `...`). **Quote with `"` or `'`**; unquoted two-word targets arrive as two tokens. Quoted targets are OPAQUE (`#`/`;` inside are ordinary; paste `Project/##objects_DC0BCE04` from `dump` verbatim); a window name containing `/` resolves (every split point tried).
- `shot` = same self-captured framebuffer as `TYRAX_SHOT`.
- Not nameable: the 3D viewport (one big item, named `Viewport canvas` since 1.74.1 — `click 'Viewport canvas'`, `drag` inside it), the imnodes flow canvas, the ImGuizmo gizmo. Reach things DRAWN over a widget with the offset: `click 'Viewport/Move (1)' 545,303` selected a comment by its icon (docs/comments.md) — take the icon's pixels from a `shot` (find + average the colour), and pair with an `expect` only the intended object produces (`Properties/Copy text` = a comment).
- Road point handles are targetable as `Road point N` (use `shiftclick` to delete). Vehicle Editor: `Vehicle preview canvas`.

## Traps

- **A `dump` rect is not a promise the click lands.** A window taller than its room submits items past its bottom edge with rects OUTSIDE the window; `click` presses over nothing and reports success. Compare the item's y with its window's rect, and **pair every state-changing click with `expect-checked`/`expect-unchecked`**.
- Not all modals close on `escape` — click their `Cancel` (see `dump`).
- **Combos and tabs are nameable** (`BeginCombo` never calls ImGui's item hook; `TabItemEx` calls it before the box exists). uiscript holds a label reported early until its box arrives, and matches unknown labels by re-hashing (`ImHashStr(label, 0, window->ID)`). So `click 'Preview in'`, `click Style` work; a combo OPTION is clickable by text once open. Hash path needs the EXACT label and only reaches widgets at a window's own scope. Procedural and Flow Graph node params live in the window's INSPECTOR after the node is selected (`Procedural/<Param>`, `Flow Graph/<Param>` while one node is selected); nodes themselves are reached by offset clicks.
- **A tab only has contents while selected — select it first.** Hidden tabs submit nothing: `dump` omits, `expect`/`click` fail as if removed. *Project Preferences* tabs: `Display`, `World`, `Rendering`, `Player`, `Build` → `click 'Project Preferences/Display'; frames 5` before the control.
  - Qualify tabs with the window: bare `click Build` opens the Build MENU (behind the logo).
  - `BeginTabItem` ends in `PushOverrideID(tab->ID)` → widgets directly in a tab are seeded by the tab, invisible to the hash fallback. A `BeginChild` in the tab body reseeds (Project Preferences does: `click 'Project Preferences/Mode'` opens the BLSS mode combo four levels in). A tab body without a child needs real labels.
  - Project Preferences is a sidebar (categories keep the old tab names: `click 'Project Preferences/Display'`), sections `<Section>##sec`, search `##prefs_search`, Close at the sidebar's bottom. Open it with `key ctrl+comma`: a bare `click Project` may hit the "Project Preferences" dock tab (prefix match) once it has been opened.
- **Project Preferences is a WINDOW since 1.20.0**: footer `Close`, no OK/Cancel, edits apply immediately → assert by reading the model back (`key ctrl+s` + grep `.tyra`, or `--dump`). `click 'Project Preferences/Advanced...'` leaves it AND *Neural Upscaler (BLSS)* open, both clickable. Two windows → qualify shared labels (`Use the upscaler`); the upscaler opens ON TOP, so assert covered Preferences items with `expect-checked` (doesn't click). Terrain grid (*Width (units)*, *Depth (units)*, *Detail (max grid cells)*) writes back on release: `text` needs `key enter` (uncommitted `text 2` left width 100).
- **Same label in two windows is ambiguous** (first match): `click Mode` hit the wrong window. A panel alone in its dock node has NO tab (its own header instead), so `click Project` usually means the logo menu's Project menu (the shim opens it) and `click Scene` the Scene WORKSPACE button; the menus are `'##MainMenuBar/Project'` / `'##MainMenuBar/Scene'`. A dock TAB is not in its window but in the dockspace's: `click 'WindowOverViewport_11111111/DockSpace_08BD597D/Properties'` (copy from `dump`).
- **Apostrophes can't be named**: `'Will this project's frames get shorter?'` → *"click needs one target"*. Scripted buttons must not contain one (hence *Will the frames get shorter?*).
- **PowerShell 5.1: script in DOUBLE quotes, targets in SINGLE** — native-arg passing eats embedded `"` (`'click "Remote Pad"'` arrives as `click Remote Pad`). `Start-Process -ArgumentList` trap applies too (screenshots-and-pad.md).
- **Input trickles one event per frame** (`io.ConfigInputTrickleEventQueue`). `wheel X -19` keeps scrolling ≥19 frames; a click computed during the scroll lands on whatever slid under it and reports SUCCESS. Measured: `frames 10` → view moved 300 px more, button never pressed; `frames 90` → two dumps agree, click lands. Rule: **`frames 60`–`90` after any `wheel`, then `dump` twice and require equal rects.** Same for `text` → `frames 5` before clicking the button that consumes it (`BeginDisabled` on an empty field makes an early Send a no-op that "succeeds").
- **Popup items live in their own window** (`##Popup_<hash>`): grep `dump` for the LABEL, confirm with `shot` before calling a popup broken.
- Names added by the 1.173 finishing round: `Asset preview` (Asset Browser inspector canvas: drag = orbit), `Vehicle Editor/Near` / `Far`, `Show probes in viewport` (GI tab, below the fold - wheel first), `Play clip` / `Pause clip` / `##cliptime` (Import model), `Bones` (Animation Editor), Material Editor `Source` / `Colour` / `Range` / `Invert`, Drone pills `Layer 1`..`Layer 4` (voice chips by full window path - `dump` shows them as 1..6), World Facts `##qfilter` / `##rfilter` / `##sfilter` / `##bbfilter`, `Viewport/##viewport_header_*/Move panel`, Ambience `Simple` / `Detailed` / look names / `Time of day` / fog `Off`..`Thick` / `Time passes`. Drone Generator: `Drone Generator/Simple` / `Detailed` (qualify - Ambience has the same pair), `##drn_mood_N`, `##drn_bright`..`##drn_chimes`, `##drn_shape`, `##drn_feeling`, `##drn_loud`, `30 s`/`1 min` chips, `Another take`, `Undo`, `Save to project`, `Open in Flow`, `+ Play in another scene...`. View > Static batches sits below the View menu's bottom at the default height - use Tools > Static Batches > *Colour batches in viewport*.
- **Controller drawings** (Remote Pad, Input Map, pad-button pickers): each element is an item named `L2 L1 R1 R2 Up Left Right Down Triangle Square Circle Cross Select Start L3 R3`, checked = pressed (Input Map: = the selected action's button) - `shiftclick 'Remote Pad/Cross'` (with *Shift-click holds a button* on) then `expect-checked 'Remote Pad/Cross'` asserts what the game is sent. A pad-button combo (`'Input Map/Pad button'`, `'Flow Graph/Button'` with the node selected, `'Properties/Toggle button'`) opens a popup with the same pad items plus list rows named `Cross`, `DpadUp`...; qualify them as `'##Combo/Triangle'` - a bare `Triangle` hits the Input Map's own diagram first. Flow Graph node ids live in `objects/<id>.json`; check the RIGHT object (two-player examples have several players).
- Close floating tool windows (or drop their open flag in the scratch layout) before scripted scene selection — an overlapping Tree Generator intercepts clicks.
- The automatic startup update check is skipped while a UI script runs (`uiScriptActive_`).
- Hermetic runs: set `XDG_CONFIG_HOME` (or `LOCALAPPDATA`) to a scratch dir — `editor.ini` and history persist toggles (e.g. `chatAllowEdits_`) into the next run; a scripted checkbox click is a toggle, so `expect-checked` right after.

- **`key ctrl+X` on macOS** is sent as `ImGuiMod_Super`: with `io.ConfigMacOSXBehaviors` ImGui swaps Ctrl and Cmd as events arrive, so a literal Ctrl never reached the editor's Ctrl shortcuts on a Mac (fixed in 1.173.0; before that `key ctrl+s` silently did nothing there).

## Wheel, zoom, node canvases

- `wheel <target> <notches>` drives canvas ZOOM; it's the only step that may resolve a bare WINDOW name (`wheel "Flow Graph" -6` = middle of canvas). Verify zoom with `dump` at two levels — node widgets are ordinary items: rects give the scale factor and offsets show self-similarity (PROGRESS 233: both under 0.1%).
- (PROGRESS 247, a node-tooltip screenshot.) **Node param widgets register only while the window is the FRONT tab** (`SkipItems` otherwise: node rects listed, unlabelled, nothing inside). Flow Graph docks behind Viewport in Default; removing windows from `open` doesn't help (Viewport isn't optional). Set `"activeLayout"` in `.tyra` to a layout that FOCUSES it (`LayoutRecipe::Debugger` = 3 → Flow Graph; `Procedural` = 4 → Procedural); then `"Flow Graph/<Param>"` works.
- `wheel` parks the cursor at the target centre and ImGui keeps it: `wheel "Flow Graph" 1; wait 1.6; shot x.png` captures the hover tooltip for whatever sits under the window middle (screen = canvas origin + model position x nodeScale, both in one `dump`). The add menu (right-click empty canvas) stays out of reach.

## Recipes elsewhere

AI Assistant chat: ai-and-packages.md. Update modal: ai-and-packages.md. Model import with fake `zenity`: acceptance-physics-procedural-import.md. Log panels: host-harnesses.md. Comment icons: above.
