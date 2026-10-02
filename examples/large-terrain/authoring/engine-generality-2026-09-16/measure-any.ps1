# Build ONE generated project against ONE engine tree and measure it on the
# physical console. Project-agnostic twin of measure-round.ps1, which is wired
# to the Motor District fixture and its asset manifest.
param(
    [Parameter(Mandatory = $true)][string]$Arm,
    [Parameter(Mandatory = $true)][string]$Project,
    [Parameter(Mandatory = $true)][string]$Engine,
    [Parameter(Mandatory = $true)][string]$Cache,
    [Parameter(Mandatory = $true)][string]$Elf,
    [switch]$SkipBuild
)
$ErrorActionPreference = 'Stop'
$root = 'D:/tyra-flush-0915'
$bin = Join-Path $Project 'bin'
$out = Join-Path $root "results/$Arm"
if (Test-Path -LiteralPath $out) { throw "Archive $out before re-running this arm" }

if (-not $SkipBuild) {
    Write-Output "== building $Arm from $Engine"
    $ErrorActionPreference = 'Continue'
    & powershell.exe -NoProfile -ExecutionPolicy Bypass -File D:/tyra-editor/tools/toolchain/native-build.ps1 `
        -Project $Project -Engine $Engine -Cache $Cache `
        -Toolchain "$env:LOCALAPPDATA\tyra-editor\toolchain\ps2dev" 2>&1 |
        Select-String -Pattern 'rebuilding|Native build complete|error:' | ForEach-Object { $_.Line }
    $ErrorActionPreference = 'Stop'
    $elfPath = Join-Path $bin $Elf
    if (!(Test-Path -LiteralPath $elfPath)) { throw "Build produced no $Elf" }
    $age = (Get-Date) - (Get-Item -LiteralPath $elfPath).LastWriteTime
    if ($age.TotalMinutes -gt 15) { throw "ELF is $([int]$age.TotalMinutes) min old - the build did not relink" }
}

Get-CimInstance Win32_Process -Filter "Name='ps2client.exe'" | ForEach-Object {
    if ($_.CommandLine -match '192\.168\.100\.150') {
        Write-Output "stopping previous file server $($_.ProcessId)"
        Stop-Process -Id $_.ProcessId -Force
    }
}
Start-Sleep -Seconds 2
New-Item -ItemType Directory -Force -Path $out | Out-Null
Remove-Item -LiteralPath (Join-Path $bin 'frame-cost.csv') -ErrorAction SilentlyContinue
Set-Content -LiteralPath (Join-Path $bin 'ps2link.run') -Value 'ps2link' -Encoding ascii
if ((Get-Content -LiteralPath (Join-Path $bin 'ps2link.run') -Raw).Trim() -ne 'ps2link') { throw 'Invalid marker' }

Push-Location -LiteralPath $bin
try {
    & D:/tyra-editor/tools/ps2client/bin/ps2client.exe -h 192.168.100.150 -t 10 reset *> (Join-Path $out 'reset.log')
    if ($LASTEXITCODE -ne 0) { throw 'Reset command failed' }
    Start-Sleep -Seconds 3
    $proc = Start-Process -FilePath D:/tyra-editor/tools/ps2client/bin/ps2client.exe `
        -ArgumentList '-h', '192.168.100.150', 'execee', "host:$Elf", '-ps2link' `
        -WorkingDirectory $bin -WindowStyle Hidden -PassThru `
        -RedirectStandardOutput (Join-Path $out 'host.log') -RedirectStandardError (Join-Path $out 'host.err')
    [pscustomobject]@{
        arm = $Arm; project = $Project; engine = $Engine; pid = $proc.Id
        started = (Get-Date).ToString('o')
        elf = (Get-FileHash -LiteralPath (Join-Path $bin $Elf)).Hash
    } | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $out 'run.json')
    Write-Output "Launched $Arm, ps2client PID $($proc.Id)"
}
finally { Pop-Location }

Write-Output "== waiting for bin/frame-cost.csv to finish"
$csv = Join-Path $bin 'frame-cost.csv'
$prev = -1
for ($i = 0; $i -lt 90; $i++) {
    Start-Sleep -Seconds 5
    $size = if (Test-Path -LiteralPath $csv) { (Get-Item -LiteralPath $csv).Length } else { 0 }
    if ($size -gt 2000 -and $size -eq $prev) {
        Copy-Item -LiteralPath $csv -Destination $out
        Write-Output "== $Arm done: $size bytes"
        exit 0
    }
    $prev = $size
}
throw "No complete frame-cost.csv after 450 s - check $out\host.log"
