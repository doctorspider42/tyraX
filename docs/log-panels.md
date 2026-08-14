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

Click a chip to hide or show that level; the choice is remembered per panel in
`editor.ini` (`logMaskOutput`, `logMaskDebug`) because which noise you want
hidden is a property of how you work, not of the project. Nothing is hidden by
default. **Copy** copies the lines currently shown — the filter applies.

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

## Select text

ImGui's editable text is one colour, so the coloured view cannot be selected
with the mouse. The **Select text** checkbox swaps in a read-only text box over
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
`App::drawLogPanel` in `src/app.cpp`, shared by both windows so they cannot
drift apart. The view is drawn through an `ImGuiListClipper`, which is what
keeps a 15 000-line log scrolling at frame rate.

## Related

- [The devkit and its channels](devkit.md) — where the `[ps2]` lines come from.
- The **error dialog**: a `TYRAX` block in either log also pops a copyable
  dialog (*Pop up on errors*, in the Debug panel). The block is the same one the
  errors chip counts.
