# The log panels — errors, warnings, verbose

![Output and Debug panels under the viewport](img/editor-overview.png)

The editor shows two logs, and they are the same problem:

- **Output** — everything the *Runner* does: the docker container, the asset
  bakes, the PS2DEV compiler, the launch, and the `[ps2]` stream a real console
  sends back over ps2link.
- **Debug** — a log *file*, tailed from disk (last 1 MB, reloaded twice a
  second). Two sources: **Game log** (`bin/log.txt`, the running game's own
  `TYRA_LOG` output and its assertion dumps) and **Emulator log** (PCSX2's
  `emulog.txt`).

Both are mostly noise with a few lines that matter. So every line is classified
into one of four levels, each panel gets a **filter chip per level with the count
of entries in it**, and the lines are drawn in the level's colour:

| Level | Colour | What lands in it |
|---|---|---|
| **errors** | the theme's danger red | `====ERR:` from the engine, a `TYRAX` assertion / non-fatal-error dump, a compiler `error:`, `make: *** [...] Error 1`, `undefined reference`, `ld returned`, and the editor's own "…failed" / "not found" lines |
| **warnings** | the theme's warn amber | `==WARN:` from the engine, a compiler `warning:`, and the editor's "Warning: …" / "Could not …" / "cannot …" lines |
| **info** | normal text | what the **editor** says about a build — `=== Build OK ===`, *Compiling (PS2DEV toolchain)…*, *Launching PCSX2* (the `[editor]` channel, when the line carries no diagnostic) |
| **verbose** | dim text | everything else: the `> …` command echoes, raw docker/rsync/compiler output, and the game's own `LOG:` commentary |

Errors and warnings also carry a mark in the left gutter in their colour (a
fainter one on their continuation lines), so problem lines are found by
scanning one column. Click a chip to hide or show that level; the choice is remembered per panel in
`editor.ini` (`logMaskOutput`, `logMaskDebug`) because which noise you want
hidden is a property of how you work, not of the project. Nothing is hidden by
default. **Copy** copies the lines currently shown — the filter applies.

## The console drawer

In the *Default* layout, **Output**, **Debug** and the **Debugger** are three
tabs of one bottom dock node — one drawer for everything the build and the
running game say (Output is the front tab; a debug launch brings the Debugger
forward, see [live-debugger.md](live-debugger.md)). Projects that saved their
Default arrangement keep it; *Layout > Reset to built-in arrangement* applies
this one. The other built-in layouts are unchanged.

Both log panels share one **header row**: the panel's own actions on the left
(Output: *Clear*, *Copy*; Debug: the source combo, *Reload*, *Auto*, *Copy*,
*Clear log*), then *Select text*, then the level chips pushed to the right edge.
When the drawer is too narrow, the pieces move to a second row instead of being
clipped (the chips stay right-aligned). The Debug window adds one quiet second
row: the file it is reading, dimmed and cut with an ellipsis (the full path is
its tooltip), and *Pop up on errors* at the right end.

Every entry has a **time** in a dim column left of the message (`HH:MM:SS`).
Output lines already carry the Runner's stamp, so the column shows that one and
the message is drawn without it. A Debug line is stamped when the panel first
sees it — which for a file that existed before the editor opened is the moment
it was first read, and a reset (*Clear log*, switching the source, a new run
recreating `bin/log.txt`, the 1 MB tail moving) restamps everything. Continuation
lines and blank lines show no time, so the column marks where each entry starts.
*Select text* and *Copy* give the raw lines: Output's keep their stamp, Debug's
have none.

The drawer **tabs carry a count**: a small pill on the *Output* / *Debug* tab
with the number of error entries (danger colour) or, when there are none, of
warnings (warn colour), "99+" past 99. The tab's label is the window's name,
and the name is its id (layouts, `--ui-script` targets and `editor.ini` store
"Output" / "Debug"), so the count cannot go into the title. Instead the window
is submitted with `ImGuiWindowFlags_UnsavedDocument` while it is docked and has
something to count — the stock flag that makes a tab reserve a marker slot at
its right end — and `App::drawLogTabBadge` paints the pill over that slot,
reading the tab's rect from the dock node's tab bar (ImGui internals:
`DockNode->TabBar`, `TabBarFindTabByID(window->TabId)`). A floating log window
gets neither the flag nor the pill.

## The two rules worth knowing

**A diagnostic is a run of lines, not a line.** A compiler error is followed by
its source snippet and its notes; an assertion dump is a banner plus a `|` body.
Those continuation lines inherit the level of the entry above them, so *hide
verbose* leaves an error **complete** instead of leaving the `error:` line
stranded without the four lines that explain it. It is also why the counts read
"1 error" for that whole block: the chips count **entries**, not lines.

**The earliest marker in a line wins.** `[editor] Warning: texture bake failed:
…` and `[editor] ISO export failed: …` both contain "failed", and only the first
is a warning — whichever word comes first is what the line is about. The
Runner's `> <command>` echoes skip the marker scan entirely, so a `-Werror` on a
compiler command line is not an error and a path containing the word is not
either.

The classification is heuristic for tool output and exact for the engine's own
prefixes (`LOG:` / `==WARN:` / `====ERR:` come straight from
`vendor/tyra/engine/inc/debug/debug.hpp`). If a line lands in the wrong bucket
the fix is a marker in `src/logview.cpp`, and the harness below is how to check
it.

## Getting to Output while a build runs

*Output* is usually a tab in a dock node, so it is often behind something else
exactly when a build is happening. The status bar carries the state instead: a
spinning **BUILDING** chip at its left end while the Runner works, a red
**BUILD FAILED** afterwards if it did not make it, and nothing at all otherwise.
Clicking either brings this panel to the front.

## Select text

ImGui's editable text is one colour, so the coloured view cannot be selected
with the mouse. The **Select text** switch swaps in a read-only text box over
the *same filtered lines* — colouring for selection, with the filter still
working. It is remembered too (`logSelectOutput` / `logSelectDebug`).

## Where the code is

`src/logview.cpp` is the whole classifier and depends on nothing but the
standard library — no ImGui, no `Project` — so a real build log can be run
through it from a small host harness (the `treegen`/`placement` pattern):

```bash
g++ -std=c++20 -Isrc -o harness harness.cpp src/logview.cpp
```

with a `main()` that calls `logview::parse()` and prints
`logview::label(line.level)` per line. That is how the buckets in the table
above were checked, including the incremental path: the panels re-classify only
the lines that were appended (`logview::parse(log, from, state, out)`), because
re-parsing a megabyte on every line a build prints costs far more than a frame —
so a harness should assert that parsing a log one line at a time gives exactly
what parsing it in one go does.

The panels themselves are `App::logSetText` / `App::logRefresh` /
`App::drawLogHeader` / `App::drawLogPanel` (+ `App::drawLogTabBadge`) in
`src/app.cpp`, under the `// --- console drawer ---` comment, shared by both
windows so they cannot drift apart. `logview::Line::stamp` is the length of a
line's Runner stamp (0 or 9); `LogView::times` holds each line's arrival time. The view is drawn through an `ImGuiListClipper`, which is what
keeps a 15 000-line log scrolling at frame rate.

## Related

- [The devkit and its channels](devkit.md) — where the `[ps2]` lines come from.
- The **error dialog**: a `TYRAX` block in either log also pops a copyable
  dialog (*Pop up on errors*, in the Debug panel). The block is the same one the
  errors chip counts.
