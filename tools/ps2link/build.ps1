# Builds the TyraX ps2link - the ONLY ps2link the editor deploys to. The
# Windows twin of build.sh; keep the two in step (same repo, same commit, same
# patch). See tools/ps2link/README.md and docs/ps2link-setup.md.
#
# Reproducible: clones a pinned ps2link, applies tyrax.patch, builds it with the
# pinned PS2DEV v2.0.0 toolchain, and drops ps2link.elf next to this script.
# The toolchain is the editor's native one (%LOCALAPPDATA%\tyra-editor\
# toolchain\ps2dev, run through WSL like a game build) when it is installed,
# else the official ps2dev/ps2dev image in Docker. Same release either way, so
# the same code - but not the same bytes: the release archive and the image
# are separate CI builds whose ps2sdk libraries list their objects in another
# order, so the linker lays the same functions out differently (measured r8:
# identical section sizes and instructions, 12k disassembly lines moved). Each
# path is reproducible against itself.
#
#   ./build.ps1              # build -> tools/ps2link/ps2link.elf  (0x01ee8000)
#   ./build.ps1 -Clean       # nuke the work tree first
#   ./build.ps1 -Low         # -> tools/ps2link/ps2link-low.elf   (0x00094000)
#   ./build.ps1 -NoUsb       # -> ...-nousb.elf: no usbd/ps2kbd/ps2mouse baked in
#   ./build.ps1 -Packed      # -> ...-packed.elf: run ps2-packer over it
#   ./build.ps1 -Docker      # use the Docker image even with a native toolchain
#
# The revision the banner shows (r8, ...) is TYRAX_REV in tyrax.patch's
# ee/Makefile hunk - the one place it lives.
#
# Two link addresses, and which one works depends on how you boot it.
#
# 0x00094000 is the "BIOS unused" window below the 0x00100000 a game loads at.
# It keeps ps2link clear of the game - but it is also where a launcher keeps
# its own resident loader, so FreeMcBoot's menu and its shortcuts black-screen
# on it (uLaunchELF, which loads elsewhere, boots it fine). 0x01ee8000 is the
# top of RAM: clear of every launcher, at the price of being in reach of a game
# that allocates its way past ~31 MB.
#
# Upstream ships the low one as its default and publishes both from CI
# ("default" and "highloading"). We default to HIGH instead, because booting
# from the FMCB menu is the normal way to start a console here and a black
# screen is a worse failure than a memory ceiling no scene has come near. The
# boot screen prints the address it actually loaded at, so a flashed card
# always says which one it is.

# -Packed runs ps2-packer over the image, the way upstream's releases ship: what
# a launcher loads is then a small stub high in memory that decompresses the
# real image down once the loader is done with its own code. That is what a LOW
# build needs to boot from FreeMcBoot's menu at all - an unpacked low image
# lands on that loader mid-load and black-screens. The default here is
# unpacked, because the high build boots from every launcher tried either way
# and unpacked is the shape that has been measured through Run -> Stop cycles.

param([switch]$Clean, [switch]$Low, [switch]$NoUsb, [switch]$Packed, [switch]$Docker)

# NOTE: no ErrorActionPreference='Stop' - git and docker write progress to
# stderr, which Stop would treat as fatal. We check $LASTEXITCODE instead.
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$work = Join-Path $here 'build'
$patch = Join-Path $here 'tyrax.patch'

# Pinned so the patch always applies cleanly; bump both when refreshing.
$repo = 'https://github.com/ps2dev/ps2link.git'
$commit = '0c6138c5553760423070d1797ac475c4d98a06e6'
# Pinned to a release tag, and the SAME tag docker/Dockerfile builds its copy of
# ps2link with, so the image's /usr/local/share/tyrax/ps2link/ and this script
# produce the same ELF. It used to be ':latest', which quietly made a script
# whose whole point is reproducibility a moving target: the same commit and the
# same patch measured 285 364 B when tools/ps2link/ps2link.elf was committed and
# 285 620 B a few weeks later, with nothing here changed. v2.0.0 gives 284 340 B
# every time.
$image = 'ps2dev/ps2dev:v2.0.0'

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

$rev = 'r' + ((Select-String -Path $patch -Pattern '^\+TYRAX_REV := (\d+)').Matches[0].Groups[1].Value)

# The editor's native toolchain: Linux binaries, run through WSL. The same
# PS2DEV v2.0.0 release the image below is tagged with.
$toolchain = Join-Path $env:LOCALAPPDATA 'tyra-editor\toolchain\ps2dev'
$native = -not $Docker -and
    (Test-Path (Join-Path $toolchain 'ee\bin\mips64r5900el-ps2-elf-gcc')) -and
    (Get-Command wsl.exe -ErrorAction SilentlyContinue)
