# GS SPRITE corona private V6 bounded shrink

V6 removes seven redundant source lower operations from immutable V5 so the full resident program set can be attempted again. V5 actual native22 assembled TC512 microinstructions and overflowed the VU1Clip+billboard budget by four words (2046 versus draw-finish2042), so it was rejected before launch. V6 is source-only; its scheduled instruction count and resident fit are not known.

The deterministic preparation and source-handoff proof record these exact replacements:

- Read the stable package header once at postpass entry, using that value for marker and count: one fewer ilw.
- Keep the0x20 Q-bound sign mask in existing fogInt across the two bound comparisons: one fewer initialization overall.
- Use the stock persistent adcMask instead of constructing another0x8000 mask: two fewer lower operations.
- For final RGBA, Z/F and Q comparisons, use the expected15/3/2 value in fogInt as the FMAND mask too: three fewer duplicate initializations.

No VI/VF names were added. The stock body before vertexLoopsDone is byte-identical. Every data load/store, floating operation and branch instruction is unchanged, and every F/Z/RGBA/Q/ADC/rectangle/span/state/fallback gate remains. The only changed source file is stapip_cull_tc_vu1.vclpp; the other499 source files, including kind9 controls, counters, producer, packet writer and cache keys, are byte-identical to V5. Default remains Off. No renderer class, effect or billboard was removed or evicted.

adcMask is initialized by MakeTyraAdcMask before begin and is used read-only by the original clip checks. The postpass does not write it. The native22 oracle independently observed persistent VI02 holding0x8000 through the stock loops/postpass/MSCNT; this is prior-artifact evidence only. V6 actual allocation must independently show its own persistent register remains intact. fogInt is a dead existing per-vertex temporary by the postpass. Each FMAND expected-mask value is initialized before its read, and the same named VI is not written between FMAND and the comparison branch. V6 native must verify distinct live physical destination/expected registers and MAC producer/consumer schedule. Fewer source instructions do not prove fewer scheduled words or correct flag latency.

Host checks passed720,915 deterministic source/VI-mask controls, plus47,293 bounded-output/state checks and269 kind9/parser/counter/offline-decoder controls. The shrink controls replay the recorded V5→V6 patch exactly, check the seven-operation delta, unchanged stock body/data operations/branches, persistent ADC read-only use and full16bit mask/ADC/header domains. They do not simulate VU MAC timing, allocation or rasterization.

Root owns the next serial native compile. Reject on uninitialized/register errors, bad FMAND mask/flag lifetime, persistent ADC/spot/options clobber or full-family resident overflow. Preserve actual object/expanded VCL/scheduled VSM and compare linked TC bytes; review all resident classes including both billboards. No fit claim is made in this source handoff. The actual V5 native artifacts are retained separately in wild-gs-sprite-corona-tc-native-v22-v5.

V5's actual kind9 control contract remains: Off/On/Off and reverse in one ELF, all effects masks0 and pool2 fixed On. Sparse cold counters report requests, not accepted VU output. capture-contract.md and corona_controls.py specify paired completed-output evidence and exact endpoint/tag/FGE validation. Actual execution provenance, positive sprites in the unchanged pricing pose, off-clock pixel equality and physical measurement still remain. The fixture contains no native outputs or runtime assets; a future complete native fixture needs qualified authored res AND .res-baked with exact298 assets including4ADPCM. No build, device, shared cache/mirror, production or V5 mutation occurred here.
