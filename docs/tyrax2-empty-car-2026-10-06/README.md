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

## Two cars with one moving dynamic point light

A private sibling keeps the same two models, short controller, S-road, ambient, physics and ordinary sampler. One cyan/blue runtime point uses the public registration API after the ordinary scene registry update. Its position moves smoothly with live frame dt at y3, radius20; registration and sine/cosine work are included. There are zero authored light objects: this is an explicit private injection, with no pools, coronas, headlights, shadow passes, night sky or audio added. All-receiver policy remains enabled, so the ordinary road and car pick paths can use it.

Native source/object/archive/ELF binding and37 parser controls pass. Root reviewed exactly four changed source scopes, all469 source pins and three owned emulator raster captures; both complete cars and a cyan-lit road appear normal. Emulator5400-loop protocol passes and the owned process was stopped with subsequent PID absence confirmed. Its HUD/elapsed time is not physical pricing.

The physical PS2 run completes5400 loops with non-pacing loop work **2.6687/2.9214/2.6660ms** for parked/short-drive/post-drive. All measured windows retain approximately16.6833ms entry cadence (~59.94Hz); each has320 rendered and zero synthetic completions. All384 raw entry samples stay below25ms. Each of the six cold frames records one enabled moving point, actual selection for both bodies and wheel batches, and the effective local-filtered negative colour-VU spot lane at real packet construction. These are sparse encoding/activation witnesses, not final VU output equality or every-frame image/scanout certification. TV confirmation is pending; the console stays running. Historical differences from the separate no-light ELF are descriptive, not a matched causal light-only price; added cold hook footprint remains unpriced.

### UDP-only reset experiment

Before the light run, root sent one exact six-byte RESET request through a private UDP-only sender while the existing host client stayed active. Host socket acceptance is recorded; on-wire delivery and console reset completion are not proved. A later fresh client established TCP18193 but produced zero stdout and no loadelf. That attempt is rejected, and the exact owned client was stopped. The user then power-cycled PS2, after which the light ELF loaded and completed normally. UDP bypass alone therefore is not a qualified reset fix; it also does not establish that the earlier reset clients stalled at TCP connect. Next isolate receipt and handover with console IOP/EE/ready breadcrumbs and independently identify the flashed r7 image. Records and sender are retained under `two-cars-light/reset-udp/`; no production reset behavior was changed.

## Two cars, moving light and a static textured model

The next private sibling adds the public night-walk Kenney pine at x5.6,z8, outside the road edge, collision/physics/shadows disabled. The normal generated model path loads the original shipped TMDL and quantized treeB texture. It is a small 96-vertex/32-triangle asset with one material, no AO/LOD/reflections. Existing cars, moving point, ambient, short controller and sampler remain. Generated model indices shift coherently; no stale parent table is reused. Extra object scanning and material/texture/render work belong to this workload, not a pure vertex-only price.

Native source/object/ELF and original runtime asset binding pass, along with 42 parser controls and 5400 emulator loops. Mandatory synchronous owner4 post-main-cull acceptance distinguishes the tree from road and vehicles at all six cold frames, with exact total closure. Both vehicles' body/wheel light activation remains positive. Root reviewed three owned raster captures showing both complete cars and the visible pine. The owned emulator was stopped with PID absence subsequently confirmed. These are sparse image/activation checks, not every-frame proof or hardware timing. This initial emulator-only checkpoint is superseded by the physical result below; emulator HUD/cadence is not hardware pricing.

## Private r7 reset breadcrumbs

The user confirms the console loader is r7. A separately named private `r7-resetdiag` preserves the original reset ordering and adds one IOP valid-RESET witness, an EE stage ledger, safe post-screen text and boot/IOP-bind/EE-handler initialization markers. Early red/yellow BGCOLOR stores may remain hidden behind the framebuffer; missing UDP text does not prove nonreceipt. No EE stdio or pre-quiesce screen drawing is introduced, and no draw occurs between accepted IOP reset and the original BOOTEND waits. The stage ledger uses initialized data to survive ExecPS2's BSS clear; remote reads can still fail when the EE command thread is stuck.

Independent root review verifies all 44 private source pins, actual objects, link maps, logs, compilers/packer and three ELF artifacts. Actual final BGCOLOR access is a 64-bit `sd`; the earlier 32-bit diagnostic candidate is preserved in LAB as unqualified, with no runtime failure claimed. Native low/high and low-packed builds pass. This does not qualify FMCB boot, hardware reset or a fix. Diagnostics alter reset-time timing/footprint and remain private.

The low-packed candidate is local at `F:/Projects/tyrax2-lab-20261001/ps2link-reset-breadcrumbs-v1/ps2link-r7-resetdiag-low-packed.elf`, SHA256 `df9aedf1328f0f6b1f1d4327e2c4d09c2c701ca12ccaa5df1e3afef8d8c6fde4`. The evidence archive retains source/proof, not the ELF binary. Safest qualification boots it separately from a known cold launcher with the existing working IPCONFIG.DAT, preserving the working r7 file. Do not load a replacement over resident ps2link: even different link addresses do not establish safe handover. No image was flashed or launched. Require banner/address, IOP UDP-bound, valid EE handler IDs and a fresh known-good game before interpreting reset-stage output. See [the preparation report](resetdiag/preparation/report.md).

