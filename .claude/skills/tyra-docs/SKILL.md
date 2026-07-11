---
name: tyra-docs
description: >
  The standing documentation rule for this repo: EVERY change updates the docs
  in the SAME commit. Use this skill whenever you add or change a feature, edit
  behavior, add/move/rename/delete files, change code generation or the project
  file format, or otherwise touch the codebase — before you consider the change
  done. It lists exactly which docs must be checked (README, PROGRESS, the other
  skills, example-project READMEs) and when each applies. If you changed code and
  did not touch any doc, treat that as a smell and re-check against this list.
---

# Keep the documentation in sync with every change

Documentation is not a follow-up task in this repo — it ships **in the same
commit** as the change that makes it true. One feature = one commit = code +
docs together. Before you call a change done, walk this checklist and update
whatever the change affected.

## What to update, and when

- **`PROGRESS.md`** — ALWAYS, for every finished change. Add a numbered entry
  (continue the sequence) in the "Also done…" section describing **what** was
  done and **how it was verified** (which test layer you reached — see
  `tyra-testing`). Match the tone of the surrounding entries; they double as the
  project's institutional memory, dead ends included. Don't rewrite old entries.

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

- **Example-project READMEs** (`examples/*/README.md`) and the projects
  themselves — when codegen, the `.tyra` format, or the terrain/asset pipeline
  changes, **regenerate the affected example projects** (their committed
  generated files drift silently otherwise) and update their READMEs. See
  `tyra-editor-dev` for the regenerate flow and `tyra-testing` for verifying it.

- **`CLAUDE.md`** — only when a project-wide, always-on rule changes (it is the
  always-loaded instruction file; keep it tiny).

- **`docs/`** — when a feature has (or needs) a design doc, update or add it.

## Rule of thumb

If you touched code and this list produced **zero** doc edits, stop and re-read
it — that is almost always a miss (a new field wants a PROGRESS entry, a moved
file wants a README/skill fix, a codegen tweak wants an example regenerate).
