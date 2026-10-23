# Host harnesses — testing editor modules from a scratch `main()`

Read when: you want to test editor logic without the GUI (pure modules, `App` private methods, collaboration sessions, log panels, Live Logic, menu styles, update/installer logic) and need the link line.

Keep harnesses in the scratchpad, never the repo. Say in the commit what a harness cannot cover (the panel around the call, anything dragged by hand).

## Link recipes

- **Pure module, one or two files** (seconds):
  - menus: `g++ -std=gnu++20 -I src -I vendor/stb -o h harness.cpp src/menustyle.cpp src/menulayout.cpp`
  - strips: `g++ -std=gnu++20 -O1 -I src -o stripcheck harness.cpp src/meshstrip.cpp`
  - log panels: `g++ -std=c++20 -Isrc harness.cpp src/logview.cpp`
  - live debugger decoder: `g++ -std=c++20 -static probe.cpp <repo>/src/livedbg.cpp -I<repo>/src` (`-static` on Windows or the exe dies `0xC0000139` looking for MinGW DLLs)
  - physics hull: `src/physhull.cpp` + `src/primmesh.cpp` + `src/objparser.cpp`
  - particle textures: `particletex.cpp` with `-I src -I vendor/stb`, a TU defining `STB_IMAGE_WRITE_IMPLEMENTATION`, a one-line stub of `project::particleLayerStem` (`bakeEffect` calls it); on Windows `-static` (from Git Bash `/mingw64` DLLs shadow scoop's → exit 127 before `main`)
  - update: `g++ -std=c++20 -I src -I vendor/glfw/include harness.cpp src/update.cpp src/json.cpp src/platform.cpp build/vendor/glfw/src/libglfw3.a -o uh.exe -lshell32 -lole32 -luuid -lws2_32 -lcomdlg32 -lgdi32` (Linux self-update: `g++ -std=c++20 -I src harness.cpp src/update.cpp src/json.cpp src/platform.cpp -o uh`)
- **Anything reaching `project.cpp`** — reuse the objects a build produced (everything except `main.cpp.o`; GUI code comes along unused, costs a link not a display):

```bash
OBJS=$(ls build-dev/CMakeFiles/tyrax-editor.dir/src/*.o | grep -v /main.cpp.o)
g++ -std=c++20 -O0 -I src -I build-dev/generated -I vendor/imgui -I vendor/imguizmo/src \
    -I vendor/imnodes -I vendor/stb -I vendor/ufbx -I vendor/miniaudio \
    harness.cpp $OBJS build-dev/CMakeFiles/tyrax-editor.dir/vendor/*/*.o \
    build-dev/CMakeFiles/tyrax-editor.dir/vendor/ufbx/ufbx.c.o \
    build-dev/libimgui.a build-dev/vendor/glfw/src/libglfw3.a \
    -ldl -lrt -lm -lGLX -lOpenGL -lpthread -o harness
```
  Windows: `src/*.obj` minus `main.cpp.obj`, plus `-static -lopengl32 -lgdi32 -lcomdlg32 -lshell32 -lole32 -luuid -lws2_32 -limm32` (missing `comdlg32` shows only as `GetOpenFileNameW` from `platform.cpp`). Add `-no-pie` when linking all of `build/`'s objects.

## `App` private methods

Link every `build/CMakeFiles/tyrax-editor.dir/**/*.o` except `main.cpp.o` (+ `libimgui.a`, glfw, `-ldl -lrt -lm -lGLX -lOpenGL -lpthread`, `-no-pie`); `#define private public` before `#include "app.hpp"`.
- Include **every header app.hpp includes first, normally**, so the macro reaches only app.hpp's body (else libstdc++ `<sstream>`: *redeclared with different access*).
- Neutralise GL. Material Editor: `matEdPaintW_ = 0` makes `matEdRegenLayer`/`matEdComposite`/`matEdSavePaintTarget` early-return, so `matEdSavePreset` → file → `matEdApplyPreset` runs with no GL context (PROGRESS 223).

## Offscreen GL viewport harness (PROGRESS 208)

Numbers instead of a picture for editor viewport work: hidden GLFW window (`GLFW_VISIBLE` false, `GLFW_INCLUDE_NONE` before glfw3.h), `glInit()`, a real `Viewport`, real `render()` at a real panel size, `glReadPixels` off `lastImageFbo_` → PNG via the already-linked `stb_image_write`. Do NOT define `STB_IMAGE_WRITE_IMPLEMENTATION` (menubake.cpp.o has it; stbi_load is in app.cpp.o) — link ALL of `build/`'s objects minus `main.cpp.o`, `-no-pie`. Measure bounding boxes, bar widths, pixel ratios.

## Menu stylesheets (docs/menu-styles.md)

- `menustyle.cpp` + `menulayout.cpp` harness (no GL/ImGui/project/font): assert the cascade (element < class < state, menu scope wins), `write(parse(t))` STABLE (Style tab saves through it), a broken declaration doesn't eat its neighbour, layout numbers. Caught `content: "{{cross}} OK"` truncated at first `}` (swallowing the next declaration) and `row:disabled` baked for every row (71% of texture heap vs 13% for a 6-row menu).
- **Classic-look pixel identity**: a menu with no stylesheet must bake byte-identical to the previous baker.
```bash
git worktree add /tmp/base HEAD && (cd /tmp/base && ./build.sh --dev)
/tmp/base/build-dev/tyrax-editor --refresh-gen A/showcase
./build-dev/tyrax-editor        --refresh-gen B/showcase
# compare A/*/res/menus/*.png with B/*/res/menus/*.png (PIL, 3 lines)
```
  Found a double-composited background ((7,10,21) vs (8,12,24)) and a lost "X SAVE O LOAD" hint line.
- **Checking that something SCROLLS: never use a pattern aligned with the scroll.** Diagonal stripes + diagonal scroll correlated to shift 0 across three rebuilds; vertical bars + horizontal scroll showed 53 px/s = declared speed.

## Collaboration sessions

`session.cpp` + `wire.cpp` have no GUI deps: run host and client `Session`s in one process over 127.0.0.1 with real sockets (drain `drainEvents()` in a sleep loop) for join/transfer/cache/kick/refresh; drive `session::diffModel`/`applyEdit` on two `Project` replicas for convergence (random concurrent edits + host relay rule → byte-identical serializations; caught two divergence bugs). Link the model-harness objects + `session`, `wire`, `platform`; `-lws2_32` on Windows, `-pthread` elsewhere. Interactive: two editor instances on one machine (second without a project), host from A, join B at `127.0.0.1` (loopback not blocked by Windows Firewall).

## Log panels' severity split (docs/log-panels.md)

`logview.cpp` is a pure function of text. Feed a realistic build log; assert counts are per ENTRY (a gcc error + snippet = one error) and that line-at-a-time parsing via `parse(log, from, state, out)` + `appendPartial` is byte-identical to one-shot (panels classify incrementally while streaming). Panel itself: `--ui-script` (`click 'Output/3 errors'`; the mouse selection is a drag and has no named widget, so check it by hand). The **Debug** window is a pill tab next to Output (and the Debugger) in the Default layout: `click Debug` selects it.

## Live Logic patches

`livelogic.cpp` has no GUI deps: ~100-line harness (link editor `src/*.obj` minus `app`/`viewport`/`gl_loader`/`main`, plus `vendor/ufbx/ufbx.c.obj` and your own `STB_IMAGE_IMPLEMENTATION` TU) can `project::load`, edit graph params, `livelogic::compile` + `encode`, write `bin/livelogic.bin` (= `App::liveLogicTick`). With Live Debugger telemetry, measure a node's fire RATE before/after (entry 192). Half-working patch → suspect wire STRIDE first; check `build/tyrax-editor.exe` was rebuilt before the `--build` that generated the game's parser (stale editor → stale interpreter).

## Audio (editor side)

Drone Generator audition (`src/audiopreview.cpp`): a harness can open the device and print peaks (PROGRESS 210). DSP needs no device: link `dronegen.cpp` alone, render a preset, measure samples incl. automation (PROGRESS 211).

## Malformed AI replies

See ai-and-packages.md (copy `aigen::repairJson` + `extractJsonObject` next to `src/json.cpp`).
