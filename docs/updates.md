# Installing TyraX, and keeping it up to date

TyraX ships as a Windows installer, every push to `main` publishes a new
version, and the editor tells you when one has arrived. This page is the whole
loop: what the installer puts on disk, how the update check behaves and how to
turn it off, and how a release is actually made.

Building from source is still first-class and unchanged — see the Quickstart in
the [README](../README.md). The installer is for people who want to *use* the
editor rather than work on it.

## The installer

`TyraX-Setup-<version>.exe` is attached to every release on the
[releases page](https://github.com/doctorspider42/tyraX/releases). It is built
with [Inno Setup 7](https://jrsoftware.org/) from `installer/tyrax.iss`.

**It installs per user by default**, into `%LOCALAPPDATA%\Programs\TyraX` — no
administrator rights, no UAC prompt. That is not a preference: it is what lets
an update install itself without stopping to ask Windows for permission. The
directory page still offers Program Files for everybody if you want it (and
`/ALLUSERS` on the command line forces it).

What lands there is a **repo-shaped tree**, not a lone executable:

```
TyraX\
  bin\tyrax-editor.exe
  vendor\tyra\        the Tyra engine, as sources - the build container mounts this
  tools\              ps2client, the ps2link build scripts, the VS Code extension
  src\                the nine VU framework files a project with VU scripts is given
  examples\           the example projects (an optional component)
```

The editor finds all four of those *relative to its own binary*, one directory
up, exactly as it does in a development checkout where the binary sits in
`build/`. Ship only the `.exe` and the first game build reports a missing
engine, so if you package TyraX yourself, keep that shape.

Uninstalling removes the program directory. It deliberately leaves your
projects and your editor settings (`%LOCALAPPDATA%\tyra-editor\editor.ini`)
alone.

**There is no Linux package yet.** On Linux, build from source (`./build.sh`);
the editor's update check there will tell you a new version exists and send you
to the release page rather than offering to install anything.

## The update check

At startup the editor asks GitHub once whether there is a newer release. It
happens on a worker thread — nothing waits for it — and **a modal appears only
if there is something newer**. If the check fails because the machine is
offline, nothing is shown at all; an editor that opens a dialog about your
Wi-Fi is an editor whose update check people switch off.

The dialog offers:

| | |
|---|---|
| **Download and install** | fetches the installer, closes the editor (asking about unsaved work as usual) and runs the update silently, then reopens |
| **What's new** | the release page in your browser |
| **Skip this version** | stop mentioning *this* version at startup; the next release is announced normally |
| **Later** | ask again next time |

**Help > Check for updates** does the same thing on demand, and answers even
when there is nothing new ("TyraX 1.51.0 is the latest version") or when the
check cannot be made — a skipped version included, because someone asking is
asking about it too.

### Turning it off

*Edit > Preferences > Updates* has **Check for updates at startup**. With it
off, nothing leaves the machine on its own; the menu item still works whenever
you ask it to. The setting lives in `editor.ini` next to the other machine-wide
preferences (`updateCheck=0`), so it is per installation and not per project.

The check is one unauthenticated request to `api.github.com` — no account, no
telemetry, nothing sent about you or your project. It goes through `curl`,
which Windows has shipped since version 1803 and every Linux distribution has;
a machine without it loses the update check and nothing else.

## How a release is made

`.github/workflows/release.yml` runs on every push to `main`:

1. **version** — reads `TYRAX_VERSION_MAJOR/MINOR/PATCH` from
   `src/version.hpp`. If that version is already tagged, it bumps `PATCH`,
   commits the header back to `main` with `[skip ci]` and uses the new number.
   Then it creates and pushes the tag, which is what claims the version.
2. **build** — Windows runner: builds the editor with MinGW-w64 and packages
   `dist\TyraX-Setup-<version>.exe` with Inno Setup 7.
3. **release** — publishes the tag with generated notes and that installer
   attached.

So **`src/version.hpp` is the single source of the version**, in CI and on a
developer's machine alike (`installer/build-installer.ps1` reads the same three
macros). Bumping MINOR by hand, with the changelog paragraph that file expects,
is what should happen for a feature — see
[format versioning](format-versioning.md) for the rules the two numbers in that
file follow. The automatic PATCH bump is the floor underneath, so that no push
to `main` can leave `main` unreleased.

### Building an installer yourself

```powershell
./installer/build-installer.ps1              # build the editor, then package it
./installer/build-installer.ps1 -SkipBuild   # package what is already in build/
```

It needs Inno Setup 7 (`winget install JRSoftware.InnoSetup.7`); the script
finds `ISCC.exe` in the usual places, including a per-user install. The result
is `dist\TyraX-Setup-<version>.exe`. The `.iss` refuses to compile under Inno
Setup 6 rather than quietly producing a different installer.
