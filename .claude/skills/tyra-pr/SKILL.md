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
  the emitted `SceneObjectData` and `MenuData` structs + their row emission
  (parallel branches append fields; after merging, the struct fields and the row
  columns MUST line up 1:1 - count them), the game-cpp prolog includes, and
  the header templates (duplicated for orbit + fpp - fix both).

  **Nothing on the host compiles those rows**, so a mismatch survives every
  editor build, the harnesses and `--refresh-gen`; the PS2 toolchain is the first
  thing that sees it (`invalid conversion from 'const char*' to 'int'`). It does
  not take a merge to cause one either - two edits anchored on the same struct
  line put a `const char*` between two ints while the emitter kept the old
  order. Cheapest check, no Docker: read the struct and one emitted row out of
  the generated header and compare them field by field (~15 lines of Python),
  then let one `--build` confirm.
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

- PR title + body in **English** (CLAUDE.md rule), body ends with the
  Claude Code footer.
- Body summarizes per-feature verification (what was proven and how) and
  names the remaining hands-on checks. Since `PROGRESS.md` was retired, the
  commit message and this body ARE the record - hold them to the honesty bar
  its entries had: which test layer you actually reached, and what a human
  still owes.
- One feature = one commit on the branch; merge commits from origin/main
  are fine and expected.

## If the change is visible on screen, the PR carries a picture

A panel, a dialog, a viewport, a generated frame - anything a reviewer would
otherwise have to build and run to see. **You can produce the picture yourself,
unattended, on either OS**: `--ui-script "... shot x.png"` drives the editor's
own widgets with no window focus, and `screenshot-window.ps1` / `wayland-control.py`
grab PCSX2 (all in tyra-testing). "I could not take a screenshot" is almost
never true here.

**Where the file lives: `docs/img/`.** That is the repo's own convention (65+
PNGs, referenced from `docs/*.md` as `img/<name>.png`), and for a visible change
it is usually **required anyway** - see the `docs/img` bullet in tyra-docs.
The PR then embeds the same file the documentation now ships, which is one
artifact doing two jobs instead of a throwaway upload.

**Pin the URL to the COMMIT, never to the branch:**

```
https://raw.githubusercontent.com/<owner>/<repo>/<full-sha>/docs/img/<name>.png
```

A branch URL 404s the moment the branch is deleted on merge, which is exactly
when people start reading the PR as history. Verify before publishing the body -
one command, and it catches a wrong repo name or an unpushed commit:

```bash
curl -sI "https://raw.githubusercontent.com/<owner>/<repo>/<sha>/docs/img/<name>.png" | head -1
```

Two mechanics that cost time the first time:

- **`gh pr edit --body` can fail** with a GraphQL *"Projects (classic) is being
  deprecated"* error while changing nothing. Use the REST route instead:
  `gh api -X PATCH repos/<owner>/<repo>/pulls/<n> -F body=@body.md`. Read the
  body back (`gh pr view <n> --json body`) rather than trusting the exit code.
- **`github.com/user-attachments/assets/...` URLs are out of reach.** Those come
  from the browser's drag-and-drop upload, which needs a github.com web session;
  a token cannot reach that endpoint and `gh` has no command for it. Do not put
  a placeholder one in a body hoping to fill it in later - it renders as a broken
  image the moment you publish. (A release asset via `gh release upload` is the
  only token-reachable alternative, for a picture you deliberately do not want in
  the tree.)

Keep the file small: there is no `pngquant` or `optipng` on these boxes, but a
256-colour quantize in PIL cuts a full-window editor grab to roughly a quarter
of its bytes with the UI text still crisp - check the text AND any gradient at
2x before committing it.
