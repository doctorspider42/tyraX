# Single source of truth for everything fetched into vendor/ and tools/.
#
# setup.ps1 fetches from these lists and build.ps1 reads the SAME lists to
# refuse to configure while a dependency the editor compiles is missing. That
# is the whole point of the file: a dependency added here is guarded
# automatically, so there is no second list to forget. When there were two
# lists, adding ufbx left build.ps1 checking the old four directories and a
# fresh clone / older worktree walked straight into cmake with half the sources
# on disk - which surfaces as the baffling pair of errors
# "Cannot find source file: vendor/<dep>/..." + "No SOURCES given to target".
#
# Fields:
#   Probe - a file the build actually needs, not just the directory, so an
#           interrupted or partial clone counts as missing.
#   Build - $true when CMake compiles it into tyrax-editor, i.e. build.ps1
#           blocks on it. vendor/tyra is the in-tree PS2 engine fork: its
#           sources are tracked in this repo and compiled inside Docker, never
#           by the editor build, so it is listed for provenance only.

$VendorDeps = @(
    @{ Url = 'https://github.com/ocornut/imgui.git'
       Branch = 'docking'; Dir = 'vendor/imgui'
       Probe = 'vendor/imgui/imgui.cpp'; Build = $true },
    @{ Url = 'https://github.com/glfw/glfw.git'
       Branch = '3.4'; Dir = 'vendor/glfw'
       Probe = 'vendor/glfw/CMakeLists.txt'; Build = $true },
    @{ Url = 'https://github.com/CedricGuillemet/ImGuizmo.git'
       Branch = 'master'; Dir = 'vendor/imguizmo'
       Probe = 'vendor/imguizmo/src/ImGuizmo.cpp'; Build = $true },
    @{ Url = 'https://github.com/Nelarius/imnodes.git'
       Branch = 'master'; Dir = 'vendor/imnodes'
       Probe = 'vendor/imnodes/imnodes.cpp'; Build = $true },
    @{ Url = 'https://github.com/nothings/stb.git'
       Branch = 'master'; Dir = 'vendor/stb'
       Probe = 'vendor/stb/stb_image.h'; Build = $true },
    @{ Url = 'https://github.com/ufbx/ufbx.git'
       Branch = 'master'; Dir = 'vendor/ufbx'
       Probe = 'vendor/ufbx/ufbx.c'; Build = $true },
    @{ Url = 'https://github.com/h4570/tyra.git'
       Branch = 'master'; Dir = 'vendor/tyra'
       Probe = 'vendor/tyra/Makefile.base'; Build = $false }
)

# stb ships single headers we #include directly; back-fill any that a stale or
# partial vendor/stb is missing (setup.ps1 does the fetching).
$StbHeaders = @('stb_image.h', 'stb_truetype.h', 'stb_image_write.h')

# MakeHuman CC0 data for the Character Generator (docs/character-generator.md).
# DATA ONLY - the MakeHuman *program* is AGPL and none of it is used here; the
# base mesh, targets, proxy meshes, rig, vertex weights and skins were
# explicitly released as CC0 in 2020 (each file carries the release notice in
# its own header, and the assets repository ships the CC0 text). Credits live
# in README.md; keep them in sync when this list grows.
#
# Fetched file-by-file rather than cloned: the two upstream repositories are
# several GB, and the editor needs ~110 of their files. The download is about
# 40 MB, all of it git-ignored under vendor/. Not compiled into the editor, so
# build.ps1 never blocks on it - the Character Generator window explains how to
# get the data when it is missing.
$MhRaw = 'https://raw.githubusercontent.com/makehumancommunity/makehuman/master/makehuman/data'
$MhAssetRaw = 'https://raw.githubusercontent.com/makehumancommunity/makehuman-assets/master/base'
# The skin textures are stored in Git LFS, so raw.githubusercontent serves a
# 132-byte pointer file instead of the PNG - media.githubusercontent serves the
# real bytes.
$MhAssetLfs = 'https://media.githubusercontent.com/media/makehumancommunity/makehuman-assets/master/base'

$MhFiles = [System.Collections.ArrayList]@(
    @{ Url = "$MhRaw/3dobjs/base.obj";              Path = 'base.obj' },
    @{ Url = "$MhRaw/rigs/default.mhskel";          Path = 'default.mhskel' },
    @{ Url = "$MhRaw/rigs/default_weights.mhw";     Path = 'default_weights.mhw' },
    @{ Url = "$MhAssetRaw/proxymeshes/proxy741/proxy741.obj";   Path = 'proxy741.obj' },
    @{ Url = "$MhAssetRaw/proxymeshes/proxy741/proxy741.proxy"; Path = 'proxy741.proxy' },
    @{ Url = 'https://raw.githubusercontent.com/makehumancommunity/makehuman-assets/master/LICENSE.txt'
       Path = 'LICENSE-CC0.txt' }
)