function WslPath([string]$Path) {
    $out = (wsl.exe wslpath -a ($Path -replace '\\', '/')).Trim()
    if (-not $out) { throw "could not translate $Path for WSL" }
    return $out
}

$high = if ($Low) { '0' } else { '1' }
# NOT $nousb: PowerShell variable names are case-insensitive, so that is the
# same variable as the [switch]$NoUsb parameter. Assigning '1' to it fails the
# type conversion, leaves the switch in place, and "$nousb" then interpolates
# as "True" - make compares that against 1, keeps the USB stack, and the build
# quietly ignores the flag it was asked for.
$usbFlag = if ($NoUsb) { '1' } else { '0' }
# The link address is baked into the command rather than passed as a container
# environment variable: a lost -e leaves LOADHIGH empty, make silently falls
# back to its own `LOADHIGH ?= 0`, and you get a low build wearing the name of
# a high one - which on this console means a black screen and no clue why.
# `make` runs ps2-packer over ee/ps2link.elf and writes bin/PS2LINK.ELF;
# `make ee` stops at the raw image.
$make = @(
    'make clean >/dev/null 2>&1 || true',
    "make $(if ($Packed) { '' } else { 'ee' }) LOADHIGH=$high NOUSB=$usbFlag"
)
if ($native) {
    Write-Host "== Building $rev with the native toolchain ($toolchain, via WSL) =="
    $root = WslPath $toolchain
    $script = (@(
        'set -e',
        "export PS2DEV='$root'",
        'export PS2SDK="$PS2DEV/ps2sdk"',
        'export PATH="$PS2DEV/bin:$PS2DEV/ee/bin:$PS2DEV/iop/bin:$PS2SDK/bin:$PATH"',
        "cd '$(WslPath $work)'",
        $make[0],
        "make ee LOADHIGH=$high NOUSB=$usbFlag"
    ) + $(if ($Packed) { @(
        # The packer step of `make`, by hand: the release's ps2-packer looks
        # for its stub under the path it was built at (/__w/ps2dev/...), not
        # next to itself, and for its modules next to argv[0] - so a full
        # path, not PATH. Same default stub, so the same packed ELF.
        'mkdir -p bin',
        '"$PS2DEV/bin/ps2-packer" -s "$PS2DEV/share/ps2-packer/stub/lzma-1d00-stub" ee/ps2link.elf bin/PS2LINK.ELF >/dev/null'
    ) } else { @() })) -join "`n"
    wsl.exe --exec bash -c $script
    Invoke-Checked 'native build'
} else {
    Write-Host "== Building $rev in $image (this pulls the image on first run) =="
    # apk adds make/git the minimal ps2dev image lacks. Non-login shell so the
    # image's toolchain PATH stays.
    $mount = ($work -replace '\\', '/') + ':/work'
    $script = (@(
        'set -e',
        'apk add --no-cache make git >/dev/null 2>&1 || true',
        'cd /work'
    ) + $make) -join "`n"
    docker run --rm -v $mount $image sh -c $script
    Invoke-Checked 'docker build'
}

$elf = Join-Path $work $(if ($Packed) { 'bin/PS2LINK.ELF' } else { 'ee/ps2link.elf' })
if (-not (Test-Path $elf)) { throw "expected $elf, not produced" }
$suffix = ''
if ($Low) { $suffix += '-low' }
if ($NoUsb) { $suffix += '-nousb' }
if ($Packed) { $suffix += '-packed' }
$out = Join-Path $here "ps2link$suffix.elf"
Copy-Item -Force $elf $out
Write-Host "== OK: $out ($((Get-Item $out).Length) bytes) =="
Write-Host "Flash this onto your PS2 as PS2LINK.ELF - see docs/ps2link-setup.md."
if ($Low -and -not $Packed) {
    Write-Host "NOTE: an unpacked low build black-screens from FreeMcBoot's menu -"
    Write-Host "      it lands on that loader mid-load. Add -Packed for a build that"
    Write-Host "      boots there (it still dies on Stop - docs/backlog.md)."
}
if ($NoUsb) {
    Write-Host "      No keyboard or mouse over ps2link with this one - it bakes in"
    Write-Host "      no usbd/ps2kbd/ps2mouse, and a game cannot load them itself on"
    Write-Host "      a network-booted ps2link. The banner reads '$rev (no USB)'."
}
