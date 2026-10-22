# Pool2 static output diagnostic input

Separate correctness ELF derived from the frozen integrated Pool2 target. No pricing conclusions apply. Only terrain_game.cpp is changed and pool2_static_probe.hpp is added: dedicated init/loop branches bypass ordinary scene initialization/rendering and the night sampler. All integrated engine source remains byte-identical to assembler-repaired proposal V3.

Root restores the same archived assets during its fresh native build. The diagnostic loads existing hud/flare-corona.png. All geometry, ST and two exact RGBA qwords are static and live through the final ready spin. The authentic View uses one96 or two96 runs with generation1; TC precise/full clipping, no lighting, no spot, triangle list and package75. Camera(0,0,-20) looks at origin; actual positive eligibility must prove inside admission.

Write game/bin/pool2-probe.cfg as `arm vertices repeats`: arm0/1, vertices96/192, repeats1/2/3. First iteration collects only cold Pool2 source counters and rejects missing eligibility, fallback, mismatch or invalidity. Later iterations keep caches/source identities and set collectCounters=false. They reject counter writes. Source counters are only first-frame evidence; warm table execution is qualified by actual final VU marker/layout/output.

Normal beginFrame/usePipeline/render/endFrame and public synchronizeFrame complete each iteration. After the final frame, POOL2PROBE_READY is printed, volatile pool2ProbeReady becomes0x504f4f32, and the exported pool2ProbeHalt spins before any new game job. A rejection uses0xbad00001 and POOL2PROBE_REJECT; never accept a merely spinning process. Root pauses at pool2ProbeHalt, verifies accepted marker/PC, captures source-bound exact-tag SaveState with MTVU disabled, validates guest text/config and compares the actual packed VU output using the separate decoder. Verify both banks from headers; do not assume parity.

Case96 final banks75/21 are a one-color test. Case192 final crossing75 contains21/54 colors followed by tail42; first75 overwritten. Full output state/GIF/STQ/RGBAQ/XYZF2 including ADC is compared; whole bank inputs are not. Draw-finish relative10..12 may change and remain below outputs. Resident TC microcode must be identical across arms.

Actual target compile/link, inside admission, SaveState outputs, rasters and warm activation remain root-owned pending checks. This source does not prove GS consumption, universal equivalence or gain. Original pricing target and source/host artifacts are preserved.
