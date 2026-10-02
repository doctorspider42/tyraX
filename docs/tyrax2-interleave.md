# TyraX2 automatic interleave attribution

The generated automatic draw-order selector needs different sampling rules when TyraX2 prepares frame N while the consumer executes N-1.

## Confirmed source contract gap

The old selector times from interleaveBegin in frame N to the next interleaveBegin and subtracts the renderer cumulative pacing stall. It assigns that interval to the order chosen for N. It probes 16 alternating frames (eight pairs), selects interleaving after at least five wins with a 1% margin, then holds the choice for 100 samples. Printed microseconds use a coarse 295-tick divisor; decisions use raw ticks, not those rounded reports.

Under the ordered pipeline, endRecording/flush for N may complete and present pending job N-1. DMA/FINISH completion waits occur before the presentation pacing clock, so subtracting getStallTotal does not remove that prior-job contribution. The labelled interval can therefore contain current EE work and prior GPU work of another order, as well as the next frame front end. Source establishes a mixed-label interval, not a measured nonzero bias or the cause of a particular slowdown.

Early returns for split views or authored modes 0/2 leave the old timing mark intact; generated scene loading does not reset it. Returning to auto can also pair an interval spanning skipped views/frames against a stale order. A future selector must reset on effective mode/scene/view changes and compare settled blocks or otherwise attribute actual job costs. The correct block length and hysteresis still need measurement.

## Physical observation and controls

The private same-call clip reuse trial passes host/emulator semantics but physically measures 18.248 /19.137 /18.411 ms for control/candidate/control, all 29.94 Hz. Nearby decision reports are 5 interleaved/0 plain,2/3 and4/1. Those reports have neighboring frame-marker association, not an exact duty cycle or causal proof. Existing pose/update/arena counters remain stable. Keep that candidate private; see the [clip record](tyrax2-clip-reuse-2026-10-02.json).

A new private six-phase same-ELF fixture keeps ordinary batching and baseline clip math. First run light-selection observer off/on/off under original authored auto. Then run separate matched pinned plain/interleaved/plain controls, with the observer off. Pins bypass selector clocks and retain heavy setup plus authored-off/split safeguards. A mode revision clears stale marks/probe state at the next helper entry. Aggregate call counts verify which path actually ran; they count helper calls rather than universally one call per game frame. No new DMA wait is added inside measurement windows. Restore authored behavior after the final window.

The discarded-selection probe distinguishes retained and overwritten picks, actual candidate counts, and sphere work shared with BLSS. Only a discarded-only sphere could disappear alongside an overwritten pick; a BLSS-shared sphere must remain. In the emulator's stationary-night warm windows, all 114 picks/frame are retained and zero are discarded. This activation evidence does not establish another scene's volume or a hardware saving.

Native build and PCSX2 2.9.93 complete 13,200 frames and five same-scene reloads. All six raw 512-frame windows and neighboring counter/profile/order windows pass parser bookkeeping checks; pins are 50 plain calls/window in phases 3/5 and 50 interleaved in phase 4, with zero blocked calls. Emulator clocks are not hardware cost evidence. Physical timing is pending, with matched stripped/symbol ELF archived. The [machine record](tyrax2-interleave-2026-10-02.json) retains sources, probe/gate changes, hashes and failed-attempt exclusions.

This is an integration gap in cost selection; ordinary flowgraph ordering and resource fences remain supported. No new mandatory flow node, production selector fix, default promotion or FPS gain is accepted by this audit.
