/* machorelink - point a Mach-O's dylib references at @loader_path, in place.
 *
 *   machorelink -l <libname> [-l <libname>...] <file>...
 *
 * Every dylib load command (LC_LOAD_DYLIB, weak, re-export, upward, lazy) and
 * LC_ID_DYLIB whose path is absolute and whose basename is one of the -l
 * names is rewritten to "@loader_path/<basename>". Prints the path of every
 * file that references one of those libraries by @loader_path afterwards -
 * the ones it changed AND ones already relinked - one per line: the caller
 * re-signs each and links the libraries beside it. That matters because the
 * PS2DEV archive ships hard-linked twins (ee/bin/<triple>-as and
 * ee/<triple>/bin/as are one inode): the first is rewritten through one name,
 * codesign then writes a NEW file for that name, and the other name is left
 * with the patched bytes and a dead signature (the kernel kills it on exec,
 * "Killed: 9"). Files with no match, or no Mach-O at all, are left
 * byte-identical. Thin and fat (universal) files both work.
 *
 * WHY THIS EXISTS (tools/toolchain/setup.sh, docs/native-toolchain.md#macos):
 * the macOS PS2DEV archive's gcc/binutils load gmp, mpfr, mpc and zstd from
 * /opt/homebrew/opt/... (Apple silicon) or /usr/local/opt/... (Intel). A
 * TyraX.app must build games on a Mac with neither Homebrew nor the Xcode
 * Command Line Tools - and install_name_tool and otool are xcode-select shims
 * that only offer to install those. So the toolchain setup relinks with this
 * instead, hard-links the four libraries beside each patched binary, and
 * re-signs it ad hoc with codesign (a real system tool).
 *
 * The new name always fits: "@loader_path/libmpfr.6.dylib" is 28 characters,
 * and every original ("/usr/local/opt/gmp/lib/libgmp.10.dylib" is the
 * shortest, 39) is longer - so the load command keeps its size and nothing
 * else in the file moves. A name that would not fit is reported and the file
 * is left alone (exit status 1).
 */
#include <mach-o/fat.h>
#include <mach-o/loader.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *g_names[16];
static int g_nameCount;
static int g_failed;

static uint32_t swap32(uint32_t v) { return __builtin_bswap32(v); }
static uint64_t swap64(uint64_t v) { return __builtin_bswap64(v); }

static const char *baseName(const char *p) {
    const char *s = strrchr(p, '/');
    return s ? s + 1 : p;
}

static int wanted(const char *base) {
    for (int i = 0; i < g_nameCount; ++i)
        if (strcmp(base, g_names[i]) == 0) return 1;
    return 0;
}

/* Patches one thin 64-bit image at buf[0..size). Returns the number of names
 * rewritten; *refs counts the wanted names it references by @loader_path
 * afterwards (rewritten or found that way). Only little-endian 64-bit images exist on macOS 11+ (arm64,
 * x86_64); anything else is skipped untouched. */
