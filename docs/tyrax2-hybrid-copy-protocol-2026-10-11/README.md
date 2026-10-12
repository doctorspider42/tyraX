# Hybrid2 Independent Copy Ownership Model

This private A41 component models safe arbitration between a VBlank copy claim
and the existing main-thread fallback before target integration. It builds on
the linked SDK/source audit, not a new hardware timing measurement.

## Verified Component

`tools/tyrax2-hybrid-copy-protocol.hpp` uses a36-byte, fixed-width volatile state;
readiness is12 bytes. Actual optimized and ASan+UBSan host executables each pass
144 readiness/publication guards and161280 exhaustive eight-event permutations
over four job IDs, plus multi-job, counter-wrap and late-completion lifecycle
cases. The same driver cross-compiles to a MIPS EE relocatable object, including
both ABI static assertions. Sources, exact compiler commands/versions, logs,
executables, object and hashes are retained in [V1](v1/report.json).

- Arm only from Idle after actual old submission and immutable packet publication.
- Claim requires matching job/packet revision, old DMA/exclusive-FINISH/GIF
  readiness, current prefix still unsubmitted, open blank and no copy already
  claimed in that field.
- Main withdrawal prevents a subsequent ISR claim. Fallback cannot claim during
  the field in which it withdrew: wait for a fresh edge, as the existing SDK does.
- ISR-first and main-first serializations cannot issue two copies. Source remains
  protected through Armed/Withdrawn/Copying; release requires actual copy DMA AND
  owned copy FINISH completion, not merely old-frame completion.
- Completion evidence survives source release until VBlank end, so successful
  early main consumption does not look late. Copy still unfinished at end counts
  as late even if it finishes/retires later; reject that target output trial.
- A released job/packet cannot reuse the same blank. Deferred fallback completes
  in the next fresh field once modeled readiness becomes true.

The first sanitizer shell command was rejected by PowerShell's unquoted comma
before compilation; quoting the flag and the structured runner both succeed.
The initial protocol allowed a fallback to use the same open blank after
withdrawal; review adds the withdrawn-field guard BEFORE the accepted V1 runner.

## Limits and Target Gate

Subsequent [adapter component](../tyrax2-hybrid-copy-adapter-2026-10-11/README.md)
passes actual adapter-code/packet/backend compile gates but finds a delayed-IRQ
window gap. A software END-handler witness alone cannot prove physical blank
completion; explicit hardware-grounded copy-window proof remains required.

This is NOT extracted actual Core/queue/MMIO code yet. Modeled booleans are
proof obligations for the adapter, not hardware facts. Volatile is NOT mutual
exclusion or cache publication. Main transitions need interrupt serialization
and memory ordering; ISR transitions must run in the corresponding handler.
ISR claim must happen immediately at VBlank START; an open-blank predicate alone
does not establish enough remaining copy time. Zero/wrapped model job IDs do
not qualify the queue's special zero-sequence behavior; target must handle or
reject unsupported epochs explicitly.

Target adapter must clear/consume the OLD exclusive FINISH before copy kick and
distinguish the COPY completion event. Prepare/publish immutable DMA data on EE,
not in ISR. Bind actual prior sequence and GIF/DMA readiness, preserve current
recording context/pending prefix/bank/HUD and prevent next clear while copy uses
its source. Model assumes each transition is atomic; it does not test unsynchronized
threads, actual INTC priority/latency, cache visibility or interrupt teardown.

Before pricing: test extracted actual adapter with hardware mocks, native binding,
positive real old-job/copy activation, full exact source/main/env gates and
physical completion-before-blank-end witness. Then same-ELF Hybrid2-only A/B/A
day/night and reverse. No runtime fix, production default, console deployment,
60Hz proof or impossibility result in this checkpoint.

Reproduce under WSL with a fresh output directory:

```sh
python3 /mnt/f/Projects/tyra-editor/tools/verify-hybrid-copy-protocol.py /mnt/f/Projects/tyrax2-hybrid-copy-protocol-replay --ee-cxx /home/spider/.cache/tyrax/native/toolchains/3594ba072b1f303047137e63/ee/bin/mips64r5900el-ps2-elf-g++
```

`v1/sha256.json` inventories every original runner output except itself; this
explanatory README is outside it. No proprietary game state is included.
