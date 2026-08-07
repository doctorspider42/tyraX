# World Facts: the game's central memory

*Tools > World Facts* is where a game's state stops being scattered variables
and becomes something you can read. A **fact** is a named, typed, documented
piece of world state — `world.power.state`, `characters.marta.trust`,
`player.hasBasementKey` — declared once in a catalog and then read and written
by flow graphs, combined into reusable conditions, reacted to by rules, and
watched live while the game runs.

It is the declared half of what the **Variables** flow nodes already do
undeclared. Those keep working and are a separate namespace: a variable is a
scratch value that exists by being typed somewhere, a fact is state somebody
wrote down. Reach for a fact when the answer to "what does this number mean"
matters to more than one graph.

The whole thing resolves to array indices at build time. There is no
string-keyed store on the console: a fact reference in a graph, a query leaf and
a rule action all become a number in `factNum[]` or `factPos[]`, the same way
object references and save values already do.

## The catalog

Every fact carries five things beyond its name.

| | |
| --- | --- |
| **Type** | Yes/no, whole number, number, one-of-several, or position. |
| **Starts at** | What a new game begins with, and what *Clear Fact* puts back. |
| **Keeps its value** | Session, checkpoint, save game, or profile — see below. |
| **Owned by** | World (one value for the game) or Scene (reset on every scene load). |
| **What it means** | Prose, shown wherever the fact is picked. Worth writing: a catalog is read far more often than it is edited. |

Names are hierarchical by convention — `characters.marta.trust` groups under
`characters.marta` in the list. That is purely a view over the name; there is no
folder to keep in sync, and a project that never uses dots simply sees one flat
list.

**One-of-several** (an enum) is the type worth reaching for early. `power.state`
with options *Broken / Powered / Overloaded* costs the console exactly what an
int costs, and everywhere else — the catalog, a graph's *Fact Is* node, the
blackboard, *Get Fact As Text* on screen — it reads as the name. Two bools that
cannot both be true is the shape that wants this.

### Computed facts

A fact can be **computed from a query** instead of stored. `marta.isAlly` is the
worked example: it is not something the game remembers, it is something the game
works out from her trust and whether she was rescued.

A computed fact has no storage at all — it *is* the query, evaluated wherever it
is read — so it costs nothing, and nothing can write to it. That second half is
the point: an alliance should never be settable behind the back of the things
that decide it. The editor will not offer one to a *Set Fact* node, and a rule
that tries to write one is a build-blocking error.

## Persistence

The tier is the whole persistence story in one field.

| Tier | Lives until | For |
| --- | --- | --- |
| **Session** | The game closes | Anything the world recomputes; the tier the Variables nodes have always had |
| **Checkpoint** | The game closes; *Load Checkpoint* puts it back | Progress within a life — a run's step count, a puzzle's state |
| **Save game** | Written to the memory card slot | Real progress |
| **Profile** | One file per card, outside the slots, shared by every save | Unlocks, a best time, "has seen the intro" |

A **scene-scoped** fact is reset to its default on every scene load, so saving
one stores something already doomed — the editor says so rather than refusing.

### Saving

Facts ride the ordinary checkpoint/save payload, with one difference from every
other block in it: **the rows are keyed by the fact's own stable id, not by its
position.** A fact's id is assigned once (`project::ensureFactIds`) and never
reused, so renaming a fact, reordering the catalog or deleting one leaves an
existing card readable — the rows that still match are restored and the rest are
ignored. That is the migration story, and it is why a fact carries an id at all;
the legacy save values are still positional and still carry the bug this avoids.

