# tyra-editor-dev — live channels and the devkit zero-cost rule

From `### 3` (Live Link, texture hot reload, Live Logic, the devkit's zero-cost
rule). See `docs/live-link.md`, `docs/live-logic.md`, `docs/devkit.md`.

## Live Link
**Cause/rule:** `App::liveLinkTick` (each frame from `drawUI`; docs/live-link.md)
streams scene edits into the running game by rewriting `bin/livelink.bin` (atomic
tmp→rename; `TXLL` v2 header + one 64-byte record per object + seq-echo footer),
which the generated `src/gen/live_link.gen.cpp` (`templates::liveLinkScript`;
empty TU in release or with the preference off) polls over `host:`. Gated on the
**debug** build profile + `ProjectSettings::liveLink` (default; toggled by the
toolbar LIVE chip, *Build > Live Link* and *Preferences > Build*). Records address
objects by `project::liveLinkIdHash` (baked as `SCENE_*_OBJECT_ID_HASHES` in
scene_data.hpp), so renames/reorders are safe; newly added objects are
**live-spawned** from an equal-recipe template via the runtime spawn pool, and
deleted ones are hidden.
**Fix:** consistency is guarded by the as-built record `bin/livelink.sig`
(`project::liveLinkSigFile`: per-object id + `liveLinkRecipeHash` + a context
hash, stamped by the Runner at build start, which also deletes stale
`livelink.bin`); recipe drift / unspawnable new objects
(`liveLinkCanSpawnLive`: baked lights, projecting decals, mirrors, objects with
graphs/scripts) flip the chip from green LIVE to amber LIVE (rebuild) and stop
writes. **If you add an object property**, decide where it belongs:
build-time-baked or clone-relevant fields go into `liveLinkRecipeHash` (and new
unspawnable categories into `liveLinkCanSpawnLive`), or Live Link will silently
not show that edit while claiming LIVE. The snapshot seq is seeded from the clock
at attach — a restarted editor must never reuse a seq the still-running game
already applied.

## Texture hot reload
**Cause/rule:** `App::liveTexNotify` (in mateditor_ui.cpp, the poller in
`liveTexScript`) rides the same gate. **The game knows a texture by the path it
was SHIPPED under, which is not always where the texture lives.** `bakeAnimAssets`
renames every texture an animated model's override `.mtl` names into a copy next
to the `.tskl` (`animBakedTextureRel` — `res/materials/x.png` ships as
`models/hero__ovr3a65_x.png`).
**Fix:** an announcement under the texture's own path matches nothing and the
repaint silently does nothing. `templates::animTextureAliases` is the single
reverse lookup (it derives the same names the bake does) and a paint updates AND
announces every one of them as one GROUP. One record matching no loaded texture is
expected (the game loaded one of the group's other paths); a whole group matching
none is the reload not happening, and the poller soft-errors naming each path it
tried. Anything that changes where a texture is baked to changes
`animBakedTextureRel`, and both callers follow.

## Live Logic
**Cause/rule:** `App::liveLogicTick` (each frame from `drawUI`;
docs/live-logic.md) is the third live channel, the one that changes BEHAVIOR:
debug profile + `ProjectSettings::liveLogic`. Codegen emits
`src/gen/livelogic.built` (per graph: scene + object id + `livelogic::graphHash`)
at build start; the editor compares every live graph against it and compiles only
the ones that differ, so untouched graphs keep running their native C++. The seam
in the generated game is one line per script — `if (livelogic::patched(scene,
ownerIdx)) return;` — so a graph is EITHER interpreted or compiled.
**Fix:** **adding a node type to the interpreter is adding a twin**: the opcode
goes in `livelogic.hpp`, the runtime body in `liveLogicOpBodies()` (templates.cpp)
and the mapping in livelogic.cpp's `actionMap`/`triggerMap`; the body must behave
exactly like the C++ `actionCode` emits for that node, and the capability check
derives from the same tables so the editor can never promise a node the
interpreter lacks. Patched graphs share EVERYTHING with compiled ones (the
`flowInt/flowBool/flowPos` statics via generated accessors, save values,
RuntimeObject state, and the Live Debugger node keys carried in each instruction).

## The time machine
**Cause/rule:** `App::livetimeTick` (docs/time-machine.md) is the third direction
of the same host: channel, and it carries one invariant of its own: the capture
walk in `liveTimeSource` (templates.cpp) is the list of everything a rewind puts
back. **A new field that the RUNNING game mutates — on `RuntimeObject`, on the
flow-variable storage, on the save values — must join that walk**, or rewinding
silently loses it while the panel claims the world came back.
**Fix:** capture and restore are twins in one function pair, in one file, in that
order; never two lists. Anything the walk cannot reach through `ScriptContext`
(the walker's fall speed, a graph class's own timers) is deliberately out and is
*named* in the panel and the doc rather than quietly missing. Bumping the walk's
shape means bumping the layout-hash mix in `liveTimeSource` too, or an old capture
will be accepted into a world that no longer matches it.

## The devkit's zero-cost rule
**Cause/rule:** a release build carries NOTHING (docs/devkit.md). In practice:
(1) the generated runtime becomes an empty TU, (2) the generated header keeps the
API as `inline` no-ops with predicates that are compile-time `false` so call sites
fold away, (3) the instrumentation in `flow_graph.gen.cpp` is not emitted at all,
and (4) no static arrays exist. Do not add a runtime `if (debugEnabled)` — that is
a branch and a table in a shipped game.
**Fix:** the rule is CHECKED: `elfsym::auditRelease` scans the built ELF for the
`TXDEVKIT-` markers and channel file names, `--audit-release` exits non-zero, and
every release build runs it. **A new devkit layer must plant its own marker**; a
new instrumentation call must go through a generated header that no-ops. Two more
easy misses: the layer's file name goes in `kStringNeedles` (elfsym.cpp), and its
generated `.cpp`/`.hpp` must join `refreshGenerated`'s overwrite list (the Remote
Pad's runtime reached a RELEASE ELF because it was generated once at project
creation and never refreshed). Run the audit in BOTH directions: `--audit-release`
against the DEBUG ELF must FAIL and name your layer, and against a release build
must come back clean. And a new channel file must be **deleted before launch** in
BOTH of the Runner's clean-up blocks (`runner.cpp` has one for the PCSX2 path and
one for the ps2link deploy): a leftover from the last session is applied on the
first poll of the fresh boot. That applies to a file the game WRITES too —
`bin/frame.tga` is deleted, because the last session's picture is a perfectly
valid answer to this session's first capture. That deletion is also why the
Debugger keeps every capture as a PNG under the project's `screenshots/`;
`platform::revealInFileManager` walks up to the nearest ancestor that exists, so
*Show file* on a removed path does not open the user's Documents folder.

