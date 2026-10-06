# Private same-car plus S-road qualification fixture

This sibling adds exactly one normal authored Road to the fresh one-Ravager map. It is not a production change, matched A/B trial or measured result. Source identity is public HEAD b5636632bf87fc4e9b958d498c3d26f8070b3f9c plus the pinned private quiet sampler, virtual-pad controller, cold witness, eight cosmetic-FX exclusions and analytic floor inherited from the fresh empty-car fixture.

There are three authored entities: one non-rendering player/camera, one Ravager and one road. One vehicle definition, no scene lights, audio, sky, HUD diagnostics, portals, mirrors, feeds, decorations, road junctions or spills. Default HUD/menu/font/save files satisfy loader asset closure; authored HUD remains disabled. Three internal model slots are body, wheel and unused fast-wheel assets of the same Ravager.

The public road pipeline generates normal boot geometry and indexed contact triangles. Width10, longitudinal step1, grip1, no texture or edge fade. Seven X/Z anchors: (0,-15), (0,0), (0,16), (3,30), (-3,44), (0,60), (0,80). Existing disabled-terrain analytic y0 remains; the public road surface is raised by0.12. The invisible floor makes departure physically safe but does not qualify road contact.

The ordinary sampler retains three1800-loop phases, five64-loop chunks at offsets800..1119, raw128 loops900..1027 and prime899. No new Count reads, waits or drains. Phase0 parked, phase1 driving, phase2 neutral post-drive; world state carries forward. Public default All receiver lighting stays enabled. This is one continuous arm.

Phase1 controller performs at most six centerline comparisons, with eight-unit lookahead and actual vehicle position/yaw/speed, then emits normal Pad R2/L2/analog state. Throttle ends at offset1056 or z50; braking continues while speed>0.1. Normal physics/camera remain; no teleports, forced camera or full road-triangle scans. Piecewise anchor centerline approximates public tessellated Catmull-Rom. Controller changed versus the straight empty-map route; its included cost is unpriced. No causal road-only subtraction is supported.

Cold offsets750/1155 register owner-3 road bag pointers after normal coarse rejects and before heavy deferral. Existing post-main-frustum-cull Core acceptance resolves actual road/car bags. Parser requires positive road/car counts, exact total sums, no billboards and six observations. ROADCONTEXT uses public roadSurfaceAt at actual car X/Z; parser requires four paved wheels, grounded1, finite road height near0.12, cover/grip near1, anchor-center offset<4 and phase1 endpoint z>=44. Grounded is Boolean contact; paved is rounded wheel coverage count. Sparse observations are not whole-run wheel trajectory, image or every-frame visibility proof.

source-review.json pins source postimages/authored entities. source-content-census.json records generated counts/engine differences. native-proof.json binds source mirrors, dependency objects, archive, linked symbols and ELF. bin-assets-source-pins.json binds complete loader assets. analyze-road.py has24 synthetic controls in parser-controls-v1. parser-controls-prep-failed-v1 preserves the initial harness indentation failure and is not qualification.

Build (PowerShell, repository cwd):

```powershell
& ./tools/toolchain/native-build.ps1 -Project F:/Projects/tyrax2-lab-20261001/tyrax2-closeout-road-car-v1/empty-car -Engine F:/Projects/tyrax2-lab-20261001/tyrax2-closeout-road-car-v1/tyra -Cache F:/Projects/tyrax2-lab-20261001/tyrax2-closeout-road-car-v1/native-cache -Toolchain C:/Users/pawel/AppData/Local/tyra-editor/toolchain/ps2dev *> F:/Projects/tyrax2-lab-20261001/tyrax2-closeout-road-car-v1/native-build.log
wsl -d Ubuntu -- python3 /mnt/f/Projects/tyrax2-lab-20261001/tyrax2-closeout-road-car-v1/native-proof.py
python F:/Projects/tyrax2-lab-20261001/tyrax2-closeout-road-car-v1/analyze-road.py <stdout> --artifact <closeout-samples.txt> --out <result.json>
```

Preparation agent executed no devices. Native/source binding and synthetic controls do not release devices or prove60fps. Root owns runtime freeze/review, process ownership, captures and hardware prices.
