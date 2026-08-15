# Project format versioning & migrations

TyraX tracks two independent versions (both defined in `src/version.hpp`):

| | What | Where | Who reads it |
|---|---|---|---|
| **Editor version** | semver `MAJOR.MINOR.PATCH` | title bar, `"editorVersion"` in the `.tyra` manifest | humans (diagnostics: *which editor wrote this file*) |
| **Format version** | monotonic int `version::kFormatVersion` | `"formatVersion"` in the `.tyra` manifest | the editor (open gate + migrations) |

They are deliberately separate: the editor version moves with every release
(feature → MINOR, fix → PATCH, breaking change → MAJOR), while the format
version moves only when the **on-disk project format** changes. Tying prompts
to the editor version would nag users after every patch; tying them to the
format version means the migration prompt appears exactly when something
irreversible is about to happen.

A project saved before versioning existed has no `formatVersion` field and
reads as **v0**, which this editor no longer opens — see the floor below.

The editor reads **`version::kMinFormatVersion` … `kFormatVersion`**, currently
v1 … v24.

## What happens when you open a project

- **Same version** — opens normally.
- **Newer than the editor** (`formatVersion` > `kFormatVersion`) — refused,
  with a message naming both versions ("update TyraX"). Opening it anyway
  would silently drop the fields this editor does not know and destroy them
  on the next save. The check lives in `project::load`, so every path (GUI,
  `--build`, `--resave`) shares it.
- **Older than the floor** (`formatVersion` < `kMinFormatVersion`) — refused
  too, naming the range this editor reads. The reader carries no translations
  below the floor, so it would recognise nothing in the file and open an
  *empty* project without saying why; a refusal is the honest answer.
- **Older, no registered migration steps** — opens silently. The reader
  defaults every missing field, so an additive bump costs an older project
  nothing; the file is re-stamped with the current version on the next save.
- **Older, with pending migration steps** — the editor prompts: old/new
  version, the list of steps that will run, and a warning that the operation
  is irreversible. On confirm it first **backs up** the format-bearing files
  (`<name>.tyra`, `objects/`, `terrain-*.heights`, `terrain-*.splat`,
  `flow-nodes/`, `screen-effects/` — `res/` assets are never touched) into
  `_backup/format-v<old>-<timestamp>/` inside the project, then migrates
  **in memory** and saves. If any step fails, the project does not open and
  **nothing on disk was modified**.

The prompt belongs to **local** opens only (`App::openProjectAt` — the CLI
argument, the Open dialog and the recent-projects list all funnel through it).

**Joining a collaboration session is the one open path that never migrates.** A
client materializes the host's project and opens it through
`App::openRemoteProject`; if the host's format needs registered steps, the
**join is refused** and the message says the host has to migrate first. Two
reasons: the project belongs to the host and migrating is irreversible, and a
client can only migrate its own replica — after which host and client would
disagree about the format while `diffModel` keeps syncing edits over fields one
of them does not have. (A host on a *newer* format is already refused by
`project::load`, like any other path.)

`_backup/` is a local safety copy, not source: the generated project
`.gitignore` excludes it, and a collaboration session never sends it to peers.

Headless (`--build`, `--resave`, `--refresh-gen`, `--apply-graph`, `--ai-graph`)
refuses projects with pending migration steps instead of silently rewriting them
— migrating is an explicit act. (`--refresh-gen` and `--build` are on the list
because baking a stale Scatter volume saves the project.)

```bash
tyrax-editor --migrate <projectDir>   # backup + migrate + resave
```

`--migrate` prints the backup location and each applied step; on an
up-to-date project it degrades to a plain resave. It writes the same file set
as `--resave` (manifest + heights + splat) — a migration that persisted less
than a resave would drop whatever it skipped.

## Rules for contributors

1. **Every feature bumps the editor version** in `src/version.hpp`:
   new feature → MINOR, fix → PATCH, breaking change → MAJOR.
2. **Every change to what `project::save()` writes bumps
   `version::kFormatVersion`** — new fields included. The bump is what lets
   an *older* editor refuse the file instead of eating it.
3. **Register a migration step** (`migrations.cpp`) for the same bump **only
   when old files need active transformation** — a rename, a semantic/unit
   change, moved or restructured data. Purely additive fields with safe
   defaults need no step (the tolerant reader handles them) and old projects
   keep opening silently.
   **A step can only transform data the file HAS.** The tempting case is a
   bump that fixes a *dropped* field — v11 is the worked example: `save()`
   never wrote a fog emitter's `opacity`, so authored values were destroyed at
   save time and the file holds nothing to migrate. The reader's default (0.6)
   is not a guess at the author's intent, it is precisely what that file has
   always meant to codegen and to the viewport, so a step could only invent a
   number and would make every affected project *change* on open. Additive,
   no step. What the bump buys is the other half: an older editor now refuses
   the file instead of dropping the new key on its own next save.
