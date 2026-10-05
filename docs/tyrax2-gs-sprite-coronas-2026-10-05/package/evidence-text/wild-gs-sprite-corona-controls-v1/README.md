# Authoritative kind9 GS SPRITE sampler adapter

This is a new private analyzer package. Kind9 is handled natively rather than relabeled as kind7/8. The qualified lattice analyzer is extended, and its underlying analyze-loop.py is byte-identical. No source fixture, native engine cache, build or device was changed.

## Guards and interpretation

The adapter requires the complete inherited sampler contract: valid5400-loop completion; three1800-loop phases; all samplers On with262 reads;384 owned raw rows and15 owned chunks; ordinary clocks, fixedDt0, profile0/hardwareTrace0, exact Sample/Chunk ABI, common sample pointer, sparse camera/context witnesses, presentation partitions, effect gates, raw/chunk/window completeness and authoritative artifact separation. Kind9 also requires mask0 and extras0 in every phase, qualified pool2 selected/enabled On throughout, and original wild variants Off.

It requires three chronological NIGHTCORONAPHASE rows with selected1 and actual Off/On/Off or On/Off/On. Six sparse NIGHTCORONAGATES rows must have exact schema and offsets750/1155. Typed cold request accounting must satisfy eligible+fallback=coldPackets, requested=eligible when enabled and0 when Off, package/source/requested vertex bounds, fog/light subset bounds and invalid0. acceptedOutputKnown must remain0. Counter rows with unknown/duplicate/nondecimal/overflow fields or inconsistent arms/partitions are rejected.

Positive cold EE requests yield REQUESTED_OUTPUT_UNKNOWN. Zero cold requests yield ZERO_COLD_REQUESTS_CACHE_REPLAY_OR_INACTIVE_UNKNOWN because warm retained/baked replay may bypass these observations. Neither result qualifies actual sprites, full timed-window activation, pixels or physical gain. Reports retain physicalGainAccepted=false, coronaAcceptedOutputKnown=false and coronaFullTimedActivationKnown=false. The elapsed contrast is explicitly the GS SPRITE request arm, with actual VU output unknown; original loop statistics and limitations remain.

## Usage and package dependencies

Run the same authoritative CLI with `--kind 9 --order 0 --joint 0 --restored 0`, or order1, supplying separate stdout and artifact paths, environment host/emulator/ps2 and a new report path. Example:

```powershell
python analyze-night-cli.py --stdout stdout.log --artifact artifact.log --environment ps2 --kind 9 --order 0 --joint 0 --restored 0 --report strict-analysis.json
```

The CLI preserves existing report files, writes structured rejection and exit1 for bad data, and pins the input logs plus all parser dependencies. Copy these six files together when root prepares ordinary helpers or evidence:

- analyze-night-cli.py
- analyze-night.py
- analyze-loop.py
- corona_controls.py
- source-controls/night_plan.hpp
- source-controls/stapip_vu1_shared_defines.h

The two source-control headers are exact snapshots from qualified physicalV2 (source-manifest b9582677a88857c64d7c807e8c7acfb170d4aa84b5ef826bdd62734f1e26ebe3). They bind validity/arm/table expressions and current header masks. The native root fixture/source/ELF provenance must still be bound separately by root's ordinary preparation helper. The copied parser helper now accepts the actual eight-digit hexadecimal samplePtr format; the inherited authoritative loop guards still require a nonzero stable pointer. Immutable V6's prior workshop helper was not edited.

## Host qualification

run-controls.py produced two synthetic full kind9 captures by adapting existing qualified host sampler transcripts, retaining all384 raw rows/15 chunks and sampler context. These are not actual target captures. Both CLI orders passed. Two additional coherent zero-cold-request controls passed with output unknown.54 negative checks passed, including malformed/duplicate/missing corona rows, partition/application/vertex/state errors, false accepted-output claims, effect/table cuts, loops/read count/raw/chunk/clock/profiling failures, wrong plan and kind9 relabel rejection. Three corruptions also exercised actual CLI exit1/rejection reports.

Twelve existing compiled-host O0/O2 captures for kinds5/6/7 and both orders passed with every inherited result field identical to the qualified old analyzer. Six O2 legacy CLI paths passed, and each legacy capture retained the raw-completeness rejection guard. No host executable was rebuilt or rerun. Existing old logs without corona rows stay valid; if new unselected corona rows are present, all their values must be zero.

proof.json binds complete host-v2 artifacts, qualified prior inputs and parser hashes. The preliminary host directory contains an incomplete setup attempt; it is excluded from qualified host evidence. final-handoff-proof.json is the immutable handoff closure. No target execution, actual SPRITE output, raster equivalence, native60FPS or physical gain is accepted by this package. Root must separately establish the completed-output capture contract before any activation or promotion claim.
