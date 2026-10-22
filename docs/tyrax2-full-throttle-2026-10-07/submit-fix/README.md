# Submission candidate evidence

This archive preserves two attempted EE submission optimizations and their
bounded comparison with reusable DMA data observed in Burnout 3.

Neither renderer change remains enabled in production. The main interpretation,
physical table and limitations are in [the full-throttle report](../../tyrax2-full-throttle.md#submission-fixes-2026-10-07).

- `engine-candidate.patch` and `host-candidate.patch`: exact rejected source/test
  changes, applied only to private or temporary copies by the fixture/host tools.
- `physical/`: nine completed passes, 4,867 rows; the analyzer explicitly labels
  the missing reverse single-lookup and combined arms unqualified. Raw console
  output includes the phase9 reload stall. `strict-rejection.log` confirms the
  default 18-pass analyzer refuses this incomplete file.
- `emulator/`: all 18 passes, same ELF. No physical timing inference. `view.png`
  is the initial roadway; `final.png` is after the completed route, beyond the
  mapped roadway. Neither is a pixel-equivalence proof.
- `recovery/`: one failed network reset/execee recovery; no new game boot.
- `burnout/`: metadata, decoded tag lists, reuse summary, acquisition/analysis
  scripts and debugger screenshot. Proprietary RAM, ISO and save states remain
  outside the repository. Scripts pin the private acquisition paths. Discarded
  capture101 used unsupported PINE MMIO reads and is not accepted evidence.
- Native binding, source manifest, build output, sanitizer output and parser
  self-tests pin the acquisition. The source reproduction changes only expected
  directory-derived Docker Compose names, not engine/game source.

Reproduce the candidate host checks with:

```sh
python3 tools/verify-tyrax2-host.py --only count --submission-candidate
```

Create another private fixture with `tyrax2-motion-probe.py <new-directory>
--submit-fix`. This applies the archived patch with a strict context check;
future source drift must fail instead of silently testing a different change.
Do not change the default renderer based on emulator speed or the missing arms.
