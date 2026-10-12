# troubleshoot/tyra-engine-dev-blss-cost.md

BLSS EE pricing, the libm cuts, the activation table, the heavy-overdraw regime and
break-even. The shipped net, the jitter and the interlock are in
`tyra-engine-dev-blss-net.md`. Symptom first.

## The EE half is priced, and the biggest term is the MLP

**Cause:** splitting the two large terms onto their own counters (`tBlssProxy` in
StaPipCore, `tBlssReproj`/`tBlssFeat`/`tBlssNet`/`tBlssPacket` in composite — the
previous round got "~3.9 ms of scene submission" **by subtraction**, which is not a
measurement) gives, in PCSX2 on the `blssrig` fixture: **net 3.96**, proxy 3.26, pkt 0.61,
reproj 0.39, feat 0.14. `runNet` being the largest is the finding — **newlib's
tanhf/expf compute in DOUBLE and the EE has no double-precision FPU**, so 15 activations a
tile are 15 software round trips.
**Fix:** four **bit-identical** cuts landed (2026-08-08), each measured on its own counter:
the proxy near-clip takes each in-front corner ONCE instead of twelve edges × two
endpoints (-1.10 ms); the deadzone compare moved IN FRONT of the logistic (-0.57 ms,
guard-banded so the original compare still decides within 0.01 of the threshold);
`passHasAlpha()` skips a composite pass with no non-zero corner (-0.30 ms); and `addBag`
hoists the per-COLUMN overlap out of the row loop (-0.20 ms). **BLSS on/off went +8.76 ->
+6.39 ms of EE in PCSX2; re-measured on the CONSOLE the same four cuts are worth 1.96 ms**
(proxy 3.95->2.39, net 2.20->1.97, pkt 0.73->0.56), taking the whole EE bill from 7.92 to
5.95 ms. **PCSX2 does not transfer per-function**: it puts `net` above `proxy`, the console
puts `proxy` above `net`, because the emulator over-weights libm. Attribute on hardware.
Bit-identity was CHECKED, not asserted: under `blssDebugView` 2 the BLSSGRID / BLSSFEAT /
BLSSOUT / BLSSFILL lines are byte-identical across 44 s of paired frames. The **activation
table is worth another 2.11 ms** (net 3.39 -> 1.29) and must flip on BOTH twins in one
commit (`TYRA_BLSS_ACT_TABLE` 512 + `src/blss.cpp`'s `--act-table` default). What is left
is a **~5.95 ms EE floor on hardware** (proxy 2.39 + net 1.97 + 1.03 + 0.55); cutting the
proxy term further means describing the frame more coarsely, a TWIN-CONTRACT change that
needs `src/blsscorpus.cpp`'s `bagOf` to cut the same way — the rule is WRITTEN and
SWITCHED OFF: the **proxy budget** (`TYRA_BLSS_PROXY_BUDGET`, default 0) caps a bag's
proxies at the number of grid TILES its whole box covers; it takes 198 proxies to 116 on
`upscaler-lab` and moves exactly one feature channel (`coverage` 0.631 -> 0.638), and it
may not go to 1 until `bagOf()`/`bagList()` cut the same way.

## The fill-count to milliseconds conversion (the calibration gate)

**Cause:** the `--blss-eval` numbers are **fill counts, not timings**, and the conversion is
measured now, not guessed: on a real PS2 one full-screen textured blended pass is **0.5896 ms
at 512x512 and 0.5174 ms at 512x448** — the calibration gate is per RASTER, which it did not
used to say, and 0.587 was a 512x512 (PAL 576i) figure read against 512x448 coverages for a
year; both are now measured back to back on one console and `perMpx` agrees to 0.3 %
(docs/profiling.md, "The calibration gate"). PCSX2 reports **0.0077 ms** for the same sweep —
it under-reports GS fill by **76x**, so NO PCSX2 GS number about this feature is admissible.
**Fix:** the first real A/B on hardware: BLSS cost **+9.83 ms per frame and saved nothing**,
because the frame was EE-bound (`drain` 0.02 ms, the `endScene` overhang 0.03 ms). **That
number is PROVISIONAL**: it was taken on a build carrying the z-mask defect above, so the
BLSS-on arm drew a frame with every palettised surface missing — the EE half stands, the GS
half understates whatever fill BLSS saves, and the A/B needs retaking on hardware.

## The feed is per-PROXY, not per-tile

**Cause:** the split counter `tBlssAccum` (`proxy=total/accum` in FTSPLIT) settled two
things about that feed, both contradicting what the code said about itself: it is 262
PROJECTIONS for 198 accepted proxies touching only **7.6 tiles each**, so the cost is
per-PROXY and not per-tile — and `addBag`'s four `floorf` calls were **real out-of-line
newlib calls, 68 instructions each** (the tanhf/expf finding one function along; replaced
by a bit-identical cast + compare).
**Fix:** interleaving the six tile accumulators to save cache lines was tried and is
SLOWER: a float is 4 bytes and a line is 64, so all sixteen tiles of a grid row already sit
in one line of each array. Two more measured negatives worth not repeating: moving the
composite's EE work in front of `endScene`'s drain can save at most `tBlssEnd`, which is
**0.05 ms in PCSX2 and 0.11 ms on hardware**; and `beginScene`'s `draw_wait_finish()` is
NOT removable — PATH1 preempts an in-flight PATH3 transfer, so the clear sprite must
complete before VU1 kicks, or the scene is drawn and then cleared over.

## The composite's last two terms were dug into and they are a floor

**Cause:** 2026-08-09, hardware, docs/profiling.md "The last terms in the composite".
`reproj` 0.275 + `feat` 0.190 was the only part of the bill nobody had opened. The method
that found the previous three wins came back empty: **`sqrtf` in `buildFeatures` compiles
to a bare `sqrt.s`** — no call, no errno branch, `/ kTile` folded to a multiply — and
`buildFeatures` is 346 instructions with **zero `jal` and zero `div.s`**. `tanhf`/`expf`/
`floorf` were wins because they were CALLS.
**Fix:** two bit-identical changes landed anyway and are worth **+0.017 ms of a 4.60 ms
bill (0.4 %)**: `finishTileStats` writes `feat[][1]/[3]/[4]/[5]` and `tGrad` directly (five
per-tile arrays and one 224-tile pass deleted — but the work MOVES, `feat` 0.190 -> 0.141
against `reproj` 0.275 -> 0.310), and `buildReproj` hoists the per-column/row screen ray and
makes the neighbour-count average an exact binary scaling. That second one **prices a
divide: ~565 fewer `div.s` a frame is 0.007 ms**, i.e. ~2 100 EE cycles. **And a fixture
trap worth knowing: `proxy`/`accum` differ by 0.051 ms between two runs of the IDENTICAL
ELF** on `upscaler-lab`, because the parked scene sometimes settles with 201 proxies instead
of 198 — `BLSSGRID`'s proxy count is the guard. `reproj`/`feat` reproduce to ±0.001.

## The feature has a regime after all — it is heavy overdraw

**Cause:** on `examples/upscaler-lab` **as it stood at `0c3f05c3`** — 12 haze banks × 256
large alpha-blended billboards = 3 072, plus the 2 fire, 2 smoke and rain that were always
there and were never counted, for **3 448 billboards over 17 emitters** whole-scene — a real
PS2 measured **530 ms with BLSS off against 157 ms with it on** — a **3.37x** speedup, on a
scene running at **1.9 FPS** nobody could watch.
**Fix:** **Re-tuned against the console** (6 banks × 32 instead of 12 × 256, i.e. **11
emitters / 568 billboards** today — that is the figure to quote for the shipped demo, never
the 3 072) it measures **52.95 ms off against 32.42 ms on** — d = +20.53 ms, 95 % CI
[+20.46, +20.61], n = 1024 paired frames per pairing, **1.63x**, for **4.60 ms of EE** plus
0.50 ms of composite fill. **Those milliseconds describe the fixture's PRE-CC0 geometry**:
its art assets were swapped for CC0 ones on 2026-08-09, which left the fill alone
(--blss-coverage 72.63 -> 72.23) but took ~4 ms of EE out of both arms; the fill model is
current, the absolute A/B is owed a re-run. The owed jitter-off re-run has **landed**
(2026-08-09): the earlier 52.86 / 32.98 / **1.60x** was the jitter-ON timing, the off arm
is unchanged and the BLSS arm came out 0.56 ms faster, and `BLSSFILL` reads **`passes = 1.56`** in both. **Break-even is 13.1 full-screen coverages at 512x448**
(`0.7548 x 0.5174 x C > 4.60 + 0.50`) and **11.5 at 512x512** — quote the break-even WITH
its raster or not at all. `breakEven()` and `--blss-coverage` PRINT the right one now
(2026-08-09): `kPassMs` became `kPassMsPerMpx` (2.2524) plus `passMs(rasterPx)`, `speedFrom`
takes the raster, and `CoverageReport` echoes back the `outW`/`outH` it counted at — 13.1 at
512x448, 11.4 at 512x512. BLSS keeps about a quarter of the fill, because `blssScale 0` is
`Scale::X2Y2` — half in *each* axis, a quarter of the pixels, NOT half the fill. The
retention term is FITTED on hardware over five load points (0.7548 saved, RMS 0.093) rather
than assumed at 0.741, and the fit's intercept 5.10 ms reproduces the independently-counted
4.60 + 0.50 to two decimals. The activation table (`TYRA_BLSS_ACT_TABLE`, and the host's
`actTable` in `src/blss.cpp` — **one number in two files, moved in the same commit or not at
all**) took `net` from 1.93 to **0.79 ms**; PCSX2 had predicted 2.11. The previous verdict
("it saves nothing, the frames are EE-bound") came from ONE low-fill fixture plus a **false
discriminator**: `drain ~ 0` does NOT mean EE-bound, because GS backpressure stalls the EE
inside the submission (charged to `submit`) and `drain` only measures the tail after the last
packet. Both arms of the 3.4x win read `drain = 0.02 ms`. **Never infer boundedness from
`drain`; change the GS load and see whether the frame shortens.** Two fixture traps found
doing this: the project key for the Live Debugger is **`liveDebug`**, not `liveDebugger`,
and leaving it on costs ~500 ms/frame of host: network I/O INSIDE the measurement; and the
rig's own window mean summed ticks in a **u32**, which overflows above ~290 ms/frame and
printed a 500 ms frame as 37 ms (now u64 — the median was always right). `upscaler-lab`
itself is a fine GS-bound stress case and a BAD demo: 1.9 FPS with BLSS off, because it was
tuned against PCSX2's 76x-under-reported fill.
