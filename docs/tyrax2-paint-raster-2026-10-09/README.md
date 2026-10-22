# A07 paint raster evidence

This archive records a bounded target-assembled C16 shader-output test in PCSX2,
not a physical performance result or production qualification.

`rejected-v1/` preserves the first run: clipping cases were blank because the
harness omitted the game's clipMargin initialization. Nonblank validation
rejected it. `accepted-v2/` matches that initialization before setRenderer and
passes all 27 raw GS readbacks. All nine EE/VU pairs and nine EE/EE controls have
exact RGBA equality. Deliberately wrong uploaded candidate colours ensure the
prepass cannot silently bypass its work. Actual route counters prove cull and
near-plane clip consumption. Each case renders four times before readback.

Both native source bindings contain 468 files. V2 ELF SHA256:
`579c24f6c1b08eaaa3f676dd15335476a76a242fd9a3538396bf2e5feefbbb00`.
The archive includes bound sources, ELFs, raw images, shader/readback harness,
build/launch logs and the accepted analyzer report. `manifest.json` hashes all
exported files except itself. Host processes owned by these runs were stopped.

No physical deployment was attempted after A06's IOP stall. Vehicle lifecycle,
unchanged-key mode changes, LOD/geometry rebuild, legacy/custom fallback,
night/fog and physical raster remain open. No default is enabled here.
See [the paint report](../tyrax2-reflection-turn.md) and
[the investigation register](../tyrax2-performance-investigation.md).
