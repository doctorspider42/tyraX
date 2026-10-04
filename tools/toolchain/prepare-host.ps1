# Checks or explicitly installs the host packages required inside the default
# WSL distribution. The Linux script owns the package list on both platforms.
[CmdletBinding()]
param([switch]$Install)
$ErrorActionPreference = 'Stop'

if (-not (Get-Command wsl.exe -ErrorAction SilentlyContinue)) {
    throw 'WSL 2 is required for native PS2 builds. Install Ubuntu with: wsl --install -d Ubuntu'
}

$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$probe = & wsl.exe --exec true 2>&1
if ($LASTEXITCODE -ne 0) {
    throw "No ready default WSL distribution was found. Install/start Ubuntu first: wsl --install -d Ubuntu`n$probe"
}

$script = (& wsl.exe --exec wslpath -a (Join-Path $here 'prepare-host.sh').Replace('\', '/') 2>&1).Trim()
if ($LASTEXITCODE -ne 0 -or -not $script) {
    throw "Could not translate the TyraX host-setup script into WSL: $script"
}

$mode = if ($Install) { '--install' } else { '--check' }
& wsl.exe --exec bash $script $mode
if ($LASTEXITCODE -ne 0) { throw "TyraX WSL host setup failed ($LASTEXITCODE)" }
