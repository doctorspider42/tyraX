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

## Incremental second-model follow-up

A private sibling adds the public Pica Turbo model/definition as a second real vehicle at x=-2.3,z=8 beside/ahead of the Ravager. The same S-road geometry, analytic floor and effects/audio exclusions remain. Ordinary drivetrain/suspension/contact and chase camera are retained. Phase1 throttle is shortened to30 frames with a z2 cap, then normal braking, to keep both models visible: this follow-up no longer traverses the bends, and its changed controller is included/unpriced. No causal incremental vehicle cost can be inferred by subtracting the original road-driving arm.

Native source/object/ELF binding and30 synthetic parser controls passed. The emulator completed5400 loops. All six cold accepted witnesses distinguish both models' body and wheel geometry through synchronous definition scopes; the shared wheel bag pointer is never used to infer identity. At each witness, Ravager/Pica body inputs contain3660/2964 vertices and each wheel carrier1200, with positive road geometry. Actual short driver movement and stationary second-car pose are required. Root reviewed three exact owned-window images showing both complete distinct models. These are sparse acceptance/image witnesses, not every-frame raster or scanout proof; emulator HUD/timing is not physical PS2 pricing.

The first fresh two-car start received no loadelf/output after a network-reset client timeout; its exact owned client was stopped and the attempt rejected. After the user power-cycled PS2, the unchanged ELF completed5400 physical loops cleanly. Records are under `two-cars/`.

### Physical two-model result

| Window | Non-pacing loop work | Entry period | Rendered / synthetic completions |
|---|---:|---:|---:|
| Both parked | 1.8952 ms | 16.6833 ms | 320 / 0 |
| Short driver throttle/braking window | 2.8012 ms | 16.6833 ms | 320 / 0 |
| Both parked after driver movement | 1.8942 ms | 16.6834 ms | 320 / 0 |

All measured windows remain approximately59.94Hz. Across384 raw entry samples, no period exceeds25ms; maximums are16.725/16.724/16.724ms. The largest sampled non-pacing loop in the short-driving arm is2.874ms. All six cold contexts preserve distinct definitions and stationary Pica pose; actual Ravager movement is established. Positive post-main-cull body/wheel counts for both definitions sum exactly to all car geometry. The user explicitly confirms both vehicles look correct. Sparse contexts, images and sampled cadence do not cover every frame or prove physical scanout/input latency. Non-pacing work is the ordinary loop, not isolated EE arithmetic.

Two nearby different models plus normal road contact remain comfortably below the frame budget in this stripped fixture. The full S road is present but bends are not traversed in this shortened route; texture/terrain/night effects/audio and large vehicle crowds remain outside scope. A causal incremental two-car price against the previous long bend route is unsupported. The console is left running with Ravager and Pica.

## Reset transition audit

The completed games and the later stop/reset/new-start failures are separate observations. A read-only audit of the installed host client finds a blocking TCP18193 connect before it creates/sends the UDP18194 reset command. The `-t10` option does not bound that connect. Reset's later mainloop timeout0 means immediate return, so a12-second process timeout is not ordinary reset waiting and does not establish packet delivery. The client also lacks a safe abort on failed sends. The exact observed failure stage remains unproven without a packet/host-stack witness.

Local console artifacts contain different r6/r7 identities; the actual resident memory-card image is unknown. A source-level IOP reconnect race is another unqualified suspect. A UDP-only request could isolate delivery from TCP file-service readiness, but no such request was sent and no reset fix is claimed. The successful two-car game stays running. See [the report](two-cars/reset-audit/report.md), installed-client disassemblies and pinned identities for details.
