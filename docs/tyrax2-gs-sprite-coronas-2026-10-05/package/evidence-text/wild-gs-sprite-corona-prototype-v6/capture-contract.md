# Completed VU output capture contract

This file specifies future diagnostics, not a performed device run. No VU memory-write marker or counter was added to the kernel, so pricing instructions remain unchanged from V4. Input bit0x0800 is a request, not success. Success is observable in the final output primitive tag after the predicate: typeSPRITE and NLOOP=N/3; fallback remains the complete triangle packet.

For a separate off-clock diagnostic run, hook completed execution of the exact linked TC image or capture each packet at its XGKICK before the next double-buffer reuse. Bind the actual program PC/image hash, input count word, scene/producer epoch and owner package identity. Save every relevant packet immediately; final two-bank snapshots omit earlier overwritten packets. Never infer success from a pre-DMA packet or C++ bool. This diagnostics apparatus must be owned/qualified by root and kept outside pricing regions.

The offline decoder accepts paired schema1 JSON objects with exactly these fields:

- schema:1; arm:0 for Off /1 for On; executed:true, established by independently verified execution trace.
- epoch: nonnegative integer identifying fixed diagnostic producer/content state; packageId: a unique owner/package identifier. Match Off and On at the same settled state.
- sourceCount: original input vertex count; inputCountWord: actual executed VU package header count/flags. Allowed flags are current count mask, sprite request and material-state bit; reject Pool2 input layouts.
- elfSha256 and tcImageSha256: exact qualified native hashes passed independently to the decoder. Strings inside a snapshot are not proof of their provenance.
- primitiveTagIndex: index of the primitive tag within outputQwords starting at the actual XGKICK address; preceding material qwords must match Off exactly. Paired packet material-state/reuse layouts must be identical.
- outputQwords: little-endian16byte hex strings, captured from actual completed VU memory. Include baseline N*3 vertex qwords. On success needs only the compacted endpoint qwords after the tag; unused tail is ignored because GIF NLOOP excludes it. On fallback needs the complete unchanged triangle payload.

The decoder validates full final six-vertex RGBA and Z/F equality, ADC/domain/Q/rectangle, both possible ordered endpoint selections, exact copied endpoint qwords and type/count-only GIFtag changes including unchanged FGE/reglist/material. The VU gate may reject whole packages despite positive requests. Return values count decoded sprite primitives; acceptance additionally requires independently proven capture origin/completed execution and coverage. Missing packages, stale buffers, mismatched epochs/native hashes or no trace evidence block acceptance. The executed boolean alone is not proof.

Synthetic host tests exercise this contract and deliberately report actualRuntimeCaptured=false. Future target evidence must separately record fixture/source/native hashes, trace/capture hashes, capture scope and actual positivity. A microbench proving SPRITE is possible does not prove fullnight pricing activation. Collect positive output in the real unmodified pricing pose and characterize warm replay too; zero cold counters cannot classify replay activation.

Before physical timing, compare full rendered pixels off-clock with constant active fog/light, rejected nonuniform F/RGBA, depth occlusion, texture/filter sampling, edges/seams, additive overlaps, alpha/dithering, portals, clip/split and lifecycle/cache transitions. Retain original ALPHA/TEST/FGE/FOGCOL/texture state. No quality switch may be disabled to manufacture activation.
