# Reset-cycle loop for the ps2link wedge (docs/ps2link-setup.md, "The reset
# wedge"): deploys the same project N times the way the editor's F6 does
# (ps2client reset, then execee - through `tyrax-editor --build --run-ps2`),
# and records per cycle the game's `KHEAD` line (EE kernel semaphores and
# threads still free at boot, logged by Tyra::DevLink::start under ps2link)
# and whether the boot wedged (`freepad: DMA Busy`, or no boot at all).
#
#   tools/ps2rig/reset-cycle.ps1 -Project <dir> -Ip 192.168.100.160 -Cycles 20
#
# Point it at the emulator rig (tools/ps2rig/devlink-emu-rig.ps1) first; on a
# real console every wedge costs a power cycle. ONE ps2client per PC at a
# time - every ps2client listens on the same tty port.
param(
    [Parameter(Mandatory)] [string]$Project,
    [Parameter(Mandatory)] [string]$Ip,
    [int]$Cycles = 20,
    [int]$BootSeconds = 45,
    [string]$Editor = ''
)

$repo = Resolve-Path (Join-Path $PSScriptRoot '..\..')
if (-not $Editor) { $Editor = Join-Path $repo 'build-dev\tyrax-editor.exe' }
$logDir = Join-Path $env:TEMP 'tyra-editor-test\reset-cycle'
New-Item -ItemType Directory -Force $logDir | Out-Null
$client = Join-Path $repo 'tools\ps2client\bin\ps2client.exe'

function Stop-Ours {
    Get-Process tyrax-editor, ps2client -ErrorAction SilentlyContinue |
        Where-Object { $_.Path -like "$repo*" } |
        ForEach-Object { Stop-Process -Id $_.Id -Force -ErrorAction SilentlyContinue }
}

$rows = @()
for ($i = 1; $i -le $Cycles; $i++) {
    Stop-Ours
    $log = Join-Path $logDir ("cycle{0:d2}.log" -f $i)
    Remove-Item -ErrorAction SilentlyContinue $log
    $p = Start-Process -FilePath $Editor -ArgumentList @('--build', $Project, '--run-ps2', $Ip) `
        -RedirectStandardOutput $log -PassThru -WindowStyle Hidden
    $deadline = (Get-Date).AddSeconds($BootSeconds + 60)
    $khead = ''; $state = 'no-boot'
    while ((Get-Date) -lt $deadline) {
        Start-Sleep -Milliseconds 500
        if (-not (Test-Path $log)) { continue }
        $t = Get-Content $log -Raw -ErrorAction SilentlyContinue
        if (-not $t) { continue }
        if ($t -match 'DMA Busy') { $state = 'WEDGED (DMA Busy)'; break }
        if ($t -match 'BUILD FAILED|Build failed') { $state = 'build failed'; break }
        $m = [regex]::Match($t, 'KHEAD free semaphores (\d+) threads (\d+)')
        if ($m.Success) { $khead = "sema $($m.Groups[1].Value) threads $($m.Groups[2].Value)"; $state = 'booted'; Start-Sleep 3; break }
    }
    $rows += [pscustomobject]@{ Cycle = $i; State = $state; Headroom = $khead }
    Write-Host ("cycle {0,2}: {1,-20} {2}" -f $i, $state, $khead)
    if ($state -ne 'booted') { break }
}
Stop-Ours
$rows | Format-Table -AutoSize | Out-String | Write-Host
