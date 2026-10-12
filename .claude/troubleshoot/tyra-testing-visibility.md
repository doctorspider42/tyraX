# troubleshoot/tyra-testing-visibility.md

What is SUBMITTED is not what is SEEN: ask by REMOVAL. Symptom first.

## A front was promoted on the assumption that submitted geometry reaches the screen

**Cause:** an inventory ranks a frame by what each producer hands the GS. It cannot say whether any of it reaches the screen.
**Fix:** the instrument that settles it needs **no new engine counter and no engine edit**, because the game can already hide an object at runtime (`ctx.objects[i].visible`):

> hide exactly one object, photograph the frame, diff it against the control.
> **Zero pixels changed means the object contributed nothing by ANY path** — silhouette, projected shadow, baked AO or reflection — so it is free to cull and there is no quality argument to have. A non-zero count IS its on-screen contribution, and `triangles / visible pixels` then ranks the population.

`examples/vehicle-playground/authoring/world-visibility-2026-09-17/` is the worked example. `probe-visibility.ps1` drives the whole population **from one boot** — write the index set to the fixture's command file, wait, then `--capture-frame` — which is the difference between twenty minutes and an hour per object.

## Three checks come BEFORE any verdict is read

**Cause:** every claim of this shape is a claim about a zero, and a broken instrument produces zeros too.
**Fix:** **within-probe repeatability**, then **control drift** (photograph the control first AND last in the same boot; a run whose control moves is dead), and **a positive control for every zero**. A lone zero cannot tell "invisible" from "the hide never reached that index". Photograph the suspected occluder removed too: if the object then paints pixels, the hide reached it and it was genuinely behind something. **Do not read that pair against the control** — the pair and the occluder-alone probe change the same screen region, so they report the same count of different pixels and the comparison silently confirms nothing. Compare pair against occluder.

## A self-capture write can tear

**Cause:** the self-capture writes over `host:` fs and that write can tear (`wrote N of M bytes - the host: write did not complete`).
**Fix:** it is a transient — **retry the capture** rather than letting it kill a long single-boot run.
