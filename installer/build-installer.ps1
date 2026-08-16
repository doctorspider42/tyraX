# Packages a built editor into dist\TyraX-Setup-<version>.exe (Inno Setup 7).
#
# Usage:
#   ./installer/build-installer.ps1              # build the editor, then package
#   ./installer/build-installer.ps1 -SkipBuild   # package what is in build/ already
#   ./installer/build-installer.ps1 -Version 1.2.3
#
# THE VERSION COMES FROM src/version.hpp and from nowhere else. The release
# workflow reads the same three macros with the same regexes (see
# .github/workflows/release.yml), so "which version is this" has one answer on a
# developer's machine and in CI.
#
# There is deliberately no build-installer.sh twin: this packages a Windows
# installer with a Windows-only tool. Linux packaging is its own job and is in
# docs/backlog.md - do not "restore parity" by adding a stub that cannot work.
param(
    [string]$Version,
    [switch]$SkipBuild,
    [string]$OutDir
)

$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path "$PSScriptRoot\..").Path
Set-Location $repo

function Get-EditorVersion {
    $header = Get-Content "$repo\src\version.hpp" -Raw
    $parts = foreach ($name in 'MAJOR', 'MINOR', 'PATCH') {
        $m = [regex]::Match($header, "(?m)^#define\s+TYRAX_VERSION_$name\s+(\d+)\s*$")
        if (-not $m.Success) { throw "src/version.hpp has no TYRAX_VERSION_$name" }
        $m.Groups[1].Value
    }
    $parts -join '.'
}

function Find-ISCC {
    $cmd = Get-Command iscc.exe -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
    # Newest first, so a machine with 6 and 7 side by side still gets 7 - which
    # is the ordinary case, since Inno Setup installs each major version in its
    # own folder and 7 does not replace 6. The LOCALAPPDATA\Programs root is
    # where a per-user install lands (what `winget install JRSoftware.InnoSetup`
    # does without elevation).
    $roots = @($env:ProgramFiles, ${env:ProgramFiles(x86)},
               (Join-Path $env:LOCALAPPDATA 'Programs'), $env:LOCALAPPDATA) |
        Where-Object { $_ }
    foreach ($v in 7, 6) {
        foreach ($root in $roots) {
            $p = Join-Path $root "Inno Setup $v\ISCC.exe"
            if (Test-Path $p) { return $p }
        }
    }
    throw @'
Inno Setup 7 not found. Install it from https://jrsoftware.org/isdl.php
(or: winget install JRSoftware.InnoSetup), then re-run this script.
'@
}

if (-not $Version) { $Version = Get-EditorVersion }
if ($Version -notmatch '^\d+\.\d+\.\d+$') {
    throw "Version must be x.y.z, got '$Version'"
}
if (-not $OutDir) { $OutDir = "$repo\dist" }

if (-not $SkipBuild) {
    Write-Host "== Building the editor ==" -ForegroundColor Cyan
    & "$repo\build.ps1"
}

$exe = "$repo\build\tyrax-editor.exe"
if (-not (Test-Path $exe)) {
    throw "No editor binary at $exe - run ./build.ps1 first (or drop -SkipBuild)."
}

$iscc = Find-ISCC
Write-Host "== Packaging TyraX $Version ==" -ForegroundColor Cyan
Write-Host "   ISCC: $iscc"
New-Item -ItemType Directory -Force $OutDir | Out-Null

& $iscc "/DAppVersion=$Version" "/DSourceDir=$repo" "/O$OutDir" "$PSScriptRoot\tyrax.iss"
if ($LASTEXITCODE -ne 0) { throw "ISCC failed with exit code $LASTEXITCODE" }

$setup = Join-Path $OutDir "TyraX-Setup-$Version.exe"
if (-not (Test-Path $setup)) { throw "ISCC reported success but $setup is missing" }
$mb = [math]::Round((Get-Item $setup).Length / 1MB, 1)
Write-Host "OK: $setup ($mb MB)" -ForegroundColor Green
