#!/bin/bash
# run_for.sh <projectdir> <log> <seconds> [noreset] - deploy.sh for a fixed
# time, then stop THIS run's ps2client by its verified Windows PID (a
# `timeout` on Windows leaves the native client running, and a live client
# keeps the console's host: session, so the next execee is lost). Never by name.
PROJ="$1"; LOG="$2"; SECS="$3"; MODE="${4:-reset}"
HERE="$(cd "$(dirname "$0")" && pwd)"
ELF=$(cd "$PROJ/bin" && ls *.elf | head -1)
bash "$HERE/deploy.sh" "$PROJ" "$LOG" $([ "$MODE" = noreset ] && echo noreset) &
sleep "$SECS"
# The client started for this run: same exe path, this ELF, newest first.
EXE="$(cygpath -w "$HERE/../ps2client/bin/ps2client.exe")"
powershell -NoProfile -Command "\$p = Get-CimInstance Win32_Process -Filter \"Name='ps2client.exe'\" | Where-Object { \$_.ExecutablePath -eq '$EXE' -and \$_.CommandLine -like '*host:$ELF*' } | Sort-Object CreationDate -Descending | Select-Object -First 1; if (\$p) { Stop-Process -Id \$p.ProcessId -Force; 'stopped ' + \$p.ProcessId } else { 'no client' }"
wait
echo "$(grep -ac '^CLD win' "$LOG") CLD windows, $(grep -ac 'DMA Busy' "$LOG") DMA Busy"
