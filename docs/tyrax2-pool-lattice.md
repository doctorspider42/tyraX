# Private Pool-lattice experiment

The package-local unique-grid kernel reduced repeated light-pool transforms,
but increased inclusive work time on the PS2. It remains a private experiment;
production rendering was not changed.

The candidate starts from the [Pool2 EE producer](tyrax2-pool2-ee-producer.md).
It preserves the original 75-vertex package boundaries, triangle order, texture
coordinates, colors and clipping history. Eligible packages transform shared
position/ST points once and scatter their results in the original order.
Candidate On also pays duplicate validation, compact input copies, replay bypass
and the submission fence needed to protect transient REF owners. This measures
the complete candidate route, rather than VU arithmetic alone.

## Physical result

Both orders used the same native ELF and full night scene, with no effect cuts.
They completed 10,800 loops, 768 raw samples and 30 chunks in total.

| Order | Phase work means, ms | Candidate minus its own controls, ms |
|---|---|---|
| Off / On / Off | 18.965639 / 19.113556 / 19.001525 | +0.147918 / +0.112032 |
| On / Off / On | 19.248373 / 18.988381 / 19.206304 | +0.259991 / +0.217923 |

Work means are inclusive elapsed time minus existing pacing. They include EE
preparation and existing waits; they are not separate EE, VU or GS timings.
Presentation remained approximately 33.3667 ms, or 30 fps. All four contrasts
were slower, so this implementation provides no reason for promotion.
Shared storage, layout and compiled routing cost remain unpriced against an
older ELF. Results must not be subtracted across source versions.

Each sampled On physical cold window observed four admitted/applied packages
and 170 fewer unique-point transforms. Those windows prove use at their sampled
frames, rather than every timed frame's activation or GS consumption.

## Correctness and failed preparation

V10 passed twelve fixed inside captures and six exact packed-output pairs with
fog disabled. Its ordinary night attempt completed but was rejected: a no-fog
guard prevented every candidate admission. That complete negative attempt is
preserved separately and has no physical pricing acceptance.

V11 restored the original fog calculation for each emitted corner, using the
cached unquantized clip vector. Twelve separate fog-enabled captures and six
paired comparisons passed, including varying interior fog values and exact
STQ/RGBA/XYZF2/ADC output and material tags. Both ordinary emulator orders passed
5,400 loops each; all six phase images were inspected without visible missing
effects or stretched geometry. Physical operator appearance confirmation was
not available when this checkpoint was recorded.

The actual linked TC image contains 472 rounded microinstructions. With all
configured classes and billboards, residency is 2006/2042 for VU1 clipping and
1806/2042 for EE clipping. Native object/image identity and retained scheduled
fog chains were independently reviewed. These checks do not prove universal
clipped output, hardware rounding, pixel equality or stable warm replay hits.
The epoch diagnostic intentionally forces replay misses.

## Exact preservation

The [source and evidence archive](tyrax2-pool-lattice-2026-10-05/README.md)
contains fourteen pricing postimages, two diagnostic postimages, strict
controls, raw physical logs, normalized copies, native and runtime proofs,
actual compiler text and images. Binary, SaveState and asset identities remain
hash-only. Its [payload manifest](tyrax2-pool-lattice-2026-10-05/SHA256.json)
preserves exact bytes, including raw Latin-1 device logs.

Starting from the previously verified 499-file Pool2 source-only restoration,
the reconstruction helper reproduced both complete inventories: 501 pricing
inputs and 502 diagnostic inputs. It neither builds nor starts a device;
authored resources and their audio conversions are separate requirements.
