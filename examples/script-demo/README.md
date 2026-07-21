# script-demo example

The general playground — the smallest complete FPP project, and the one the
main README points you at first. It exists so you can try scripting, gizmos,
terraforming and Build & Run without wrecking a shared sample: open it, poke at
it, undo, repeat.

Open `script-demo.tyra` in the editor (`File > Open Project`) and Build & Run
(`F5`), or build headless:

```powershell
build\tyrax-editor.exe --build examples\script-demo --run
```

The `.history` undo file, `.vscode/` IntelliSense config, baked `.res-baked/`,
`obj/` and `bin/` are local state — the editor recreates them on open/build and
they are gitignored. The source of truth is `script-demo.tyra` plus the user
script under `src/scripts/`.

## What to do

You spawn on flat terrain in first person. Walk with the **left stick**, look
with the **right stick**. At startup the script logs `Hello from TyraX!` to the
PCSX2 console (visible in the editor's *Output* window, or `bin/log.txt`).
The script also ships a commented-out example — walk up to the **orange box**
and press **X** to snap the sky to a warm orange — that you can uncomment to see
scripting drive the scene.

## How it is wired

Two small pieces, one of each scripting flavor:

- **A flow graph** on the spawn point: **On Start ─▶ Set Sky Color** paints the
  starting sky when the scene loads (author it in the *Flow Graph* tab).
- **A global script**, [`src/scripts/example_interaction.cpp`](src/scripts/example_interaction.cpp)
  — a class deriving from `Script`, registered with `TYRA_SCRIPT(...)`. Its
  `init()` logs a hello line once, and `update()` carries a commented-out
  example that finds the box, checks the player is in range and that **Cross**
  was clicked, then toggles `ctx.skyColor`. It is left off by default so a jump
  (X) does not recolor the sky; the file is never regenerated, so it is yours
  to edit and uncomment.

This is the pre-object-script flavor kept deliberately simple. For the modern
component style (a class you *attach* to any object, with `self` / `onStart` /
`onUpdate` / `onUsed`), see the full guide in
[docs/object-scripts.md](../../docs/object-scripts.md); the *FPP* template that
new projects use ships an object-script version of this same interaction.

## Try next

- Open the script in VS Code (*Project panel > Scripts > Open in VS Code*) for
  working IntelliSense against the engine headers, tweak the range or the sky
  color, and Build & Run again.
- Drop in more primitives (*Scene* menu), move them with the gizmos (`W`/`E`/`R`),
  and sculpt the terrain (*Sculpt (T)* in the viewport).
- Attach an object script (*Properties > Scripts > New script...*) to see the
  Unity-style flavor side by side with the global script here.
