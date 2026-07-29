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
with spaces is quoted. A target is `"Window/Label"` or a bare `"Label"`;
matching is case-insensitive, ignores a trailing `...` on menu entries, and takes
the window as a prefix — so `click "Remote/Cross"` is fine.

| Command | What it does |
|---|---|
| `click <target>` | hover, press, release — over three frames, like a real cursor |
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
- A widget must be **visible** to be found: something scrolled out of view, or in
  a collapsed section, has to be scrolled/opened first (the containing tree node
  is a normal item, so `click` it).
- **A combo's dropdown cannot be opened by name.** ImGui reports a label only for
  the widgets that call its item-info hook, and `BeginCombo` is not one of them —
  `dump` shows the combo's rect with no label. Pick the value another way (the
  Project panel's object list, a `Selected object`-style button, or editing the
  `.tyra` in a scratch copy) and read the result off a `shot`.
- Labels are what the code passes to ImGui, minus anything after `##`. Two
  widgets with the same label in the same window are ambiguous — the first one
  wins (which is also an argument for the `##scope` suffixes the codebase already
  uses).
- Modals do not all close on `escape`. Click their `Cancel` — `dump` will tell
  you it is there.

## Cost

The item registry is off unless a script asks for it: ImGui only calls the hooks
when its own `TestEngineHookItems` flag is set, so an ordinary session pays one
never-taken branch per widget. `IMGUI_ENABLE_TEST_ENGINE` is defined for the
imgui target **and** every consumer (`PUBLIC` in CMake) because it adds fields to
`ImGuiContext` — the library and the editor must agree about that struct.
