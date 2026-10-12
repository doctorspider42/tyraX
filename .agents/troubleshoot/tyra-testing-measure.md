# troubleshoot/tyra-testing-measure.md

Measuring a frame — pixel arithmetic, the frame-timing rig and the stability gate. Symptom first.

## A screen effect "cannot be seen" and one that is not happening look identical

**Cause:** a Camera Shake at amplitude 0.05 measured **0 pixels differing** across four captures 250 ms apart — the ease-out cuts it to 0.03, and a 3 cm camera translation is sub-pixel at 512x448 with nothing closer than 5.6 m.
**Fix:** before debugging a subtle visual, do the arithmetic on how many PIXELS the effect is worth at the render resolution, and re-run the fixture with the effect turned up past that (at amplitude 2.0 the horizon swept 215 px and 545k pixels differed). The flip side: a fixture whose frames are byte-identical between captures is a clean instrument — that zero is what lets a 200-pixel horizon shift mean something.

## A capture is worth more when it is measured

**Cause:** eyeballing a PNG is not evidence.
**Fix:** read pixel ROWS out of it rather than eyeballing — plateau widths and adjacent-step sizes told flat shading apart from Gouraud on a cylinder, and two engine builds A/B'd from a frozen fixture came out byte-identical. A few lines of PIL will do it. Driving PCSX2 by hand inherits none of the launcher's setup: the Runner is what forces `HostFs` and the USB ports, so run through the editor at least once first, and set `Renderer = 13` (software) while PCSX2 is **closed** (it rewrites the file on exit).

## When an A/B compares two objects in ONE frame, prove which is which

**Cause:** the screen's X runs opposite to world X in the generated game (a Player at yaw 0 looks along +Z), so "the left one" is the object at *positive* X. An hour went into a banding hypothesis about the wrong cylinder.
**Fix:** the cheap disambiguator — force one object's colour to something absurd for a single run and see which one changes.

## A VU1 TIMING hazard is invisible in PCSX2 under EVERY renderer

**Cause:** PCSX2's VU does not model the pipeline hazards the hardware has, so a microprogram that reads a register one row too early runs *correctly* in the emulator (measured: a branch at a label whose condition was produced in a jump's delay slot clipped against the wrong frustum planes on a real PS2 while the same ELF rendered clean in PCSX2 and passed a value oracle over 277 traces).
**Fix:** if a change touches VU scheduling, latency or padding, PCSX2 is a smoke test and the console is the verdict. Every value oracle is blind to it by construction. Symptom vocabulary for the related ADC-bit class: **stray smeared triangles at screen edges**, which the HW renderer also masks.

## A temporal effect needs a SETTLE test, not a frame diff

**Cause:** "the two captures are identical" does not mean the picture is frozen — a heavy trail over a repeating pattern averages it FLAT, and a long trail is indistinguishable from a freeze while the camera moves.
**Fix:** ask whether the picture CATCHES UP: drive the motion, `neutral`, wait a few seconds, capture twice. A settled frame that is SHARP is a trail; one that is smeared (or still showing the boot splash thousands of frames in) is a freeze. Inspect static high-contrast HUD text after the idle clear: a diagonal down-right tail means the 1:1 history sprite sampled texel boundaries instead of centres. Motion blur's default idle clear is ONE sharp frame after about 0.12 s below its meaningful-motion threshold, not a permanent zero: after the settle capture, keep the camera parked and move an object across it — the object must still trail. Test the option-off arm separately; test at a forced low frame rate or on hardware too (a per-frame displacement threshold passed at 60 FPS in PCSX2 and failed at ~30 FPS on PS2 because pad drift doubled per frame).

## Rendering correctness: use the software renderer

**Cause:** the HW renderer masks GS raster-window wrap bugs that real hardware shows.
**Fix:** switch PCSX2 to the **software renderer** before judging visuals. Give the game a few seconds to reach a steady state, then screenshot; compare against a known-good screenshot when hunting regressions. An **axis-aligned walk over the flat checkerboard terrain is nearly invisible to a pixel diff** (translating along the grid maps the repeating pattern onto itself — a 2.5 s forward hold changed an 11-pixel band); turn the camera first (right stick), then walk (~150k px).

## A strict pixel A/B between two RUNS usually cannot work

