# Windows entry point for the Docker-free PS2 toolchain. PS2DEV's current
# Windows archive still needs an MSYS environment; TyraX instead uses the same
# pinned Linux bundle through WSL, avoiding a second subtly different toolchain.
[CmdletBinding()]
param([string]$Root = "$env:LOCALAPPDATA\tyra-editor\toolchain\ps2dev")
$ErrorActionPreference = 'Stop'
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
if (-not (Get-Command wsl.exe -ErrorAction SilentlyContinue)) {
    throw 'WSL 2 is required for native PS2 builds on Windows. Install Ubuntu with: wsl --install -d Ubuntu'
}
$script = (wsl.exe wslpath -a $here.Replace('\', '/')).Trim()
$target = (wsl.exe wslpath -a ([IO.Path]::GetFullPath($Root).Replace('\', '/'))).Trim()
if (-not $script -or -not $target) { throw 'Could not translate the toolchain path into WSL.' }
wsl.exe --exec bash "$script/setup.sh" $target
if ($LASTEXITCODE -ne 0) { throw "TyraX toolchain setup failed ($LASTEXITCODE)" }
