# Raw evidence: the EE submission rearchitecture

Item 3 of [docs/ee-submission-rearchitecture.md](../../../../docs/ee-submission-rearchitecture.md)'s
order of work, built behind the gate designed in
[docs/baked-stream-acceptance-gate.md](../../../../docs/baked-stream-acceptance-gate.md).

**No console was touched.** `192.168.100.150` belongs to another agent this
round. Everything here is either host arithmetic or PCSX2 counts and pixels,
and the hardware milliseconds are a later slot.

## The gate's self-test, and why it is the first thing in this directory

The gate's whole claim is one sentence: **two chains that hand VIF1 the same
words hash the same, however they were built.** Everything the redesign is
allowed to do rests on it, so it is demonstrated rather than argued — and it
does not need a PS2 to demonstrate, because it is a property of the decoder.

```bash
g++ -O2 -std=c++17 -I stub -I ../../../../vendor/tyra/engine/inc \
    -DTYRA_STAPIP_VIFHASH=1 -o gate-selftest gate-selftest.cpp && ./gate-selftest
```

`gate-selftest.cpp` **includes the engine's own translation unit**
(`vendor/tyra/engine/src/renderer/3d/pipeline/static/core/stapip_vif_hash.cpp`),
so it exercises the shipped decoder and cannot drift away from what the console
folds. `stub/` holds the four SDK headers it needs — `tamtypes.h`, a `qword_t`,
a `dma_tag_t` and an empty `debug.hpp`. Nothing is read from the tree, so it
keeps working with the gate compiled out.

Output: [`selftest-output.txt`](selftest-output.txt). Every line is `ok`.

| what it checks | result |
| --- | --- |
| a 36-quadword control chain and a 2-quadword baked chain over the same geometry | **hash identically** |
| three packets against one, over the same bag | **hash identically** |
| two baked packets against one unbaked packet — *the case the counter gate forbade* | **hash identically** |
| one vertex word flipped inside a `REF` payload | caught |
| one package dropped from the run | caught |
| the same packages in the wrong order | caught |
| `MSCAL` naming a different microprogram address | caught |
| the prim GIFtag's `NLOOP` off by one | caught |
| a texture upload moved between a different pair of draws | caught |
| six extra VIF `NOP`s | **not** caught, correctly |
| an unknown VIFcode | latches `broken` and names the code |
| a `REF` address that is not quadword aligned | refused, not hashed |

### Three things the self-test settled that the design page had only asserted

**The cadence really is invisible, and the reason is narrower than "NOPs".**
Two facts have to hold together. `StaPipCore::render` calls
`clearLastProgramName()` **once per bag, not per packet** (`stapip_core.cpp`
line 693), so a bag split across two packets emits `MSCNT` at the head of the
second and does not re-kick; and the end tag's two VIFcode slots are `NOP`, so a
packet boundary contributes nothing to the stream. The first version of this
test reset the kick state per packet — modelling a plausible engine rather than
this one — and three packets then hashed *differently* from one. **If either
fact ever stops holding, this test fails and the gate says the cadence is
visible, which is the correct answer at that point rather than a bug in the
gate.**

**A stub `dma_tag_t` with the wrong bitfield layout silently turns every `REF`
into a `CNT`.** The hardware puts `ID` at bits 28..30; a stub that put it at
26..28 made the decoder walk off the end of every chain and segfault. Recorded
because it is the kind of thing that, in a version that did not crash, would
have produced a confident and wrong hash. `stub/dma_tags.h` now carries the
ps2sdk layout and a comment saying so.

**A DMA `REF` address is 32 bits, and a 64-bit host has to be honest about it.**
The test reserves a sub-4 GB arena and places everything a `REF` can name inside
it, because the shipped decoder resolves the address field exactly as the DMAC
does. That is the test being faithful, not a workaround.

## The fixture check, before any measurement in this directory is quotable

Mandatory on every arm, and it is not implied by anything else:

```
ROADSTRIP scene 0 strips 1 packages 470 triangles 31050
TERRAINSTRIP scene 0 chunk 2,2 strips 1 vertices 588 packages 8 triangles 512
grep -n "stripRun = 7" <fixture>/src/terrain_game.cpp   ->   75u
```

**A matching capture hash is a PICTURE check and never a FIXTURE check.** Both
round-one failures matched the expected capture hash while measuring a 72-run
scene. And the editor is rebuilt from this worktree before the fixture is
regenerated: `--refresh-gen` regenerates faithfully with a stale baker, and an
editor binary sitting in `build/` is not the editor at the tree's commit.

## Files

- `gate-selftest.cpp` — the host self-test above, over the engine's own decoder.
- `selftest-output.txt` — its output.
- `stub/` — the four SDK headers the host build needs.
