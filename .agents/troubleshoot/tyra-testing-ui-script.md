# troubleshoot/tyra-testing-ui-script.md

Driving the editor's own UI (`--ui-script`), the editor's self-capture, and the offscreen viewport harness. Symptom first.

## Driving the editor with `--ui-script` (docs/ui-scripting.md)

**Cause:** a panel, dialog or toggle needs an unattended assertion with no window focus.
**Fix:**

```powershell
build\tyrax-editor.exe --ui-script <projectDir> `
    'frames 20; click Tools; click "Remote Pad"; shot panel.png; quit'
TYRAX --ui-script [projectDir] --file <script.ui>    # or --stdin
```

Steps: `click|rightclick|hover|doubleclick|hold|drag|wheel|key|text|wait|frames|shot|dump|log|quit` plus `expect` / `expect-not` (`expectnot`) / `expect-checked` / `expect-unchecked`; the exit code is 0 only if every step passed. **Always start with `dump`** (`--ui-script <dir> "frames 20; dump"`): it prints every widget with its rect AND checked/open state. A step that names a target WAITS for it; a target is `"Window/Label"` or a bare `"Label"` with case-insensitive prefix matching, **quoted, with either `"` or `'`** (an unquoted two-word target arrives as two tokens and fails; `"Remote/Cross"` and a bare `"Label"` both resolve, and a window name containing `/` is split at every point). Quoting makes the target OPAQUE (a `#` or `;` inside quotes is ordinary), so a name straight out of `dump` (`Project/##objects_DC0BCE04`) can be pasted verbatim. `shot` writes the same self-captured framebuffer as `TYRAX_SHOT`. What it CANNOT name: the 3D viewport, the imnodes flow canvas and the ImGuizmo gizmo. The four pointing steps take an optional `<dx>,<dy>` offset from the target's centre.

## A `dump` rect does not mean the click will land

**Cause:** a window taller than the room it got still submits items past its bottom edge, so they are listed with rects OUTSIDE the window and `click` on one presses over nothing and still reports success.
**Fix:** compare the item's y against its window's rect (both in the same `dump`) and **pair every state-changing click with `expect-checked` / `expect-unchecked`**, which turns a silent no-op into a failed run.

## Combos and tabs dump as `-`

**Cause:** `BeginCombo` never calls ImGui's item-info hook, and `TabItemEx` calls it BEFORE the box exists.
**Fix:** `uiscript.cpp` closes both — a label reported ahead of its box is held until the box arrives, and a target that matches nothing by label is matched by RE-HASHING it (`ImHashStr(label, 0, window->ID)`). So `click 'Preview in'` and `click Style` work, and a combo's OPTION can be clicked by its own text once the dropdown is open. Two limits: the hash path needs the EXACT label and only reaches widgets submitted at a window's own scope.

## A tab's controls "have been removed"

