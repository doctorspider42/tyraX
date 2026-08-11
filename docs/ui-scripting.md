# UI scripting — driving the editor without a human

```
tyrax-editor --ui-script [projectDir] "click Tools; click 'Remote Pad'; shot pad.png"
tyrax-editor --ui-script [projectDir] --file check.ui
```

This runs the **real editor** and holds its mouse and keyboard itself. Steps name
widgets — `click "Remote Pad/Cross"`, not `click 837,412` — and the exit code is
0 only if every step succeeded, so a scripted GUI run gates a shell script like
any other test.

It is the [Remote Pad](remote-pad.md)'s lesson applied one level up: instead of
fighting the window manager for focus and synthesising OS clicks at guessed
coordinates, go through what the application already owns.

## Why by name, and why it needs no focus

Two things ImGui already does, that nobody was using:

- **It announces every widget it submits** — id, bounding box, label, and status
  flags like *checked* / *open* — through four `extern` hook functions that exist
  so an external test engine can implement them. `src/uiscript.cpp` implements
  them. So the editor knows what is on screen and where, by name.
  (`imgui_test_engine` itself is **not** vendored: we need none of it, and its
  licence is not ours to take on.)
- **Its input is a queue, not a poll.** `io.AddMousePosEvent` /
  `AddMouseButtonEvent` / `AddKeyEvent` are how the GLFW backend feeds real
  input, and a script can push the same events. Nothing goes near the OS.

What follows from that is the whole value:

| | synthetic OS input | this |
|---|---|---|
| Window focus | required (and on Windows a background process cannot reliably take it) | **not needed** — the editor can be behind everything |
| Coordinates | pixels, per DPI, per UI scale, per layout | widget names |
| A moved panel | breaks the script | keeps working |
| Widget state | read off a screenshot by eye | asserted (`expect-checked`) |
| Timing | `sleep` and hope | `click` waits for its target |

## The script

Commands are separated by newlines or `;`, `#` starts a comment, and a target
with spaces is quoted — `"like this"` or `'like this'`, whichever your shell
leaves alone. A target is `"Window/Label"` or a bare `"Label"`; matching is
case-insensitive, ignores a trailing `...` on menu entries, and takes the window
as a prefix — so `click "Remote/Cross"` is fine.

**Quoting is what makes a target opaque**, and that matters more than it sounds:
inside quotes a `#` is an ordinary character rather than a comment, and so is a
`;`. ImGui ids are full of `#` (a child region is registered as
`Project/##objects_DC0BCE04`), which is exactly how `dump` and a failure message
spell them — so a name copied out of `dump` can be pasted straight into a
script. A **window** name may itself contain `/` for the same reason, so every
split point of `Window/Label` is tried, longest window prefix first.

| Command | What it does |
|---|---|
| `click <target>` | hover, press, release — over three frames, like a real cursor |
| `rightclick <target>` | the context-menu button, same three frames - how a right-click menu is reached at all |
| `doubleclick <target>` | two clicks inside ImGui's double-click time |
| `hold <target> [seconds]` | press and keep it down (default 0.5 s) |
| `hover <target>` | move the cursor onto it and leave it there |
| `drag <target> <dx> <dy>` | press on it and slide by that many pixels |
| `wheel <target> <notches>` | scroll over it, one notch per frame (negative = down/out) |
| `key <chord>` | `ctrl+n`, `f9`, `escape`, `shift+tab`, … |
| `text <string>` | type into whatever has keyboard focus |
| `wait <seconds>` / `frames <n>` | let the editor run |
| `shot <file.png>` | write the editor's own framebuffer (works with no display permissions, and survives the AMD present quirk) |
| `expect <target>` / `expect-not <target>` | assert it is / is not on screen |
| `expect-checked` / `expect-unchecked` | assert a checkbox's or menu item's tick |
| `dump` | print every widget on screen, with its rect and state |
| `log <text>` | a line in the output |
| `quit` | close the editor (added implicitly at the end) |

**Every step that names a target waits for it** (up to 5 s) instead of sleeping.
That is what makes menus scriptable: a popup only exists a frame after the click
that opened it, and `click "Remote Pad"` simply waits for it to appear. A step
that times out fails the run and prints **what was on screen instead** — the
expensive part of UI automation is otherwise a blank "not found".

