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

### The guard band, on the other two routes

`docs/vu1-clipping.md` moved screen-edge packages off the clipper and onto the
cull path; two smaller cases were left alone deliberately and are worth
measuring before touching:

- **The small-bag branch.** `StaPipCore::render` sends a bag with fewer than
  `maxVertCount * 2` vertices straight to `renderSubpkgs` at 1/3 package size,
  so a guard-band-only bag is batched back together by `fillByCopyMax` - a COPY
  where the full-package path hands VU1 a pointer. Routing that branch through
  `renderPkgs` instead would give it the pointer path; the reason it exists is
  to skip a double classification, so the question is which costs more.
- **A bag-level guard-band test.** The main bbox is classified before any
  package is created. A bag entirely inside the guard band could skip package
  classification altogether, at the price of no longer dropping its OUTSIDE
  packages - the same trade the routing already makes one level down, but over
  a much bigger box. Measure on a scene of large objects, not on a terrain.

### Ship the baked HUD sprites of the nine repaired examples

Nine example projects (`custom-nodes`, `cutscene-demo`, `large-terrain`,
`layer-streaming`, `mirror-room`, `nav-ai`, `object-spawning`,
`physics-playground`, `script-demo`) carried a `res/.gitignore` of `*` and so
tracked no assets at all. The rule is fixed and their authored assets are in,
but their baked `res/hud/*.png` and `res/fonts/atlas-default.png` are still
untracked, because the four font-derived ones (`icons.png`, `use-text.png`,
`pick-text.png`, the atlas) bake from a **system** font and a Windows box
produces Consolas where the committed copies in the other examples are DejaVu —
committing them here would disagree with the `font_data.gen.hpp` metrics beside
them. Add them from a Linux `--refresh-gen`, where the bake matches the rest of
the tree, and confirm with `git status` on a fresh clone that a built example
leaves nothing untracked.

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

### Let the Menu Editor's fit check know the real heap

`menulayout.cpp` measures a menu against a hardcoded 282 000-word texture heap,
which is only the 32-bit, both-targets-reserved case — a 16-bit project has
nearly twice that and still gets warned as if it did not. Take the number from
the project's settings the way the engine does. See [GS VRAM](gs-vram.md).

### Report ps2sdk's `GS_SET_DIMX` bitmask upstream

Each DIMX entry is a **3-bit signed** value (-4..+3) and ps2sdk masks all
sixteen with `0x00000003`, so the negative half of any dither matrix collapses
onto 0..3, the offsets stop cancelling and the dither biases the image upward.
The patch is `0x00000003` -> `0x00000007` in `common/include/gs_gp.h`, and the
regression risk is nil: a caller passing 0..3 gets bit-identical output. Still
present on master with no open issue when checked (2026-08-12); PCSX2's own
`s32 DM00 : 3` is the evidence for the width. `tyraxDitherMatrix()` in
`renderer_core_gs.cpp` packs the register by hand until it lands, and says so.

### Bound terrain UVs on very large maps

Terrain UVs grow with world position and can outrun the GS fixed-point range.
Fold each chunk by whole texture repeats during generation, preserving the
picture under REPEAT while bounding coordinates by chunk size.

### Ship one example with sculpted terrain

DONE — `examples/ambient-occlusion`. Left here only for the fact that produced
it: every heightmap in `examples/` used to be flat, relief 0.00, all of them,
which is how a bare 30° slope came to darken itself by 16% for several releases
with nobody seeing it. Keep at least one example sculpted.

### Let imported models receive the scene occlusion

The occlusion model is now good enough for it, and that was the blocker rather
than the plumbing. Re-measured on the console with `examples/ambient-occlusion`
(real kit props): with models receiving, a crate under another crate reads 0.98
of its uncovered neighbour's brightness against 0.87 for the AO-off scene, and
nothing reads as a lump - where the old distance-based response gave 0.78 and a
visibly darker box. Model AO (per texel, in the shipped texture) answers a
model's SELF occlusion and is transform-invariant, so it can never answer for a
neighbour; this is the other half.

What it needs, and why it is its own commit: `g_aoOff` off for type 5 in BOTH
the solo and the static-batch paths, model part bags switched to Gouraud (a
per-vertex value is invisible under flat shading - the lesson from the block
work), and a re-verification pass over the examples, because it changes how
every textured prop in every project is shaded.

### Give scene occluders more than one box each

