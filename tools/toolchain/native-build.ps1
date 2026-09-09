# Windows bridge for native-build.sh. The game and cache stay on the Windows
# filesystem; only the compiler process runs in WSL, with no Docker daemon.
[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$Project,
    [Parameter(Mandatory=$true)][string]$Engine,
    [Parameter(Mandatory=$true)][string]$Cache,
    [Parameter(Mandatory=$true)][string]$Toolchain,
    [switch]$Rebuild
)
$ErrorActionPreference = 'Stop'
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
function WslPath([string]$Path) {
    $resolved = [IO.Path]::GetFullPath($Path)
    $out = (wsl.exe wslpath -a $resolved.Replace('\', '/')).Trim()
    if (-not $out) { throw "Could not translate path for WSL: $resolved" }
    return $out
}
$argsForBash = @(
    (WslPath (Join-Path $here 'native-build.sh')),
    (WslPath $Project), (WslPath $Engine), (WslPath $Cache),
    (WslPath $Toolchain), $(if ($Rebuild) { '1' } else { '0' })
)
wsl.exe --exec bash @argsForBash
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
