# Single source of truth for everything fetched into vendor/ and tools/ on
# Linux/macOS - the POSIX twin of deps.ps1. setup.sh fetches from these lists
# and build.sh reads the SAME lists to refuse to configure while a dependency
# the editor compiles is missing. Keep the two files in step: a dependency
# added to one and not the other leaves that platform's build guard blind.
#
# Each VENDOR_DEPS entry is  url|mirror|commit|ref|dir|probe|build
#   commit - the EXACT commit fetched. Never a branch name. A branch is a
#            moving target: two people running setup.sh a month apart used to
#            get two different imgui, and because the loop skips a directory
#            whose probe already exists, nobody ever noticed their vendor/ had
#            frozen at whatever HEAD happened to be that day. Pinning is what
#            makes a build of this repo reproducible. To bump a dependency,
#            change the SHA here and in deps.ps1, delete the vendor directory,
#            re-run setup and actually build.
#   ref    - the branch or tag that commit came from. Documentation only: it is
#            what you `git log` to pick the next SHA. Nothing fetches it.
#   mirror - our fork of the upstream, tried when upstream fetch fails. Every
#            pinned SHA above is reachable there, so the editor still builds if
#            an upstream repo is deleted, renamed or force-pushed. Mirrors are
#            plain GitHub forks (doctorspider42/tyrax-vendor-*); refresh one
#            with `gh repo sync doctorspider42/<fork>` before bumping its SHA.
#   probe  - a file the build actually needs, not just the directory, so an
#            interrupted or partial clone counts as missing.
#   build  - "1" when CMake compiles it into tyrax-editor, i.e. build.sh blocks
#            on it. vendor/tyra is the in-tree PS2 engine fork: its sources are
#            tracked in this repo and compiled inside Docker, never by the
#            editor build, so it is listed for provenance only - its commit is
#            the upstream fork point recorded in .gitignore, not something
#            setup.sh checks out over the tracked engine sources.

VENDOR_DEPS=(
    "https://github.com/ocornut/imgui.git|https://github.com/doctorspider42/tyrax-vendor-imgui.git|b334d19b667958ed970000073644d911fae17e57|docking|vendor/imgui|vendor/imgui/imgui.cpp|1"
    "https://github.com/glfw/glfw.git|https://github.com/doctorspider42/tyrax-vendor-glfw.git|7b6aead9fb88b3623e3b3725ebb42670cbe4c579|3.4|vendor/glfw|vendor/glfw/CMakeLists.txt|1"
    "https://github.com/CedricGuillemet/ImGuizmo.git|https://github.com/doctorspider42/tyrax-vendor-imguizmo.git|dc25afb98bc3ebe00dfc9a23ba7235fead2ccb1d|master|vendor/imguizmo|vendor/imguizmo/src/ImGuizmo.cpp|1"
    "https://github.com/Nelarius/imnodes.git|https://github.com/doctorspider42/tyrax-vendor-imnodes.git|eb36902c892548ef94f88f51ad7e7c9c7058a71c|master|vendor/imnodes|vendor/imnodes/imnodes.cpp|1"
    "https://github.com/nothings/stb.git|https://github.com/doctorspider42/tyrax-vendor-stb.git|31c1ad37456438565541f4919958214b6e762fb4|master|vendor/stb|vendor/stb/stb_image.h|1"
    "https://github.com/ufbx/ufbx.git|https://github.com/doctorspider42/tyrax-vendor-ufbx.git|fcc5d6ba444cfd3eb80677dba5e37e493941abe5|master|vendor/ufbx|vendor/ufbx/ufbx.c|1"
    "https://github.com/mackron/miniaudio.git|https://github.com/doctorspider42/tyrax-vendor-miniaudio.git|9634bedb5b5a2ca38c1ee7108a9358a4e233f14d|master|vendor/miniaudio|vendor/miniaudio/miniaudio.h|1"
    "https://github.com/h4570/tyra.git|https://github.com/doctorspider42/tyrax-vendor-tyra.git|92734168a21f8071643a49b9573eeb7b4aba2110|master|vendor/tyra|vendor/tyra/Makefile.base|0"
)

# stb ships single headers we #include directly; back-fill any that a stale or
# partial vendor/stb is missing (setup.sh does the fetching). Fetched from the
# pinned stb commit, NOT from master - a back-filled header from a newer stb
# than the rest of vendor/stb is exactly the silent version skew the pins exist
# to prevent.
STB_HEADERS=(stb_image.h stb_truetype.h stb_image_write.h)

