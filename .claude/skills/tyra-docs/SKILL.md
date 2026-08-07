---
name: tyra-docs
description: >
  The standing documentation rule for this repo: EVERY change updates the docs
  in the SAME commit. Use this skill whenever you add or change a feature, edit
  behavior, add/move/rename/delete files, change code generation or the project
  file format, or otherwise touch the codebase — before you consider the change
  done. It lists exactly which docs must be checked (README, the other skills,
  example-project READMEs) and when each applies. If you changed code and
  did not touch any doc, treat that as a smell and re-check against this list.
---

# Keep the documentation in sync with every change

Documentation is not a follow-up task in this repo — it ships **in the same
commit** as the change that makes it true. One feature = one commit = code +
docs together. Before you call a change done, walk this checklist and update
whatever the change affected.

## What to update, and when

- **The commit message and the PR description** — ALWAYS, for every finished
  change. This is where `PROGRESS.md` used to go: it was retired at ~15 800
  lines because it had become a second copy of the git log that every branch
  conflicted in, and because completed-work entries had started leaking into its
  own backlog section. Nothing was lost — the file is in git history (see
  `docs/backlog.md` for the recipe).

  What the old entries were *good* at is still required, just in its proper
  place: say **what** changed, **why**, and **how it was verified** — which test
  layer you actually reached (see `tyra-testing`), and what a human still owes.
  Dead ends and measured numbers belong there too; that honesty was the valuable
  half of the log. The difference is that a commit message is attached to the
  diff it describes, so it cannot drift or collide with a parallel branch.

  A fact worth *re-reading later* — a trap, a measurement, a "why it is this way"
  — does not belong in a commit message alone. Put it in the relevant `docs/`
  page or skill, where the next person will actually look for it.

- **`docs/backlog.md`** — when the change finishes, adds or reshapes queued work.
  Tick off what you completed and add what your change made newly necessary.
  This is the forward-looking half of the retired log, and the only part of it
  that is still a living file.

- **`README.md`** — when the change adds/removes a user-visible feature, a new
  panel or menu, a new example project, or changes the repo structure. Keep the
  feature list, the "Example projects" section and the "Structure" list current.

- **`.claude/skills/*/SKILL.md`** — when the change alters something a skill
  describes:
  - `tyra-editor-dev` — the source map, the model → serialization → codegen → UI
    chain, the file-ownership rules, or the "a feature touches the whole chain"
    guidance.
  - `tyra-engine-dev` — anything under `vendor/tyra` (renderer/clipper/VU1/audio/
    loaders) or its pitfalls list.
  - `tyra-testing` — how to build/run/verify (new CLI flags, new verification
    steps, new asset-bake behavior).
  - `tyra-pr` — the PR workflow or a new conflict hot spot.
  - this skill (`tyra-docs`) — if the set of docs or the rule itself changes.

  **`.agents/skills/` is the Codex twin of this directory and moves with it, in
  the same commit** — the same rule as the platform pairs above. The Claude copy
  is the source of truth; the twin is a plain regeneration of it with two
  substitutions (the skills path itself, and `AGENTS.md` in place of the root
  instruction file), so **never hand-edit only one side**. The twin was added as a
  one-off snapshot with no sync since, and had drifted a full retired-`PROGRESS.md`
  behind — plus it pointed at a `.Codex/` scripts path that exists in no
  checkout — before anyone noticed. That is what this bullet exists to prevent.

- **Example-project READMEs** (`examples/*/README.md`) and the projects
  themselves — when codegen, the `.tyra` format, or the terrain/asset pipeline
  changes, **regenerate the affected example projects** (their committed
  generated files drift silently otherwise) and update their READMEs. See
  `tyra-editor-dev` for the regenerate flow and `tyra-testing` for verifying it.

- **A new example project** — for a **larger, user-facing feature** (a new
  flow-node family, object type, editor subsystem, whole workflow), proactively
  **propose adding a small dedicated demo under `examples/`** — a focused
  per-feature project (like `examples/layer-streaming` or `examples/custom-nodes`)
  with its own README saying what to do and how it's wired. Examples are the
  most discoverable docs there are. Scaffold via `--new`, overlay the scene/
  assets, `--build` so the committed generated files are fresh, and verify it
  boots (see `tyra-testing`). Don't force one for a small tweak; do offer it
  whenever a feature is big enough that a user would want to see it in action.

- **`ai-support/`** — the assistant guides installed into generated projects
  ("Add AI support"; embedded into the exe at build). They document the
  generated-project layout, the ownership markers, the flow-graph model and
  the editor CLI — when any of those change, update the affected files under
  `ai-support/` too (see `docs/ai-support.md`).

- **The platform twin** — not a doc, but it belongs on this checklist because
  it is missed the same way: several files in this repo exist once per platform
  (`deps.ps1`/`deps.sh`, `setup.ps1`/`setup.sh`, `build.ps1`/`build.sh`, the
  `if(WIN32)`/`else()` halves of `CMakeLists.txt`, the `#ifdef _WIN32`/`#else`
  halves of `src/platform.cpp`). If your change touched one member of a pair,
  it must touch the other **in the same commit**. The inventory and the two
  traps are in `tyra-editor-dev` ("Platform parity").

- **`src/version.hpp`** — not a doc, but part of the same per-change ritual:
  bump the editor semver for every user-facing change (feature → MINOR, fix →
  PATCH, breaking → MAJOR), and bump `kFormatVersion` whenever the change
  alters what `project::save()` writes — plus a migration step in
  `migrations.cpp` when existing files need transforming. See
  `docs/format-versioning.md`.

- **`CLAUDE.md`** — only when a project-wide, always-on rule changes (it is the
  always-loaded instruction file; keep it tiny).

- **`docs/`** — when a feature has (or needs) a design doc, update or add it.
  Note that `docs/*.md` is no longer only for humans: every page is **compiled
  into the editor** (`cmake/embed_docs.cmake` -> `docs_gen.hpp`) and read at
  runtime by the in-editor AI Assistant, which answers from it (docs/ai-chat.md).
  Three consequences. A page written well is a feature two ways over, and a page
  left stale misinforms an assistant as well as a reader. A new page joins the
  assistant's index by existing - the index is derived from each page's own H1 and
  first sentence, so **open with a plain prose sentence that says what the thing
  is**, not with a table, a quote or a bullet list (those are skipped, and the
  page then indexes as whatever prose comes next). And the content must not
  contain the raw-string delimiter `)TYRAXDOC"` - cmake fails the build if it
  does, which is the only way this can bite you.

## Rule of thumb

If you touched code and this list produced **zero** doc edits, stop and re-read
it — that is almost always a miss (a moved file wants a README/skill fix, a
codegen tweak wants an example regenerate, a trap you just hit wants a line in
the `docs/` page where the next person will hit it too).
