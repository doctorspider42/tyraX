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
# THE TWIN IS installer/build-package.sh, and it is a twin in purpose rather
# than line for line: this drives a Windows-only tool (Inno Setup) to produce
# one .exe, that one stages a tree and produces a .tar.gz, a .deb and an .rpm.
# What the two MUST keep in step is the CONTENT of the install - the same
# binary, engine, tools, VU sources, examples and licence files in the same
# exe-relative shape - so a new exe-relative lookup in the editor is an entry
# in tyrax.iss AND in build-package.sh's stage_tree, in the same commit.
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

# Which major an ISCC.exe is, or 0 when it will not say. The banner ("Inno
# Setup 7 Command-Line Compiler") goes to STDOUT while the usage text goes to
# stderr, and `/?` exits 1 - so capture stdout, parse it, and ignore the exit
# code. Do not add `2>&1` here: in Windows PowerShell that wraps a native
# command's stderr in ErrorRecords and turns this probe into a failure.
function Get-ISCCMajor([string]$path) {
    try { $out = & $path /? | Out-String } catch { return 0 }
    if ($out -match 'Inno Setup (\d+)') { return [int]$Matches[1] }
    return 0
}

function Find-ISCC {
    # VERSIONED FOLDERS FIRST, PATH LAST, AND VALIDATE WHAT COMES BACK. Both
    # halves of that were paid for by the first release run: chocolatey's
    # `innosetup` package is still on the 6.x line, so the CI runner had a
    # 6.7.1 `ISCC.exe` shim on PATH - this function returned it without
    # looking, and the build died inside ISPP on the .iss's own version guard
    # instead of here, where the message can say what to do about it. Inno
    # Setup installs each major in its own folder and 7 does not replace 6, so
    # a machine with both is the ordinary case; LOCALAPPDATA\Programs is where
    # a per-user install lands (winget without elevation).
    $roots = @($env:ProgramFiles, ${env:ProgramFiles(x86)},
               (Join-Path $env:LOCALAPPDATA 'Programs'), $env:LOCALAPPDATA) |
        Where-Object { $_ }
    $candidates = @()
    foreach ($v in 7, 6) {
        foreach ($root in $roots) { $candidates += (Join-Path $root "Inno Setup $v\ISCC.exe") }
    }
    $cmd = Get-Command iscc.exe -ErrorAction SilentlyContinue
    if ($cmd) { $candidates += $cmd.Source }

    $tooOld = @()
    foreach ($c in $candidates) {
        if (-not (Test-Path $c)) { continue }
        $major = Get-ISCCMajor $c
        if ($major -ge 7) { return $c }
        if ($major -gt 0) { $tooOld += "$c (Inno Setup $major)" }
    }
    $found = if ($tooOld) { "Found, but too old:`n  " + ($tooOld -join "`n  ") } else { 'None found.' }
    throw @"
Inno Setup 7 not found - installer/tyrax.iss needs it and refuses to compile
under 6. $found

Install it with:  winget install JRSoftware.InnoSetup.7
or from https://jrsoftware.org/isdl.php - note that chocolatey's `innosetup`
package is the 6.x line and will not do.
"@
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
