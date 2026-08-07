# Backlog

What is queued and what is being worked on. This is the forward-looking half of
the retired `PROGRESS.md`; the ~15 800 lines of finished-work entries that used
to sit alongside it are **not** lost - they are in git history:

```
git log --diff-filter=D --  PROGRESS.md      # the commit that retired it
git show <that commit>^:PROGRESS.md          # the whole file as it last stood
git log -p --follow -- PROGRESS.md           # or read it entry by entry
```

Finished work is now recorded where it is useful rather than in one growing
file: the commit message says what changed and why, the PR description carries
the verification, and any fact worth reusing belongs in the relevant
`docs/` page or skill.

## In progress

- Lighting-effects batch: dynamic point lights (done, 113), sun lens flare,
  god rays, dynamic light on animated models, visible beams, blob shadows.

## Queued (rough order)

- **Neural upscaler (BLSS) follow-ups** (docs/neural-upscaler.md,
  docs/blss-reconstruction.md). The proof of concept shipped: half-res 3D
  render, sub-pixel `XYOFFSET` jitter, an 8-12-3 MLP trained on the host and
  baked into the game, and a 1..5-pass Gouraud composite whose blend fields are
  the network's output. What it deliberately does not do yet, roughly in the
  order it is worth doing:
  - **DONE, AND THE PRESCRIBED FIX WAS REFUTED BEFORE IT WAS WRITTEN: make the
    raster-redirect brackets nest.** `RendererCoreEnvMap::end()`, the camera
    feed and `RendererCoreShadowMap::end()` restored FRAME/SCISSOR/ZBUF/XYOFFSET
    from `gs->getCurrentFrameBuffer()` - the display buffer *unconditionally*
    rather than whatever was redirected before them - from INSIDE the generated
    `renderScene()`, so the first of them cancelled the BLSS redirect and the
    rest of the frame drew full-res into the display buffer, where the
    composite's opaque base pass painted over it. Silent: no assert, no
    signature except "BLSS did nothing".

    This entry used to prescribe the minimal fix - have
    `getCurrentFrameBuffer()` return the BLSS target while the bracket is open,
    "and none of the three `end()` implementations changes". **That is not
    sufficient and would have shipped a differently broken frame.** Those
    implementations restore FOUR registers and only FRAME comes from that
    accessor: SCISSOR and XYOFFSET come from `RendererSettings`, so a nested
    bracket would have left a 512-wide scissor over a 256-wide FRAME (writes
    wrap into the next row) and a window-centred offset for the wrong window,
    with the frame's sub-pixel jitter dropped. Moving the accessor would also
    have been wrong for its four post-fx callers and for BLSS' own
    `endScene()`/`composite()`, which genuinely want the display buffer.

    What shipped instead is an explicit raster target on `RendererCoreGS`:
    `RasterTarget` (frame address, FBW, scissor, XYOFFSET in 1/16 px) with
    `getRasterTarget()` / `redirectRasterTo()` / `endRasterRedirect()` /
    `emitRasterRestore()`. `getCurrentFrameBuffer()` keeps its old meaning and
    is documented as "the display buffer". Verified in PCSX2 on a fixture with
    three `projShadow` casters: before, no projected shadow was drawn at all;
    after, they are there and visibly soft-edged, i.e. produced inside the
    low-res target and reconstructed by the composite, at 50 FPS / 100 % speed.

    **Two latent bugs came out with it, neither of them BLSS's** and both live
    in any project using the feature, upscaler or not: none of the three
    restores carried the `InterlacedField` per-field XYOFFSET bias
    (`getFieldYOffset16`), so a bracket in that mode handed the rest of the
    frame a raster window half a scan line off; and the shadow-map restore left
    ZBUF on its own 64x64 silhouette buffer, so every projected-shadow receiver
    patch failed GEQUAL - drawn, then discarded. `emitRasterRestore()` writes
    ZBUF explicitly and LAST.

    **STILL OPEN: the PORTAL bracket was not converted.**
    `RendererCorePostFx::portalMaskBegin/End` also run inside `renderScene()`
    and still take FRAME from `gs->getCurrentFrameBuffer()` plus a display-sized
    SCISSOR and XYOFFSET - the same bug, a fourth time. It does not gate the
    feature, because portals are independently incompatible with BLSS (they want
    real display-resolution depth), but converting them to `emitRasterRestore()`
    is what would leave "portals are incompatible" a depth argument only.
  - **DONE, AND THE PRESCRIBED FIX WAS REFUTED BY ITS OWN MEASUREMENT: flicker
    in the ORACLE's objective.** This entry used to say "score a candidate over
    a PAIR of consecutive frames with a penalty on the difference". That was
    implemented, swept and **measured to be a bad trade**. It is
    `--flicker-weight`, it defaults to **0**, and the knob and the term both
    stay - a weight set to zero after measuring is not the same as a feature
    deleted. Swept jointly with `--fill-weight` (84 frames, 600 epochs, 3-6
    training seeds per point), at fill 6:

    | `--flicker-weight` | 0.00 | 0.02 | 0.05 | 0.15 |
    |---|---|---|---|---|
    | held-out PSNR | 23.38 | 23.16 | 22.90 | 22.37 |
    | training flicker | 21.01 | 20.86 | 20.80 | 20.20 |

    (bilinear: 23.26 dB, 23.41 flicker.) Every non-zero setting pays 0.2-1.0 dB
    of OUT-OF-DISTRIBUTION quality for a few percent of flicker, and 0.02 upwards
    already scores below plain bilinear. **The form is wrong, not just the
    weight**: MSE against the reprojected history is minimised by the picture
    FREEZING, which is free on the near-static training shots and is ghosting on
    the held-out orbit and dolly - it cannot tell "stable because the jitter got
    fused" from "stable because nothing moved". If this is picked up again, gate
    the penalty on reprojection confidence FIRST.

    **What delivered the stability instead was the fill term** (next entry), by
    culling the point and sharpen passes - which are exactly the two that
    alternate with the jitter. At flicker 0, moving fill 0 -> 6 took flicker from
    21.49 to 21.01 (training) and 27.12 to 26.62 (held out) at no quality cost.

    The earlier **"lock the jitter phase to the field parity" theory stays
    REFUTED**, and the reason is worth keeping: a blending deinterlacer was
    averaging adjacent frames and hiding the symptom, which is exactly why
    enabling it looked like a fix, and the bob survives into progressive 480p
    unchanged. The real cause is that jittered sampling produces a different
    frame every time - which is the POINT, it is where the extra information
    comes from - and the temporal accumulator was not fusing it. Nothing about
    interlacing or field parity is involved.

    **STILL OPEN, and it is now the only open half of this item: nobody has
    watched the emulator since the fill term landed.** Every console observation
    on record was taken from a net trained by the old fill-blind objective. The
    host's flicker metric improved with the retune; whether the picture is still
    bobbing on a television is UNVERIFIED in both directions. That is a
    twenty-minute PCSX2 boot and it gates the feature.
  - **DONE: charge the oracle for the fill it asks for.** `--fill-weight`,
    default 6, charged as a STEP on the quantised alpha byte (a weight rounding
    to alpha 1 costs a whole pass and buys nothing, so a smooth penalty would
    park there), mirroring the engine's own skip rule. `--blss-eval` gained the
    occupancy columns so the effect is visible in the tool rather than in one
    report. Measured: sharpen occupancy 79% -> 28% training and 93% -> 14% held
    out; mean full-screen passes 4.25 -> ~3.4 against a 5.00 worst case and 1.00
    for plain bilinear; quality unchanged in distribution. The knee is sharp -
    at fill 7.5 the network stops generalising, and at 12 it scores a full
    decibel BELOW plain bilinear.

    **What this did NOT fix, and it is a live follow-up: point and temporal are
    still drawn over most of the screen.** Measured on the shipped net, 59%/81%
    of grid cells in distribution and **96%/99% out of it**, where the oracle
    reaches better PSNR at 1.53-1.94 total passes against the network's
    2.99-3.29. So roughly half the remaining fill is the network failing to
    generalise the cost model, not a floor - and the doc claim that "passes 2..5
    cover a minority of the screen" is true of the SHARPEN pass only. Occupancy
    is also seed-sensitive (2.99-3.60 training, 3.29-4.27 held out over four
    seeds) while PSNR barely moves, so it is not a number to put in a budget.
  - **Held-out numbers in this feature are inside the seed noise, and every
    write-up must say so.** The split is 2 shots out of 7 and held-out PSNR moves
    +-0.4 dB on the seed alone. Re-running train+eval at four seeds: BLSS beats
    bilinear out of distribution by +0.16, +0.26, **-0.23** and +0.22 dB, i.e. a
    mean of **+0.10 dB and one loss in four**. The in-distribution win is the
    solid one (+1.03 .. +1.15 over the same four). Two earlier write-ups quoted
    "+0.18 dB" and "+0.24 dB" held-out from single runs; both were noise, and
    both cost debugging time. Worth doing properly: make the seed spread part of
    what `--blss-eval` reports, so the tool states its own error bar instead of
    each report having to remember to.
  - **DONE: shrink the z-buffer, and BLSS is now measurably VRAM-POSITIVE.**
    `RendererCoreGS::allocateVramBuffers` sizes z from
    `RendererSettings::getRasterWidthUI/HeightUI` instead of the display buffer:
    57 344 words instead of 229 376 at 2x2, i.e. 672 KB back at 512x448 and
    768 KB at 512x512, against 224 / 256 KB for the low-res colour target. The
    ordering problem (z is allocated third in `gs.init()`, long before a
    generated game's `init()` calls `blss.configure()`) is solved by laying the
    permanent VRAM region out again from `configure()`, through
    `RendererCore::rebuildPermanentBuffers()` - `setDisplayOutput`'s mode-change
    branch minus the mode, so `vram.reset()` keeps one implementation and the
    frame buffers come back at the same addresses. It runs before `buildScene()`
    loads any asset, so docs/gs-vram.md's "permanent buffers before any texture"
    invariant holds.

    What makes it safe is one invariant, and it is the thing to keep: **
    `zBuffer.mask` is 0 only INSIDE the low-res bracket.** Every
    `draw_enable_tests` / `draw_setup_environment` in the engine reads that one
    field, so the 2D/HUD/post-fx half of the frame - full-screen sprites at
    z = 0xFFFFFFFF, which would otherwise stamp 448 rows at a 512 stride -
    cannot write past the smaller allocation.

    Measured, PCSX2 software renderer, scratch `fpp` project, `Pal576i`
    (512x512), the game's own VRAMSTAT line at frame 240: free VRAM **0.227 MB
    with BLSS off, 0.234 MB with BLSS on before the change (and one eviction -
    which is what put that space back), 0.727 MB after, with no eviction**;
    largest free block 232 KB -> 744 KB; z 262 144 -> 65 536 words. So BLSS on
    now leaves half a megabyte MORE texture VRAM than BLSS off, which is exactly
    the 768 KB returned minus the 256 KB target. **Measured in that display mode
    and no other.**
  - **Train on the project, not on the procedural corpus.** `--blss-train
    <projectDir>` is the shape; `src/blsscorpus.cpp` is already structured so
    the scene source is swappable, and the features come from `BagProxy` lists
    that a real scene walk can produce as easily as the rasteriser does.
  - **Bake a per-material UV-repeat constant.** `texDetail` is the texel-density
    proxy, and the engine can only supply `texW * texH` (`stapip_core.cpp`): it
    does not know how many times a material tiles over a surface. The corpus can,
    and folding the UV span in makes the channel a better aliasing predictor. But
    it is then a quantity only one side computes (a floor tiling 100x trains at
    1.0 and runs at 0.03), so the corpus was cut back to the raw area for parity.
    Bake the repeat per material, multiply it in on the engine side, and both
    sides get the strong feature.

    **Correction, because this entry's own headline is out of date.** It used to
    read "this is what stands between BLSS and being worth enabling", on the
    strength of 23.24 dB held-out with the UV span against 21.03 dB without it.
    The 21.03 dB baseline no longer exists: the `kTemporalMax` accumulator fix
    and the `kMinCoverage` sky fix have since taken the held-out row to ~23.4 dB
    with the *raw-area* feature, i.e. most of the gap that number described was
    two other bugs. What the UV span is now worth is **unmeasured against the
    current baseline**, and given that held-out PSNR moves +-0.4 dB on the seed
    alone, re-measuring it means a multi-seed run and not a single one. Beyond that, the editor could measure actual high-frequency energy
    per texture at bake time - a feature DLSS structurally cannot have, and the
    one place this design could beat it rather than imitate it.
  - **Unroll the temporal loop during label generation.** The oracle's labels
    stand in the previous low-res render upscaled, because the true history
    depends on the previous frame's weights. `--blss-eval` already closes the
    loop for the *reported* numbers; closing it for the *labels* means either
    iterating training to a fixed point or a short BPTT window.
  - **Depth of field, portals and split-screen are mutually exclusive with it,
    and NOTHING BUT THE UI ENFORCES THAT.** All three want real GS depth at
    display resolution, which since the z-buffer shrink is not merely unwritten
    but unallocated. Two corrections to what this entry and the docs used to
    say. **Codegen does not refuse the combination**: no path in
    `src/templates.cpp` gates DoF, portals or split-screen on `blssEnabled`, so
    "the generated game does not emit them together" - which appeared in
    docs/neural-upscaler.md, docs/blss-reconstruction.md and the engine skill -
    was always false; the *Neural upscaler (BLSS)* block of
    `drawPreferencesModal` is the entire interlock, and it now names the reason
    per feature. And two of the three fail for a second, independent reason:
    portals cancel the raster redirect (see the nesting entry above), and split
    frames are never bracketed at all - codegen wraps only the single-view
    branch, so a split frame renders full-resolution with scene depth writes
    still masked. DoF is the interesting one to actually fix: it would need an
    upscaled depth buffer, which the GS cannot produce in a blend pass, so it
    probably means keeping a low-res depth *colour* target and re-deriving z - a
    design, not a fix. Making codegen refuse (or auto-disable, loudly) instead of
    trusting a warning is the cheap half.
  - **Close the UV-clamp parity gap.** The UV register's fields are 14 bits
    unsigned, so the engine clamps grid-corner UVs to >= 0 and the host, which
    evaluates `u(x)` analytically per pixel, does not need to. Across the first
    tile column and row the two disagree by up to a quarter texel of UV
    gradient (~13 % of tiles, sub-texel). The fix is for the host to interpolate
    clamped CORNER UVs the way the rasteriser does - the same treatment the
    weight field already gets - rather than evaluating the closed form.
  - **No editor viewport preview** (the same position custom screen effects
    take). The viewport already renders at PS2 resolution and presents through a
    fragment shader, so a preview is reachable; it was left out rather than
    shipping a preview that quietly disagreed with the console.
- **AI Assistant follow-ups** (docs/ai-chat.md). The window, the tool table and
  the docs-as-skills prompt shipped; what was deliberately left out, roughly in
  the order it is worth adding:
  - **No build/run tool.** The assistant cannot start a Docker build or PCSX2,
    on purpose - an agentic loop that spends ten minutes and a container because
    the model felt like it is not a good default. The shape it wants is an
    explicit confirmation in the chat (a tool call the user has to press), which
    is a UI pattern the editor does not have yet.
  - **No terrain.** Sculpting and splat painting are heightmap-shaped, not
    chat-shaped; a tool would have to invent a language for them.
  - **No asset import and no file writes.** Importing means a file dialog and a
    dependency-closed copy (the Asset Browser's job); a tool that could write
    `res/` would also have to answer "what may it overwrite".
  - Project-wide data, the feedback loop and building all landed: `get_section`/
    `set_section` (the project's own per-section JSON, so every collection is
    covered by two tools rather than one pair each), `refresh_generated`, and
    `build_game` with the chat parking until the build settles. The running game
    followed: `game_state` / `game_log` / `graph_activity` read the devkit
    channels and `press_pad` drives the controller, both parking the turn - so
    the loop closes (place a thing, build it, drive the player into it, read what
    the graph did). Still out: the time machine (rewinding is a stranger verb for
    an assistant than reading), Live Link edits into a running game, and anything
    visual - it can read what the game SAYS, never what it looks like.
  - The step budget (8 rounds), the transcript budget (60 KB), the read_doc cap
    (48 KB), the 40-hit search cap and the 100-chats-per-project history cap are
    constants picked by eye, not measured against a long session.
  - Full-text search over the docs and saved chat history both landed with the
    window (`search_docs`, `--search-docs`, the History popup). What search still
    lacks is any notion of a synonym: it matches exact substrings, so a question
    in words the docs do not use ("lag" for frame time, "collision box" for
    `collisionMode`) falls through to its loose tier. A small hand-written alias
    table would be cheap and is probably worth more than anything cleverer.
- **Menu styling follow-ups.** The stylesheet, the layout engine, the Style tab
  and the runtime compositor shipped (docs/menu-styles.md;
  `.claude/plans/menu-styling.md` records the design). What was left out on
  purpose and is worth doing next: the HUD, loading screens and the credits roll
  still position themselves in the raw framebuffer instead of the logical
  512x448 space menus now scale from, so they are the remaining half of "the UI
  does not move when the display mode does" (the mechanism is there -
  `Sprite::drawSize` plus the `project::displayModes` table). Also: a `close`
  transition is parsed and generated but the runtime only plays `open` and
  `cursor`; `description { area: right }` is laid out but untested on a wide
  panel; and a per-menu "bake crisp for mode X" would remove the 1.2x upscale
  softness at 1080i for a second texture's worth of VRAM.
- **Save Editor: the checks only real hardware can make.** The feature is
  complete and builds, but three things cannot be proven from the host: the card
  failure feedback (a **full**, **absent** or **unformatted** card), and the icon
  itself — the two-line title break and the animated icon's motion only show in
  the **PS2 BIOS browser**. `docs/save-editor.md` says the same at the bottom.
- **VU authoring: the user-facing half of the VU framework.** Model + codegen
  for a program someone composes from stages, per-mesh parameters, the VU panel,
  and vu-lab rebuilt as an authoring demo. The whole enabling layer is done and
  verified on hardware; see `.claude/plans/vu-authoring.md` for the plan, the
  decisions already made and the traps.
- **VU0 kernels: real-hardware pass.** Done and verified in PCSX2: a project
  writes `src/vu0/*.cpp` against `vu::Kernel`, the container emits the
  microprogram and its EE driver, and `examples/vu-lab`'s `Ranges` kernel agrees
  with the EE's own `sqrtf` to 1e-6 units over 39 objects. What is still owed is
  the same run on a physical PS2 (PCSX2's COP2 timing is not the console's), and
  a measurement of what `run()` actually costs in EE time per batch size - the
  docs say "32 elements is microseconds" from reasoning, not from a stopwatch.
- **VU framework: bit-exact replay on REAL hardware.** `--vu-replay` reproduces
  a PCSX2 capture exactly (36/36 GS vertices) but not one taken over ps2link
  from a physical PS2 - there the closest candidate is off by far more than
  rounding, which points at the reconstruction (the per-mesh constants come
  from a chain the capture does not contain) rather than at the VU's
  arithmetic. Capturing the object-data chain is the prerequisite; until then
  the simulator is validated against the emulator, not the console.
- **VU framework: describe the `cull` family, then `clip`.** `cull` is `as_is`
  plus an MVP transform and the ADC clip check - the builder already has
  `transform`, and `clipw`/`fcand` are in the IR. `clip` is the hard one and
  probably stays partly artisanal: Sutherland-Hodgman has real control flow and
  scratch polygon buffers that an expression-level DSL will not express; the
  honest shape is a declarative skeleton with hand-written instruction blocks
  plugged into it.
- **Colour grading through the CLUT.** Every texture is already palettized
  (4-bit, 16 entries), so the GS is already doing a per-pixel colour lookup for
  free - re-map those entries through a grading curve and textured surfaces get
  true per-pixel grading at no runtime cost. That is strictly more than today's
  grading can do: `grading.hpp` compiles to the GS blender's gain/lift/mix, with
  no gamma and saturation only approximated, and the GS has no dependent texture
  read so a full-frame LUT is not available at all. Pairs with a
  luminance-indexed LUT on VU1 for the untextured half (the builder can already
  express the indexed load); the two together cover a scene without grading
  anything twice. See `.claude/plans/clut-grading.md` for the shape and the
  traps - the first unknown is whether the CLUT is cached in VRAM, which decides
  whether a run-time re-grade is cheap.

- **VU framework: per-project program specialization.** The editor knows at build
  time which program variants a project can use - a project with no matcap
  material does not need the two `tce` programs, which is ~400 instructions of
  micro-memory headroom for free and no swap. Needs the union of what a project
  *may* use (spawn-pool prefabs included) plus "generate everything" under Live
  Link, or an object spawned at run time finds no program to draw with.
- **Capture the object-data chain alongside the qbuffer chain.** `--vu-replay`
  currently carries the per-mesh constants (matrices, tags, fog) over from the
  memory snapshot, which is only sound while the snapshot belongs to the mesh
  being replayed. Capturing the chain that uploads them would make the
  reconstruction exact instead of merely usually-right, and it is the same step
  `docs/devkit.md` already names for pairing input to output.
- **Measure the cost of `ensureProgramSet` swaps.** The billboard program set is
  swapped in and out per bag with a full DMA drain on both sides, so a scene that
  interleaves billboard and ordinary bags pays for a swap at every transition.
  Nothing measures it today; sorting bags by program set would bound it to two
  swaps a frame.

- **Apache boilerplate headers on `src/*.cpp`** — the Apache License 2.0
  *recommends* (does not require) attaching its short header comment to each
  source file. TyraX is Apache-2.0 (`LICENSE`) but no source file carries the
  header, so a file copied out of this repo in isolation says nothing about its
  terms. It is a ~90-file mechanical sweep, deliberately left out of the
  licensing work so it would not bury that diff. Low urgency: the repo-level
  `LICENSE` + `NOTICE` are what actually establish the terms, and generated
  games are covered separately by `LICENSE-EXCEPTION.md` (which is a real grant,
  not a recommendation). Worth doing on a quiet day, alone, in one commit.

- **Finish opt-in dynamic lighting per object** (branch
  `claude/gi-dynamic-lighting-wip`; docs/global-illumination.md is the
  surrounding design). An object may opt into being lit by the LIT VU1 program
  with its four light colours re-read from the probe grid every frame - the
  deal animated models already take - so it relights with zero latency,
  including while it spins. Entry (132) settled the three suspects the WIP
  commit left, plus two more nobody had listed, and established that the
  reported banding was a misread of the screen: the dyn-lit cylinder shades
  smoothly. What is left before this can merge:
  - **The owner has not seen it in their own scene.** Everything so far is a
    scratch fixture of primitives plus a two-material model.
  - **One probe sample per OBJECT, taken at its origin.** That is the whole
    point (it moves, so it cannot be baked), but it means a large object is lit
    as if it stood at its own centre, and an object whose origin sits inside
    geometry reads that occlusion over its whole surface. Decide whether that
    is the documented deal or whether big objects want a second sample.
  - **The ANIMATED-model path still reconstructs along the sun** (entry 133 did
    the dyn-lit one). `updateAndRenderAnimObjects` evaluates L1 along
    `SCENE_LIGHT_*` and every model shares one `animLightDirs`, so a character
    in a bounce-lit interior leans the same wrong way this just fixed. Same
    shape of fix: per-model directions plus the dominant-L1 direction. Kept
    separate because it touches every animated model in every project.
  - **A textured dyn-lit part is untested.** `litScale` handles the 128 vs 255
    split by construction, but no fixture has exercised it.
  - Then: a README bullet, a docs/global-illumination.md section, and an
    example (or a dyn-lit prop dropped into `examples/gi-showcase`).

  **The two engine-level facts this work established** are worth keeping
  whatever happens to the feature - both are now in
  docs/global-illumination.md's trap list: the untextured/textured colour-space
  split for a lit bag, and `StaPipVU1Cull_D` never reading the colour bag at
  all (the albedo must be folded into the light colours).

- **Session internet exposure** — today sessions are LAN (or any mesh VPN:
  Tailscale/ZeroTier make remote peers look local, zero code). The researched
  built-in options, in preference order: a Cloudflare quick tunnel
  (`cloudflared`, free, no account, random URL per session = invite link;
  needs a WebSocket `wire::Transport` impl - client side via native WinHTTP,
  no OpenSSL), playit.gg (free TCP tunnels, account required), UPnP
  (miniupnpc, best-effort, dies on CGNAT). The `wire::Transport` interface is
  the only integration point - protocol/session code never sees sockets.

- **Log panels: search and jump-to-next-error** (docs/log-panels.md). The
  severity split landed the filter; the two obvious companions were left out of
  it deliberately. A **text filter** box (an `ImGuiTextFilter` over the same
  `visible` list) is the other half of "find the line I care about". And
  **jump to next/previous error** is nearly free now that `logview::Line::cont`
  marks which lines START an entry: walk `visible` for the next `!cont` line of
  a level and `SetScrollY` to it. Both are UI-only - the classifier needs
  nothing.

- Hands-on pass over the Flow Graph editor UX (needs a human with a mouse)
- Object physics vs objects (stacking), player physics polish (pad feel)
- Model picking uses the unit-box approximation (big models pick imprecisely -
  the parser now exposes the real AABB, the viewport pick could use it)
- HUD images draggable directly in the viewport
- Positional audio (volume falloff by distance to an object)
- **Game build speed, what is left after the incremental pass.** The build is
  incremental now (nothing-changed builds went ~75 s -> ~5 s; see the numbers
  in `tyra-testing`), so the remaining cost is the case that legitimately
  recompiles: an edit to `inc/scene_data.hpp` - which moving one object
  produces - invalidates most translation units, ~47 s on
  `examples/showcase`. Two candidates, in order of payoff:
  - **Get the scene DATA out of the header.** The generated tables live in
    `scene_data.hpp`, so a position change is a header change. Emitting them
    into one `.cpp` with `extern` declarations behind a header that only moves
    when the SHAPE changes would turn "moved an object" into a one-file
    recompile. The catch is every `constexpr`/array-size use of those tables;
    worth an audit before committing to it.
  - **Precompiled header for `<tyra>`.** Measured 3 s -> 1 s and 2 s -> 1 s on
    two TUs, at 95 MB of `.gch` per flag set - re-measure under `-j`, where
    six readers of a 95 MB file may cost more than they save.
  - `ccache` in the build image is the cheap third option (it would cover
    branch switches and revert-and-rebuild), but it needs a Dockerfile again -
    the per-project image was deliberately removed.
- Compressed music streaming (SPU2-native ADPCM/VAG, ~3.5:1 vs 16-bit PCM) -
  needs a custom double-buffered SPU RAM streamer in the engine; audsrv only
  streams PCM and plays ADPCM one-shots
- Flow graph: more nodes (timers with reset, variables)
- Animations: measure VU0 skinning (entry 34) on real hardware once ps2link
  is installed - PCSX2 prices COP2 ops like FPU ops so it can only show
  parity; the SIMD win (one FMAC = 4 lanes) is a hardware-only effect.
- Animations stage 3 (optional) - VU1 skinning microprogram: matrix palette
  in VU memory, weights in the vertex stream, new VCL program (respect the
  vcl_sml.i history first). Measure EE headroom before starting - skinning
  now runs on VU0 in macro mode (entry 34) at ~37% EE load for 3 characters
  in PCSX2, so the EE is not the bottleneck yet. Palette per batch limited
  by VU memory (~24-32 bones).
- Engine perf, next target: the packager's per-frame package arrays are
  pooled now (entry 79 - measured worth only ~2%) and the per-package bbox
  classification is now the cheap object-space AABB test (entry 100 - 47->73
  FPS precise / 120 FPS vu1-clipping on the 98k PCSX2 benchmark, so the
  companion work below is DONE in PCSX2 terms); the real endgame is the
  engine author's own TODO in
  stapip_clipper.hpp - move clipping to VU1 entirely ("too much time").
  **Measured on real PS2 (2026-07-11**; clipbench: 128x128 terrain at detail
  128 = ~98k verts, spinning FPP camera, COP0 timers around `clipper.clip` +
  loop/pre-endFrame markers, 4 runs x ~2400 frames): the EE clipper costs
  **8.6-9.8 ms per frame (max 11.6 ms)** in "precise" mode - ~28% of the
  frame - at ~1.3 us per crossing-package vertex; ~52% of surviving packages
  crossed the frustum. The frame is **100% EE-bound**: with vsync off, EE
  work is ~33 ms and endFrame is 0.5 ms (GS/VU1 idle). But the same scene in
  "fast" mode still misses 50 FPS (EE ~26.5 ms, and it submits 841 vs 356
  packages because only the precise path classifies per package), so moving
  clipping to VU1 *alone* gets this scene to ~24.4 ms EE - still 25 FPS
  vsync-locked (~41 FPS with vsync off, up from ~30). Real payoffs: ~9-11 ms
  of EE headroom, scenes hovering just over the 20 ms budget flip 25->50,
  the sky dome / animated objects / particles (hardcoded fullClipChecks=true)
  come along for free, and "fast" mode's vanishing-triangle artifacts stop
  being the price of speed. Worth doing **together with packager pooling**
  to actually reach <20 ms on heavy scenes. Full design + milestones:
  `docs/vu1-clipping-plan.md`; PCSX2 undercounts the clip cost ~15-20%, so
  measure on hardware. Reusable instrumented scene:
  %TEMP%\tyra-editor-test\clipbench (terrain_game.cpp owns a perfTick() +
  auto-spin patch, codegen marker removed).

- **DONE (2026-07-31): ps2link can be debugged in PCSX2, without the console.**
  A second, portable copy of the emulator with **DEV9 bridged** onto the LAN
  boots `ps2link.elf` and answers the real `ps2client` — `reset` and `execee`
  included — so a wedged console is a killed process instead of a walk to the
  hardware. Validated against a case with a known answer: the same
  Run -> Stop -> Run cycle shows **r4 falling through to the BIOS browser and
  off the LAN, where r6 comes back and runs the payload again**. Recipe, the
  four gotchas and the limits are in `docs/ps2link-setup.md`
  ("Testing a change without a console"); the hard limit is that **PCSX2 cannot
  produce an EE exception** — main RAM starts at address 0, so the NULL
  dereference behind the r4 `BadAddr 8` crash is an ordinary load there.

  Follow-ups worth doing, none of them blocking: **script the rig** (a
  `tools/ps2link/lab.ps1` + `lab.sh` pair that copies the emulator, patches
  `[DEV9/Eth]`, stages the ELF and runs a Run -> Stop -> Run cycle — the manual
  steps are all in the doc), and consider letting the editor's *Real PS2* target
  point at the emulated console for a smoke test before touching the hardware.

- **DONE (2026-07-31): ps2link's low build boots from FMCB and survives Stop.**
  Both halves are now measured on the console. The low packed no-USB image boots
  from the FreeMcBoot menu, and flashed to the card and booted from that menu it
  took **12 consecutive Run -> Stop cycles**, the series ending on its own limit
  rather than on a failure - where before r6 it died on the **first** Stop every
  time. The high build gives the same 12/12. So the top-of-RAM address is no
  longer load-bearing: the shipping image sits at `0x00094000` and leaves the
  top of memory to the game, which was the point of the whole entry.

  The fix is r6, described under "What the console actually died of" below;
  the short version is that `pkoReset()` rebooted the IOP immediately before
  `ExecPS2()`, and ps2sdk's C runtime init runs in crt0 against that IOP before
  `main()` ever gets control.

  Still open, and now its own entry below: **keyboard and mouse on the low
  build.** Baking the USB HID stack in makes the image too big for the FMCB
  menu, so the shipping low build is the no-USB one.

  Everything below is the investigation, kept because most of it is a record of
  what was NOT the cause.

  The original question - why our low build black-screened from the FMCB menu
  while the owner's stock `PS2LINK.ELF` booted - **is answered**. Everything
  below was flashed to the same console and booted from that menu; uLaunchELF
  boots all of them.

  | build | ELF | decompressed image ends at | FMCB menu |
  |---|---|---|---|
  | owner's stock release, Apr 2024, packed | 89 364 B | `0x000dea20` | yes |
  | stock upstream, packed by us | 103 364 B | `0x000dea20` | **yes** |
  | ours r4 `--no-usb`, packed | 104 164 B | `0x000df3a0` | **yes** |
  | ours r4 full, packed | 118 740 B | `0x000eb5a0` | no |
  | ours r6 full, packed | 118 612 B | `0x000eb3a0` | no (not retested; see below) |
  | any of the above, **unpacked** | 233-285 KB | — | no |
  | ours r4 full, high, unpacked | 285 492 B | — | yes |

  Two independent causes, which is why single-variable theories kept failing:

  1. **Unpacked never works from that menu.** A raw ELF's PT_LOAD sits at
     `0x00094000` and the loader writes it over its own resident code. Upstream
     ships `ps2-packer`'d releases (entry `0x01d0001c`, one segment high in
     memory) that decompress down to `0x00094000` after the loader is done -
     which is what `make` gives us and `make ee` does not.
  2. **Packed still fails if the decompressed image reaches too far.**
     `0x000df3a0` boots, `0x000eb5a0` does not, so something FMCB keeps
     resident sits between them and our USB HID stack's ~50 KB is what pushes
     us over it.

  The new problem: **the packed low build dies on the r4 reset.** Stop kills
  the game and the console then answers nothing, not even ping - a full EE
  death, where the same reset on the high unpacked build survives four
  Run -> Stop cycles untouched. Chased as far as this:

  - a reset with **no game running** survives;
  - one real bug was found and fixed on the way - `pkoReset()` called
    `init_scr()`, i.e. drove the GS, *before* `ResetEE` quiesced the DMA the
    dying game still had in flight. That is worth having regardless, and it
    moved the low build from dying on the first Stop to dying on the second;
  - so something else remains, it is address-dependent (identical code at
    `0x00094000` dies, at `0x01ee8000` does not), and it needs a game to have
    run. That smells like memory below `0x00100000` being written by something
    - the region between the image end and the game's load address holds the
      SIF command buffer at `0x000ff800`;
  - EE `printf` breadcrumbs do **not** survive to the PC: the IOP reboot
    follows within a millisecond and takes the queued tty with it. The screen
    is the only channel past that point - and it is black there, because
    `ExecPS2` blanks it before the code that would print anything;
  - adding those breadcrumbs found a *different* bug, worth knowing about:
    **no stdio may be called in the reset path.** `printf` takes newlib's
    stdout lock, and after `ExecPS2` re-enters the image that lock is a
    cleared `.bss` field until newlib re-initializes, so the console dies in
    `__retarget_lock_acquire_recursive` with `BadAddr 8` - a cycle or two
    later, looking unrelated. That one hit the *high* build (four cycles ->
    two) and is fixed; `scr_printf` is fine, `printf` is not;
  - removing the stdio did **not** help the low build: it still dies on the
    first Stop after a game. The console then answers nothing at all, not even
    ping, which places the death between the IOP reboot and `loadModules()` -
    the fresh image never gets the network back up;
  - the two deaths are **not the same bug**. The high build dies after two to
    four cycles with the network still up (IOP alive, EE gone) - that is the
    newlib-lock one. The low build dies on the first cycle with everything
    gone. Fixing the first does nothing for the second.

  Two attempted fixes made things **worse** and were reverted; both are worth
  not re-trying:

  - **moving `init_scr()` after `ResetEE`** in `pkoReset` (reasoning: do not
    drive the GS while the dying game's DMA is in flight). Sound, and the high
    build went from four clean cycles to two. Unmeasured reasoning lost to a
    measurement, so the original order stands.
  - **forcing newlib's locks in early `main()`** with `free(malloc(1))` +
    `fflush(stdout)`, to beat the interrupt-driven printf to them. That made
    the *high* build die on the first cycle, with the low build's signature -
    stdio init talks to the IOP over the SIF, and at that point in the restart
    there is no RPC yet. Exactly the mistake the fix was aimed at.

  **Correction, and it invalidates the framing above.** The "four clean
  Run -> Stop cycles" this was all measured against was the **low, unpacked**
  build **started over the network** (`execee` from a card-booted ps2link) - it
  was quoted afterwards as a property of the high build, which it never was.
  Re-measured from a card boot, with the console power-cycled first:

  | build | started from | cycles before the console needed a power cycle |
  |---|---|---|
  | low, unpacked | network `execee` | 4 (stopped there, not a failure) |
  | low, packed | network `execee` | 1 |
  | high, packed | memory card | 2 |
  | high, unpacked | memory card | 1 |

  So the variable that best explains the data may be **how ps2link was
  started**, not its address and not packing - which is the opposite of the
  story the entry above tells. Nothing here is clean enough to conclude from:
  every row is one or two samples of an intermittent failure.

  What that means for the user, stated plainly: **on a card boot, Stop still
  leaves the console needing a power cycle.** The game does die - that part is
  fixed and measured - but ps2link does not reliably come back. The parts of
  this that ARE fixed and verified are the command reaching a busy console (IOP
  thread priorities), the editor no longer refusing to deploy, the pad no
  longer hanging the game, and why an unpacked low build black-screens from
  FreeMcBoot's menu.

  That comparison has since been run, with the same high unpacked image booted
  both ways (card, and `execee` over a low packed build on the card): **one
  cycle either way**. So the start method is not the variable either, and the
  night's 4/4 was luck rather than a property of anything.

  The one variable nobody controlled: **the game binary changed mid-session.**
  The 4/4 run used `drone.elf` as built the previous night; every run after
  11:48 used a rebuild that carries the engine's pad fix.

  **That lead is dead, and two others with it (2026-07-31, afternoon).**

  - The pad-fix theory needed the pre-fix engine to have *frozen* the game, so
    that the 4/4 run was really measuring "reset with no game running". It did
    not: the console has a pad connected, so the pre-fix engine ran fine. The
    binary is not the variable.
  - A game built before 2026-07-10 (commit `f358e595`, which added the ps2link
    deploy path and `IrxLoader::keepIopResident`) **cannot be used as a test
    vehicle at all**: it reboots the IOP on startup and takes ps2link's network
    modules with it. The symptom is a `host:` log that stops after ~9 lines and
    a console that stops answering ping *while the game keeps running on the
    TV*. That is not a console death; do not score it as one.
  - Measurement trap that voided two runs here: **a `ps2client` left over from a
    previous deploy keeps serving `host:`**. The next game boots but reads the
    *old* directory's files (`open host:livepad.bin` → `fd = -1` forever) and
    its chatter lands in the previous log. Kill every `ps2client` before each
    cycle and verify the new log actually grows.
  - **Ping is not a liveness check.** The console answers ping in the exact
    failure state being chased (IOP alive, EE gone). A cycle only counts as
    survived if `execee` produces `loadelf` output.

  **What the console actually died of, resolved (2026-07-31).** Read off the
  screen after a Stop that wedged it: `BadAddr 0x00000008`, `EPC 0x00098604`,
  `Cause 0x70008008` — ExcCode 2, a TLB miss on a load. Against the low build's
  `ps2link.map`, `0x98604` lands inside `__retarget_lock_acquire_recursive`,
  +0x14, whose prologue is `move s0,a0` / `lw v1,8(s0)`: the lock argument was
  NULL. That is the **newlib stdout lock** — cleared `.bss` after `ExecPS2`
  re-enters the image.

  This kills the entry's own claim that "the two deaths are not the same bug".
  The low build died of the same newlib-lock bug the high build did. The r4 fix
  ("no stdio in `pkoReset`") missed it because the remaining `printf()`s were
  not in `pkoReset` — a disassembly sweep of the whole image found exactly eight
  `printf`/`puts` call sites and **all eight are error paths**: `pkoDumpReg`,
  `pkoDumpMem` ×2, `pkoGSExec`, the two user-thread failures on the deploy path,
  and the two `initCmdRpc()` thread failures that run on every ps2link start.
  Error paths are precisely what a broken restart takes, which is why this
  looked address-dependent and intermittent for a whole session. r4 had even
  routed a `CreateThread` failure to `printf` deliberately (see
  `docs/ps2link-setup.md`), i.e. it wired the crash in.

  **r5 removes all of it** (`scr_printf` everywhere in `cmdHandler.c`; verified
  on the binary — zero `printf`/`puts` call sites remain, `puts`/`_puts_r` are
  no longer linked in). Note the LTO trap when reading the map: symbol
  boundaries do not match code layout, so a call site "inside `pkoReset`" was
  really `pkoDumpMem`'s. Resolve call sites by their **format string**, not by
  nearest-symbol-below.

  **r5 does not fix Stop.** Measured immediately after, high unpacked started by
  `execee`, `drone` fully running (1049 log lines, live-pad polling): the
  console died on cycle 1, no ping at all. So the stdio crash was a
  *consequence* — something was already failing and `printf` shot the messenger.

  What changed is the evidence: the death is now a **black screen with no
  exception**. The handler installs its own `init_scr()` and would have printed
  one, so nothing faults any more — the machine dies silently.

  **The re-executed image never reaches `main()`, measured.** Reasoning from
  the black screen alone does not establish that, and the first attempt to
  instrument it failed in a way worth recording: a breadcrumb written to the GS
  background colour register (`0x120000E0`) is invisible **twice over** — the
  background only shows where nothing is drawn, so a marker set while the debug
  screen is up sits behind its framebuffer, and one set after `ExecPS2` has
  blanked the display cannot show at all. That probe reported black no matter
  what happened; it proved nothing.

  The working probe turns the display back on first — `SetGsCrt` is a syscall,
  it touches no DMA and has nothing to wedge on — and only then sets the
  colour, with no framebuffer over it. Read the CRT mode from the ROM region
  byte at `0x1FC7FF54` (`'E'` = PAL, mode 3, else mode 2); a wrong mode fails to
  sync and looks exactly like the black screen being diagnosed. With that in
  place at the top of `main()`, three outcomes are distinguishable: text = the
  console came up, green = `main()` was entered but `init_scr()` did not
  finish, black = `main()` was never entered.

  Measured: **black**. And the other end is pinned too — a death before
  `ExecPS2` would have left `pkoReset`'s own lines ("program stopped",
  "rebooting the IOP...") on screen, and the screen is clear. So `ExecPS2` ran,
  blanked the display, and control never arrived at `main()`. The remaining
  window is `ExecPS2` → crt0 → the prologue of `main()`.

  The strongest suspect in that window, and it already has a witness in this
  entry: **ps2sdk's C runtime init runs before `main()` and talks to the IOP
  over the SIF** (`_libcglue_init`, `__fdman_init`, the pthread glue — ps2link
  already disables what it can via `DISABLE_PATCHED_FUNCTIONS()` and friends).
  `pkoReset` has just rebooted the IOP, which comes back with *no modules
  loaded*, so an RPC from crt0 has nothing to answer it. That is the same
  mechanism as the reverted `free(malloc(1))` + `fflush(stdout)` experiment
  above, which died in exactly this way for exactly this reason — it only
  looked like a separate mistake because it forced the calls early by hand
  instead of letting crt0 make them.

  Cycle counts on r5, high unpacked, `execee`-started, `drone` fully running
  each time: **1 cycle** without the probe, **2 cycles** with it (died on the
  3rd). Do not read that as the probe helping — the documented spread on this
  bug is 1–4 cycles, so both numbers are inside the noise. It is recorded only
  so the next session does not mistake a lucky run for a fix.

  **Fixed in r6: the IOP was rebooted twice per Stop, and the first one is the
  one that broke it.** `pkoReset()` rebooted the IOP and then called
  `ExecPS2()`, while the re-executed image's `main()` calls `restartIOP()` a few
  lines in and reboots it again. The second is the one that has to happen. The
  first strands crt0: ps2sdk's C runtime init (`_libcglue_init`,
  `__fdman_init`, the pthread glue) runs before `main()` and talks to the IOP
  over the SIF, so it comes up against an IOP that has just restarted with no
  modules on it and nobody to answer. That is exactly the window the probe
  pointed at, and it is the same mechanism as the reverted `free(malloc(1))`
  experiment above — which now reads as a correct diagnosis applied in the wrong
  place, not a separate mistake.

  Measured, `drone` fully running before every Stop and every "survived"
  confirmed by the **next deploy**, not by ping:

  | build | cycles |
  |---|---|
  | r4 | 1–2 |
  | r5 | 1 |
  | r6 high, with the probe still in | 8/8, stopped at the limit |
  | r6 high, `pkoReset()` fixed | 12/12, stopped at the limit |
  | r6 high, both reboot sites fixed | 12/12, stopped at the limit |
  | **r6 low packed no-USB, flashed, booted from the FMCB menu** | **12/12, stopped at the limit** |

  `restartIOP()` also gained its missing `SifIopSync()` edge — it waited only
  for the boot to *finish*, which falls straight through when BOOTEND is still
  set from the previous boot. Harmless while `pkoReset()` did a complete reboot
  of its own; it is now the only IOP reboot in the program.

  `pkoRestartImage()` had the same shape and r6 changes it the same way. It only
  runs on the initial boot, which is why a card boot always survived it. Booting
  the **high** build over a running low one comes up cleanly and gives the
  12-cycle result above.

  **Booting the low build over a running high one still does not work**, and r6
  does not fix it — retried with both reboot sites changed: the console answers
  ping afterwards and accepts no deploy, so the EE never came up. Consequence for
  the iteration recipe at the top of this entry: **the high build can be shot
  over a low one, but not the other way round.** That is a limit of the recipe,
  not of the build — the run that once looked like a low-build Stop failure had
  died in the boot instead. The low build was measured from a card boot, and
  gives 12/12 like the high one.

  Iterating does not need reflashing, but **only in one direction, and this
  paragraph used to have it backwards.** Keep the **low** build on the card and
  `execee` **high** candidates over the network: that works and is how the
  12-cycle series were taken. The reverse - a low candidate over a running high
  image - leaves the console answering ping and accepting no deploy, i.e. the EE
  never comes up, and r6 does not change that (retried with both reboot sites
  fixed). Measuring the low build therefore wants a card boot. A console killed
  either way needs a power cycle, one per attempt.

  Better still, most of this no longer needs the console at all: ps2link runs in
  a DEV9-bridged PCSX2 and answers the real `ps2client` (separate DONE entry
  above). Hardware is for what the emulator cannot do - EE exceptions, and the
  FreeMcBoot menu.

  The low+packed+no-USB build is the one to ship: it boots from every launcher
  and leaves the top of RAM alone. The USB HID stack would then need loading
  from the card at runtime (`SifLoadModule` from next to `PS2LINK.ELF`) rather
  than baked into the image - which is also how it stops costing 50 KB of that
  window.

  **Does r6 make low+USB fit? No, and it was never going to.** That build was
  never blocked by the reset path - it is blocked by FreeMcBoot's loader, and
  the USB HID stack's ~50 KB is what pushes the image past whatever FMCB keeps
  resident. Measured on r6 rather than assumed: `_end` is `0x000eb3a0`, i.e.
  **512 bytes** below the r4 image that did not boot, against a last-known-good
  `0x000df3a0` some 49 KB lower. Not retested on hardware, because a 512-byte
  move across a 49 KB gap is not a hypothesis. uLaunchELF still boots it, so
  low+USB is usable from there today (`ps2link-low-packed.elf`); the FMCB menu
  wants the runtime-`SifLoadModule` route above.

