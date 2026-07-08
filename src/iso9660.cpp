#include "iso9660.hpp"

#include <algorithm>
#include <cstring>
#include <ctime>
#include <fstream>
#include <map>

namespace fs = std::filesystem;

namespace iso9660 {

constexpr uint32_t SECTOR = 2048;

using Sector = std::vector<uint8_t>;  // always SECTOR bytes

static void put16LE(uint8_t* p, uint16_t v) {
    p[0] = v & 0xff;
    p[1] = v >> 8;
}
static void put16BE(uint8_t* p, uint16_t v) {
    p[0] = v >> 8;
    p[1] = v & 0xff;
}
static void put32LE(uint8_t* p, uint32_t v) {
    p[0] = v & 0xff;
    p[1] = (v >> 8) & 0xff;
    p[2] = (v >> 16) & 0xff;
    p[3] = v >> 24;
}
static void put32BE(uint8_t* p, uint32_t v) {
    p[0] = v >> 24;
    p[1] = (v >> 16) & 0xff;
    p[2] = (v >> 8) & 0xff;
    p[3] = v & 0xff;
}
// ISO9660 "both byte orders" fields: little-endian copy then big-endian copy.
static void putBoth16(uint8_t* p, uint16_t v) {
    put16LE(p, v);
    put16BE(p + 2, v);
}
static void putBoth32(uint8_t* p, uint32_t v) {
    put32LE(p, v);
    put32BE(p + 4, v);
}
static void putPadded(uint8_t* p, size_t len, const std::string& s) {
    memset(p, ' ', len);
    memcpy(p, s.data(), std::min(len, s.size()));
}

struct File {
    fs::path source;
    std::string isoPath;   // as passed in
    std::string id;        // "USE.PNG;1"
    int dir = 0;           // owning directory index
    uint32_t lba = 0, size = 0;
};

struct Dir {
    std::string id;             // "HUD" (root: empty)
    int parent = 0;             // index into dirs; root points at itself
    std::vector<int> subdirs;   // indices into dirs
    std::vector<int> files;     // indices into files
    uint32_t lba = 0, size = 0; // extent (size = multiple of SECTOR)
    uint16_t number = 0;        // 1-based path table number
};

// A directory record; identifiers "\0" and "\1" encode '.' and '..'.
static void appendRecord(std::vector<uint8_t>& out, const std::string& id, uint32_t lba,
                         uint32_t size, bool isDir, const std::tm& tm) {
    const size_t idLen = id.size();
    const size_t len = 33 + idLen + (idLen % 2 == 0 ? 1 : 0);

    // Records must not straddle a sector boundary - pad to the next sector.
    const size_t used = out.size() % SECTOR;
    if (used + len > SECTOR) out.resize(out.size() + (SECTOR - used), 0);

    std::vector<uint8_t> r(len, 0);
    r[0] = (uint8_t)len;
    putBoth32(&r[2], lba);
    putBoth32(&r[10], size);
    r[18] = (uint8_t)(tm.tm_year);
    r[19] = (uint8_t)(tm.tm_mon + 1);
    r[20] = (uint8_t)tm.tm_mday;
    r[21] = (uint8_t)tm.tm_hour;
    r[22] = (uint8_t)tm.tm_min;
    r[23] = (uint8_t)tm.tm_sec;
    r[24] = 0;  // GMT offset
    r[25] = isDir ? 0x02 : 0x00;
    putBoth16(&r[28], 1);  // volume sequence number
    r[32] = (uint8_t)idLen;
    memcpy(&r[33], id.data(), idLen);
    out.insert(out.end(), r.begin(), r.end());
}

// Everything write() decides before emitting a single byte; plan() stops here.
struct Prepared {
    std::vector<Dir> dirs;
    std::vector<File> files;
    std::vector<int> ptOrder;  // directory indices in path table order
    uint32_t ptSize = 0, ptSectors = 0, ptL = 0, ptM = 0, totalSectors = 0;
    std::tm now{};
};

// Renders one directory extent. Record lengths depend only on identifiers,
// so this doubles as the size dry-run during layout planning (LBAs still 0).
static std::vector<uint8_t> renderDir(const Prepared& img, const Dir& d) {
    std::vector<uint8_t> out;
    appendRecord(out, std::string(1, '\0'), d.lba, d.size, true, img.now);
    const Dir& par = img.dirs[d.parent];
    appendRecord(out, std::string(1, '\1'), par.lba, par.size, true, img.now);
    // '.'/'..' first, then all entries sorted by identifier
    std::vector<std::pair<std::string, int>> entries;  // id -> +fileIdx / -dirIdx-1
    for (int f : d.files) entries.push_back({img.files[f].id, f});
    for (int s : d.subdirs) entries.push_back({img.dirs[s].id, -s - 1});
    std::sort(entries.begin(), entries.end());
    for (const auto& [id, idx] : entries) {
        if (idx >= 0)
            appendRecord(out, id, img.files[idx].lba, img.files[idx].size, false, img.now);
        else {
            const Dir& sd = img.dirs[-idx - 1];
            appendRecord(out, id, sd.lba, sd.size, true, img.now);
        }
    }
    out.resize((out.size() + SECTOR - 1) / SECTOR * SECTOR, 0);
    return out;
}

static std::string prepare(const std::vector<FileEntry>& files, Prepared& img) {
    if (files.empty()) return "no files to write";

    // --- Build the directory tree --------------------------------------
    std::vector<Dir>& dirs = img.dirs;
    dirs.resize(1);  // [0] = root
    std::vector<File>& fileNodes = img.files;

    for (const auto& fe : files) {
        std::error_code ec;
        if (!fs::is_regular_file(fe.source, ec))
            return "source file missing: " + fe.source.string();

        int cur = 0;
        size_t start = 0;
        const std::string& p = fe.isoPath;
        if (p.empty() || p.front() == '/' || p.find('\\') != std::string::npos)
            return "bad iso path: '" + p + "'";
        while (true) {
            size_t slash = p.find('/', start);
            std::string part = p.substr(start, slash - start);
            if (part.empty()) return "bad iso path: '" + p + "'";
            if (slash == std::string::npos) {
                File f;
                f.source = fe.source;
                f.isoPath = p;
                f.id = part + ";1";
                f.dir = cur;
                std::error_code sec;
                f.size = (uint32_t)fs::file_size(fe.source, sec);
                fileNodes.push_back(f);
                dirs[cur].files.push_back((int)fileNodes.size() - 1);
                break;
            }
            int next = -1;
            for (int d : dirs[cur].subdirs)
                if (dirs[d].id == part) next = d;
            if (next < 0) {
                Dir d;
                d.id = part;
                d.parent = cur;
                dirs.push_back(d);
                next = (int)dirs.size() - 1;
                dirs[cur].subdirs.push_back(next);
            }
            cur = next;
            start = slash + 1;
        }
    }

    // Duplicate identifiers inside one directory (e.g. two files that only
    // differed in case before upper-casing) would produce a broken image.
    for (const auto& d : dirs) {
        std::map<std::string, int> seen;
        for (int f : d.files)
            if (++seen[fileNodes[f].id] > 1)
                return "duplicate name on disc: " + fileNodes[f].isoPath;
        for (int s : d.subdirs)
            if (++seen[dirs[s].id + ";1"] > 1)  // dir vs dir clash only
                return "duplicate directory name on disc: " + dirs[s].id;
    }

    // --- Number directories (path table order: depth, parent, name) ------
    std::vector<int> depth(dirs.size(), 0);
    for (size_t i = 1; i < dirs.size(); ++i) depth[i] = depth[dirs[i].parent] + 1;
    std::vector<int>& ptOrder = img.ptOrder;
    ptOrder.resize(dirs.size());
    for (size_t i = 0; i < dirs.size(); ++i) ptOrder[i] = (int)i;
    std::sort(ptOrder.begin(), ptOrder.end(), [&](int a, int b) {
        if (depth[a] != depth[b]) return depth[a] < depth[b];
        if (dirs[a].parent != dirs[b].parent) return dirs[a].parent < dirs[b].parent;
        return dirs[a].id < dirs[b].id;
    });
    for (size_t i = 0; i < ptOrder.size(); ++i) dirs[ptOrder[i]].number = (uint16_t)(i + 1);

    // Record order inside each directory must be sorted by identifier.
    for (auto& d : dirs) {
        std::sort(d.files.begin(), d.files.end(),
                  [&](int a, int b) { return fileNodes[a].id < fileNodes[b].id; });
        std::sort(d.subdirs.begin(), d.subdirs.end(),
                  [&](int a, int b) { return dirs[a].id < dirs[b].id; });
    }

    // --- Plan the layout --------------------------------------------------
    // Path table size (same for the L and M variants).
    uint32_t& ptSize = img.ptSize;
    for (int di : ptOrder) {
        size_t idLen = (di == 0) ? 1 : dirs[di].id.size();
        ptSize += (uint32_t)(8 + idLen + (idLen % 2));
    }
    img.ptSectors = (ptSize + SECTOR - 1) / SECTOR;

    std::time_t nowT = std::time(nullptr);
#ifdef _WIN32
    localtime_s(&img.now, &nowT);
#else
    img.now = *std::localtime(&nowT);
#endif

    uint32_t lba = 16 + 2;  // system area + PVD + terminator
    img.ptL = lba;
    img.ptM = lba + img.ptSectors;
    lba = img.ptM + img.ptSectors;

    for (int di : ptOrder) {  // directory extents, path-table order
        dirs[di].lba = lba;
        dirs[di].size = (uint32_t)renderDir(img, dirs[di]).size();
        lba += dirs[di].size / SECTOR;
    }
    for (auto& f : fileNodes) {  // file data, input order = layout order
        f.lba = lba;
        lba += (f.size + SECTOR - 1) / SECTOR;
        if (f.size == 0) lba += 1;  // give empty files a real (zeroed) sector
    }
    img.totalSectors = lba;
    return "";
}

std::string plan(const std::vector<FileEntry>& files, PlannedImage* out) {
    Prepared img;
    std::string err = prepare(files, img);
    if (!err.empty()) return err;
    out->files.clear();
    for (const auto& f : img.files) out->files.push_back({f.isoPath, f.lba, f.size});
    out->dataStartLba = img.files.front().lba;
    out->totalSectors = img.totalSectors;
    return "";
}

std::string write(const fs::path& isoFile, const std::string& volumeId,
                  const std::vector<FileEntry>& files, std::vector<PlacedFile>* outPlacement) {
    Prepared img;
    std::string err = prepare(files, img);
    if (!err.empty()) return err;
    std::vector<Dir>& dirs = img.dirs;
    std::vector<File>& fileNodes = img.files;
    const std::vector<int>& ptOrder = img.ptOrder;
    const uint32_t ptSize = img.ptSize, ptSectors = img.ptSectors;
    const uint32_t ptL = img.ptL, ptM = img.ptM, totalSectors = img.totalSectors;
    const std::tm& now = img.now;

    // --- Write the image ---------------------------------------------------
    std::ofstream out(isoFile, std::ios::binary | std::ios::trunc);
    if (!out) return "cannot create " + isoFile.string();

    std::vector<uint8_t> zeros(SECTOR, 0);
    for (int i = 0; i < 16; ++i) out.write((const char*)zeros.data(), SECTOR);

    // Primary Volume Descriptor
    {
        std::vector<uint8_t> pvd(SECTOR, 0);
        pvd[0] = 1;
        memcpy(&pvd[1], "CD001", 5);
        pvd[6] = 1;
        putPadded(&pvd[8], 32, "PLAYSTATION");
        putPadded(&pvd[40], 32, volumeId);
        putBoth32(&pvd[80], totalSectors);
        putBoth16(&pvd[120], 1);       // volume set size
        putBoth16(&pvd[124], 1);       // volume sequence number
        putBoth16(&pvd[128], SECTOR);  // logical block size
        putBoth32(&pvd[132], ptSize);
        put32LE(&pvd[140], ptL);
        put32BE(&pvd[148], ptM);
        {
            std::vector<uint8_t> root;
            appendRecord(root, std::string(1, '\0'), dirs[0].lba, dirs[0].size, true, now);
            memcpy(&pvd[156], root.data(), 34);
        }
        putPadded(&pvd[190], 128, "");             // volume set id
        putPadded(&pvd[318], 128, "TYRA-EDITOR");  // publisher
        putPadded(&pvd[446], 128, "TYRA-EDITOR");  // data preparer
        putPadded(&pvd[574], 128, "PLAYSTATION");  // application id
        putPadded(&pvd[702], 37, "");
        putPadded(&pvd[739], 37, "");
        putPadded(&pvd[776], 37, "");
        char date[18];
        snprintf(date, sizeof(date), "%04d%02d%02d%02d%02d%02d00", now.tm_year + 1900,
                 now.tm_mon + 1, now.tm_mday, now.tm_hour, now.tm_min, now.tm_sec);
        memcpy(&pvd[813], date, 16);  // creation
        memcpy(&pvd[830], date, 16);  // modification
        memset(&pvd[847], '0', 16);   // expiration: none
        memset(&pvd[864], '0', 16);   // effective: none
        pvd[881] = 1;                 // file structure version
        out.write((const char*)pvd.data(), SECTOR);
    }
    // Volume Descriptor Set Terminator
    {
        std::vector<uint8_t> term(SECTOR, 0);
        term[0] = 255;
        memcpy(&term[1], "CD001", 5);
        term[6] = 1;
        out.write((const char*)term.data(), SECTOR);
    }

    // Path tables (L: little-endian fields, M: big-endian)
    auto writePathTable = [&](bool bigEndian) {
        std::vector<uint8_t> pt;
        for (int di : ptOrder) {
            const Dir& d = dirs[di];
            std::string id = (di == 0) ? std::string(1, '\0') : d.id;
            std::vector<uint8_t> r(8 + id.size() + (id.size() % 2), 0);
            r[0] = (uint8_t)id.size();
            if (bigEndian) {
                put32BE(&r[2], d.lba);
                put16BE(&r[6], dirs[d.parent].number);
            } else {
                put32LE(&r[2], d.lba);
                put16LE(&r[6], dirs[d.parent].number);
            }
            memcpy(&r[8], id.data(), id.size());
            pt.insert(pt.end(), r.begin(), r.end());
        }
        pt.resize(ptSectors * SECTOR, 0);
        out.write((const char*)pt.data(), pt.size());
    };
    writePathTable(false);
    writePathTable(true);

    // Directory extents
    for (int di : ptOrder) {
        auto data = renderDir(img, dirs[di]);
        out.write((const char*)data.data(), data.size());
    }

    // File data, sector-padded, in the caller's order
    for (const auto& f : fileNodes) {
        std::ifstream in(f.source, std::ios::binary);
        if (!in) return "cannot read " + f.source.string();
        char buf[65536];
        uint32_t written = 0;
        while (in.read(buf, sizeof(buf)) || in.gcount() > 0) {
            out.write(buf, in.gcount());
            written += (uint32_t)in.gcount();
        }
        if (written != f.size) return "file changed during write: " + f.source.string();
        const uint32_t pad = (SECTOR - written % SECTOR) % SECTOR + (written == 0 ? SECTOR : 0);
        out.write((const char*)zeros.data(), pad);
    }

    out.flush();
    if (!out) return "write failed: " + isoFile.string();

    if (outPlacement) {
        outPlacement->clear();
        for (const auto& f : fileNodes)
            outPlacement->push_back({f.isoPath, f.lba, f.size});
    }
    return "";
}

}  // namespace iso9660
