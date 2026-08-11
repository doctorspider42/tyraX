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

- **OWED: a hardware pass on the frame-pacing / neural-upscaler MERGE.** The
  two features were merged onto one branch and verified as far as this machine
  goes - clean editor build, `--vu-check`, idempotent `--refresh-gen`, the
  `--blss-train` anchor unmoved at `e069f286ea0c524999bfd9dac769608c`, the
  shipped default net still loading, four Docker builds (BLSS on / pacing on /
  both on / both off) and PCSX2 boots. **The console at 192.168.100.150 was
  unreachable for the whole session** (`Destination host unreachable` - not even
  ICMP; it wants a power cycle nobody has done), so *nothing* below has been
  seen on real hardware, and an emulator number is not a substitute for any of
  it:
  - the INTC vblank handler and the `DISPFB` latch (the pacing feature's whole
    mechanism, and exactly what an emulator is friendlier about than a console);
  - the frame warp's GIF packet on a real GS - PCSX2 is admissible for EE
    aggregate and counts, never for GS fill (under-reported 76x) nor for
    per-function attribution;
  - **the third display buffer's headroom arithmetic**, which the merge changed:
    it now subtracts the BLSS low-res target, which the engine's reserve never
    named. Both the engine check and its host twin `project::tripleBufferingFit`
    moved, and only the host half has been exercised;
  - **the BLSS-history interlock's second half** - `composite()` dropping the
    temporal pass when the history buffer is the render target
    (docs/frame-extrapolation.md). Reasoned from the buffer arithmetic and
    exercised as a build, never seen firing: the shipped default net asks for no
    temporal weight, so the pass it drops is one that was not running anyway on
    every project measured. **RETIRED as a task, 2026-08-11**: the combination
    it guards is now refused by `blssClashes()`, so no build can reach it. The
    guard stays in the engine for the next feature that presents without
    rendering, and the three-buffer rotation it was reasoned about was LOGGED
    frame by frame in the process - the history is always the previous RENDERED
    frame, intact.
- **The upscaler x frame extrapolation interlock is a REFUSAL, not a fix, and
  what would lift it is named** (docs/frame-extrapolation.md, "Why not with the
  upscaler"). Both features reproject the previous frame, extrapolation halves
  the world rate, and the doubled camera delta tears the picture in motion. The
  shape of a real fix is a temporal pass gated on its own **reprojection
  magnitude** rather than on the network's weight alone - i.e. distrust history
  that has moved more than some fraction of a tile. That is a **twin-contract**
  change: `src/blss.cpp`'s oracle models the same blend and the shipped nets
  were fitted against it, so it is a retraining question and not a clamp. Two
  measurements would need re-taking with it: the fold tables in
  docs/neural-upscaler.md and the hardware A/B.
- **The motion-only blind spot in every gate on this branch.** The stability
  gate and the byte-identity harnesses all freeze the camera AND the emitters,
  deliberately, because that is what made them reproducible - and a fault that
  exists only under motion is therefore invisible to all of them. This one was
  found by a user walking around. Worth a paired **moving** fixture: a
  frame-indexed camera script (docs/profiling.md's rig already prescribes one
  for timing, for the same reason `--pad` is unusable as a repeatable drive) so
  two builds can be compared at identical vantages while the camera moves.
- **OWED before this branch's PR: a merge of `origin/main`.** The branch was
  0 behind when the PR #212 merge started and main landed two commits during
  it - #211 (terrain wrap mode, which moves `renderer_core_gs.hpp`, the same
  header the pacing work touches) and #213 (audsrv). `git merge-tree` predicts
  conflicts in **`src/version.hpp`** and **`renderer_core_gs.hpp`**. Neither was
  taken here, deliberately: the task was the #212 merge and a second unverified
  merge at handover is worse than a named one. The version.hpp resolution is the
  same shape as the one recorded there - one number per landing, the branch that
  arrives second renumbers - and main gets to say which is which.
- **The checked-in example projects were NOT regenerated for this merge**, and
  deliberately. `--refresh-gen` is idempotent on them (checked on upscaler-lab,
  showcase, script-demo, portals) but their committed `inc/font_data.gen.hpp`
  differs by machine - the font atlas resolves to whatever TTF the box has - so
  regenerating here would commit one machine's glyph metrics along with the four
  new `FRAME_EXTRAPOLATION*` constants. Do it on a box whose fonts match the
  ones the examples were last baked with, or decide the metrics are not worth
  tracking and say so.

- **Frame pacing follow-ups** (docs/frame-pacing.md, triple buffering landed
  with the measurements in that page):
  - **Triple buffering at 512x448, paid for by BLSS.** The neural upscaler
    sizes the z buffer from the RASTER rather than the display buffer, which at
    2x2 gives back 172 032 words - comfortably more than the 229 376 a third
    display buffer costs. So the combination the VRAM guard refuses today
    (full-height interlaced + triple) should fit with BLSS on. Unmeasured. The
    "it is already accounted for by construction" note that used to be here was
    HALF RIGHT and the other half was a real bug: `getHeapWords()` is indeed
    read after z is placed, but the BLSS **low-res colour target** is allocated
    later still (configure -> the VRAM rebuild -> allocate) and the 384 KB
    reserve never named it, so the guard was offering the third buffer against
    space the upscaler was about to take - 114 688 words at 512x448 1x2, i.e. a
    128 KB texture heap instead of 576 KB. Both the engine check and
    `project::tripleBufferingFit` subtract it now, and the host twin asks
    `blssUse` rather than the project default (a MIXED project pins z at the
    full raster and gets none of the saving). What is still owed is the
    measurement, on hardware.
  - **A hardware pass.** Everything in that page is PCSX2. The INTC vblank
    handler and the `DISPFB` latch are exactly what an emulator is friendlier
    about than a console.
  - **Tell the game when the buffer count changed.** `setDisplayOutput` re-runs
    the VRAM layout, so switching to a mode with no room silently drops to two
    buffers - correct, but a menu that offers the mode cannot say so.
  - **DONE: frame extrapolation** (docs/frame-extrapolation.md) - the engine
    module and `RendererCore::presentWarpFrame` land here; measured 25 Hz world /
    50 Hz picture, plus the project switch that wires it into the generated
    loop (Preferences > Build). Still owed: a **frame-accurate way
    to verify a synthesised frame** (see the doc - the compositor screencast
    cannot isolate one of two images alternating at 50 Hz), redrawing dynamic
    objects and the HUD on top of a warped frame, and hardware.
  - **BLSS makes examples/showcase lose its terrain.** With `blssEnabled` on,
    the palettised ground renders as flat sky colour while untextured
    primitives (trees, the crate) draw correctly - the exact signature the
    branch's own z-mask comment describes (a zeroed CLUT zeroes its alpha, and
    ATEST NOTEQUAL/AREF 0 then discards every fragment of 4-bit palettised
    geometry). Reproduced with frame extrapolation OFF, so it is not the warp.
    Found while wiring the warp to BLSS' per-tile depth; not diagnosed further.
  - **Does the BLSS composite have the same TEXA/COLCLAMP bug?** The frame warp
    shipped with a state block that WROTE those two registers and then
    "restored" them to assumed GS reset values - which hue-shifted every scene
    with post fx in it, because nothing else in the engine writes them and there
    was no value to put back (docs/frame-extrapolation.md). The warp no longer
    touches them. `RendererCoreBlss::composite` still does, and it has to (it
    genuinely blends), so the open question is whether its restore VALUES are
    right. Untested: check BLSS on a project with bloom and grain.
  - **The guard band, and the DISPFB pan it would unlock.** Both the warp's edge
    smear and the "free" 2D reprojection (`graph_set_framebuffer`'s last two
    arguments are the DISPFB in-buffer offset; the engine passes `0, 0`
    everywhere, so a shifted re-present costs two register writes in the vblank
    handler) need a framebuffer WIDER than the display window. That means
    splitting the physical raster from the displayed one and widening the
    frustum to match - and `M4x4::perspective` takes the raster size as its
    scale, so the widened fov/aspect breaks the "frustum planes are independent
    of the raster scale" invariant BLSS' host/console parity rests on. Costed
    and deliberately deferred, not forgotten.

