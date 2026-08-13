# TyraX

*Pronounced **TIE-raks** — /ˈtaɪræks/ (like "tie" + "racks").*

**A 3D editor that makes real PlayStation 2 games**, engine included — based on
[Tyra](https://github.com/h4570/tyra).

Sculpt terrain, drop in models and lights, wire the gameplay in a visual flow
graph — then press one key and a PS2 runs your world. PCSX2, or a real console
over ethernet.

Under the hood TyraX writes the game as ordinary C++ against the engine and
compiles it in Docker with the PS2 toolchain. Both halves live in this repo —
the editor and the engine (`vendor/tyra/engine`) — and the generated sources
are yours to take over, file by file, whenever you want them.

While the game runs, the editor stays attached: drag an object and it moves on
the console as you drag, hot-patch a flow graph with no rebuild, put a
breakpoint on a node and step through it, or rewind the console to where it was
a few seconds ago. A release build carries none of that — and proves it with an
audit.

And the ceiling is nowhere near 2002: baked global illumination with light
probes, portals you walk through, reflections **ray-traced per pixel on VU0**,
a neural upscaler measured **1.63× faster on a real console**, split-screen
co-op, NPCs that find their way around walls.

The editor is C++20 + Dear ImGui (docking) + GLFW + OpenGL 3.3, one source tree
for **Windows and Linux**.

![The TyraX editor in its default Face buttons theme: the Project panel (scenes, objects, layers, assets) on the left, the 3D viewport with a checkerboard terrain, scene objects, a transform gizmo and camera-entity frustum wedges in the center, the Properties panel for the selected box on the right, and the build Output docked below.](docs/img/editor-overview.png)

> This file is the map. Every feature has a guide in **[docs/](docs/README.md)** —
> that index is the manual.

## Why would anyone do this?

I've always loved the PlayStation 2. My impossible dream was to make a game for
it one day. Even with such amazing tools like PS2SDK and Tyra, it was still a
little too much. Then one day came the AI. While tinkering with it, I thought:
"What if it could help me create a bona fide PS2 engine with a real editor? And
I mean real, like Unity or CryEngine?". It started as a joke... but here we are.

### Whaaaat, it is all AI slop?

Well, if you really have to call it that... but know it will break my heart.
I've spent countless hours working on this project. Debugging, testing,
designing, thinking, and asking the right questions. It would never be possible
for me to create it without the AI. Please give it a chance :)

## Quickstart

**Windows**

```powershell
scoop install mingw cmake ninja      # editor toolchain (+ optional: scoop install ccache)
./build.ps1 -Run                     # everything else, including the first-run setup
```

**Linux**

```bash
./setup.sh --deps                    # system packages: toolchain, X11/Wayland/GL headers, zenity, ccache
./build.sh --run                     # everything else, including the first-run setup
```

**There is no setup step to forget.** On a fresh clone (or a stale worktree)
the build script notices that `vendor/` is missing something and runs
`setup.ps1` / `setup.sh` itself — fetching dependencies at pinned commits,
checking the toolchain, configuring CMake. The one thing it cannot do is
install *system* packages as root — that is what the first line above is for —
and when something is missing, it stops and names the exact command to run.

Flags: `-Run`/`--run`, `-Clean`/`--clean` (from scratch), `-Dev`/`--dev` (a
fast-to-rebuild `-O1` build in its own `build-dev/` — too slow for host bakes
or benchmarks). ccache/sccache is picked up automatically if it is on `PATH`.
From `cmd.exe`, `build.cmd` / `setup.cmd` wrap the PowerShell scripts.

**Windows is MinGW-w64 GCC only — MSVC cannot build this.** The PS2 project
templates in `src/templates.cpp` are raw string literals far past MSVC's hard
16380-byte cap (*C2026 string too big*), and no flag lifts it. Point VS's CMake
settings at the scoop MinGW kit, or just use `build.ps1`.

Then, in the editor:

1. **`File > New Project`** (`Ctrl+N`) — name, location, world scale, terrain
   size and one of three presets (FPP / third person / empty). The preset is
   fixed for the project's life; everything else is editable later.
2. The *Viewport* shows the terrain (drag to orbit, scroll to zoom).
3. **Build & Run** (`F5`) — the first build pulls the `h4570/tyra` image and
   compiles the engine inside the container (minutes, once). Later builds take
   seconds, and PCSX2 boots the ELF automatically.

