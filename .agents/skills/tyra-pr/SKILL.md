---
name: tyra-pr
description: >
  Checklist for opening and updating pull requests in this repo. Use this
  skill EVERY time you are about to create a PR (gh pr create), push more
  commits to an existing PR, or the user says a PR has conflicts. Core rule:
  a PR is not "created" until you have VERIFIED it is conflict-free against
  origin/main - main moves fast here (parallel Claude branches land daily)
  and stale-base PRs are the norm, not the exception.
---

# Pull requests in this repo

## The golden rule: never ship a PR without checking for conflicts

`main` moves fast (parallel feature branches merge daily), so assume your
base is stale. **Creating the PR is not the last step - verifying it is
mergeable is.**

### Before `gh pr create`

```powershell
git fetch origin main
git log --oneline HEAD..origin/main   # anything incoming?
git merge origin/main                 # merge NOW, locally
```

If the merge conflicts: resolve, rebuild the editor (`./build.ps1` or `./build.sh`), run at
least one Docker game build (`build\tyrax-editor.exe --build <projectDir>` -
codegen conflicts compile only inside the container), commit the merge, THEN
create the PR.

### After `gh pr create` / after every push

Verify what GitHub actually computed - a local merge test can drift from
GitHub's view if someone merged to main in between:

```powershell
gh pr view <num> --json mergeable,mergeStateStatus
```

- `"mergeable": "MERGEABLE"` → done.
- `"mergeable": "CONFLICTING"` → fetch + merge origin/main, resolve, verify,
  push again, re-check.
- `"mergeable": "UNKNOWN"` → GitHub is still computing; wait a few seconds
  and query again (don't conclude anything from UNKNOWN).

Report the mergeable state to the user as part of "PR is up".

## Conflict hot spots (this codebase specifically)

- **`src/templates.cpp`** - almost every feature touches codegen. Watch for:
  the emitted `SceneObjectData` struct + its object-row emission (parallel
  branches append fields; after merging, the struct fields and the row
  columns MUST line up 1:1 - count them), the game-cpp prolog includes, and
  the header templates (duplicated for orbit + fpp - fix both).
- **`src/app.cpp`** - UI moves around (Properties window, panels); prefer
  re-applying your widget in main's new location over keeping the old block.
- **`examples/script-demo/`** - generated files conflict textually but are
  NOT worth hand-merging: resolve any way, then regenerate the sample with a
  Docker build and commit the regenerated files.
- **`PROGRESS.md`** - gone. It was retired at ~15 800 lines precisely because it
  was this list's most reliable entry: every branch appended to the same spot,
  so every PR conflicted there. If you are merging a branch old enough to still
  edit it, resolve by deleting the file (`git rm PROGRESS.md`) and move anything
  the entry said into your commit message and PR body.

## After resolving: verify before pushing

A merge that compiles the editor can still emit game code that does not
compile on the PS2 toolchain. Minimum: `./build.ps1` / `./build.sh` clean + one
`--build <projectDir>` returning exit 0 (see tyra-testing). If the merge
touched player/scene runtime templates, boot PCSX2 once.

## PR content conventions

- PR title + body in **English** (AGENTS.md rule), body ends with the
  Claude Code footer.
- Body summarizes per-feature verification (what was proven and how) and
  names the remaining hands-on checks. Since `PROGRESS.md` was retired, the
  commit message and this body ARE the record - hold them to the honesty bar
  its entries had: which test layer you actually reached, and what a human
  still owes.
- One feature = one commit on the branch; merge commits from origin/main
  are fine and expected.
