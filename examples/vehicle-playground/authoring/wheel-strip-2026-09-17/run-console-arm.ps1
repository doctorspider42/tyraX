# Boot one TIMING arm on the PHYSICAL PS2 and collect its per-frame CSVs.
#
# Milliseconds only come from here. PCSX2 emulates no EE data cache, so its
# counters are exact and its timings are not; this round's package counts were
# taken there and every `ms` on this page was taken on 192.168.100.150.
#
# THE CONSOLE IS A SHARED RESOURCE AND ONE `ps2client` OWNS IT.
# Check who holds it before running this - `Get-CimInstance Win32_Process` for
# a live `ps2client` command line, not just "is the port listening", because
# tcp/18193 listening means ps2link is up and says nothing about whose session
# it is serving (docs/ps2link-setup.md).
#
# Two rules that each cost a run somewhere in this repo's history:
#   - the RESIDENT-IOP MARKER (`bin/ps2link.run` containing `ps2link`) must
#     exist and be verified BEFORE execee. Without it the launch resets the IOP,
#     takes the host filesystem with it, and can need a physical reboot;
#     `-ps2link` on execee alone is not sufficient with this crt0.
#   - the second `ps2client` IS the file server for the whole session. It must
#     stay alive for as long as the game runs, with its working directory set
#     to the project's `bin/`, or the game loses `host:` mid-run and the CSVs
#     never appear.
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$Fixture,
    [Parameter(Mandatory = $true)][string]$Out,
    [string]$Ps2Ip = '192.168.100.150',
    # The fixture writes after 1440 updates: 120 warm-up + 240 recorded frames
    # in each of four parked poses. On hardware that is around two minutes of
    # wall clock plus the boot and the asset load over host:.
    [int]$TimeoutSec = 900,
    [string[]]$Expect = @('frame-cost.csv', 'district-benchmark.csv')
)
$ErrorActionPreference = 'Stop'

$bin = Join-Path $Fixture 'bin'
$elf = Join-Path $bin 'vehicle-playground.elf'
if (!(Test-Path -LiteralPath $elf)) { throw "No ELF at $elf" }
New-Item -ItemType Directory -Force -Path $Out | Out-Null

# Use the MAIN CHECKOUT's ps2client: a worktree path is a different binary to
# Windows Firewall and pops a prompt nobody is watching.
$ps2client = 'D:/tyra-editor/tools/ps2client/bin/ps2client.exe'
if (!(Test-Path -LiteralPath $ps2client)) { throw "No ps2client at $ps2client" }

# A previous arm's artefacts must not be mistaken for this one's.
foreach ($f in $Expect + @('log.txt', 'livedbg.cmd', 'livedbg.bin', 'frame.tga',
                           'frame-attrib.csv', 'district-benchmark-pose.txt')) {
    Remove-Item -LiteralPath (Join-Path $bin $f) -Force -ErrorAction SilentlyContinue
}

# The resident-IOP marker, written and then VERIFIED by absolute path.
$marker = Join-Path $bin 'ps2link.run'
Set-Content -LiteralPath $marker -Value 'ps2link' -NoNewline -ErrorAction Stop
if (!(Test-Path -LiteralPath $marker)) { throw "Could not write $marker" }
Write-Output "marker ok: $marker"

# Reset ps2link, then hand it the ELF. Never chain these: a failed marker write
# must not reach execee.
& $ps2client -h $Ps2Ip -t 10 reset
Start-Sleep -Seconds 4

$prev = Get-Location
Set-Location -LiteralPath $bin      # host: maps to THIS directory
try {
    $srv = Start-Process -FilePath $ps2client -PassThru -WorkingDirectory $bin `
           -ArgumentList '-h', $Ps2Ip, 'execee', 'host:vehicle-playground.elf', '-ps2link' `
           -RedirectStandardOutput (Join-Path $Out 'ps2client-stdout.txt') `
           -RedirectStandardError  (Join-Path $Out 'ps2client-stderr.txt')
    Write-Output "ps2client pid $($srv.Id) serving $bin to $Ps2Ip"

    $deadline = (Get-Date).AddSeconds($TimeoutSec)
    $done = $false
    while ((Get-Date) -lt $deadline) {
        Start-Sleep -Seconds 5
        if ($srv.HasExited) { throw "ps2client exited early (code $($srv.ExitCode)) - the file server is gone" }
        $have = $Expect | Where-Object { Test-Path -LiteralPath (Join-Path $bin $_) }
        if ($have.Count -eq $Expect.Count) { Start-Sleep -Seconds 5; $done = $true; break }
    }
    if (-not $done) { throw "Timed out waiting for $($Expect -join ', ')" }
} finally {
    Set-Location -LiteralPath $prev
    # Stop ONLY the server this script started, by handle. Never
    # `taskkill /IM ps2client` - that is machine-wide and reaps other sessions.
    if ($srv -and -not $srv.HasExited) { Stop-Process -Id $srv.Id -Force -ErrorAction SilentlyContinue }
}

foreach ($f in $Expect + @('log.txt')) {
    $src = Join-Path $bin $f
    if (Test-Path -LiteralPath $src) { Copy-Item -Force $src (Join-Path $Out $f) }
}
if (Test-Path -LiteralPath (Join-Path $Fixture 'ARM.json')) {
    Copy-Item -Force (Join-Path $Fixture 'ARM.json') (Join-Path $Out 'ARM.json')
}
Write-Output "collected into $Out"
Get-ChildItem -LiteralPath $Out | Select-Object Name, Length
