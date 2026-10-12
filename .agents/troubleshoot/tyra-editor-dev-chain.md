# tyra-editor-dev — the chain (adding a feature)

`### 3. A feature usually touches the whole chain` from the skill. Before
coding, list which parts your change needs — most features need most.

## A new scene-object property is missing from undo
**Cause:** `SceneObject::operator==` was not updated. `History::push()`
short-circuits on equality, so the new field compared equal to what it replaced
and nothing was pushed (`shadowMode` shipped in 1.62.0 without it, fixed 1.67.0;
the symptom read as a broken undo).
**Fix:** add the field to `SceneObject` **including `operator==`** in project.hpp
→ `objectJson` + `readObjectsArray` in project.cpp (save AND load; default the
read for backward compatibility; match the emission style of a similar field —
some bools are always emitted, some omitted at their default) → properties UI in
app.cpp (+ `commitChange()`) → `sceneDataContent()` in templates.cpp → game
runtime in the `terrain_game.cpp` template (`TPL_*` strings) → viewport rendering
if it is visual.

## A new object type blocks the player or eats a raycast
**Cause:** a geometry-free marker type was not added to every marker skip list,
which are scattered by *number* (`o.data.type == N`), not by enum.
**Fix:** add to `PrimitiveType` (0–20 used, `kPrimitiveTypeCount` bounds "every
type" loops; keep values stable — serialized), then every list: the USE-target
scan, the carry/throw sweep, `physObstacle` and the geometry `switch` in
templates.cpp, `flowRaycast` in `flowGraphScript`, plus `blocksNavigation`
(navmesh.cpp) and `objectShape`/`regionCountFor` (aobake.cpp, whose `default`
already excludes unknown types). If the type needs per-object variable-length
data, do not grow the fixed `SceneObjectData` POD — emit a flat side table into
scene_data.hpp keyed by (scene, object), the `OBJECT_SCRIPT_ATTACHES`/`MIRRORS`/
`SCROLLERS` pattern. Collision goes through the single pair
**`objectCollides`/`objectCollisionBox`** (host twin `placement::collisionBox`/
`collides`) — a new collider type is TWO functions, not six.

## A new flow-graph node is undocumented for the AI or in the wrong menu
**Cause:** `.desc`, per-parameter tips, or `.category` were skipped; all are read
by three consumers (add-menu tooltip, node-hover tooltip, the AI generator's
`nodeCatalogLine`).
**Fix:** a `flowNodeType` entry in flowgraph.hpp (designated initializer;
`.category` is the add-menu submenu, derived by `flowNodeCategories()` — a new
category costs a string; `.desc` mandatory by convention, and a tip on every
parameter: `.numTips[i]`, `.strTip`, `.str2Tip`, `.execInTips[i]`). `.desc` says
what the NODE does and why; a tip says what ONE knob does. Never restate a raw
slot name (`num[0]`, `str`) in prose — address a parameter by the label its
widget carries (`flowStrLabel`/`numLabels` are the single source). Then node UI
(pins, params) in the flow-graph editor, then codegen in `flowGraphScript()`
(templates.cpp) compiling to `src/gen/flow_graph.gen.cpp` (one script class per
object graph; object references resolve to indices at codegen; bool logic folds
into inline C++).

## A numeric parameter reads as `0.000` instead of a named choice
**Cause:** it is really a small enum but was left as a bare number; the editor
otherwise branches on the label STRING (`"Loop"`, `"Channel"`, `"Times"`) — a
heuristic a new node cannot join.
**Fix:** declare `.numChoices[i]`, a `'|'`-separated list of option labels in
value order; the editor draws a dropdown. The stored value is still `num[]`, so
codegen, links and existing projects are untouched. A declared choice wins over
every string heuristic.

## A computed value in a graph
**Cause/rule:** a value a graph computes rides the **number plane**
(`FlowLinkNum`, `numIn`/`numOut`): a wired number REPLACES the target's `num[0]`,
mirroring `posIn` over X/Y/Z. Codegen resolves it to a self-contained float C++
expression (`numExprImpl`/`numOperand` in `flowGraphScript`), so a value needs no
runtime slot.
**Fix:** `flowNumFolds()` is the single predicate deciding whether an input folds
over every link or takes only the first, read by the editor's link pruning AND
codegen; it reads two declared flags, `numFold` (n-ary: Add, Min, Modulo) and
`numInExtra` ("the wire is an operand of its own, num[0] is a separate param" —
Clamp's value between Min/Max, Number At Least's value against its Threshold).
Inference was wrong (`pure && numIn && numOut` is true of a unary Sine too). A
new number consumer reads `numOperand(n)` instead of `n.num[0]`, and accepts
that **Live Logic cannot patch it** (`capability()` rejects any graph with a
number link — the IR carries num[] as compile-time constants).

## A pos→num→pos cycle recurses until the stack goes
**Cause:** `posExprImpl` and `numExprImpl` are mutually recursive through the
forward-declared `numInputVis`/`numOperandVis`; a cycle that hops planes is
invisible to either guard alone.
**Fix:** those two take the **visited path**, not a fresh one, and
`numExprImpl` must drop **its own id** from the path before calling
`posExprImpl` (or the position walk reads the node as visited and skips its own
input link). `--refresh-gen` on a deliberate pos→num→pos cycle is the check. A
new *int* consumer must round (the plane is float, `flowInt` is not) and, if it
names variables, join BOTH copies of the collect list (`collectFlowVars` in
templates.cpp and the identical walk in livelogic.cpp) — a variable named only by
a getter still takes its index slot, and a missing entry shifts every index after
it. A position is three independent C++ expressions, so a long Vector chain is
re-emitted once per component per consumer (`PosRotateY` folds `sinf`/`cosf` at
codegen time for a typed-in constant).

## A node that decides where exec goes next
**Cause/rule:** the `Flow` category (Branch, Sequence, Gate, Switch Number,
Timer, Tween, For Loop) needs per-branch emission.
**Fix:** set `execOutCount` + `execOutLabels` and emit each branch. The branch a
link LEAVES is `FlowLink::fromPin` (serialized `"fpin": N`, omitted at 0); output
0 keeps pin slot 1, outputs 1..7 take slots 18..24 (`kFlowMaxExecOut` = 8), and
**`flowExecOutCount(t)` is the one answer** to "how many outputs does this type
have" (read by the editor's pin submission, both link-validity checks, and
codegen). Inside `actionCode`, `branch(outPin, pad)` returns that output's whole
chain as inline C++; each branch walks with its OWN COPY of the visited path
(two outputs of one Sequence may reach the same action, which then runs twice,
while a link back into the path is still a cycle). Nodes needing per-frame state
(Timer, Tween, Cooldown) declare it through `addMember` in the per-node state
pass and tick in the `update()` prologue like Delay — never straight into
`members`, or the time machine cannot see it. **Live Logic cannot patch a
branching node**: `capability()` rejects any graph where `flowExecOutCount > 1`,
and `livelogic.cpp`'s exec walk filters `fromPin != 0`.

