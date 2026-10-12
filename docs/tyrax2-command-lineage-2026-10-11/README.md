# A36 Recorded VIF Command Lineage

Offline analysis of accepted physical A18 day/night closures. No PS2 launch,
new runtime instrumentation, new clocks, renderer change or performance price.
Input archives remain unchanged:

- `../tyrax2-three-path-2026-10-09/command-price-v4-physical-day-20261010`
- `../tyrax2-three-path-2026-10-09/command-price-v5-night-20261010`

The reader verifies archive hashes for all used authority files, native binding,
closure/log/ELF identities and unchanged before/after resident programs. The
captured FrameChainArena closure owns every nonempty REF densely, in tag order.
Its first REF permits deriving the EE base; every subsequent address, payload
extent and final allocation cursor must agree. Arbitrary DMA chains do not
have this contract and are rejected rather than guessed.

The preserved actual Vif1ChainCheck::Stream is compiled on host to identify
commands versus payload. ELF program bytes are compared exactly at8-byte
boundaries against resident VU memory; stripped ELF symbols come from the
hash-pinned native nm export. V1 incorrectly required an ELF symbol table;
it rejected before analysis. The repaired reader does not loosen image matches.
The buffer header count is checked against actual relative2 V4 input length.
MVP-delimited runs are command structure, not certified bags or scene owners.

## Results

All759 day and805 night launches map to exact resident image ranges; no
unmapped call. Four negative controls pass per closure: truncated ownership,
changed REF address, actual truncated VIF payload and unknown VIF command.

| Resident image range | Day calls / UNPACK bytes | Night calls / UNPACK bytes |
| --- | --- | --- |
| Cull_TC | 481 / 1537904 | 510 / 1632192 |
| Clip_TC_FOLD_PAINT | 230 / 139584 | 240 / 146176 |
| Cull_C | 13 / 19616 | 22 / 31952 |
| Clip_C_FOLD | 5 / 3712 | 3 / 2160 |
| Cull_TCE_ENVN_PAINT | 27 / 98256 | 27 / 98256 |
| Billboard_T | 3 / 2576 | 3 / 2576 |

Night Cull_TC has117 triangle-list and393 strip headers,33654 relative2
input vectors. Its381 75-vector packages account for28575 of those vectors.
Clip_TC has2877 input vectors and240 list headers. Env image has2025 vectors
in27 strip packages. Image range is not a unique semantic shading mode:
folded entry paths can share one resident image; do not infer source producers
or lighting from the filename alone.

The largest night MVP-delimited run is46 Cull_TC calls /3390 input vectors /
164336 UNPACK bytes; input bounds approximately(-0.90,-0.12,-2.64) to
(0.97,0.93,2.24). A27-call env run has nearby local bounds and98256 bytes.
Vehicle association is a plausible inference, NOT a confirmed object identity.
Other large runs have world-scale bounds; reports retain exact inputs/hashes.

Night stream has827 VIF FLUSH and111 FLUSHE commands. The preserved SDK start/
continue helper emits FLUSH before MSCAL/MSCNT; object heads need FLUSHE before
absolute uniforms. These are NOT827 EE DMA launches/host waits, and removal is
not licensed by their count. The accepted workload has one native submission.

## Decision

Do not resume blind clipping or list-ticket tuning. This historical frame's
dominant recorded traffic is ordinary cull/strip input, not clip payload or
env program launches. This does NOT prove those classes dominate EE elapsed,
that reflections are cheap, or that current A35 coverage is complete. A18
predates A28/A29/A30/A31 and uses different routing; do not transfer its counts
or timings to V7. Its owned-command endpoint remains evidence that substantial
net preparation exists in that frozen state, not moving60fps certification.

Next necessary bridge: capture the unchanged current moving-baseline command
closure at a matched drive state using the already-qualified A18 synchronized
freeze protocol, retain ordinary-return main/env raster gates, and decode it
with this tool. Add producer/view lineage only where command structure cannot
identify it. No new broad clock partition is needed. Select an architecture
change only after current source/pass/route ownership explains the multiplier.

This bridge now passes on both targets: [current V7 capture](current-v7-v1/README.md).
Physical657 calls/one submission, exact main/env ordinary-return controls;
actual owner labels show roads46.48% of main source vectors, not EE time.
Unknown owners and command-owner attribution remain unresolved. No new price
or default change; current and historical archives remain separate.
