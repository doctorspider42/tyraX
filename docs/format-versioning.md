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
reads as **v0**.

## What happens when you open a project

- **Same version** — opens normally.
- **Newer than the editor** (`formatVersion` > `kFormatVersion`) — refused,
  with a message naming both versions ("update TyraX"). Opening it anyway
  would silently drop the fields this editor does not know and destroy them
  on the next save. The check lives in `project::load`, so every path (GUI,
  `--build`, `--resave`) shares it.
- **Older, no registered migration steps** — opens silently. The tolerant
  reader defaults missing fields and keeps reading legacy keys forever;
  the file is re-stamped with the current version on the next save.
- **Older, with pending migration steps** — the editor prompts: old/new
  version, the list of steps that will run, and a warning that the operation
  is irreversible. On confirm it first **backs up** the format-bearing files
  (`<name>.tyra`, `objects/`, `terrain-*.heights`, `terrain-*.splat`,
  `flow-nodes/`, `screen-effects/` — `res/` assets are never touched) into
  `_backup/format-v<old>-<timestamp>/` inside the project, then migrates
  **in memory** and saves. If any step fails, the project does not open and
  **nothing on disk was modified**.

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
4. Keep the loader tolerant: `project::load` keeps reading legacy keys
   forever (see the `"stickDeadzone"` → per-stick example). A migration step
   transforms the **loaded model** where the old data's *meaning* changed;
   a step that needs data the reader no longer parses can re-read files
   itself via `Project::dir`.

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

| Version | Editor | Change |
|---|---|---|
| 0 | pre-1.0.0 | everything before versioning existed (legacy shapes are lifted by the tolerant reader: inline objects, single `"layout"` dump, project-level terrain, ...) |
| 1 | 1.0.0 | the `formatVersion` / `editorVersion` stamp itself (no migration step — nothing to transform) |
