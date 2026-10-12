param(
    [Parameter(Mandatory=$true)][string]$BinDir,
    [string]$Ip = '192.168.100.150',
    [string]$ElfName = 'vehicle-playground.elf',
    [int]$OwnedClientId = 0
)
$ErrorActionPreference = 'Stop'
$bin = (Resolve-Path -LiteralPath $BinDir).Path
$elf = Join-Path $bin $ElfName
if (-not (Test-Path -LiteralPath $elf -PathType Leaf)) { throw "Missing ELF: $elf" }
$client = (Resolve-Path (Join-Path $PSScriptRoot 'ps2client/bin/ps2client.exe')).Path
$out = Split-Path $bin -Parent
$recordPath = Join-Path $out 'editor-deploy.json'
if (Test-Path -LiteralPath $recordPath) { throw 'Use a fresh staging directory; do not overwrite deployment evidence.' }
$clients = @(Get-CimInstance Win32_Process -Filter "Name='ps2client.exe'")
foreach ($proc in $clients) {
    if ($proc.ProcessId -ne $OwnedClientId) {
        throw "Refusing a live unowned ps2client PID $($proc.ProcessId): $($proc.CommandLine)"
    }
}
if ($OwnedClientId -ne 0 -and $clients.Count -gt 0) {
    Stop-Process -Id $OwnedClientId
    Wait-Process -Id $OwnedClientId -ErrorAction SilentlyContinue
}
$resetOut = Join-Path $out 'reset-listen.log'
$listener = Start-Process -FilePath $client -ArgumentList @('-h',$Ip,'listen') -WindowStyle Hidden -PassThru -RedirectStandardOutput $resetOut -RedirectStandardError (Join-Path $out 'reset-listen.stderr.log')
try {
    $reset = Start-Process -FilePath $client -ArgumentList @('-h',$Ip,'-t','10','reset') -WindowStyle Hidden -PassThru -Wait -RedirectStandardOutput (Join-Path $out 'reset.log') -RedirectStandardError (Join-Path $out 'reset.stderr.log')
    if ($reset.ExitCode -ne 0) { throw "ps2link reset failed: $($reset.ExitCode)" }
    for ($i=0; $i -lt 8; $i++) {
        if ((Get-Item -LiteralPath $resetOut).Length -gt 0) { break }
        Start-Sleep -Milliseconds 250
    }
} finally {
    if (-not $listener.HasExited) { Stop-Process -Id $listener.Id; Wait-Process -Id $listener.Id -ErrorAction SilentlyContinue }
}
# Runner::deployToPs2 and stageReplayChannel, without regenerating the fixture.
foreach ($name in @('log.txt','livedbg.bin','livedbg.cmd','frame.tga','livelogic.bin','livetime.bin','livetime.rst','livepad.bin','replay.in','replay.arm','replay.out','replay.stop','replay.st')) {
    $path = Join-Path $bin $name
    if (Test-Path -LiteralPath $path) { Remove-Item -LiteralPath $path }
}
[IO.File]::WriteAllText((Join-Path $bin 'ps2link.run'), "deployed by TyraX`n", [Text.Encoding]::ASCII)
$pins = [ordered]@{}
Get-ChildItem -LiteralPath $bin -File -Recurse | Sort-Object FullName | ForEach-Object {
    $pins[$_.FullName.Substring($bin.Length+1).Replace('\','/')] = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLower()
}
$pins | ConvertTo-Json -Depth 3 | Set-Content -LiteralPath (Join-Path $out 'launch-inputs.json')
for ($i=0; $i -lt 40; $i++) { Start-Sleep -Milliseconds 250 }
$log = Join-Path $out 'console.log'
$args = @('-h',$Ip,'execee',"host:$ElfName",'-ps2link')
$game = Start-Process -FilePath $client -ArgumentList $args -WorkingDirectory $bin -WindowStyle Hidden -PassThru -RedirectStandardOutput $log -RedirectStandardError (Join-Path $out 'console.stderr.log')
$status = 'unconfirmed-file-server-kept-alive'
for ($waited=0; $waited -lt 15000; $waited+=250) {
    if ($game.HasExited) { $status='client-exited'; break }
    if ((Get-Item -LiteralPath $log).Length -gt 0) { $status='console-output-received'; break }
    Start-Sleep -Milliseconds 250
}
$record = [ordered]@{pid=$game.Id;status=$status;bin=$bin;args=$args;elfSha256=(Get-FileHash -LiteralPath $elf -Algorithm SHA256).Hash.ToLower();runner='src/runner.cpp::resetPs2Link/deployToPs2';time=(Get-Date).ToString('o')}
$record | ConvertTo-Json -Depth 3 | Set-Content -LiteralPath $recordPath
$record | ConvertTo-Json -Depth 3
if ($status -eq 'client-exited') { throw 'ps2client exited during startup' }
