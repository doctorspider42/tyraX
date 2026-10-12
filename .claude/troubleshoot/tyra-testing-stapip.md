# troubleshoot/tyra-testing-stapip.md

The static pipeline's retained commands, content version, baked VIF stream and static submission batches. Symptom first.

## Retained static command data acceptance (1.96)

**Cause:** the A/B is one knob in one worktree, one project directory: `TYRA_STAPIP_RETAINED_COMMANDS` in `vendor/tyra/engine/inc/renderer/3d/pipeline/static/core/stapip_qbuffer_renderer.hpp`, 1 against 0. The engine is rebuilt by the ordinary `--build` because the header is an engine source; the generated game, the assets and every other number are common mode.
**Fix:** three checks, in this order, and the first two are the ones that mean something:
- **The packet must be byte-identical, so the picture must be.** Frozen camera, `--capture-frame` three times per arm, confirm the three repeats of each arm are byte-identical to each other BEFORE comparing arms. Anything non-zero across arms is a bug in the capture-and-replay.
- **The counts must not move.** VU1 packages, submitted vertices and packet flushes come from the same bags producing the same packets, so `FTCLIP`'s `verts` and `flush` and the frame-cost CSV's submission count must be identical between arms. The new counters are `STAPIPRET retained=N rebuilt=M per frame, cache=K KB` (a debug engine logs them every 300 frames) or `StaPipCore::takeRetainedCommandHits/Builds` from the game.
- **Then, and only then, the time.** And **PCSX2 cannot price this change**: it emulates no EE data cache, while the change trades computing bytes for reading them out of a cold ~128 KB arena. Its delta is an upper bound on the hardware saving and must be labelled as one.

Stress the invalidation deliberately — force texture evictions with a batch pending, switch the pipeline away and back, cross an LOD threshold in both directions, reload the scene, and move the camera over hundreds of frames. `rebuilt` per frame is the instrument: near zero on a parked pose, spiking exactly where geometry changed.

## The content-version contract, and its negative test

**Cause:** `BagArray<T>` (docs/bag-content-version.md) is what makes the baked stream's invalidation structural: every array the generated game hands a `StaPipBag` is one, `data()` is const, and every mutation stamps `contentVersion`. The whole argument is that a raw write does not compile, so the check is a NEGATIVE one and needs no PS2 toolchain.
**Fix:**

```bash
tools/bag-array-enforcement.sh <project>/inc/bag_array.gen.hpp
```

Seven cases against the REAL generated header (`TYRAX_BAG_ARRAY_NO_TYRA` drops only the four `bind()` overloads): two positive — the sanctioned API compiles, and the stamp MOVES on every mutation — and five escapes that must each be refused. **Falsify it before believing it**: make `data()` non-const in a copy of the header and it must go red on exactly the two cases that property guards (measured: `passed 5, failed 2`). The positive control is load bearing too.

## Two traps this conversion produced

**Cause:** both read as something else.
**Fix:**
- **`bind()` aims the bag at `data()`, and an EMPTY vector's `data()` may be null**, which the engine rejects with *"Vertices are required in 3D render bag!"* at the first frame. Size the ring before binding, and before the FIRST updater/builder too. Vehicle smoke clears dead slots, skid geometry may spawn in the physics update, and vehicle glow writes its lit-lamp geometry before checking whether its bag exists. PCSX2 hid the write because its RAM starts at zero; hardware reported cause 3, `BadAddr 0`.
- **A new generated file must join `refreshGenerated`'s allowlist, not just the `--new` scaffold.** `inc/bag_array.gen.hpp` was written at creation and not on refresh, so every existing project (and every performance fixture, which is how it was caught) regenerated code that needs it and then failed to compile.

## Baked VIF stream acceptance (TYRA_STAPIP_BAKED_STREAM)

