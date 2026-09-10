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
Linux distribution and the same packages inside it. The editor reports the
exact install command when one is missing. Docker and Docker Desktop are not
required for normal builds.

For a manual install:

```bash
./tools/toolchain/setup.sh "$HOME/.cache/tyrax/ps2dev"
```

```powershell
.\tools\toolchain\setup.ps1
```

## Docker fallback

Choose **Docker fallback** under *Edit > Preferences > Build backend*, or pass
`--docker` after the project path to the headless `--build` command. The fallback
keeps the previous volume-based incremental build and can still select an image
through `TYRAX_IMAGE`. `docker/Dockerfile.fromsource` compiles the same vendored
OpenVCL, vclpp and bin2s trees as the native backend and runs the OpenVCL tests,
so fallback does not mean a second compiler implementation. The inherited
`docker/Dockerfile` remains solely as the Sony-vcl A/B reference.

![Native build backend selected in Editor Preferences](img/native-build-backend.png)

### Coming back from the Docker fallback

The container writes into the project through a bind mount **as root**, and on
Windows WSL stores that ownership in the file's metadata. A later native build
runs as your normal user, so every generated file the container left behind is
undeletable from WSL - `rm` reports *"Permission denied"* for the whole of
`bin/` and `obj/` even though the NTFS ACL grants you full control, and the
build fails on its first clean step:

```
[editor] Toolchain changed - rebuilding engine and game objects...
rm: cannot remove '.../examples/showcase/obj/gen': Permission denied
[editor] Native build failed.
```

`native-build.sh` handles this itself: when a native `rm` cannot remove one of
the generated trees it deletes it through Windows (`cmd.exe /c rd /s /q`), which
ignores the WSL metadata, and `make` recreates the tree as the current user
afterwards. Only `bin/`, `obj/` and the cached engine objects are ever dropped
this way - never sources or resources. To clear it by hand, delete the
project's `bin/` and `obj/` from Windows (Explorer, or `Remove-Item -Recurse
-Force`), not from the WSL shell.

Either clean puts the dropped tree's own `.gitignore` back. `bin/.gitignore`
and `obj/.gitignore` are committed files - they are what keeps those
otherwise-empty directories in git - so wiping the tree used to leave the
checkout showing a deleted tracked file after every toolchain change and every
*Build > Clean*. The file is preserved byte for byte, so a project that
customised it keeps its own version.

## Licences and provenance

Every redistributed source tree carries its upstream licence file. The package
also includes `THIRD-PARTY-LICENSES.md`, which records the exact bases and the
modification status. The downloaded PS2DEV archive is not redistributed by
TyraX; it comes directly from the official PS2DEV release and its upstream
licences and source links travel inside that distribution. No Sony `vcl` binary
is installed by the native path.
