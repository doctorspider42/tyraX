PRs and docs in english

Every change updates the documentation in the same commit — see the `tyra-docs` skill for the checklist (README, the relevant skills, example READMEs).

Files that exist once per platform (`deps.ps1`/`deps.sh`, `setup.*`, `build.*`, the `if(WIN32)`/`else()` and `#ifdef _WIN32`/`#else` halves) are edited as a PAIR, in the same commit — see "Platform parity" in the `tyra-editor-dev` skill.

Respond in the same language as the User's message.

All temporary work belongs under the primary checkout's git-ignored `.tmp/` directory: use `.tmp/worktrees/` for disposable Git worktrees and `.tmp/builds/`, `.tmp/tests/`, and `.tmp/artifacts/` for auxiliary builds, scratch projects, harnesses, logs, and captures. Linked worktrees share that primary scratch root; do not create scratch trees elsewhere. Remove each temporary item as soon as it is no longer needed; remove worktrees with `git worktree remove` only after their work is committed and pushed. Keep active worktrees until the task or review using them is finished. Persistent toolchain/dependency caches and authored project output are not temporary scratch data.
