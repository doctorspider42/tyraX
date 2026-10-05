# Historical V1 design notes

This copied document preserves the initial source design only. Current readiness and root instructions are ROOT-WORKFLOW.md and proof.json in this V2 workshop. The typed overlay is now written and binds repaired proposal V3; decoder controls below are explicitly reused from the immutable V1 workshop, not host/proof.json here (which tests workflow symbols/configs). References to waiting for typed API or local codec-control files below describe that historical boundary.

# Static Pool2 packed-output qualification precursor

This is a separate correctness ELF design, not a pricing ELF or accepted runtime. The integrated Pool2 source is still mutable; the typed game overlay must wait for its sealed source and actual bag/view API. No emulator, device, native compiler, or shared build cache was used for this precursor.

## Cases and ownership

Use statically owned position, STQ and color arrays, stable model/material/camera state, and two exact RGBA values. Keep all arrays and run metadata alive through final frame completion and the ready spin. Use the production projection and complete inside-frustum admission; do not force a cull flag. Disable effective lighting/spot selection explicitly using the sealed API, with no unrelated game passes or postFX. Geometry is triangle-list, finite, nondegenerate and deterministic. Use fixed texture/material, fog/depth/ADC settings in both arms.

Case A has 96 vertices and one authentic 96-vertex color run. Its 75/21 packages exercise one-color, reload and short-tail paths. It does not demonstrate a two-color transition.

Case B has 192 vertices, two authentic consecutive 96-vertex color runs, and packages 75/75/42. The second 75-vertex package starts at source offset75 and contains 21 vertices of color0 followed by54 of color1. The final42 vertices have color1. Final two VU banks contain that crossing75 and tail42; the first75 has been overwritten and cannot be accepted from the final snapshot. Never replace the authentic run metadata with shortened runs.

Run each case with arm0/1 and repeatFrames1/2/3. All use the same correctness ELF and resident TC image. The initial iteration may collect cold source witnesses. Warm iterations set collectCounters=false, retain caches and source identities, and must not trigger the cold verification replay bypass. The actual final count/TC-table marker, descriptor and output—not merely a setter or packet-writer witness—qualify the warm arm.

## Proposed game seam (typed overlay pending)

Read the diagnostic configuration once during startup. Validate arm0/1, case A96/B192, repeatFrames1..3 and immutable camera/material identity. In TerrainGame::loop, take an early dedicated probe branch before ordinary dynamic scene jobs. Begin the normal frame, render only the owned static bag, end the frame, and invoke the existing public RendererCore::synchronizeFrame(). Repeat without changing arrays or resetting caches. After the final completion, write an EE ready marker and spin before any new game job or VU submission. Root pauses at that marker using owned UI/debugger control, then saves through stock PINE. PINE itself has no pause command.

Do not add a polling fence that can time out and silently accept. The existing completion path must be source-bound to the diagnostic. Pipeline completion waits for VIF sequence and the recorded FLUSHA/DIRECT GS FINISH, then presents. The synchronous draw-finish helper uploads three relative-TOPS qwords at10..12 and xgkicks them; it does not overwrite header0/1 or these output ranges. At minimum N21, legacy output starts relative65 and table output starts47. Therefore ordinary completion can precede the halt; modified input10..12 is deliberately excluded from comparison. Other frame passes that could overwrite a bank must be removed from the diagnostic branch and checked in the final call graph.

## Capture authority

Use exact PCSX2 v2.9.93 source commit94d86c891b1621c0b252e4fc2e155bf90274dcc0, executable/config hashes and MTVU disabled. Stock PINE EE Read32 does not expose VU1 local memory: handler-backed reads can successfully return zero. SaveState.cpp serializes vu1Memory.bin and vu1MicroMem.bin as16KiB each. Require paused status1 before and after, expected ready PC, stable completed archive, exact SaveState version and unchanged guest .text matching the correctness ELF. Use the existing source-bound capture helper for full archive CRC and EE-text authority. Snapshot drains are observer operations; this experiment does not price them.

The offline selected-member reader handles stored, deflate and method93 explicitly. Before decompression it requires exact selected filenames/sizes, duplicate-free bounded member inventory, bounded compressed lengths, matching local/central headers and CRC. Its copied method93 reader uses installed system libzstd via ctypes when the Python zstandard module is absent. No dependency installation is required. Selected extraction alone is not full archive integrity or paused-completion authority. Require identical vu1MicroMem.bin hashes between both arms.

## Decoder ABI and acceptance

The decoder currently binds the draft bank bases22/483, 461-qword capacity, count mask0x3ff, table marker0x400, material-state marker0x8000 and inside TC route. These constants must be checked against sealed source, actual assembled image/native identity and actual package headers before accepting a probe.

Legacy output begins base+2+3N; table output begins base+2+2N+3. Material state occupies1 or9 qwords, followed by N*3 packed STQ/RGBAQ/XYZF2 qwords. Require exact expected final counts, marker, descriptor N/version1/reserved0 and authentic triangle run boundary. Validate primitive GIF NLOOP=N, packed mode, registers ST/RGBAQ/XYZF2 and A+D state-tag shape. Compare material/primitive header and every output qword bit for bit, including RGBAQ Q and XYZF2 ADC. Match packages by count, not bank parity. Whole local banks/input bytes are intentionally not compared because the layouts and completion inputs differ.

Acceptance is only tested packed VU output equality for these fixed cases and warm repeats. It does not prove GS consumption, raster equivalence, all inputs, retained replay correctness outside these cases, hardware precision, or a performance gain. Root separately owns raster and actual runtime qualification.

## Prepared host controls

host/proof.json records four synthetic packet-pair controls plus stored/deflate member extraction and13 rejecting mutations (STQ, RGBAQ, XYZ, ADC, primitive loops, table marker, run boundary, descriptor size/version/reserved, count, clip bits and truncated memory). These are decoder controls, not actual VU execution.

method93-old-capture-codec-control.json records read-only extraction of the existing linux-warmed-frame.p2s: its VU memory and micro members use method93. That historical capture is not Pool2 evidence. The first syntactically invalid test script is preserved as test-decoder-attempt1-syntax-rejected.py; no accepted proof was overwritten.

Root workflow after seal: create fresh diagnostic source/ELF; run case/arm/repeat matrix; save at owned ready marker; bind full capture authority; run decode-vu1-output.py --savestates --baseline baseline.p2s --table table.p2s --case A96|B192 --out NEW.json; independently review actual headers/output and rasters. No root device/helper operation has been executed here.