## Several triggers on one node (show/hide/toggle/add)
**Cause/rule:** the old convention was a Show*/Hide* *pair* of node types; the
five surviving pairs were merged away (Set Object Visible / Set HUD Visible / Set
Text Visible / Set Layer Loaded / Animation).
**Fix:** set `execInCount` + `execInLabels` and switch on the `pin` argument in
`actionCode(n, pad, pin, visited)`. The pin a link fires is `FlowLink::toPin`
(serialized `"pin": N`, omitted at 0); pin ids come from `flowExecInPin` (slot 2
for the first, spare slots 10..15 for the rest — `kFlowMaxExecIn` = 7). **Retiring
a node type DELETES every graph that used it** — `readFlowGraph` drops an unknown
type silently with no rename table; if a retirement must survive existing
projects it needs a real migration step + format bump. The merge only fits when
both branches share the node's param (`Play/Stop Music` and `Play/Stop Sequence`
stayed separate).

## A project-specific node wants C++ changes
**Cause/rule:** prefer a **custom node** — a `flow-nodes/*.flownode` file, no C++
change (`flownode.cpp`, `docs/custom-flow-nodes.md`).
**Fix:** custom nodes plug into the same `flowNodeType()` lookup, the add-menu
via `flowAllNodeTypes()`, and a `flowCustomNode()` branch in `actionCode()`. A
`call = fn` custom node runs a user function in `inc/scripts/flow_nodes.hpp` via
`FlowNodeIO`. Its **object output is a runtime value**, so `resolveTarget()`
returns a C++ int-*expression* (`objOut<id>`), and built-in object actions fed
such a ref are bounds-guarded by the wrapper `actionCode()` emits. The built-in
**Raycast** node uses the same runtime-latch machinery (every
`flowCustomNode(...)` check also accepts `type == "Raycast"`). The **AI nodes**
(Patrol Waypoints / Chase Player / Flee / Stop AI / On Player Seen) compile to
`navigation.gen.cpp`; a new AI node usually only needs a new `nav*` entry point
plus an `actionCode` branch. Anything changing what blocks walkability must
update `navmesh::bake` (host only — the game reads the baked bitmap). Header keys
and `{placeholder}`s are mirrored in the VS Code extension's `SPEC` table
(`tools/vscode-tyrax/extension.js`) and grammar — keep them in sync.

