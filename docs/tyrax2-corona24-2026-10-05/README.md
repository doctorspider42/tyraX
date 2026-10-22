# Private 24-bit corona follow-up

See [the result and limits](../tyrax2-gs-sprite-coronas.md#24-bit-follow-up-isolated-output-and-drawing-pixels-pass).
This checkpoint qualifies an isolated 24-bit SPRITE packet route and identical
positive **drawing-buffer** pixels. It does not qualify ordinary activation,
Hybrid scanout, console behavior or performance. Production rendering is unchanged.

`source-v1` and `source-v2` each contain six exact source postimages relative to
the previous frozen 500-input V6 pricing source. Each complete diagnostic
inventory has 502 inputs. Version 2 adds only an off-clock presentation observer
to version 1's header; all engine images remain identical between them.
`fixture-v*` preserves source/build/assets provenance; runtime folders contain
strict decoder/protocol helpers and synthetic controls. The initial adapted
wrong-depth negative control was corrected from the newly valid 24-bit scale
to the invalid 16-bit scale before qualification. A successful host control
report does not substitute for an actual target capture.

`captures` preserves ten owned source/native-bound captures: eight packet
controls and two positive observer captures. Savestates, ELF/object binaries,
audio and terrain resources are hash-only. Completed capture reports and
paired comparisons bind the external savestates. Black window screenshots are
retained explicitly. `gs-draw-raster.png` reads actual saved PSMCT32 drawing
VRAM; the enlarged image only scales its observed 17×19 footprint. Both full
RGBA buffers are equal, with 323 nonblack pixels. The readback uses saved
FRAME0/SCISSOR0 and source-pinned GS version-9 layout/swizzle tables.

The isolated compiler attempts are distinct: wrapper scheduling leaves the
early read; globally removing SCE latency handling overflows full residency;
the paired ABS dependency assembles at the original 506 words. Actual native
V26/V27 and completed GIF output, rather than compilation alone, qualify the
last approach. Global build tools and caches were not patched by hand.

To reconstruct sources, first use the prior archive's
`package/assemble-private-source.py` with a hash-matching external terrain splat,
then provide its `pricing-v6-source500` output to `restore-source.py`:

```text
python restore-source.py --baseline-source <pricing-v6-source500> --version 1 --out <new-private-source-v1>
python restore-source.py --baseline-source <pricing-v6-source500> --version 2 --out <new-private-source-v2>
```

The restorer verifies all archived payloads, all 500 baseline inputs and every
one of the final 502 source bytes. It does not copy assets, build or deploy.
Historical laboratory helpers contain absolute local paths and must not be
treated as portable deployment commands. `SHA256.json` covers every payload
except itself. Earlier negative evidence remains in the separate old archive.
