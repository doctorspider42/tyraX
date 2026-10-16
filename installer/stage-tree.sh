# WHAT IS IN A TYRAX PACKAGE - sourced by build-package.sh (Linux: tarball,
# .deb, .rpm) and build-macos.sh (macOS: TyraX.app in a .dmg), so the two can
# never ship different trees. installer/tyrax.iss is the Windows twin and
# shares no lines with this file: a new exe-relative lookup in the editor needs
# a line in tyrax_stage_tree below AND a [Files] entry there, in one commit.
#
# Kept to bash 3.2 and the BSD userland (macOS runs it): no `\+` in sed, no
# `install -D`, no GNU-only tar flags.

# The editor version from src/version.hpp - the one place it is authored.
tyrax_editor_version() {  # tyrax_editor_version <repo>
    local part n out=""
    for part in MAJOR MINOR PATCH; do
        n="$(sed -n "s/^#define TYRAX_VERSION_$part[[:space:]][[:space:]]*\([0-9][0-9]*\)[[:space:]]*$/\1/p" \
             "$1/src/version.hpp" | head -n1)"
        [ -n "$n" ] || { echo "src/version.hpp has no TYRAX_VERSION_$part" >&2; exit 1; }
        out="${out:+$out.}$n"
    done
    echo "$out"
}

# The nine VU framework files a project with VU scripts is handed. Exactly
# project.cpp's kVuFrameworkFiles - the probe it locates them with is
# src/vushader.hpp, so all nine travel or none do.
TYRAX_VU_FRAMEWORK=(vuir.hpp vuir.cpp vusim.hpp vusim.cpp vugen.hpp vugen.cpp
                    vushader.hpp vushader.cpp vumain.cpp)

# rsync is not assumed: a fresh container has tar and cp and little else.
tyrax_copy_tree() {  # tyrax_copy_tree <src-dir> <dst-dir> <find-prune-args...>
    local src="$1" dst="$2"; shift 2
    mkdir -p "$dst"
    ( cd "$src" && find . "$@" -print0 | tar --null -cf - --files-from=- ) \
        | ( cd "$dst" && tar -xf - )
}

# The repo-shaped tree the editor resolves its shipped files from, one
# directory above its binary (platform::resourceBinDir):
#
#   <stage>/bin/tyrax-editor
#   <stage>/vendor/tyra/...    the engine, as sources
#   <stage>/vendor/openvcl, vendor/vclpp   the VU tools the toolchain builds
#   <stage>/tools/...          ps2client, toolchain + ps2link scripts, the .vsix
#   <stage>/src/...            the nine VU framework files a project is given
#   <stage>/examples/...
#
# On macOS <stage> is TyraX.app/Contents/Resources and bin/ is left empty: the
# binary lives in Contents/MacOS, and resourceBinDir() answers Resources/bin.
tyrax_stage_tree() {  # tyrax_stage_tree <repo> <exe|""> <stage> <marker word>
    local repo="$1" exe="$2" stage="$3" marker="$4" f
    rm -rf "$stage"
    mkdir -p "$stage/bin" "$stage/src"

    [ -z "$exe" ] || install -m 0755 "$exe" "$stage/bin/tyrax-editor"

    # The engine the editor compiles every game against, as sources - the build
    # container bind-mounts this directory read-only.
    #
    # PRUNE BY DIRECTORY HERE, NEVER BY EXTENSION, and the three below are
    # exactly what .gitignore drops under vendor/tyra: what is not in git is
    # build leftovers a dev checkout happens to carry, and everything else is
    # CONTENT. This list was once "*.o -o -name '*.a' -o -name '*.elf'", which
    # is the same intent spelled as a file type - and it silently dropped
    # vendor/tyra/audsrv/bin/libaudsrv.a, a committed artifact of the audsrv
    # fork that runner.cpp overlays onto the container's PS2SDK. Its two
    # siblings (audsrv.irx, audsrv.h) matched no pattern and travelled, so the
    # overlay's `cp a && cp b && cp c` died on the missing lib BEFORE copying
    # the header, every game then compiled against stock PS2SDK headers, and
    # the build ended in "'audsrv_adpcm_set_volume_and_pan' was not declared" -
    # for everyone who installed TyraX and for nobody who built from a checkout.
    tyrax_copy_tree "$repo/vendor/tyra" "$stage/vendor/tyra" \
        \( -name .git -o -path './engine/obj' -o -path './engine/bin' \
           -o -path './audsrv/.work' \) -prune -o -type f

    tyrax_copy_tree "$repo/vendor/openvcl" "$stage/vendor/openvcl" \
        \( -name .git -o -name build -o -name openvcl \) -prune -o -type f
    tyrax_copy_tree "$repo/vendor/vclpp" "$stage/vendor/vclpp" \
        \( -name .git -o -name vclpp \) -prune -o -type f

    # ps2client (network deploy), the ps2link build scripts and the VS Code
    # extension package the editor installs on request. ps2client is fetched
    # per-OS by setup.sh, so this is the build of it for the packaging host.
    # tools/macos is the macOS packager's own output (bundled host tools) and
    # is staged by it, never copied from a checkout.
    tyrax_copy_tree "$repo/tools" "$stage/tools" \
        \( -path './ps2link/work' -o -path './macos' -o -name '*.o' -o -name '*.a' \) \
        -prune -o -type f

    for f in "${TYRAX_VU_FRAMEWORK[@]}"; do
        install -m 0644 "$repo/src/$f" "$stage/src/$f"
    done

    # The attribution a distributed build carries (LICENSE-EXCEPTION.md says
    # what a game made with it owes, which is nothing).
    for f in LICENSE LICENSE-EXCEPTION.md NOTICE THIRD-PARTY-LICENSES.md README.md; do
        install -m 0644 "$repo/$f" "$stage/$f"
    done
    install -m 0644 "$repo/resources/icon.png" "$stage/tyrax-editor.png"

    # Build output of an example that was opened in a dev checkout is not
    # content - a fresh clone (what CI packages) has none of it either way.
    tyrax_copy_tree "$repo/examples" "$stage/examples" \
        \( -name bin -o -name obj -o -name '.history' -o -name '*.elf' -o -name '*.iso' \) \
        -prune -o -type f

    # THE MARKER IS WHAT THE UPDATER READS (update::installKind). One word, no
    # newline needed, and its absence means "a source checkout" - which is the
    # correct answer for a repo built with ./build.sh.
    printf '%s\n' "$marker" > "$stage/.tyrax-package"

    # Whatever umask the packager happened to run under is not what ships: a
    # group-writable file inside a system package is a lintian/rpmlint finding
    # and, under /opt, a real one.
    chmod -R go-w "$stage"
}
