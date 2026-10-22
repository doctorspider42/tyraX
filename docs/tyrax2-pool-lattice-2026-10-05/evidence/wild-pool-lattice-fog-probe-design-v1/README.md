# V11 fog diagnostic source preparation

Source-only workshop. The earlier V1 diagnostic and runtime helpers remain unchanged.
Copy the two files under `source/game` into a fresh pricing V11 copy, retaining
the same header filename and ready/halt symbols. Pricing inventory remains 501;
the dedicated diagnostic inventory is 502. Root owns source freeze, native build,
native review, device execution and capture binding.

The unchanged real 5x5 grid uses exact shared position/ST bits, 96/192 occurrences,
75-occurrence packages and independent lattice arm 0/1. Both arms use the compact
table and lazy backing. Color epochs 1..3, counters disabled, zero source readiness
and beginFrame/usePipeline/render/endFrame/synchronizeFrame/READY lifecycle remain.

Fog uses the actual `RendererCore::setFog` API with RGB 23/47/71, start 19, end 23;
bag `fogDisabled` is false. Camera depth of the finite nonflat grid spans
20..21.375, strictly inside the transition. No fixed fog coefficient is authored.
The strict protocol separately records fog setup; actual coefficients must be
decoded from saved VU output before any target acceptance.

`prepare.py` derives pinned earlier source/helpers and pins V11 preparation.
It only writes private source and helper files and syntax-checks Python. Its
proof is source readiness, not native or target-output acceptance.
