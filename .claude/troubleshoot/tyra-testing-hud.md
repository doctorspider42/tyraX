# troubleshoot/tyra-testing-hud.md

Exact first-entry HUD acceptance and hybrid triple-buffer acceptance. Symptom first.

## Exact first-entry HUD acceptance (1.150.1)

**Cause:** a config read must be guarded by the one-time init result, not merely follow a helper whose internal `done` returns false on later frames — otherwise host I/O happens every frame and the timing control is invalid.
**Fix:** use the stationary night fixture and record every frame through the exact driver transition. Compare one ELF with a boot-only old/new HUD gate and repeat the control. Check single-init logs, identical ELF hashes, driver/speed/traffic state and aligned CSVs. Keep first-entry work/HUD/uploads separate from warm medians.

For lifecycle acceptance, revisit scenes, count atlas copies and check sprite and texture identity; evict VRAM and reload normally. Plain and unknown-token strings must not request the icon sheet. A valid icon must load once and reuse its sheet thereafter. Run these loading/render checks after timing, never inside the benchmark window. Compile a non-vehicle orbit project too. Physical PS2 measurements are authoritative; emulator boots and images are correctness evidence only.

For the HUD lifetime probe, mutate VRAM inside an active render frame after scene submission and before frame end, then test resolved icons in ordinary HUD frames. The between-frame forced mutation stalled at the barrier in PCSX2; do not count that or a blocked forced scene transition as passed lifecycle evidence. Repeated font preparation is distinct from full scene revisit acceptance. Correctness-only -O1 harness timings are never hardware performance evidence.

## Hybrid triple-buffer acceptance (1.153.0)

**Cause:** hybrid double/triple buffering has several configuration-specific failure modes.
**Fix:** use a frozen-camera scratch project with solid geometry and HUD. Boot hybrid double/triple variants in the software renderer; compare the finished scene captures and inspect GS dumps for CT32 draw at slot 0, CT16 copies to slots 1/2, DTHE on only during copies, and PSMZ32 depth. Check fresh frame progress and absence of alternating black frames. Exercise limiter on/off, field mode, a BLSS layout rebuild and a mode with insufficient headroom. In a mixed project, use a scratch global script with `ctx.requestDisplayMode` to switch field/full PAL at the normal pre-frame boundary, including while BLSS is scene-disabled; check 3/2 buffer grants and fresh captures after both switches. Regular 32-bit triple buffering must still rotate all three draw targets. Verify the Preferences checkbox can be enabled in Hybrid and persists after reopening. Space repeated `--capture-frame` CLI calls by more than one second: their command sequence is wall-clock seconds, so two calls in the same second are seen as the same command. A PCSX2 run establishes correctness, not hardware frame-pacing performance.
