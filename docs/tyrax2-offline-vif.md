# Offline final VIF geometry experiment

This private follow-up to [offline terrain](tyrax2-offline-world.md) moves final geometry packet production before gameplay. It retains the original repeating ground texture, lifted shadow subsets, colour precision, reflections and Hybrid2 presentation. Material composition is excluded.

The final geometry stream includes native SDK partitioning, inline VIF/UNPACK headers, payloads and VU start/continue commands. Camera matrices, clip state, fog parameters and material heads with current TEX0/CLUT VRAM addresses remain late bound by the existing compiler. This is not an entirely frozen whole-scene DMA chain.

## Reproduction and acceptance

`tyrax2-offline-vif-fixture.py OUT SOURCE --bootstrap` clones a source-bound, current-builder offline-terrain price fixture. Before recording it reserves the ordinary world textures and restores every one of the 1370 effective terrain LOD/edge variants. The actual SDK compiler exports each geometry fragment; no host reimplementation guesses its byte layout. Separate emulator and PS2 bootstrap data retain the previous platform-specific native colour inputs.

`tyrax2-offline-vif-compile.py SOURCE OUT --oracle-platform emulator|ps2` validates record structure, metadata, dimensions and complete coverage, then reuses the terrain factorer's base/sparse-edge representation. It reconstructs every final native geometry stream exactly and rejects malformed inputs. Structural metadata must be invariant across edge variants and identical across platforms. VU destinations and package capacity are validated at runtime; a different program layout fails admission rather than executing a stale address.

`tyrax2-offline-vif-fixture.py OUT SOURCE --compiled EMU --ps2-compiled PS2` makes the moving comparison. Off uses the current native terrain producer and native final compiler. On restores both raw offline terrain and final geometry VIF. The measured difference therefore includes both stages; the earlier raw-only experiment supplies context, not an additive timing control. Loading and common resident tables stay outside the price window. A scoped callback copies the final stream and applies edge words directly into the existing owned world-cell body. The original owner publication, epoch validation, retained old readers, first-wrap prefix and FLUSHE behavior remain in force.

The original SDK/materialized full-body audit remains mandatory. An additional geometry audit compares prepared final bytes against the actual SDK outside recording. `verify-world-cell-price.py` retains exact 54 held rasters, six endpoint reconstructions, 3942 poses, capture/paint workload and owner delivery gates; `--quality-trial` is forbidden here. It additionally requires the actual final-VIF path, exact SDK witnesses and per-platform asset pins. Use `tyrax2-offline-terrain-stage.py` so physical attempts receive both PS2 assets. Emulator runs use the private invisible WSL/Xvfb session.

## Physical results

The [sealed machine report](tyrax2-offline-vif-2026-10-20.json) binds the bootstrap and moving sources, ELF, logs, platform assets and complete readers after the owned emulator and file-server processes stopped. Both platforms pass six trips, 54 exact held main/environment/display rasters, six exact reconstructed endpoints and 3942 equal pose rows. Each On phase records 195 actual prepared geometry copies and 36 additional exact SDK geometry audits outside recording; the original full-body SDK oracle also passes. Old queue readers remain positive through replacement, and delivery has no fallback. A physical reference/On image pair was also inspected directly.

| Measurement | Native producer/compiler | Raw terrain plus final VIF offline |
| --- | --- | --- |
| Compiler/publication elapsed per replacement, 93 replacements per trip | 0.578–0.587 ms | 0.451–0.453 ms |
| Same-ELF mean non-pacing saving, forward / reverse | Reference | 0.077975 / 0.043239 ms |
| Actual physical presentation rate | 59.060–59.169 Hz | 59.278 Hz |
| Late presentation intervals per 537 moving frames | 8 / 7 / 7 | 6 / 6 / 6 |

The local compiler/publication boundary becomes about 22–23% cheaper, but that boundary is visited on replacements, not every frame. Material-head construction, body assembly/publication and the ordinary per-frame rendering remain. The measured boundary does not identify the exclusive cost of each remaining operation. Non-pacing elapsed includes scheduling and remaining waits; it is not pure EE time. This is not stable 60 Hz and does not demonstrate a substantial gain beyond the earlier raw-terrain trial (0.057/0.046 ms in its own ELF). Do not subtract results across different runs/ELFs to claim an isolated VIF saving. All three On presentation traces retain the same six late intervals as that earlier raw-only experiment.

All 1370 native final streams factor exactly: 47,972,016 geometry bytes become a 1,440,532-byte artifact per platform, with 4562 changed edge words. Native table capacity is 1,473,504 bytes in addition to 1,345,264 bytes for raw terrain: 2,818,768 bytes total, excluding allocator overhead and 3300 bytes of static binding metadata. Ordinary arrays and final owned bodies remain resident too. The table/load footprint is common to the two measured roles, so this is not pricing against a pristine table-free executable. This prototype's RAM cost and small net saving do not justify a production default.

The initial packaging reader rejected an LF/CRLF copied-header digest mismatch; the corrected manifest pins the actual native-bound header bytes and retains the repository digest separately. The metadata writer now emits canonical UTF-8/LF bytes, matching its recorded digest on Windows and Linux. Fresh compiler runs on both platforms produce byte-identical binary and metadata artifacts, including exact stored-file digests. The sealed compiled metadata was canonicalised accordingly. These packaging corrections changed no tested ELF, binary terrain/VIF asset, packet byte, raster criterion or timing sample. The incomplete first bootstrap construction and intermediate failed builds remain unqualified artifacts.

Production integration, a portable host colour exporter, full-current-production/night/variable-dt coverage and a fully relocatable material-state format remain separate work. The console stays powered; all owned test processes have stopped. No production defaults were changed.