**Cause:** same shape as the retained-command A/B and one extra rule. `TYRA_STAPIP_BAKED_STREAM` in the same engine header, 1 against 0, one project directory, plus `TYRA_FRAME_PROFILE` and `TYRA_STAPIP_BAKED_REPORT` set to 1 in **both** arms (measurement flips — revert them before committing).
**Fix:**
- **The gate is the picture plus three counters.** `FTCLIP` gives `cull`/`clip`/`guard`, `verts` and `flush`, and `STAPIPBAKE` gives `chainQw` — the DMA chain quadwords the static pipeline hands VIF1 per frame, compiled into the control arm too. Nothing here is a millisecond.
- **That counter gate only works while the flush cadence is held fixed.** A run of packages under one `REF` tag cannot cross a packet flush boundary, so pinning `packetFlushes` pins the saving. Any change that moves the cadence must use the redesigned gate instead — docs/baked-stream-acceptance-gate.md: a canonical, NOP-stripped hash of the word stream VIF1 actually receives (decoded at the packet tap, with texture mutations interleaved in order) plus byte-identical pixels over a pose sweep. The `FTCLIP` routing counts become diagnostics that explain a hash failure.
- **A gate arm is never a timing arm.** The hash reads ~2.7 MB a frame and costs 10-25 ms; it is a separate ELF from anything you time.
- **Drive the Motor District fixture by its pose file, never with `--pad`.** `benchmark-district.py`'s sampler reads `bin/district-benchmark-pose.txt` every 30 frames after its 1440 measured frames, and writes `bin/district-benchmark.csv` at exactly that moment — the CSV appearing is the "it is safe to write to bin/ now" signal. Phase 0 is garage day, the only pose to compare pixels at.
- **Launch PCSX2 yourself** (`pcsx2-qt.exe -batch -nogui -logfile <yours> -elf <abs path>`), never `--build --run`; delete `bin/livedbg.cmd` and `bin/frame.tga` before the boot and between captures.
- **The stale-libtyra trap**: the feature is an engine header, so the ELF only moves if `libtyra` was re-archived. Check the build log for `Engine sources changed - rebuilding libtyra...` followed by an `elf-ar rcs bin/libtyra.a` line, and `sha256sum` the two ELFs — they MUST differ. A null A/B is only evidence if the two arms were two builds.
- **A MATCHING CAPTURE HASH IS NOT A FIXTURE CHECK.** This round's first pass borrowed an editor binary that predated the 72 -> 75 package ceiling, so `stripRun = 72u` and `cull=40175 ... verts=55332` instead of `38750 ... 55836`; its garage-day capture STILL hashed to the published `415f970f...73f1e` because the strip run changes how a surface is cut, not which pixels it covers. Check the SCENE: `ROADSTRIP scene 0 ... packages 470` and `TERRAINSTRIP ... vertices 588 packages 8`, plus `grep -n "stripRun = 7" <fixture>/src/terrain_game.cpp`. Build the editor from the worktree you are measuring, every time.
- **`STAPIPMISS` says WHY a bag rebuilt**: one counter per invalidation reason (`bbox`, `prim`, `streams`, `program`, `size`, `new`, `incomplete`) plus the biggest bag that moved. It splits `bbox=` from `content=` since the content-version contract: `bbox=N content=0` is a mesh that MOVED, `bbox=0 content=N` is one that was RE-SHADED. At garage day the district reads `bbox=300 content=600` over 300 frames and names the 2 280-vertex bag.
- **THE ADVERSARIAL ARM IS THE ONE THAT FINDS REAL DEFECTS, AND IT NEEDS NO CONTROL.** `TYRA_STAPIP_BAKED_VERIFY` rebuilds every block with the ordinary writers and compares, so it runs under `--keep-routes` with the traffic MOVING. `failed=0` is the acceptance (measured 2026-09-17: **169 843 blocks checked, `failed=0`** over ~12 600 frames). Enrich its MISMATCH line before hunting: the stream pointers plus the differing quadword's four floats named the caller in one run.
- **`--profile quiet-debug` turns Live Debugger OFF, so `--capture-frame` does not work on that fixture.** For the pixel half, flip `"liveDebug": true` in the fixture's `.tyra` for BOTH arms.

## Static submission batch acceptance (1.93)

**Cause:** a lower DMA count alone is not acceptance — the larger bag prototype reduced sends but increased pipeline waits.
**Fix:** use complete baked resource hashes, four parked day/night poses, warmed unarmed frame-cost rows, a second baseline boot and GS captures. Texture residency must be stressed separately: evict while a batch is pending and switch the pipeline away/back, then verify fresh frame progress, reuploads and intact textures. Night images contain animated star twinkle and lamp flicker; compare repeated same-build captures before calling a pixel delta a renderer regression. See docs/static-submission-batching.md.
