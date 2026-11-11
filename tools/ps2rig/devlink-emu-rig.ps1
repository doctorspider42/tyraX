# devlink emulator rig (docs/devlink.md): a portable PCSX2 copy that boots
# ps2link with DEV9 bridged onto the LAN, so a game deployed with
# `ps2client execee` runs under ps2link exactly like on a console - devlink
# included - without touching the real PS2.
#
#   tools/ps2rig/devlink-emu-rig.ps1 -GameBin <project>\bin -AdapterGuid {GUID} -Ip 192.168.100.160
#
# What it does:
#   1. copies the installed PCSX2 to %TEMP%\tyra-editor-test\pcsx2-rig once
#      (-portable: own inis/bios/logs, the user's config is never touched);
#   2. writes [DEV9/Eth] (EthEnable, EthApi=PCAP_Bridged, EthDevice=<bare GUID>)
#      - the section name and the BARE GUID are both load-bearing (PCSX2
#      ignores [DEV9] and prepends \Device\NPF_ itself);
#   3. puts ps2link.elf + IPCONFIG.DAT into the game's bin\ - host: is served
#      by PCSX2's HostFs relative to the -elf directory, not by ps2client;
#   4. boots it. Then: ps2client -h <Ip> execee host:<game>.elf
#
# Liveness: execee, never ping (ICMP loses to the IOP reboot window here).
param(
    [Parameter(Mandatory)] [string]$GameBin,
    [Parameter(Mandatory)] [string]$AdapterGuid,
    [string]$Ip = '192.168.100.160',
    [string]$Mask = '255.255.255.0',
    [string]$Gateway = '192.168.100.1',
    [string]$Pcsx2Dir = 'C:\Program Files\PCSX2',
    [string]$Ps2Link = ''
)

$repo = Resolve-Path (Join-Path $PSScriptRoot '..\..')
if (-not $Ps2Link) { $Ps2Link = Join-Path $repo 'tools\ps2link\ps2link.elf' }
if (-not (Test-Path $Ps2Link)) { throw "no ps2link ELF at $Ps2Link - run tools/ps2link/build.ps1" }

$rig = Join-Path $env:TEMP 'tyra-editor-test\pcsx2-rig'
if (-not (Test-Path (Join-Path $rig 'pcsx2-qt.exe'))) {
    New-Item -ItemType Directory -Force $rig | Out-Null
    Copy-Item -Recurse -Force (Join-Path $Pcsx2Dir '*') $rig
    New-Item -ItemType File -Force (Join-Path $rig 'portable.ini') | Out-Null
    # The BIOS is not copied by the install; take the user's.
    $userBios = Join-Path ([Environment]::GetFolderPath('MyDocuments')) 'PCSX2\bios'
    if (Test-Path $userBios) {
        New-Item -ItemType Directory -Force (Join-Path $rig 'bios') | Out-Null
        Copy-Item -Force (Join-Path $userBios '*') (Join-Path $rig 'bios')
    }
}

$ini = Join-Path $rig 'inis\PCSX2.ini'
New-Item -ItemType Directory -Force (Split-Path $ini) | Out-Null
# Start from the user's own settings (BIOS choice, finished setup wizard):
# without them a portable PCSX2 in -batch mode just exits, silently.
$userIni = Join-Path ([Environment]::GetFolderPath('MyDocuments')) 'PCSX2\inis\PCSX2.ini'
if (Test-Path $userIni) { Copy-Item -Force $userIni $ini }
$guid = $AdapterGuid.Trim()
if (-not $guid.StartsWith('{')) { $guid = '{' + $guid + '}' }
$lines = if (Test-Path $ini) { Get-Content $ini } else { @() }
$out = New-Object System.Collections.Generic.List[string]
$skip = $false
foreach ($l in $lines) {
    if ($l -match '^\[DEV9/Eth\]') { $skip = $true; continue }
    if ($skip -and $l -match '^\[') { $skip = $false }
    if (-not $skip) { $out.Add($l) }
}
$out.Add('[DEV9/Eth]')
$out.Add('EthEnable = true')
$out.Add('EthApi = PCAP_Bridged')
$out.Add("EthDevice = $guid")
$out.Add('InterceptDHCP = false')
Set-Content -Encoding utf8 $ini $out

Copy-Item -Force $Ps2Link (Join-Path $GameBin 'ps2link.elf')
$ipconf = Join-Path $GameBin 'IPCONFIG.DAT'
[IO.File]::WriteAllText($ipconf, "$Ip $Mask $Gateway")
if (-not (Test-Path $ipconf)) { throw "could not write $ipconf" }
# Without it ps2link boots at its compiled default 192.168.1.10 ("Using
# cached config") and is unreachable from this LAN.

$exe = Join-Path $rig 'pcsx2-qt.exe'
Start-Process $exe -ArgumentList @('-portable', '-batch', '-elf', (Join-Path $GameBin 'ps2link.elf')) -WorkingDirectory $rig
Write-Host "Booting ps2link at $Ip. Then: tools\ps2client\bin\ps2client.exe -h $Ip execee host:<game>.elf"
