#!/usr/bin/env bash
# Packages TyraX for macOS: TyraX.app inside a drag-to-Applications disk image,
# signed and (when credentials are given) notarized - the macOS sibling of
# build-package.sh (Linux) and build-installer.ps1 (Windows).
#
#   ./installer/build-macos.sh                       # build, sign (auto), dmg
#   ./installer/build-macos.sh --skip-build          # package build-macos/editor as is
#   ./installer/build-macos.sh --sign -              # ad-hoc signature only
#   ./installer/build-macos.sh --notary-profile tyrax-notary
#   ./installer/build-macos.sh --version 1.2.3 --out-dir dist
#
# Output: <out-dir>/tyrax-<version>-macos-<arch>.dmg. The "-macos-<arch>.dmg"
# tail is a contract: update::platformAssetSuffix picks the release asset by
# it, and update::runInstaller mounts it and swaps TyraX.app.
#
# WHAT IS INSIDE (docs/updates.md#macos). The app is the same repo-shaped tree
# every TyraX package ships - installer/stage-tree.sh, the one definition the
# Linux packager uses too - as TyraX.app/Contents/Resources, plus:
#   Contents/MacOS/tyrax-editor     the editor (platform::resourceBinDir maps
#                                   its lookups onto Resources/bin)
#   Contents/Resources/tools/macos  installer/macos/build-host-tools.sh: GNU
#                                   make, the gmp/mpfr/mpc/zstd the PS2DEV gcc
#                                   needs, prebuilt OpenVCL/vclpp/bin2s and
#                                   machorelink, with their sources - so building
#                                   a game needs neither Homebrew nor the Xcode
#                                   Command Line Tools (only a project with its
#                                   own VU programs needs a compiler)
#
# SIGNING. --sign <identity> | auto | -. "auto" (the default) takes the first
# "Developer ID Application" identity in the keychain, else signs ad hoc (runs
# on this Mac; Gatekeeper refuses it anywhere else). Every Mach-O inside is
# signed with the hardened runtime and a secure timestamp, then the bundle,
# then the disk image - the order notarization requires.
#
# NOTARIZATION. Either --notary-profile <name> (a keychain profile made once
# with `xcrun notarytool store-credentials <name> ...`), or the App Store
# Connect API key in APPLE_API_KEY_PATH + APPLE_API_KEY_ID + APPLE_API_ISSUER
# (what CI uses). Without either the image is signed but not notarized. With
# it, the app is notarized and stapled BEFORE it goes into the image (so a
# copy dragged out works offline), then the image itself.
#
# Runs on the build Mac's arch: arm64 on Apple silicon, x86_64 on Intel. The
# PS2DEV archive a user's first build downloads is chosen at run time per arch
# (tools/toolchain/setup.sh), so each image only has to match its own editor.
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."
REPO="$PWD"
# shellcheck source=installer/stage-tree.sh
. "$REPO/installer/stage-tree.sh"

VERSION=""
OUT_DIR=""
SKIP_BUILD=0
SIGN="auto"
NOTARY_PROFILE="${TYRAX_NOTARY_PROFILE:-}"
while [ $# -gt 0 ]; do
    case "$1" in
        --skip-build) SKIP_BUILD=1 ;;
        --version) shift; VERSION="${1-}" ;;
        --out-dir) shift; OUT_DIR="${1-}" ;;
        --sign) shift; SIGN="${1-}" ;;
        --notary-profile) shift; NOTARY_PROFILE="${1-}" ;;
        *) echo "Unknown option: $1" >&2
           echo "Expected --skip-build, --version <x.y.z>, --out-dir <dir>, --sign <identity|auto|->, --notary-profile <name>" >&2
           exit 2 ;;
    esac
    shift
done
[ "$(uname -s)" = Darwin ] || { echo "build-macos.sh runs on macOS only" >&2; exit 2; }

[ -n "$VERSION" ] || VERSION="$(tyrax_editor_version "$REPO")"
case "$VERSION" in
    [0-9]*.[0-9]*.[0-9]*) ;;
    *) echo "Version must be x.y.z, got '$VERSION'" >&2; exit 1 ;;
esac
[ -n "$OUT_DIR" ] || OUT_DIR="$REPO/dist"
mkdir -p "$OUT_DIR"
OUT_DIR=$(cd "$OUT_DIR" && pwd)
ARCH="$(uname -m)"                         # arm64 | x86_64
WORK="$REPO/build-macos"
export MACOSX_DEPLOYMENT_TARGET=15.0       # = LSMinimumSystemVersion in Info.plist.in
DMG="$OUT_DIR/tyrax-$VERSION-macos-$ARCH.dmg"

