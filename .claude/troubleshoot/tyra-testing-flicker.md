# troubleshoot/tyra-testing-flicker.md

Measuring a console-only flicker: the failure-rate fixture. Symptom first.

## A corruption that exists only on a real PS2 and only in SOME frames of a static scene

**Cause:** the 1.81.1 slot-pool race showed a lamp's corona as a sliver in 4-19 of 30 frames, never in PCSX2. A single `--capture-frame` per build is then a coin toss dressed as a verdict, and two builds photographed once each will "differ" for no reason.
**Fix:** the fixture that works:

1. **Pin the run**: an input replay (`bin/replay.in`, docs/input-replay.md) pins `dt`, and a tiny script parks the camera on a fixed pose from a frame number on (pose A until frame 1400, pose B for ever after; the cited `vantagecam.cpp` is not in the tree — `zz_vantage.cpp` under `examples/large-terrain/authoring/` is the frame-indexed shape). The same pose then renders for the rest of the run.
2. **Sample, don't shoot**: wait for `--debug-state` to report a frame past the park, then take 24-30 free-running `--capture-frame`s a few seconds apart.
3. **Score each against ONE reference** of that pose (a build known good, or PCSX2) with the HUD rows masked (`d[:70,:] = 0`; the FPS/MEM text differs between builds), and report `pixels > 40` per sample. The number is the **rate**, and the distinct values tell you how many interleavings there are (a race gives a small discrete set; garbage gives all different).
4. **Bisect with barriers, not theories**: an arm per synchronisation point (GS FINISH after every bag, a VIF1 FLUSH packet the EE waits for, a FLUSH in the VIF stream only, one wait at one suspected site), 24+ samples each, read the FPS off the HUD too. In 1.81.1 only the arms where the EE waited fixed it — that single fact located the writer.

## Three traps in the failure-rate fixture

**Cause:** each produced a confident, wrong answer.
**Fix:** (1) the fixture compares CONTENT, so both arms must be the same project directory (two scratch copies with a moved object "differed" by exactly one "PICK UP" prompt and cost a day); (2) `--capture-frame` resumes a halted game, so never mix it with halt/step walking inside one sample series; (3) `--debug-state` reads `bin/livedbg.bin`, which the PREVIOUS run left behind at its last frame — delete it before launching or the "past the park" wait returns at once and the series photographs the moving camera.
