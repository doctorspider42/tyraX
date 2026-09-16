# Boot one arm in PCSX2 and wait for the fixture to write its own CSVs.
#
# NEVER `--build --run`: it reaps other worktrees' emulators, and this machine
# routinely has several sessions live. PCSX2 is launched directly on this arm's
# ELF with its own -logfile, and only the process whose command line names THIS
# fixture is ever stopped.
#
# Run the editor through a normal --build --run once beforehand so that HostFs
# and `Renderer = 13` (software) are already in PCSX2.ini; this script does not
# rewrite the emulator's configuration.
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$Fixture,
    [Parameter(Mandatory = $true)][string]$Out,
    # The fixture writes after 1440 updates. At ~25 FPS in the garage that is
    # around 80 s of emulated time, plus the boot; 300 s is generous.
    [int]$TimeoutSec = 420,
    [string[]]$Expect = @('frame-inventory.csv', 'district-benchmark.csv')
)
$ErrorActionPreference = 'Stop'

$elf = Join-Path $Fixture 'bin/vehicle-playground.elf'
if (!(Test-Path -LiteralPath $elf)) { throw "No ELF at $elf" }
$elf = (Resolve-Path -LiteralPath $elf).Path   # PCSX2 rebases a relative -elf
New-Item -ItemType Directory -Force -Path $Out | Out-Null

# A previous run's artefacts must not be mistaken for this one's.
foreach ($f in $Expect + @('log.txt', 'livedbg.cmd', 'frame.tga')) {
    Remove-Item -LiteralPath (Join-Path $Fixture "bin/$f") -ErrorAction SilentlyContinue
}

$pcsx2 = @(
    "$env:ProgramFiles\PCSX2\pcsx2-qt.exe",
    "${env:ProgramFiles(x86)}\PCSX2\pcsx2-qt.exe"
) | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
if (-not $pcsx2) { throw 'pcsx2-qt.exe not found; set its path in Preferences' }

$log = Join-Path $Out 'emulog.txt'
$proc = Start-Process -FilePath $pcsx2 -PassThru -ArgumentList @(
    '-batch', '-nogui', '-logfile', $log, '-elf', $elf)
Write-Output "PCSX2 pid $($proc.Id) on $elf"

$deadline = (Get-Date).AddSeconds($TimeoutSec)
$done = $false
while ((Get-Date) -lt $deadline) {
    Start-Sleep -Seconds 5
    if ($proc.HasExited) { throw "PCSX2 exited early (code $($proc.ExitCode))" }
    $have = $Expect | Where-Object { Test-Path -LiteralPath (Join-Path $Fixture "bin/$_") }
    if ($have.Count -eq $Expect.Count) { Start-Sleep -Seconds 3; $done = $true; break }
}
try { $proc.CloseMainWindow() | Out-Null } catch {}
Start-Sleep -Seconds 2
if (-not $proc.HasExited) { Stop-Process -Id $proc.Id -Force }
if (-not $done) { throw "Timed out waiting for $($Expect -join ', ')" }

# The emulator's own log stays in $Out and is deliberately NOT part of what an
# evidence directory carries: it is BIOS noise plus absolute Documents paths,
# and the one line worth keeping (the PCSX2 version) goes in the README.
foreach ($f in $Expect + @('log.txt')) {
    $src = Join-Path $Fixture "bin/$f"
    if (Test-Path -LiteralPath $src) { Copy-Item -Force $src (Join-Path $Out $f) }
}
if (Test-Path -LiteralPath (Join-Path $Fixture 'ARM.json')) {
    Copy-Item -Force (Join-Path $Fixture 'ARM.json') (Join-Path $Out 'ARM.json')
}
Write-Output "Collected into $Out"
Get-ChildItem -LiteralPath $Out | Select-Object Name, Length