An occluder is a single oriented box or sphere per object
(`aobake::collectOccluders`), so a chair, an L-shaped wall and a doorway arch
are all one rectangle to the bake. Splitting a model's triangles into 2–4 boxes
is what would lift that ceiling. Do it after the solid-angle change above, not
before — a better response over one box may be enough for most of them.

### Model AO for animated models and for shared textures

[Model AO](ambient-occlusion.md#model-ao) covers static `.obj` assets only.
Two deliberate gaps are worth revisiting once it has been used in anger. An
animated `.glb`/`.fbx` bakes a bind-pose AO map perfectly well, but its
textures ship through `animBakedTextureRel`, so the multiply needs a second
hook and the map is a lie for anything that deforms much. And a texture shared
by several model assets is skipped outright, because two UV layouts over one
image make a single multiply wrong for both — the honest fix is a per-asset
COPY of the texture in the bake, which trades the feature's zero-VRAM property
for coverage and should only be done where the author asks for it.

### Pre-lit bake parameters are editor state, not project state

`litbake::Params` (size, rays, padding, strength, floor, seed) lives on the App
and is not serialized, but it IS part of every object's staleness signature
(docs/prelit-models.md). So `--bake-prelit` uses the defaults and considers an
object baked at 256 px from the panel stale, and a second editor session starts
from the defaults too. The honest fix is per-object bake parameters stored
beside `prelitSig`, which is also what would let one hero wall be 256 while the
rest of the scene is 128 — worth doing the first time somebody mixes sizes.

### Linux packaging: the two things it did not do

Done in 1.52.0 — `installer/build-package.sh` stages the repo-shaped tree once
and emits a `.tar.gz`, a `.deb` and an `.rpm`; the tarball self-updates and the
two packages are told to use the package manager (docs/updates.md). Two pieces
were deliberately left out and are worth doing when somebody asks for them.

**No AppImage**, and not for effort reasons: an AppImage's payload is a
user-private FUSE mount under `/tmp/.mount_*`, and the game build bind-mounts
`vendor/tyra` into a container whose daemon runs as root — which cannot traverse
that mount. So the one format that looks tailor-made for this would ship an
editor that cannot build a game, unless it first copied the engine out to a real
directory, at which point the tarball is simpler and honest. Revisit only with
that copy-out step designed.

**x86_64 only.** An `aarch64` package is a runner and a second matrix row (plus
`platformAssetSuffix` learning the architecture, which is why the suffix already
carries it) — but the PS2 toolchain image would have to run under emulation on
that host, so measure a game build there before promising anything.

### Make the packagers prove they shipped what git tracks

1.55.3 fixed one tracked file going missing from every package — an exclusion
written as `*.a` took `vendor/tyra/audsrv/bin/libaudsrv.a` with the build
leftovers, and every installed editor then failed every game build
(docs/updates.md). Nothing would have caught it: both packagers describe what to
LEAVE OUT, so a new exclusion is only ever tested by somebody installing the
result and trying to build a game — which is the slowest feedback loop in this
repo and the one a developer never runs. A cheap assertion closes it: take
`git ls-files vendor/tyra tools`, drop the ignored directories, and fail the
release job if any of it is absent from the staged tree. The awkward half is
Windows, where the staged tree only exists inside the compiled Setup — either
parse ISCC's `Compressing:` lines (it lists every file it packs, which is how
the 1.55.3 fix was verified) or run the installer into a temp directory in CI
and diff that.

### Sign the Windows installer

The released `TyraX-Setup-<version>.exe` is unsigned, so Windows SmartScreen
warns on first run and the in-editor updater installs a binary whose only
provenance is the URL it came from. A code-signing certificate plus a signing
step in the release workflow fixes both; until then, the honest mitigation would
be publishing the installer's SHA-256 with the release and having
`update::download` check it (the release JSON already carries the asset's size,
but not its digest).

### Find the corona's missing 1.3x on the console