## Start with `dump`

Nothing else in this doc matters as much:

```bash
tyrax-editor --ui-script ~/TyraProjects/mygame "frames 20; dump"
```

```
135 item(s) on screen
  [##MainMenuBar] File  @ 12,3 132x51  [closed]
  [##MainMenuBar] Tools  @ 1278,3 153x51  [closed]
  [New Project] Project name  @ 1383,511 624x57  [input]
  [New Project] Claude Code  @ 1383,1201 300x57  [unchecked]
  [New Project] Create  @ 1383,1438 360x57
  [Remote Pad] Drive with the editor's keyboard  @ 84,942 741x57  [checked]
```

That is the labels a script can use, the state it can assert, and the answer to
"what is this dialog's button actually called". It is also the fastest way to
find out why a step failed: dump at that point and read.

## A worked example

The check that proved the Remote Pad's on-screen buttons really reach a running
game — the editor drives its own panel, and the pad state file is read from
outside:

```
frames 20
click Tools
click "Remote Pad"
expect "Remote Pad/Cross"
shot pad.png
hold "Remote Pad/Cross" 4.0
quit
```

While that runs, `bin/livepad.bin` shows the Cross bit set for 3.9 s of a 4.0 s
hold, and clears when the editor exits. Neither window was ever focused.

## Limits

- **The 3D viewport is not made of ImGui widgets.** Its image is one big item, so
  a script can `drag` inside it (orbiting, a sculpt stroke) but cannot name an
  object. Picking an object is a `drag`/`click` at an offset, or better, do it
  through the Project panel's list, which *is* widgets.
- Same for the flow-graph canvas (imnodes) and the gizmo (ImGuizmo): they draw
  themselves rather than submitting named items. The widgets *inside* a node
  (its combos, drags, checkboxes) are ordinary items, so they show up in `dump`
  and are what a script measures a canvas change by.
- `wheel` is the one step allowed to resolve a **bare window name**, because a
  canvas exposes no item to aim at: `wheel "Flow Graph" -6` scrolls six notches
  out over the middle of the window, which is how the flow-graph zoom is tested.
  Every step that ends in a click still excludes whole-window items, or a bare
  window name would "click" whatever sits in its centre.
- **A `wheel` keeps scrolling long after the step returns, and the next `click`
  lands on whatever has slid under it.** ImGui trickles queued input **one event
  per frame** (`io.ConfigInputTrickleEventQueue`), so `wheel X -19` hands the
  window nineteen notches over the following nineteen frames *at least*. The
  click that follows still reports SUCCESS - the item existed when it was looked
  up - so this reads as "the button does nothing" and costs an hour. Measured
  driving a long dialog: at `frames 10` the view moved another 300 px between
  the `dump` and the click and the button was never pressed; at `frames 90` two
  consecutive dumps agree and the click lands. So after any `wheel`: **`frames
  60`-`90`, then `dump` TWICE and require the rects to agree** before clicking.
  The same trickle is why `text` needs a `frames 5` before the button that
  consumes what was typed.
- A widget must be **visible** to be found: something scrolled out of view, or in
  a collapsed section, has to be scrolled/opened first (the containing tree node
  is a normal item, so `click` it).
