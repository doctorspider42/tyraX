# Actual host Live Debugger command encoder

The Linux helper links actual checkout src/livedbg.cpp and includes actual src/livedbg.hpp; it uses their atomic writeCommand and readSnapshot APIs without format replicas or stubs. It was compiled with WSL g++ C++20. This directory contains source, binary and build log. Compilation does not attach any editor or start a game.

Root may execute `command-encoder write <fixture/bin/livedbg.cmd> <fresh-nonzero-sequence> [watch-index ...]` against its explicitly owned fixture. The default Command state has no breakpoints, pause, forced nodes, capture or RAM measurement. Writing an accepted command establishes the ordinary diagnostic session; this is intentional observer activation and not release-default or performance evidence. A repeated sequence does not apply another command. Every command replaces the full desired state, so use only for the isolated receiver diagnostic session; do not overwrite a separate debugging user's commands.

Root may execute `command-encoder snapshot <fixture/bin/livedbg.bin>` to parse a complete snapshot using the actual repository parser. Exit 3 means incomplete/unreadable snapshot; retry boundedly because writes can race. Printed seq/frame/scene and watched object transforms/active flags witness debug progress and object state, not dynamic-light receiver flags or animation cursors. Actual lighting ownership still needs independent visual/runtime witnesses.

Root-only execution examples from WSL:

```
/mnt/f/Projects/tyrax2-lab-20261001/player-light-public-command-encoder-v1/command-encoder write /mnt/f/Projects/tyrax2-lab-20261001/player-light-public-runtime-v1/fixtures/vehicle-players/bin/livedbg.cmd 1001
/mnt/f/Projects/tyrax2-lab-20261001/player-light-public-command-encoder-v1/command-encoder snapshot /mnt/f/Projects/tyrax2-lab-20261001/player-light-public-runtime-v1/fixtures/vehicle-players/bin/livedbg.bin
```

No game build, emulator, device command or fixture channel write was executed by the helper preparer. The accompanying editor-format-build.log is an independent normal editor rebuild for the source agent's whitespace-only template substitution fix; the durable runtime editor binary is retained unchanged.
