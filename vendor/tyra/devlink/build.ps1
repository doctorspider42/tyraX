# Builds devlink.irx (docs/devlink.md) and the engine's embedded copy of it.
# The Windows twin of build.sh; keep the two in step.
#
#   ./build.ps1            native toolchain (through WSL) when installed, else Docker
#   ./build.ps1 -Docker    force the pinned ps2dev/ps2dev image
#
# Uses PS2DEV v2.0.0 - the same toolchain tools/ps2link is built with, which is
# what makes this module's ps2ip imports match the ps2ip ps2link keeps resident.
# The artifacts are committed (bin/devlink.irx and
# vendor/tyra/engine/src/debug/devlink_irx.cpp): rebuild and commit them
# together with any change under iop/.
param([switch]$Docker)

$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$image = 'ps2dev/ps2dev:v2.0.0'
$toolchain = Join-Path $env:LOCALAPPDATA 'tyra-editor\toolchain\ps2dev'
$native = -not $Docker -and
    (Test-Path (Join-Path $toolchain 'iop\bin\mipsel-none-elf-gcc')) -and
    (Get-Command wsl.exe -ErrorAction SilentlyContinue)

function WslPath([string]$Path) {
    $out = (wsl.exe wslpath -a ($Path -replace '\\', '/')).Trim()
    if (-not $out) { throw "could not translate $Path for WSL" }
    return $out
}

if ($native) {
    Write-Host "== devlink: native toolchain ($toolchain, via WSL) =="
    $root = WslPath $toolchain
    $script = @(
        'set -e',
        "export PS2DEV='$root'",
        'export PS2SDK="$PS2DEV/ps2sdk"',
        'export PATH="$PS2DEV/bin:$PS2DEV/ee/bin:$PS2DEV/iop/bin:$PS2SDK/bin:$PATH"',
        "cd '$(WslPath $here)'",
        'bash ./inner-build.sh'
    ) -join "`n"
    wsl.exe --exec bash -c $script
} else {
    Write-Host "== devlink: Docker ($image) =="
    $mount = ((Split-Path -Parent $here) -replace '\\', '/') + ':/tyra'
    docker run --rm -v $mount -w /tyra/devlink $image sh -c 'apk add --no-cache make bash >/dev/null 2>&1 || true; bash ./inner-build.sh'
}
if ($LASTEXITCODE -ne 0) { throw "devlink build failed ($LASTEXITCODE)" }
Write-Host "== OK: $(Join-Path $here 'bin\devlink.irx') =="
