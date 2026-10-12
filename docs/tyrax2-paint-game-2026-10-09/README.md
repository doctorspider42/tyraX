# A11 generated paint lifecycle evidence

Default-OFF generated paint integration passes host state tests and a native
emulator night-road lifecycle run. This archive is correctness evidence, not a
physical speed measurement or a whole-scene pixel-equivalence proof.

Fixture: `F:/Projects/tyrax2-paint-game-night-20261009-v1`.
ELF: `623dd458b0674c55c8ce9903a0f7b01a459ac4397e4a5d980d5c31ea541ac698`.
The native binding covers 471 source files. Three completed phases contain
12,124 paint visits, 1,746 accepted bases, 940 EE refreshes (62 held-basis),
all six modes, both authored LOD tiers, zero scalar/state errors and final
`DRIVEDONE valid=1`. Each phase confirms midnight, 66 road chunks, eight active
lights and 18 ground-shadow pins. Nine malformed-result controls are rejected.

The current production paint block is generated independently for FPP and
orbit, then inserted into preserved A08 deterministic orchestration. Diagnostic
hooks explicitly select EE control and check accepted basis/every EE colour.
Current production engine capability, uniforms and whole images are used;
historical C16 engine patches are not. Forced LOD, stationary camera sweeps and
scene reloads are covered; full-throttle driving is NOT covered by this run.

The target compiler accepts freshly generated physics TUs/headers for both
templates at flag 0/1. The host compiles the actual template loop with sanitizers:
6,000 source/count/stamp/LOD/rebuild/representation transitions, 185 held-basis
refreshes, and three bad map-stamp/basis/readiness mutations rejected. Disabled
preprocessor tokens match the before snapshot. In-place rebuild and same-pointer
normal edits are host cases, not injected target cases. Actual night captures
are visual smoke checks, not paired whole-scene readback or timing evidence.

`fixture/` contains bound sources, ELF, logs, parsed checks, captured frames,
the three body TMDLs used to derive the expected LOD mask and asset hashes.
Other assets remain in the original local fixture; this is not a standalone
distribution of the full game. `generated-fpp/` and `generated-orbit/` preserve
fresh sources/headers and manifests. `host/` includes before templates, editor
build/generation and host/regression logs; `tools/` records verification scripts.

Retained setup failures: relative-path example refresh failed and was rerun
successfully with an absolute path; Windows screenshot capture lacks AF_UNIX
and was rerun under WSL. Initial analysis caught CRLF conversion in the saved
production-block witness. That diagnostic text was normalized to LF to match
the pre-recorded hash and the generator now writes bytes. No bound source or
ELF changed after launch. Final analysis passes, including archive hashes.

PS2 still has the A06 freepad DMA Busy stall. Its owned host server remains
alive; no restart/deploy/power-off occurred. No default promotion or 60 Hz claim.
