# troubleshoot/tyra-testing-blss.md

The BLSS neural upscaler and VU1 microprogram layers (both inside Layer 1). Symptom first.

```
TYRAX --blss-train [<projectDir>] [--all-shots] [--threads N] [-o out.net]
TYRAX --blss-eval  [<projectDir>] [-i net] [--cv] [--features] [--dump <dir>]
TYRAX --blss-emit  [-o inc/blss_net.gen.hpp]
TYRAX --blss-coverage <projectDir> [--frames N] [--raster N] [--threads N]
                                   [--out WxH] [--verbose]
TYRAX --vu-check [engineDir]         # exit 0 = every program parsed AND every
                                     #   generated one matches its handwritten
                                     #   twin bit for bit, in the host simulator
TYRAX --vu-list <file.vclpp>         # expand + disassemble one microprogram
TYRAX --vu-emit <outDir>             # generate .vclpp + the EE program classes
TYRAX --vu-replay <projectDir>       # re-run a console VU1 capture on the host
```

## The BLSS coverage number cannot be re-run

**Cause:** the estimator was a button in a GUI; a number nobody can re-run is a number nobody can check.
**Fix:** `--blss-coverage` is the SPEED half of "should this project have BLSS on", the headless twin of the window's *Will the frame get faster?* button — same `blss::measureCoverage`, same verdict arithmetic, in-process, about a second. It prints per-shot and overall mean/p95 coverages with the geometry/emitter split, the derived verdict, and machine-readable `[blss] coverage …` lines. It reads the project's reconstruction mode (`blssNetwork`) and prices the verdict for it — plain mode's EE bill is a seventh of the neural one, so the same coverage count lands against a **2.6** break-even instead of a 13.1 one — and prints the OTHER mode's line beside it. Read its output as an overdraw **index** that over-states its scale by about a third, not as milliseconds (docs/neural-upscaler.md, "The overdraw count is an INDEX"; docs/profiling.md).

## A BLSS decibel table cannot be reproduced