## A new procedural node generates nothing or lies about runtime support
**Cause/rule:** the node registry is the single source; the window renders params
from it and the bake only sees the Output.
**Fix:** an entry in `procNodeTypes()` (procgraph.cpp: pins, params with UI
ranges, `.rows` kind, mandatory `.desc`, **and a `.tip` on every parameter** —
the tooltip is `.desc` plus one line per control) → a branch in `evalNode`
(procgen.cpp) → **decide whether it can run at RUNTIME too**: if it can, add it to
`kRuntimeNodes` AND write its `emit*` in procrt.cpp (the table is what the
capability check reads, so a node listed without an emitter is a promise the
compiler cannot keep); if it cannot, leave it out and it is reported honestly.
Derive randomness from the point key, and make a generator's count a PREFIX of
its sequence rather than a reseed. A runtime emitter must reproduce those numbers
EXACTLY (same hash, same channel indices). A node that reads the scene must have
its inputs covered by `bakeHash`, or a stale bake will not be noticed.

## A project preference a scene can override adds a key to every existing project
**Cause:** a serializer that follows `writeSceneVisuals` and emits every category
whether active or not would break `--resave` byte-identity.
**Fix:** one `bool` on `SceneOverrides` (+ its `operator==`, or undo drops it), a
branch in `project::resolvedSettings`, and a serializer that writes the flag and
the scene-local values **only when the override is on** (the neural upscaler's
`"upscaler"`/`"blss"` pair is the worked example). Everything downstream reads
`resolvedSettings`, never the raw field.

## Changing a member initializer changes old projects' behaviour
**Cause:** every `read*Section` guards on `find("key")`, so the struct
initializer is what a project saved *before that key existed* loads as.
**Fix:** when a fresh project should start somewhere else, keep the legacy answer
in the struct and **`project::create` assigns the new one** (the AmbiencePreset
`aoEnabled` precedent; `buildProfile = "debug"`, `keyboardMouse = false`, the
Empty preset's `orbitSpeed = 0`). `create`'s block is also the only place that
may scale metric-by-definition defaults by `ProjectSettings::unitsPerMeter` (the
FPP preset is a 1.8 m player at any scale), and the *New Project* modal's
per-field prose belongs in a `prefHelp("...")` `(?)` tooltip, not `TextDisabled`
paragraphs.

## The starting preset (`Project::gameTemplate`) is create-only
**Cause/rule:** it picks which **user-ownable** game-template sources are
generated (`src/terrain_game.cpp`, `inc/terrain_game.hpp`); flipping it would
overwrite the user's work or leave an owned file no longer matching what the
project builds.
**Fix:** the three presets (`fpp`, `thirdperson`, `orbit` (Empty)) live in ONE
table, `kNewPresets` in app.cpp, read by both the dialog and the disabled
Preferences row; `project::create` is the only writer. The two player presets
generate the SAME sources (`Project::hasPlayerTemplate()` is the one place they
are treated as one) and differ only in the seeded Player object's `playerMode`. A
new preset is a row plus a `create` branch; a new game template is that plus a
`hasPlayerTemplate`-style predicate at the `templates::generate` fork.

## A tooltip placed before an `IsItem*` query stops the edit from saving
**Cause:** `paramTip()` must be called AFTER every `IsItem*` query on the widget it
documents — a tooltip is a window and ImGui's "last item" is context-global, so a
`paramTip` placed before the `IsItemDeactivatedAfterEdit()` that commits an edit
silently stops the edit from being saved the moment the cursor rests on it.
**Fix:** call it after the query; a drawn param tip also suppresses the node-level
tooltip that frame (`paramTipShown`). `flowNodeDoc()` is the ONE tooltip renderer
(title, `desc`, then a line per parameter and per exec pin), used by both the
add-menu tooltip and the node hover (the procui.cpp `procNodeDoc` precedent) — the
two editors must not grow two answers to "what does hovering a node tell me".