- **A widget inside an unselected tab does not exist. Select the tab first.**
  `BeginTabItem` returns false for every tab but the front one and its body is
  never submitted, so nothing in it reaches the registry: `dump` does not list
  it, `expect` fails, `click` fails, and the failure reads as "that control is
  gone" rather than "that tab is not open". The tab strip itself *is*
  nameable — `uiscript.cpp` holds a tab's label until its box arrives, which is
  what made tabs targetable at all — so the fix is one step:

  ```
  click "Project Preferences/Display"
  frames 5
  click "Project Preferences/Use the upscaler"
  ```

  Two things that go with it. **Qualify the tab with its window**: a tab label
  is a short common word, and *Project Preferences* has a `Build` tab while the
  menu bar has a `Build` menu — `find` takes the first match, so a bare
  `click Build` opens the menu. And **a tab changes what the id-hash fallback
  can reach** (the bullet below): `BeginTabItem` pushes the tab's own id, so a
  widget submitted *directly* inside a tab is seeded by that id and not by the
  window's, which is what the fallback hashes against. Wrapping each tab body in
  a `BeginChild` puts it back — a child is a window and reseeds the stack with
  its own id — which is what *Project Preferences* does, so `click "Project
  Preferences/Mode"` still opens a combo four levels in. A tab body submitted
  without one needs real labels on anything a script has to name.

  The dialogs with tabs today are *Project Preferences* (Display / World /
  Rendering / Player / Build), the Neural Upscaler window, the Menu Editor, the
  Material Editor and the World Facts window.

  **Project Preferences is a WINDOW, not a modal, since 1.20.0**, which changes
  three things for a script. Its footer button is `Close` and there is no `OK`
  or `Cancel` — the window applies every edit as it is made, so a run asserts by
  reading the model back (`--dump`, or `key ctrl+s` then grep the `.tyra`)
  rather than by pressing a confirm button. Nothing is blocked behind it, so a
  script may drive another window in the same run — `click 'Project
  Preferences/Advanced...'` now leaves both it and *Neural Upscaler (BLSS)*
  open, and both take clicks. And because two windows are open, **a bare label
  is ambiguous**: `Use the upscaler` exists in both, so qualify it
  (`'Project Preferences/Use the upscaler'`). Mind the ordinary window hazard
  that replaces the modal one: the upscaler window opens ON TOP of Preferences
  and a `click` on a covered item lands on the window in front — assert covered
  items with `expect-checked` (which does not click) and click only what is
  clear, or raise the window you want first.

  The one control in it that does **not** apply as you type is the terrain grid
  — *Width (units)*, *Depth (units)*, *Detail (max grid cells)* — because
  changing the heightmap's dimensions resamples it. Those write back on
  release, so a script must `key enter` after `text` (or `drag` the slider and
  let go) before the value reaches the project; measured, a `text 2` left
  uncommitted keeps the stored width at 100.
- **`dump` listing a rect is not a promise the click will land.** A window taller
  than the room it was given still *submits* the items past its bottom edge, so
  `dump` prints them with rects outside the window - and `click` on one hovers,
  presses and releases over nothing, then reports **success**. It looks exactly
  like a feature that does not work (that is how a save-marking change read as
  broken in one panel out of five, while the code was fine). Compare a widget's
  y against its window's rect - both are in the same `dump` - and **pair any
  state-changing click with an assertion**: `expect-checked` / `expect-unchecked`
  around it turns a silent no-op into a failed run.
- **A combo dumps with no label, but it can still be clicked.** ImGui reports a
  label only for the widgets that call its item-info hook, and `BeginCombo` is
  not one of them, so `dump` shows the combo's rect with a `-` where its name
  should be. `find` covers that by **re-hashing** the target the way ImGui would
  (`ImHashStr(label, 0, window->ID)`), so `click "Project Preferences/Mode"`
  opens the dropdown and the option is then clickable by its own text. Two
  limits: the hash has no prefixes, so the label must be **exact**, and it only
  reaches widgets submitted at a **window's own scope** — anything under a
  `PushID` (a tab item, an explicit `PushID(label)`) is seeded by that instead.
  A widget whose entire label is hidden behind `##` has nothing to hash and
  stays unreachable; pick the value another way and read the result off a `shot`.
- Labels are what the code passes to ImGui, minus anything after `##`. Two
  widgets with the same label in the same window are ambiguous — the first one
  wins (which is also an argument for the `##scope` suffixes the codebase already
  uses).
- **`hover` does not HOLD the cursor.** The GLFW backend rewrites `io.MousePos`
  from the real mouse every frame, so a synthetic hover lasts about as long as
  the step that injected it — which means a tooltip on a *delay*
  (`ImGuiHoveredFlags_ForTooltip`) usually will not be there by the time a later
  `shot` runs, and its absence proves nothing. A tooltip shown while the cursor
  is held — during a `click` step, or one with no delay — screenshots fine.
- Modals do not all close on `escape`. Click their `Cancel` — `dump` will tell
  you it is there.

## Cost

The item registry is off unless a script asks for it: ImGui only calls the hooks
when its own `TestEngineHookItems` flag is set, so an ordinary session pays one
never-taken branch per widget. `IMGUI_ENABLE_TEST_ENGINE` is defined for the
imgui target **and** every consumer (`PUBLIC` in CMake) because it adds fields to
`ImGuiContext` — the library and the editor must agree about that struct.