**Cause:** a tab only has contents while it is the SELECTED one. `BeginTabItem` returns false for every tab but the front one and its body is never submitted, so `dump` does not list it and `expect`/`click` fail.
**Fix:** select the tab first — `click 'Project Preferences/Display'; frames 5`. **Qualify a tab with its window** (a bare `click Build` opens the menu bar's `Build` menu); the five tabs are `Display`, `World`, `Rendering`, `Player`, `Build`. A tab pushes an id (`PushOverrideID(tab->ID)`), which moves what the hash fallback can see — wrapping the tab body in a `BeginChild` restores it (Project Preferences does, verified: `click 'Project Preferences/Mode'` opens the BLSS mode combo four levels in). A tabbed dialog stops needing `wheel` and starts needing tab clicks; a tab BODY still scrolls.

## Project Preferences is a WINDOW, not a modal (since 1.20.0)

**Cause:** its footer button is `Close`; there is no `OK` and no `Cancel`, because every edit applies as it is made.
**Fix:** assert by reading the model back (`key ctrl+s` then grep the `.tyra`, or `--dump`) instead of pressing a confirm button. A bare label is ambiguous with two windows up (qualify `'Project Preferences/Mode'`); the upscaler window opens ON TOP, so assert covered items with `expect-checked`, which does not click. The terrain grid (`Width (units)`, `Depth (units)`, `Detail (max grid cells)`) writes back on release, so `text` needs a `key enter` after it (an uncommitted `text 2` leaves the stored width at 100).

## A widget whose whole label is hidden behind `##`

**Cause:** a compact search field (`##assetsearch`, `##objsearch`) dumps as `-` with a rect and nothing to name.
**Fix:** reach it by CLICKING ITS RECT: read the rect off `dump`, add the editor window's screen offset (compare one `dump` rect against one full-screen `shot` to get it — ~67,70 on this box) and drive it with `wayland-control.py click --at x,y` / `type`.

## `wheel` keeps scrolling for far longer than the step takes

**Cause:** ImGui trickles queued input ONE EVENT PER FRAME (`io.ConfigInputTrickleEventQueue`), so `wheel X -19` hands the window nineteen notches over the following nineteen frames at least; the view is still moving when the next `click` computes its target.
**Fix:** after any `wheel`, wait **`frames 60`-`90`**, then `dump` TWICE and require the rects to agree before clicking anything. `wheel <target> <notches>` is also how a canvas ZOOM is driven (verify with `dump` at two zoom levels, not a screenshot) and how you park the cursor for a node-hover tooltip (`wheel "Flow Graph" 1; wait 1.6; shot x.png`). The add menu (right-click on empty canvas) stays out of reach.

## A label with an apostrophe cannot be named

**Cause:** the tokenizer opens a quoted run on a single quote at a token boundary, so `'Will this project's frames get shorter?'` parses as two tokens and the step dies with *"click needs one target"*.
**Fix:** a button that has to be scripted must not have an apostrophe (that is why the Preferences verdict button reads *Will the frames get shorter?*). An apostrophe in GENERATED TEXT is the same hazard: an unpaired `'` after a `#error` makes GCC read an unterminated character constant, so every TU reading the file gets a *missing terminating ' character* warning (`errorSafe()` in templates.cpp guards it). And the same label in two different windows is ambiguous to `find` (which takes the first match) — qualify as `'Project Preferences/Mode'`; the menu bar is `'##MainMenuBar/Project'`.

## PowerShell 5.1 mangles the `--ui-script` argument

**Cause:** native-command argument passing re-parses the string and eats embedded `"`, so `TYRAX --ui-script $P 'click "Remote Pad"'` reaches the editor as `click Remote Pad` and fails.
**Fix:** put the script in DOUBLE quotes and its targets in SINGLE ones: `"click 'Remote Pad'"` survives intact.

## A node's param widgets are unreachable

**Cause:** a node's param widgets register only while its window is the FRONT tab (behind another tab the window is drawn with `SkipItems`). The Flow Graph ships docked behind Viewport in the Default layout.
**Fix:** set `"activeLayout"` in the project's `.tyra` to a layout whose recipe FOCUSES the window (`LayoutRecipe::Debugger` = index 3 focuses Flow Graph; `Procedural` focuses Procedural), then everything inside the nodes is nameable as `"Flow Graph/<Param>"`.

## The editor renders WHITE (title bar only, capture blank)

**Cause:** a GL present quirk, not a code regression.
**Fix:** capture `D:\tyra-editor\build\tyrax-editor.exe` (the main-repo baseline binary) the same way. If that is blank too, GUI visual verification is unavailable for this session — say so rather than claiming a visual check that did not happen.

## Capturing the editor's own framebuffer on a timer

**Cause:** no display permissions at all, or the AMD present quirk that leaves the window blank.
**Fix:** set `TYRAX_SHOT=<dir>` (optionally `TYRAX_SHOT_EVERY=<s>`, default 2) and it writes `<dir>/shotNN.png` every interval (`App::captureFrameIfRequested`). It reads what the editor DREW. It cannot click anything — before `--ui-script` existed, a panel behind a menu was pre-opened by adding its key to the active layout's `open` list in the project's `.tyra` (`kLayoutWindowKeys` in app.cpp: `"drone"`, `"tree"`, `"material"`, ...).

## An editor viewport check needs numbers, not a picture

**Cause:** you want measurements rather than an image.
**Fix:** an offscreen GL harness — a hidden GLFW window (`GLFW_VISIBLE` false, `GLFW_INCLUDE_NONE` before glfw3.h), `glInit()`, a real `Viewport`, real `render()` calls at a real panel size, then `glReadPixels` off `lastImageFbo_` into a PNG through the already-linked `stb_image_write` (do NOT define `STB_IMAGE_WRITE_IMPLEMENTATION` — menubake.cpp.o has it, and stbi_load comes from app.cpp.o, so link ALL of `build/`'s objects minus `main.cpp.o`, `-no-pie`). What it cannot cover (the surrounding UI, anything dragged by hand) — say so in the commit.
