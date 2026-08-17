#!/usr/bin/env bash
# Packages a built editor into dist/ - the POSIX twin of build-installer.ps1.
#
#   ./installer/build-package.sh                  # build the editor, then the tarball
#   ./installer/build-package.sh --skip-build     # package what is in build/ already
#   ./installer/build-package.sh --deb --rpm      # also the two distro packages
#   ./installer/build-package.sh --all
#   ./installer/build-package.sh --version 1.2.3 --out-dir dist
#
# THE VERSION COMES FROM src/version.hpp and from nowhere else. This script,
# build-installer.ps1 and .github/workflows/release.yml all read the same three
# macros, so "which version is this" has one answer on either platform's
# developer machine and in CI.
#
# THREE FORMATS, ONE STAGING TREE, AND THE TARBALL IS THE PRIMARY ONE. What
# makes the Windows installer good is not that it is an installer: it is that it
# installs PER USER, without root, which is what lets an update install itself
# with nothing to authenticate against (docs/updates.md). A .deb or .rpm cannot
# do that - every update is a root operation - so the tarball is the format that
# reproduces the Windows story, and the two distro packages are a convenience
# layer over the same staged tree for people who would rather their package
# manager owned the files. That is also why the editor only self-updates a
# tarball install and says so for the other two: see update::installKind, which
# reads the one-word marker file staged below.
#
# WHY THE TREE HAS THE SHAPE IT HAS: the editor resolves the bundled engine
# (templates.cpp: engineSourceDir), the PS2 deploy tools (runner.cpp: findTool),
# the VS Code extension (app.cpp: installVsCodeExtension) and the VU framework
# sources (project.cpp: editorSourceDir) RELATIVE TO ITS OWN BINARY, one
# directory up - which in a development checkout is the repo root, because the
# binary sits in build/. So the packaged layout reproduces that shape:
#
#   <root>/bin/tyrax-editor
#   <root>/vendor/tyra/...    the engine, as sources
#   <root>/tools/...          ps2client, the ps2link build scripts, the .vsix
#   <root>/src/...            the nine VU framework files a project is given
#   <root>/examples/...
#
# Shipping a bare binary compiles nothing: the first game build reports a
# missing engine. If you add a new exe-relative lookup to the editor, add its
# files to stage_tree below AND to installer/tyrax.iss in the same commit.
#
# The .deb and .rpm put that whole tree in /opt/tyrax (which both packaging
# policies allow for a self-contained third-party application) and symlink
# /usr/bin/tyrax-editor at it; platform::exePath resolves /proc/self/exe
# through fs::canonical, so the symlink lands on the real directory and the
# exe-relative lookups keep working.
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."
REPO="$PWD"

VERSION=""
OUT_DIR=""
SKIP_BUILD=0
WANT_TAR=0
WANT_DEB=0
WANT_RPM=0
EXPLICIT_FORMAT=0

while [ $# -gt 0 ]; do
    case "$1" in
        --skip-build|-SkipBuild) SKIP_BUILD=1 ;;
        --deb)  WANT_DEB=1; EXPLICIT_FORMAT=1 ;;
        --rpm)  WANT_RPM=1; EXPLICIT_FORMAT=1 ;;
        --tar|--tarball) WANT_TAR=1; EXPLICIT_FORMAT=1 ;;
        --all)  WANT_DEB=1; WANT_RPM=1; WANT_TAR=1; EXPLICIT_FORMAT=1 ;;
        --version|-Version) shift; VERSION="${1-}" ;;
        --out-dir|-OutDir)  shift; OUT_DIR="${1-}" ;;
        *) echo "Unknown option: $1" >&2
           echo "Expected --skip-build, --tar, --deb, --rpm, --all, --version <x.y.z>, --out-dir <dir>" >&2
           exit 2 ;;
    esac
    shift
done
# A bare run makes the tarball, because that is the format an install can
# update itself from. Naming any format at all makes exactly the formats named,
# so `--deb` does not silently also build a tarball.
[ "$EXPLICIT_FORMAT" = 1 ] || WANT_TAR=1

