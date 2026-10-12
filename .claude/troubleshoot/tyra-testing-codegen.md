# troubleshoot/tyra-testing-codegen.md

Layer 2 — inspecting generated code without Docker. Symptom first.

## I need to check codegen without a game build

**Cause:** most features live or die in the generated code, and you can inspect it without building.
**Fix:** `--new` writes every generated file — grep them for your new constants/logic. For an **existing** project use `--refresh-gen <projectDir>` directly; it refreshes `inc/scene_data.hpp`, `src/gen/flow_graph.gen.cpp`, etc. without provisioning the native toolchain or contacting Docker. Remember the ownership split: `.gen.*` files and `scene_data.hpp` are always rewritten (trust them after a refresh); `terrain_game.cpp` / `controls.hpp` / `script.hpp` regenerate only while their marker line is intact, so a stale-looking file there may be user-owned by design, not a codegen bug.

## Testing a specific graph/scene shape

**Cause:** the `.tyra` is the source of truth.
**Fix:** edit the project's `<name>.tyra` directly (the editor tolerates external edits and discards the stale undo history in `<name>.history`), then refresh and inspect. `examples/script-demo/` is a checked-in generated project — a useful diff baseline, but only as fresh as its last regeneration; regenerate the sample first if codegen changed.

## Codegen harnesses

**Cause:** `project.cpp`, `templates.cpp` and `json.cpp` have no ImGui/GLFW dependency.
**Fix:** a scratch `main()` that builds a `Project` in code, calls `templates::generate()` and asserts on the emitted strings links into a tiny host harness without the GUI. Keep such harnesses in the scratchpad, not the repo.

## Menu stylesheet cascade / writer bugs

**Cause:** the Style tab saves through `write(parse(t))`, so an unstable writer rewrites people's files behind their backs.
**Fix:** a harness over `menustyle.cpp` + `menulayout.cpp` alone (neither needs GL, ImGui, `project.cpp` or a font) links in seconds:

```bash
g++ -std=gnu++20 -I src -I vendor/stb -o h harness.cpp \
    src/menustyle.cpp src/menulayout.cpp && ./h
```

Assert the cascade (element < class < state, menu scope wins), that `write(parse(t))` is STABLE, that a broken declaration does not eat the good one next to it (a `content: "{{cross}} OK"` was truncated at the first `}` and swallowed the next declaration), that `row:disabled` is NOT baked for every row (71% of the texture heap vs 13% for a 6-row menu), and the layout numbers.

## The Classic look must bake byte-identically

**Cause:** a menu with no stylesheet must bake byte-identically to the pre-stylesheet baker.
**Fix:** build the previous editor in a worktree, `--refresh-gen` the same example copies with both, and diff `res/menus/*.png`:

```bash
git worktree add /tmp/base HEAD && (cd /tmp/base && ./build.sh --dev)
/tmp/base/build-dev/tyrax-editor --refresh-gen A/showcase
./build-dev/tyrax-editor        --refresh-gen B/showcase
# compare A/*/res/menus/*.png with B/*/res/menus/*.png (PIL, 3 lines)
```

## "Something scrolls" measured as frozen

**Cause:** the test pattern was aligned with the scroll vector (diagonal stripes with a diagonal scroll gave a shift of exactly 0). Separately, PCSX2 unfocused does not take injected keys.
**Fix:** use vertical bars plus a pure horizontal scroll (that showed 53 px in one second, matching the declared speed). Click into PCSX2's window before driving it, and read its status bar (FPS/VPS/Speed) to know the machine is actually running before concluding anything about motion.

## Testing an `App` private method

**Cause:** a copy of the logic is not the shipped function.
**Fix:** link every `build/CMakeFiles/tyrax-editor.dir/**/*.o` except `main.cpp.o` (plus `libimgui.a`, glfw and `-ldl -lrt -lm -lGLX -lOpenGL -lpthread`, `-no-pie`), and reach the members with `#define private public` before `#include "app.hpp"`. Include **every header app.hpp includes first, normally**, so the macro only ever reaches app.hpp's own body (libstdc++'s `<sstream>` otherwise fails with *redeclared with different access*), and neutralise whatever pulls in GL — for the Material Editor, `matEdPaintW_ = 0` makes `matEdRegenLayer`/`matEdComposite`/`matEdSavePaintTarget` early-return, so `matEdSavePreset` -> file -> `matEdApplyPreset` runs with no GL context. What it cannot cover is the panel around the call — say so rather than implying a click-through happened.

## Live Logic patches, and tier layout

**Cause:** `livelogic.cpp` has no GUI dependency, and generated asset tiers are byte-parseable.
**Fix:** a ~100-line harness (link editor `src/*.obj` minus `app`/`viewport`/`gl_loader`/`main`, plus `vendor/ufbx/ufbx.c.obj` and your own `STB_IMAGE_IMPLEMENTATION` TU) can `project::load`, edit graph params, run `livelogic::compile` + `encode`, write `bin/livelogic.bin` (what `App::liveLogicTick` ticks), and with Live Debugger telemetry measure a node's fire RATE before/after. A half-working patch usually means a wrong wire STRIDE, or that `build/tyrax-editor.exe` was not rebuilt before the `--build` that generated the game's parser (stale editor -> stale interpreter). Tier vertex counts come from the bytes via the `src/tmdl.hpp` / `glbparser.cpp` layout.

## Collaboration session convergence

**Cause:** `session.cpp` + `wire.cpp` have no GUI dependency.
**Fix:** a harness can run a host `Session` and a client `Session` in one process over 127.0.0.1 with real sockets (drain `drainEvents()` in a sleep loop) to test join/transfer/cache/kick/refresh, and drive `session::diffModel`/`applyEdit` on two `Project` replicas (random concurrent edits + the host relay rule → assert byte-identical serializations). Link the same .obj set as the model harness plus `session`, `wire`, `platform`, and `-lws2_32` on Windows (`-pthread` elsewhere). For the interactive layer, run two editor instances on one machine (the second without a project), host from A, join from B at `127.0.0.1`.
