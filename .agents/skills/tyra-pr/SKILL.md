---
name: tyra-pr
description: >
  Checklist for opening and updating pull requests in this repo. Use this
  skill EVERY time you are about to create a PR (gh pr create), push more
  commits to an existing PR, merge one, or the user says a PR has conflicts.
  Core rule: a PR is not "created" until you have VERIFIED it is conflict-free
  against origin/main - main moves fast here (parallel Claude branches land
  daily) and stale-base PRs are the norm, not the exception.
---

# Pull requests in this repo

`main` is protected (a push needs a PR - a direct push fails with GH013) and
moves daily. **Creating the PR is not the last step; verifying it is mergeable is.**

## Before `gh pr create`

```bash
git fetch origin main
git log --oneline HEAD..origin/main   # anything incoming?
git merge origin/main                 # merge NOW, locally
```

On a conflict: resolve, rebuild the editor (`./build.sh` / `./build.ps1`), run
one game build (`tyrax-editor --build <projectDir>`, native PS2DEV by default -
codegen conflicts only show up in the PS2 compile), commit the merge, then
create the PR.

## After `gh pr create` / every push

```bash
gh pr view <num> --json mergeable,mergeStateStatus
```

- `MERGEABLE` -> done. `CONFLICTING` -> fetch + merge origin/main, resolve,
  verify, push, re-check. `UNKNOWN` -> GitHub is still computing; ask again,
  conclude nothing.
- Report the mergeable state to the user as part of "PR is up".

## Merging = releasing

Every push to main runs `.github/workflows/release.yml`: it tags and publishes
`v<version.hpp>` if untagged, else the next PATCH (docs/updates.md). So a merged
PR ships within the hour, and even a docs-only merge cuts a patch release. Put
the user-facing text in `CHANGELOG.md` under `## <x.y.z>` (tyra-docs) and the
release job publishes that section as the notes - the editor's update dialog
shows it. A merge that bumps nothing gets the next PATCH and generated notes,
unless the changelog has a section for that PATCH.

## Conflict hot spots

- **`src/templates.cpp`** - almost every feature touches codegen. The emitted
  `SceneObjectData` / `MenuData` structs and their row emission must line up
  field for field after a merge (count them); header templates are duplicated
  for orbit + fpp (fix both). **Nothing on the host compiles those rows** - a
  mismatch survives editor builds, harnesses and `--refresh-gen`, and first
  shows in the PS2 compile (`invalid conversion from 'const char*' to 'int'`).
  Two edits anchored on one struct line cause it without any merge. Cheapest
  check: compare the struct and one emitted row from the generated header
  (~15 lines of Python), then one `--build`.
- **`src/app.cpp` and the `*_ui.cpp` TUs** (props_ui, modelimport_ui, ...) - UI
  moves; re-apply your widget in main's new location rather than keeping the
  old block.
- **`examples/*/` generated files** - not worth hand-merging: resolve any way,
  regenerate with a build, commit the regenerated files.
- **`PROGRESS.md`** - retired (git history only). A branch old enough to edit
  it: `git rm PROGRESS.md`, move the entry into the commit message / PR body.

## After resolving

Minimum: editor build clean + one `--build <projectDir>` exit 0 (tyra-testing).
If player/scene runtime templates were touched, boot PCSX2 once.

## PR content

- Title + body in **English** (AGENTS.md).
- Body = per-feature verification (which test layer was actually reached, how)
  + what a human still owes. With PROGRESS.md gone, commit messages and PR
  bodies ARE the record.
- One feature = one commit; merge commits from origin/main are fine.

## A visible change carries a picture

Anything a reviewer would otherwise build to see. You can always make it
unattended: `--ui-script "... shot x.png"` for the editor, `--capture-frame` /
`wayland-control.py` / `screenshot-window.ps1` for the game (tyra-testing).

- Put it in **`docs/img/`** - usually required anyway (tyra-docs), so the PR
  embeds the file the docs ship.
- **Pin the URL to the commit**, never the branch (a branch URL 404s once the
  branch is deleted on merge), and check it before publishing:
  `https://raw.githubusercontent.com/<owner>/<repo>/<full-sha>/docs/img/<name>.png`
  + `curl -sI <url> | head -1`.
- Shrink it: no pngquant/optipng here, but a PIL 256-colour quantize
  (`quantize(256, method=MEDIANCUT, dither=NONE)`) cuts a full-window grab to
  ~1/4 with text still crisp; check text and gradients at 2x.

## Mechanics

- `gh pr edit --body` can fail with a GraphQL "Projects (classic) is being
  deprecated" error while changing nothing - use
  `gh api -X PATCH repos/<owner>/<repo>/pulls/<n> -F body=@body.md` and read the
  body back (`gh pr view <n> --json body`).
- `github.com/user-attachments/assets/...` URLs need a browser session; a token
  cannot create them. Never put a placeholder in a body. (`gh release upload`
  is the only token-reachable alternative for a picture kept out of the tree.)