# ------------------------------------------------------------ the editor ----
EDITOR_BUILD="$WORK/editor"
if [ "$SKIP_BUILD" != 1 ]; then
    echo "== Building the editor (macOS $MACOSX_DEPLOYMENT_TARGET+, $ARCH) =="
    ./setup.sh   # vendor/ at the pinned commits, and the macOS ps2client
    cmake -S . -B "$EDITOR_BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_OSX_DEPLOYMENT_TARGET="$MACOSX_DEPLOYMENT_TARGET"
    cmake --build "$EDITOR_BUILD"
fi
EXE="$EDITOR_BUILD/tyrax-editor"
[ -x "$EXE" ] || { echo "No editor binary at $EXE - drop --skip-build." >&2; exit 1; }

# ---------------------------------------------------------- host tools ------
HOST_TOOLS="$WORK/tools-macos"
if [ ! -x "$HOST_TOOLS/bin/machorelink" ] \
   || [ "$REPO/installer/macos/build-host-tools.sh" -nt "$HOST_TOOLS/bin/machorelink" ] \
   || [ "$REPO/tools/toolchain/machorelink.c" -nt "$HOST_TOOLS/bin/machorelink" ]; then
    echo "== Building the bundled host tools =="
    rm -rf "$HOST_TOOLS"
    "$REPO/installer/macos/build-host-tools.sh" "$HOST_TOOLS"
fi
# OpenVCL, vclpp and bin2s are built from the trees the app ships; rebuild them
# whenever those trees are newer than the binaries (a stale prebuilt would
# silently assemble every VU1 program with the previous compiler).
if [ -n "$(find "$REPO/vendor/openvcl/src" "$REPO/vendor/vclpp" "$REPO/tools/toolchain/bin2s" \
          -newer "$HOST_TOOLS/bin/openvcl" -type f -print -quit)" ]; then
    echo "== VU tool sources changed - rebuilding the bundled host tools =="
    rm -rf "$HOST_TOOLS"
    "$REPO/installer/macos/build-host-tools.sh" "$HOST_TOOLS"
fi

# --------------------------------------------------------------- the app ----
echo "== Assembling TyraX.app $VERSION =="
STAGE="$WORK/stage"
APP="$STAGE/TyraX.app"
rm -rf "$STAGE"
mkdir -p "$APP/Contents/MacOS"
tyrax_stage_tree "$REPO" "" "$APP/Contents/Resources" macapp
mkdir -p "$APP/Contents/Resources/tools/macos"
cp -R "$HOST_TOOLS/." "$APP/Contents/Resources/tools/macos/"
install -m 0755 "$EXE" "$APP/Contents/MacOS/tyrax-editor"
sed "s/@VERSION@/$VERSION/g" "$REPO/installer/macos/Info.plist.in" > "$APP/Contents/Info.plist"
plutil -lint "$APP/Contents/Info.plist" >/dev/null
printf 'APPL????' > "$APP/Contents/PkgInfo"

ICONSET="$WORK/AppIcon.iconset"
rm -rf "$ICONSET"
mkdir -p "$ICONSET"
for px in 16 32 128 256 512; do
    sips -z "$px" "$px" "$REPO/installer/macos/AppIcon-1024.png" --out "$ICONSET/icon_${px}x${px}.png" >/dev/null
    sips -z $((px * 2)) $((px * 2)) "$REPO/installer/macos/AppIcon-1024.png" \
        --out "$ICONSET/icon_${px}x${px}@2x.png" >/dev/null
done
iconutil -c icns "$ICONSET" -o "$APP/Contents/Resources/AppIcon.icns"

# ------------------------------------------------------------- signing ------
if [ "$SIGN" = auto ]; then
    SIGN=$(security find-identity -v -p codesigning 2>/dev/null \
           | sed -n 's/.*"\(Developer ID Application: [^"]*\)".*/\1/p' | head -n 1)
    [ -n "$SIGN" ] || SIGN="-"
fi
if [ "$SIGN" = "-" ]; then
    echo "== Signing ad hoc (no Developer ID identity) - this image runs on this Mac only =="
    SIGN_ARGS=(--force --sign -)
else
    echo "== Signing with: $SIGN =="
    SIGN_ARGS=(--force --timestamp --options runtime --sign "$SIGN")
fi
# Inside out: every Mach-O in Resources (host tools, ps2client), then the
# bundle, whose seal covers them. `file` tells a Mach-O from a script or a
# Windows .exe (tools/ps2client ships both, and the PE is only data here).
while IFS= read -r -d '' f; do
    if file -b "$f" | grep -q '^Mach-O'; then
        codesign "${SIGN_ARGS[@]}" "$f"
    fi
