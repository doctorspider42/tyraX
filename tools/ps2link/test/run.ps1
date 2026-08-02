# Builds and runs the ps2link host: protocol harness (test_net_fio.c) with the
# PC's own gcc. The Windows twin of run.sh; keep the two in step.
#
# Needs tools/ps2link/build to exist (run tools/ps2link/build.ps1 once) - the
# harness compiles the real patched iop/net_fio.c out of that work tree.
#
#   ./run.ps1              # patched tree: expected to pass
#   ./run.ps1 -Pristine    # upstream file at the pinned commit: expected to FAIL
#
# The -Pristine run is the point of the harness: it is what shows the tests
# actually catch the bugs tyrax.patch fixes. See tools/ps2link/README.md.

param([switch]$Pristine)

$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$build = Join-Path (Split-Path -Parent $here) 'build'
$out = Join-Path $here 'harness.exe'

if (-not (Test-Path (Join-Path $build 'iop/net_fio.c'))) {
    throw "no ps2link work tree at $build - run tools/ps2link/build.ps1 first"
}

$srcDir = Join-Path $build 'iop'

if ($Pristine) {
    # Pull the untouched file straight out of git so the comparison cannot be
    # spoiled by leftover edits in build/.
    $srcDir = Join-Path $here 'pristine'
    New-Item -ItemType Directory -Force -Path $srcDir | Out-Null
    $commit = (Select-String -Path (Join-Path (Split-Path -Parent $here) 'build.ps1') `
                             -Pattern "^\`$commit = '([0-9a-f]+)'").Matches[0].Groups[1].Value
    git -C $build show "${commit}:iop/net_fio.c" | Set-Content -Encoding utf8 (Join-Path $srcDir 'net_fio.c')
    if ($LASTEXITCODE -ne 0) { throw "could not read iop/net_fio.c at $commit" }
    Copy-Item -Force (Join-Path $build 'iop/net_fio.h') $srcDir
    Write-Host "== Harness against PRISTINE upstream $($commit.Substring(0,10)) (expected to FAIL) =="
} else {
    Write-Host '== Harness against the patched tree (expected to pass) =='
}

& gcc -o $out (Join-Path $here 'test_net_fio.c') `
    -I (Join-Path $here 'shim') -I $srcDir -I (Join-Path $build 'include') -Wall
if ($LASTEXITCODE -ne 0) { throw "gcc failed (exit $LASTEXITCODE)" }

& $out
$rc = $LASTEXITCODE

if ($Pristine) {
    if ($rc -eq 0) { throw 'the pristine tree PASSED - the tests no longer catch the bugs' }
    Write-Host '== As expected: the unpatched tree fails =='
    exit 0
}

if ($rc -ne 0) { throw "harness failed (exit $rc)" }
Write-Host '== OK =='
