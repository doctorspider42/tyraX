# Primary PMU overflow review

Documentation capture only: no fixture/helper modifications, builds or runtime operations. The primary hardware source is Sony Computer Entertainment's EE Core User's Manual, Version 6.0, obtained from a public mirror. `source-pins.json` records its URL/hash and the relevant printed/PDF pages. The previously pinned ps2stuff headers and upstream GNU/ps2dev opcode source provide separate API/syntax evidence. No PCSX2 counter model or COP0 Count wrap behavior is used as a hardware premise.

## Established hardware facts

The [Sony manual](https://docs.alexrp.com/mips/ee.pdf), sections 7.1.1-7.2.8, specifies 31 counter-value bits and bit 31 as OVFL. CTE gates counting and exception generation; reset initially clears only CTE. Counter overflow generates a Level 2 performance-counter exception, described as unmaskable in section 4.2.3. The instruction-stepping example shows counter values continuing beyond the overflow boundary before handler entry. Immediate saturation/automatic stopping therefore cannot be assumed. The manual does not establish a modulo-delta rule suitable for this observer.

`MFPS rt,0` reads PCCR. The I-side selector excludes uncached instruction fetches. The D-side selector also counts uncached/uncached-accelerated loads and cache-disabled loads; it is not exclusively cached-data misses. These event definitions do not state a maximum selector-6 increment rate per CPU cycle. Two-instruction issue width alone does not qualify that rate.

## Consequences for the proposed observer

Rejecting a high bit only at a later endpoint does not prevent a performance exception before that endpoint is reached. Leaving counters enabled across loading, export I/O, a stopped renderer or the entire experiment is unsuitable without an established event-rate/time bound or a qualified overflow handler. No such handler is authorized or validated by this review.

Conditional arithmetic, not a bound: a counter starting at zero would need less than `2^31 / R` enabled CPU cycles if a proven maximum rate were `R` events per cycle. Using an externally qualified CPU frequency of 294.912 MHz gives approximately 7.28 seconds for R=1 and 3.64 seconds for R=2. This review has **not established either rate for selector 6**. Therefore a 128-frame interval lasting about 4.3 seconds cannot be called safe solely because it is less than 7.28 seconds.

Per-Scene common reset/enable/stop in both arms is preferable to phase-long counting because it greatly narrows the enabled lifetime. Put the same configuration work in both arms, preserve ordered four-read On endpoints, and audit actual linked control/read instructions. All common configuration, serialization and code-layout cost remains unpriced versus a binary without the observer. It changes the proposed experiment's common footprint and requires a new source/native binding.

That design still needs an explicit qualification: a Scene containing an unbounded inherited wait may never reach its stop operation. A small observed elapsed duration or low endpoint cannot prove a universal enabled-lifetime bound. Avoid treating this review as authorization to add interrupt masking, an overflow handler, watchdogs or new consumer fences.

Before configuration, sample PCCR immediately and refuse an unknown active owner. At successful completion, stop the owned counters with CTE cleared and synchronization before export or further idle processing. A raw-endpoint-only trial also needs safe enabled-lifetime qualification; declining to calculate deltas does not remove the exception risk.

## Syntax evidence and remaining gate

The [pinned ps2dev binutils opcode table](https://github.com/ps2dev/binutils-gdb/blob/530530b9df4ca59bb66fa01f31a6fc5600f67c97/opcodes/mips-opc.c) contains MFPS encoding `0x4000c800`, mask `0xffe0ffc1`, operand format `t,P`, and EE admission. Sony's mnemonic table specifies operand zero for PCCR. This establishes source syntax/encoding, not actual acceptance or placement in the project's installed linked ELF; root's native audit remains necessary.

The counter width, exception hazard and MFPS syntax gates are now sourced. Maximum selector-6 event rate, general wrap/flag-clear behavior, installed handler behavior and bounded enabled lifetime remain unresolved. Keep miss totals provisional/raw until those gates are qualified. Call the D channel a **D-side bus-read/cache-miss event**, rather than a pure cache-miss bill. No observer tax, current miss cost or recoverable frame gain is measured here.
