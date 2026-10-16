# The start page, New Project and opening a project

The start page is what the editor shows while no project is open: the ways
into a project on the left, the bundled examples on the right, and a strip of
setup checks at the bottom. It replaces the docked layout until a project
opens - the panels would only say "No project open" - and *File > Close
Project* brings it back.

![The start page: New project and Open folder, the recent projects with when
each was last saved, the bundled examples as cards, and the setup chips at the
bottom.](img/start-page.png)

## Recent projects

**New project** opens the [New Project](#new-project) dialog (Ctrl+N, Cmd+N on
a Mac); **Open folder...** picks a folder holding a `.tyra` file (Ctrl+O).

Below them, the projects you opened, most recent first, each with its folder
(your home folder shown as `~`) and when it was last saved. Click a row to open
it. With more than four entries a **Filter** field narrows the list by name or
folder. The **...** on a hovered row offers *Open*, *Locate...*, *Reveal in
folder* and *Forget* - forgetting only drops the entry, the project stays on
disk.

A project whose folder is gone (an unplugged drive, a renamed folder) stays in
the list with a **moved or deleted** badge: clicking it asks where it is now
(*Locate...*) instead of failing, and the found folder replaces the entry.

The list lives in `editor.ini` (one `recentProject=` line per folder, at most
ten); the "last saved" time is the project file's own date, so it needs no
bookkeeping of its own.

## Examples

Every package ships the example projects of the [examples/](../examples)
folder. The right-hand side lists them as cards - the picture from the
example's README when it has one, its title and first sentence, and a category
tag. **Featured** shows six of them; **All** and the category chips
(Rendering, Gameplay, Vehicles, Cinema & UI, Performance) show the rest.

Clicking a card **opens a copy**: the example folder is copied into your
projects folder (*Edit > Preferences > Projects*, `~/TyraProjects` by default),
as `<example>`, or `<example>-2` beside an earlier copy, and the copy opens. The
shipped example is never edited - an installed package may not even be
writable, and the next update would overwrite your changes.

## Setup chips

The strip at the bottom says what the first Run will need, before it fails:

| Chip | Says | Click |
|---|---|---|
| TyraX *version* | up to date, an update is available (amber), or nothing yet | opens the update dialog, or checks now |
| PCSX2 | found (the configured path, else the same auto-detect Run uses) or not found (amber) | Edit > Preferences |
| Native toolchain | ready, or set up on the first build (amber - a one-time download); *Build: Docker fallback* when that backend is picked | Edit > Preferences |
| Real PS2 | the ps2link IP, or "no IP set" | Edit > Preferences |

The PCSX2 and toolchain checks look at the disk every few seconds, so
installing PCSX2 while the page is open turns the chip green without a restart.
On the right: links to Preferences, these docs and the release notes.
