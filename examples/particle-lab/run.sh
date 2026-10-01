#!/usr/bin/env bash
# Runs the built ELF in PCSX2, outside the editor. Set PCSX2 below (or export
# PCSX2=/path/to/pcsx2) when yours is not on PATH / not a flatpak.
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"

PCSX2="${PCSX2:-}"
if [ -z "$PCSX2" ]; then
    for c in pcsx2-qt pcsx2; do
        command -v "$c" >/dev/null 2>&1 && PCSX2="$c" && break
    done
fi
if [ -z "$PCSX2" ] && command -v flatpak >/dev/null 2>&1 &&
   flatpak info net.pcsx2.PCSX2 >/dev/null 2>&1; then
    PCSX2="flatpak run net.pcsx2.PCSX2"
fi
if [ -z "$PCSX2" ]; then
    for c in "$HOME"/Applications/*.AppImage "$HOME"/Downloads/*.AppImage; do
        case "$(basename "$c")" in [Pp][Cc][Ss][Xx]2*) PCSX2="$c"; break;; esac
    done
fi
if [ -z "$PCSX2" ]; then
    echo "PCSX2 not found - install it or set PCSX2=/path/to/pcsx2" >&2
    exit 1
fi

ELF="bin/$(grep -oE '[^ ]*\.elf' Makefile | head -1)"
[ -f "$ELF" ] || { echo "$ELF not found - build the project first." >&2; exit 1; }

pkill -x pcsx2-qt >/dev/null 2>&1 || true
pkill -x pcsx2 >/dev/null 2>&1 || true
exec $PCSX2 -elf "$PWD/$ELF"
