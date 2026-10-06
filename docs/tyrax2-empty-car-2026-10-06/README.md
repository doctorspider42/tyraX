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

## Incremental S-road follow-up

The next private sibling adds one ordinary public Road to the same stripped car scene: width10, seven XZ anchors(0,-15),(0,0),(0,16),(3,30),(-3,44),(0,60),(0,80), sample step1, no texture/junctions/dressing. The disabled-terrain analytic floor remains; normal road tessellation and indexed road-height queries provide the raised0.12 contact surface. No VU source changed. A bounded virtual-pad lookahead controller steers/brakes through the bends; its changed cost is included and unpriced, so this separate-ELF follow-up cannot price a causal road-only delta.

Native source/object/ELF binding and24 parser controls passed. The emulator completed5400 loops; all six cold witnesses show four paved wheels, grounded contact and separate positive post-main-cull road/car geometry acceptance. The driving endpoint reached past the second bend. Root reviewed three exact owned-window images showing the car and road. These are sparse witnesses, not every-frame wheel-path or scanout proof, and emulator timings do not price PS2.

The initial empty-map-to-road network reset client timed out; a bare fresh ELF start had empty stdout/no loadelf despite ping. That start is rejected and its exact owned client stopped. After the user power-cycled PS2, the unchanged road ELF completed5400 loops cleanly. Preparation/runtime records are under `road/`; no renderer failure follows from the rejected start.

### Physical S-road result

| Window | Non-pacing loop work | Entry period | Rendered / synthetic completions |
|---|---:|---:|---:|
| Initial parked | 1.3848 ms | 16.6833 ms | 320 / 0 |
| Steering/throttle/braking through both bends | 2.4046 ms | 16.6833 ms | 320 / 0 |
| Post-drive neutral | 1.3297 ms | 16.6833 ms | 320 / 0 |

All observed windows remain approximately59.94Hz. The384 raw entry samples have zero periods above25ms; per-phase maximums are16.731/16.713/16.713ms. The largest sampled driving non-pacing loop is3.651ms. All six cold witnesses show four paved wheels/grounded contact, positive road and car accepted geometry and no billboards; the driving endpoint is beyond both bends. This bounds the sampled windows and six sparse contexts, not every-frame trajectory or scanout. The user reports a normal car, uniformly white road and black empty surrounding, consistent with the deliberately untextured/no-sky fixture; no every-frame image certificate is implied.

The simple untextured S-road workload retains ample headroom, while a large textured road network, terrain, decorations, lighting and audio remain untested in this isolation ladder. The changed feedback-controller workload is included: historical empty-map differences are descriptive, not a matched causal road-only price. The console is left running with the road variant.
