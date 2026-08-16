# Installing TyraX, and keeping it up to date

TyraX ships as a Windows installer and as three Linux packages, every push to
`main` publishes a new version, and the editor tells you when one has arrived.
This page is the whole loop: what each package puts on disk, how the update
check behaves and how to turn it off, and how a release is actually made.

Building from source is still first-class and unchanged — see the Quickstart in
the [README](../README.md). The packages are for people who want to *use* the
editor rather than work on it.

## What a release carries

Every release on the
[releases page](https://github.com/doctorspider42/tyraX/releases) has four
assets:

| Asset | For | Updates itself? |
|---|---|---|
| `TyraX-Setup-<version>.exe` | Windows 10 1809+ | yes |
| `tyrax-<version>-linux-x86_64.tar.gz` | any Linux | yes |
| `tyrax_<version>_amd64.deb` | Debian, Ubuntu, Mint… | no — `apt` does |
| `tyrax-<version>-1.x86_64.rpm` | Fedora, openSUSE, RHEL… | no — `dnf`/`zypper` does |

**All four contain the same tree**, and that shape is not decoration:

```
bin/tyrax-editor      the editor
vendor/tyra/          the Tyra engine, as sources - the build container mounts this
tools/                ps2client, the ps2link build scripts, the VS Code extension
src/                  the nine VU framework files a project with VU scripts is given
examples/             the example projects
```

The editor finds all four of those *relative to its own binary*, one directory
up, exactly as it does in a development checkout where the binary sits in
`build/`. Ship only the executable and the first game build reports a missing
engine, so if you package TyraX yourself, keep that shape.

None of them bring Docker or PCSX2. Building a game needs Docker; running one
needs PCSX2 or a real console.

## Windows

`TyraX-Setup-<version>.exe` is built with
[Inno Setup 7](https://jrsoftware.org/) from `installer/tyrax.iss` — a 64-bit
Setup (`SetupArchitecture=x64`, since the editor is 64-bit and there is no
32-bit build) whose wizard follows Windows' light or dark mode.

**It installs per user by default**, into `%LOCALAPPDATA%\Programs\TyraX` — no
administrator rights, no UAC prompt. That is not a preference: it is what lets
an update install itself without stopping to ask Windows for permission. The
directory page still offers Program Files for everybody if you want it (and
`/ALLUSERS` on the command line forces it).

Uninstalling removes the program directory. It deliberately leaves your
projects and your editor settings (`%LOCALAPPDATA%\tyra-editor\editor.ini`)
alone.

## Linux

Three formats, and **which one you pick decides whether the editor can update
itself**. That is the whole trade, so it is worth stating plainly: what makes
the Windows installer pleasant is not that it is an installer, it is that it
installs *per user, without root*. A `.deb` or an `.rpm` cannot do that — every
update is a root operation — so the **tarball is the format that reproduces the
Windows experience**, and the two distro packages are a convenience layer for
people who would rather their package manager owned the files.

### The tarball — recommended

```bash
tar xzf tyrax-<version>-linux-x86_64.tar.gz -C ~/.local/share
~/.local/share/tyrax-<version>/bin/tyrax-editor
```

Unpack it anywhere you can write. Nothing is installed system-wide, nothing
needs root, and the editor updates itself in place — it downloads the next
tarball, closes (asking about unsaved work as usual), unpacks over its own
directory and starts again. The first run also writes a `.desktop` entry and an
icon into `~/.local/share`, so TyraX appears in your application menu and its
window carries the right icon under Wayland.

To uninstall, delete the directory. Your projects and
`~/.config/tyra-editor/editor.ini` are elsewhere and are untouched.

### The .deb

```bash
sudo apt install ./tyrax_<version>_amd64.deb
```

Installs the tree under `/opt/tyrax` with `/usr/bin/tyrax-editor` pointing at
it, plus the desktop entry and icon. It depends on the X11/GL libraries the
editor links, on `curl` (the update check), and on `zenity | kdialog` — the
editor has no file browser of its own, so without one of those every Open and
Import button has nothing to open. Docker is a *Suggests*, not a dependency:
you need it to build a game, not to run the editor.

Updating is `apt install ./tyrax_<newversion>_amd64.deb` with the newer file.
The editor will still tell you a new version exists; it just says to use your
package manager instead of offering to do it itself.

### The .rpm

```bash
sudo dnf install ./tyrax-<version>-1.x86_64.rpm
```

The same tree, the same `/opt/tyrax` layout, the same reasoning. It needs
rpm 4.13+ for its `(zenity or kdialog)` dependency, which is every distribution
new enough to run the binary anyway.

Its payload is **xz**, stated in the spec rather than left to the machine
doing the packaging — that default is the *builder's*, not the package's, and
v1.52.0 duly shipped a 31 MB rpm because the CI runner's `rpmbuild` reaches for
gzip. The same tree is 13 MB compressed properly. xz rather than zstd because
it asks only rpm 4.8 of whoever installs, and because Debian-family `rpm` links
liblzma for certain.

### Which distributions

The packages are built on Ubuntu 22.04 (glibc 2.35), so they run on anything
with glibc 2.35 or newer — Ubuntu 22.04+, Debian 12+, Fedora 36+ and their
relatives. A binary runs on a *newer* glibc than it was built against, never an
older one, so the CI runner image is the floor; it is named as such in
`.github/workflows/release.yml`.

x86_64 only. The PS2 toolchain runs in Docker either way, so an ARM host would
need a package of its own and there is no demand for one yet.

## The update check

At startup the editor asks GitHub once whether there is a newer release. It
happens on a worker thread — nothing waits for it — and **a modal appears only
if there is something newer**. If the check fails because the machine is
offline, nothing is shown at all; an editor that opens a dialog about your
Wi-Fi is an editor whose update check people switch off.

The dialog offers:

| | |
|---|---|
| **Download and install** | fetches the package, closes the editor (asking about unsaved work as usual) and applies the update, then reopens |
| **What's new** | the release page in your browser |
| **Skip this version** | stop mentioning *this* version at startup; the next release is announced normally |
| **Later** | ask again next time |

**The first button appears only when this install can replace itself** — a
Windows install or a Linux tarball, and in the tarball's case only if the
directory is actually writable. A `.deb`, an `.rpm` or a source checkout gets a
sentence naming what to do instead (`apt`, `dnf`, or `git pull && ./build.sh`)
rather than a button that cannot work. The editor tells the cases apart by a
one-word `.tyrax-package` marker file at the install root, written by the
packaging script; no marker means a source checkout, which is the correct
answer for a repo you built yourself.

One consequence worth knowing about the in-place update, on both platforms: it
**overlays** the new files onto the old tree rather than wiping it first. A file
that a later release stopped shipping is left behind. That is deliberate — the
alternative is deleting a directory somebody may have put their own projects
in — and it is what the Windows installer has always done.

**Help > Check for updates** does the same thing on demand, and answers even
when there is nothing new ("TyraX 1.52.0 is the latest version") or when the
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
   `src/version.hpp`. If `v<that>` is not tagged, it releases the tree as it
   stands; if it is, it takes the highest `v<MAJOR>.<MINOR>.*` tag and goes one
   PATCH past it. Then it creates and pushes the tag, which is what claims the
   version — and is the only thing the workflow writes to the repository.
2. **build** — Windows runner: stamps that PATCH into `src/version.hpp` *in the
   workspace*, builds the editor with MinGW-w64 and packages
   `dist\TyraX-Setup-<version>.exe` with Inno Setup 7.
3. **build-linux** — Ubuntu 22.04 runner: builds the editor and runs
   `installer/build-package.sh --all` for the tarball, the `.deb` and the
   `.rpm`.
4. **release** — publishes the tag with generated notes and all four attached.

**`src/version.hpp` is where the version is authored** — in CI and on a
developer's machine alike (`installer/build-installer.ps1` and
`installer/build-package.sh` read the same three macros with the same regex) —
but between releases its PATCH is a *floor*, not a fact: the tags
record which patches are spent, and a release goes past them. So a build from a
checkout reports the number in the file, while a released build reports the
number on its tag, because the build job rewrites the file before compiling.
That is what keeps the binary, the installer and the release saying one
version — otherwise a build released as 1.51.1 would introduce itself as 1.51.0
and then offer itself an update, forever.

CI never touches MAJOR or MINOR. Bumping those by hand, with the changelog
paragraph that file expects, is what should happen for a feature (see
[format versioning](format-versioning.md) for the rules the two numbers in that
file follow) and it resets the patch sequence. The automatic PATCH is the floor
underneath, so that no push to `main` can leave `main` unreleased.

> **Why not commit the bump back?** The first version of this workflow did,
> with `[skip ci]`. `main` carries a ruleset saying *changes must be made
> through a pull request*, so the push was rejected (`GH013`) and the release
> stopped there. Rulesets apply to branches; a **tag** push is unaffected, which
> is why the tag — not a commit — is what records a version as used.

No asset name is hardcoded in the editor: the update check picks the asset whose
name *ends* with its platform's suffix (`.exe`, `-linux-x86_64.tar.gz`), so the
version inside a file name is free to move. Those two suffixes are the only part
of the naming that is a contract.

### Building packages yourself

```powershell
./installer/build-installer.ps1              # build the editor, then package it
./installer/build-installer.ps1 -SkipBuild   # package what is already in build/
```

It needs Inno Setup 7 — `winget install JRSoftware.InnoSetup.7`. **Chocolatey's
`innosetup` package is the 6.x line and will not do**; that is not a detail, it
is what broke the first release run, and the script now says so by name rather
than letting the compile die inside the `.iss`'s own version guard. It looks for
`ISCC.exe` in the usual places (per-user installs included), **checks the major
version of each candidate** and takes the first that is 7 or newer — so a
chocolatey 6.x shim sitting on `PATH` is skipped rather than picked. CI
downloads Inno Setup 7 straight from its own GitHub release and verifies the
SHA-256 before running it.

The result is `dist\TyraX-Setup-<version>.exe`. The `.iss` refuses to compile
under Inno Setup 6 rather than quietly producing a different installer, and it
uses 7-only features (`SetupArchitecture`, the `dynamic` wizard appearance), so
that refusal is real rather than ceremonial.

```bash
./installer/build-package.sh                 # build the editor, then the tarball
./installer/build-package.sh --skip-build    # package what is already in build/
./installer/build-package.sh --all           # tarball + .deb + .rpm
```

A bare run makes the tarball, because that is the format an install can update
itself from; naming formats (`--tar`, `--deb`, `--rpm`, `--all`) makes exactly
those. `--deb` needs `dpkg-deb` (`apt-get install dpkg-dev`) and `--rpm` needs
`rpmbuild` (`apt-get install rpm`, or `dnf install rpm-build`); both are staged
from the same tree the tarball is, so the three cannot disagree about what is
inside them.
