# Disc images and the title ID

*Project > Export PS2 ISO* writes a bootable disc image next to the project
(`<name>.iso`). It is the shipping form of a TyraX game: a real console has no
`host:` filesystem, so the ELF the editor builds into `bin/` can only be reached
by a development tool (PCSX2's host fs, or ps2link over the network). A disc
image is the same game with nothing external holding it up — it boots in PCSX2
from *System > Start File*, burns to DVD-R, and runs from a USB drive through
Open PS2 Loader.

Nothing about it is exotic, and one myth is worth killing first: **retail PS2
games boot an ELF too.** A commercial disc carries a `SYSTEM.CNF` reading

```
BOOT2 = cdrom0:\SLUS_213.45;1
VER = 1.00
```

and the file `SLUS_213.45` is a plain PS2 ELF — just named after the game's
product code instead of carrying an `.elf` extension. So the difference between a
TyraX build and a retail release was never the executable format. It was the
wrapper (this page) and, before that, the fact that the assets came from a
developer's PC rather than from the disc.

## What the export actually does

Three things, in this order:

1. **Generates `SYSTEM.CNF`** — `BOOT2` pointing at the boot file, `VER = 1.00`,
   CRLF line endings like a retail disc. It is never written into the project; it
   is built in a temp file and embedded.
2. **Orders the files by load group** so the drive does not seek between reads:
   `SYSTEM.CNF` + the boot ELF first, then anything pinned in `iso-layout.txt`,
   then the startup assets (HUD, the built-in sprites, every sound effect —
   whatever `init()` opens), then each scene's assets in scene order, then the
   music, then the rest. *Project > Disc Layout...* shows the resulting LBA map
   and lets you drag rows to pin an order of your own.
3. **Writes the ISO9660 image** with an in-tree writer (`src/iso9660.cpp`)
   rather than shelling out to mkisofs — the whole point of rolling our own is
   that file extents land in exactly the order the planner chose.

The engine reads from `cdrom0:` with no change on the game's side: names are
upper-cased with a `;1` version suffix and `\` separators, handled in
`vendor/tyra/engine/src/file/file_utils.cpp`.

## The title ID

*Project > Preferences > Build > Title ID* is the game's product code, shaped
like a retail one: four letters, three digits, two digits (`TYRA_141.83`). The
field is tolerant — case and separators are filled in, so `tyra14183` becomes
`TYRA_141.83` — and anything that is not four letters followed by five digits is
rejected rather than half-accepted. A fresh project is given one derived from its
stable project id, so two projects on one machine never collide; **Suggest**
re-derives it.

Three things read it:

- **The boot file's name on the disc.** With a title ID the ELF lands on the
  image as `TYRA_141.83` and `SYSTEM.CNF` points `BOOT2` there — exactly the
  retail arrangement. Without one it keeps the ELF's own name
  (`MYGAME.ELF`), which boots fine and looks like homebrew.
- **The memory card save folder.** `SAVE_MC_DIR` in the generated
  `inc/save_system.gen.hpp` becomes `/BATYRA-14183`, the same `BA` + flattened-id
  derivation retail uses (`BASLUS-21345`).
- **Open PS2 Loader and PCSX2**, which key per-game configuration, per-game
  memory cards and cover art on the ID.

### Why the default prefix is not SLUS

`SLUS`, `SLES`, `SCUS`, `SCES`, `SLPS`, `SLPM` and friends are Sony publisher
codes, assigned per commercial release. Typing one in makes this project
*indistinguishable from that game* to every tool that keys on the ID: OPL and
PCSX2 will hand it that game's per-title settings, and it will share that game's
memory card folder. The editor warns when you use one. `TYRA_` is the default
because no console release carries it.

### Changing it moves the save folder

The save folder is derived from the ID, so changing the ID orphans saves already
written to a memory card under the old one — the game will not see them. This is
also the argument for setting the ID *early* and then leaving it alone: it is the
**stable** half of a project's identity. Renaming the project does not touch it,
whereas the pre-title-ID `TYRA-<NAME>` folder followed the project name and moved
whenever the project was renamed.

A project saved before title IDs existed has none, and keeps both legacy
behaviors (the ELF's own boot name, the `TYRA-<NAME>` save folder) until one is
assigned — so an existing game's saves are never moved behind its author's back.

## Verifying an image

The export has a headless twin, which is what makes the disc path checkable from
a script (it needs `bin/<name>.elf`, so build first):

```bash
tyrax-editor --export-iso <projectDir>
```

It logs the boot file, the save folder and the full LBA layout, then the image
size. To check the bytes rather than the log, the first data sector holds
`SYSTEM.CNF` and the metadata sectors below it hold the directory records:

```python
data = open('mygame.iso', 'rb').read()
print(data[22 * 2048: 22 * 2048 + 64].split(b'\x00')[0])  # b'BOOT2 = cdrom0:\\TYRA_141.83;1\r\nVER = 1.00\r\n'
print(b'TYRA_141.83;1' in data[:22 * 2048])               # the directory record
```

Then boot the image in PCSX2 (*System > Start File*) and read `emulog.txt` for
`ELF ... is executing`. Booting the ISO rather than the ELF is the only way to
exercise the `cdrom0:` path conversion at all — a `host:` boot goes through the
OS and hides an unnormalized asset path that a real console would fail on (see
the asset-path rule in the `tyra-editor-dev` skill).

## Not done yet

Two retail conventions the export still does not reproduce, both cosmetic on an
emulator and visible on hardware:

- **`icon.sys` + a save icon.** A retail save shows a 3D icon and a title in the
  console's memory card browser; a homebrew save without `icon.sys` is a generic
  entry. The save folder is now named correctly, but its icon is not there yet.
- **A UDF/ISO9660 bridge filesystem.** Retail PS2 DVDs carry both. A
  CD-sized ISO9660 image boots on hardware as-is; images past CD size have not
  been verified on a real console from this writer.
