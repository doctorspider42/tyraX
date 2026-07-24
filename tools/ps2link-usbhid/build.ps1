# Builds a custom ps2link.elf with the USB HID stack (usbd + ps2kbd + ps2mouse)
# baked into its own boot, so a game deployed over the network (F6 "Run on PS2")
# can reuse the resident drivers and get a keyboard AND mouse - see
# docs/keyboard-mouse.md and tools/ps2link-usbhid/README.md.
#
# Reproducible: clones a pinned ps2link, applies usbhid.patch, builds inside the
# official ps2dev/ps2dev toolchain image (Docker), and drops ps2link.elf next to
# this script. Needs Docker Desktop running.
#
#   ./build.ps1              # build -> tools/ps2link-usbhid/ps2link.elf
#   ./build.ps1 -Clean       # nuke the work tree first

param([switch]$Clean)

# NOTE: no ErrorActionPreference='Stop' - git and docker write progress to
# stderr, which Stop would treat as fatal. We check $LASTEXITCODE instead.
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$work = Join-Path $here 'build'
$patch = Join-Path $here 'usbhid.patch'

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

Write-Host "== Applying usbhid.patch =="
git -C $work apply --verbose $patch 2>&1 | Write-Host
Invoke-Checked 'git apply'

Write-Host "== Building in $image (this pulls the image on first run) =="
# make ee builds the unpacked ELF (no ps2-packer needed). apk adds make/git the
# minimal ps2dev image lacks. Non-login shell so the image's toolchain PATH stays.
$mount = ($work -replace '\\', '/') + ':/work'
docker run --rm -v $mount $image sh -c @'
set -e
apk add --no-cache make git >/dev/null 2>&1 || true
cd /work
make clean >/dev/null 2>&1 || true
make ee
'@
Invoke-Checked 'docker build'

$elf = Join-Path $work 'ee/ps2link.elf'
if (-not (Test-Path $elf)) { throw "expected $elf, not produced" }
$out = Join-Path $here 'ps2link.elf'
Copy-Item -Force $elf $out
Write-Host "== OK: $out ($((Get-Item $out).Length) bytes) =="
Write-Host "Flash this onto your PS2 (memory card / boot medium) in place of stock ps2link."
