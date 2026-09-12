# Windows entry point for the Docker-free PS2 toolchain. PS2DEV's current
# Windows archive still needs an MSYS environment; TyraX instead uses the same
# pinned Linux bundle through WSL, avoiding a second subtly different toolchain.
[CmdletBinding()]
param(
    [string]$Root = "$env:LOCALAPPDATA\tyra-editor\toolchain\ps2dev",
    [switch]$InstallHostDependencies
)
$ErrorActionPreference = 'Stop'
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
try {
    if ($InstallHostDependencies) {
        & (Join-Path $here 'prepare-host.ps1') -Install
    } else {
        & (Join-Path $here 'prepare-host.ps1')
    }
} catch {
    if (-not $InstallHostDependencies) {
        $prepare = Join-Path $here 'prepare-host.ps1'
        throw "WSL host prerequisites are missing. Install them explicitly with:`n  powershell.exe -NoProfile -ExecutionPolicy Bypass -File `"$prepare`" -Install`n$($_.Exception.Message)"
    }
    throw
}
$script = (wsl.exe wslpath -a $here.Replace('\', '/')).Trim()
$target = (wsl.exe wslpath -a ([IO.Path]::GetFullPath($Root).Replace('\', '/'))).Trim()
if (-not $script -or -not $target) { throw 'Could not translate the toolchain path into WSL.' }
wsl.exe --exec bash "$script/setup.sh" $target
if ($LASTEXITCODE -ne 0) { throw "TyraX toolchain setup failed ($LASTEXITCODE)" }