editor_version() {
    local part n out=""
    for part in MAJOR MINOR PATCH; do
        n="$(sed -n "s/^#define TYRAX_VERSION_$part[[:space:]]\+\([0-9]\+\)[[:space:]]*$/\1/p" \
             "$REPO/src/version.hpp" | head -n1)"
        [ -n "$n" ] || { echo "src/version.hpp has no TYRAX_VERSION_$part" >&2; exit 1; }
        out="${out:+$out.}$n"
    done
    echo "$out"
}

[ -n "$VERSION" ] || VERSION="$(editor_version)"
case "$VERSION" in
    [0-9]*.[0-9]*.[0-9]*) ;;
    *) echo "Version must be x.y.z, got '$VERSION'" >&2; exit 1 ;;
esac
[ -n "$OUT_DIR" ] || OUT_DIR="$REPO/dist"
case "$OUT_DIR" in /*) ;; *) OUT_DIR="$PWD/$OUT_DIR" ;; esac

ARCH_GNU="$(uname -m)"          # x86_64
ARCH_DEB="amd64"
[ "$ARCH_GNU" = "x86_64" ] || ARCH_DEB="$ARCH_GNU"
NAME="tyrax"
STAGE_TOP="$OUT_DIR/stage"
STAGE="$STAGE_TOP/$NAME-$VERSION"

if [ "$SKIP_BUILD" != 1 ]; then
    echo "== Building the editor =="
    "$REPO/build.sh"
fi
EXE="$REPO/build/tyrax-editor"
[ -x "$EXE" ] || {
    echo "No editor binary at $EXE - run ./build.sh first (or drop --skip-build)." >&2
    exit 1
}

# The nine VU framework files a project with VU scripts is handed. Exactly
# project.cpp's kVuFrameworkFiles - the probe it locates them with is
# src/vushader.hpp, so all nine travel or none do.
VU_FRAMEWORK=(vuir.hpp vuir.cpp vusim.hpp vusim.cpp vugen.hpp vugen.cpp
              vushader.hpp vushader.cpp vumain.cpp)

# rsync is not assumed: a fresh container has tar and cp and little else.
copy_tree() {  # copy_tree <src-dir> <dst-dir> <find-prune-args...>
    local src="$1" dst="$2"; shift 2
    mkdir -p "$dst"
    ( cd "$src" && find . "$@" -print0 | tar --null -cf - --files-from=- ) \
        | ( cd "$dst" && tar -xf - )
}

stage_tree() {  # stage_tree <marker word>
    local marker="$1"
    rm -rf "$STAGE"
    mkdir -p "$STAGE/bin" "$STAGE/src"

    install -m 0755 "$EXE" "$STAGE/bin/tyrax-editor"

    # The engine the editor compiles every game against, as sources - the build
    # container bind-mounts this directory read-only.
    #
    # PRUNE BY DIRECTORY HERE, NEVER BY EXTENSION, and the three below are
    # exactly what .gitignore drops under vendor/tyra: what is not in git is
    # build leftovers a dev checkout happens to carry, and everything else is
    # CONTENT. This list was once "*.o -o -name '*.a' -o -name '*.elf'", which
    # is the same intent spelled as a file type - and it silently dropped
    # vendor/tyra/audsrv/bin/libaudsrv.a, a committed artifact of the audsrv
    # fork that runner.cpp overlays onto the container's PS2SDK. Its two
    # siblings (audsrv.irx, audsrv.h) matched no pattern and travelled, so the
    # overlay's `cp a && cp b && cp c` died on the missing lib BEFORE copying
    # the header, every game then compiled against stock PS2SDK headers, and
    # the build ended in "'audsrv_adpcm_set_volume_and_pan' was not declared" -
    # for everyone who installed TyraX and for nobody who built from a checkout.
    copy_tree "$REPO/vendor/tyra" "$STAGE/vendor/tyra" \
        \( -name .git -o -path './engine/obj' -o -path './engine/bin' \
           -o -path './audsrv/.work' \) -prune -o -type f

    # ps2client (network deploy), the ps2link build scripts and the VS Code
    # extension package the editor installs on request. ps2client is fetched
    # per-OS by setup.sh, so this is the Linux build of it.
    copy_tree "$REPO/tools" "$STAGE/tools" \
        \( -path './ps2link/work' -o -name '*.o' -o -name '*.a' \) -prune -o -type f

    local f
    for f in "${VU_FRAMEWORK[@]}"; do
        install -m 0644 "$REPO/src/$f" "$STAGE/src/$f"
    done

    # The attribution a distributed build carries (LICENSE-EXCEPTION.md says
    # what a game made with it owes, which is nothing).
    for f in LICENSE LICENSE-EXCEPTION.md NOTICE THIRD-PARTY-LICENSES.md README.md; do
        install -m 0644 "$REPO/$f" "$STAGE/$f"
    done
    install -m 0644 "$REPO/resources/icon.png" "$STAGE/tyrax-editor.png"

    # Build output of an example that was opened in a dev checkout is not
    # content - a fresh clone (what CI packages) has none of it either way.
    copy_tree "$REPO/examples" "$STAGE/examples" \
        \( -name bin -o -name obj -o -name '.history' -o -name '*.elf' -o -name '*.iso' \) \
        -prune -o -type f

    # THE MARKER IS WHAT THE UPDATER READS (update::installKind). One word, no
    # newline needed, and its absence means "a source checkout" - which is the
    # correct answer for a repo built with ./build.sh.
    printf '%s\n' "$marker" > "$STAGE/.tyrax-package"

    # Whatever umask the packager happened to run under is not what ships: a
    # group-writable file inside a system package is a lintian/rpmlint finding
    # and, under /opt, a real one.
    chmod -R go-w "$STAGE"
}

desktop_entry() {  # desktop_entry <exec path>
    # Field codes and the app id must agree with platform::installDesktopEntry
    # and App::run's kAppId - the Wayland compositor matches the surface's app
    # id against this file's basename, and a mismatch costs the window its icon.
    cat <<EOF
[Desktop Entry]
Type=Application
Name=TyraX
GenericName=PS2 game editor
Comment=Edit PS2 scenes and flow graphs, then build and run the game
Exec=$1 %f
Icon=tyrax-editor
Terminal=false
StartupNotify=true
StartupWMClass=tyrax-editor
Categories=Development;IDE;Graphics;
Keywords=PlayStation;PS2;Tyra;game;engine;
EOF
}

mkdir -p "$OUT_DIR"
BUILT=()

# ---------------------------------------------------------------- tarball ----
if [ "$WANT_TAR" = 1 ]; then
    echo "== Packaging TyraX $VERSION (tar.gz) =="
    stage_tree tarball
    TARBALL="$OUT_DIR/$NAME-$VERSION-linux-$ARCH_GNU.tar.gz"
    rm -f "$TARBALL"
    # Sorted + fixed owner so two builds of one tree differ only where the
    # tree does; the top-level directory carries the version, so unpacking two
    # releases side by side cannot clobber either.
    tar --sort=name --owner=0 --group=0 --numeric-owner \
        -czf "$TARBALL" -C "$STAGE_TOP" "$NAME-$VERSION"
    BUILT+=("$TARBALL")
fi

# -------------------------------------------------------------------- deb ----
if [ "$WANT_DEB" = 1 ]; then
    command -v dpkg-deb >/dev/null 2>&1 || {
        echo "dpkg-deb not found - install dpkg-dev (apt-get install dpkg-dev)." >&2
        exit 1
    }
    echo "== Packaging TyraX $VERSION (deb) =="
    stage_tree deb
    ROOT="$OUT_DIR/deb/${NAME}_${VERSION}_${ARCH_DEB}"
    rm -rf "$ROOT"
    mkdir -p "$ROOT/opt/$NAME" "$ROOT/usr/bin" "$ROOT/usr/share/applications" \
             "$ROOT/usr/share/icons/hicolor/256x256/apps" "$ROOT/DEBIAN"
    cp -a "$STAGE/." "$ROOT/opt/$NAME/"
    ln -s "/opt/$NAME/bin/tyrax-editor" "$ROOT/usr/bin/tyrax-editor"
    desktop_entry /usr/bin/tyrax-editor > "$ROOT/usr/share/applications/tyrax-editor.desktop"
    install -m 0644 "$REPO/resources/icon.png" \
        "$ROOT/usr/share/icons/hicolor/256x256/apps/tyrax-editor.png"

    # zenity | kdialog because platform::pickFile shells out to whichever is
    # there and the editor has no file browser of its own; curl because that is
    # what the update check speaks. Docker is a Suggests and not a Depends: it
    # is needed to BUILD a game, not to run the editor, and pulling a container
    # runtime in as a hard dependency of a scene editor is not our call to make.
    INSTALLED_KB=$(du -sk "$ROOT/opt" | cut -f1)
    cat > "$ROOT/DEBIAN/control" <<EOF
Package: $NAME
Version: $VERSION
Section: devel
Priority: optional
Architecture: $ARCH_DEB
Maintainer: doctorspider42 <noreply@github.com>
Installed-Size: $INSTALLED_KB
Depends: libc6, libstdc++6, libgcc-s1, libgl1, libx11-6, libxrandr2, libxinerama1, libxcursor1, libxi6, libxkbcommon0, curl, zenity | kdialog
Suggests: docker.io | docker-ce, pcsx2
Homepage: https://github.com/doctorspider42/tyraX
Description: Editor for the Tyra PlayStation 2 game engine
 TyraX edits 3D scenes and flow graphs and generates a complete PS2 game
 project from them, which it builds in Docker and runs in PCSX2 or on a real
 console. It ships with the Tyra engine sources, the PS2 deploy tools and the
 example projects.
 .
 Installed under /opt/tyrax. Building a game additionally needs Docker;
 running one needs PCSX2 or a modded PlayStation 2.
EOF
    DEB="$OUT_DIR/${NAME}_${VERSION}_${ARCH_DEB}.deb"
    rm -f "$DEB"
    dpkg-deb --root-owner-group --build "$ROOT" "$DEB" >/dev/null
    BUILT+=("$DEB")
fi

# -------------------------------------------------------------------- rpm ----
if [ "$WANT_RPM" = 1 ]; then
    command -v rpmbuild >/dev/null 2>&1 || {
        echo "rpmbuild not found - install it (apt-get install rpm / dnf install rpm-build)." >&2
        exit 1
    }
    echo "== Packaging TyraX $VERSION (rpm) =="
    stage_tree rpm
    TOP="$OUT_DIR/rpmbuild"
    rm -rf "$TOP"
    mkdir -p "$TOP/SOURCES" "$TOP/SPECS" "$TOP/BUILD" "$TOP/RPMS" "$TOP/BUILDROOT"
    tar --sort=name --owner=0 --group=0 --numeric-owner \
        -czf "$TOP/SOURCES/$NAME-$VERSION.tar.gz" -C "$STAGE_TOP" "$NAME-$VERSION"
    desktop_entry /usr/bin/tyrax-editor > "$TOP/SOURCES/tyrax-editor.desktop"
    cp "$REPO/resources/icon.png" "$TOP/SOURCES/tyrax-editor.png"

    # The rich `(zenity or kdialog)` dependency needs rpm 4.13+ (2016), which
    # is every distribution this binary's glibc floor can run on anyway.
    # %define _build_id_links none: the payload is one prebuilt binary, and the
    # default build-id symlinks make rpmbuild fail on a package it did not
    # compile itself.
    cat > "$TOP/SPECS/$NAME.spec" <<EOF
%global _build_id_links none
%global __os_install_post %{nil}
%global debug_package %{nil}
# STATE THE PAYLOAD COMPRESSOR, because the default is the BUILDER's and not
# the package's: Ubuntu 22.04's rpmbuild (which is what CI runs) defaults to
# gzip, so v1.52.0 shipped a 31 MB rpm of the same tree a local rpm 6 packed
# into 15 MB with zstd - a payload nobody chose, twice the download for
# nothing. xz rather than zstd: it needs only rpm 4.8 (2010) on the machine
# INSTALLING, packs the same 15 MB, and - the reason that decides it - Debian
# and Ubuntu's rpm links liblzma for certain while its zstd support is not
# something to bet a release job on.
%define _binary_payload w7.xzdio

Name:           $NAME
Version:        $VERSION
Release:        1
Summary:        Editor for the Tyra PlayStation 2 game engine
License:        See /opt/$NAME/LICENSE
URL:            https://github.com/doctorspider42/tyraX
Source0:        %{name}-%{version}.tar.gz
Source1:        tyrax-editor.desktop
Source2:        tyrax-editor.png
BuildArch:      $ARCH_GNU
AutoReqProv:    no
Requires:       glibc, libstdc++, libgcc, libX11, libXrandr, libXinerama, libXcursor, libXi, libxkbcommon, curl
Requires:       (mesa-libGL or libGL)
Requires:       (zenity or kdialog)
Recommends:     docker-ce
Recommends:     pcsx2

%description
TyraX edits 3D scenes and flow graphs and generates a complete PS2 game project
from them, which it builds in Docker and runs in PCSX2 or on a real console. It
ships with the Tyra engine sources, the PS2 deploy tools and the example
projects.

Installed under /opt/$NAME. Building a game additionally needs Docker; running
one needs PCSX2 or a modded PlayStation 2.

%prep
%setup -q

%install
mkdir -p %{buildroot}/opt/$NAME
cp -a . %{buildroot}/opt/$NAME/
mkdir -p %{buildroot}%{_bindir}
ln -s /opt/$NAME/bin/tyrax-editor %{buildroot}%{_bindir}/tyrax-editor
mkdir -p %{buildroot}%{_datadir}/applications
install -m 0644 %{SOURCE1} %{buildroot}%{_datadir}/applications/tyrax-editor.desktop
mkdir -p %{buildroot}%{_datadir}/icons/hicolor/256x256/apps
install -m 0644 %{SOURCE2} %{buildroot}%{_datadir}/icons/hicolor/256x256/apps/tyrax-editor.png

%files
/opt/$NAME
%{_bindir}/tyrax-editor
%{_datadir}/applications/tyrax-editor.desktop
%{_datadir}/icons/hicolor/256x256/apps/tyrax-editor.png
EOF
    # rpmbuild traces every %install line to STDERR, so the log is captured and
    # only replayed when it failed - a successful package should print one line
    # like the other two formats do. The "absolute symlink" warning about
    # /usr/bin/tyrax-editor is expected and is the whole point of /opt.
    if ! rpmbuild --define "_topdir $TOP" -bb "$TOP/SPECS/$NAME.spec" \
         >"$TOP/rpmbuild.log" 2>&1; then
        cat "$TOP/rpmbuild.log" >&2
        echo "rpmbuild failed (spec: $TOP/SPECS/$NAME.spec)" >&2
        exit 1
    fi
    RPM="$(find "$TOP/RPMS" -name '*.rpm' -type f | head -n1)"
    [ -n "$RPM" ] || { echo "rpmbuild reported success but produced no .rpm" >&2; exit 1; }
    mv "$RPM" "$OUT_DIR/"
    BUILT+=("$OUT_DIR/$(basename "$RPM")")
fi

rm -rf "$STAGE_TOP" "$OUT_DIR/deb" "$OUT_DIR/rpmbuild"

echo
for f in "${BUILT[@]}"; do
    printf 'OK: %s (%s)\n' "$f" "$(du -h "$f" | cut -f1)"
done