**Cause:** eight flags measure a configuration no project can currently ask for, and a table of decibels whose configuration is not written down is a table nobody can reproduce (that is how this feature published six wrong numbers).
**Fix:** the flags each print a line saying so — `--tile N`, `--scale WxH` (the raster scale; the ENGINE is generic, it is `blssScale` that can only name 2x2 and 1x2), `--act-table N`, `--no-anim`, `--still` (freeze each shot at one camera and one pose so only the jitter phase advances — refused by `--blss-train` and by `--cv`), `--proxy-budget`, `--emitter-proxy` (the SIXTH twin-contract rule — give each enabled particle emitter a bag proxy; engine half is `TYRA_BLSS_EMITTER_PROXY`; it makes the six channels DESCRIBE the particles and the corpus renderer still DRAWS none, so a PSNR from an `--emitter-proxy` run prices the description against a particle-free truth — read `--features` and a console `BLSSFEAT` line through `--probe`, not the dB) and `--ignore-shot-plan` (do not read the project's training-shot plan). The sampler is announced in both directions now.

## A BLSS thread-count change changes the net

**Cause:** a thread-dependent result would silently unmake every measured table.
**Fix:** `--threads N` (0 = every core, clamped to 32) bounds the corpus render, the oracle and `--blss-eval`'s per-method loop; it is a wall-clock knob and nothing else — the same `--seed` must write a byte-identical `blss.net` AND a character-identical eval table at any thread count. The check is two runs and `md5sum`:

```bash
build/tyrax-editor --blss-train examples/procedural --frames 156 --epochs 400 \
    --all-shots --threads 1 -o /tmp/t1.net
build/tyrax-editor --blss-train examples/procedural --frames 156 --epochs 400 \
    --all-shots            -o /tmp/auto.net
md5sum /tmp/t1.net /tmp/auto.net    # must be identical
```

The stronger form builds the commit *before* the change into its own worktree and checks its `blss.net` matches too. Use `--frames 78 --epochs 200` for a faster smoke check. All three verbs end with a `blss: timing` phase line (`corpus X, oracle Y, fit Z`; `corpus X, eval Y`; `corpus X, oracle Y, folds Z`); per-shot corpus lines report **cpu ms, not wall ms**.

## `--blss-eval` says the feature is not worth it

**Cause:** `--blss-eval <projectDir>` is the "should this project have the feature on at all" check: the **oracle** row is the scene's ceiling, and on some scenes it is +0.00 dB.
**Fix:** it needs no trained net for that — with no `-i` and no `blss.net` to find it runs net-free, drops the `BLSS (trained)` row and still prints the verdict, exit 0. (`-i` IS required when checking twin parity, which is a claim about that row.) Two lines are printed for a caller: `[blss] verdict headroom=… passes=… bilinear=… oracle=… native=…` from every `--blss-eval`, and `[blss] fold k of n` from `--cv`. Never quote a plain `--blss-eval`'s held-out column — use `--cv`.

## `--blss-eval` with several positionals is a union corpus

**Cause:** `--blss-eval <a> <b> bestiary --cv --cv-groups` is leave-one-PROJECT-out: the held-out shot's whole project is removed from the training set, the only form of "can I ship one net" that means anything.
**Fix:** plain `--cv` holds out one shot and trains on eleven other moves of the same scene (docs/neural-upscaler.md, "Can one net ship for every project?").

## `--vu-check` passes but the microcode is wrong

**Cause:** the check stages VU1 memory itself, so a term whose staged inputs make it evaluate to zero is compared by comparing nothing (that happened to the spot light for its whole life — every `w` lane of its three quadwords was left at 0, and all of `invRange2`/`cosCut2`/`invSoft` are `w` lanes).
**Fix:** run `--vu-check` after ANY change under `vendor/tyra/.../*.vclpp` or to `src/vugen.cpp`; it parses all 25 shipped microprograms and diffs every quadword of the GIF packet the GS would receive. When you add, remove or GATE a term, falsify it first: invert the branch or zero the term in the HANDWRITTEN program (no editor rebuild needed — that side is read off disk) and confirm it reports `DIFFERENT` (docs/vu-framework.md).

## `--vu-check` reports DIFFERENT after an engine swap

**Cause:** both sides of every comparison must come from ONE commit — the generated side is compiled into the binary, the handwritten side is read off disk. `git archive <rev> vendor/tyra/engine` + `--vu-check <thatDir>` swaps exactly one half and MANUFACTURES failures against a stale exe (measured: 7 DIFFERENT programs plus the matcap identity-at-zero, all of which pass when each half runs against its own peer).
**Fix:** keep the 2x2 — old and new binary against old and new engine. Both diagonals passing means skew, not a bug. The check now says so itself (`note: FOREIGN engine` / `note: ... is NEWER than this executable`). Check `git log -1` and the exe's mtime before trusting a run from `D:\tyra-editor`.

## `--vu-replay` cannot resolve a flush, or does nothing

**Cause:** only the LAST mesh of a chain can be replayed (a terrain flush with fourteen chunks is not resolvable), and **a project with no flow-graph node has no devkit layer at all** — `live_debug.gen.cpp` compiles to an empty TU and the capture button does nothing (the zero-cost rule working, not a broken feature).
**Fix:** `--vu-replay` reconstructs the input from `bin/vucap.bin` and diffs its own output against the console's — the only layer that checks the simulator against REAL hardware. `examples/vu-lab` is the fixture built for it. Use the Debugger's flush picker to capture a flush carrying ONE mesh.

## Capturing and decoding a VU1 packet

**Cause:** a `--vu-check`/`--vu-replay` miss needs the raw chain.
**Fix:** arm via *Debugger > VU > Capture VU1 packet*, `livedbg.cmd` flags bit 3, or `arm-vucap.py` (`VERSION = 2`); `TYRAX --dump-vucap <projectDir> [--full] [--peek <qw>[,n]]` decodes it (default shows the first four staged packets x four vertices, useless for diffing; `--full` prints all, `--peek` prints raw data-memory quadwords as floats + words). **Quadwords 1016..1023 are free** in the static pipeline's map: code above `begin:` runs once per activation while `begin:` loops per batch, so a slot pointer set up above `begin:` turns them into a per-batch ring — otherwise every `--peek` reports the LAST batch. **The responder exists only when the project has instrumentable flow-graph nodes** (`liveDebugOn = liveDebugEnabled && !syms.nodes.empty()` — by default `live_debug.gen.cpp` is a ~220-byte comment); add one node (`--apply-graph <dir> <object> graph.json` with `OnStart` -> `Log`) and rebuild. A stale `CMD_VERSION` arms nothing silently (the constant lives in the generated runtime, currently `2U`, `src/templates.cpp`); delete `livedbg.cmd` before booting or a leftover command eats the first capture. Run `TYRAX --help` for the full flag list.
