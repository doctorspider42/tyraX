# Target layout and linked VU footprint qualification

Actual R5900 g++ compiled current PipelineInfoBag and the HEAD predecessor through an isolated old-header include overlay. All other header dependencies are the actual checkout and actual PS2SDK; .d files, objects, sources, warnings and disassembly are retained. No engine/game cache was rebuilt or modified. Both compilations produced no warnings.

PipelineInfoBag grows from 40 to 44 bytes, alignment remains 4. The new bool occupies offset 32, which moves dynLightSkipSlot to 36 and spotLit/dateLit/blssProxy to 40/41/42. This is a real +4 bytes per bag and a binary layout change; current engine/game code must be rebuilt together. The target-compiled constructor probe returns 1, establishing default reception enabled in this compiled source (not a host substitute).

The actual current stripped game ELF and its actual linked symbol ELF match the native build record hashes. CodeStart/CodeEnd addresses come from that symbol ELF; bytes come from the actual game ELF sections. Fifteen of sixteen compared VU images are identical to the archived main11 private proof. StaPipVU1Cull_TC has 2448 bytes/306 microinstructions now versus 4048 bytes/506 in that private baseline, whose corona SPRITE prototype is deliberately not production-promoted. It is not a change caused by the receiver option. The comparison is retained with DIFF status, not rewritten to a blanket identity PASS.

head-vu-closure-proof.json separately verifies every tracked .vclpp/.vcl/.vsm source and recursive local source includes against HEAD, with actual byte pins and line-ending-normalized equality. Linked program image sizes measure individual microinstruction footprint, not their simultaneous resident layout, dispatch cost or achieved FPS. No hardware performance claim follows from this evidence.

Initial stripped-ELF symbol lookup failure is recorded separately; corrected extraction uses the actual symbol ELF. No full old engine build, emulator or device run was performed.
