---
name: tyra-docs
description: >
  The standing documentation rule for this repo: EVERY change updates the docs
  in the SAME commit. Use this skill whenever you add or change a feature, edit
  behavior, add/move/rename/delete files, change code generation or the project
  file format, or otherwise touch the codebase — before you consider the change
  done. It lists exactly which docs must be checked (README, CHANGELOG, the
  skills, example-project READMEs) and when each applies. If you changed code
  and did not touch any doc, treat that as a smell and re-check against this list.
---

# Docs ship in the same commit as the change

One feature = one commit = code + docs. Walk this list before calling a change done.

| What | When | Rule |
|---|---|---|
| **Commit message + PR body** | always | What changed, why, how it was verified (which test layer you actually reached - tyra-testing), dead ends, measured numbers, what a human still owes. This replaced `PROGRESS.md` (retired at ~15 800 lines; git history, recipe in `docs/backlog.md`). A fact worth re-reading later also goes into a `docs/` page or skill. |
| **`CHANGELOG.md`** | every user-facing change (anything bumping MINOR/PATCH) | Bullet under the shipping version, newest on top, written for the USER. Same text goes into the GitHub release notes (`gh release edit v<x.y.z> --notes-file`), which the workflow leaves as a bare "Full Changelog" link (tyra-pr). Starts at 1.170.0. |
| **`docs/backlog.md`** | work finished, added or reshaped | Tick off done items, add newly necessary ones. The only living part of the old log. |
| **`README.md`** | user-visible feature added/removed, new example, repo structure | A map, not a manual: one line per feature linking its `docs/` page, one table row per example. Needing more than a line = the `docs/` page is missing. |
| **`docs/README.md`** | new `docs/` page | A page is not done until indexed there with a one-paragraph summary. |
| **`docs/*.md`** | a feature has / needs a design or user doc | Compiled into the editor (`cmake/embed_docs.cmake` -> `docs_gen.hpp`) and read by the in-editor AI Assistant (docs/ai-chat.md): a stale page misleads it too. Its index uses each page's H1 + first sentence - **open with a plain prose sentence saying what the thing is** (tables/quotes/bullets are skipped). Never contain `)TYRAXDOC"` (cmake fails). Developer-only material belongs in a skill, not here. |
| **`docs/img/*.png`** | a documented screen changed | Screenshots go stale silently. `grep -n '^!\[' docs/*.md` for the pages showing what you touched. **Retake on the same fixture** the old shot used (copy the example out first - `--refresh-gen` mutates it), driven by `--ui-script ... shot` (tyra-testing). Catches clipped/truncated layout, and the same file goes into the PR (tyra-pr). |
| **Skills** (`.agents/skills/*`) | the change alters what a skill describes | `tyra-editor-dev`: source map, model -> serialization -> codegen -> UI chain, ownership rules. `tyra-engine-dev`: anything in `vendor/tyra`, pitfalls. `tyra-testing`: build/run/verify, CLI flags, acceptance recipes. `tyra-pr`: PR flow, conflict hot spots. `tyra-vehicle-modeling`: vehicle import rules, `make-ravager.py`. `tyra-docs`: this list. Each big skill is a short `SKILL.md` + `references/*.md` - put detail in the reference its index points at, and add a row to that index for a new reference. |
| **`.agents/skills/`** (Codex twin) | **every** skill edit, same commit | Generated from `.agents/skills` - never hand-edit one side (it once drifted a whole PROGRESS.md behind). Regenerate: `rm -rf .agents/skills && cp -r .agents/skills .agents/skills && grep -rlZ '' .agents/skills \| xargs -0 sed -i 's#\.agents/skills#.agents/skills#g; s#CLAUDE\.md#AGENTS.md#g'` (text files only; scripts/ binaries none today). |
| **Example projects** (`examples/*`) | codegen, `.tyra` format, terrain/asset pipeline changed | Regenerate the affected examples (their generated files drift silently) and update their READMEs. |
| **New example project** | larger user-facing feature (node family, object type, subsystem, workflow) | Propose a focused demo under `examples/` with its own README: `--new`, overlay scene/assets, `--build`, verify it boots. Not for small tweaks. |
| **`ai-support/`** | generated-project layout, ownership markers, flow-graph model, editor CLI changed | Guides installed into generated projects and embedded in the exe (docs/ai-support.md). CLI tables live in `ai-support/claude/skills/tyra-project/SKILL.md` and `ai-support/copilot/copilot-instructions.md` - update both. |
| **Platform twin** | touched one of `deps.ps1`/`deps.sh`, `setup.*`, `build.*`, `if(WIN32)`/`else()` in CMakeLists, `#ifdef _WIN32`/`#else` in `src/platform.cpp` | Edit the pair in the same commit (tyra-editor-dev, "Platform parity"). |
| **`src/version.hpp`** | every user-facing change | Semver: feature MINOR, fix PATCH, breaking MAJOR, with a short comment. `kFormatVersion++` whenever `project::save()` output changes, plus a `migrations.cpp` step when old files need transforming (docs/format-versioning.md). |
| **`AGENTS.md`** | a project-wide always-on rule changes | Keep it tiny. |

If you touched code and this list produced zero doc edits, re-read it: a moved
file wants a README/skill fix, a codegen tweak wants an example regenerate, a
trap you just hit wants a line where the next person will hit it.