# MakeHuman CC0 data for the Character Generator (docs/character-generator.md).
# The POSIX twin of deps.ps1's $MhFiles/$MhAssets - keep the two lists in step.
#
# DATA ONLY - the MakeHuman *program* is AGPL and none of it is used here; the
# base mesh, targets, proxy meshes, rig, vertex weights and skins were
# explicitly released as CC0 in 2020 (each file carries the release notice in
# its own header, and the assets repository ships the CC0 text, fetched below as
# LICENSE-CC0.txt). Credits live in README.md and THIRD-PARTY-LICENSES.md; keep
# them in sync when this list grows.
#
# Fetched file-by-file rather than cloned: the two upstream repositories are
# several GB and the editor needs ~110 of their files. The download is about
# 40 MB, all of it git-ignored under vendor/. Not compiled into the editor, so
# build.sh never blocks on it - the Character Generator window explains how to
# get the data when it is missing.
MH_RAW='https://raw.githubusercontent.com/makehumancommunity/makehuman/master/makehuman/data'
MH_ASSET_RAW='https://raw.githubusercontent.com/makehumancommunity/makehuman-assets/master/base'
# The skin and clothing textures are stored in Git LFS, so raw.githubusercontent
# serves a 132-byte pointer file instead of the PNG - media.githubusercontent
# serves the real bytes.
MH_ASSET_LFS='https://media.githubusercontent.com/media/makehumancommunity/makehuman-assets/master/base'

MH_ASSETS_DIR='vendor/mh-assets'
MH_ASSETS_PROBE='vendor/mh-assets/base.obj'

# Populates MH_FILES with "url|path" entries. A function rather than a literal
# array because most of the list is combinatorial (see the target sets below),
# exactly as deps.ps1 builds it with nested foreach.
tyrax_mh_files() {
    MH_FILES=(
        "$MH_RAW/3dobjs/base.obj|base.obj"
        "$MH_RAW/rigs/default.mhskel|default.mhskel"
        "$MH_RAW/rigs/default_weights.mhw|default_weights.mhw"
        "$MH_ASSET_RAW/proxymeshes/proxy741/proxy741.obj|proxy741.obj"
        "$MH_ASSET_RAW/proxymeshes/proxy741/proxy741.proxy|proxy741.proxy"
        "https://raw.githubusercontent.com/makehumancommunity/makehuman-assets/master/LICENSE.txt|LICENSE-CC0.txt"
    )
    # The macro target set: every combination the macro sliders blend between.
    # "<ethnicity>-<gender>-<age>" carries the facial/skeletal character, and
    # "universal-<gender>-<age>-<muscle>-<weight>" the build. Height and body
    # proportions have their own 144-file sets upstream; the generator scales
    # for height instead, so they are deliberately not fetched (see the docs).
    for e in african asian caucasian; do
        for g in female male; do
            for a in baby child young old; do
                MH_FILES+=("$MH_RAW/targets/macrodetails/$e-$g-$a.target|targets/$e-$g-$a.target")
            done
        done
    done
    for g in female male; do
        for a in baby child young old; do
            for m in minmuscle averagemuscle maxmuscle; do
                for w in minweight averageweight maxweight; do
                    n="universal-$g-$a-$m-$w.target"
                    MH_FILES+=("$MH_RAW/targets/macrodetails/$n|targets/$n")
                done
            done
        done
    done
    # Skins (2048x2048 diffuse maps, downscaled to 256 at generate time). Six
    # of the 22 upstream textures, chosen to span age x tone x gender plus the
    # two clothed "special suit" ones.
    for s in young_lightskinned_female_diffuse young_lightskinned_male_diffuse \
             young_darkskinned_female_diffuse young_darkskinned_male_diffuse \
             young_caucasian_female_special_suit young_caucasian_male_special_suit; do
        MH_FILES+=("$MH_ASSET_LFS/skins/textures/$s.png|skins/$s.png")
    done
    # Clothes and hair. These are `.mhclo` files - the SAME barycentric binding
    # format as the body proxy, which is why a shirt fits a generated body with
    # no extra machinery. Each asset is a manifest + a mesh + a 2048-square
    # diffuse map (Git LFS again). A curated handful rather than the full
    # wardrobe: the textures dominate the download.
    for c in male_casualsuit01 male_worksuit01 male_elegantsuit01 \
             female_casualsuit01 female_elegantsuit01 shoes01; do
        for ext in mhclo obj; do
            MH_FILES+=("$MH_ASSET_RAW/clothes/$c/$c.$ext|clothes/$c.$ext")
        done
        MH_FILES+=("$MH_ASSET_LFS/clothes/$c/${c}_diffuse.png|clothes/${c}_diffuse.png")
    done
    for h in short01 bob01 long01 ponytail01; do
        for ext in mhclo obj; do
            MH_FILES+=("$MH_ASSET_RAW/hair/$h/$h.$ext|hair/$h.$ext")
        done
        MH_FILES+=("$MH_ASSET_LFS/hair/$h/${h}_diffuse.png|hair/${h}_diffuse.png")
    done
}

