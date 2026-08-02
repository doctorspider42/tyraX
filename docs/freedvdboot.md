# FreeDVDBoot: a disc that boots on an unmodified PlayStation 2

*Project > Export FreeDVDBoot ISO*, or `--export-fdvdb`, writes
`<project>/<name>-fdvdb.iso`: your game on a disc that a **stock** PS2 boots.
No modchip, no FreeMcBoot, no ESR, nothing installed on the console — you burn
the DVD-R, put it in, and the game starts.

This page explains why that is possible at all, what the editor generates, and
the two things it cannot do for you.

## Why a burned disc normally does nothing

A PS2 refuses to boot a burned game disc, and the reason is not one check but a
stack of them. The last one is physical: pressed PlayStation discs carry data
written as a modulation of the track itself, formed by the laser beam recorder
that cuts the glass master. A consumer burner writes pits into a dye layer
inside a pre-formed groove; nothing in it can reproduce that. Setting the DVD+R
"book type" to DVD-ROM (bitsetting) fools some players, but not this one.

So the game path is closed. The DVD-**Video** path is not — the PS2 is also a
DVD player, and a DVD player must accept DVD-R, because that is what the medium
is for. **FreeDVDBoot**, by CTurt, is a buffer overflow in the DVD Player's
parser for the IFO files that hold a DVD-Video's navigation structure. A
specially built `VIDEO_TS` makes the player jump into a payload on the disc,
which then loads an ELF. The disc is read as a movie and executes as a game.

The exploit is not a copy-protection bypass — it is a bug in a parser — which is
why it was published as research and why it is the normal way to run your own
code on hardware you own.

## What the editor writes

`-fdvdb.iso` is a **UDF/ISO9660 hybrid** ("bridge") disc. Both filesystems
describe the *same* sectors, and both are needed, for different readers:

| Side | Read by | Holds |
| --- | --- | --- |
| **UDF** | the PS2 DVD Player (`udfio`) | `VIDEO_TS/` — this is what triggers the exploit and loads the ELF |
| **ISO9660** | your running game (`cdrom0:`) | `SYSTEM.CNF`, the ELF, and every asset, laid out by the usual disc-layout rules |

`VIDEO_TS/` holds exactly three files:

- `VIDEO_TS.IFO` — the exploit trigger, **yours** (see below),
- `VTS_01_0.IFO` — exploit setup, **yours**,
- `VTS_02_0.IFO` — **your game's ELF**, under an IFO name. This is what
  FreeDVDBoot's loader launches; the file name is what the payload looks for,
  the contents are a plain PS2 ELF.

The ELF is on the disc twice: once as `VTS_02_0.IFO` for the exploit, once under
its own name for `SYSTEM.CNF`. That costs a few MB out of 4.7 GB and buys a disc
that also boots the ordinary way on a modded console.

The UDF side deliberately exposes **only** `VIDEO_TS/`. Your assets do not need
UDF entries — the game reads them through `cdrom0:`, which is the ISO9660 side.

Sector layout, if you ever need to reason about it:

```
0..15    system area
16..17   ISO9660 PVD + terminator
18..20   UDF volume recognition (BEA01 / NSR02 / TEA01)
32..47   UDF main volume descriptor sequence
48..63   UDF reserve sequence
64..65   logical volume integrity + terminator
256      anchor
257      partition block 0 = File Set Descriptor  (+ terminator at 258)
259..    ISO9660 metadata, then all file data
tail     UDF file entries and directory extents
last     closing anchor
```

## Setting it up

**1. Find your console's DVD Player version.** Boot with no disc and press
**Triangle**. Supported: all PS2 Slims and Bravia TV units (3.10 / 3.11), 3.04,
and some Phats on 2.10 / 2.12.

**2. Download the matching filesystem folder.** From
[CTurt's FreeDVDBoot](https://github.com/CTurt/FreeDVDBoot), take the
`Filesystems/<your version>/` directory — e.g.
`Filesystems/All PS2 slims (3.10 + 3.11) - English language/`.

The editor does **not** ship these files and never will: the FreeDVDBoot
repository carries no licence, so its contents are all-rights-reserved and not
ours to redistribute. You download them; the editor reads them.

**3. Point the editor at it.** *Edit > Preferences > FreeDVDBoot folder*. The
field validates as you type — the usual mistake is picking the repository root
instead of the folder for your version. Either the version folder or the
`VIDEO_TS` inside it is accepted.

**4. Export and burn.** *Project > Export FreeDVDBoot ISO*. Burn to **DVD-R**
(not +R, not RW — they strain a 20-year-old laser), **lowest speed**,
**finalised**.

**5. Set the console's system language to English.** The exploit depends on
memory contents that other languages change. Boot with no disc, press Circle,
System Configuration.

## The two things this cannot do for you

**It cannot be tested in PCSX2.** The emulator does not run the DVD Player at
all, so the exploit path is unreachable there. `-fdvdb.iso` still boots in PCSX2
as a plain PS2 disc (the ISO9660 side), which verifies your game — but *not*
that FreeDVDBoot fires. Only real hardware answers that.

**It cannot fix an ELF that lands on the loader.** FreeDVDBoot's loader is still
resident when your ELF takes over, so an ELF whose segments cover
`0x84000-0x85FFF` or `0x250000-0x29FFFF` hangs the console. The exporter parses
your ELF's program headers and **refuses to write the image** if they clash,
naming the segment:

```
error: the ELF overlaps FreeDVDBoot's loader and would hang the console:
  - segment 0x00240000-0x002BFFFF overlaps the loader's 0x250000-0x29FFFF
```

A stock Tyra game links at `0x100000` and is nowhere near either range, so this
should never fire — but a linker-script change could put you there, and finding
out on hardware costs an evening.

## CLI

```bash
tyrax-editor --export-fdvdb <projectDir> [--fdvdb-dir <folder>]
```

Export-only, like the rest of the disc tooling: it writes the image from
whatever is in `bin/`, so run `--build` first. That keeps it Docker-free and
scriptable. Without `--fdvdb-dir` the folder comes from `editor.ini`, so a
machine configured through Preferences needs no flag.

## What is not supported

**Phat consoles on 2.10-2.13.** That variant does not use a filesystem at all —
the ELF is copied into a fixed offset (`0x5bb000`) of a prebuilt 7 MB
`dvd.base.iso`, which leaves no room for a game's assets. It works for a
single self-contained ELF like uLaunchELF; it cannot carry a Tyra game. If you
have a Phat on 2.10/2.12, the practical route is to use stock FreeDVDBoot to
install FreeMcBoot, then boot the ordinary `-esr.iso` or run over ps2link.

## Credit

The exploit, the payloads and the IFO files are **CTurt's**
([FreeDVDBoot](https://github.com/CTurt/FreeDVDBoot),
[technical writeup](https://cturt.github.io/freedvdboot.html)). TyraX only
places files on a disc; none of the exploit is ours, and none of it is in this
repository.
