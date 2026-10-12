# AGENT-READ-ME-FIRST.md — how to work in TyraX

Read this **first**, before the root instruction file (`CLAUDE.md` /
`AGENTS.md`) and before any skill. It is the routing table: it tells you *which*
skill to load for the work in front of you, and *which* known-failure note to
read when you are stuck — so you never load a 300 KB skill to change a constant.

Two rules govern this folder:

1. **Load skills by task.** Open the root instruction file plus the one or two
   skills named below. Nothing else.
2. **Every file here is capped at 200 lines** — SKILL.md and troubleshoot alike.
   That is an attention budget, not a style rule: a 200-line file is loaded on
   purpose; a 2000-line file is skipped, re-read, or read wrong. If a change
   does not fit, it belongs in a new file. Skills are rules only; everything
   shaped like "I hit X, the cause was Y, the fix is Z" lives in `troubleshoot/`,
   and everything shaped like a manual lives in `docs/`. Read
   `skills/tyra-skill-maintenance/SKILL.md` before writing or editing anything
   in this tree.

---

## The project in three lines

TyraX is a **3D editor and a PlayStation 2 engine in one C++20 repo**
(`src/` = editor, `vendor/tyra/engine` = an in-tree fork of
[h4570/tyra](https://github.com/h4570/tyra)). It generates project code, builds
games for PCSX2 and real hardware, and ships the engine as a library rather than
a dependency. A sibling game project (`E:\dev\gunner`) consumes it.

---

## Routing table — load exactly these

| The task in front of you | Load |
|---|---|
| You just started; don't know the repo | the root instruction file, then this table. Stop. |
| **You changed code and are about to call it done** | `skills/tyra-docs/SKILL.md` — every change updates docs in the same commit |
| Editing the **editor** (`src/`), codegen, the `.tyra` format, the model→serialization→codegen→UI chain | `skills/tyra-editor-dev/SKILL.md` |
| Editing the **engine fork** (`vendor/tyra`), the renderer/clipper/VU1, audio, loaders | `skills/tyra-engine-dev/SKILL.md` |
| **Building, running, verifying** anything (CLI, captures, PCSX2, perf fixtures) | `skills/tyra-testing/SKILL.md` |
| Opening or updating a **pull request**, or a PR has conflicts | `skills/tyra-pr/SKILL.md` |
| **Modelling a vehicle** for the PS2 path (Blender, atlas, bake) | `skills/tyra-vehicle-modeling/SKILL.md` |
| Writing, splitting or deleting **any file in this tree** | `skills/tyra-skill-maintenance/SKILL.md` |

Two at once is normal (a codegen change = `tyra-editor-dev` + `tyra-docs`).
Four is a smell: you are starting something too big.

---

## Which branch you work on

Two remotes: **`upstream`** is the original project
(`github.com/doctorspider42/tyraX`) - fetch latest FROM it; **`origin`** is the
developer's fork - push `dev-local` TO it. **Local work lives on `dev-local`;
`main` only mirrors `upstream/main` and is never committed to.** Syncing =
fast-forward `main`, then rebase `dev-local` onto it, so your work always sits on
the latest upstream HEAD. The exact commands - including
`git push --force-with-lease origin dev-local` - are in `skills/tyra-pr/SKILL.md`.
This is the default **unless the user names a branch, a remote or a fetch
strategy.**

---

## When you are stuck

**Read `troubleshoot/` before you debug.** Each file holds failures that have
already happened here, written symptom first. Find yours by the symptom, not by
the cause:

| Symptom shape | Read |
|---|---|
| An editor feature half-works — it appears in the model but not in the UI, or the generated code is wrong | `troubleshoot/tyra-editor-dev.md` |
| PS2 rendering is corrupt, geometry is smeared, a pass you cannot see does nothing | `troubleshoot/tyra-engine-dev.md` |
| A capture/verification run fails, is slow, or proves nothing | `troubleshoot/tyra-testing.md` |
| A generated-project skill or `AGENTS.md` did not install/refresh | `troubleshoot/tyra-docs.md` |

Per-topic files (`troubleshoot/<skill>-<topic>.md`) sit beside these; the skill
itself names the exact one for its own traps. **If none of them fit, write the
new one** — that is the point of the folder. Symptom in the observer's words,
then **Cause**, then **Fix**.

---

## The six things that will bite you if you skip this folder

1. **Docs ship with the code, in the same commit.** Touched code and zero doc
   edits is almost always a miss. (`tyra-docs`)
2. **Platform pairs are edited as a pair, in the same commit** —
   `deps.ps1`/`deps.sh`, `setup.*`, `build.*`, the `if(WIN32)`/`else()` halves of
   `CMakeLists.txt`, the `#ifdef _WIN32`/`#else` halves of `src/platform.cpp`.
   (`tyra-editor-dev` → "Platform parity")
3. **`.claude/skills/` is the source of truth; `.agents/skills/` is its twin.**
   Never hand-edit one side only — mirror any change to a skill, and any new
   `troubleshoot/` folder, in the same commit.
4. **SUBMITTED is not SEEN.** A green capture can prove a packet went out and
   nothing about what the rasteriser did with it — ask by **removal**. And a
   debug build over ps2link is not the player's frame. (`tyra-testing`)
5. **A skill that lags the code is worse than none** — it is confidently wrong.
   Update the skill in the same commit as the change that taught you the lesson,
   and prune on every edit.
6. **200 lines is the ceiling for every file in this tree**, SKILL.md and
   troubleshoot alike. Split at an entry boundary rather than growing a file.