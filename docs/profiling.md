# Profiling the generated game (EE frame time)

The PS2 games this editor generates are almost always **EE-bound**: a frame
that misses the vsync budget (20 ms PAL / 16.7 ms NTSC) drops straight to the
next divisor (50 → 25 FPS), and it is nearly always the EE, not the GS, that
ran out of time. Finding *which* EE phase overran is the whole game. This doc
describes the built-in profiler and the deeper manual technique behind it.

The worked example throughout is the usable-object highlight: it looked like a
cheap effect but dropped the showcase to 25 FPS. The full write-up is in the
retired `PROGRESS.md` (the usable-highlight rounds) — see [Backlog](backlog.md)
for how to read it out of git history.

## The built-in frame profiler

**Project > Preferences > Build**, set the profile to **debug**, then tick
**Show frame profiler** (alongside *Show FPS* / *Show memory usage*). The game
draws a per-phase EE-time breakdown in the top-left corner, averaged over ~1 s:

```
FRAME 22.00 SCENE 12.90
HL 1.46 PART 0.99
```

- **FRAME** — whole-frame wall-clock (ms). With vsync on this reads the
  quantized budget (20.0, 40.0 …); the phase sums below it are the real work.
- **SCENE** — sky + terrain + objects + animated models (+ the bodies of
  highlighted usables, which are real geometry).
- **HL** — usable-highlight *overhead* only (the shell/apron submits, not the
  object bodies).
- **PART** — particle systems.

Everything here is gated behind the `DEBUG_SHOW_PROFILER` constexpr in
`terrain_config.hpp`, so a release build (or the option unticked) contains
none of the timer reads — zero cost. The timing uses the EE **COP0 Count**
register (`mfc0 $9`), which ticks at 294.912 MHz (half the 590 MHz EE clock);
divide tick deltas by 294912 for milliseconds.

This is enough to answer "is the highlight/particles/scene the problem?". For a
finer breakdown you drop to the manual technique.

## Static-pipeline VU1 telemetry

`StaPipCore` also has an opt-in counter block for questions below the frame
profiler's resolution: how many packages actually took the cull, clip, or
outside route; how many triangles those routes represented; how many frustum
planes each clip package crossed; how often the qbuffer flushed; how long the
EE waited for VIF1/VU1; and how often the resident/billboard program set was
swapped.

```cpp
stapip.core.setTelemetryEnabled(true);

// Render the interval to measure.

const Tyra::StaPipTelemetry sample = stapip.core.takeTelemetry();
const float vuWaitMs = sample.vu1WaitTicks / 294912.0F;
const float swapWaitMs = sample.programSetWaitTicks / 294912.0F;
```

`takeTelemetry()` returns the accumulated interval and clears every counter.
`activePlanePopcount[0..6]` is a histogram for clip-routed packages. With VU1
clipping enabled this is the exact mask that the clip program consumes, derived
from the MVP/clip-margin planes rather than the looser view-frustum culling
planes. Mask generation stays active when telemetry is off because it is part
of rendering; only the histogram increments and COP0 timing reads disappear.
Telemetry is disabled by default.

## Manual deep-dive (finer breakdown)

When you need to see *inside* a phase — e.g. "which branch of the engine's
`StaPipCore::render` is the highlight spending its time in?" — the recipe that
works:

1. **Own the generated `terrain_game.cpp`.** Copy the project to a short path
   (`%TEMP%\tyra-editor-test\<name>` — long `host:` ELF paths crash PCSX2), and
   delete the first-line ownership marker so the build stops regenerating it.
2. **Bracket the phases you care about with COP0 reads** and accumulate into
   file-scope counters, exactly like the built-in profiler does — but as deep
   as you need (per engine call site, per package branch). Prefer the built-in
   `StaPipTelemetry` API for static-pipeline routing, qbuffer waits, or
   program-set swaps; add temporary engine counters only for phases it does not
   expose, then revert those edits.
3. **Make the repro deterministic without a pad.** Overwrite the camera in
   `loop()` with a slow orbit around the object of interest, e.g.

   ```cpp
   static float a = 0.0F; a += 0.004F;
   entX = cx + r * cosf(a); entZ = cz + r * sinf(a);
   entY = terrainHeightAt(entX, entZ);
   entYaw = atan2f(cx - entX, cz - entZ);
   cameraPosition = Vec4(entX, entY + PLAYER_EYE_HEIGHT, entZ);
   cameraLookAt   = Vec4(entX + sinf(entYaw), cameraPosition.y, entZ + cosf(entYaw));
   ```

4. **A/B in the same run.** Flip the feature under test on/off every N frames
   (a `static bool`, toggled when the averaging window rolls over) and print
   two labelled rows. Comparing HL-on vs HL-off *in one session* removes
   scene/camera variance — this is how the highlight cost was isolated.
5. **Read it on screen.** EE `printf` / `TYRA_LOG` does **not** reach
   PCSX2's `emulog.txt`, so print with the debug glyph HUD (`drawHudText`).
   That atlas only has **digits, `.` and UPPERCASE** letters — keep labels
   uppercase (`SCENE`, `HL`, `PART`). Screenshot the window with
   `.claude/skills/tyra-testing/scripts/screenshot-window.ps1 -ProcessName pcsx2-qt`.

### Gotchas learned the hard way

- **PCSX2's EE is faster than a real PS2 relative to the budget**, so a drop
  that shows in the emulator is real, but the emulator can also *hide* a
  real-hardware drop. Numbers are directional; confirm pathological cases on
  hardware (see the `dynpip-guard-band-cost` / `vu1-clipping-cost-measurement`
  developer notes for the real-PS2 deploy recipe).
- **`endFrame` time is mostly vsync idle** on a vsync-locked frame — do *not*
  read it as GS load. The reliable signals are whole-frame time and the
  per-phase EE sums.
- **Submit count × near-object cost dominates.** A near object is always
  partially in the frustum, which routes it through the engine's most
  expensive path (subpackage classify + clip). Any effect that re-submits a
  near mesh N times multiplies that. The highlight's 25 ms was 5 extra
  full-mesh submits of a 9k-vertex primitive; the fix cut both the submit
  count and the per-submit vertex count.
- **FRAME high with SCENE / HL / PART flat means the cost is not rendering
  at all**, and the built-in profiler cannot see it — those three phases live
  inside `renderScene`, and everything else the loop does is unbracketed. The
  showcase's "25 FPS for the first ten seconds, then 50 for good" was this:
  `FRAME` fell 31 → 20 ms while `SCENE` sat at 5.7 the whole way. Bracketing
  the loop itself (before `beginFrame` / render / `endFrame`, then splitting
  the first of those) put all ~10 ms in the sound step, and an A/B — remove the
  music, then remove the emitter — pinned it on an audsrv call waiting behind
  the music stream's blocking one. The fix was one line in the engine; see
  [sound.md](sound.md#emitters-and-streaming-music-no-longer-fight-over-audsrv).
  Two habits from that hunt: bracket **phases the profiler does not cover**
  before theorising, and check the emulator's own speed readout in the same
  capture — PCSX2's `Speed:` stayed at 70–85% across the whole run, which is
  what ruled out host-side warm-up and proved the drop was real inside the
  emulated machine (the game's own FPS comes from the PS2's T3 hblank timer, so
  it never sees how fast the emulator is running).

## See also

- `.claude/skills/tyra-testing` — building, booting PCSX2, the screenshot
  script, and the layered verification story.
- [VU1 clipping plan](vu1-clipping-plan.md) — real-hardware EE-clipper cost
  measurements and the clipping-to-VU1 design.