# Real-PS2 network deploy tools ("Run on PS2" in the editor): ps2client talks
# to a console running the TyraX ps2link. The Runner looks for it in
# tools/ps2client/bin. Not needed to build the editor, so build.sh never blocks
# on it. Note the ps2client tarball is per-OS - this is the Linux build;
# deps.ps1 names the Windows one.
#
# ps2link itself is NOT downloaded: the console side is always OUR ps2link,
# built from tools/ps2link/ (a pinned upstream + tyrax.patch, in Docker) and
# flashed onto the memory card once. See docs/ps2link-setup.md.
#   url|dir|probe
PS2_TOOLS=(
    "https://github.com/ps2dev/ps2client/releases/download/v1.3.0/ps2client-211df54b-ubuntu-latest.tar.gz|tools/ps2client|tools/ps2client/bin/ps2client"
)

# Distro packages the editor needs to configure, link and run: the toolchain,
# the X11/Wayland/GL development headers GLFW builds against, and zenity - the
# editor has no built-in file browser, so without zenity (or kdialog) every
# Open/Import button has nothing to open (see platform::pickFile).
#
# ccache is the one OPTIONAL entry: nothing needs it, but CMake picks it up off
# PATH (TYRAX_COMPILER_CACHE in CMakeLists.txt) and this repo is normally
# checked out in several worktrees at once, each recompiling the same TUs from
# scratch. Installed here so that pickup actually happens on Linux - the
# Windows README says `scoop install ccache` for the same reason. Note
# find_program caches the miss, so installing it AFTER a build directory exists
# needs a reconfigure (`./build.sh --clean`, or delete the build dir) before it
# takes effect.
#
# `setup.sh --deps` installs the list for whichever package manager it finds;
# build.sh names it when a tool or header is missing. One list per family
# because the split of the X11 headers into packages differs everywhere - the
# CONTENT is the same set every time, so a new dependency has to be added to
# all four or that distro's users get a link error instead of a clear message.
SYSTEM_PACKAGES_apt="build-essential cmake ninja-build git pkg-config libgl1-mesa-dev libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev libxkbcommon-dev libwayland-dev wayland-protocols zenity ccache"
SYSTEM_PACKAGES_dnf="gcc-c++ cmake ninja-build git pkgconf-pkg-config mesa-libGL-devel libX11-devel libXrandr-devel libXinerama-devel libXcursor-devel libXi-devel libxkbcommon-devel wayland-devel wayland-protocols-devel zenity ccache"
SYSTEM_PACKAGES_pacman="base-devel cmake ninja git pkgconf mesa libx11 libxrandr libxinerama libxcursor libxi libxkbcommon wayland wayland-protocols zenity ccache"
SYSTEM_PACKAGES_zypper="gcc-c++ cmake ninja git pkg-config Mesa-libGL-devel libX11-devel libXrandr-devel libXinerama-devel libXcursor-devel libXi-devel libxkbcommon-devel wayland-devel wayland-protocols-devel zenity ccache"

# Package manager for THIS machine -> "<manager>|<install command>|<packages>",
# or "" when none is recognised. Sourced by both setup.sh (to install) and
# build.sh (to print the one command that fixes a missing toolchain).
tyrax_system_packages() {
    if command -v apt-get >/dev/null 2>&1; then
        echo "apt|apt-get install -y|$SYSTEM_PACKAGES_apt"
    elif command -v dnf >/dev/null 2>&1; then
        echo "dnf|dnf install -y|$SYSTEM_PACKAGES_dnf"
    elif command -v pacman >/dev/null 2>&1; then
        echo "pacman|pacman -S --needed --noconfirm|$SYSTEM_PACKAGES_pacman"
    elif command -v zypper >/dev/null 2>&1; then
        echo "zypper|zypper install -y|$SYSTEM_PACKAGES_zypper"
    fi
}

# How to become root for a package install: sudo when it can actually
# authenticate, else pkexec (which asks in the desktop's own dialog - the only
# thing that works from a non-interactive shell with no tty). Empty when we are
# already root, "-" when neither is available.
tyrax_root_prefix() {
    [ "$(id -u)" = "0" ] && return 0
    if command -v sudo >/dev/null 2>&1 && { sudo -n true >/dev/null 2>&1 || [ -t 0 ]; }; then
        echo "sudo"
    elif command -v pkexec >/dev/null 2>&1; then
        echo "pkexec"
    else
        echo "-"
    fi
}