4. Keep the loader tolerant about **absent** keys — a missing field defaults
   and an additive bump then costs an older project nothing. Tolerance is not
   the same as carrying a *translation* for a key that was renamed or moved:
   those are what `kMinFormatVersion` exists to retire, and the retiring is a
   deliberate act (raise the floor, say so above `kFormatVersion`), never a
   silent drop. A migration step transforms the **loaded model** where the old
   data's *meaning* changed; a step that needs data the reader no longer
   parses can re-read files itself via `Project::dir`.
5. **A branch renumbers its bump on the way in — it never argues for the
   number it authored.** Two features may not share a format number: the
   number is the whole basis on which an older editor refuses a file, so if
   `6` means "collision-box overlay" to main and "the upscaler's shot plan"
   to a branch, an editor that knows only the first will happily open the
   second and drop the fields it cannot see on its next save. Whoever merges
   moves their entries to the top of the list, keeping one number per landing
   (a branch that bumped three times keeps three), and says in the comment
   what the old numbers were — the version-history comment in
   `src/version.hpp` is the record, and the reverb, sound-priority, World
   Facts and BLSS entries all carry that note. **Grep the docs for the old
   numbers in the same commit**: the meaning is quoted in prose
   (`docs/neural-upscaler.md`, `docs/blss-reconstruction.md`,
   `docs/backlog.md`, the skills) as often as it is in the header, and a
   renumber that stops at `version.hpp` leaves every one of them lying.
   No migration step is needed for a renumber itself when both sides were
   additive: nothing on disk changes shape, only the label the file claims.

### Adding a migration step

```cpp
// migrations.cpp, inside all():
static const std::vector<Migration> steps = {
    {1, "walk speed: units/frame -> units/second",
     [](Project& p, std::string& err) {
         for (SceneData& sc : p.scenes)
             for (SceneObject& o : sc.objects)
                 if (o.type == PrimitiveType::Player) o.playerWalkSpeed *= 60.0f;
         return true;
     }},
};
```

A step upgrades `from` → `from + 1`; the chain runs in order, so a project
several versions behind migrates through every step in one go. `summary` is
shown verbatim in the migration prompt and the `--migrate` output — write it
for the user.

**List steps by ascending `from`, once each, and bump `kFormatVersion` in the
same commit.** `migrations::run` applies steps in registration order, so an
out-of-order entry would transform data a later step still expects untouched.
`migrations::validate()` checks that, plus every `from` inside
`[0, kFormatVersion)`, an `apply`, and a non-empty `summary`.

It runs in **two** places, and the first one is the one that matters:

- `migrations::all()` shouts on stderr at the registry's first use — which is
  every project open and every headless command. This catches the mistake you
  are actually going to make: **a step registered without bumping
  `kFormatVersion`**. That makes `stepsFor()` return nothing, so the gate never
  fires and `run` is never reached — the step would silently never run, and the
  symptom is the maddening "my migration does nothing".
- `run` checks too, where a bad registry aborts the migration with disk
  untouched instead of transforming data in the wrong order.

It is deliberately *not* a "no gaps" check — a purely additive bump registers no
step at all, so missing versions in the chain are the normal case.

**A step must not change `Project::name`.** `save()` writes `<name>.tyra` and
does not delete a manifest under the old name, so a renaming step leaves two
`.tyra` files in the project directory and `load()` takes whichever the
directory iterator yields first — possibly the pre-migration one. Rename
*fields*, not the project.

With no step registered, the prompt and the backup are unreachable by
construction. To exercise them, register a throwaway step and bump
`kFormatVersion` locally (that is how they were tested); `--migrate` is the
path that needs no GUI dialog.

## Format history

**The per-version record is the comment block above `kFormatVersion` in
`src/version.hpp`** — one entry per landing, saying what the version added and
why it did or did not need a step. Read it there; rule 5 above is why it is the
record, and a second copy here would be the thing that goes stale. The table
below is only the two versions that predate those entries.

| Version | Editor | Change |
|---|---|---|
| 0 | pre-1.0.0 | everything before versioning existed — objects inline in the manifest, a single `"layout"` dump, a project-level terrain block and flow graph, raw TTF paths where a font name now goes. **No longer read**: the translations were retired with `kMinFormatVersion = 1`, since TyraX has never shipped publicly and no such file exists outside this repo's history. |
| 1 | 1.0.0 | the `formatVersion` / `editorVersion` stamp itself (no migration step — nothing to transform); the oldest format this editor opens |
