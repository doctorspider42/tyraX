# Fresh empty one-car isolation, 2026-10-06

Current public editor generated a new empty/no-terrain project with one Ravager and a nonrendering camera/driver entity. No map objects, lights, sky, portals, music or SFX are present. A private analytic y=0 collision floor has no geometry or draw calls; eight cosmetic vehicle FX bodies are explicitly cut. Main vehicle geometry, suspension and driving physics remain. Preparation files record these boundaries and native source/object/ELF identity; binaries, SDK and assets are hash-only.

The emulator completed all 5400 loops, 15 chunks and 384 raw samples. All six cold render witnesses accepted four bags / 4860 input vertices with zero billboards; actual driving-phase position changed and vehicle height stayed finite. Strict parser and 16 synthetic controls passed. These are input acceptance and protocol checks, not proof of every displayed pixel or physical PS2 performance.

Root reviewed all three exact-window screenshots at original resolution: the full car is centered and visible before driving, after movement, and after completion. This bounds three captures, not every frame. Do not infer stable 60fps from emulator HUD or elapsed values. Physical PS2 completed 5400 loops after a power cycle. The first start omitted default preloaded HUD resources and is rejected; the same ELF was repeated with complete resource staging, no missing-asset warnings. The authored HUD remains disabled. Human TV appearance is still pending.

This is a stripped diagnostic, not equivalent-output pricing against vehicle-playground. Common quieting/controller/sampler/witness overhead remains unpriced. Static, driving and post-drive windows are one arm, not matched A/B savings.

## Physical result

| Window | Non-pacing loop work | Entry period | Rendered / synthetic completions |
|---|---:|---:|---:|
| Initial parked | 1.1858 ms | 16.683334 ms | 320 / 0 |
| Short straight throttle | 1.8252 ms | 16.683335 ms | 320 / 0 |
| Post-drive neutral | 1.1856 ms | 16.683333 ms | 320 / 0 |

All three measured windows show approximately59.94Hz ordinary cadence. Across128 raw entry periods per phase, maximums were16.729/16.730/16.713ms, with zero periods above25ms. The parser's rounded16.68ms threshold is not a dropped-field counter. This is bounded observed cadence, not a physical scanout oracle or coverage of every frame. Actual driving moved the car, all six cold witnesses accepted4 bags/4860 vertices, and synthetic completion count is zero. Non-pacing loop work includes the game's ordinary work/waits; it is not isolated EE instruction time or a VU/GS bill.

The minimal car/physics/pipeline workload has ample observed headroom. This removes a universal one-car/presentation bottleneck as an explanation of full-night performance, while leaving populated-scene geometry, effects, audio and interactions to be isolated. Do not subtract these separate-ELF values from historical playground timings as an equivalent-output saving. The console remains running for visual review.
