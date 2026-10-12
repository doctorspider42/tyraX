# A24: clip grouping and destructive no-clip control

This private experiment compares conservative plane-aware strip grouping with
removal of the complete software-clipping route during Vehicle Playground
driving. Production renderer/defaults and authored projects are unchanged.

## Contract

The user requests both the candidate and a no-clipping comparison, specifically
to avoid optimizing a region which might not matter. The source is bound A23,
but its rejected adjacent-list bypass is removed: qbuffer code matches the
ordinary A16/production implementation byte-for-byte. EE paint, roads, three
vehicles, live reflections, ordinary frame pipeline and real dt are retained.
The rejected target gate stays off. No new scope clocks are introduced.

One ELF runs `Off/Group/Off/NoClip/Off`, repeated for day/night. Warmup and
parking remain 1,200 + 120 loops, then ordinary full throttle until z>=108,
with the existing camera sweep. Sparse offset%128==64 witnesses run in every
mode; that frame and its successor are excluded from pricing in all modes.
Compare non-pacing whole-frame work and eight equally weighted route bins.
Do not subtract different real-dt frame indices or price emulator cadence.

Group mode changes only consecutive true Clip subranges of refined strips.
Outer strip packages, classification, winding/order, Cull/Outside routes,
active plane masks and existing queue/expansion ownership remain unchanged.
Eligibility is narrowly the inspected ordinary textured, many-colour, unlit
class with no billboard/override. The 460-qword half must fit
`2 + 9 + (3 + 3*(p+1))*N` and retain 91 qwords. One active plane permits 39
input vertices (typically three existing four-triangle blocks together).
Six or unknown/zero-mask planes keep the existing 12-input cap. Every wider
job asserts the bound before submission. Other program classes stay unchanged.
This is not C18 routing refinement, C20 road repacking or a new cache policy.

NoClip mode uses local effective `fullClipChecks=false` without mutating shared
info bags. It suppresses clip-plane setup, clip subdivision/strip expansion
and clip-program submission; ordinary whole-bag/package frustum rejection
and cull programs remain. Cull can discard crossing triangles rather than
cut them, so surfaces can disappear. More off-screen packages may also reach
VU under the existing clip-disabled policy. This is a **destructive workload
contrast**, not a quality-equivalent fix, pure EE price or exact upper bound.
An assertion and sparse witness require zero clip calls in this mode. A small
net change would weaken this lead for the tested route; a large gain only
motivates isolating the responsible preparation/consumer work next.

After each trip, five held-state ordinary renders `Off/Group/Off/NoClip/Off`
settle eight frames/arm with current reflection capture, fixed interleave and
GS target. Read main/environment VRAM: 100 images/platform. Group and returned
controls must be exact; NoClip differences are reported, not silently accepted
as quality equivalence. Checkpoints are outside priced travel.

## Preparation checkpoints

Private implementation and reader are prepared. The host capacity/group-range
harness passes 131,072 cases under ASan/UBSan, including mask-zero and high-bit
fallback. Initial fixture V1 stops on an obsolete 14-occurrence source anchor;
the bound source has 11, so no V1 build/deploy occurred. Corrected V2 is separate.
Native/emulator/physical results were pending at that checkpoint. Completed
results are below; no promotion follows from preparation alone.

V2's first moving day Group witnesses have no merged jobs. Source review
finds that the initial implementation covers refined strips only; ordinary
roads still take the unrefined strip loop. V2 remains useful for the full
NoClip contrast and must finish without changing its bound ELF. It is not
proof that plane-aware capacity is ineffective on the unrefined route.

V3 adds capacity-bounded input chunks to that existing unrefined loop as well.
Its per-triangle expansion keeps the global strip triangle index, so odd-width
chunks preserve alternating winding. A further 730 odd/even chunk/order host
cases pass under ASan/UBSan. Outer packages, topology and current conservative
parent masks remain unchanged. V3 runs only six trips `Off/Group/Off` day/night;
NoClip still appears in cold five-arm raster checks, but is not repriced there.
Read V2 for no-clip timing and V3 for the widened candidate, never subtract
different ELFs. Additional program classes remain outside this intervention.

## V2 completed no-clip result

All ten trips complete on emulator and physical PS2, bound to 475 files and ELF
`22e6ac07d696d4c67619357083f296c09481f5973203f08eab499d700c064c3f`.
Both readers pass twelve rejection controls and 100 readbacks each. Group and
all returned controls are exact; NoClip images differ substantially in both
main/environment views. Physical NoClip witnesses report zero clip submissions
and positive bypass selection in both moods. No allocation/DMA/assertion errors.