done < <(find "$APP/Contents/Resources" -type f -print0)
codesign "${SIGN_ARGS[@]}" "$APP"
codesign --verify --deep --strict "$APP"

# --------------------------------------------------------- notarization -----
NOTARY=()
if [ -n "$NOTARY_PROFILE" ]; then
    NOTARY=(--keychain-profile "$NOTARY_PROFILE")
elif [ -n "${APPLE_API_KEY_PATH:-}" ] && [ -n "${APPLE_API_KEY_ID:-}" ] && [ -n "${APPLE_API_ISSUER:-}" ]; then
    NOTARY=(--key "$APPLE_API_KEY_PATH" --key-id "$APPLE_API_KEY_ID" --issuer "$APPLE_API_ISSUER")
fi
notarize() {  # notarize <file to submit> <file to staple> [dmg-may-stay-unstapled]
    echo "== Notarizing $(basename "$1") (this waits for Apple) =="
    xcrun notarytool submit "$1" "${NOTARY[@]}" --wait --timeout 1h --output-format json \
        > "$WORK/notary.json"
    if ! grep -q '"status" *: *"Accepted"' "$WORK/notary.json"; then
        cat "$WORK/notary.json" >&2
        local id
        id=$(sed -n 's/.*"id" *: *"\([^"]*\)".*/\1/p' "$WORK/notary.json" | head -n 1)
        [ -z "$id" ] || xcrun notarytool log "$id" "${NOTARY[@]}" >&2 || true
        echo "Notarization failed." >&2
        exit 1
    fi
    # Stapling fetches the ticket from Apple's CloudKit, a separate service.
    # Right after an accepted submission that lookup can fail for minutes
    # ("The network connection was lost", Error 68) - every Intel release run
    # of 1.172.1 hit it on the dmg, while the app inside stapled fine. Retry
    # the fetch with a growing pause (~5 min in all); never resubmit.
    local attempt
    for attempt in 1 2 3 4 5 6; do
        xcrun stapler staple "$2" && return 0
        [ "$attempt" -lt 6 ] || break
        echo "stapler failed (attempt $attempt of 6), retrying in $((attempt * 20))s" >&2
        sleep $((attempt * 20))
    done
    # The disk image is the one place an unstapled ticket is acceptable: it IS
    # notarized (Accepted above), so Gatekeeper finds the ticket online when
    # it is opened, and the app it carries has its own stapled ticket and
    # works offline once dragged out. The app itself must never ship unstapled.
    if [ "${3-}" = dmg-may-stay-unstapled ]; then
        echo "WARNING: $(basename "$2") is notarized but its ticket is not stapled (TyraX.app inside is)." >&2
        return 0
    fi
    echo "Stapling $(basename "$2") failed; it IS notarized, so a re-run only has to staple." >&2
    exit 1
}
if [ ${#NOTARY[@]} -gt 0 ] && [ "$SIGN" != "-" ]; then
    ditto -c -k --keepParent "$APP" "$WORK/TyraX-notarize.zip"
    notarize "$WORK/TyraX-notarize.zip" "$APP"
    rm -f "$WORK/TyraX-notarize.zip"
else
    echo "== Not notarizing (no --notary-profile / APPLE_API_KEY_*, or ad-hoc signed) =="
fi

# -------------------------------------------------------------- the dmg -----
echo "== Building the disk image =="
PY="$WORK/py"
if [ ! -x "$PY/bin/dmgbuild" ]; then
    python3 -m venv "$PY"
    # Pinned: dmgbuild writes the window layout (.DS_Store) directly.
    "$PY/bin/pip" install --quiet "dmgbuild==1.6.5"
fi
BG="$WORK/dmg-background.tiff"
tiffutil -cathidpicheck "$REPO/installer/macos/dmg-background.png" \
    "$REPO/installer/macos/dmg-background@2x.png" -out "$BG" >/dev/null
rm -f "$DMG"
"$PY/bin/dmgbuild" -s "$REPO/installer/macos/dmg-settings.py" \
    -D app="$APP" -D background="$BG" -D volicon="$APP/Contents/Resources/AppIcon.icns" \
    -D layout="$REPO/installer/macos/dmg_layout.py" \
    "TyraX" "$DMG"
if [ "$SIGN" != "-" ]; then
    codesign --force --timestamp --sign "$SIGN" "$DMG"
    if [ ${#NOTARY[@]} -gt 0 ]; then
        notarize "$DMG" "$DMG" dmg-may-stay-unstapled
    fi
fi

echo "== Done =="
ls -lh "$DMG"
