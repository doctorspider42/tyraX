# tyra-editor-dev — gameplay: areas, reverb, scrollers, players, input

From `### 3` (runtime geometry, areas/reverb, scroller, player/multiplayer,
input actions), `docs/areas.md`, `docs/reverb.md`, `docs/multiplayer.md`,
`docs/input-bindings.md`, `docs/player-speeds.md`.

## A new "0 = inherit" field
**Cause/rule:** player movement speeds are three tiers: `SceneObject::playerWalkSpeed`
is the base; `playerRunSpeed`/`playerSprintSpeed` default to **0 meaning
inherit**, and the fallback chain lives in exactly two functions —
`project::playerRunSpeed`/`project::playerSprintSpeed` (plus
`settingsRunSpeed`/`settingsSprintSpeed` for the Player-less fallback walker).
**Fix:** codegen bakes the RESOLVED numbers into `PLAYER_RUN_SPEEDS[]`/
`PLAYER_SPRINT_SPEEDS[]` and the Properties panel prints them through the same
two functions, so the runtime needs no fallback branch and the panel cannot
promise a speed the console does not run. The tiers are written to the `.tyra`
**only when non-zero** (an untouched project resaves byte for byte); 0 resolves
to run = walk and sprint = walk x `sprintMultiplier` (an old project MOVES
identically). The stick's deflection ramps walk→run
(`WALK + (RUN - WALK) * stickMag`) while sprint pins the top FLAT. Both walkers
(`updatePlayerWalker` and the FPP tail's) carry the expression, and
`drivePlayerAnim` normalises `speedFrac` by the RUN speed. The Player walker
reads the tiers from `PlayerCtl::speeds[3]`, seeded from the tables at scene load
(a variable, not the constants, because a Live Link v3 record (80-byte stride)
streams a speed edit through `ScriptContext::playerSpeeds`; the three fields are
OUT of `liveLinkRecipeHash`, look speed stays baked). A per-object sprint CLIP
(`playerSprintClip`, "" = the run clip) is chosen from the sprint BUTTON, never a
speed threshold, and its playback rate divides out sprint/run so a clip authored
at sprint pace plays 1x.

## Player / two-player work
**Cause/rule:** the generated game's walker state is a per-player `PlayerCtl`
struct (`players[2]` in the game hpp templates) and the walker is
`updatePlayerWalker(PlayerCtl&, pi, Tyra::Pad&)` in `TPL_GAME_CPP_SCENE` — NOT
the old loose `entX/entYaw/...` members.
**Fix:** scene tables come in pairs (`PLAYER_*` / `PLAYER2_*`, first/second
Player object per scene) selected via the `PP_*(pi)` macros in scene_data.hpp; a
new per-player Player-object property must be added to the paired table emitter
(one loop emits both prefixes) and read through a new `PP_` macro.
`ProjectSettings::multiplayer` ("off"/"shared"/"split") + `p2JoinOnStart` gate
everything; menu bind 7 = Player count (edge-triggered +
`syncPlayerCountMenuValue` write-back).

## Anything that reads a button
**Cause/rule:** never emit `pad.getClicked().<Button>` for gameplay. The
generated game reads inputs through **named actions** — `inputPressed(pad,
IA_ROLE_JUMP)` / `inputClicked(...)` from `inc/input_map.gen.hpp`, defined in
`src/gen/input_map.gen.cpp` (`inputMapHeader`/`inputMapSource` in templates.cpp).
**Fix:** a new built-in behaviour needing its own button adds an
`InputAction::Role` (input.hpp) → a `kSeeds` entry in
`project::ensureInputActions` (so existing projects get a default binding) → a
role slot in the `kRoles` table of `inputMapHeader` (emits `IA_ROLE_*`, -1 when
the project has no such action) → the read site. The three layers the runtime
folds are preset → player override (an `inputCodes()` index persisted in a save
value by a `MenuEntry::RebindKey` row, applied by
`TerrainGame::applyInputBindings`) → a **user-owned** `controls.hpp`'s
`BTN_*`/`KEY_*` (wins only when it disagrees with the default preset).
`Pad::injectVirtual` folding of the USB keyboard/mouse is table-driven in the same
generated TU (`inputApplyKeyboardMouse`). `inputCodes()` is the dense rebind code
space whose index 0 means "the preset's binding" and whose numbers land in
memory-card saves, so it is **append-only** (`INPUT_CODES` is its generated
twin). `kPadButtonNames` is Tyra::PadButtons order. The raw `OnButton` flow node
stays raw; `OnAction` is the configurable one.

## Areas: replace a hand-typed distance with a placed volume
**Cause/rule:** `PrimitiveType::Area` (docs/areas.md) is the reference for this
pattern. The volume is an ordinary `SceneObject` (transform = the box),
references are BY NAME (`SceneObject::catchArea`, `SceneLayer::streamArea`, a
flow node's `str` with `FlowParamKind::AreaName`), and the point test lives in
exactly two places — `project::areaContainsPoint` (host: editor previews AND
codegen) and `pointInArea` emitted into `scene_data.hpp` (both generated TUs).
**Fix:** putting the runtime twin in the generated DATA header keeps it a single
definition; `project::areaCaughtObjects` is the ONE expansion used by the
Properties preview, the viewport mirror preview, the baked target tables and
`batchBlockedNames`. A new consumer calls those — do not re-derive the box math.
A catch area can also be **live** (`SceneObject::catchAreaLive`): only
`project::areaLiveCandidates` — objects that can move — is re-tested, and that
predicate (`project::objectRuntimeMovable`, over `project::runtimeRefNames`) is
the exact complement of the immovability `staticBatchEligible` relies on, so a
live candidate always has the solo bag a second submission needs. The immovable
rest stays baked in the fixed list and movables are dropped FROM it. Candidates
bake into a shared `CATCH_CANDIDATES` table sliced per owner (`liveArea`/
`firstCand`/`candCount` on `MirrorData`/`PortalData`/`CamFeedData`);
`TerrainGame::collectLiveCaught` walks a slice plus the spawn pool.

## Reverb zones: a sound is heard in the room the listener left
**Cause/rule:** an area can be a **reverb zone** (docs/reverb.md) — the SPU2's
hardware reverb, authored as a room. A `REVERB_ZONES` side table keyed by
(scene, object) leaves the BOX unbaked, so `TerrainGame::updateReverb` reads the
live transform through `pointInArea`. The console has **two** reverb units, one
per SPU2 core; `updateReverb` cross-fades rooms across them (the incoming room
takes the free unit while silent, then both depths ramp).
**Fix:** the per-sound control is a BIT (`SceneObject::soundReverb`, the Play
Sound node's `Dry`), not an amount. **The obligation on ANY new code that plays a
sound:** a unit is reachable only by voices on its own core, so every play site
must offset its channel by `ScriptContext::reverbBusBase` (0 = core 1, 24 = core
0); a site that forgets is heard in the room the listener just left. Anything
caching per-channel state needs the bus in its key (`sndChBus` beside
`sndChVol`/`sndChPan`). `reverbPresets()` (flowgraph.hpp) is the single preset
table read by the Area combo, the Set Reverb node and codegen — its ORDER IS THE
WIRE FORMAT (`Tyra::AudioReverb::Preset`, libsd's `SD_EFFECT_MODE_*`), so append
only. **The voices are FINITE** (24 per bus, 16 Play Sound / 8 emitters,
docs/sound.md): a new play site goes through `pickSoundSlots` (the emitter loop's
per-frame ranking, priority then loudness, with a steal margin) or
`flowPickSfxChannel` (an ended voice, else the lowest priority strictly below,
else the sound is DROPPED — a drop stays unlogged). The runtime table is per bus
and resets when the room moves to the other core.

## A type whose data drives OTHER baked objects
**Cause/rule:** `Scroller` (type 19) is the reference.
**Fix:** codegen APPENDS clone objects to the scene table (authored indices must
never shift, or every flow graph / mirror / player reference silently retargets)
and emits a generated director script that repositions them each frame. Mirror
the `scrollsim.cpp` shared-host / `scroller.gen.cpp` per-frame-twin split so the
editor preview and the PS2 runtime compute the same layout from one source of
truth — and keep any formula duplicated in the generated twin flagged as such in
both files.

## Reference facts (upstream `flow-graph-input-player.md` extras)
- Files: `flowgraph.hpp` (~2.75k): `FlowNode`, `FlowLink` (exec/object-id/position/bool/text/number kinds; `toPin`/`fromPin`), `FlowGraph`, `flowNodeTypes()`, custom registry (`CustomFlowNode`, `customFlowNodes()`), `reverbPresets()`. Graphs are per object in `objects/<id>.json`. `flowgraph_ui.cpp` (~2.3k): imnodes editor; `flowNodeDoc()`; `paramTip()` after `IsItem*`; `paramTipShown`. Codegen `flowGraphScript()` -> `src/gen/flow_graph.gen.cpp`.
- Navigation `navmesh.cpp/.hpp` (~175+50): host-only `bake(Project, SceneData)` -> `navDataHeader` -> `inc/nav_data.gen.hpp` (gated on AI nodes existing); viewport overlay `App::updateNavOverlay` (signature-cached); `src/gen/navigation.gen.cpp` runs A*; `blocksNavigation` is a marker skip list.
- Input `input.hpp` (~270): `InputAction` (name + label + `Role` + rebindable), `InputBinding`, `InputPreset`, `InputMap` (`Project::input`, `Section::Input`); `kPadButtonNames`, `inputKeyNames()`, `inputCodes()` (append-only; `INPUT_CODES` twin); `project::ensureInputActions`. `inc/input_map.gen.hpp` / `src/gen/input_map.gen.cpp` (`inputMapHeader`/`inputMapSource`, `IA_ROLE_*`).
- Player: speed tiers `PLAYER_RUN_SPEEDS[]`/`PLAYER_SPRINT_SPEEDS[]`; two players use `PLAYER2_*` tables via `PP_*(pi)` macros and `PLAYER2_INDEXES` (split view needs a second `Player`); `ProjectSettings::multiplayer` + `p2JoinOnStart`; menu bind 7.
- World Facts `facts.cpp/.hpp` (~960+360): `layoutOf` (`factNum[]`/`factPos[]`); condition TREE folded to C++; `cyclicQueries`; `facts_ui.cpp` (`Section::Facts`); `project::renameFactRefs`. Commit Checkpoint sentinels `SAVE_COMMIT_AUTOSAVE`/`SAVE_COMMIT_NEXT` resolved by `resolveCommitSlot`.