static int patchImage(unsigned char *buf, size_t size, const char *file, int *refs) {
    if (size < sizeof(struct mach_header_64)) return 0;
    struct mach_header_64 h;
    memcpy(&h, buf, sizeof h);
    if (h.magic != MH_MAGIC_64) return 0;
    size_t off = sizeof h;
    const size_t end = off + h.sizeofcmds;
    if (end > size) return 0;
    int changed = 0;
    for (uint32_t i = 0; i < h.ncmds && off + sizeof(struct load_command) <= end; ++i) {
        struct load_command lc;
        memcpy(&lc, buf + off, sizeof lc);
        if (lc.cmdsize < sizeof lc || off + lc.cmdsize > end) return changed;
        switch (lc.cmd) {
            case LC_LOAD_DYLIB:
            case LC_LOAD_WEAK_DYLIB:
            case LC_REEXPORT_DYLIB:
            case LC_LOAD_UPWARD_DYLIB:
            case LC_LAZY_LOAD_DYLIB:
            case LC_ID_DYLIB: {
                struct dylib_command dc;
                if (lc.cmdsize < sizeof dc) break;
                memcpy(&dc, buf + off, sizeof dc);
                const uint32_t nameOff = dc.dylib.name.offset;
                if (nameOff >= lc.cmdsize) break;
                char *name = (char *)buf + off + nameOff;
                const size_t room = lc.cmdsize - nameOff; /* bytes incl. NUL */
                if (strnlen(name, room) == room) break;   /* unterminated */
                const char *base = baseName(name);
                if (!wanted(base)) break;
                if (name[0] != '/') {                     /* already relative */
                    if (strncmp(name, "@loader_path/", 13) == 0) ++*refs;
                    break;
                }
                char repl[1024];
                snprintf(repl, sizeof repl, "@loader_path/%s", base);
                if (strlen(repl) + 1 > room) {
                    fprintf(stderr, "machorelink: %s: no room for %s\n", file, repl);
                    g_failed = 1;
                    break;
                }
                memset(name, 0, room);
                memcpy(name, repl, strlen(repl));
                ++changed;
                ++*refs;
                break;
            }
            default:
                break;
        }
        off += lc.cmdsize;
    }
    return changed;
}

/* Returns how many wanted libraries the file references by @loader_path once
 * done (0 = nothing for the caller to do). */
static int patchFile(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return 0; }
    const long len = ftell(f);
    if (len < 8) { fclose(f); return 0; }
    rewind(f);
    unsigned char *buf = malloc((size_t)len);
    if (!buf || fread(buf, 1, (size_t)len, f) != (size_t)len) {
        free(buf);
        fclose(f);
        return 0;
    }
    fclose(f);

    int changed = 0, refs = 0;
    uint32_t magic;
    memcpy(&magic, buf, 4);
    if (magic == MH_MAGIC_64) {
        changed = patchImage(buf, (size_t)len, path, &refs);
    } else if (magic == FAT_CIGAM || magic == FAT_CIGAM_64) {
        /* Fat headers are big-endian on disk; this tool only runs on
         * little-endian Macs, hence the *_CIGAM magics and the swaps. */
        uint32_t n;
        memcpy(&n, buf + 4, 4);
        n = swap32(n);
        const int is64 = magic == FAT_CIGAM_64;
        const size_t entry = is64 ? sizeof(struct fat_arch_64) : sizeof(struct fat_arch);
        for (uint32_t i = 0; i < n && 8 + (i + 1) * entry <= (size_t)len; ++i) {
            const unsigned char *e = buf + 8 + i * entry;
            uint64_t offset, size;
            if (is64) {
                struct fat_arch_64 a;
                memcpy(&a, e, sizeof a);
                offset = swap64(a.offset);
                size = swap64(a.size);
            } else {
                struct fat_arch a;
                memcpy(&a, e, sizeof a);
                offset = swap32(a.offset);
                size = swap32(a.size);
            }
            if (offset + size > (uint64_t)len) continue;
            changed += patchImage(buf + offset, (size_t)size, path, &refs);
        }
    }

    if (changed > 0) {
        f = fopen(path, "r+b");
        if (!f || fwrite(buf, 1, (size_t)len, f) != (size_t)len) {
            fprintf(stderr, "machorelink: cannot write %s\n", path);
            g_failed = 1;
            refs = 0;
        }
        if (f) fclose(f);
    }
    free(buf);
    return refs;
}

int main(int argc, char **argv) {
    int i = 1;
    for (; i + 1 < argc && strcmp(argv[i], "-l") == 0; i += 2) {
        if (g_nameCount == (int)(sizeof g_names / sizeof g_names[0])) {
            fprintf(stderr, "machorelink: too many -l names\n");
            return 2;
        }
        g_names[g_nameCount++] = argv[i + 1];
    }
    if (g_nameCount == 0 || i >= argc) {
        fprintf(stderr, "usage: machorelink -l <libname> [-l <libname>...] <file>...\n");
        return 2;
    }
    for (; i < argc; ++i)
        if (patchFile(argv[i]) > 0) printf("%s\n", argv[i]);
    return g_failed;
}
