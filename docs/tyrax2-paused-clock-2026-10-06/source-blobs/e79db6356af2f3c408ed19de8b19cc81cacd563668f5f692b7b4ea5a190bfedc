# Working on this project with others

This project is laid out to keep git merges painless when several people edit it
at once.

## What merges cleanly (just edit)

- **Scene objects** live one per file under `objects/<id>.json`. Each object has
  a stable id, so editing, moving or recoloring different objects never
  conflicts - you each touch a different file.
- **The manifest** (`<name>.tyra`) holds project-wide settings and, per scene, an
  ordered list of object ids. Editing settings or different scenes merges
  line-by-line. The one place two people can still collide is *both adding an
  object to the same scene at the same time* - a one-line conflict in the id
  list, trivial to resolve (keep both ids).

## What to lock first (cannot auto-merge)

Some files are a single indivisible blob a git auto-merge would corrupt.
`.gitattributes` marks them **lockable**; lock one before editing so no one else
edits it concurrently:

- `terrain-*.heights` - a scene's terrain heightmap (one grid; two sculpts can't
  merge).
- everything under `res/` - imported textures, models, audio, fonts.

Locking uses Git LFS's lock registry (no LFS storage / no special server):

    git lfs install                       # once per clone
    git lfs lock   terrain-main.heights   # claim before editing
    git lfs unlock terrain-main.heights   # release when done
    git lfs locks                         # see who holds what

Until you lock a lockable file it is read-only in your working copy - the
reminder to lock it. If your team does not use locking, ignore this: the files
still work as plain git files.

## Not tracked / regenerated

`obj/`, `bin/*.elf`, `.res-baked/`, `docker-compose.yml`, `*.history` and the
`*.gen.*` sources are build output or local state (see `.gitignore`) - never
resolve merge conflicts in generated files; fix the source and rebuild.

The one exception is `.res-baked/gi/` - the global-illumination cache. No build
can produce it (baking is an explicit, minutes-long step), so it is checked in
like an authored asset and travels with the project.