| NoClip contrast | Bracketing Off mean ms | NoClip mean ms | Mean saving ms | Equal-distance saving ms | NoClip entry Hz |
| --- | ---: | ---: | ---: | ---: | ---: |
| Day | 18.478777 | 15.609735 | 2.869043 | 3.120602 | 44.270314 |
| Night | 20.667956 | 17.552219 | 3.115736 | 3.188333 | 37.562185 |

The adjacent day controls are 35.837/36.147 Hz, night 33.859/33.286 Hz.
NoClip p95 remains 20.593/24.376 ms and max 25.212/27.943 ms: sustained 60 Hz
is not reached even after removing necessary cutting. This rejects "no net
benefit from disabling the clip route" for this test, not every possible frame.
It does NOT attribute the saving exclusively to EE: subdivision, input work,
VU cutting, changed submitted geometry/GS work and waits change together.
Real dt and frame-index camera sweep also produce different moving view
histories when cadence changes. Equal-distance means reduce frame weighting
bias but do not make those histories identical. The large net contrast is a
reason to investigate this region, not a literal recoverable EE budget.

V2 Group has zero physical merges in its sparse witnesses (one tiny merge
only in emulator night); its -0.156/-0.101 ms mean contrasts do not price the
intended ordinary-road intervention. Do not promote it or call the unrefined
route tested by V2. V3 is the separately bound correction described above.

PS2 deployment follows the editor-network-reset procedure, not repeated blind
resets. The V2 owned client is stopped after completion to settle its log; the
console remains powered on and V3 uses a fresh editor-protocol deployment.
Evidence preservation/read-only archive reanalysis is tracked below.

## V3 completed grouping result

All six ordinary trips complete on both platforms, with exact Group and
returned-control rasters in all 60 readbacks/platform. Both readers pass eleven
rejection controls. Native/source binding is 475 files, ELF
`c5dff3b4569a8af59d11caadf01d575285739f69f8c740dfe1cd8060b298f07f`.
No output-budget assertion, allocation or DMA error occurs. Physical sparse
witnesses positively select wider groups: day removes 34 old jobs across
three frames, night 25; maximum observed input is 18 vertices. Emulator sees
up to 39 input vertices and removes 41/72 jobs across four frames per mood.
These are sampled witnesses, not whole-trip totals or matched-pose prices.

| Group contrast | Bracketing Off mean ms | Group mean ms | Mean saving ms | Equal-distance saving ms | Group entry Hz |
| --- | ---: | ---: | ---: | ---: | ---: |
| Day | 18.462458 | 18.480897 | -0.018439 | -0.010686 | 35.963994 |
| Night | 20.656370 | 20.508386 | 0.147984 | 0.115649 | 33.923264 |

Day p95 is 25.025608 ms against 24.830492/25.077162 controls; night is
27.967156 against 27.924750/28.191135. Night control drift is 0.228740 ms;
candidate saves only 0.033614 ms versus the first and 0.262354 versus the last.
This is a small mixed result, not a robust general moving gain or a reason to
enable the optimization by default. Neither mood reaches sustained 60 Hz.
Keep the private implementation and close this particular grouping proposal
as the larger preparation fix. Do not infer that all clipping is cheap: V2's
complete NoClip contrast is about 3 ms, with large raster/workload changes.

The remaining distinction is EE preparation versus consumer cutting/geometry:
if revisited, a separate destructive intervention could retain the ORIGINAL
EE clip classification, subdivision and expansion, replacing only the final
clip-program dispatch with ordinary cull. This would isolate a different net
route contrast, still not pure instruction costs or quality equivalence.
Do not run it implicitly or subtract V2/V3 ELFs; it needs its own same-ELF
controls and bypass proof. More group sizes or cache knobs are not justified
by the completed result.

## Evidence

V2's 711-file `no-clip-v2` archive preserves sources, both runs, 200 raw GS
readbacks, CSVs, logs, ELFs, asset hashes and verification tools. Both read-only
reanalyzes reproduce the reports byte-for-data with the inventory unchanged.
V3's 632-file preservation is `group-v3`; both archived-platform reanalyses
also pass with data-identical reports and an unchanged inventory. Use the
archived reader with `--archive --output` outside the archive.
Tools include the final V3-aware generator and reader;
bound fixture source files, rather than regenerated-source assumptions, are
authoritative for each revision. V1 was never built or launched.

Both owned emulator instances and console clients are stopped only after their
runs finish. PS2/computer remain on. Production renderer, version, project
format and authored maps are unchanged; no default promotion is claimed.
