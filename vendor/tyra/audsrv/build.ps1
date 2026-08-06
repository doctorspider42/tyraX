# Builds the TyraX audsrv - the audio server every generated game links and
# loads. The Windows twin of build.sh; keep the two in step.
#
# Reproducible: fetches a PINNED ps2sdk into .work\ (gitignored), overlays the
# sources next to this script over its iop/sound/audsrv + ee/rpc/audsrv, builds
# inside the project's own toolchain image, and drops the three artifacts into
# bin\. Needs Docker Desktop running and network for the first fetch.
#
#   .\build.ps1           # -> bin\audsrv.irx, bin\libaudsrv.a, bin\audsrv.h
#   .\build.ps1 -Clean    # drop the work tree first (forces a fresh fetch)
#   .\build.ps1 -Check    # build, then DIFF against the committed bin\ and
#                         # restore it - proves the sources produce what ships
#
# See README.md next to this script for why the work tree exists, why the
# artifacts are committed, and for the licence position (audsrv is LGPL v2,
# unlike the rest of PS2SDK).
[CmdletBinding()]
param([switch]$Clean, [switch]$Check)

$ErrorActionPreference = 'Stop'

# The ps2sdk commit our sources were taken from: 00f199ae~1, the last one
# before the build system started requiring srxfixup / a newer toolchain than
# the image has. Do not bump it without rebuilding and re-testing a game.
$Ps2sdkCommit = 'e78a9cb2ea816a72a7466000c51558fd2b57f5a7'
$Ps2sdkUrl    = 'https://github.com/ps2dev/ps2sdk.git'
# ...and our fork of it, tried when the upstream fetch fails - the same
# arrangement (and the same naming) deps.ps1 uses for every vendored
# dependency. This module's SOURCES are in this directory, so an upstream
# that vanishes cannot take audsrv with it; what it would take is the SDK
# TREE this builds against (makefiles, headers, the imports/exports
# tooling). A plain GitHub fork serves arbitrary reachable SHAs, which is
# why the mirror needs no tyrax-specific tag.
$Ps2sdkMirror = 'https://github.com/doctorspider42/tyrax-vendor-ps2sdk.git'
$Image        = 'h4570/tyra'

# The container path the sources are built at. It is part of the output: the EE
# objects keep debug info, so the build path ends up inside libaudsrv.a. Keeping
# it fixed is what makes two builds on two machines comparable.
$BuildPath = '/tmp/pf'

$here = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $here
$work = Join-Path $here '.work\ps2sdk'

if ($Clean -and (Test-Path (Join-Path $here '.work'))) {
    Write-Host '[audsrv] dropping the work tree...'
    # The container builds as root; on Docker Desktop the bind mount is mapped
    # for us, but go through the container anyway so both twins behave alike.
    docker run --rm -v "$(Join-Path $here '.work'):/w" $Image sh -c 'rm -rf /w/*' 2>&1 | Out-Null
    Remove-Item -Recurse -Force (Join-Path $here '.work') -ErrorAction SilentlyContinue
}

if (-not (Test-Path (Join-Path $work '.git'))) {
    Write-Host "[audsrv] fetching ps2sdk @ $($Ps2sdkCommit.Substring(0,8))..."
    New-Item -ItemType Directory -Force -Path $work | Out-Null
    git -C $work init -q
    $fetched = $false
    foreach ($url in @($Ps2sdkUrl, $Ps2sdkMirror)) {
        # A failed fetch prints to stderr, and Windows PowerShell turns native
        # stderr into a TERMINATING error while $ErrorActionPreference is
        # 'Stop' - so the fallback would never be reached (setup.ps1 carries
        # the same note next to Invoke-Native). Judge git by its exit code.
        $prev = $ErrorActionPreference
        $ErrorActionPreference = 'Continue'
        try { git -C $work fetch -q --depth 1 $url $Ps2sdkCommit 2>$null | Out-Null }
        finally { $ErrorActionPreference = $prev }
        if ($LASTEXITCODE -eq 0) { $fetched = $true; break }
        Write-Host "  ...$url did not serve $Ps2sdkCommit, trying the next remote" -ForegroundColor Yellow
    }
    if (-not $fetched) {
        # Leave nothing half-initialised: an empty .git would make the next run
        # skip the fetch entirely and fail much later, inside the container.
        Remove-Item -Recurse -Force (Join-Path $here '.work') -ErrorAction SilentlyContinue
        throw "Could not fetch ps2sdk $Ps2sdkCommit from either remote"
    }
    git -C $work checkout -q FETCH_HEAD
}

