# Windows bridge for native-build.sh. Continuous vehicle WAV loop intent comes
# from inc/vehicle_sound_loops.gen.txt in the shared Windows/Linux backend.
# Runner prepares mono PCM16/22050 WAVs in .res-baked/sfx before this bridge;
# native-build.sh encodes that mirror and preserves the res/sfx originals.
# Windows owns the authored project and installed toolchain. The shared backend
# mirrors build inputs to WSL's Linux filesystem and syncs bin/ back on success.
# TYRAX_NATIVE_DIRECT=1 bypasses the mirror for A/B measurements.
# The shared backend preserves Makefile timestamps and checks the real engine
# archive on every build; unchanged games skip the link on both platforms.
[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$Project,
    [Parameter(Mandatory=$true)][string]$Engine,
    [Parameter(Mandatory=$true)][string]$Cache,
    [Parameter(Mandatory=$true)][string]$Toolchain,
    [switch]$PrepareHost,
    [switch]$Rebuild
)
$ErrorActionPreference = 'Stop'
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
if ($PrepareHost) {
    & (Join-Path $here 'setup.ps1') -Root $Toolchain -InstallHostDependencies
}
function WslPath([string]$Path) {
    $resolved = [IO.Path]::GetFullPath($Path)
    $out = (wsl.exe wslpath -a $resolved.Replace('\', '/')).Trim()
    if (-not $out) { throw "Could not translate path for WSL: $resolved" }
    return $out
}
# Docker can leave bin/ unwritable to WSL. Clean that generated tree with
# Windows cmdlets before the backend syncs its Linux outputs back into it.
$projectRoot = [IO.Path]::GetFullPath($Project).TrimEnd('\', '/')
$projectBin = [IO.Path]::GetFullPath((Join-Path $projectRoot 'bin'))
if (-not $projectBin.StartsWith($projectRoot + [IO.Path]::DirectorySeparatorChar,
        [StringComparison]::OrdinalIgnoreCase)) { throw 'Unsafe generated output path.' }
if (Test-Path -LiteralPath $projectBin -PathType Container) {
    wsl.exe --exec test -w (WslPath $projectBin)
    if ($LASTEXITCODE -ne 0) {
        Write-Host '[editor] bin is owned by an old Docker build - removing it via Windows...'
        $ignorePath = Join-Path $projectBin '.gitignore'
        $hadIgnore = Test-Path -LiteralPath $ignorePath
        $ignoreBytes = [byte[]]@()
        if ($hadIgnore) { $ignoreBytes = [IO.File]::ReadAllBytes($ignorePath) }
        Remove-Item -LiteralPath $projectBin -Recurse -Force
        New-Item -ItemType Directory -Path $projectBin -Force | Out-Null
        if ($hadIgnore) { [IO.File]::WriteAllBytes($ignorePath, $ignoreBytes) }
    }
}
$argsForBash = @(
    (WslPath (Join-Path $here 'native-build.sh')),
    (WslPath $Project), (WslPath $Engine), (WslPath $Cache),
    (WslPath $Toolchain), $(if ($Rebuild) { '1' } else { '0' })
)
wsl.exe --exec bash @argsForBash
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
