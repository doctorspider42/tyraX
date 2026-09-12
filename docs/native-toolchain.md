# Native PS2 toolchain

TyraX builds games without Docker by default, using a pinned PS2DEV toolchain and
the reviewed VU tools shipped as source with the editor.

## What the first build installs

The editor runs `tools/toolchain/setup.sh` lazily. On Linux it runs directly; on
Windows `setup.ps1` invokes the same script through WSL, so both platforms use
the same Linux binaries and produce the same PS2 code. The only downloaded
artifact is the official PS2DEV v2.0.0 release archive. Its SHA-256 is pinned and
verified before extraction. It contains GCC, binutils, PS2SDK and the console
utilities that are impractical to bootstrap during every editor installation.

The parts TyraX changes are in this repository and are compiled locally:

- `vendor/openvcl` — TyraX fork snapshot `89efa51e`, based on upstream `a5867c3`;
- `vendor/vclpp` — the MIT-licensed VCL preprocessor at `00e44ecf`;
- `tools/toolchain/bin2s` — the AFL-2.0 PS2SDK utility at `8f397576`;
- `vendor/tyra/audsrv` — the LGPL-2.0 audsrv fork used by the engine.

Setup runs the complete OpenVCL suite before installing it. A content hash of
those source trees forms the toolchain identity, so changing a compiler flag or
vendored source invalidates both the tool install and cached VU/engine objects.
On Windows the default cache is `%LOCALAPPDATA%\tyra-editor\toolchain`; on Linux
it is `${XDG_CACHE_HOME:-~/.cache}/tyrax`.

## Host prerequisites

Linux needs `build-essential`, CMake, curl and rsync. Windows needs WSL with a
Debian/Ubuntu distribution and the same packages inside it. A normal build only
checks these host prerequisites and never changes the distribution. The
dedicated bootstrap installs them through `apt` only after an explicit
`--install`/`-Install` choice:

```bash
bash ./tools/toolchain/prepare-host.sh --install
```

```powershell
.\tools\toolchain\prepare-host.ps1 -Install
```

The Windows installer exposes the same operation as the unchecked **Prepare the
native PS2 toolchain in the default WSL distribution** task. Selecting it may
open a console for the WSL user's `sudo` password; it then provisions the pinned
toolchain too. Updates do not silently inherit the choice. Docker and Docker
Desktop are not required for normal builds.

For a manual install:

```bash
./tools/toolchain/setup.sh "$HOME/.cache/tyrax/ps2dev"
```

```powershell
.\tools\toolchain\setup.ps1 -InstallHostDependencies
```

For CI or a scripted build, `native-build.ps1 -PrepareHost` performs the same
opt-in bootstrap before compiling. Without that switch it remains read-only with
respect to the WSL distribution and prints the command above when a tool is
missing.

## Docker fallback

Choose **Docker fallback** under *Edit > Preferences > Build backend*, or pass
`--docker` after the project path to the headless `--build` command. The fallback
keeps the previous volume-based incremental build and can still select an image
through `TYRAX_IMAGE`. `docker/Dockerfile.fromsource` compiles the same vendored
OpenVCL, vclpp and bin2s trees as the native backend and runs the OpenVCL tests,
so fallback does not mean a second compiler implementation. The inherited
`docker/Dockerfile` remains solely as the Sony-vcl A/B reference.

![Native build backend selected in Editor Preferences](img/native-build-backend.png)

## Licences and provenance

Every redistributed source tree carries its upstream licence file. TyraX does
not bundle general-purpose Linux host binaries such as CMake, GCC or `rsync`:
they are dynamically linked to, and maintained by, the selected distribution;
duplicating an entire Linux userland would enlarge the installer substantially
and create a second security-update channel. The PS2-specific binaries are
either downloaded as the pinned official PS2DEV archive or built from the
redistributable sources shipped with TyraX.

The package also includes `THIRD-PARTY-LICENSES.md`, which records the exact
bases and the modification status. The downloaded PS2DEV archive is not
redistributed by TyraX; it comes directly from the official PS2DEV release and
its upstream licences and source links travel inside that distribution. No Sony `vcl` binary
is installed by the native path.