The **profile** is its own small file (`profile.sav` next to the ELF, or
`profile.sav` in the game's card directory), written whenever a profile fact
moves rather than on a timer, and read once at boot before the first scene
loads.

## Using facts in a flow graph

The *Facts* node category:

| Node | What it does |
| --- | --- |
| **Set Fact** | Writes one. Three exec pins: `set`, `add`, `toggle` — a counter is *On Button → add* with Value 1 and needs no read at all. |
| **Set Fact Position** | The same for a position fact; a wired position beats the X/Y/Z params. |
| **Clear Fact** | Back to the value the CATALOG declares, rather than a number retyped at every reset site. |
| **Get Fact** | Pure number. |
| **Fact Is True** | Pure bool — the usual way into a gate or an *On Condition*. |
| **Get Fact Position** | Pure position, ready for a Teleport or a Move Object To. |
| **Get Fact As Text** | Pure text. A one-of-several fact prints its option NAME. |
| **Fact At Least / At Most / Is** | Pure bool comparisons. A wired number replaces the threshold, so one fact can be compared against another. |
| **Query** | Pure bool: a named condition (below). |
| **On Fact Changed** | Trigger: the fact's value differs from the frame before — whoever wrote it, graph or rule. The reactive door in, with no polling and no *On Condition* restating state you are already storing. |

*On Fact Changed* carries **three exec outputs** rather than being three node
types: **changed** fires on any move, **became true** on the `0 → non-zero`
edge and **became false** on the way back. So *"when the generator is
repaired"* is one node, with no *Fact Is True* and no *On Condition* beside it.

It costs one float comparison plus two `if`s per frame per node — the same
order as *On Condition*, which is to say nothing at all next to a draw submit.
A **position** fact only has *changed*: three coordinates have no truth to
cross, and the other two outputs are left unwired for one.

Reach past it for a **Query wired into On Condition** the moment the answer
needs more than a yes/no edge — a threshold, several facts at once — because
then the condition is authored once in this window instead of being restated in
every graph that happens to need it.

A fact is **picked from the catalog**, never typed. That is the difference that
buys the rest: the editor knows the fact's type, so *Set Fact*'s Value is a
checkbox for a yes/no and a list of option names for a one-of-several, and a
node naming a fact that was deleted says so instead of compiling to nothing.

## Queries: conditions with names

A **query** is a reusable named condition over facts — `CanEnterBasement`,
`MartaWillTalk`. It is a tree of ALL / ANY / NONE groups over comparisons, and
it may name other queries.

The argument for them is not brevity, it is having one place to change. The same
`CanEnterBasement` gates a door, a dialogue line, an NPC's behaviour and a flow
graph; without it that condition is copied into four graphs and three of them go
stale the first time the design moves.

An empty ALL is true and an empty ANY is false — the same arithmetic in the
editor and on the console, so the preview cannot disagree with the game.

**Why?** Under every query and rule is a live explanation: the condition tree
with the value each leaf actually read, and the child that decided each group
marked. It reads the running game when one is attached and the catalog's
defaults otherwise, so it answers with the game off. That diagnostic is the
reason a condition here is a tree rather than an expression string — the same
structure compiles, evaluates in the editor, and explains itself.

## Rules: reacting to facts

A **rule** is `when <condition> then <actions>`. Actions set, add to or toggle a
fact, or send an event into a graph.

| Policy | Runs |
| --- | --- |
| **When it becomes true** | On the false → true transition. The default, and what you want almost always. |
| **Every frame while true** | Every frame the condition holds. Right for something that must be re-asserted, wrong for anything that counts or sends. |
| **Once per run** | The first transition and then never again. "Per run" is exact — the spent flag lives in the engine and clears when the game restarts, so a rule that must stay spent across a save should write a fact and test it in its own condition. |

Rules tick once per frame **before every graph**, so a fact a rule writes is
already there by the time a graph looks. A rule may write a fact another rule
watches, so the engine re-runs until nothing changes — bounded at 8 passes,
because "until nothing changes" is otherwise a hang with no way to break in on a
console.

The editor detects **reaction cycles** (rule A writes what rule B reads, and
back) and reports the chain as a warning. It settles rather than hangs, but it
is almost never what anybody meant.

A rule is deliberately small: it reacts, it does not orchestrate. Anything with
steps, timing or branching is a flow graph, and **Send Event** is the door into
one.

## The World Blackboard

The *Blackboard* tab is every fact in the running game, live: its value, its
tier, and **who changed it last** — "Set Fact in Main / Door, 12 frames ago", or
the name of the rule that did it. The game rings every fact write with the key
of the node (or the rule) responsible, and the debugger's symbol file turns that
number back into a place in the editor.

It reads the running game through the **Live Debugger** channel
([live-debugger.md](live-debugger.md)), so it needs a debug build with that
preference on. With nothing attached it shows the catalog's own defaults, which
is still what a new game would start at.

**Override** holds a fact at a value. It is re-asserted every frame until
cleared, not applied once — a fact a rule rewrites continuously would otherwise
flicker back before anyone could see it.

**Find Usages** is on every fact and every query: which flow graphs, queries,
rules, computed facts and scenarios reference it. The answer to "what breaks if
I change this", which for a catalog entry is the question that matters most.

## Scenarios

A **scenario** is a saved set of fact values — *"generator repaired, Marta
saved, player has no key"*. Applying one pushes it into the running game as
blackboard overrides, so a situation twenty minutes deep can be looked at now.

*Capture from the running game* is the fast way to make one: play into the
situation once and press it.

## Validation

The window always shows what it can know without running the game: duplicate or
empty names, writes to computed facts, unknown references, enum defaults outside
their option list, scene-scoped facts asking to be saved, query cycles and rule
reaction cycles. Errors are worth fixing before a build; notes are worth reading
once.

A **query cycle** is an error rather than a warning because it would otherwise
recurse forever in the generator; such a query compiles to a constant `false` so
a broken catalog produces a build with a wrong answer instead of a compiler that
never returns.

## What this costs on the console

- One float per stored scalar fact, three per position fact. A computed fact
  costs nothing.
- A query is one folded C++ expression at each place it is read — no runtime
  storage, no lookup.
- A rule is an `if` plus two bools of state, evaluated once per frame.
- The change history, the blackboard and the overrides are **devkit only**: a
  release build carries none of it (`--audit-release` checks), and `factWrite`
  folds to a plain store.

## Limits worth knowing

- **Live Logic cannot hot-patch a graph that uses facts.** The fact store lives
  in the compiled game and the interpreter's IR has no way to reach it, so such
  a graph is reported as needing a build ([live-logic.md](live-logic.md)).
- **Rules write single numbers.** A position fact is written from a graph's *Set
  Fact Position*, not from a rule.
- **A rule's "once per run" does not survive a restart.** See the policy table.
- Ceilings: 512 facts, 256 queries, 256 rules, 32 simultaneous overrides, 128
  changes in the history ring.

## Example

`examples/world-facts` is a level built entirely on this: three generator parts
to collect, an NPC to rescue, a locked basement and a power state other things
react to. Between them the pieces exercise every fact type, every persistence
tier, both scopes, a computed fact, nested queries, all three rule policies, a
rule that sends an event, *On Fact Changed*, and a one-of-several fact printed
by name. `build-scene.py` authors it and is the readable form of the design.
