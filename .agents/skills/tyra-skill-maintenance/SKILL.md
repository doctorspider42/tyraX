---
name: tyra-skill-maintenance
description: >
  The lean contract for the files in this repository's `.agents/skills/` tree
  and `troubleshoot/`. Use this skill whenever you are writing, editing, splitting
  or deleting a SKILL.md, adding an entry to troubleshoot/, or deciding where a
  piece of knowledge belongs. It states what a skill file may contain (rules),
  what must be extracted to `troubleshoot/` (symptom to fix), and the hard size
  ceiling. Read it before touching any file in `.agents/skills/`.
---

# Writing and maintaining skills in this repo

A skill is a **rule an agent must not break while doing a category of work**. It
is not a tutorial, a manual, a changelog, or a bug archive. The engine's skills
grew into reference documents; this contract exists to stop that.

## The lean contract

**Ceiling: 200 lines per file — SKILL.md and every troubleshoot file. Aim well
under it (60–120 is normal).** Over 200 the content is in the wrong file. The
goal is agent attention, not line count: a 200-line file is loaded on purpose, a
2000-line file is skipped, re-read, or read wrong.

A lean SKILL.md contains:

1. **Frontmatter** (`name`, `description`) — keep it; it is what makes an
   assistant reach for the skill, so it must state *when* to load it, not what
   the skill is about.
2. **A one-line statement of what the skill governs.**
3. **The rules** — numbered, with the *why* where the why is not obvious. A rule
   whose reason is self-evident gets one line; a counter-intuitive rule gets
   two plus its dead end.
4. **A compact "where things are" map** — file paths and ownership, not prose.
5. **A pointer to the relevant `troubleshoot/*.md`.**

What it must not contain: tutorials, architecture essays, per-feature how-tos,
version-tagged release notes, measured numbers with no rule attached, or any
narrative about how the work went.

**The test for whether a line belongs in a skill:** can an agent violate it and
get a wrong result that no test catches? If yes, it is a rule — keep it. If a
test would catch it, delete the line; the test is the cheaper enforcement.

## Splitting: where does content go?

Ask in order. First yes wins.

| Does it… | Goes to |
|---|---|
| State a **rule an agent must not break**? | the SKILL.md |
| Read like **"I hit X, the cause was Y, the fix is Z"**? | `troubleshoot/<skill>.md` |
| Describe **per-feature structure** (how this one subsystem hangs together)? | `docs/<page>.md`, linked from the skill |
| Describe **system structure generally** (what calls what, who owns what)? | the SKILL.md, as a compact map |
| Measure something / report a release? | the commit message and PR body (`tyra-docs`) |
| Describe a **verification procedure** for one feature? | `tyra-testing`, in one short entry — not the feature's skill |
| Change every session (status, queue)? | `docs/backlog.md` |

### The three split triggers

Split when any is true:

1. **Size** — over 200 lines. **Always split a troubleshoot file over 200** at an
   entry boundary into `troubleshoot/<skill>-<topic>.md`, leaving the main file as
   an index plus the entries most likely to be hit. A split must be **lossless**:
   same entries, same text, new homes — verify by counting entries before/after.
2. **Two topics** — you can name them independently and a reader doing task A
   never needs B.
3. **Two shapes** — one chunk reads as *rule* and the next as *this broke here*.
   That is the `skills/` ↔ `troubleshoot/` boundary, and it is what the first split
   of this tree was for: `tyra-engine-dev`'s "Hard-won pitfalls" was 1064 lines of
   exactly this shape.

### Extracting, not just splitting

After a removal, the remainder must be **shorter and clearer**. If a sentence now
says "see the pitfalls section" with nothing to point at, the split failed. Every
extracted file is reachable **by name** from both the skill it serves and
`AGENT-READ-ME-FIRST.md`, and the pointer must be *specific* ("pipeline stalls →
`troubleshoot/tyra-engine-dev.md`"), not "see the other files".

## Writing a new skill

1. **Check it is a skill** — state the rule in one sentence. If you cannot, it is
   reference material: delete it or point at `docs/`.
2. **Name it for the TASK** (`tyra-<area>`), and keep the YAML frontmatter.
3. **One skill per routing-table row** in `AGENT-READ-ME-FIRST.md`.
4. **Link it** from that table, and link its troubleshoot files from its "get
   stuck" section.
5. **Mirror it into `.agents/skills/` in the same commit** — see the parity rule
   below. Never edit only one side.

## Writing a troubleshoot entry

One repeated format:

```
## <the symptom, in the observer's words>
**Cause:** <one or two sentences>
**Fix:** <what to do>
```

Symptom **first**: the reader arrives with the symptom, not the cause. If you
cannot state the symptom, you do not yet understand the failure — keep digging.
Name the file for the **skill it serves or the symptom area**:
`troubleshoot/tyra-engine-dev.md`, `troubleshoot/pcsx2-capture.md`.

Only failures that **actually happened here**. A troubleshooting file for a
hypothetical failure is fiction that misleads. No narrative, no "I spent all
afternoon" — but a line on *why it is counter-intuitive* is welcome, because
that is what stops the retry.

## Maintaining

- **Update in the same commit as the change that taught the lesson.** A skill
  that lags the code is worse than none: it is confidently wrong.
- **Prune on every edit.** Every pass should remove at least as much as it adds.
  These files rot by accretion, never by subtraction.
- **If a rule moved to `AGENTS.md`**, delete it here and leave the reference.
- **If a skill describes behaviour the code no longer has**, fix or delete it the
  day you notice.

## The twin-parity rule (this repo)

`.agents/skills/` is the **source of truth**; `.agents/skills/` is the Codex
twin — a plain regeneration with two substitutions (the skills path itself, and
`AGENTS.md` in place of the root instruction file). **Never hand-edit only one
side**, and mirror any new skill, any edit and any new `troubleshoot/` folder in
the **same commit**. The twin was added as a one-off snapshot with no sync and
had drifted a full retired `PROGRESS.md` behind before anyone noticed; that is
what this rule prevents.

## Cross-repo boundary

`E:\dev\gunner` (the PS2 game) has its own `.agents/` tree. Content about **how
the engine works** belongs here; content about **how the game uses the engine**
belongs in Gunner's skills and troubleshoot files, cross-linking this side.
Duplicated text across the two repos diverges and then lies.