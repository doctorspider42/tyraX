PRs and docs in english

Every change updates the documentation in the same commit — see the `tyra-docs` skill for the checklist (README, PROGRESS, the relevant skills, example READMEs).

Respond in the same language as the User's message.

## Never escalate privileges silently

Never run `pkexec`, and never run bare `sudo`. Both hand off to a graphical password dialog that
blocks the command indefinitely — you get no output, no error, and no way to tell that the user is
staring at a prompt. From your side it just looks like a hang.

`.claude/settings.json` denies these commands outright, so an attempt fails immediately instead of
hanging. When that happens, **stop and report** — in the same turn, before doing anything else:

- the exact command you wanted to run,
- why it needed root,
- what is now blocked as a result.

Do not retry, do not reach for a privileged workaround (`pkexec`, `setfacl` on system files,
writing under `/etc`), and do not quietly skip the step and continue as if it succeeded. Root
access is the user's call, not yours.

**The common case — `docker` fails with permission denied on `/var/run/docker.sock`:** the login
session is missing the `docker` group. Confirm it by comparing `id` (this process) with
`id "$USER"` (the user database) — if the second lists `docker` and the first does not, that is it.
Do not patch the socket with `setfacl`. Report that the user needs to log out and back in (or
reboot) for the group to take effect, and stop there.