# The macro target set: every combination the macro sliders blend between.
# "<ethnicity>-<gender>-<age>" carries the facial/skeletal character, and
# "universal-<gender>-<age>-<muscle>-<weight>" the build. Height and body
# proportions have their own 144-file sets upstream; the generator scales for
# height instead, so they are deliberately not fetched (see the docs).
foreach ($e in @('african', 'asian', 'caucasian')) {
    foreach ($g in @('female', 'male')) {
        foreach ($a in @('baby', 'child', 'young', 'old')) {
            [void]$MhFiles.Add(@{ Url = "$MhRaw/targets/macrodetails/$e-$g-$a.target"
                                  Path = "targets/$e-$g-$a.target" })
        }
    }
}
foreach ($g in @('female', 'male')) {
    foreach ($a in @('baby', 'child', 'young', 'old')) {
        foreach ($m in @('minmuscle', 'averagemuscle', 'maxmuscle')) {
            foreach ($w in @('minweight', 'averageweight', 'maxweight')) {
                $n = "universal-$g-$a-$m-$w.target"
                [void]$MhFiles.Add(@{ Url = "$MhRaw/targets/macrodetails/$n"
                                      Path = "targets/$n" })
            }
        }
    }
}

# Skins (2048x2048 diffuse maps, downscaled to 256 at generate time). Six of
# the 22 upstream textures, chosen to span age x tone x gender plus the two
# clothed "special suit" ones.
foreach ($s in @('young_lightskinned_female_diffuse', 'young_lightskinned_male_diffuse',
                 'young_darkskinned_female_diffuse', 'young_darkskinned_male_diffuse',
                 'young_caucasian_female_special_suit', 'young_caucasian_male_special_suit')) {
    [void]$MhFiles.Add(@{ Url = "$MhAssetLfs/skins/textures/$s.png"; Path = "skins/$s.png" })
}

# Clothes and hair. These are `.mhclo` files - the SAME barycentric binding
# format as the body proxy, which is why a shirt fits a generated body with no
# extra machinery. Each asset is a manifest + a mesh + a 2048-square diffuse
# map (Git LFS again). A curated handful rather than the full wardrobe: the
# textures dominate the download.
foreach ($c in @('male_casualsuit01', 'male_worksuit01', 'male_elegantsuit01',
                 'female_casualsuit01', 'female_elegantsuit01', 'shoes01')) {
    foreach ($ext in @('mhclo', 'obj')) {
        [void]$MhFiles.Add(@{ Url = "$MhAssetRaw/clothes/$c/$c.$ext"; Path = "clothes/$c.$ext" })
    }
    [void]$MhFiles.Add(@{ Url = "$MhAssetLfs/clothes/$c/${c}_diffuse.png"
                          Path = "clothes/${c}_diffuse.png" })
}
foreach ($h in @('short01', 'bob01', 'long01', 'ponytail01')) {
    foreach ($ext in @('mhclo', 'obj')) {
        [void]$MhFiles.Add(@{ Url = "$MhAssetRaw/hair/$h/$h.$ext"; Path = "hair/$h.$ext" })
    }
    [void]$MhFiles.Add(@{ Url = "$MhAssetLfs/hair/$h/${h}_diffuse.png"
                          Path = "hair/${h}_diffuse.png" })
}

$MhAssets = @{ Dir = 'vendor/mh-assets'; Probe = 'vendor/mh-assets/base.obj'
               Files = $MhFiles }

# Real-PS2 network deploy tools ("Run on PS2" in the editor): ps2client.exe
# talks to a console running ps2link. The Runner looks for it in
# tools/ps2client/bin; ps2link goes onto the console's memory card once
# (edit IPCONFIG.DAT for your LAN - format: "ip netmask gateway"). Not needed
# to build the editor, so build.ps1 never blocks on these.
$Ps2Tools = @(
    @{ Url = 'https://github.com/ps2dev/ps2client/releases/download/v1.3.0/ps2client-211df54b-windows-latest.tar.gz'
       Dir = 'tools/ps2client'; Probe = 'tools/ps2client/bin/ps2client.exe' },
    @{ Url = 'https://github.com/ps2dev/ps2link/releases/download/RenameMe/ps2link-0269a955-highloading.tar.gz'
       Dir = 'tools/ps2link';   Probe = 'tools/ps2link/ps2link/PS2LINK.ELF' }
)