For exact source evidence, the reset loader's `.gitattributes` and `.gitignore` are archived as `.gitattributes.raw` and `.gitignore.raw`. Their original bytes remain pinned; restore the original basenames only when reconstructing the private source tree. Keeping them as data prevents upstream Git policies from normalizing or hiding the evidence. The native source manifest describes original LAB names; the archive inventory describes retained names.

### Physical static-model result

| Window | Non-pacing loop work | Entry period | Rendered / synthetic completions |
|---|---:|---:|---:|
| Parked | 2.6972 ms | 16.6833 ms | 320 / 0 |
| Short driver throttle/braking | 2.9829 ms | 16.6833 ms | 320 / 0 |
| Post-drive parked | 2.6954 ms | 16.6833 ms | 320 / 0 |

After the user confirmed a physical restart, the unchanged qualified ELF completed 5400 loops on PS2 with no load/asset/DMA error. All observed windows retain approximately 59.94 Hz, with no entry period above 25 ms among the 384 raw samples and zero synthetic completions in measured windows. At each of six cold frames the tree contributes one accepted bag/96 vertices; road/car/model totals close, both vehicle definitions retain positive body/wheel geometry and effective light paths, and actual short driver movement is established. The user confirms the current scene looks correct. The earlier plain moving-light checkpoint had no TV confirmation for that exact ELF; this confirmation applies to the new pine scene.

The small textured 32-triangle pine does not exhaust this fixture's frame budget. It does not establish a budget for dense scenery, a complex building, a full-night scene or audio. Separate-ELF historical differences are descriptive, not a matched incremental model-only price. Sparse accepted inputs and raw timing do not establish every-frame raster/scanout or input latency. Non-pacing work includes the ordinary loop and its waits, not pure EE arithmetic. The owned client remains serving and no reset was sent after this run.

## Eight buildings on the same road

The next private sibling adds four public north and four west facades at x-6.6/+6.6, z12/20/28/36, yaw90, without collision, shadows, extra lights or effects. Both cars, the moving point, pine, short route and ordinary sampler remain. Normal static batching stays enabled. Actual public baked per-part budgets total11328 vertices/3776 triangles for these instances; this is a nominal imported budget, not an accepted-frame or postclip emitted count.

Native source/object/ELF/asset binding,48 synthetic parser controls and5400 emulator loops pass. All six cold frames contain accepted building carriers representing all eight shown owners, with exact road+cars+pine+building input totals. Shared carriers are counted once; owner refs prove accepted-carrier membership rather than independent pixel visibility. Both vehicle body/wheel light paths remain positive. Root reviewed three owned captures and confirmed the emulator stopped; image interpretation is bounded by the supplied runtime review (the initial shot's pine is not separately distinguishable).

Physical measurement awaits reset-ready console start. Emulator cadence is not PS2 pricing or a new60fps result. Common observer footprint remains unpriced; extra objects/materials/textures/batching belong to the workload, so a triangle-only causal explanation is not established.

### Physical eight-building result

The user physically restarted PS2 and removed USB before this run. The reviewed ELF started with fresh loadelf/Loaded evidence and completed 5400 loops. Parked / short drive / post-drive non-pacing work is 5.2653 / 5.2789 / 5.2748 ms. All measured windows remain approximately 59.94 Hz, each with 320 rendered and zero synthetic completions. None of the 384 raw entry periods exceeds 25 ms.

At all six cold observations, buildings contribute 12 accepted carriers / 11,328 input vertices with all eight owner memberships positive. Road, both cars and pine reconcile to 24 total carriers / 20,661 vertices. Vehicle light paths remain active. This scene still meets the observed 60 FPS class; greater geometry does not alone break this particular workload. Separate ELF differences do not price triangles alone, and counts are accepted input rather than postclip emitted geometry. The whole non-pacing loop includes waits. Visual confirmation is pending. USB removal is recorded, but no reset-cause conclusion follows from a successful cold start.

## Fifty buildings and actual80133 accepted inputs

The same corridor now has25 north/west pairs at x-6.6/+6.6 and z12..204. Camera, short controller, road, both cars, moving point, pine and public static batching remain. Fifty checked64-bit owner refs and bounded512-carrier registration prevent high-bit loss;52 parser controls and actual-header host mask/capacity controls pass. Public building budget70800 was separately qualified as actual accepted input: all six emulator cold frames contain24 building carriers/70800 vertices and36 total carriers/80133 vertices. Owner refs mean shown membership represented in accepted carrier input, not50 independent pixel images or postclip triangles. Root reviewed3owned captures of both complete cars and long facades; pine is not separately distinguished visually. Exactowned emulator stop and PID absence were confirmed; Xvfb remains available.

Fresh physical v2 completed5400 loops: nonpacing 15.4556/15.4649/15.4654ms, entry periods 16.6833/16.6833/16.6834ms, about59.94Hz in these bounded windows. All six cold frames accept80133 input vertices and all50 owner refs. User confirmed the current80k TV image looks good; this does not retroactively qualify the earlier8-building TV. The serving client remains active; archived stdout is the immutable completed prefix, not a terminal-run claim. Common sampler/observer footprint remains unpriced and nonpacing is not pureEE time.

The first physical launch after one bounded standard network reset without USB had no freshload and was rejected, not measured. Reset stdout was empty; this is not evidence of a renderer failure or a fixed reset/USB cause. The later user physical restart allowed freshv2 loading. No triangle-only causal performance explanation or generic60fps guarantee follows from this ladder.