**Cause:** an orbiting or auto-spinning camera is at a different phase in each boot, and the interlaced FIELD modes alternate fields, so window captures never line up (a "1.7M pixels differ" result is almost always this).
**Fix:** freeze the camera/pose in the fixture, or better **compare the DATA on the console**: log an FNV hash (and the first few raw bit patterns) from inside the game and diff the log. For a format change you can load BOTH formats in one run and compare them against each other.

## Performance: PCSX2's FPS overlay

**Cause:** you need to know where a frame is spent.
**Fix:** software renderer, 3+ samples. Full PAL frame rate is 50 FPS. Frames are almost always EE-bound — `endFrame` is mostly vsync idle, so do not read a saving there as render work. For *where*, enable the built-in **frame profiler** (debug build profile + *Preferences > Build > Show frame profiler*): per-phase EE ms on the HUD (whole frame / scene / usable-highlight / particles). For **GS VRAM**, a debug build prints `VRAMSTAT` lines into `bin/log.txt` (texture binds/hits/uploads/re-uploads/evictions, resident count, free MB, largest free block) — see docs/gs-vram.md. Parallel worktree sessions each run their own emulator, so pass **`-ProcessId <pid>`** with the pid whose `-elf` path is your project. Since 1.22.0 `--build --run` closes only the instance whose `-elf` names your own ELF.

## An ABSOLUTE frame time: use `TYRA_FRAME_PROFILE 2`, not 1

**Cause:** Level 1 carries the packet-structure walker and pipeline telemetry and is not transparent — HUD SCENE 13.21 ms uninstrumented, 17.70 at level 1, 13.04 at level 2 on the same console pose.
**Fix:** `TYRA_FRAME_PROFILE` (default 0) gives engine counters and a once-a-second `FRAMETIME` line with mean/median/p95, plus a raw per-frame dump so two runs compare as PAIRED samples. Prove transparency in every report by matching SCENE against an uninstrumented boot. Read `pre`/`stall`/`period`/`miss` as well as `work`: `work` starts at `beginFrame()`, so a frame can miss its field with `over20=0`. Drive the camera from a **frame index in an object script**, not `--pad`; turn Live Link / Live Debugger / Live Logic / Remote Pad / Time Machine **off**; and **run the page's GS fill calibration before quoting any PCSX2 number about GS cost** — measured, PCSX2 under-reports fill by **76x** (docs/profiling.md).

## Testing whether a picture is STILL needs a period-2 test, not a diff

**Cause:** three instruments in a row got this wrong.
**Fix:** freeze the camera **and every emitter**; point it at **textured** content; capture **back to back** rather than on a stride (an even stride lands on the same phase forever); **never `-Trim`** and take the crop once; report a **cross-correlation lag** as well as a pixel count; and cluster by pairwise difference (two balanced clusters, near-zero within and large between, IS the alternation). Check the verdict against a labelled A/B/C where you already know the answer. On Windows select the PCSX2 instance by the project on its command line (`Get-CimInstance Win32_Process ... CommandLine -like "*<project>*"`, then `-ProcessId` that pid). A GDI grab reads the SCREEN, so an occluded PCSX2 yields a plausible, entirely fictional "stable" table — capture with **`-PrintWindow`** and diff frame 0 of one arm against frame 0 of another. Freezing the camera and the emitters is also what makes this instrument blind to every fault that only exists while the picture is moving — see `tyra-testing-motion-gate.md`.

## Fixture hygiene before any A/B

**Cause:** a fixture can measure a different game and say nothing.
**Fix:** **hash the two ELFs before believing a null** — swapping only `src/terrain_game.cpp` between compiles lets `Copy-Item`/`cp -p`/`git checkout` keep the old mtime, so `make` does nothing and every counter delta reads 0.0; stamp the file, delete the matching `.o` in the project AND the `-Cache` tree, then hash. `tools/instrument-frame-cost.py` rewrites the file itself, which is why that flow does not hit it. A **capture hash is not a fixture check**: an old editor baked `stripRun = 72u` yet garage day still hashed `415f970f...73f1e`, because a strip run changes how a surface is cut, not which pixels it covers. For a frozen-pose A/B you can also drive the camera from a global script (`TYRA_SCRIPT`) instead of the pad — `examples/large-terrain/authoring/engine-generality-2026-09-16/zz_vantage.cpp` is the frame-indexed shape.
