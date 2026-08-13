# Backlog

This is only unfinished work that still has a clear payoff and a testable end.
Finished investigations belong in commit history; reusable facts belong in the
relevant guide or developer skill.

The retired `PROGRESS.md` is still available when old implementation history is
actually needed:

```bash
git log --diff-filter=D -- PROGRESS.md
git show <retirement-commit>^:PROGRESS.md
git log -p --follow -- PROGRESS.md
```

## Small

### Ignore baked models in nested asset folders

The generated `res/.gitignore` covers `/models/*.tmdl`, `*.tskl` and
`*.tanm`, but not models below `res/models/<folder>/`. Use recursive rules in
both new-project generation and `--refresh-gen`, then verify a nested imported
model leaves no derived files in `git status`.

### Search the log panels

Add a text filter plus next/previous-error actions to Output and Debug. Reuse the
existing parsed entries and severity filters; continuation lines must stay with
their parent entry. See [log panels](log-panels.md).

### Select tabs explicitly in UI scripts

Add a `tab <window>/<name>` step. It should select the tab, wait until its body
is submitted, and fail clearly when either the window or tab does not exist.
See [UI scripting](ui-scripting.md).

### Add aliases to documentation search

Give the AI Assistant's exact-substring docs search a small hand-written alias
table, for example `lag -> frame time` and `collision box -> collision mode`.
Keep the mapping visible and deterministic rather than introducing fuzzy search.
See [AI chat](ai-chat.md).

### Report triple-buffer fallback after a display-mode change

A runtime display-mode switch may correctly fall back from three buffers to two
when VRAM is tight. Expose that result so an options menu can tell the player
what actually happened. Keep the answer tied to the engine allocation result,
not a second host-side guess. See [frame pacing](frame-pacing.md).

### Stop polling interval-zero sound emitters every frame

An emitter with `interval = 0` currently retries `audsrv` every frame. Schedule
the next play from the sample length instead: one call per loop, still seamless,
including after a dropped frame. See [sound](sound.md).

### Audit the remaining audsrv RPC calls for blocking work

Review the fork's RPC handlers and their callers for waits that can stall the
single audsrv path. Record the cost beside each caller and replace blocking
polls where the API allows it. The music-streaming stall is the known control.

### Bound terrain UVs on very large maps

Terrain UVs grow with world position and can outrun the GS fixed-point range.
Fold each chunk by whole texture repeats during generation, preserving the
picture under REPEAT while bounding coordinates by chunk size.

## Medium

### Move generated scene data out of the header

Moving one object changes `scene_data.hpp` and currently invalidates most game
translation units. Emit the data into one generated `.cpp`; keep only stable
types, declarations and shape constants in the header. Audit every
`constexpr`/array-size consumer and prove unchanged projects regenerate
byte-identically before measuring the rebuild win.

### Preview BLSS in the editor viewport

The viewport already renders at PS2 resolution and presents through a fragment
shader. Add an honest preview of the selected BLSS mode and debug view; do not
ship a visual approximation that disagrees with the console. See
[neural upscaling](neural-upscaler.md).

### Drag HUD images in the viewport

Let authors move HUD images directly in the viewport with snapping and numeric
properties staying in sync. The drag must respect the active display mode,
logical canvas and widescreen behavior.

### Unify HUD, loading-screen and credits coordinates

Menus use a logical 512x448 canvas and compensate for output mode and anamorphic
widescreen. Decide which non-menu elements preserve aspect and which pin to
screen edges, then apply one coordinate model to the HUD, loading screens and
credits. See [menu styles](menu-styles.md).

### Finish the BLSS proxy budget twin

The engine-side proxy cap exists behind `TYRA_BLSS_PROXY_BUDGET` but stays off
because the host corpus does not apply the identical rule. Implement the same
whole-box projection, tile count and part stride on the host; enable both twins
in one change and re-run parity plus performance measurements. See
[BLSS reconstruction](blss-reconstruction.md).

### Specialize VU programs per project

Generate only program variants a project may use, including spawn-pool prefabs.
Keep a generate-everything path for Live Link so runtime additions cannot ask
for a missing program. Measure micro-memory headroom and program-set swaps.
See [VU authoring](vu-authoring.md).

### Capture object-data uploads in VU replay

`--vu-replay` captures the qbuffer chain but reconstructs some per-mesh
constants from a memory snapshot. Capture the object-data upload chain too, so
input and output can be paired exactly on both PCSX2 and hardware. See
[the VU framework](vu-framework.md).

### Query runtime procedural compatibility from the AI Assistant

Expose the editor's capability check for a specific procedural graph as a
read-only AI tool. The answer must name unsupported nodes and parameters rather
than returning a bare yes/no. See [runtime procedural generation](procedural-runtime.md).

## Large

### Add terrain mipmaps

Build and upload an opt-in mip chain, terrain first, and use the GS LOD path to
reduce distant shimmer and moire. Account for the roughly 33% texture-memory
cost, verify small-level addressing on hardware, and keep non-mipped textures
unchanged.

### Render particle emitters in the BLSS corpus

The corpus counts emitter coverage and can describe emitter proxies, but its
training images still omit the particles themselves. Render the same billboard
population, motion and blending as the runtime, then retrain and re-evaluate the
affected example projects. See [neural upscaling](neural-upscaler.md).

### Refresh dynamic content on extrapolated frames

Frame extrapolation carries camera motion between rendered world frames, while
animations, moving objects and HUD updates remain at the world rate. Redraw the
dynamic layers on the synthetic presentation without feeding that frame into
BLSS history. See [frame extrapolation](frame-extrapolation.md).

### Add a frame-extrapolation guard band

Render beyond the visible picture so a camera warp reveals real pixels instead
of stretching the edge. This changes raster size, frustum math and BLSS
host/console assumptions, so treat it as a shared rendering design rather than
a larger texture allocation.

### Grade palettized textures through their CLUT

Remap palette entries through a grading curve for per-pixel textured colour at
no extra draw pass. Pair it with a defined path for untextured geometry, settle
whether runtime palette updates are cheap, and avoid grading any surface twice.

### Load ps2link USB modules from the memory card

Replace the embedded USB IRX buffers with `SifLoadModule` calls to modules next
to `PS2LINK.ELF`. The goal is one build that boots from FreeMcBoot and still
supports keyboard and mouse; verify paths, missing-module feedback and both
launchers on hardware. See [ps2link setup](ps2link-setup.md).

### Host collaboration sessions over the internet

Add an invite-link transport on top of the existing `wire::Transport`
interface, preferably using an optional tunnel rather than exposing a raw
listening port. Define authentication, session lifetime and failure UI before
shipping it. LAN and mesh-VPN sessions must keep working unchanged. See
[collaboration](collaboration.md).