## Requirements

- **Windows or Linux.**
- [Docker](https://www.docker.com/products/docker-desktop/) running — the game is
  compiled inside the `h4570/tyra` container.
- [PCSX2](https://pcsx2.net/) with a BIOS configured (auto-detected in
  `Program Files\PCSX2`, on `PATH`, as a flatpak or an AppImage; any other
  location goes in `Edit > Preferences`).
- To build the editor: CMake, Ninja, GCC — **MinGW on Windows**, plus the
  X11/Wayland/GL headers on Linux (`./setup.sh --deps`). `zenity` or `kdialog`
  provides the native file dialogs on Linux.
- **Keep the project path short.** PCSX2's `host:` loader silently refuses an ELF
  path longer than ~145 characters — the game never starts and nothing is logged.
  The editor warns in *Output*.

## What it does

Each line links to its guide; the full index is [docs/README.md](docs/README.md).

**World & assets**

- **Scenes and objects** — primitives, models, lights, markers and gameplay
  volumes, each in its own `objects/<id>.json` so a team edits different objects
  without git conflicts. Picking, gizmos, rubber-band selection,
  [surface snapping and cursor-following paste](docs/object-placement.md),
  [orthographic and axis views](docs/orthographic-views.md), and a viewport that
  can rasterize the way [the console does](docs/ps2-viewport.md).
- **[Terrain](docs/terrain.md)** — optional per scene, sculpted with a brush and
  [painted with blended material layers](docs/terrain-painting.md).
- **Models** — `.obj` compiled into a binary
  [`.tmdl` with distance LOD](docs/model-pipeline.md), and
  [`.glb`/`.fbx` animated models](docs/animated-models.md) with a non-destructive
  clip editor that can also
  [borrow clips from another rigged file](docs/animation-import.md) — drop a
  Mixamo download onto a character you already have.
- **[Asset Browser](docs/asset-browser.md)** — a real file manager over `res/`
  that knows who references every asset and moves files with their references.
- **[Materials and texture painting](docs/material-painting.md)** — `.mtl`
  authoring, a layer stack painted onto your own mesh, UV unwrap/validator, and
  [raytraced map bakes](docs/material-baking.md) with smart masks.
- **Generators** — [procedural scatter graphs](docs/procedural-generation.md)
  baked to chunk meshes or [run on the EE](docs/procedural-runtime.md),
  [prefabs](docs/prefabs.md), the [Tree Generator](docs/tree-generator.md) and
  the [Drone Generator](docs/drone-generator.md) for ambient music.
- **[World scale](docs/world-scale.md)** — one number that keeps imported reality
  the size your own content is.

**Look**

- **Lighting** — directional light, point lights (baked or dynamic, with
  flicker), light beams, blob and projected shadows, lens flare and god rays.
- **Baked realism** — [global illumination + light probes](docs/global-illumination.md),
  [ambient occlusion](docs/ambient-occlusion.md) and a
  [day/night cycle](docs/day-night-cycle.md) the whole bake follows.
- **Surfaces** — [emissive materials](docs/emissive-materials.md),
  [sphere-mapped chrome](docs/reflective-materials.md), Mirror objects,
  [VU0-raytraced mirrors](docs/raytraced-reflections.md),
  [live texture feeds](docs/texture-feeds.md) and [portals](docs/portals.md).
- **Screen** — sky, fog, bloom, film grain and your own
  [`.screenfx` effects](docs/custom-screen-effects.md), plus
  [TV safe areas](docs/safe-areas.md) to frame against.
- **Frame delivery** — the [neural upscaler (BLSS)](docs/neural-upscaler.md),
  [frame extrapolation](docs/frame-extrapolation.md) and
  [triple-buffered pacing](docs/frame-pacing.md).

**Gameplay & logic**

- **Flow graph** — visual logic (triggers → actions), extensible with your own
  [`.flownode` nodes](docs/custom-flow-nodes.md).
- **[Object scripts](docs/object-scripts.md)** — Unity-style C++ components in
  `src/scripts/`, a directory the editor never touches.
- **Player and physics** — FPP / third-person / noclip player entities with
  [walk, run and sprint speeds](docs/player-speeds.md), rigid
  bodies, [collision boxes](docs/collision-boxes.md), pickable and usable
  objects, and [two-player shared or split screen](docs/multiplayer.md).
- **World state** — [areas](docs/areas.md),
  [streaming layers](docs/streaming-layers.md),
  [world facts](docs/world-facts.md), runtime spawning and the
  [endless scroller](docs/endless-scroller.md).
- **[NavMesh + NPC AI](docs/navigation-ai.md)** — baked on the host, A* on the EE.
- **Cinematics** — the Cutscene Director, fed by keyframes or by a
  [phone-recorded 6DoF take](docs/camera-takes.md) or the
  [live phone viewfinder](docs/phone-camera.md), with per-sequence camera,
  player and HUD control. A player flashlight is suspended instead of following
  the cinematic camera, and the `Play Sequence` flow node exposes an `after`
  output for chaining gameplay to that exact playback instead of guessing its
  duration with a timer.
- **Input** — [named actions and rebinding](docs/input-bindings.md),
  [button glyphs in text](docs/text-icons.md), and
  [USB keyboard & mouse](docs/keyboard-mouse.md).
- **Audio** — music, sound emitters, [voice priority](docs/sound.md) and
  [hardware reverb rooms](docs/reverb.md).

**The game around the game**

- HUD sprites, fonts, baked on-screen texts and runtime text.
- Menus with [CSS-shaped stylesheets](docs/menu-styles.md) and scaffolded options
  screens (volume, controls, video mode).
- [Loading screens](docs/loading-screens.md) and boot splashes,
  [credits rolls](docs/credits.md), and
  [memory card saves + checkpoints](docs/save-editor.md).

**Performance**

- Static batching, [texture atlasing](docs/texture-atlasing.md),
  mesh LOD, draw distances and [GS VRAM residency](docs/gs-vram.md).
- The in-game [frame profiler](docs/profiling.md).
- The [VU framework](docs/vu-framework.md): describe a microprogram in C++,
  generate both sides of it and run it in a host simulator with no PS2 —
  and [compose VU1 programs out of stages](docs/vu-authoring.md), or write a
  VU0 compute kernel, with no assembly.

**Iterating on a running game** — the [devkit](docs/devkit.md), and a release
build that provably carries none of it

- **Build & Run** in PCSX2 (`F5`), or on a
  [real PS2 over ethernet](docs/ps2link-setup.md) (`F6`).
- [Live Link](docs/live-link.md) (edit the running game),
  [Live Logic](docs/live-logic.md) (hot-patch a flow graph),
  [the Live Debugger](docs/live-debugger.md) (breakpoints, stepping, watches),
  [the time machine](docs/time-machine.md) (put the game back where it was) and
  [the Remote Pad](docs/remote-pad.md) (hold its controller, no focus needed).
- VU1 packet capture, self-reporting crashes and
  [logs split by severity](docs/log-panels.md).
- [UI scripting](docs/ui-scripting.md) — the editor drives itself by widget name.

**Team & AI**

- Git-friendly by construction, plus
  [live LAN collaboration sessions](docs/collaboration.md).
- The [AI Assistant](docs/ai-chat.md) (these docs are baked into the exe, and it
  can edit the project), [AI flow-graph generation](docs/ai-flow-graph.md), the
  [AI-agent CLI](docs/ai-tools.md) and
  [AI support files in projects](docs/ai-support.md).
- The [VS Code extension](docs/vscode-extension.md) for `.flownode`/`.screenfx`,
  [interface themes](docs/editor-theme.md), and
  [format versioning and migrations](docs/format-versioning.md).

## Shortcuts

| Shortcut | Action |
| --- | --- |
| `Ctrl+N` / `Ctrl+O` / `Ctrl+S` | New / open / save project |
| `F5` / `F6` | Build & run in PCSX2 / on a real PS2 |
| `Ctrl+Z` / `Ctrl+Y` | Undo / redo |
| `Ctrl+C` / `Ctrl+V` | Copy / paste the selected object (the paste follows the cursor; `Esc` cancels) |
| `1` / `2` / `3` | Move / rotate / scale tool (viewport) |
| `Delete` / `End` | Delete the selection / drop it to the floor (viewport) |
| `Num 5` / `Num 1` / `Num 3` / `Num 7` | Perspective ⇄ ortho / front / right / top view (`Ctrl` = opposite side) |

## Example projects

Every project under [examples/](examples) opens with `File > Open Project` and
has its own README saying what to look at and how it is wired.

Most examples are focused technical exhibits, so some still favour clarity over
presentation. The showcase is the polished exception: a full playable tour
built to demonstrate how those systems work together.

| Example | What it shows |
| --- | --- |
| [script-demo](examples/script-demo) | Start here. Walk to the box, press X, and the sky obeys — one object script, and you've touched the whole pipeline |
| [showcase](examples/showcase) | Three worlds collide: a cinematic fantasy village, portal laboratory and neon city combining animation, AI, streaming, physics, VU0 ray tracing, GI and every ounce of PS2 spectacle |
| [layer-streaming](examples/layer-streaming) | Two buildings, one corridor — and the building behind you quietly stops existing, GTA3-style |
| [large-terrain](examples/large-terrain) | A 2048×2048 world that could never fit in 32 MB of RAM. It doesn't have to |
| [cutscene-demo](examples/cutscene-demo) | 14 seconds of dolly, hard cut, shake, FOV ramp and cinema bars. Skippable, of course |
| [nav-ai](examples/nav-ai) | A guard that patrols, spots you, and chases you around the wall instead of into it. The rabbit just runs |
| [physics-playground](examples/physics-playground) | 28 hyperactive bodies rain onto a terraced slope. Doubles as the physics benchmark |
| [object-spawning](examples/object-spawning) | GTA-style traffic conjured and dismissed by two flow-graph nodes |
| [portals](examples/portals) | A cube falls through a portal pair forever. You get to walk through instead |
| [mirror-room](examples/mirror-room) | The classic PS2 mirror trick, shown from backstage — your reflection included |
| [raytraced-mirror](examples/raytraced-mirror) | Reflections ray-traced per pixel on VU0. On a PS2. There's a resolution knob |
| [reflections](examples/reflections) | Static sphere maps vs the live `@sky` mode — with a sky cycler so you can catch the difference |
| [probe-aim](examples/probe-aim) | A chrome ball that shows what's behind you: probes aimed along the reflected ray |
| [texture-feeds](examples/texture-feeds) | Two monitors on a wall — one plays live CCTV, the other a raytraced mirror |
| [lighting](examples/lighting) | One dusk plaza wearing everything at once: torches, shafts, flare, god rays, shadows, a flashlight |
| [glow](examples/glow) | A midnight walk through four stations of things that glow |
| [global-illumination](examples/global-illumination) | One red wall, one green wall — every other tint in the room is bounce |
| [gi-showcase](examples/gi-showcase) | The guided GI tour, ending in a room lit by nothing but bounce |
| [day-night](examples/day-night) | The same place at dawn, noon, dusk and night — plus one scene where the clock actually runs |
| [material-lab](examples/material-lab) | The material pipeline on a single pedestal: baked AO, smart masks, atlasing, live reload |
| [procedural](examples/procedural) | Every node in the scatter library at work in six volumes, baked down to 17 chunk meshes |
| [blocks-terrain](examples/blocks-terrain) | A cube world the EE invents at boot. Press TRIANGLE for a new one. Still 50 FPS |
| [cube](examples/cube) | A 3×3×3 lattice of rooms — prefabs times runtime generation, in ~4 draw calls |
| [world-facts](examples/world-facts) | Every fact type and all four persistence tiers, exercised across a two-scene level |
| [save-points](examples/save-points) | Both halves of saving: in-RAM checkpoints, and a 3-slot memory-card shrine with a 3D icon |
| [credits](examples/credits) | An end roll straight from a text file — plus a card-mode dedication that remembers where you left it |
| [two-players](examples/two-players) | Couch co-op: 1P/2P title menu, split screen, and a friend hot-joining on pad 2 |
| [reverb-rooms](examples/reverb-rooms) | The same knock in four rooms. Only the acoustics change |
| [custom-nodes](examples/custom-nodes) | Roll your own flow nodes: one backed by C++, one written inline |
| [endless-scroller](examples/endless-scroller) | An infinite tunnel, tiled from a single authored chunk |
| [endless-runner](examples/endless-runner) | An endless track that never repeats — variant groups, per-chunk odds, ever-rising speed |
| [upscaler-lab](examples/upscaler-lab) | The fill-bound scene built to make the neural upscaler sweat. It wins: 1.63× on real hardware |
| [video-modes](examples/video-modes) | 480i / 480p / 1080i and 4:3 / 16:9, switched at runtime — with keep-or-revert |
| [vu-lab](examples/vu-lab) | Five props on five VU1 paths — capture a draw off the console, replay it on the host |

## CLI

Projects can be created, built and inspected headlessly:

```bash
tyrax-editor --new <name> <parentDir> [width] [depth] [empty|fpp|thirdperson] [unitsPerMeter] [--no-terrain]
tyrax-editor --build <projectDir> [--run] [--run-ps2 [ip]] [--rebuild]
tyrax-editor --resave <projectDir>        # load + save in the current format
tyrax-editor --migrate <projectDir>       # backup + apply format migrations
tyrax-editor --refresh-gen <projectDir>   # regenerate game sources (no Docker)
tyrax-editor --debug-state [dir]          # which project is being debugged, and how fresh its devkit files are
tyrax-editor --vu-check                   # run every VU1 microprogram in the host simulator
tyrax-editor --vu-replay <projectDir>     # re-run a console VU1 capture on the host and diff it
tyrax-editor <projectDir|project.tyra>    # open the GUI with a project loaded
```

(`build\tyrax-editor.exe` on Windows, `build/tyrax-editor` on Linux.)

A project needing real data migrations is refused by `--build`/`--resave`/
`--refresh-gen` — run `--migrate` first, see
[docs/format-versioning.md](docs/format-versioning.md). A second family of
commands (`--dump`, `--list-nodes`, `--dump-graph`, `--apply-graph`, `--ai-graph`,
`--add-ai-support`, `--chat-prompt`) targets AI assistants working inside a
generated project: [docs/ai-tools.md](docs/ai-tools.md). The editor can also drive
its own UI (`--ui-script`) and the game's controller (`--pad`) unattended.

## How Build & Run works

`docker compose up -d` gives the project its own container from the `h4570/tyra`
image. The engine sources in `vendor/tyra` are bind-mounted read-only, synced into
a shared build volume with a checksum `rsync` and rebuilt only when they changed —
so every project built from the same checkout shares one `libtyra`. Then the
project's sources are rsynced in, `make -j` runs inside the container, `bin/` comes
back to the host and PCSX2 is launched on the ELF.

Every step is incremental, code generation included: a generated file whose content
did not change is not rewritten, so a build with nothing to do finishes in seconds.
**Build > Rebuild** drops the container, the objects and the compiled engine when
an incremental build cannot see what went wrong; *Clean* also wipes `bin\`.

## Run on a real PS2

With a console on the LAN running the **TyraX ps2link**, **Build > Build && Run on
PS2** (`F6`) boots the game over ethernet: the ELF and every asset are served from
the project's `bin\` on this PC (no ISO, no SMB) and the console's log streams into
*Output* as `[ps2]` lines. Set the IP in `Edit > Preferences > Real PS2`.

The console side is always **our own** ps2link — a pinned upstream plus this repo's
patch, built in Docker by [`tools/ps2link`](tools/ps2link/README.md) and flashed to
a memory card once; it bakes in the USB keyboard/mouse stack and fixes the hangs
that made a network session need a console reset. Stock ps2link is not a supported
target. The one-time setup — hardware, flashing, `IPCONFIG.DAT`, firewall ports and
every failure message — is **[docs/ps2link-setup.md](docs/ps2link-setup.md)**.

## The engine

The engine half of TyraX lives in `vendor/tyra/engine`: a fork of the
[Tyra engine](https://github.com/h4570/tyra) (Apache-2.0, forked at `9273416`)
developed directly in this repo rather than consumed as a dependency — edit it and
the next Build & Run picks the change up, for every project built from this
checkout. Every departure from upstream is marked `Modified by TyraX` in the
sources: clipper outcodes and guard bands, static pools instead of per-call
allocations, a real WAV header parser, DualShock actuators, and the VU1/GS work the
editor's features are built on. Generated games additionally enable the precise
per-triangle clipper and correct the upstream `clipMargin`.

Before each build the editor refreshes its generated files. Files carrying a
`Generated by TyraX` marker on their first line are rewritten; **delete that line
to take ownership** of a file, and the editor stops regenerating it.

## Structure

- `vendor/tyra/engine` — the engine: the PS2 runtime every generated game links
  against (versioned in this repo, Apache-2.0).
- `src/` — editor code: the UI shell (`app`) with the big panels in sibling TUs
  (`props_ui`, `flowgraph_ui`, `hud_ui`, `cutscene_ui`, `mateditor_ui`,
  `devkit_ui`, `chat_ui`), `assetbrowser`, `viewport` (GL preview),
  `project`+`templates` (the project generator), `runner` (Docker/PCSX2),
  `platform` (the single OS abstraction), and `vuir`/`vuasm`/`vusim`/`vugen`
  (the [VU framework](docs/vu-framework.md)).
- `docs/` — the user guides, and the **AI Assistant's knowledge base**: every page
  is embedded into the exe at build time, so a page written for a human teaches
  the assistant too.
- `ai-support/` — the assistant guides installed into generated projects.
- `examples/` — the example projects listed above.
- `vendor/` (rest) — editor dependencies, fetched at pinned commits from the one
  list per platform (`deps.ps1` / `deps.sh`).
- `tools/` — the PS2 network-deploy tools (`ps2client`, the
  [TyraX ps2link](tools/ps2link/README.md)) and the
  [VS Code extension](docs/vscode-extension.md).
- Developer architecture guides live under [.claude/skills/](.claude/skills).

## Credits

This project stands on the shoulders of the PS2 homebrew community:

- **[Tyra engine](https://github.com/h4570/tyra)** by Sandro Sobczyński (h4570)
  and contributors (Apache-2.0) — forked in-tree, and the source of the
  `h4570/tyra` Docker toolchain image. License text:
  [`vendor/tyra/LICENSE`](vendor/tyra/LICENSE).
- **[ps2client](https://github.com/ps2dev/ps2client)**,
  **[ps2link](https://github.com/ps2dev/ps2link)** and
  **[PS2SDK](https://github.com/ps2dev/ps2sdk)** by the
  [ps2dev project](https://ps2dev.github.io/) — the network link behind "Run on
  PS2" and the SDK every generated game links against
- Editor dependencies: 
  [Dear ImGui](https://github.com/ocornut/imgui),
  [GLFW](https://www.glfw.org/),
  [ImGuizmo](https://github.com/CedricGuillemet/ImGuizmo),
  [imnodes](https://github.com/Nelarius/imnodes),
  [stb](https://github.com/nothings/stb),
  [ufbx](https://github.com/ufbx/ufbx),
  [miniaudio](https://github.com/mackron/miniaudio).
- **[PCSX2](https://pcsx2.net/)** — the emulator behind every `F5`.
- **[Tyra Discord](https://discord.gg/PpTAkQh6u)** — the amazing project community.

Every notice, full license text and exact redistribution term — plus the
file-by-file policy for third-party assets — is in
[THIRD-PARTY-LICENSES.md](THIRD-PARTY-LICENSES.md).

## License

TyraX — the editor and the engine fork alike — is licensed under the
**Apache License 2.0** ([LICENSE](LICENSE), [NOTICE](NOTICE)): upstream Tyra is
already Apache-2.0, so matching it keeps the whole tree under one set of terms.

**Games you generate are not covered by that.** You can release a game made with
TyraX commercially, with closed source: TyraX writes a project's C++ from templates
in this repository, and [LICENSE-EXCEPTION.md](LICENSE-EXCEPTION.md) grants that
generated output to you with **no conditions at all** — no attribution, no license
text, no notice, no source disclosure.

The one thing an exception cannot waive, because the rights are not TyraX's to
waive: a generated game links the **Tyra engine** (Apache-2.0) and **PS2SDK**
(Academic Free License v2.0). Neither is copyleft — neither obliges you to publish
source, restricts commercial use, or reaches your own game logic, art, audio or
levels. Both ask only that the credit travels with the binary, and that is handled
for you: every project TyraX creates gets a **`THIRD-PARTY-NOTICES.txt`** at its
root, pre-filled with exactly those notices. Ship it beside the ELF, in the package
or as an in-game credits screen and you are compliant. It is written once and never
regenerated, so your own credits added to it survive every build.