- **Neural upscaler (BLSS) follow-ups** (docs/neural-upscaler.md,
  docs/blss-reconstruction.md). The proof of concept shipped: half-res 3D
  render, sub-pixel `XYOFFSET` jitter, a 6-12-3 MLP trained on the host and
  baked into the game, and a 1..5-pass Gouraud composite whose blend fields are
  the network's output. What it deliberately does not do yet, roughly in the
  order it is worth doing:
  - **DONE: plain mode - the reduced raster with no network at all**
    (`ProjectSettings::blssNetwork`, format v12, docs/neural-upscaler.md
    "Plain mode"). The network is nearly the whole EE bill (`proxy` 2.34 +
    `reproj` 0.28 + `feat` 0.19 + `net` 0.78 of 4.60), and a net that asks
    for nothing still costs every millisecond of it. Plain deletes the bag
    feed, the reprojection, the feature grid, the MLP and 472 of the grid's
    476 vertices, keeps the raster, the VRAM saving and the build interlock,
    and takes break-even from **13.1 full-screen coverages to 2.6** at
    512x448. Byte-identical picture, measured: 0 differing pixels of 811 426
    over nine cross-pairings against a build whose net asks for nothing.
    **What is still owed: one console run.** The 0.52 ms bill is arithmetic
    over hardware terms (begin 0.41 + end 0.10 survive; the four deleted
    terms read exactly 0.000 in PCSX2, which is the half an emulator can
    settle) rather than a hardware measurement of the mode, because
    192.168.100.150 answered `reset` with 0 and nothing else for the whole
    round. docs/profiling.md, "Plain mode's EE bill".
  - **OWED: re-measure the hardware A/B on `examples/upscaler-lab`'s CC0
    geometry.** Its art assets were replaced on 2026-08-09 - the cottage and the
    animated spider had **unverified redistribution terms**, which is not a state
    the feature's flagship demo could stay in - with buildings kit-bashed from
    Kenney's Retro Urban Kit (CC0 1.0) and the `wobbler.glb` five other examples
    already ship. Two of the three things that could have moved were measured and
    did not: the **fill is intact** (`--blss-coverage` 72.63 -> 72.23, the
    emitters byte-identical at 6 x 32 haze billboards, `PART` 0.46 ms in every
    sample of both runs) and the **oracle ceiling went up** (+1.058 -> +1.108 dB,
    jitter off, 2x2; CV +0.39 dB, 0 of 6 folds below bilinear). The third did:
    the **EE is ~4 ms a frame cheaper** in PCSX2 - `SCENE` 11.53 -> 7.53 ms mean,
    16.82 -> 10.16 peak, and the old build's four 25-FPS frames are gone - almost
    all of it the animated model (2 x 1092 vertices -> 2 x 123). So both arms
    should land ~4 ms lower and the ratio above 1.63x, and **that sentence is
    arithmetic over an emulator measurement, not a result**. `52.95 -> 32.42 ms /
    1.63x` is now labelled everywhere as a measurement of the previous geometry.
    The console at 192.168.100.150 was unreachable for the whole session
    (`Destination host unreachable`, not even ICMP - it needs a physical power
    cycle), and PCSX2 is inadmissible for GS fill, which it under-reports by 76x.
    The speed model fitted from the old runs is **unaffected** and needs no
    re-fit: it prices fill, and the fill did not move.
  - **DECIDE: `resources/blss-default.net`'s corpus now includes a scene that no
    longer exists.** The shipped default net was fitted on seven example projects
    plus the bestiary, and `examples/upscaler-lab` is one of the seven - so its
    corpus changed the moment the geometry did. **The net has deliberately NOT
    been refitted here**, because refitting it moves the published
    leave-one-project-out row (+0.29 dB against a project's own +0.31) and every
    md5 that anchors it (`879146bdee7f3b183c05985012753649`), which is its own
    decision with its own measurement round. Nothing is broken in the meantime: a
    net is fitted on a distribution, not on a file list, and the swap moved the
    input distribution very little (`texDetail` mean 0.297 -> 0.312, still the
    channel most correlated with the temporal weight). Route it as its own piece
    of work, and refit + re-measure the fold table in one commit if you do.
  - **DONE - BLSS is a PER-SCENE setting** (docs/neural-upscaler.md, "Per
    scene"). `SceneOverrides::upscaler` carries `blssEnabled` + `blssNetwork`;
    the scale, the jitter and the reconstruction tuning stay project-wide,
    because one project ships one net and its provenance sidecar records the
    scale and the sampler it was fitted for. Format v13, additive AND
    inheriting, so no migration step and byte-identical regeneration for every
    project whose scenes resolve alike.

    **The build interlock is per scene now, and that is the bigger half.**
    `blssClashes()` refused the build for the whole PROJECT, so one linked
    portal in scene 7 disabled the upscaler in all ten scenes. It asks each
    scene that RESOLVES the upscaler on, and the `#error` names the scene and
    the local remedy.

    **The theory this was going to lean on is FALSE and was checked first**: a
    scene change does NOT re-lay VRAM. `loadScene()` frees and re-acquires
    textures one at a time, ref-counted, only for assets the incoming scene
    does not also need; it never calls `vram.reset()` or `evictAll()` and never
    moves a permanent buffer. Reconfiguring there would have been the per-frame
    problem in a quieter place.

    **So it does not reconfigure.** A project whose scenes disagree pins the z
    buffer at the full display raster once, at init
    (`RendererCoreGS::setZRasterScale` via `configure()`'s new `nativeScenes`
    argument); `RendererCoreBlss::setScene()` then flips two flags and rebuilds
    the projection. Measured in PCSX2 over ~1 200 scene switches: **zero
    evictions**, resident count and free VRAM constant, and 10.04 / 10.02 /
    9.99 scene loads per second for flip / uniform-on / off - a 0.5 ms spread
    on a ~100 ms load, not resolvable. A native scene inside a mixed build is
    **pixel-identical in the 3D picture** to the same scene in a BLSS-off build
    (96 of 307 200 pixels differ, all of them the debug HUD's frame counter).

    **The price, and it is per project rather than per frame**: a mixed project
    gives up the z-buffer saving. Measured at 512x512, free texture heap:
    native 0.375 MB, uniform BLSS 0.875 MB, mixed **0.125 MB** - i.e. 0.25 MB
    worse than native and 0.75 MB worse than uniform BLSS, which is the same
    trade the runtime-toggle entry below prices at 224/672 KB for a 512x448
    raster, except that only projects that actually mix pay it.

    Two things it does NOT do, both filed rather than forgotten: the `zBuffer.
    mask` invariant is untouched (the mask is still DERIVED from the
    allocation - `endScene()` now restores that derived value instead of a
    literal 1, which is what makes a native scene after an upscaled one
    correct), and **`--blss-train` still shoots every scene**, including scenes
    that will render natively, so a project with one upscaled scene out of ten
    fits its net mostly on frames it will never see. Skipping them needs a
    special case for "no scene uses it yet" (the normal way to train is before
    switching it on) and, more importantly, a measurement that it helps.
  - **Switching BLSS on and off at runtime - NOT BUILT, and the arithmetic is
    why.** Asked for as a comparison and debugging tool: flip the feature every
    N frames and an A/B has no scene or camera variance at all, which is what
    docs/profiling.md recommends and what this branch has never been able to
    do (every measurement it has published needed two builds and paired
    frames). It is a good idea and it is filed rather than dropped.

    **The obstacle.** `RendererCoreBlss::configure()` re-lays the whole
    permanent GS VRAM region and evicts every texture, because the z buffer's
    SIZE follows the raster and z is allocated third, long before a generated
    game's `init()` runs. That is safe at the top of `init()` and nowhere
    else, which is exactly what codegen says out loud.

    **The way through, and its price.** Allocate both layouts up front - a
    display-sized z AND the low-res target - so a switch touches no
    allocation. Checked against `allocateVramBuffers` + `RendererCoreGSVRam::
    getSize` (width to a multiple of 64, `width*height` words at 32bpp, page-
    aligned to 2048) at 512x448 output, 2x2:

    | layout | permanent | texture heap |
    |---|---|---|
    | native (2 frame + z 512x448) | 688 128 w = 2 688 KB | **1 408 KB** |
    | BLSS baked (z 256x224 + target) | 573 440 w = 2 240 KB | **1 856 KB** (+448) |
    | both layouts resident | 745 472 w = 2 912 KB | **1 184 KB** (-224) |

    The +448 KB row is not arithmetic on its own: the running game reports
    `VRAMSTAT ... freeMB=0.75708` with BLSS off and `1.19458` with it on,
    a difference of exactly **0.4375 MB = 448 KB**, and the two reconstruction
    modes report the identical figure. So the toggle would cost **672 KB of
    texture VRAM against a baked BLSS build and 224 KB against native** - it
    gives up the whole memory win and then some, out of a budget
    docs/gs-vram.md puts at ~1.33 MB. Correct framing if it is ever built: a
    project setting, **off by default**, documented with that cost.

    **Why it is filed and not built, beyond the memory.** (1) It requires
    relaxing the one invariant this feature has already paid catastrophically
    for - `zBuffer.mask` is DERIVED from the z allocation, and the round that
    set it by hand one statement before the rebuild that cleared it again
    stamped depth across the texture heap and deleted every 4-bit palettised
    texture in the game. A live per-frame mask is that invariant made
    conditional. (2) `blssClashes()` would still have to refuse depth of
    field, portals and split view, because the BLSS half of the toggle still
    has no display-resolution depth - so the projects most likely to want an
    A/B are exactly the ones it cannot serve. (3) `needsBufferRealloc()` keys
    on the raster scale, so every toggle would ask for the rebuild the design
    exists to avoid; it has to be decoupled from the allocation. **That last
    one is now DONE** - `RendererCoreGS::setZRasterScale` pins what the z
    buffer is sized for independently of the active raster scale, and the
    per-scene switch above rides on it. A per-FRAME toggle would still need the
    other three, and it would pay the both-layouts-resident row of the table
    above for every project rather than only the ones that mix. (4) The
    measurement it buys is now much cheaper without it: **plain mode makes the
    interesting comparison a build-time one**, and the paired-build method
    works - an object script that pins the camera and hides every emitter
    gives frames that are byte-identical between captures, which is a cleaner
    instrument than an in-run flip (no history buffer to invalidate, no
    settling frames to discard on each side of the switch).

    **What would have to be checked if it is built**, all of which assume
    BLSS-ness is fixed for a frame today: the post-fx bracket and
    `getRasterTarget()`, the HUD path's z masking, the interlaced-field Y bias
    (applied by `composite()` and deliberately not by `beginScene()`),
    `hasPrev` across a switch, and the projection rebuild
    (`core3D->setFov`). And it wants hardware: PCSX2's renderers mask GS
    raster-window bugs that a real console shows.
  - **DONE, and it answers "can one net ship for every project": YES, if the
    bestiary and real projects are in the SAME corpus.** Leave-one-PROJECT-out
    (`--cv-groups`, new) over seven projects chosen by oracle ceiling, 18
    fold-runs each, jitter off, 12 frames/shot. On `examples/upscaler-lab` - the
    only example with a ceiling big enough to discriminate (+0.72 dB; 12 of the
    32 examples are under +0.10 and cannot tell two nets apart at all) - a net
    that has NEVER seen it scores **+0.29 dB against its own net's +0.31**, fold
    sds 0.37 and 0.34. The two arms that fail: the **bestiary alone** is a
    lottery (-0.34 dB mean over seven projects, **-1.09 worst**), and **projects
    alone** degenerate - at deadzone 0 that net asks for 2.15 passes and scores
    -0.10 dB with 22/42 folds below bilinear, and only the shipped deadzone
    turns it into plain bilinear. `--standardise` re-run at six channels is
    still worse (-0.05 vs +0.03, and `material-lab` -0.59). Mechanism, shown
    with `--features`/`--probe`: `texDetail` is identically zero on five of the
    seven projects and it is the bestiary's most oracle-correlated channel, so
    the probe reports `band 0.0 %` - none of the training corpus lies inside the
    frame's own band. **Also retracted: the -0.40/+0.06/+0.77 row that set the
    "fit the project you ship" rule had its ceiling measured at jitter ON and
    its margins at jitter OFF** (+0.773 vs +0.345 on the same scene today); the
    sampler is announced in both directions now. Full account in
    docs/neural-upscaler.md, "Can one net ship for every project?".

    **DONE: the default net ships and it is the fallback.**
    `resources/blss-default.net` (+ its `.meta`), fitted on the seven screened
    projects AND the bestiary with `--all-shots --frames 660 --no-jitter`
    (12 frames/shot over 55 shots, 39 s on six cores, md5
    `879146bdee7f3b183c05985012753649`) and embedded into the editor by
    `cmake/embed_binary.cmake`. `templates.cpp` bakes the project's own
    `blss.net` if it has one, the shipped default if it does not, and the random
    initialisation only when the embedded asset cannot be read at all - so the
    window's "the game will be built with RANDOM weights" is no longer a state
    any project reaches. Both the generated header and the boot log name the net
    they got. Provenance is a `<net>.meta` SIDECAR written by `--blss-train`
    (topology, `kNetVersion`, tile, activation table, scale, jitter, sharpen,
    corpus, the exact command) rather than a longer file header, because the net
    file's bytes are a published reproducibility anchor - `e069f286…` still
    reproduces after the change. The loader refuses a topology/tile/version
    mismatch and warns on scale / jitter / activation table. Also re-run and
    closed: the 39-fold bestiary table at the shipped activations
    (+0.42 -> **+0.41 dB**, sd 0.34, 3 of 39 below bilinear, 1.79 passes, proxy
    count unchanged at 1 217).

    **STILL OWED, and it is one CI job**: rebuild `resources/blss-default.net`
    whenever the corpus, the topology or `blss::kNetVersion` moves. The check is
    `tyrax-editor --blss-emit --act-table 0 -o <tmp>` from a directory with no
    `blss.net` - it loads the embedded net, runs its `.meta` against the
    compiled-in constants and exits non-zero when this build cannot read it -
    plus a diff of the `.meta` against a re-run of the `command` line recorded
    inside it (the sidecar carries no timestamp precisely so that diff is
    meaningful). One window item is also owed and belongs to the BLSS panel, not
    here: a *Use the shipped default* / *Train on this project* choice with the
    default's provenance on screen. (The four stale strings in
    `drawBlssSettings` this used to list alongside it are **gone** - three of
    them were in the unconditional inline paragraphs the
    [two-layer split](neural-upscaler.md#two-layers-and-why-the-reduction-is-correct-now)
    deleted, and the fourth said the game would be built with RANDOM weights,
    which stopped being true of any project when the default net started
    shipping.)
  - **DONE (corpus half): the training-shot plan is honoured by the trainer.**
    `blssscene::loadProject` now gates the six automatic moves on
    `Project::blssShots`, appends the author's own vantages, and carries a
    per-shot frame count that `generate()` honours (explicit counts first, the
    rest share the remainder; over-budget is scaled with a printed line, and a
    starved shot is reported rather than dropped in silence).
    `--ignore-shot-plan` reproduces the old behaviour. **Compatibility is
    checked, not asserted**: a default plan writes nothing to the `.tyra` and
    `--blss-train examples/procedural --all-shots --frames 72 --no-jitter` still
    writes md5 `e069f286ea0c524999bfd9dac769608c`. Found on the way: a Cutscene
    take **displaces** an automatic move rather than adding to one, so
    `examples/upscaler-lab` has never had a `strafe` shot - the only move with
    real parallax. The per-scene line now prints
    `N shot(s) (a take, b authored, c automatic)` so that is visible, and a
    non-default plan lifts the 6-shot cap.
  - **DONE, and it reverses the feature's verdict: BLSS WINS 3.4x on a
    GS-bound scene, on real hardware.** `examples/upscaler-lab` measures
    **530 ms with BLSS off against 157 ms with it on** (d = +373 ms, 95 % CI
    [+369, +377], n = 262 paired frames) for **5.95 ms of EE**. The old verdict
    - "it saves nothing, the frames are EE-bound" - came from one low-fill
    fixture plus a **discriminator that does not work**: `drain ~ 0` does not
    mean EE-bound, because GS backpressure stalls the EE inside the submission.
    Break-even is ~13 full-screen coverages (BLSS keeps 25.9 % of the fill -
    `blssScale 0` is quarter-area, not half). Four bit-identical EE cuts landed
    with it, worth **1.96 ms on hardware** (7.92 -> 5.95), and the activation
    table another **1.14** (-> 5.02). See profiling.md.
  - **DONE - `examples/upscaler-lab` re-tuned against hardware.** It ran at
    **1.9 FPS** with BLSS off because it had been tuned against PCSX2, which
    under-reports GS fill by 76x. Now 6 banks x 32 billboards instead of
    12 x 256: **52.86 ms off / 32.98 ms on = 18.9 -> 30.3 FPS, 1.60x**,
    n = 1024 paired frames. It cannot go faster in the BLSS-on arm than ~40 FPS
    - the scene's non-haze floor is 24.8 ms with BLSS on - and thinning the haze
    further only shrinks the win.
  - **DONE, and the answer is no: BLSS CAN go below half resolution and should
    not, except on one axis for one reason.** `blssScale` offered 2x2 and 1x2
    because the HOST could not express anything else - `blss::Scale` was a
    two-member enum whose `scaleY()` returned a literal 2 - while the engine has
    always been generic (`setRasterScale(sx, sy)` takes any positive pair).
    `--scale WxH` sweeps it now. `examples/upscaler-lab`, `--cv --cv-seeds 5`,
    120 frames, 30 fold-runs per row, jitter off; the 2x2 row reproduces the
    published +0.33 / sd 0.34 / 2 of 30 / 1.65 passes exactly:

    | scale | margin over its own bilinear | sd | below bil | passes | absolute dB | VRAM back |
    |---|---|---|---|---|---|---|
    | 2x2 | +0.33 | 0.34 | 2/30 | 1.65 | **26.98** | 448 KB |
    | 4x2 | +0.37 | 0.31 | 0/30 | 1.70 | **26.00** | 672 KB |
    | 2x4 | +0.47 | 0.38 | 0/30 | 1.75 | **24.76** | 672 KB |
    | 4x4 | +0.45 | 0.34 | 0/30 | 1.76 | **24.35** | 784 KB |

    **The network does not degrade - the picture does.** The margin and the
    oracle's ceiling (+1.02 / +0.99 / +1.05 / +0.99) are flat across the sweep, so
    a per-tile kernel decision is worth the same at 4x as at 2x; but the absolute
    PSNR drops up to 2.6 dB, which is EIGHT TIMES the whole trained margin. Fill
    given back through the composite is real and negligible (+0.11 pass on
    upscaler-lab, +0.30 on procedural = 0.065-0.18 ms). Break-even moves only
    ~12.6 -> ~10.0 coverages because the 5.02 ms EE bill is an
    output-resolution quantity and does not move at all; **VRAM returned rises
    75 %**, which is the only real argument for any of this.

    **The "2x4 is the sweet spot, vertical detail is already compromised by
    interlace" hypothesis is REFUTED.** 4x2 and 2x4 are the same pixel count, the
    same fill and the same 672 KB, and on the fixture with headroom 4x2 is
    **1.25 dB better**. `2x4` should never ship. `4x2` is the one worth exposing,
    for a VRAM-bound project only; `4x4` only for a project whose textures do not
    fit at all. Engine side needs **nothing** - it is `blssScale` (a third value),
    templates.cpp's ternary, the window's combo, and a `blssJitter` interlock,
    because +-4/16 of a LOW-RES pixel stops being an output-pixel centre below
    2x2. Full account and the second fixture in docs/neural-upscaler.md
    ("Below half resolution, swept").
  - **DONE (host half): the PROXY BUDGET, the twin contract's fifth rule.**
    `--proxy-budget` caps a bag's proxy count at the tiles its whole box covers
    (`cap = clamp(tiles, 1, 32)`, `group = ceil(parts / cap)`, the existing
    consecutive merge) in `bagList()` - per FRAME, because the cap is
    camera-dependent. **It ships OFF, matching the engine's
    `TYRA_BLSS_PROXY_BUDGET = 0`**; the two move in one commit or not at all,
    because a host describing a frame with 122 proxies while the console uses 187
    is exactly the drift that had this net fitted to bounding spheres for eleven
    commits. **Measured, it costs nothing**: upscaler-lab 187.2 -> 121.8
    proxies/frame (-35 %) for +0.33 -> +0.34 dB at the same sd 0.34, the same
    2/30 below bilinear, the same 1.65 passes and the same occupancy;
    procedural 281.7 -> 219.5 (-22 %) for -0.04 -> -0.04. The only feature channel
    that moves is `coverage`, 0.693 -> 0.695, which is the same channel and the
    same direction the engine reported (0.631 -> 0.638). **Ready for the paired
    flip whenever the engine side wants it.**
  - **OWED - re-run that hardware A/B against the jitter-OFF build.** The example
    shipped `blssJitter: true` when the 52.86/32.98 pair was measured and ships
    `false` since 2026-08-09 (its net is retrained to match). The re-run was
    attempted the same day and did not complete: the console stopped answering
    `ps2client` and dropped off the LAN, which needs a power cycle at the
    machine. Nothing was adjusted in its place - the published figure is
    labelled as the jitter-on timing everywhere it appears. **Attempted a second
    time on 2026-08-09 and lost the console the same way** - it answered ping at
    the start of the session, booted the fixture once, and was then gone from the
    ARP table entirely. Two rig traps found on that attempt are written up in
    docs/profiling.md: `--build` deletes `bin/ps2link.run` (without it the game
    logs to `bin/log.txt` over `host:` INSIDE the measurement), and `taskkill`
    without `/F` will not close `ps2client`. The jitter changes
    *where* the half-res raster samples, not how much of it there is, and the
    retrained net asks for the same 1.76 passes, so the number is expected to
    hold; that is exactly the prediction the re-run is for. Rig: set
    `TYRA_FRAME_PROFILE 1` in
    `vendor/tyra/engine/inc/debug/frame_profile.hpp`, then the protocol in
    docs/profiling.md (fixture reusable at `%TEMP%\tyra-editor-test\ulabhw`).
  - **The activation table is still one decision away**, and it is a TWO-LINE
    commit that must move both twins at once: `TYRA_BLSS_ACT_TABLE` 0 -> 512 in
    `vendor/tyra/engine/src/renderer/core/blss/renderer_core_blss.cpp`, and
    `int actTable = 0;` -> `512` at `src/blss.cpp:1429`, then a `--blss-eval -i`
    parity run. Worth 2.11 ms in PCSX2 but only **~1.5 ms on hardware** (the
    emulator over-weights libm). The engine half was landed, hashed and DEAD
    until 2026-08-08 - `runNet` never called it; it does now.
  - **DONE, and the answer is a floor: the composite's LAST two terms are not
    worth optimising.** `reproj` (0.275) + `feat` (0.190) was the only part of
    the EE bill nobody had opened. Disassembled first, on the method that found
    `tanhf`/`expf` and `floorf`: **`sqrtf` is a bare `sqrt.s`** on the R5900 and
    `buildFeatures` has **zero `jal` and zero `div.s`** - the libm-round-trip
    seam is exhausted, and the three previous wins were all the same win. Two
    bit-identical changes landed anyway (fuse the tile-stat pass into
    `finishTileStats`, hoist `buildReproj`'s per-column/row screen ray) and are
    worth **+0.017 ms of a 4.60 ms bill** - a simplification, not a speed-up.
    They also **price a divide**: ~565 fewer `div.s` a frame is 0.007 ms, which
    is the number that disposes of every remaining arithmetic idea here. Bill
    **4.60 -> 4.58 ms**, break-even unchanged at 11.5 coverages. Full account and
    the fixture trap it exposed (`proxy` differs 0.051 ms between two runs of the
    IDENTICAL ELF; `BLSSGRID`'s proxy count is the guard) in docs/profiling.md,
    "The last terms in the composite". **What is left on the EE is `proxy`
    (2.34) and it is the twin-contract item below.**
  - **The next EE cut is twin-contract work, and the ENGINE HALF IS NOW WRITTEN
    AND SWITCHED OFF.** The bag-proxy feed is the largest remaining EE term on
    hardware (2.69 ms). The rule that makes it cheaper is the **proxy budget**:
    cap a bag's proxies at the number of grid TILES its whole box covers, since
    the grid resolves nothing finer than a tile - stated exactly in
    docs/blss-reconstruction.md section 2 and implemented behind
    `TYRA_BLSS_PROXY_BUDGET` (0 = today's fixed cap of 32). **What is owed is the
    host half**: `bagOf()` / `bagList()` in `src/blsscorpus.cpp` must apply the
    same cap - the same projection of the whole object AABB, the same tile
    arithmetic, the same `ceil(parts / cap)` - and then the switch flips on both
    sides in ONE commit. Measured with it on: 198 proxies -> 116, 262 projections
    -> 174, and the only feature channel that moves is `coverage` (0.631 ->
    0.638). A screen-area floor was considered and rejected: it can empty a
    distant bag entirely, which hands its tiles `coverage = 0`.
  - **DONE: `kPassMs` was per 512x512 and is now per PIXEL** (landed
    2026-08-09, both halves). `FrameProfile::gsFillProbe` sized its calibration
    sprite from the CURRENT FRAMEBUFFER and said nothing about it, and the
    fixture that measured it ran PAL 576i, so **0.5872 ms was per 262 144 px** -
    while a coverage out of `blss::measureCoverage` is per the PROJECT'S own
    raster (512x448 = 229 376 px on `examples/upscaler-lab`, 448x448 on a
    progressive one). The probe hands its raster back (`outW`/`outH`) and the
    generated `GSFILL` line carries `raster=WxH` and a mode-independent
    `perMpx=`, so the constant cannot be read against the wrong resolution
    again - and **both rasters were measured on one console back to back**:
    512x512 -> **0.5896**, 512x448 -> **0.5174**, `perMpx` agreeing to 0.3 %,
    i.e. the cost is pure per-pixel.
    The estimator now says so: `fill::kPassMsPerMpx` (2.2524, the mean of the
    two points, reproducing both to 0.2 %) plus `fill::passMs(rasterPx)`,
    `breakEven(rasterPx)` and `speedFrom(coverages, rasterPx)`;
    `CoverageReport` echoes back the `outW`/`outH` it counted at so the window
    and `--blss-coverage` cannot price the same scene differently. Break-even
    is **13.1** coverages at 512x448 and **11.4** at 512x512 - measured both
    ways through the CLI - and every page that quoted a bare 11.5 now names
    the raster (README.md, docs/neural-upscaler.md, docs/profiling.md,
    examples/upscaler-lab/README.md, the skills).
    **The residual is NOT explained by any of this, and that is the check that
    keeps it honest**: the over-read was measured with both instruments on the
    same fixture at the same resolution, so a per-raster correction moves both
    sides of that comparison and cancels - about **26 %** of over-read
    survives it. That remainder is still the modelled emitter term (a counted
    haze coverage costs 0.436 ms), i.e. the magnified 128-square puff being
    cheaper per pixel than the probe's 1:1 framebuffer blit, and only a
    console settles it. **The camera theory of that gap is dead** - measured
    under the fixture's own parked gameplay camera the counter reads **78.99**,
    above the 72.63 six-move mean, not the ~57.7 the theory predicted; the
    ratio to hardware is constant (1.35 / 1.26 / 1.27 / 1.36) at 6 / 4 / 2 / 0
    haze banks, i.e. proportional and not a modelling error in where the puffs
    are. Tables in docs/neural-upscaler.md, "The overdraw count is an INDEX".
  - **The proxy feed's own measurements are owed on hardware.** Everything in the
    round above was taken in PCSX2 (the console was off the LAN), which this repo
    admits for COUNTS and bit-identity and refuses for attribution. Owed: the
    pre/post `proxy=total/accum` A/B for the landed `floorf` cut, the same for
    the budget once its twin lands, and the restated break-even. Projection, not
    a measurement: ~4.25 ms of EE and ~11 coverages against today's measured 5.02
    and 13.
  - **DONE: `--blss-train <projectDir>` writes `blss.net` into the PROJECT.
    It used to write it into the current directory** - `CliOpts::netPath` and
    `outPath` both defaulted to the literal `"blss.net"` (`src/blss.cpp`) - and
    `--blss-eval <projectDir>` read it from there too, so the flow the generated
    header and the docs both prescribe ("`--blss-train <projectDir> --all-shots`
    then rebuild") trained a net the build never picked up unless you happened to
    have `cd`-ed into the project first; `blssBake` looks for
    `<projectDir>/blss.net`. Found the hard way on 2026-08-09: the net landed in
    the repo root, the rebuild's boot log still said "the editor's built-in
    default network" - telling the exact truth - and the run had to be redone.
    Fixed by resolving the default against the project when there is EXACTLY ONE
    project positional, on the read side as well as the write; an explicit
    `-i`/`-o` still wins, and the bestiary and the union corpus keep the
    cwd-relative default because a net fitted on several projects belongs to
    none of them. **The diagnostic half landed on 2026-08-09**: `--blss-train`
    did NOT print an absolute path - it printed `outPath` as given, and
    `lexically_normal()` only normalises - so from a foreign cwd it still
    reported `examples/showcase/blss.net`, which names a different file from
    every directory and is exactly the ambiguity that cost the hardware round.
    `blss::displayPath` (`weakly_canonical`, because the net does not exist yet
    when the message is formatted, falling back to `absolute` and then to the
    plain string, because a net that was fitted and written but could not be
    pretty-printed is still a fitted net) now spells the net, its `.meta`
    sidecar and both `--blss-emit` outputs absolutely.
    Verified four ways from a foreign cwd: the net lands in the project and not
    in the cwd, `--blss-eval` reports `net source=project` where it used to say
    `default`, the bestiary still writes `./blss.net`, `-o` still wins - and the
    bytes are untouched, `examples/procedural` at 72 frames `--no-jitter` still
    md5 `e069f286ea0c524999bfd9dac769608c`. Why it survived so long: the BLSS
    window runs its job with cwd = the project AND passes `-o`, so the two
    spellings named one file and only the shell could ever see the bug.
  - **The corpus renderer draws no emitters, so the QUALITY half of the feature
    is blind to the scenes the SPEED half exists for. The tools now SAY so
    (2026-08-09); drawing them is still owed and is not small.**
    `blsscorpus.cpp` models emitters only in the coverage counter
    (`billboardOf` / `emitterCentres` / `countEmitter`, the
    `--blss-coverage` path); `renderScene()` takes geometry and materials and
    has no emitter parameter at all.

    **How bad it is, measured 2026-08-09 on this tree** (jitter off, 2x2,
    shipped defaults). The scene that prints the confidently wrong sentence is
    `examples/showcase`, NOT `examples/upscaler-lab` - the earlier version of
    this entry named the wrong one, and both of its numbers for it were stale:

    | project | `--blss-eval` | `--blss-coverage` geom + emit | emitter share |
    |---|---|---|---|
    | `examples/showcase` | `headroom=+0.006` -> "WILL NOT BENEFIT" | 15.24 = 0.67 + **14.57** | **95.6 %** |
    | `examples/upscaler-lab` | `headroom=+1.108` at 1.39 passes | 72.23 = 0.96 + **71.27** | **98.7 %** |

    `upscaler-lab` reads a +1.11 dB ceiling today, so it no longer contradicts
    the speed verb; the retracted claims are its `+0.000` / "WILL NOT BENEFIT"
    and its "3 072 billboards" (it has **11 emitters and 568 billboards**).
    `showcase` is the live case: a near-zero ceiling measured on **4.4 %** of
    the frame's fill, quoted verbatim by the BLSS window.

    **Landed 2026-08-09, because a silently wrong verdict is worse than a
    missing one:** the WARNING (from d240ef7a) now reaches the ANSWER as well
    as the top of the run. A project with any enabled emitter and a ceiling
    under +0.10 dB prints **`NO VERDICT:`** with the count and a pointer to
    `--blss-coverage` instead of "THIS SCENE WILL NOT BENEFIT", and the
    machine-readable line carries **`emitters=N`** (appended - that line is
    parsed key=value with unknown keys ignored; the TABLES above it are read by
    column position and must never gain a column). Verified: `showcase` prints
    NO VERDICT, `procedural` is unchanged with `emitters=0`, and the anchor
    `--blss-train examples/procedural --all-shots --frames 72 --no-jitter` is
    still md5 `e069f286ea0c524999bfd9dac769608c` at `--threads 1` and at auto.
    It is a caveat, not a fix.

    **THE BLOCKER WAS THE ENGINE, AND HALF OF IT IS NOW GONE - BEHIND A SWITCH
    THAT SHIPS OFF (2026-08-09).** An emitter bag used to contribute **no
    BagProxy at all** on the console: `buildParticles` sets
    `frustumCulling = None` (templates.cpp), so `stapip_core.cpp` had no bbox,
    fell to `addBagSphere(modelTranslation, radius = 0)`, and `addBag` rejected
    it at `x1 <= x0`; the corpus agreed by accident, because `bagList()` only
    walked geometry.

    The **sixth rule of the twin contract** closes that
    (docs/blss-reconstruction.md section 2, measured up on the upscaler page):
    an emitter bag is described by ONE box - the AABB over the centres it is
    about to submit, grown per axis by `|R.axis|*(max|m00|+max|m10|) +
    |U.axis|*(max|m01|+max|m11|)` - and `--emitter-proxy` makes `bagList()`
    build the same box from the modelled pool `--blss-coverage` already uses.
    **`TYRA_BLSS_EMITTER_PROXY` and `--emitter-proxy` are both 0**, and the
    measurement is why:

    - it works - `upscaler-lab` goes 198 -> 207 proxies, 147 -> **224 of 224**
      covered tiles, and `texDetail` stops describing the crates and starts
      describing `puff.png` (0.466 -> 0.211);
    - the twin checker works too, which is the part that unblocks everything
      else here: a console `BLSSFEAT` line probed against the matching corpus
      arm reads 96.9 % support on `coverage` against 67.9 % for the
      deliberately mismatched arm, so the two halves can now be caught drifting;
    - **and `coverage` becomes a CONSTANT** - `1.000/1.000/1.000` in every tile
      - with `depthGrad` nearly so (spread 0.101), because one AABB over a haze
      bank hands every tile the whole bank's depth range. That is the sky-dome
      failure again, in the channel the rule was supposed to rescue;
    - for **+0.88 ms of EE** (BLSS 3.21 -> 4.09 ms in PCSX2; `net` and `reproj`
      grow too, because covering 224 tiles instead of 147 runs the MLP on all of
      them) and **break-even 13.1 -> ~15.3 coverages** at 512x448.

    **THE SPATIAL SPLIT WAS THE NAMED NEXT STEP AND IT IS NOW A MEASURED NO
    (2026-08-09). CLOSED - do not re-open it without reading this.** The entry
    used to say: bin an emitter's centres by their COORDINATES, take one AABB
    per bin, each box then carries a *local* depth range, and it is
    order-independent so unlike a split by pool slot it is still twinnable. All
    of that is TRUE. It was implemented on both twins, measured, and removed;
    the rule, the three design decisions and the full tables are in
    docs/blss-reconstruction.md section 2, "A seventh rule that was measured and
    rejected".

    The result, on the console at a vantage where the one-box arm reproduced
    this entry's own 207 proxies of 273 and 4.09 ms: **224 of 224 covered tiles
    before and after**, `coverage` and `depthGrad` still `1.000/1.000/1.000`,
    proxies 207 -> **241** of 310 projected, tile updates 2 636 -> **6 077**,
    BLSS EE 4.07 -> **5.25 ms**, break-even ~15.3 -> **~18.2**. On the host the
    share of tiles reading exactly 1.000 moves the WRONG way in both fixtures
    (`coverage` 96.9 -> 98.4 %, `depthGrad` 87.8 -> 99.0 % on `upscaler-lab`).

    Two reasons, and both generalise past this particular partition:
    - **A partition of a solid region is a TILING of that region, and a tiling
      has the same union.** `coverage` is decided by the union of the boxes, so
      no spatial split can shrink it; `depthGrad` is a max over every bag
      touching a tile, so a split along the view axis reunites the range and a
      split across it leaves each cell its whole range. The premise needed the
      pool to be CLUSTERED, and a Tyra emitter's pool never is - `updateParticles`
      spawns uniformly over the emitter's own XZ rect and integrates one
      velocity, and `emitterCentres` models the same box with a Halton pool.
    - **The flat channel is the FIXTURE, not the description.** Strip
      `upscaler-lab` to one small fire emitter and `coverage` reads 0.690 mean /
      67.8 % at 1.000 in all three arms, identical to flag-off. On the shipped
      fixture `--blss-coverage` counts 71.65 of 72.63 coverages as emitters, so
      "covered, in every tile" is simply true there.

    So the sixth rule's +0.88 ms is what an emitter proxy costs, and there is no
    cheaper description waiting to be found. Both switches stay at 0 and no fold
    table or shipped net changes. **If the emitter half is picked up again, the
    open item is the one below, not this one.**

    What has NOT changed: the corpus renderer still draws no particles. So an
    `--emitter-proxy` run predicts a frame whose ground truth has none, and its
    PSNR is the cost of the description (measured: -0.02 dB against a fold sd
    of 0.20-0.27, i.e. nothing) rather than the benefit. The rest of this entry
    is still owed.

    So **drawing particles with BOTH switches still at 0 would move every LABEL
    and not one INPUT.** All
    six channels would still describe the opaque geometry alone while the truth
    image became 96-99 % particles. On `showcase` the oracle would be re-fitted
    against a frame whose predictors describe 4.4 % of it - not a harder
    learning problem, an *unlearnable* one - and the net would fit whatever
    geometry feature happened to correlate. That is worse than not drawing them,
    and it is why the honest order was and remains: **engine describes emitter
    bags first, corpus draws them second.** The first half is written and
    measured now; it is not yet ON, so the constraint is unchanged in practice.

    Two things that ordering fixes, and ONE OF THEM NOW WORKS:
    - **the twin contract is checkable again** (2026-08-09). Its instrument is a
      console `BLSSFEAT` line probed against the corpus distribution
      (`--blss-eval --probe`), which compares FEATURE VECTORS - and with the two
      emitter switches on, emitters finally produce one. Confirmed against a
      deliberately MISMATCHED pairing, which loses a third of `coverage`'s
      support and half of `depthGrad`'s reachable band. Every rule below can now
      be transcribed and CHECKED rather than transcribed and hoped for;
    - **`--blss-coverage`'s emitter model could stop being modelled.**
      `billboardOf`/`emitterCentres` average the life curves and spread the pool
      through a box; a real simulation would replace them - at the cost of
      re-publishing every coverage number, so it is its own tail. Note they are
      now SHARED with the proxy rule (one definition, two consumers), so a
      change there moves the described boxes as well as the pixel estimate.

    **The sixteen twin rules a correct particle corpus owes**, all read out of
    `templates.cpp` `updateParticles` / the billboard `.vclpp` / the GS setup,
    so this is an inventory rather than a research task. Note (2) and (4): the
    earlier version of this entry prescribed **back-to-front sorting** and **"a
    BagProxy matching what `StaPipBillboardBag` submits"**, and BOTH are wrong -
    the console does not sort, and it submits nothing.
    1. blending: GS `(Cs - Cd) * As >> 7 + Cd`, in a rasteriser whose stated
       invariant is that it has none;
    2. **draw order = emitter order then pool-slot order, NOT depth.** The
       console iterates `particles` and submits each pool as one bag;
    3. z-test GEQUAL **with z-write** (`PipelineZTest_Standard`), so particles
       occlude each other in draw order;
    4. **the bag's proxy follows `--emitter-proxy`** - no proxy with the switch
       at 0 (the shipped state, and the exact console match), and the sixth
       rule's single box with it at 1. This is the rule the entry has now got
       backwards TWICE: it first prescribed "a BagProxy matching what
       `StaPipBillboardBag` submits", then "no proxy, and that is free"; the
       truth is that it is a switch and both halves have to read it;
    5. alpha test `!= 0` with `AFAIL = KEEP_ALL` (a zero-alpha particle writes
       nothing at all, not even z);
    6. MODULATE with TCC = RGBA, so `As = At * Af >> 7` for the textured
       emitters (`upscaler-lab`'s haze is `puff.png`, RGBA32 by a per-asset
       quality override) - and the product can exceed 128;
    7. per-quad **`clipw` cull against the +-2048 px raster window: any corner
       out drops the WHOLE quad**, which a 9-unit haze puff near the eye hits;
    8. corner = `C +- (R*m00 + U*m01) +- (R*m10 + U*m11)`, STs
       `(0,1)(1,1)(1,0)(0,0)`;
    9. the camera basis `right = normalise(fwd.z, 0, -fwd.x)`,
       `up = (-rz*fwd.y, rz*fwd.x - rx*fwd.z, rx*fwd.y)`; rain uses world-up;
    10. per-kind spawn, velocity, lifetime for six kinds;
    11. per-kind size and alpha curves (peak alpha: fire 90, smoke 40, fog
        `opacity*60`, sparks 110, rain 70, custom `opacity*128`) and fire's
        colour ramp;
    12. fog's per-particle swirl (rotation by index and age);
    13. rain's terrain fall distance and custom's die-on-ground, i.e. a
        `terrainHeightAt` twin (`heightAtWorld` in blssscene.cpp already is one);
    14. `emitFollow`, which makes the spawn box camera-relative;
    15. the LCG `prand` with seed `12345 + runtimeIndex * 7919`, its exact
        consumption order, and the staggered first respawn;
    16. **a fixed `dt` the console does not have.** `g_frameDt` is real elapsed
        time - on `upscaler-lab` that is 33 ms with BLSS on and 53 ms off, so
        the two arms of the feature's own A/B do not even simulate the same
        particle field. The corpus would have to invent one (1/50 s is the
        defensible choice, and `AnimMesh`'s per-console-frame pose table is the
        precedent).

    **The tail, and it is smaller than this entry used to claim.**
    `resources/blss-default.net` is fitted on `upscaler-lab material-lab
    endless-runner cube save-points procedural endless-scroller bestiary` - of
    those eight members exactly **one** (`upscaler-lab`) has any emitter, ~6 of
    55 shots. `showcase` is NOT a member. So the refit blast radius is ~11 % of
    the corpus, not "seven projects several of which have emitters". It still
    has to be one commit: refit the default, regenerate the `.meta`, re-run the
    published fold tables, or the CI check that reproduces the net from its
    recorded command fails, correctly.

    Until then: treat a PSNR for a particle-heavy project as NOT MEASURED, read
    the speed verdict instead, and pin the vantage (`blssShots`) before quoting
    anything at all.
  - **A FOG emitter loses its Opacity on save/load - authored 0.3 comes back
    0.6, and the fog is silently twice as dense.** Found while inventorying the
    emitter parameters above; NOT fixed here because `src/project.cpp` was
    outside this change's ownership.

    `project::save()` writes the emitter's `opacity` **only inside the
    `if (k == 5)` custom-physics block** (`src/project.cpp`, the `"emitter": {`
    writer), while the loader reads `opacity` for every kind and defaults it to
    **0.6**, and codegen emits `o.emitterOpacity` for every kind
    (`src/templates.cpp`, the `SCENE_OBJECTS` writer). Fog is the one non-custom
    kind that USES it - `alpha = d.emitOpacity * 60.0F` in `updateParticles` -
    so a fog emitter is the case where the round trip loses data. Kinds 0/1/3/4
    ignore the field, and kind 5 saves it, which is why this has survived.

    Symptom sequence: set a fog emitter's Opacity to 0.3, the viewport preview
    obeys it (`viewport.cpp` reads the same field), save, reopen - the value
    reads 0.6 and the built game gets 0.6. Fix is one line (write `opacity`
    outside the `k == 5` block) plus a thought about whether existing projects
    need a migration: they cannot be distinguished from ones authored at the
    default, so the honest answer is probably "no migration, note it in the
    format-version entry".

    It does NOT break the BLSS twin: the corpus and the generated game both read
    the same lossy `.tyra`, so they agree with each other and are equally wrong
    about what the author typed.
  - **The degenerate-net fast path is designed and unlanded.** When every output
    is deadzoned (measured: `point 0 % / temporal 0 % / sharpen 0 %` on a real
    project's own net) the composite is exactly one full-screen bilinear blit,
    and pass 0's 16-row grid could be one sprite - worth the 0.31-0.56 ms of
    `pkt`. It needs a guard for negative jitter (the grid clamps UV at 0, a
    sprite would interpolate through it) and a frame-buffer comparison against
    the strip, because sprite and triangle UV DDA are not obviously bit-equal.
  - **DONE: the corpus render is threaded, and DETERMINISM is what pays for it**
    (7d3dbf67). The oracle has been parallel per frame since 1b9c7a74; the corpus
    render was the last serial phase that could be parallel at all (7.2-7.7 s of
    a ~24 s default run). The one thing making it serial was `prevLow` carried
    between iterations - an order dependency - so it is re-rendered from its own
    camera and its own jitter phase, the same image by construction, for 1.5%
    more work. `--threads N` (0 = every core, clamped to 32) bounds both parallel
    phases and is a WALL-CLOCK knob only: on `examples/procedural`, 156 frames,
    400 epochs, `--all-shots`, `--threads 1`/`3`/`6`/auto and **the pre-change
    binary** all write md5 `6b2fba90d0f059f055134a55df478c8e`. That last row is
    the load-bearing one - every published fold table stays a measurement of this
    code. 6 cores, 1 -> auto: corpus 7.5 -> 1.9 s, oracle 54.6 -> 10.4 s, fit 6.2
    -> 6.2 s, total 68.4 -> 18.5 s; end to end against the previous commit,
    24.4/23.6 s -> 18.5/18.5 s.

    **New from this item: the bottleneck is the FIT.** At every core the oracle
    is 10.4 s (56%), the corpus 1.9 s (10%) and the sequential Adam SGD **6.2 s
    (34%)** - each step reads the weights the previous one wrote, so `--threads`
    cannot touch it. Not worth a GPU at 6 seconds; if the cycle needs to be
    faster, the oracle's coordinate descent is still more than half of it.
  - **DONE: the window answers "should I turn this on" in one click, and every
    long button says what it costs.** The window was built by its author and read
    like it: it told the user to "run the Evaluate tab before turning this on"
    and Evaluate could not run without a `blss.net` that only Train could
    produce. Above the tabs now: **`Will this scene benefit?`**, a NET-FREE
    `--blss-eval` whose oracle row is the scene's ceiling, rendered as *THIS
    SCENE WILL NOT BENEFIT* or *Headroom: +0.95 dB available at 1.22 passes* with
    a *Train the network* button under it. Plus: an **ETA** on Train / Evaluate /
    Cross-validate (`blssui::estimate`, calibrated against seven timed runs, all
    within 17%); the Train tab down to **Frames and Epochs** with the six
    research knobs behind an *Advanced* header, tooltips verbatim; clash warnings
    that **name the scene and object** and carry a `Select it` button, and that
    are shown whether the feature is on or off; a **live VRAM line** under the
    Render scale combo computed for this project's own raster - which is how
    "**1x2 hands back exactly nothing**" got written down for the first time
    (z shrinks by precisely what the low-res target costs, at every output size);
    a **difference view** (|A-B| x8) on the Compare tab with a three-thumbnail
    strip under the verdict; and the smaller ones - 123 weights instead of 500
    bytes, a retrain warning when the net's `.args` sidecar disagrees with the
    project's sharpen or scale, *Restore defaults* covering every field, and a
    warning when the four tabs' frame counts drift apart. Driven end to end with
    `--ui-script` on two fixtures and screenshotted; the arithmetic additionally
    checked from a host-only harness. Still unseen: the finished cross-validation
    table and the error banner.
  - **DONE: `--blss-eval` is 4.6x faster, `--cv` 1.23x, and all of it is
    BIT-EXACT.** That was the other half of the item above - the TRAIN cycle got
    threaded, the EVALUATE cycle did not, and a plain `--blss-eval` was ~80% one
    serial oracle. Three changes, `examples/showcase`, 156 frames, `--threads 8`,
    minimum of alternating repeats: 89.8 -> 19.5 s and 48.3 -> 39.3 s. (1)
    `evalRecurrent` runs in parallel over
    **shot runs** - the temporal chain only ever resets where the shot id changes
    - with the workers producing per-frame values and the sums folded in serially
    in corpus order, so no accumulator sees a different sequence of addends. (2)
    `--cv` computes its two bilinear rows **once per corpus**: an all-zero weight
    field never reads the history, so a frame's bilinear PSNR is a constant, and
    every fold used to re-composite eleven training shots as well as its own to
    rediscover it - that is the whole of the `--cv` row, 19% of the run. (3) the
    oracle stops sweeping `wD` at `--sharpen 0`, where all nine candidates
    quantise to `aD = 0` - ~16% of such an evaluation (16.1 -> 14.0 s over 26
    frames at `--threads 1`) and nothing at the default 0.5, which is why it does
    not show in the two numbers above. Proven by diff: the eval and fold
    tables are character-identical to the pre-change binary's, and `--threads 1`
    vs `8` vs auto still write md5 `24cb12467edb034df24a7e66b505b384` on that
    project, the same as before the change.

    **Also DONE, and it is a bug fix rather than an optimisation: `--blss-eval`
    runs NET-FREE.** The settings panel has told users to evaluate their project
    "before turning this on" since it shipped, and that was impossible - the verb
    loaded `blss.net` first and bailed with "cannot open blss.net", so a fresh
    project could not perform its own documented first step. The load-bearing row
    is the ORACLE, which involves no network at all. A missing DEFAULT net now
    just drops the trained row (exit 0); only an explicit `-i` that cannot be
    opened is still an error. `--blss-eval` also prints the verdict itself, in
    the window's own words, plus `[blss] verdict headroom=… passes=… bilinear=…
    oracle=… native=…` for a caller, and `--cv` prints `[blss] fold k of n` so a
    progress bar over the fold loop can be a real fraction. All three verbs now
    end with a `blss: timing` phase line.
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

    **DONE: the PORTAL bracket was the fourth copy and is converted**
    (332f3193). `RendererCorePostFx::portalMaskBegin/End` also run inside
    `renderScene()` and took FRAME from `gs->getCurrentFrameBuffer()` plus a
    display-sized SCISSOR and XYOFFSET - the same bug, a fourth time. Both read
    `getRasterTarget()` and restore through `emitRasterRestore()` now, so there
    is ONE implementation and this bracket finally carries the InterlacedField
    per-field XYOFFSET bias; Begin re-narrows the scissor to the portal's bbox
    after the restore, because the destination view must stay bounded. "Portals
    are incompatible with BLSS" is a depth argument only now. Verified in PCSX2
    on examples/portals with BLSS off, before vs after, same fixture and camera:
    the same picture past frame 4000, no assert.
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

    (bilinear: 23.26 dB, 23.41 flicker.) **That sweep was read off ONE held-out
    split and it overstated the price by an order of magnitude.** Re-measured
    under cross-validation (13 shots, 21 fold-runs per point, decay 1e-4,
    fill 16): flicker weight 0.02 costs **0.02 dB**, not 0.22 - and the per-fold
    flicker column is identical to two decimal places with and without it. So the
    term is not expensive; it simply **does not work**. **The form is wrong, not
    just the weight**: MSE against the reprojected history is minimised by the picture
    FREEZING, which is free on the near-static training shots and is ghosting on
    the held-out orbit and dolly - it cannot tell "stable because the jitter got
    fused" from "stable because nothing moved".

    **CLOSED 2026-08-09: the form was fixed as prescribed, swept, and it is
    ALSO a bad trade - so do not pick this up a fourth time.**
    `--flicker-form period2` charges for the stationary period-2 alternation the
    candidate weights would leave, derived in closed form, which freezing cannot
    pay for. Nine weights x 30 fold-runs on `examples/upscaler-lab` with the
    jitter ON: the alternation reaches the jitter-off floor only at weight 5,
    where the margin is **+0.29 against the +0.33 that turning the jitter off
    buys for free**, and where **15 folds of 30 lose to plain bilinear**
    (sd 0.51 -> 1.40). Mean passes never moves (1.73 -> 1.75), so it is not a fill
    trade - what it costs is generalisation. Below weight 1.5 the knob does
    nothing at all. Curve, metric and the two ways the metric had to be fixed
    before it could judge anything: docs/neural-upscaler.md ("The trade curve").

    **The prescribed gate was itself wrong, and that is the transferable part:**
    `(1 - motion) * (1 - depthGrad)` looks threshold-free and is exactly ZERO on
    most of the frame, because `motion` reads 1.0 on 49.1% of `upscaler-lab`'s
    tiles and `depthGrad` on 41.0%. A gate built out of saturated channels is a
    gate that is always shut. Outliers are clamped instead.

    **RETRACTED: "what delivered the stability instead was the fill term"** (it
    used to say so here, by culling the point and sharpen passes). The fill term
    culls the TEMPORAL pass too, and the accumulator is the only thing that damps
    the phase difference at all - so it makes the bob worse, not better. The
    flicker numbers it was read off (21.49 -> 21.01 training, 27.12 -> 26.62 held
    out) are a lag-1 metric, which cannot see a period-2 artefact. What delivered
    the stability is turning the jitter off.

    The earlier **"lock the jitter phase to the field parity" theory stays
    REFUTED**, and the reason is worth keeping: a blending deinterlacer was
    averaging adjacent frames and hiding the symptom, which is exactly why
    enabling it looked like a fix, and the bob survives into progressive 480p
    unchanged. The real cause is that jittered sampling produces a different
    frame every time - which is the POINT, it is where the extra information
    comes from - and the temporal accumulator was not fusing it. Nothing about
    interlacing or field parity is involved.

    **The "nobody has watched the emulator" half is ANSWERED**: a human was shown
    three builds and called the jitter-on one "like an earthquake" while the other
    two were byte-identical over 40 captures. The bob is real, the jitter is the
    cause, and turning it off is the cure.

    **The INSTRUMENT is DONE too (2026-08-09): `--still`.** It was the fixture and
    not the objective. Every frame of a shot now uses the shot's FIRST camera and
    FIRST pose, so only the jitter phase advances - the reprojection becomes the
    identity (the warp gate keeps **100 %** of the frame instead of 29.6 %) and
    the animation freezes with it. On the ANIMATED corpus the metric's floor goes
    **2.614 -> 0.095** levels while the artefact reads **4.434**, i.e. 47x the
    floor instead of below it, and `--still --no-anim` reproduces `--still` to
    within 0.004 - so **`--no-anim` is no longer a prerequisite for reading this
    column**. It also put numbers on three things that were only derived: the
    temporal pass alone takes the bob 4.434 -> 0.263 (17x, which is
    `(1-c)/(1+c)`, and is why a fill term that culls it makes the bob WORSE), the
    sharpen pass makes it worse than doing nothing (6.062) because it lands after
    the accumulator, and the shipped objective's oracle leaves 0.979 where an
    all-temporal composite leaves 0.263 - that gap IS what accuracy-plus-fill
    trades away. It is a FIXTURE, not a corpus: `--blss-train` and `--cv` refuse
    it, because every frame of a shot is the same frame.
  - **DONE: charge the oracle for the fill it asks for.** `--fill-weight`,
    now **16**, charged as a STEP on the quantised alpha byte (a weight rounding
    to alpha 1 costs a whole pass and buys nothing, so a smooth penalty would
    park there), mirroring the engine's own skip rule. `--blss-eval` gained the
    occupancy columns so the effect is visible in the tool rather than in one
    report. Measured: sharpen occupancy collapses to 14.9% training and 4.8%
    held out; mean full-screen passes 4.25 -> ~2.9 against a 5.00 worst case and
    1.00 for plain bilinear; quality unchanged in distribution.

    **The "sharp knee at 6" was an artefact of the single split it was read off,
    and there is no cliff.** Re-swept over 21 fold-runs per point: the margin
    over bilinear runs +0.36 / +0.53 / +0.55 / +0.50 / +0.43 dB at fill
    6 / 12 / 16 / 24 / 40, i.e. a **plateau from 12 to 24** across which the fill
    falls by most of a pass. 16 ships. The old recommendation, 6, is the worst
    point on the new sweep, and 12 - "a full decibel below plain bilinear" -
    is +0.53. Set the fill weight and the weight decay TOGETHER: at decay 1e-5
    the same corpus reads +0.36 at fill 6 and +0.33 at fill 12.

    **What this did NOT fix, and it is a live follow-up: point and temporal are
    still drawn over most of the screen.** Measured on the shipped net, 80%/81%
    of grid cells in distribution and **90%/91% out of it**, where the oracle
    reaches better PSNR at 1.36-1.56 total passes against the network's
    2.90-2.91. So roughly half the remaining fill is the network failing to
    generalise the cost model, not a floor - and the doc claim that "passes 2..5
    cover a minority of the screen" is true of the SHARPEN pass only. Occupancy
    is noisier than PSNR (sd 0.61 over 39 cross-validation fold-runs, with the
    empty `flat` pan alone at 4.33 passes), so it is not a number to put in a
    budget - size off the 5.00 worst case.
  - **DONE, AND IT REVERSED THE HEADLINE: cross-validate instead of trusting one
    split.** This entry used to read "held-out numbers are inside the SEED noise
    and every write-up must say so - +-0.4 dB on the seed alone, mean +0.10 dB
    and one loss in four". **The +-0.4 dB was not the seed.** `--blss-eval --cv`
    is leave-one-shot-out cross-validation (`--cv-seeds N` independent corpora,
    `--cv-folds N` to shorten it), and it separates the two sources of spread:
    fold to fold the sd is **0.40 dB**, while the sd of the per-seed fold MEAN is
    **0.01 dB**. A single held-out split was a sample of size one, and this
    feature quoted one FIVE times.

    The honest number is **+0.42 dB over plain bilinear** out of distribution
    (13 shots x 3 seeds = 39 fold-runs, 3 below bilinear, 1.80 mean passes), or
    **+0.26 dB** over the six shots that took no part in choosing the defaults.
    So the careful-sounding retraction "about parity, ~+0.1 dB" UNDERSTATED the
    feature. Two things had to be re-swept as a result: `kFillWeight`'s "sharp
    knee at 6" does not exist (12..24 is a plateau; it ships at **16**), and
    `--flicker-weight 0.02` costs **0.02 dB**, not the 0.22 the old split
    reported - and moves the flicker column not at all, which is the real reason
    it ships at zero. The corpus grew from 7 shots to **13** in the same pass,
    with shots 0..6 rendering bit-identically so the before/after is a comparison.

    The cleanest illustration, and worth keeping: when this was written the
    shipped 2-of-13 split read **-0.02 dB** while the 13-fold mean read **+0.40**,
    because that split contains `corridor` - the shot the net loses on. (It reads
    +0.17 against +0.42 today; same lesson, smaller gap, no more trustworthy.)

    **Still open from this item:** the network loses on `corridor` (-0.15 dB)
    because `depth` is a clamped 1/w against `kDepthRef = 8` that spends the shot
    pinned at 1.0, and `--blss-eval --features` says 58.6% of ALL corpus tiles
    read it at exactly 1.0 (`depthGrad` 61.5%, `coverage` 71.9%). A saturated
    feature is a feature the network does not have. A log or reciprocal mapping
    is a `buildFeatures()` change on BOTH twins, i.e. a contract change. Also
    measured and worth not repeating: **input standardisation fitted the training
    shots better and generalised WORSE**, which is the shape of everything in
    this feature - it is variance-limited, not optimisation-limited, and the
    search went to regularisation instead (weight decay 1e-5 -> **1e-4**).
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
    `zBuffer.mask` is 0 only INSIDE the low-res bracket** - DERIVED in
    `allocateVramBuffers` from the allocation, never assigned by a caller.
    `configure()` used to set it one statement before the rebuild that cleared
    it again, and the depth that then leaked past the allocation deleted every
    4-bit palettised texture in the scene (docs/blss-reconstruction.md
    section 6). Every
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
  - **DONE, AND IT IS THE DIFFERENCE BETWEEN HELPING AND HURTING: train on the
    project.** `--blss-train <projectDir>` / `--blss-eval <projectDir>` build the
    corpus from the project's own scenes - `src/blssscene.cpp` walks primitives,
    static .obj and terrain chunks (the three sources `gibake::build()` uses)
    into world-space triangles, with six camera moves per scene derived from the
    bounds and the player start, plus any authored Cutscene Director track.
    Animated .glb is skipped BECAUSE it goes down the dynamic pipeline, which
    does not feed BLSS at all.

    Measured on `examples/procedural`, 72 frames, both nets fitted `--all-shots`:
    the **bestiary-trained net scores -0.40 dB, i.e. WORSE THAN DOING NOTHING**,
    at 1.72 passes; a project-trained one scores +0.06 dB at 1.19; the oracle
    bounds it at +0.77 / 1.20. The mechanism is out-of-range inputs, not
    over-fitting: `texDetail` is identically zero over that untextured project
    and is the bestiary's channel most correlated with the temporal weight, so
    the bestiary net asks for 62-93% temporal occupancy where the oracle asks
    7-30%. So: **fit the project with `--all-shots` and ship that net**, and do
    NOT quote a project corpus' held-out decibel - leave-one-shot-out over six
    camera moves of one scene reads -0.17 dB, 9 of 18 fold-runs below bilinear.
    Some projects have nothing to win: on `examples/showcase` the ORACLE itself
    scores +0.00 dB, which `--blss-eval <projectDir>` is now how you find out.

    **Closed by `7d3dbf67`:** the window's corpus is now one switch in the
    header, shared by all five verbs, and it DEFAULTS to this project's own
    scenes with `--all-shots` on - so the configuration that was measured to work
    is the one the UI expresses first. The progress line counts frames rather
    than assuming 13 shots.
  - **DONE: describe the frame to the network instead of handing it a constant,
    and keep the instrument this time.** `StaPipCore` was handing BLSS the bag's
    bounding SPHERE, once per bag: `wNear = w - radius` collapses to the near
    clamp, so a generated game's whole frame was TWO proxies and every tile read
    `depth = 1 depthGrad = 1 coverage = 1`. Fixed on both twins - an object-space
    AABB near-clipped along its twelve edges (`addBagBox()` <-> the corpus'
    `bagOf()`), one proxy per VU1 package instead of one per bag, a
    threshold-free rule dropping a box that straddles the eye and still fills the
    frame, and `PipelineInfoBag::blssProxy` so codegen can opt the sky dome, star
    field and sun/moon discs out entirely (a shell's AABB describes nothing and
    only the submitter knows it is a shell). Console, debug view 2, before ->
    after: 2 -> 41 proxies, 196/196 -> 159/196 tiles covered, 5.00 -> 1.96 passes.

    The instrument is the durable half and it is PERMANENT now: engine debug view
    2 logs `BLSSGRID`/`BLSSWORST`/`BLSSFEAT`/`BLSSOUT`/`BLSSFILL` to the game's
    `bin/log.txt`, and `--blss-eval --probe "<BLSSFEAT line>"` places that vector
    inside the corpus distribution. An earlier round added exactly this, read it
    once and DELETED it - after which the net was fitted to one distribution and
    run on another for eleven commits with nobody able to compare them.

    **Closed by `7d3dbf67`:** the Debug view combo has three entries, so view 2
    is reachable without a text editor. It offered only 0 and 1 against a field
    `project.cpp` clamps to `0..2`, which meant a project that already had the
    logger on displayed as "Off" and writing the widget back reset it.

    **Still open from this item:** the sky-dome opt-out has never been booted
    (the session that wrote it lost its compositor), so it is verified at compile
    level only.
  - **DONE: two input channels measured and deleted.** `histAge` (the recurrent
    one) was letting the net memorise "this shot has been still a while" - it hurt
    most on the shot with the HIGHEST histAge - and taking it out removed the
    whole recurrent path, including the twin contract's most drift-prone rule.
    `luma` was a channel the EE CANNOT produce: `stapip_core` can only fill it
    from `color->single`, so every per-vertex-lit mesh read a constant 0.5 while
    the corpus spread it over 0..0.48 - fitted on a feature, run on a constant,
    and the constant was OUT of the corpus' range. `kFeatures` 8 -> 6, 147 -> 123
    weights, `kNetVersion` 3. Both were settled by `--cv --cv-seeds 3
    --drop-feature <name>` **against `edgeDens` as a CONTROL**, which is the only
    thing that makes a 0.02-0.03 dB difference mean anything; tables in
    `src/blss.hpp` and docs/neural-upscaler.md. If a photometric channel is ever
    wanted back, the honest form is an EDITOR BAKE into the bag, not a run-time
    sample.
  - **DONE, AND IT IS THE WORST RESULT THIS FEATURE HAS PRODUCED: a BLSS frame
    has been timed on a real PS2.** The page's standing caveat - "no BLSS frame
    has ever been timed, in the emulator or on hardware" - is retired. There is
    a rig now (docs/profiling.md, "Timing a frame that BLSS is in"): COP0
    counters behind `TYRA_FRAME_PROFILE` (default 0, a shipped `libtyra.a`
    carries none of it), read at `beginFrame` and immediately before the vsync
    wait so the number is sub-frame WORK at a locked 50 Hz, plus a fairness
    fence so the BLSS-off arm is drained at the same point as BLSS' own three
    brackets. Output is one `FRAMETIME` line a second plus a raw per-frame dump
    for PAIRED statistics, driven by a frame-INDEXED script camera so frame k of
    run A is the same view as frame k of run B.

    **The calibration gate says PCSX2 cannot measure this feature.** K
    full-screen textured blended sprites per frame, K = 0/2/4/8/16, each with
    its own draw_finish: real PS2 **0.5872 ms** per pass, PCSX2 **0.0077 ms** -
    both perfectly linear, so PCSX2 under-reports GS fill by **76x** and is
    timing its emulated GIF, not a raster. No emulator GS number about a feature
    that trades fill for fill is admissible.

    **On hardware, BLSS cost +9.83 ms per frame and saved nothing.** 1000 frames
    per arm after a 150-frame warm-up, two runs per arm, all four cross-pairings
    within 0.02 ms: mean(d) = -9.83 ms, 95% CI [-9.85, -9.81], n = 924 paired
    frames. 9.42 -> 19.25 ms mean, 0 -> 158 frames over the 20 ms PAL budget.
    Where it went: composite 5.41 ms of which **5.10 ms is EE** (reprojection +
    MLP + the ~5 700-qword packet), `beginScene` 0.45 ms, and ~3.9 ms of extra
    scene submission from the per-package bag proxies. What it saved: nothing,
    because `drain` read **0.02 ms in both arms** and the half-res scene's GS
    overhang was **0.03 ms**. The frame is EE-bound, so halving the raster
    cannot shorten it. A second fixture built specifically to be GS-bound (no
    terrain, sixteen nested cubes around the camera) read the same 0.02 ms:
    untextured opaque geometry is too cheap per pixel to overtake a ~9 ms EE
    frame. **This is the wrong scene for the feature - but nothing in the
    generated-game runtime has been shown to be the right one**, and that is now
    the question this feature stands or falls on. Next: find or build a scene
    whose `drain` is non-zero on hardware (textured, alpha-blended, high
    overdraw), and cut the 5.10 ms composite EE half, which is a cost BLSS pays
    on every scene whether or not there is fill to save.
  - **DONE, AND IT IS STILL BROKEN: the oscillation was re-measured and it is
    still there; there is a kill switch now.** The fill term culls the point and
    sharpen passes - the two that alternate with the jitter - which was the
    reason to hope, and it is not enough: on a real project's scenes it culls
    the **temporal** pass too, and the temporal accumulator is the only thing
    entitled to fuse the two jitter phases. Measured on a static camera with a
    project-trained net: **30.8 % of the picture alternates between two images
    every frame** (amplitude 1.42/255), against 0.05 % for BLSS off. The test
    had to be built to see it at all - the documented reason this survived is a
    sampler with an EVEN frame stride landing on one jitter phase every time -
    so it freezes the camera, samples off the frame clock, and tests for a
    period-2 signature (two balanced clusters) rather than for "did it change".

    **`blssJitter`** (ProjectSettings, default false, format version 9) pins the
    offset to 0; the picture then measures indistinguishable from the BLSS-off
    control. Two things it still needs: **a UI control** (the setting is
    hand-edited in the `.tyra` today - `src/blss_window.cpp` and
    `App::drawPreferencesModal` both draw `drawBlssSettings`, so it goes in
    there), and **the host twin**. `src/blss.cpp`'s oracle and corpus always
    model the jittered sampler, so a net trained today and run with the jitter
    off is being run out of distribution - the flag has to reach the trainer
    before jitter-off is a supported configuration rather than an escape hatch.

    **Re-confirmed independently on `examples/upscaler-lab`'s fixture** (a
    static scene, frozen camera, no particles, no animation, sampled at an ODD
    0.34 s = 17-frame stride): BLSS off gives mean lag-1 **0.018 %** / max
    0.052 %; BLSS on gives mean lag-1 **1.443 %** / max **1.755 %**, and the
    picture takes only those two values - when the sampling phase aligns, lag-1
    drops to 0.05 % and lag-2 rises to 1.75 %, the two states swapping roles.
    That is a clean period-2 alternation at ~33x the still-picture floor. Note
    this scene's project-trained net puts **72-78 % of its weight on the
    temporal pass** (and 0 % on point and sharpen), i.e. the accumulator that is
    supposed to fuse the two phases is doing most of the work here - and it
    bobs anyway, so "the fill term culled temporal" is not the whole story.
  - **BLOCKER, found by `examples/upscaler-lab`: with BLSS on, every TEXTURED
    primitive and the textured terrain disappear.** Reproduced in a minimal
    control (a fresh `--new` fpp project, one plain-coloured box, one box with a
    `map_Kd` material, terrain with and without a terrain material): plain box
    draws, textured box gone, textured terrain gone, untextured terrain draws,
    textured *models* (`.tmdl`/`.tskl`) draw. BLSS off, all of it draws.
    Independent of `textureQuant` (`4bit` and `none` both fail), of static
    batching, of baked AO, of fog and of `blssTemporal`. `VRAMSTAT` shows the
    textures resident and bound thousands of times a frame with `evict=0`, so
    the pass IS submitted and its texture IS bound and nothing reaches the
    low-res target - which is what the GS alpha test rejecting every fragment
    looks like (StaPip draws with the "pass only when alpha != 0" cutout rule,
    and the composite is a PATH3 pass; see the GS post-fx rule about restoring
    ALPHA/TEST/TEX1/XYOFFSET). Until this is fixed BLSS cannot be enabled on any
    project with a textured floor, which is most of them, and no fill A/B on
    real content means anything. `examples/upscaler-lab` is the fixture and its
    README carries the table.
  - **The corpus cannot see the fill.** `blssscene` walks primitives, static
    `.obj` and terrain chunks only (`blssscene.cpp:216-231`), and on the console
    particle bags contribute no BLSS proxy at all - `stapip_core.cpp:282-286`
    gives them no bbox, so the sphere fallback has radius 0 and is rejected. On
    a scene whose overdraw IS particles (the case the feature exists for) the
    net is therefore fitted on the static half and run on a frame dominated by
    haze it has never seen. Either give billboard bags a proxy box, or say in
    the docs that particle fill is out of distribution.
  - **`TPL_RES_GITIGNORE` only reaches one directory level.** The baked-model
    rules are `/models/*.tmdl` / `*.tskl` / `*.tanm`, so a project that keeps a
    model in a SUBFOLDER of `res/models/` (which the Asset Browser encourages,
    and which a multi-file Wavefront asset with its own `.mtl` and texture
    practically requires) commits its bake. Found on
    `examples/upscaler-lab`, whose models have lived in a subfolder throughout:
    411 KB of derived data would have gone in, and the CC0 rebuild of that
    example puts 325 KB of `res/models/depot/*.tmdl` in exactly the same place.
    The example carries a local top-up; the template and the append-if-missing
    block in `refreshGenerated` want `/models/**/*.tmdl` instead.
  - **A `fog` emitter's `opacity` is silently dropped on save.**
    `project.cpp:690` serialises the custom physics block - `speed`, `spread`,
    `gravity`, `weight`, `life`, `grow`, **`opacity`**, `dieOnGround` - only for
    `kind == "custom"` (5), but the READER at `:3661-3692` accepts `opacity` for
    every kind and `templates.cpp:7876` uses it for `kind == 2` (fog) as the
    density knob. So a fog emitter's opacity can be authored, is used by the
    game, and does not survive a round trip. Either serialise `opacity` for fog
    too or stop reading it there; the emitter panel is the third place to keep
    in step.
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
    current baseline**, and re-measuring it means `--blss-eval --cv` and not a
    single split. Beyond that, the editor could measure actual high-frequency energy
    per texture at bake time - a feature DLSS structurally cannot have, and the
    one place this design could beat it rather than imitate it.
  - **Unroll the temporal loop during label generation.** The oracle's labels
    stand in the previous low-res render upscaled, because the true history
    depends on the previous frame's weights. `--blss-eval` already closes the
    loop for the *reported* numbers; closing it for the *labels* means either
    iterating training to a fixed point or a short BPTT window.
  - **DONE: depth of field, portals and split-screen are mutually exclusive
    with it, and THE BUILD REFUSES THE PAIR** (332f3193). All three want real GS
    depth at display resolution, which since the z-buffer shrink is not merely
    unwritten but unallocated. For three commits nothing but the preferences
    warning enforced that - no path in `src/templates.cpp` gated them on
    `blssEnabled`, so "the generated game does not emit them together", which
    appeared in docs/neural-upscaler.md, docs/blss-reconstruction.md and the
    engine skill, was simply false, and a user who went past the warning got an
    ELF that compiled, booted and drew the wrong picture. `blssClashes()` +
    `blssInterlock()` now put `#error` lines into the generated
    `inc/scene_data.hpp` naming the feature and the scene, so nothing that
    produces an ELF gets past them; `generate()` prints the same on the host.
    Refusing rather than auto-disabling, because a build that quietly measured a
    different configuration than the project describes is the worst outcome for a
    proof of concept whose point is being measured.

    Each condition mirrors the generated game rather than the coarser question,
    and **`App::drawPreferencesModal` was brought into line with all four**: DoF
    per SCENE, quantised to 1/128 and gated on a non-zero focus; **the `Set Depth
    Of Field` flow node**, which raises DoF at runtime in a project whose
    authored amount is 0 everywhere and which the dialog missed entirely;
    portals only when the target resolves to another Portal in the same scene;
    split screen only when a scene has a **second Player object**
    (PLAYER2_INDEXES), which the dialog used to ignore, warning about projects
    that never render a split frame.

    **Still open:** DoF is the interesting one to actually fix. It would need an
    upscaled depth buffer, which the GS cannot produce in a blend pass, so it
    probably means keeping a low-res depth *colour* target and re-deriving z - a
    design, not a fix.
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
  `Sprite::drawSize` plus the `project::displayModes` table). **The same split
  now applies to WIDESCREEN**: menu panels cancel the anamorphic stretch
  (docs/menu-styles.md "Widescreen", `RendererSettings::getWindowAspect()`) and
  those three surfaces do not, so they still come out a third wider on 16:9.
  Whether they SHOULD is a design question rather than a bug - a HUD pinned to
  the screen edges is arguably meant to follow a wider frame, while a fixed-
  aspect element (a radar, a portrait, an icon) is not - so the answer is
  probably per element, not per surface, and it wants deciding before it is
  coded. The save menu is the clear-cut half: it is a `GameMenu` that is drawn
  by its own function and takes neither the resolution scale nor the aspect
  compensation, which is simply inconsistent with every other menu. Also: a `close`
  transition is parsed and generated but the runtime only plays `open` and
  `cursor`; `description { area: right }` is laid out but untested on a wide
  panel; and a per-menu "bake crisp for mode X" would remove the 1.2x upscale
  softness at 1080i for a second texture's worth of VRAM.
- **Sound priority and voice stealing** - DONE (docs/sound.md), all three steps
  of `.claude/plans/sound-priority.md`: emitters rank by priority then loudness
  instead of hashing their scene index into one of 8 slots, the audsrv fork
  gained a forced play (so a pinned Play Sound channel cuts off, as its tip
  always claimed), and both the node and the emitter carry a Priority. What the
  brief could not settle stays open, and it is a listening question rather than
  a code one: **does a forced restart over a live voice click?** If it does, the
  fixes in order are a volume drop plus a play on the next frame, or a KOFF and
  the ADSR release - both a frame of latency, so they are not worth paying
  until someone hears the problem. The other half of that question is whether a
  click is masked in practice (a gunshot stealing footsteps hides a lot; two
  quiet voice lines do not).
- **Reverb: a third room, and the tail of the cross-fade.** The two reverb
  units are both in use now (docs/reverb.md): a room owns a bus and transitions
  cross-fade across them. Two things were left where the chip runs out. A THIRD
  room entered while a fade is still running waits for the first to finish
  leaving (it waits rather than glitching, but it is a wait); and a sound is
  committed to a bus when it starts, so a long sample carried between rooms
  keeps the old room for its whole length rather than being re-routed. Both are
  arguably correct behaviour, both are worth re-examining if a project trips
  over them.
- **Reverb on real hardware.** Everything in docs/reverb.md was measured in
  PCSX2, which does emulate SPU2 reverb. Wanted: the same decay-tail
  measurement on a console, plus a check that the `sceSdInit`-before-audsrv
  ordering behaves there too.
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

