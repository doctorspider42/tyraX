#!/usr/bin/env bash
# Starts the script debugger's WSL relay (gdb-relay.c) for one GDB port.
# Windows only: the editor runs `wsl.exe --exec bash gdb-relay.sh <port>` while
# it serves a game with the script debugger on (docs/script-debugger.md).
# Compiles the relay with the WSL host's cc on first use or when the source is
# newer, and replaces a relay a previous editor left on the same port.
set -eu
if [ "$#" -ne 1 ]; then
  echo "usage: gdb-relay.sh <gdb-port>" >&2
  exit 2
fi
here=$(cd "$(dirname "$0")" && pwd)
out="${XDG_CACHE_HOME:-$HOME/.cache}/tyrax"
bin="$out/tyrax-gdb-relay"
mkdir -p "$out"
if [ ! -x "$bin" ] || [ "$here/gdb-relay.c" -nt "$bin" ]; then
  cc -O2 -o "$bin.tmp" "$here/gdb-relay.c"
  mv "$bin.tmp" "$bin"
fi
pkill -f "^$bin $1\$" 2>/dev/null || true
exec "$bin" "$1"
