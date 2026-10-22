#!/bin/bash
# Deploy a built arm to the physical PS2 over ps2link and serve its host: files.
# Run it as a background task; it IS the host: server - stop it by stopping the
# task, and never kill ps2client by name (other sessions may own one).
# usage: deploy.sh <projectdir> <logfile> [noreset]
# env: PS2_IP (default 192.168.100.150)
# Prefer `noreset` on the first deploy after a power cycle: a ps2link reset after a
# long run regularly wedges the console (`freepad: DMA Busy`).
PROJ="$1"; LOG="$2"
HERE="$(cd "$(dirname "$0")" && pwd)"
PC="$HERE/../ps2client/bin/ps2client.exe"
IP="${PS2_IP:-192.168.100.150}"
cd "$PROJ/bin" || exit 3
[ -f ps2link.run ] || { echo "NO ps2link.run marker" > "$LOG"; exit 4; }
if [ "$3" != "noreset" ]; then
  timeout 20 "$PC" -h "$IP" -t 10 reset > "$LOG.reset" 2>&1
  sleep 10
fi
ELF=$(ls *.elf | head -1)
exec "$PC" -h "$IP" execee "host:$ELF" -ps2link > "$LOG" 2>&1