Write-Host '[audsrv] overlaying the TyraX sources...'
# Copy, do not link: the build runs in a container where a host link pointing
# outside the mount resolves to nothing. The removal goes through the container
# so both twins behave alike (on Linux a previous build leaves root-owned
# obj/irx/lib directories behind).
docker run --rm -v "${work}:/w" $Image sh -c 'rm -rf /w/iop/sound/audsrv /w/ee/rpc/audsrv' | Out-Null
foreach ($pair in @(@('iop', 'iop\sound\audsrv'), @('ee', 'ee\rpc\audsrv'))) {
    $dst = Join-Path $work $pair[1]
    Remove-Item -Recurse -Force $dst -ErrorAction SilentlyContinue
    New-Item -ItemType Directory -Force -Path $dst | Out-Null
    Copy-Item -Recurse -Force (Join-Path $here ($pair[0] + '\*')) $dst
}

Write-Host "[audsrv] building (Docker: $Image)..."
$script = @"
set -e
export PS2SDKSRC=$BuildPath
cd $BuildPath/iop/sound/audsrv && rm -rf obj irx && make CC=gcc
cd $BuildPath/ee/rpc/audsrv   && rm -rf obj lib && make CC=gcc
"@
docker run --rm -v "${work}:$BuildPath" -w $BuildPath $Image sh -c $script
if ($LASTEXITCODE -ne 0) { throw 'audsrv build failed' }

$out = Join-Path $here 'bin'
if ($Check) {
    $out = Join-Path $here '.work\out'
    Remove-Item -Recurse -Force $out -ErrorAction SilentlyContinue
}
New-Item -ItemType Directory -Force -Path $out | Out-Null
Copy-Item -Force (Join-Path $work 'iop\sound\audsrv\irx\audsrv.irx')  (Join-Path $out 'audsrv.irx')
Copy-Item -Force (Join-Path $work 'ee\rpc\audsrv\lib\libaudsrv.a')    (Join-Path $out 'libaudsrv.a')
Copy-Item -Force (Join-Path $work 'ee\rpc\audsrv\include\audsrv.h')   (Join-Path $out 'audsrv.h')

if ($Check) {
    Write-Host '[audsrv] comparing against the committed bin\ ...'
    $rc = 0
    foreach ($f in @('audsrv.irx', 'audsrv.h')) {
        $a = Get-FileHash (Join-Path $out $f) -Algorithm SHA256
        $b = Get-FileHash (Join-Path $here "bin\$f") -Algorithm SHA256
        if ($a.Hash -eq $b.Hash) { Write-Host "  $f byte-identical" }
        else { Write-Host "  $f DIFFERS"; $rc = 1 }
    }
    # libaudsrv.a is NOT expected to be byte-identical: ar stamps each member
    # with the build time, and gcc's LTO section names carry a per-compilation
    # random id. A real change shows up in audsrv.irx or in the member sizes,
    # which build.sh compares with ar; there is no ar on a stock Windows box,
    # so this twin reports the size of the archive and leaves it at that.
    $an = (Get-Item (Join-Path $out 'libaudsrv.a')).Length
    $bn = (Get-Item (Join-Path $here 'bin\libaudsrv.a')).Length
    Write-Host "  libaudsrv.a $an bytes vs $bn committed (ar timestamps and"
    Write-Host "              gcc's LTO ids differ by design - see build.sh)"
    exit $rc
}

Write-Host '[audsrv] done:'
Get-ChildItem $out | Format-Table Name, Length
Write-Host ''
Write-Host 'Commit bin\ together with whatever source change produced it, then'
Write-Host 'rebuild a game (the Runner re-applies the overlay when these bytes change).'