- **ps2link: load the USB HID stack from the card instead of baking it in.**
  Would give one image that boots from **every** launcher (FMCB menu included)
  *and* has keyboard + mouse - today you pick one or the other, because the
  three `.irx` are ~50 KB of the EE image and that is exactly what pushes the
  low build past FreeMcBoot's loader (numbers in the entry above). Moving them
  out of the image costs IOP memory, which is not the scarce thing here.

  **What changes.** `loadModules()` in `ee/ps2link.c` currently does
  `SifExecModuleBuffer(usbd_irx, ...)` on three `bin2c`'d buffers declared in
  `ee/irx_variables.h`; `ee/Makefile` adds them to `IRX_FILES` unless `NOUSB=1`.
  Replace that block with `SifLoadModule()` calls against files sitting next to
  `PS2LINK.ELF` on the card, and the `NOUSB` switch disappears - there is only
  one build again.

  **The four things that make this less trivial than it reads:**

  - **Where the card is.** `ps2link.c` already keeps `argv[0]` in `elfName`, so
    the boot path is known (`mc0:/BOOT/PS2LINK.ELF` → `mc0:/BOOT/`). But a
    network-booted image has `host:ps2link.elf` there, and `pkoReset()` re-execs
    with the same `elfName` - so derive the directory, and when it is not a
    `mc?:` path, skip the load rather than guessing.
  - **Nothing can read the card at that point.** `rom0:SIO2MAN`, `rom0:MCMAN`
    and `rom0:MCSERV` are loaded only in `loadModules()`'s `if_conf_len == 0`
    branch, which `return`s immediately after - the normal path never loads
    them. They have to come first on the path that then reads `mc0:`.
  - **A missing module may not be fatal.** `pkoLoadModule()` calls
    `SleepThread()` when `SifLoadModule` fails, which is right for the network
    stack and wrong here: a console with no keyboard must still boot and still
    accept deploys. Needs its own non-fatal wrapper.
  - **No stdio on the failure path** - see r5 above. `scr_printf` only.

  **Setup and packaging.** The three `.irx` come from the ps2dev image
  (`/usr/local/ps2dev/ps2sdk/iop/irx/{usbd,ps2kbd,ps2mouse}.irx`); `build.sh` /
  `build.ps1` should drop them next to the built ELF so "copy these four files
  to the card" is one instruction, and `docs/ps2link-setup.md` needs that step.
  Both build scripts are edited as a pair.

  **How far this can be verified, and where it stops.** Image size and the FMCB
  boot are checkable: build, read `_end` out of `ee/ps2link.map` (want it back
  around `0x000df3a0`, not `0x000eb3a0`), flash, boot from the FMCB menu. That
  the modules load is visible in the boot log. **Actual key and mouse input is
  NOT verifiable on this console** - its USB keyboard and mouse are invisible to
  it, uLaunchELF included, so the feature has only ever been confirmed in PCSX2.
  Do not read "no keystrokes on hardware" as a bug in this change and do not go
  debugging it in software; that ground is already burned.

