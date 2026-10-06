# PMUv2 permanent-failure latch follow-up

PASS for the repaired unreleased source/control semantics. No implementation edits, builds or runtime performed by this reviewer. This supersedes the prior report observation that fail did not permanently disarm.

NightPMU::failed is initialized false only at process initialization; fail sets it true. No production phase/reset assignment clears it. begin exits before phase clear, setup or sampling when failed. SceneScope constructor also exits before any read/reset/enable on failed. stop deliberately does not early-return on failed: a scope already entered still validates active identity/configuration and stops the still-owned configuration, including a failure raised by a nested scope or changed game context. Changed unknown configuration still receives no write.

Updated root host harness contains permanent-latch tests after high-bit endpoint, game-owner change and unknown configuration. Subsequent same-phase sample and next-phase/setup attempts cannot add configure/reset/enable/stop writes. Its actual source and root command/log proof hashes match. All501 actual manifest files match; manifest remains unfrozen. Manifest SHA256:89152b56a5df577bf1bf5ca75b85250ee662b82e5a2cc74d6e734e889c6d4926.

Root compile-only proof now includes the actual header in a standalone target TU; header/TU/object/disassembly-log pins match local captured files. Target object disassembly reports mfps1, mfpc2, mtpc2, mtps3, sync.p7, mtc00. This resolves standalone assembler acceptance/encoding, not full linked application placement or execution. Compiler binary hashes are root-captured and were not independently reread here.

Runtime remains BLOCKED: inherited Scene can hang before stopping; maximum event-rate and bounded enabled lifetime remain unqualified. Permanent failure handling does not prevent a first overflow before a completed endpoint read, and identical-value foreign takeover remains undetectable by PCCR value guards. No qualified miss or runtime-cost claim.
