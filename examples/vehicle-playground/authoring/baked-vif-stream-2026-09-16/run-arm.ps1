param(
  [Parameter(Mandatory=$true)][string]$Elf,
  [Parameter(Mandatory=$true)][string]$Tag,
  [string]$Fx     = 'C:\Users\papaj\AppData\Local\Temp\tyra-editor-test\bakeab',
  [string]$Out    = 'C:\Users\papaj\AppData\Local\Temp\claude\D--tyra-editor\40dd8f5e-81d5-4703-bac0-575680f88601\scratchpad\arms',
  [string]$Editor = 'D:\tyra-editor\.claude\worktrees\agent-a56e6b39381318e57\build\tyrax-editor.exe',
  [int]$Pose = 0,
  [int]$Captures = 3
)
$ErrorActionPreference = 'Stop'
$bin  = Join-Path $Fx 'bin'
$dest = Join-Path $Out $Tag
New-Item -ItemType Directory -Force -Path $dest | Out-Null

# Put this arm's ELF in place and clear every devkit artefact the previous arm
# left: a leftover livedbg.cmd is applied at boot and eats the first capture,
# and a leftover frame.tga is read back by the next --capture-frame.
Copy-Item -LiteralPath $Elf -Destination (Join-Path $bin 'vehicle-playground.elf') -Force
(Get-Item (Join-Path $bin 'vehicle-playground.elf')).LastWriteTime = Get-Date
foreach ($f in 'livedbg.cmd','livedbg.bin','frame.tga','log.txt','district-benchmark.csv','district-benchmark-pose.txt','vucap.bin') {
  Remove-Item -LiteralPath (Join-Path $bin $f) -Force -ErrorAction SilentlyContinue
}

$elfAbs = (Resolve-Path (Join-Path $bin 'vehicle-playground.elf')).Path
$p = Start-Process -FilePath 'C:\Program Files\PCSX2\pcsx2-qt.exe' -PassThru `
     -ArgumentList '-batch','-nogui','-logfile',(Join-Path $dest 'emulog.txt'),'-elf',$elfAbs
Write-Output "[$Tag] pcsx2 pid=$($p.Id) elf=$elfAbs"

# The benchmark script writes its CSV the frame it stops sampling, so the CSV
# IS the "it is safe to drive this" signal - nothing may be written to bin/
# before it appears.
$csv = Join-Path $bin 'district-benchmark.csv'
$deadline = (Get-Date).AddSeconds(420)
while (-not (Test-Path -LiteralPath $csv)) {
  if ((Get-Date) -gt $deadline) { Write-Output "[$Tag] TIMEOUT waiting for the sampling window"; break }
  Start-Sleep -Seconds 3
}
Write-Output "[$Tag] sampling window done at $(Get-Date -Format HH:mm:ss)"

# Hold the garage-day pose. The sampler re-reads this every 30 frames.
Set-Content -LiteralPath (Join-Path $bin 'district-benchmark-pose.txt') -Value "$Pose" -NoNewline
Start-Sleep -Seconds 12

for ($i = 1; $i -le $Captures; $i++) {
  Remove-Item -LiteralPath (Join-Path $bin 'frame.tga') -Force -ErrorAction SilentlyContinue
  & $Editor --capture-frame $Fx -o (Join-Path $dest "shot$i.png") 2>&1 | Out-String | Write-Output
  Start-Sleep -Seconds 3
}

Start-Sleep -Seconds 8
Copy-Item -LiteralPath (Join-Path $bin 'log.txt') -Destination (Join-Path $dest 'log.txt') -Force -ErrorAction SilentlyContinue
Copy-Item -LiteralPath $csv -Destination (Join-Path $dest 'district-benchmark.csv') -Force -ErrorAction SilentlyContinue

Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue
Start-Sleep -Seconds 2
Write-Output "[$Tag] done"
