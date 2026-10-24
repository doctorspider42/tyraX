# VU1 microprograms and the upscaler (BLSS) — host-side layers

Read when: you change `*.vclpp`, `src/vugen.cpp`, capture/decode a VU1 packet, or verify the upscaler (BLSS) - its interlock, its pictures or its frame times.

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

## The upscaler (docs/upscaler.md)

There is no host-side BLSS layer any more: the neural half and its `--blss-train/--blss-eval/--blss-emit/--blss-coverage` verbs were removed in 2.1.0. What is left to verify is codegen (layer 1), the picture (PCSX2) and the frame time (console only - PCSX2 under-reports GS fill ~76x).

- **Codegen**: `--refresh-gen` a scratch copy and grep `inc/scene_data.hpp` for `BLSS_PARTICLES` / `BLSS_ENABLED` / the per-scene table, and the game sources for `beginParticles` (Particles only) or the `beginScene/endScene/composite` bracket (Whole 3D scene). A Set Upscaler (BLSS) node anywhere must make the project mixed (both rasters reserved).
- **Pictures, frozen camera, on `examples/upscaler-lab`**: copy it out, drop the director object (its On Start plays the "haze tour" sequence) and set the Player's walk/look speed 0, then capture each arm (Off / Whole 3D scene / Particles only) in PCSX2 at progressive 480p with the debug HUD off (docs/profiling.md, "The stability gate"). The emitters still move, so compare pictures by eye or by structure, not byte-for-byte.
- **Frame times**: console only, `TYRA_FRAME_PROFILE` + paired `FTRAW` windows (performance-and-profiling.md; docs/profiling.md "The measurement protocol").

- *Whole 3D scene* x depth of field / linked portals / split view / frame extrapolation is a build-time `#error` living in one generated TU (Particles only clashes with nothing): `--refresh-gen` writes `src/gen/blss_interlock.gen.cpp` (prints `[blss] BUILD WILL BE REFUSED`), deletes it once the clash is gone. To force the refused combo in a scratch fixture, delete the file and build in the container by hand (`--build` would regenerate it):
```powershell
Remove-Item "$P\src\gen\blss_interlock.gen.cpp"
docker compose --project-directory $P -f "$P\docker-compose.yml" up -d
docker compose ... exec -T compiler sh -c "rsync -a --delete --exclude=.git --exclude=obj --exclude=bin /host/ /src/"
docker compose ... exec -T compiler sh -c 'cd /src && make -j$(nproc)'
docker compose ... exec -T compiler sh -c "rsync -ac --include=*/ --include=bin/** --exclude=* /src/ /host/"
```
- Generated-text apostrophes: an unpaired `'`/`"` after `#error` makes GCC warn *missing terminating ' character* in every TU (the BLSS interlock once put it on all fourteen). Guard: `errorSafe()` in templates.cpp; use plain words.
- Adaptive BLSS needs a hardware decision gate: see performance-and-profiling.md.
