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