Measured while bringing the beams into the viewport: a PCSX2 frame's beam
corona adds **1.26-1.31x** what its own sprite implies, while the editor's twin
adds 0.97-1.00x of it. The instrument is beam-on minus beam-off in each
renderer, sampled straight up from the light and fitted against the bake's own
alpha curve (`t^2 (0.3 + 0.7 t)` from `menubake::bakeFlareRGBA` kind 2) times
the light colour. It is a pure AMPLITUDE factor, not a size one: fitting a free
radius instead gives rms 12.5 against 2.4, and the fitted radius scale would
have to be 1.18 while the glow demonstrably dies at the same radius on both
sides. It is also independent of everything tried - the same factor at
`lightBright` 1.3 and 0.4 (so not the `min(k, 1)` FIX clamp), in interlaced and
progressive display modes (so not field rendering), at every radius from 20 to
55 % of the sprite (so not a texel offset), and the shipped
`res/hud/flare-corona.png` is byte-for-byte the bake. The sky's authored colour
reads the same in both captures, so it is not a global capture gain either.
Candidates left: the GS texture function or the `GS_SET_ALPHA(0,2,2,1,FIX)`
path in `StaPipQBufferRenderer` doing something other than `Cs*FIX/128 + Cd`,
PCSX2's software blending of a 16-bit target, or a second draw of the same
quad. Settle it before making either side match the other - the viewport
currently reproduces the sprite exactly, which is the defensible half.

## Medium

### Vehicles: what a drive still owes (docs/vehicles.md)

The feature drives - enter, steer, drift and wall collision are all
machine-verified on the emulator via the VEH telemetry - and these are the
gaps, each with a testable end:

- ~~**A drive is silent.**~~ DONE, and the interesting part is that the blocker
  was never the pitch. `SD_VPARAM_PITCH` was always reachable, libsd was already
  linked and `logVoiceState` already READ that register - what was missing was
  that nothing could LOOP, and looping turned out to live in the ENCODED sample
  rather than in the play call (`adpenc -L` sets the SPU2 block loop flags). So
  the build encodes any `res/sfx/*-loop.wav` that way, the engine fork gained one
  function (`AudioAdpcm::setPitch`), and the runtime quantises the register to 32
  steps because `sceSdSetParam` is a blocking `SifCallRpc`. Two instruments were
  needed to call it done: the telemetry for the TRACKING (idle 800 rpm -> pitch
  1408, 6585 -> 4192, dropping at every upshift) and a capture of PCSX2's own
  audio output for the AUDIBILITY (spectral centroid 194 Hz idle -> 417 Hz at the
  first-gear redline -> 243 Hz after changing up). Left here for the rule: a
  sound feature needs both, because a correct register nobody can hear and an
  audible noise that ignores the sim look identical in a log.

- ~~**Hide a third-person avatar while driving.**~~ DONE. `vehicleDrivingAnd`
  ANDs `vehicleDriver_ < 0` into the line that already applies a cutscene's *Hide
  player*, so the condition is the driver state itself - no flag to clear, and
  getting out restores the avatar with no second writer. Left here for the fact
  that produced it: that line exists TWICE, once per game-cpp head, and a
  placeholder that reached only one of them would work in an orbit project and not
  in an FPP one.
- **The distant one-submit tier.** vehbake already produces a body with the
  wheels' geometry available; what is missing is a merged body+wheels bake and
  a distance switch in renderVehicleWheels/the body row, so a parked fleet far
  away costs one submit per car instead of two. Done when the telemetry (or a
  bag count in the Stats tab) shows the switch happening at the distance.
- **Hardware frame cost.** The two submits are a design property, not a
  measurement - nothing has timed a driven frame on a real PS2 (docs/profiling.md
  has the method). Done when docs/vehicles.md quotes measured EE ms for one
  car driving, the way the BLSS page quotes its fill numbers.
