# VU1 microprograms and the neural upscaler (BLSS) — host-side layers

Read when: you change `*.vclpp`, `src/vugen.cpp`, capture/decode a VU1 packet, or work on BLSS training/eval/coverage.

## VU1 (docs/vu-framework.md)

```
TYRAX --vu-check [engineDir]          # exit 0 = every program parsed AND every generated one matches its handwritten twin bit for bit
TYRAX --vu-list <file.vclpp> [engineDir]   # expand + disassemble one microprogram
TYRAX --vu-emit <outDir> [engineDir]  # generate .vclpp + EE program classes
TYRAX --vu-replay <projectDir> [engineDir] # re-run a console VU1 capture on the host and diff
TYRAX --dump-vucap <projectDir> [--full] [--peek <qw>[,n]]
python .agents/skills/tyra-testing/scripts/arm-vucap.py <projectDir> <flushIndex> [seq]
```

- **Run `--vu-check` after ANY change under `vendor/tyra/.../*.vclpp` or `src/vugen.cpp`.** Closest thing to a unit test: parses all shipped microprograms (32 at 029d84fb6), runs the described ones against handwritten originals on random input, diffs every GIF-packet quadword; also an RGB SH numeric oracle (signed chromatic coefficients, opposing normals, rotation, both lighting modes, negative output, saturation). Milliseconds. Models no cycle timing and no MAC/STATUS flags.
- **A green `--vu-check` means something only if you can make it red.** It stages VU1 memory itself; a term whose staged inputs are zero compares nothing (the spot light's `w` lanes — `invRange2`/`cosCut2`/`invSoft` — were 0 for its whole life; 21 ops/vertex unverified). When adding/removing/GATING a term, falsify first: invert the branch or zero the term in the HANDWRITTEN program (read off disk, no rebuild) → must report `DIFFERENT` (docs/vu-framework.md "The check that makes this more than a plausible story").
- **Rebuild before believing a failure; never attribute by swapping the engine alone.** Generated side is compiled into the binary, handwritten side read off disk → `git archive <rev> vendor/tyra/engine` + `--vu-check <thatDir>` swaps one half and manufactures failures (measured: 7 DIFFERENT + matcap identity-at-zero, all passing against their own peer). Output warns `note: FOREIGN engine` / `note: ... is NEWER than this executable`. Do the 2x2 (old/new binary x old/new engine); both diagonals passing = skew. A parallel checkout (e.g. `D:\tyra-editor`) often has a days-old `build/`: check `git log -1` and exe mtime.
- `--vu-check` does not validate EE source pointers: lighting DMA lifetime (dangling REF to stack data) needs a temporal check — fixed pose, compare a frame sequence (exclude FPS HUD), then animate/walk. Exercise real packet submission.
- **VU timing hazards are invisible in PCSX2 under every renderer** (no pipeline-hazard model). A branch whose condition came from a jump's delay slot clipped against wrong planes on real PS2 (triangles appearing/occluding, camera-dependent) while PCSX2 was clean, zero asserts, and a value oracle passed 277 traces / 2631 branch conditions. Any change to VU scheduling/latency/padding: PCSX2 = smoke test, console = verdict. ADC-bit class symptom: **stray smeared triangles at screen edges** (HW renderer masks it too).

### `--vu-replay`

Only layer checking the simulator against real hardware: rebuilds input from `bin/vucap.bin`, diffs against the console's output. Fixture: `examples/vu-lab`. Only the LAST mesh of a chain replays — use the Debugger's flush picker to capture a ONE-mesh flush (a 14-chunk terrain flush is not resolvable).

### Capturing a packet

- Arm via *Debugger > VU > Capture VU1 packet*, `livedbg.cmd` flags bit 3, or `arm-vucap.py` (writes the same `bin/livedbg.cmd`). Bit 4 = index in bits 8–23; without bit 4 each capture walks to the next flush (PROGRESS 201). Name the flush index — "the next packet" is a different draw each time.
- **Responder exists only when the project has instrumentable flow-graph nodes** (`liveDebugOn = liveDebugEnabled && !syms.nodes.empty()`). Bare scratch project → no devkit layer (`live_debug.gen.cpp` is a ~220-byte comment). Add one node (`--apply-graph <dir> <object> graph.json` with `OnStart` → `Log`), rebuild.
- **Stale `CMD_VERSION` arms nothing silently** — game drops it (`if (magic != CMD_MAGIC || version != CMD_VERSION) return;`), script says "armed", `--dump-vucap` says "no capture yet" (console AND emulator). Constant lives in the generated runtime (`src/templates.cpp`, currently `2U`); `arm-vucap.py` `VERSION = 2`. One landed arming sets `vuCapPending` and refuses later ones until the write finishes.
- **Delete `livedbg.cmd` before booting a fixture** — a leftover command is applied at boot and eats the first capture (looks like an off-by-one).
- `--dump-vucap` prints DMA tags, VIF codes, UNPACK destinations, MSCAL entry, meshes in the flush, which bag flush, live render resolution, amber findings, and the staged GIF packets. Healthy: `UNPACK V4_32 -> VU1 addr 0` for scales, referenced `num=21` blocks for double-buffered vertex data, model-space positions w=1.0. `refe qwc=32789` or float garbage = walker lost sync (by-reference rule, tyra-engine-dev).
- Plain decode shows the first four staged packets x four vertices — right for reading, useless for diffing. `--full` prints all; `--peek <qw>[,n]` prints raw data-memory quadwords as floats + words. **Quadwords 1016..1023 are free** in the static pipeline's map; code ABOVE `begin:` runs once per activation, `begin:` loops per batch → a slot pointer set up above `begin:` turns them into a per-batch ring (otherwise every peek reports the LAST batch, where builds usually agree). Differences deep in the list show in header counts.
- Consecutive captures of the same draw have identical LENGTH (same bag, fixed 16 KiB tail) — distinguish by mtime / header frame.

## Neural upscaler (docs/neural-upscaler.md)

```
TYRAX --blss-train [<projectDir>] [--all-shots] [--threads N] [--frames N] [--epochs N] [-o out.net]
TYRAX --blss-eval  [<projectDir>...] [-i net] [--cv [--cv-groups]] [--features [--probe "<line>"]] [--dump <dir>]
TYRAX --blss-emit  [-i blss.net] [-o inc/blss_net.gen.hpp]
TYRAX --blss-coverage <projectDir> [--frames N] [--raster N] [--threads N] [--out WxH] [--verbose]
```
Shared: `--assets <dir> --seed N --sharpen K --scale-1x2 --weight-decay W --standardise --flicker-weight W --fill-weight W --drop-feature <name>[,...] --no-package-split --cv-seeds N --cv-folds N` (see `--help`). BLSS verbs are unbuffered (progress through pipes).

- **`--blss-coverage`** = speed half of "turn BLSS on?", headless twin of *Will the frame get faster?* (same `blss::measureCoverage`, ~1 s). Prints per-shot/overall mean/p95 coverage with **geometry/emitter split**, verdict, `[blss] coverage …` lines. Use the project's own raster (default) or the verb and button answer different questions. Reads `blssNetwork` and prices for that mode — plain mode's EE bill is 1/7 of neural, break-even **2.6** vs **13.1** — and prints the other mode's line too (they often disagree).
  - It is an overdraw **index** that over-states scale by about a third, not ms: first run 72.63 vs 58.7 blended-pass equivalents (hardware anchor, `examples/upscaler-lab`); located as a constant emitter-term scale error. Under the fixture's parked gameplay camera it reads **78.99**; counted/measured ratio 1.35 / 1.26 / 1.27 / 1.36 at haze 6/4/2/0 banks (docs/neural-upscaler.md "The overdraw count is an INDEX"; docs/profiling.md "Calibrating the speed model against hardware").
- **Eight flags measure a configuration no project can ask for, and each prints a line saying so:** `--tile N`, `--scale WxH` (engine generic; `blssScale` only names 2x2 and 1x2), `--act-table N`, `--no-anim`, `--still` (freeze each shot at one camera/pose, only jitter phase advances — period-2 fixture; refused by `--blss-train` and `--cv`), `--proxy-budget` (5th twin-contract rule, off both sides), `--emitter-proxy` (6th rule; engine half `TYRA_BLSS_EMITTER_PROXY`; makes the channels DESCRIBE particles but the corpus renderer DRAWS none → PSNR prices description vs particle-free truth; read `--features` and a console `BLSSFEAT` line via `--probe`, not dB), `--ignore-shot-plan` (six automatic moves, takes on, equal frame share). An unrecorded configuration produced five wrong published numbers, plus a sixth (ceiling at jitter ON, margins at jitter OFF).
- **Several positionals = UNION corpus; `bestiary` is a member.** `--blss-eval <a> <b> bestiary --cv --cv-groups` = leave-one-PROJECT-out (the only honest "one net for all"). Plain `--cv` holds out one shot and trains on eleven moves of the same scene ("Can one net ship for every project?").
- **Verify parallel-phase changes with `--threads`** (0 = every core, clamped to 32; bounds corpus render, oracle, eval loop; wall-clock only). Same `--seed` must give byte-identical `blss.net` and character-identical eval table at any thread count:
```bash
build/tyrax-editor --blss-train examples/procedural --frames 156 --epochs 400 --all-shots --threads 1 -o /tmp/t1.net
build/tyrax-editor --blss-train examples/procedural --frames 156 --epochs 400 --all-shots -o /tmp/auto.net
md5sum /tmp/t1.net /tmp/auto.net    # must be identical
```
  Stronger: build the pre-change commit in its own worktree, its `blss.net` must match too (every published table is a seeded run). 6 cores: ~68 s at `--threads 1`, ~18 s auto; smoke: `--frames 78 --epochs 200`.
- Every verb ends with a `blss: timing` line (`corpus X, oracle Y, fit Z` / `corpus X, eval Y` / `corpus X, oracle Y, folds Z` for `--cv`). Per-shot corpus lines are **cpu ms, not wall ms**.
- `--blss-eval <dir>` is also the "feature on at all?" check: **oracle** row = scene ceiling (sometimes +0.00 dB). Needs no net: without `-i` and no `blss.net` it runs net-free, drops `BLSS (trained)`, still prints verdict, exit 0 (so `-i` IS required for twin-parity claims). Caller lines: `[blss] verdict headroom=… passes=… bilinear=… oracle=… native=…`, and `[blss] fold k of n` from `--cv`. Never quote a plain eval's held-out column — use `--cv`.
- BLSS x frame extrapolation is a build-time `#error` living in one generated TU: `--refresh-gen` writes `src/gen/blss_interlock.gen.cpp` (prints `[blss] BUILD WILL BE REFUSED`), deletes it once the clash is gone. To force the refused combo in a scratch fixture, delete the file and build in the container by hand (`--build` would regenerate it):
```powershell
Remove-Item "$P\src\gen\blss_interlock.gen.cpp"
docker compose --project-directory $P -f "$P\docker-compose.yml" up -d
docker compose ... exec -T compiler sh -c "rsync -a --delete --exclude=.git --exclude=obj --exclude=bin /host/ /src/"
docker compose ... exec -T compiler sh -c 'cd /src && make -j$(nproc)'
docker compose ... exec -T compiler sh -c "rsync -ac --include=*/ --include=bin/** --exclude=* /src/ /host/"
```
- Generated-text apostrophes: an unpaired `'`/`"` after `#error` makes GCC warn *missing terminating ' character* in every TU (the BLSS interlock once put it on all fourteen). Guard: `errorSafe()` in templates.cpp; use plain words.
- Adaptive BLSS needs a hardware decision gate: see performance-and-profiling.md.
