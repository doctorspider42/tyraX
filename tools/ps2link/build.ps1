# Builds the TyraX ps2link - the ONLY ps2link the editor deploys to. The
# Windows twin of build.sh; keep the two in step (same repo, same commit, same
# patch). See tools/ps2link/README.md and docs/ps2link-setup.md.
#
# Reproducible: clones a pinned ps2link, applies tyrax.patch, builds inside the
# official ps2dev/ps2dev toolchain image (Docker), and drops ps2link.elf next to
# this script. Needs Docker Desktop running.
#
#   ./build.ps1              # build -> tools/ps2link/ps2link.elf
#   ./build.ps1 -Clean       # nuke the work tree first
#   ./build.ps1 -LoadHigh    # -> tools/ps2link/ps2link-loadhigh.elf
#
# -LoadHigh links ps2link at 0x01ee8000 (top of RAM) instead of the default
# 0x00094000. The default sits in the "BIOS unused" window below the 0x00100000
# a game loads at - out of the game's way, but right on top of a launcher that
# keeps its own code down there: FreeMcBoot's main menu / shortcuts hand over
# to a low-memory loader, and loading over it is a black screen with no network
# (uLaunchELF, which does not, boots the same ELF fine). The high build swaps
# the problem: it is clear of every launcher, but a game that allocates its way
# past ~31 MB would overwrite it instead. The boot screen prints the address it
# actually loaded at, so there is never any doubt which one is flashed.

param([switch]$Clean, [switch]$LoadHigh)

# NOTE: no ErrorActionPreference='Stop' - git and docker write progress to
# stderr, which Stop would treat as fatal. We check $LASTEXITCODE instead.
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$work = Join-Path $here 'build'
$patch = Join-Path $here 'tyrax.patch'

# Pinned so the patch always applies cleanly; bump both when refreshing.
$repo = 'https://github.com/ps2dev/ps2link.git'
$commit = '0c6138c5553760423070d1797ac475c4d98a06e6'
$image = 'ps2dev/ps2dev:latest'

function Invoke-Checked([string]$what) {
    if ($LASTEXITCODE -ne 0) { throw "$what failed (exit $LASTEXITCODE)" }
}

if ($Clean -and (Test-Path $work)) {
    # git keeps object files read-only, which can make a plain -Force removal
    # fail; clear the attribute first, then verify - a silent failure here used
    # to fall through to "reusing existing clone" and build a stale tree.
    Get-ChildItem -Recurse -Force $work | ForEach-Object { $_.Attributes = 'Normal' }
    Remove-Item -Recurse -Force $work -ErrorAction SilentlyContinue
    if (Test-Path $work) { throw "could not remove $work (a file is locked?)" }
}

if (-not (Test-Path (Join-Path $work '.git'))) {
    Write-Host "== Cloning ps2link @ $($commit.Substring(0,10)) =="
    New-Item -ItemType Directory -Force -Path $work | Out-Null
    git clone --quiet $repo $work 2>&1 | Write-Host
    Invoke-Checked 'git clone'
    git -C $work checkout --quiet $commit 2>&1 | Write-Host
    Invoke-Checked 'git checkout'
} else {
    Write-Host "== Reusing existing clone (use -Clean to reset) =="
    git -C $work checkout --quiet -- . 2>&1 | Write-Host
    Invoke-Checked 'git checkout'
}

Write-Host "== Applying tyrax.patch =="
git -C $work apply --verbose $patch 2>&1 | Write-Host
Invoke-Checked 'git apply'

Write-Host "== Building in $image (this pulls the image on first run) =="
# make ee builds the unpacked ELF (no ps2-packer needed). apk adds make/git the
# minimal ps2dev image lacks. Non-login shell so the image's toolchain PATH stays.
$mount = ($work -replace '\\', '/') + ':/work'
$high = if ($LoadHigh) { '1' } else { '0' }
docker run --rm -v $mount -e LOADHIGH=$high $image sh -c @'
set -e
apk add --no-cache make git >/dev/null 2>&1 || true
cd /work
make clean >/dev/null 2>&1 || true
make ee LOADHIGH=$LOADHIGH
'@
Invoke-Checked 'docker build'

$elf = Join-Path $work 'ee/ps2link.elf'
if (-not (Test-Path $elf)) { throw "expected $elf, not produced" }
$out = Join-Path $here $(if ($LoadHigh) { 'ps2link-loadhigh.elf' } else { 'ps2link.elf' })
Copy-Item -Force $elf $out
Write-Host "== OK: $out ($((Get-Item $out).Length) bytes) =="
Write-Host "Flash this onto your PS2 as PS2LINK.ELF - see docs/ps2link-setup.md."
if (-not $LoadHigh) {
    Write-Host "NOTE: this is the 0x00094000 build. It boots from uLaunchELF, but"
    Write-Host "      FreeMcBoot's own menu and its shortcuts hand over to a loader"
    Write-Host "      that lives at that address, so booting it there is a black"
    Write-Host "      screen. If that is how you start it, rebuild with -LoadHigh"
    Write-Host "      and flash ps2link-loadhigh.elf instead."
}
