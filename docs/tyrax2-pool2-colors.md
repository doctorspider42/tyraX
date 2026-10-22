# Private Pool2 color representation experiment

Pool2 replaces repeated light-pool color inputs with a package-local table, but its completed physical trials did not establish a repeatable speedup.

This is an experimental source archive, not a change to the shipped renderer or its default. It follows the [independent fifteen unconventional ideas](tyrax2-wild-experiments.md). The user confirmed the full-night candidate's car, lights, shadows and HUD looked normal after the reverse physical trial.

## What was implemented

The private kind-7 route preserves original positions, texture coordinates, per-vertex color arrays, 96-source-vertex pool boundaries and the existing 75-vertex package limit. Each eligible package receives a bank-local descriptor and two RGBA128 color records. VU1 selects the original color by source-run position, including packages crossing a pool boundary. The descriptor is emitted inline; no pointer to short-lived metadata enters a DMA reference.

In the observed cold windows, 19 invocations included nine eligible packages and ten fallbacks. The candidate admitted 618 vertices: 618 color qwords became 18 table color qwords plus nine descriptor qwords. Original EE array construction and expanded color writes still run. This experiment therefore demonstrates a smaller input representation, not removal of that EE work.

The ordinary continuation marker is read at every entry, including MSCNT. Uniform-color alpha conversion is preserved exactly: the legacy uniform path's 129 and the direct table path's 128 cannot be silently interchanged. Cache identity includes layout and generation. The candidate's nine-qword retained block exceeds the unchanged seven-qword capacity and safely misses retained replay; that cost is part of this candidate. No capacities, packet limits or fences were enlarged.

Two failed native revisions are retained. An uninitialized VU integer register failed first; the next revision exhausted OpenVCL register allocation. V3 narrows register lifetimes and passed the actual toolchain, linked-object and ABI checks. Resident totals are 1,908 VU microinstructions for VU1 clipping and 1,708 for EE clipping, including the 206-instruction billboard code, below draw-finish at 2,042.

## Physical results

Both orders used the same ELF, source and 298 runtime assets. Each completed 5,400 loops with 384 raw samples. Values below are inclusive elapsed time minus existing pacing, not a pure EE or VU execution bill.

| Order | First phase | Middle phase | Last phase | Candidate minus each control |
|---|---:|---:|---:|---:|
| Off / On / Off | 19.483231 ms | 19.354911 ms | 19.473371 ms | -0.128319 / -0.118460 ms |
| On / Off / On | 19.448272 ms | 19.469797 ms | 19.470380 ms | -0.021525 / +0.000582 ms |

Presentation stayed approximately 33.367 ms in every phase, around 30 fps. The forward improvement did not repeat in reverse order. These results do not justify promotion or a claim of 60 fps. They also do not price the common new branches against an older ELF: only each trial's own arms are compared.

Cold counters and source-expansion comparisons occur at untimed offsets 750 and 1,155 in each phase, with replay bypassed equally in both arms. They report zero mismatches and invalid inputs. These sparse comparisons establish cold source input correctness, not actual packed VU output or continuous warm replay correctness.

## Actual VU output checks

A separate diagnostic ELF exercises two fixed cases in twelve fresh emulator boots: each arm, each case, and one, two or three completed render iterations. Case A contains 96 source vertices split 75/21. Case B contains 192 vertices, exercising a crossing 75-vertex package and a 42-vertex tail. Its first 75-vertex package has been overwritten in the final banks and is explicitly excluded from the comparison.

Six paired SaveState decodes matched every tested STQ, RGBAQ, XYZF2 and ADC qword, plus GIF state and primitive headers: 1,917 payload qwords and 36 header qwords in total. Actual 16 KiB VU1 memory and microcode came from CRC-verified SaveState members after normal frame completion, not PINE's VU1 address reads, which returned misleading zero data. ELF text, EE-ready value, paused halt PC, configs and owned process shutdowns are bound to the captures. MTVU was disabled in this correctness profile.

Static inputs have no per-iteration epoch. Repeat-two and repeat-three cases bind completed calls and final-bank equality, but cannot independently prove a fresh warm VU execution for every iteration. This is fixed-case emulator packet equality, not universal floating-point equivalence, GS consumption, physical VU precision or performance evidence.

The archive preserves three host-control repairs: an initially misfocused Space key disabled a breakpoint; a missing host audio library raised a blocking modal; and one graceful emulator shutdown needed an exact-owned second signal. Later correctness profiles use host audio backend Null. No such observer or host audio change entered the physical pricing ELF.

## Reproduction and remaining work

The [source archive](tyrax2-pool2-source-2026-10-05/README.md) includes exact overlays, host checks, native audits, failed revisions and a verified source-only reconstruction helper. The [evidence archive](tyrax2-pool2-evidence-2026-10-05/README.md) retains raw physical logs, analyses, lifecycle records and decoded capture proofs. Full ELF binaries, SaveStates, images and assets remain external hash-pinned laboratory artifacts. The [checkpoint index](tyrax2-pool2-2026-10-05.json) binds both packages.

Any future promotion needs a repeatable physical benefit, removal or pricing of remaining EE expansion, and broader precision/clipping/replay coverage. The unique-grid representation remains a separate unimplemented candidate. No additional device run is required to close this experiment's recorded scope.