- **Vehicle-vs-vehicle and vehicle-vs-physics.** A car stops at walls and
  pillars; it does not yet trade momentum with physics crates (PHYS_PUSH is
  the player's shove, unused here) or with another car. Done when driving into
  the physics-playground crates scatters them.
- **AI traffic.** The whole input path is already a struct (DriveInput /
  the runtime's inThrottle..inHand locals) precisely so a controller other
  than the pad can fill it; navigation.gen.cpp already runs A* for walkers.
  Done when a second vehicle patrols waypoints with no pad attached.
- **The editor test drive ignores instance scale.** The runtime multiplies
  track, wheelbase, ride height and the camera rig by the placed object's
  uniform scale; the host sim drives the raw spec, so a car authored at scale
  1.5 handles differently in the two. The example's car IS scale 1.5. Done when
  the test drive takes the instance's scale - probably a scale argument on
  step() rather than pre-scaled spec copies, so specFields stays the one list.
- **The editor test drive's walls are approximate.** World AABBs via
  placement, not the console's slide resolver - a rotated wall blocks a wider
  footprint in the editor than on the console. Fine for tuning; worth one
  sentence of honesty in the panel if anyone reports it.
- ~~**No speedometer.**~~ DONE - speed, gear and the nitrous tank through
  `drawFontText`, verified on the console reading 88 / gear 5 / NOS 3. A TACHO is
  still open, and the reason it is not a small addition is that a PS2 sprite is
  AXIS-ALIGNED: a swinging needle is not a sprite rotation but either a pre-baked
  sheet per angle or a small bag of geometry. Left here for the trap the first
  version hit - the nitrous line sat at 0.945 of the frame height, where the
  emulator's own picture already cut it in half and a CRT would have lost it
  entirely. Anything added to that readout gets checked against
  docs/safe-areas.md, from the bottom row up.
- **The vehicle controls are RAW pad reads.** Only USE goes through the Input Map;
  throttle, brake, handbrake, nitrous and the camera cycle are
  `pad.getPressed().Cross` and friends, which is exactly what
  docs/input-bindings.md says not to do. Five `InputAction::Role`s plus their
  `kSeeds` and `kRoles` rows fixes it and makes a wheel or a rebind possible at
  all. Done when a project can rebind the throttle.
- **The full NFS paint stack** - the wet-lacquer look is more than the env map
  the body already has, and the era's recipe is known (per-vertex passes, all
  VU1-computable): a WHITE SPECULAR from Blinn-Phong (N.H)^p written into the
  vertex ALPHA and added via the GS's HIGHLIGHT/HIGHLIGHT2 texture functions
  (their RGB is Texture*VertexRGB + VertexAlpha - the alpha lands as pure
  white, which is exactly the era's burned-out highlight); a FAKE FRESNEL
  (1 - N.V, optionally raised to a power) as the reflection pass's per-vertex
  alpha, so the silhouette gets the silver rim while camera-facing paint keeps
  its colour - that needs the env pass switched from the per-bag additiveBlendFix
  constant to source-alpha additive blending; and optionally a STATIC sphere
  map with vertical light streaks instead of "@sky" (the Underground night
  look), which is just a per-vehicle refl texture choice plus a generator
  script. All three want a dedicated VU1 program for cars (transform + N.L +
  N.H + reflection UV + Fresnel into alpha, then two or three GIFTag sets for
  the passes) - docs/vu-framework.md is the tooling, and the litany's own
  estimate is right: very doable, not monstrous. Done when a driven car shows
  a moving white highlight and a silver rim, verified by the shine A/B method
  (crop the body, diff against the pass disabled).
- **Tyre smoke.** `DriveState::slip` is already the ONE number for it (the
  sideways slide and the wheelspin folded together, measured reading 0.9 at a wall
  impact), so this is a particle question rather than a vehicle one: an emitter
  that can be moved to a wheel anchor and burst from game code.

### ANSWERED: the guard does run under ps2link, and guards nothing

```
SIF RPC guard: seen 306,   guarded 0
SIF RPC guard: seen 14329, guarded 0     <- ~150 completions/second
SIF RPC guard: seen 29483, guarded 0
```

Fresh boot verified by the protocol below (two boot lines in the capture, first
`VRAMSTAT` at `f=120`). So the handler **is** on the dispatch path on hardware -
~29 500 completions in ~200 s - and none of them needed guarding. Every earlier
zero was therefore a real negative, not a handler that was never asked. It also
works in PCSX2 (429 completions), so both targets are covered.

Getting to that took three retracted conclusions, and the protocol that survives
is the useful residue:

- **A deploy is only fresh if the capture proves it.** Require a boot line
  (`Clut set` / `Pad initialized`) or a low first `VRAMSTAT f=`. `bin/livedbg.bin`
  appearing proves nothing: a game still running from an earlier deploy resumes
  polling the instant a file server returns and writes exactly that file, so a
  refused deploy is indistinguishable from a successful one. One capture read
  `f=136800` - a 45-minute-old ELF being measured as if it were the new one.
- **`--run-ps2` does not capture the boot output**; a manual `ps2client execee`
  redirected by bash does. Any once-only log line will be missed by the former,
  so make announcements periodic.
- **Never mirror another library's private struct.** A diagnostic that printed
  ps2sdk's dispatch slot inferred `struct cmd_data`'s layout from the
  ASSIGNMENT order in `sceSifInitCmd()` instead of the declaration - `iopbuf` is
  declared third and assigned last - so it indexed the IOP receive-buffer
  address as an EE array and faulted the game: `TLB load, BadAddr 0x00019640,
  EPC` inside `SifRpcGuard::report()`. That crash was mistaken for the bug being
  reproduced. The read is gone; the counters stay.

**The measurement below was invalid and the conclusion it produced is
withdrawn.** Recorded in full because the flaw is the reusable part.

What was claimed: the guard handled 429 completions in PCSX2 and zero across
several ps2link sessions, therefore it is inert under ps2link.

**The PCSX2 half stands** - 429 completions, so the guard works and the counter
works. The ps2link half does not, for two compounding reasons:

1. **`--run-ps2` never captured the boot output.** A manual `ps2client execee`
   capture contains `Pad initialized`, `Clut set`, `Hello from TyraX`; four
   `--run-ps2` captures contain none of them. The liveness line fires ONCE, on
   the first frame, so it sat outside every capture window.
2. **Worse: "the game came up" was never actually verified.** The check was "did
   `bin/livedbg.bin` appear", and a game **already running from an earlier
   deploy** resumes polling the moment a file server reappears and writes that
   file. So a refused deploy looks exactly like a successful one. Caught by the
   frame counter in a late capture reading **f=136800** - about 45 minutes of
   uptime, i.e. an old ELF. Several late measurements, including the
   `eeCrashHandler` on/off comparison, were made against a binary that was not
   the one just built.

**Protocol fix, mandatory for any future run on this:** a deploy counts as fresh
only if the capture contains a boot line (`Clut set` or `Pad initialized`) **or**
the first `VRAMSTAT` reports a low `f=`. Never trust `livedbg.bin` appearing.
And make any liveness announcement PERIODIC, not once, so a late-starting capture
cannot miss it.

So the question is open exactly as before: does the game's `SIF_CMD_RPC_END`
handler run under ps2link? The evidence that it CAN is unchanged and still the
strongest thing here - the original crash reported `EPC 0x00271C78`, which is
inside the game's image (games load at `0x00100000`, the low ps2link sits at
`0x00094000`), so the game's own `_request_end` was executing when it faulted.

The withdrawal also **restores** the two conclusions the invalid measurement had
knocked out: the v1-vs-v2 hang table and the `g_noPacket = 1` reading are back to
what they were - suggestive, unproven, and not contradicted.

Same ELF, two targets, one counter (`SifRpcGuard::seen()` + a one-time log line):

| target | completions the guard handled |
|---|---|
| PCSX2 | **429** in ~25 s (`SIF RPC guard: live, 429 completion(s) handled`) |
| ps2link, 3 sessions, ~20 000 `[ps2]` lines total | **0** - the line never appeared |

So the guard is functional and does not run where the crash happened. **Every
zero it reported on hardware means "never asked", not "nothing wrong".** Checked
against the obvious mistakes first: the string is in the deployed ELF, other
engine `TYRA_LOG` lines arrive in the same captures, `engine.o` references both
`install()` and `report()`, the generated game goes through `Engine::run` ->
`realLoop` -> `report()`, `install()` position (first vs last in `initAll`) makes
no difference, and `eeCrashHandler` on vs off makes no difference either - that
correlation is dead.

**Two earlier conclusions on this branch are withdrawn because of it.** If the
guard does not run under ps2link then v1 and v2 are functionally identical there,
so the guard cannot have caused the v1 hangs: the 2-of-4 against 0-of-6 table is
back to being noise, and "v1 was harmful" is not supported. (v2's shape is still
the better design on its own merits - completing the client is correct whether or
not anything is guarded.) And the single `g_noPacket = 1` read cannot have come
from a handler that does not run, so treat it as an artefact.

**The sharp open question, and it is a good one.** The original crash reported
`EPC 0x00271C78`. A game loads at `0x00100000` and the low ps2link sits at
`0x00094000`, so that EPC is in the **game's** image - the game's own
`_request_end` was executing when it faulted. So the game's handler *can* be on
the dispatch path under ps2link; it simply was not in any session measured here.
Until somebody works out what differs, a game-side guard cannot be trusted on
hardware, and the defence may belong in `tools/ps2link/tyrax.patch` instead -
i.e. in ps2link's own ps2sdk, where the other instance lives.

Next probe, cheap: register the guard for `SIF_CMD_RPC_BIND` as well and log that
separately. A bind happens at `fioInit()` before any game traffic, so it says
whether the game's dispatcher was ever consulted at all or stopped being
consulted at some point.

### Finish verifying the SIF RPC completion guard

**Measured, and it can invalidate every other number this feature has produced.**
The guard now counts *every* completion it is handed (`SifRpcGuard::seen()`) and
announces itself once in the log the first time that count is non-zero. Across
**two ps2link sessions, ~6000 and ~7000 `[ps2]` log lines each**, that line never
appeared - so the handler recorded **zero** completions, while the game's `host:`
file I/O worked throughout. The line is present in the deployed ELF (`strings`),
54 other engine `TYRA_LOG` lines arrived in the same capture, `engine.o`
references both `install()` and `report()`, the generated game really does go
through `Engine::run` -> `realLoop` -> `report()`, and moving `install()` to the
very end of `initAll` changed nothing.

If completions are dispatched through **ps2link's own** ps2sdk sifrpc instance -
it keeps one live in the same EE address space, with the stock unguarded
`_request_end` - then a zero `rejected()` count means "never asked", not
"nothing wrong", and the guard cannot protect the devkit session it was written
for. "The game works" does **not** discriminate: ps2link's `_request_end` takes
`cd` from the packet payload and would complete the game's client just as
correctly.

**The lead to pull first is a correlation, not a theory.** The one session that
ever reported a non-zero counter (`g_noPacket = 1`) had
`ProjectSettings::eeCrashHandler` **off**. Every session that measured
`seen == 0` had it **on**, and `ee_dbg_install()` hooks the EE exception vectors.
So: run the same announce build twice, with the crash handler off and on, and see
whether the liveness line appears. That is one build and two deploys.

Also worth checking directly: whether the game's `sceSifInitCmd()` actually
repoints the IOP (it should send `SIF_CMD_CHANGE_SADDR` with its own `pktbuf`
because ps2link left `SIF_SYSREG_SUBADDR` non-zero), and which of the two
`_SifCmdIntHandler`s on `DMAC_SIF0` ends up seeing a non-zero `psize`.

**Until this is settled, treat the guard as unproven in both directions** - it
has no demonstrated benefit and now no demonstrated reach either. PR #222 is a
draft again for that reason.

### Finish verifying the SIF RPC completion guard

The guard (`vendor/tyra/engine/src/debug/sifrpc_guard.cpp`) is measured to do no
harm and has **no demonstrated benefit** — the fault it defends against has
never been reproduced. Teardown A/B on hardware, one generated tree, arms
differing only in `engine.o`'s reference to `install()`:

| build | teardowns | hangs |
|---|---|---|
| no guard | 6 | 0 |
| guard v1 (returned early) | 4 | **2** |
| guard v2 (completes the client) | 3 | 0 |

Three clean cycles against a 2-in-4 rate is p ~= 0.125. **Run three more** and it
is near 0.016. The trigger is a teardown, not load: deploy, settle 45 s, kill the
file server, then reattach a client and count the game's own `open host:` lines —
251-427 when alive, zero when hung (`scratchpad/.../teardown-cycles.sh`). Allow
32 s after `ps2client reset`. Five rapid cycles drive the console into
answers-ping-refuses-every-deploy and it needs the physical button.

Reading the counters needs care: `.bss` addresses move between builds, so
re-derive them per ELF (`nm <name>.elf.sym | grep g_noPacket`) — a previous
build's address returns plausible garbage. And a hung game can keep the `host:`
channel busy enough that `dumpmem` never answers, so it is still unknown whether
the fault fired during those three clean cycles at all.

### Find what produces the anomalous SIF RPC completion

**Four reproduction approaches are exhausted, all negative**, so this is now a
code-reading job rather than an experiment:

| approach | scale | result |
|---|---|---|
| amplified RPC load | 120 min, ~79 500 frames, 2.7x density | nothing |
| live editing session | all four channels writing, 50 min | nothing |
| teardown (kill the file server) | 6 unguarded, 4+3 guarded | no crash |
| livetex hammer | 1024 bumps, **757 confirmed PNG re-reads** | nothing |

The last one closed the final condition from the original report - a whole PNG
decoded inside one frame on the main thread while the streamer thread reads. It
needs no GUI: the `livetex.bin` layout is `TXLT`, version, seq, count, then
104-byte records of a 96-byte path + u32 generation, and a footer of
`seq ^ 0x5A5A5A5A`. Re-announce a byte-identical file so `reload()` cannot take
its changed-size escape, target a path the game really knows
(`inc/texture_data.gen.hpp`), and write tmp-then-rename with a retry - Windows
refuses the rename while the file server holds the target, constantly. A
`--livetex` CLI verb next to `--pad` would make this a first-class test tool.

Two things that shape the search now. The fault fired **once**, in a session
nobody instrumented, and nothing since has moved it - so treat any future
occurrence as precious and read the counters immediately. And note the guard
lives only in the GAME's sifrpc instance while ps2link keeps its own live in the
same address space: if completions are being dispatched through ps2link's
unguarded `_request_end`, the game's counter would stay at zero no matter what,
which is consistent with every zero measured so far. **That is the first thing to
check.**


The guard proves *a* completion arrives with a null packet; it does not say who
sent it. Ranked candidates, none discriminated: ps2link's `pkoSendSifCmd()`
reusing one unsynchronised 1 KB buffer with no DMA wait (its own known rough
edge, and the IOP exception handler shares that buffer); the IOP-side reply ring
`_rpc_get_fpacket()`, a 32-slot round robin with no in-use flag and no interrupt
protection; and `_SifCmdIntHandler()` calling `EI()` before it has copied the
packet out and cleared `psize`. Which counter fires — `rejectedNoPacket` vs
`rejectedBadClient` — narrows it. Note ps2link keeps its **own** ps2sdk sifrpc
instance live in the same address space as the game, and the guard is installed
only into the game's.

### Recover the console from the refuses-every-deploy wedge

Repeatable now: five rapid reset + teardown cycles get there, and so does a
hung game. Ping answers, `tcp/18193` listens, every `execee` is ignored, three
`ps2client reset`s change nothing and only the physical Reset recovers it. Same
family as the historical hang list in
[ps2link-setup.md](ps2link-setup.md); worth a look now that there is a recipe.


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

### Make the small render targets follow the colour depth

The env map, the camera feed and the four shadow slots are PSMCT32 render
targets read as textures (~192 KB when a project uses all three). They could
follow the project's colour depth the way the post-fx work buffers now do, for
about half of that — but they are bound through `Texture::vramResident`, whose
`TextureBuilderData` has no 16-bit `bpp`, so `TextureBpp` has to be widened
first. Left out of the colour-depth work deliberately: the frame buffers were
90% of the win. See [GS VRAM](gs-vram.md).

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
cost — the heap is bigger than it was, 1.08 MB at 32-bit colour and ~1.95 MB at
16-bit ([GS VRAM](gs-vram.md)), but a mip chain is still opt-in per texture —
verify small-level addressing on hardware, and keep non-mipped textures
unchanged.

### Offer a 16-bit Z buffer

The z buffer is the largest single block left at 229 376 words, as much as both
frame buffers cost together at 16-bit colour, and `PSMZ16` would halve it. What
stops it being free is precision: the projection runs `near` 0.1 / `far` 51200
and depth is hyperbolic, so 16 bits resolve roughly 1.5 world units at 100 out
and ~38 at 500 — terrain and baked shadows would z-fight. So it is a per-project
option gated on raising `near`, judged on a fixture with a short view distance,
not a default. The mechanical part is small but crosses four layers: the VU1 z
scale (`0xFFFFFF / 2`) is an EE-side constant in `stapip_vu1_program.cpp` and
its dynpip / mcpip twins, and the same constant appears in the generated game's
EE clipper (`templates.cpp`), in `vugen`/`vusim` and in `vucap`. Also unsettled:
whether `PSMZ16` may pair with a `PSMCT32` frame buffer, which decides whether
the two depths can be chosen independently at all. See [GS VRAM](gs-vram.md).

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

### A devkit self-screenshot command (works on locked desktops and real hardware)

The 2026-08-17 corona session proved the game can dump its own framebuffer
through `host:` (ps2sdk libdebug's `ps2_screenshot_file`, VIF1 reverse FIFO;
pass the framebuffer address in BLOCKS - `fb->address / 64` - or SBP's 14 bits
overflow and the pages scramble). Productize it as a devkit channel: a command
bit in `livedbg.cmd` (the VU capture is the precedent), a debug-only generated
runtime write into `bin/frame.tga`, a Debugger button, the TXDEVKIT marker +
`kStringNeedles` entry, and stale-file cleanup in both Runner launch paths. It
is the only capture path that survives a locked desktop, and the only one that
exists at all on a real console. See [live-debugger](live-debugger.md).
