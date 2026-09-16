# Boot one arm in PCSX2, hold the garage-day pose, capture it, keep the log.
#
# Adapted from ../baked-vif-stream-2026-09-16/run-arm.ps1. Two rules it keeps,
# both learned the hard way:
#   - LAUNCH PCSX2 YOURSELF, never `--build --run`, which reaps other
#     worktrees' emulators.
#   - DELETE every devkit artefact the previous arm left first. A leftover
#     livedbg.cmd is applied at boot and eats the first capture; a leftover
#     frame.tga is what the next --capture-frame reads back.
#
# The benchmark script writes bin/district-benchmark.csv the frame it stops
# sampling, so the CSV appearing is the "it is safe to write to bin/ now"
# signal. Nothing may be written there before it.
[CmdletBinding()]
param(
  [Parameter(Mandatory=$true)][ValidateSet('ctl','cand','verify','poison')][string]$Arm,
  [string]$Fx   = "$env:TEMP\tyra-editor-test\eeR2",
  [string]$Out  = "$env:TEMP\tyra-editor-test\eeR2-arms",
  [int]$Pose    = 0,
  [int]$Captures = 3,
  [int]$WaitSeconds = 1500
)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..\..')).Path
$editor = Join-Path $root 'build\tyrax-editor.exe'
$bin  = Join-Path $Fx 'bin'
$dest = Join-Path $Out $Arm
New-Item -ItemType Directory -Force -Path $dest | Out-Null

Copy-Item -LiteralPath (Join-Path $Out "$Arm.elf") `
          -Destination (Join-Path $bin 'vehicle-playground.elf') -Force
(Get-Item (Join-Path $bin 'vehicle-playground.elf')).LastWriteTime = Get-Date
foreach ($f in 'livedbg.cmd','livedbg.bin','frame.tga','log.txt',
               'district-benchmark.csv','district-benchmark-pose.txt','vucap.bin') {
  Remove-Item -LiteralPath (Join-Path $bin $f) -Force -ErrorAction SilentlyContinue
}

$elfAbs = (Resolve-Path (Join-Path $bin 'vehicle-playground.elf')).Path
$p = Start-Process -FilePath 'C:\Program Files\PCSX2\pcsx2-qt.exe' -PassThru `
     -ArgumentList '-batch','-nogui','-logfile',(Join-Path $dest 'emulog.txt'),'-elf',$elfAbs
Write-Output "[$Arm] pcsx2 pid=$($p.Id)"

# A GATE ARM IS NEVER A TIMING ARM: this build folds every payload quadword the
# frame submits, so it runs at a fraction of normal speed and the sampling
# window takes correspondingly longer. That is the design, not a problem.
$csv = Join-Path $bin 'district-benchmark.csv'
$deadline = (Get-Date).AddSeconds($WaitSeconds)
while (-not (Test-Path -LiteralPath $csv)) {
  if ((Get-Date) -gt $deadline) { Write-Output "[$Arm] TIMEOUT waiting for the sampling window"; break }
  Start-Sleep -Seconds 5
}
Write-Output "[$Arm] sampling window done at $(Get-Date -Format HH:mm:ss)"

Set-Content -LiteralPath (Join-Path $bin 'district-benchmark-pose.txt') -Value "$Pose" -NoNewline
Start-Sleep -Seconds 40   # the sampler re-reads the pose file every 30 frames

for ($i = 1; $i -le $Captures; $i++) {
  Remove-Item -LiteralPath (Join-Path $bin 'frame.tga') -Force -ErrorAction SilentlyContinue
  & $editor --capture-frame $Fx -o (Join-Path $dest "shot$i.png") 2>&1 | Out-String | Write-Output
  Start-Sleep -Seconds 10
}

Start-Sleep -Seconds 20   # let one more STAPIPVIFHASH window land
Copy-Item -LiteralPath (Join-Path $bin 'log.txt') -Destination (Join-Path $dest 'log.txt') -Force -ErrorAction SilentlyContinue
Copy-Item -LiteralPath $csv -Destination (Join-Path $dest 'district-benchmark.csv') -Force -ErrorAction SilentlyContinue
Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue
Start-Sleep -Seconds 2
Write-Output "[$Arm] done"
