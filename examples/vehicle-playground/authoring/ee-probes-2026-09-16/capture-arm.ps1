# Capture the GARAGE-DAY frame of one arm on the physical PS2, for the
# byte-identical-picture gate.
#
# The benchmark sampler holds pose 3 (outer night) after frame 1440 unless
# bin/district-benchmark-pose.txt asks for another, and it only reads that file
# AFTER the measurement window - so the capture cannot disturb the samples.
# Garage day is pose 0; the night poses TWINKLE and are never byte-identical,
# so nothing may be read from a repeat there.
#
# The picture comes from the game itself (devkit livedbg bit 6 -> bin/frame.tga,
# which --capture-frame drives and converts), not from a window grab.
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$Arm,
    [int]$Pose = 0,
    [string]$Root = 'D:/tyra-eeprobe-0916',
    [int]$TimeoutSeconds = 300
)
$ErrorActionPreference = 'Stop'
$host_ = '192.168.100.150'
$client = 'D:/tyra-editor/tools/ps2client/bin/ps2client.exe'
$fixture = Join-Path $Root "arms/$Arm"
$bin = Join-Path $fixture 'bin'
$out = Join-Path $Root "captures"
New-Item -ItemType Directory -Force -Path $out | Out-Null

foreach ($stale in 'frame-cost.csv', 'frame-attrib.csv', 'frame.tga', 'district-benchmark-pose.txt') {
    Remove-Item -LiteralPath (Join-Path $bin $stale) -ErrorAction SilentlyContinue
}
Set-Content -LiteralPath (Join-Path $bin 'ps2link.run') -Value 'ps2link' -Encoding ascii

& $client -h $host_ -t 10 reset *> (Join-Path $out "$Arm-reset.log")
if ($LASTEXITCODE -ne 0) { throw 'Reset command failed' }
Start-Sleep -Seconds 3
$proc = Start-Process -FilePath $client `
    -ArgumentList '-h', $host_, 'execee', 'host:vehicle-playground.elf', '-ps2link' `
    -WorkingDirectory $bin -WindowStyle Hidden -PassThru `
    -RedirectStandardOutput (Join-Path $out "$Arm-host.log") `
    -RedirectStandardError (Join-Path $out "$Arm-host.err")
try {
    # The pose file is only consulted past frame 1440, so write it early and
    # wait for the run to reach the held phase.
    Set-Content -LiteralPath (Join-Path $bin 'district-benchmark-pose.txt') -Value "$Pose" -Encoding ascii
    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    while ((Get-Date) -lt $deadline) {
        if (Test-Path -LiteralPath (Join-Path $bin 'frame-cost.csv')) { break }
        Start-Sleep -Seconds 3
    }
    if (!(Test-Path -LiteralPath (Join-Path $bin 'frame-cost.csv'))) { throw "arm $Arm never reached frame 1440" }
    Start-Sleep -Seconds 8   # let the held pose settle before asking for a frame
    & D:/tyra-editor/build/tyrax-editor.exe --capture-frame $fixture -o (Join-Path $out "$Arm-pose$Pose.png") --timeout 90
    if ($LASTEXITCODE -ne 0) { throw "capture-frame failed for $Arm" }
    Write-Output "captured $out\$Arm-pose$Pose.png"
}
finally { Stop-Process -Id $proc.Id -ErrorAction SilentlyContinue }
