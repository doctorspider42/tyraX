# Reviewed SPR/CALL experimental source snapshot (2026-10-04)

This archive preserves the exact seven private engine candidate files compiled in the V3 experiment, three actual quiet measurement headers, and the actual private benchmark driver. It also preserves the older V2 CALL cache for comparison. Source bytes are copied without rewriting. The current eleven source inputs match both the frozen source manifest and the independent actual native mirror review.

This is an immutable experiment snapshot, not a standalone game, a production renderer, or a supported default API. The remaining engine, generated scene, authored/baked assets, SDK, build inputs, executables, native dependency evidence, and runtime records remain in the external LAB fixtures. Relative includes require that full private engine context. No binaries or assets are included here.

## Source map

- `v3/engine/inc/renderer/core/paths/path1/` contains the writer, native CALL cache and validator, SPR prefix stager, SPR transaction storage, and queue interface.
- `v3/engine/src/renderer/core/paths/path1/vif1_queue.cpp` contains the actual integrated queue implementation.
- `harness/` contains the three quiet debug headers and `game/src/proto_benchmark.cpp` from the compiled fixture.
- `comparison/v2/engine/inc/renderer/core/paths/path1/native_call_cache.hpp` preserves the previous payload parsing implementation. It is comparison evidence, not the compiled V3 cache.
- `metadata/provenance.json` binds each copied file to its original source, frozen manifest, actual native mirror review, and saved host controls. Large proofs are referenced by external path and SHA-256 rather than duplicated.

## Experimental behavior and contracts

The private bool mode setter accepts bits 1 (finalized-prefix SPR copy), 2 (CALL replay), and 4 (direct SPR transaction construction), with default mode 0. It refuses mode changes during active/protected recording before changing state. The archive does not enable these modes in a production game. Scratchpad/DMA reservation and installed SDK behavior require their separate runtime qualification; a source scan cannot prove global BIOS/IRQ ownership.

CALL replay caches bounded immutable REF/CNT recipes with their VIF commands and initial cycle state. A successful recipe emits one CALL to a cached target ending in RET. Targets and referenced immutable payloads retain bank leases until completion; retirement does not overwrite leased bytes. The private validator restricts targets, CALL depth, boundaries, and termination. Ineligible recipes, unavailable entries, or refused ownership retain the original REF path. The storage-aware writer preserves TTE/VIF ordering and SPR transaction handling; existing cache flushes and waits remain.

V3 checks for a free unleased cache destination before decoding a miss. Its local payload feeder repeatedly skips words only while the original VIF stream state says they are payload; command words still pass through the original parser. The production stream parser is unchanged. Saved host controls compare full stream state across fragmented and randomized inputs, including pending payload ranges that must not be dereferenced.

One-REF diagnostic replay remains enabled by default. Such a replay does not demonstrate avoided multi-record encoding. The broader source proposal has an optional multi-record policy bridge; that queue bridge is **not installed in the compiled V3 snapshot**. V3 changes only the cache header relative to V2; its queue source is byte-identical to V2 after the canonical `uint32_t` snapshot-word type correction required by the actual EE SDK typedefs.

## Evidence and limits

The seven candidate files and four harness files bind to the actual compiled V3 source freeze and independently reviewed native ELF through metadata. Fresh merged host runs passed at O0/O2 for CALL (1,515 checks), direct SPR (51,149 checks), and joint behavior (1,555 checks). The separate bulk parser oracle passed 44,312 checks at each optimization level. Compile-only proposal evidence qualifies header compatibility; it does not assert that the broader proposal queue equals the compiled queue.

Copied preparation proofs retain their original pending/native-not-accepted flags. Later root native provenance and independent native review are separate authorities, not rewritten preparation results. This archive introduces no performance benefit, hardware visual acceptance, or runtime equivalence claim. Physical pricing belongs to the separately qualified same-ELF, both-order runtime records. Timings from different source versions cannot be subtracted to attribute a gain to these edits.

`SHA256.json` covers every archived file except itself. The package closure proof is stored outside the package so root can copy this directory into the repository without changing source bytes. Documentation and metadata authored for this archive use English UTF-8 with LF line endings.