## Reference facts (upstream `devkit-runner-and-network.md`, channels)
- Zero-cost rule: a release build carries nothing (empty runtime TU, inline no-op header, no instrumentation, no static arrays); `elfsym::auditRelease` scans for `TXDEVKIT-<layer>` markers + `kStringNeedles` channel file names; `--audit-release` exits 0/1; a new layer plants its own marker, adds its file name, routes instrumentation through a generated no-op header, and joins `refreshGenerated`'s overwrite list; audit BOTH ways. `elfsym.cpp/.hpp`.
- Cadence (1.90.0): `ProjectSettings` `liveLinkPollFrames`/`liveLogicPollFrames`/`liveDebugPollFrames`/`liveDebugSnapshotFrames`/`timeMachineFrames` (0 = platform default or 1..120), format 54 additive; link cadence also controls texture polling; cadence never gates graph execution, watch sampling, Remote Pad or replay input.
- Channels tick from `drawUI` (`liveLinkTick`, `liveLogicTick`, `livedbgTick`, `livetimeTick`, `remotePadTick`, `replayTick`, `pollGameError`), each standing down on `!hasProject_`.
- Live Link (`App::liveLinkTick`): debug profile + `ProjectSettings::liveLink`; rewrites `bin/livelink.bin` (tmp->rename; `TXLL` header, `LL_VERSION` 4, one 80-byte record per object, seq-echo footer); `src/gen/live_link.gen.cpp` (`templates::liveLinkScript`); records address objects by `project::liveLinkIdHash` (`SCENE_*_OBJECT_ID_HASHES`); `bin/livelink.sig` via `project::liveLinkSigFile`; recipe drift / unspawnable objects via `liveLinkCanSpawnLive`; new property joins `liveLinkRecipeHash`; streamed fields stay out.
- Texture hot reload: `App::liveTexNotify` (mateditor_ui.cpp); poller `liveTexScript` / `live_tex.gen.cpp`; `bakeAnimAssets`+`animBakedTextureRel` ship `models/hero__ovr<hash>_x.png`; `templates::animTextureAliases` is the reverse lookup.
- Live Logic (`App::liveLogicTick`): debug + `ProjectSettings::liveLogic`; `livelogic.cpp/.hpp` (~850+225) with `BlockKind`/`OpCode`/`CondOp`/`PosKind`, `Block`/`Instr`/`Program`; templates.cpp GENERATES the enums/dispatch; `compile()` mirrors `flowGraphScript` (`resolveTarget`/`posExpr`/`boolInputsOr`); `capability()` gate; `graphHash()` excludes positions; `builtListText()`/`loadBuiltList()` (`src/gen/livelogic.built`); seam `if (livelogic::patched(scene, ownerIdx)) return;`; new interpreted node twins `liveLogicOpBodies()` + `actionMap`/`triggerMap`. `uiScriptTick`.
