# A12 V1 superseded light witness

The first moving-paint run completes all three road trips and reports zero
paint scalar/state errors, but its return-to-day light witness is invalid.
It scans the whole dynLights capacity, including stale enabled flags outside
dynLightCount. This is not a renderer bug: clearDynLights resets the live count,
and rendering consumers respect that count. The corrected analyzer rejects
V1's eight lights on return to day. Retain this run, not a successful day/night
qualification; V2 fixes the observer and repeats the full test.

ELF `8062a4132a866e68866a88388d6c0fc34f1c573b0ff21f1b2de95e11097f2945`,
471 bound native sources. The old analysis JSON/log was produced before this
observer error was found and is superseded by the corrected rejection, retained
in the V2 fixture. No timing or physical result is claimed from either analysis.

A second PINE screenshot client connected but timed out while the parked
capture client was active. The emulator was not restarted; only the original
parked capture completed. The attempted offset helper edit was removed and
its source hash matches A11. No moving-image claim follows from this attempt.

Bound sources, ELF, CSV, captures, manifests and the LOD inventory assets are
archived. Other game assets remain in the local fixture; asset-hashes.json
records them. Current tools include the corrected observer, while bound target
source is the authoritative record of what actually ran in V1.
