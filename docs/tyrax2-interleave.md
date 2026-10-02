# TyraX2 automatic interleave attribution

The generated automatic draw-order selector needs different sampling rules when TyraX2 prepares frame N while the consumer executes N-1.

## Confirmed source contract gap

The old selector times from interleaveBegin in frame N to the next interleaveBegin and subtracts the renderer cumulative pacing stall. It assigns that interval to the order chosen for N. It probes 16 alternating frames (eight pairs), selects interleaving after at least five wins with a 1% margin, then holds the choice for 100 samples. Printed microseconds use a coarse 295-tick divisor; decisions use raw ticks, not those rounded reports.

Under the ordered pipeline, endRecording/flush for N may complete and present pending job N-1. DMA/FINISH completion waits occur before the presentation pacing clock, so subtracting getStallTotal does not remove that prior-job contribution. The labelled interval can therefore contain current EE work and prior GPU work of another order, as well as the next frame front end. Source establishes a mixed-label interval, not a measured nonzero bias or the cause of a particular slowdown.

Early returns for split views or authored modes 0/2 leave the old timing mark intact; generated scene loading does not reset it. Returning to auto can also pair an interval spanning skipped views/frames against a stale order. A future selector must reset on effective mode/scene/view changes and compare settled blocks or otherwise attribute actual job costs. The correct block length and hysteresis still need measurement.

## Physical observation and controls

The private same-call clip reuse trial passes host/emulator semantics but physically measures 18.248 /19.137 /18.411 ms for control/candidate/control, all 29.94 Hz. Nearby decision reports are 5 interleaved/0 plain,2/3 and4/1. Those reports have neighboring frame-marker association, not an exact duty cycle or causal proof. Existing pose/update/arena counters remain stable. Keep that candidate private; see the [clip record](tyrax2-clip-reuse-2026-10-02.json).

A new private six-phase same-ELF fixture keeps ordinary batching and baseline clip math. First run light-selection observer off/on/off under original authored auto. Then run separate matched pinned plain/interleaved/plain controls, with the observer off. Pins bypass selector clocks and retain heavy setup plus authored-off/split safeguards. A mode revision clears stale marks/probe state at the next helper entry. Aggregate call counts verify which path actually ran; they count helper calls rather than universally one call per game frame. No new DMA wait is added inside measurement windows. Restore authored behavior after the final window.

The discarded-selection probe distinguishes retained and overwritten picks, actual candidate counts, and sphere work shared with BLSS. Only a discarded-only sphere could disappear alongside an overwritten pick; a BLSS-shared sphere must remain. Both emulator and physical stationary-night warm windows have 114 retained picks and 912 candidates/frame, with zero discarded picks. There is no overwritten selection to remove in this pose. This activation evidence does not establish another scene's volume or a hardware saving. Enabled retained sphere/pick scopes measure 0.149/0.658 ms, including observer/context cost; they are not pure removable cost or directly comparable with differently instrumented earlier scopes.

Native build, PCSX2 2.9.93 and one physical boot complete 13,200 frames and five same-scene reloads. All six raw 512-frame windows and neighboring counter/profile/order windows pass parser bookkeeping checks; pins are 50 plain calls/window in phases 3/5 and 50 interleaved in phase 4, with zero blocked calls. Emulator clocks are not hardware cost evidence. The [machine record](tyrax2-interleave-2026-10-02.json) retains sources, probe/gate changes, matching stripped/symbol ELF hashes and failed-attempt exclusions.

Physical authored-auto observer off/on/off work is 18.199/18.295/18.200 ms. The 0.095–0.096 ms enabled delta can include changed auto decisions and is not a pure observer bill. Separate pinned plain/interleaved/plain work is 18.281/18.028/19.200 ms. Apparent interleaved savings of 0.253–1.172 ms coexist with 0.919 ms plain-control spread; one sequential boot does not establish a stable gain or explain that spread. All phases deliver approximately 30 Hz (neighboring periods 33.370–33.403 ms). Repeat the exact ELF from a fresh physical boot before accepting an order change. A later client launch received only old game frames above 29,000 without a fresh start and is excluded as a repeat.

The second fresh boot of the identical ELF also passes all 13,200 frames and window/count checks. Auto off/on/off work is 18.042/18.293/18.126 ms; enabled scope activation remains 114 retained picks and 912 candidates, zero discarded. Pinned plain/interleaved/plain measures 18.392/17.988/18.346 ms: interleaved saves 0.358–0.404 ms against 0.046 ms within-boot plain spread. This is positive stationary-pose order evidence, with approximately 30 Hz delivery unchanged. It does not explain the first boot's slow restored control or the separate clip candidate regression. No production selector change follows yet.

Source triage excludes stale heavy counters as direct pin-plain routing inputs, but finds different retained memory and unreset animation clocks. Scene reload does not reset dynamic-light/star time, and the hour-pinning script is followed by a real-dt day/night tick. Actual light/sky/packet digests were not recorded, so complete equal-input attribution remains open. A separate private control can replay phase-relative light/star evaluation and current-hour sky evaluation without changing physics dt; price its observer and compare actual ordered light fields and star outputs. Do not silently change the existing measured ELF.

That private visual-time control now passes a fresh physical 13,200-frame boot,
all six raw/count/order windows and eleven matching-offset samples per phase.
Pinned plain/interleaved/plain work is 17.521326/17.307883/17.522563 ms:
0.213443–0.214679 ms interleaved benefit against 0.001237 ms plain spread,
with approximately 30 Hz delivery unchanged. All sampled pinned light/runtime,
camera, star-output and sky digests/counts agree. Visual time is replayed equally
in all arms; real physics dt and allocator/cache history remain normal. This
strengthens the stationary-pose order evidence, without establishing full
unsampled/pixel/query identity or the cause of earlier drift. Different clock
evaluation and observer/layout prevent treating lower cross-build work as a
production gain. No emulator was run, as requested by the user; new physical
image confirmation remains separate. The [controlled-clock record](tyrax2-pinned-clock-2026-10-02.json)
retains exact source, matching ELF/symbol and frozen-log provenance.

The next narrow EE reuse census counts adjacent enabled affine-inverse input repeats first, then identical picker query inputs. The private count-only inverse observer passes 100,039 actual-source host comparisons for each macro setting and independent count bookkeeping, with exact restoration of baseline sources; native/emulator/physical activation remain separate gates. It adds no clocks, skips or packet changes. The inverse depends on 12 model elements; picker reuse additionally needs current ordered mutable light state. Existing transform-cache hits prove neither identity. Count and price key/observer cost before implementing a one-entry cache; do not revive the rejected full transformed-light cache or freeze public light state once per frame.

This is an integration gap in cost selection; ordinary flowgraph ordering and resource fences remain supported. No new mandatory flow node, production selector fix, default promotion or FPS gain is accepted by this audit.
