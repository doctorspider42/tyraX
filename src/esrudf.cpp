#include "esrudf.hpp"

#include <array>
#include <cstring>
#include <fstream>
#include <vector>

namespace fs = std::filesystem;

namespace esrudf {

namespace {

constexpr uint32_t SECTOR = 2048;

// esrtool's CRC-16/CCITT table + the fake DVD-Video partition (see header).
#include "esrudf_data.inc"

// --- little-endian scalar writers ----------------------------------------
void put16(uint8_t* p, uint16_t v) {
    p[0] = v & 0xff;
    p[1] = (v >> 8) & 0xff;
}
void put32(uint8_t* p, uint32_t v) {
    p[0] = v & 0xff;
    p[1] = (v >> 8) & 0xff;
    p[2] = (v >> 16) & 0xff;
    p[3] = (v >> 24) & 0xff;
}

// CRC-16/CCITT (init 0, poly 0x1021) - the UDF tag CRC. Ported byte-for-byte
// from esrtool so a tag we build here and one esrtool would recompute match.
uint16_t udfCrc(const uint8_t* b, size_t n) {
    uint16_t crc = 0;
    for (size_t i = 0; i < n; ++i) {
        uint8_t lo = crc & 0xff, hi = (crc >> 8) & 0xff;
        crc = (uint16_t)lo << 8;
        crc ^= CRC_LOOKUP[hi ^ b[i]];
    }
    return crc;
}

// Fills a 16-byte UDF descriptor tag (ECMA-167 1/7.2): identifier, version 2,
// CRC over the crcLen bytes that follow the tag, the tag location, and finally
// the tag checksum (sum of the other 15 header bytes).
void setTag(uint8_t* s, uint16_t tagId, uint32_t tagLoc, uint16_t crcLen) {
    put16(s + 0, tagId);
    put16(s + 2, 2);  // descriptor version (UDF <= 2.00, as on DVD-Video)
    s[4] = 0;         // tag checksum, computed last
    s[5] = 0;         // reserved
    put16(s + 6, 0);  // tag serial number
    put16(s + 8, udfCrc(s + 16, crcLen));
    put16(s + 10, crcLen);
    put32(s + 12, tagLoc);
    uint8_t sum = 0;
    for (int i = 0; i < 16; ++i) sum += s[i];
    sum -= s[4];
    s[4] = sum;
}

// UDF dstring (1/7.2.12): 8-bit compression id, then the ASCII bytes, with the
// used length (compression id included) in the last byte of the field.
void dstring(uint8_t* d, size_t n, const std::string& s) {
    memset(d, 0, n);
    if (s.empty()) return;
    d[0] = 8;
    size_t k = std::min(s.size(), n - 2);
    memcpy(d + 1, s.data(), k);
    d[n - 1] = (uint8_t)(k + 1);
}

// UDF character set specification (1/7.2.1): CS0 = OSTA Compressed Unicode.
void charspec(uint8_t* d) {
    memset(d, 0, 64);
    memcpy(d + 1, "OSTA Compressed Unicode", 23);
}

// UDF entity identifier (1/7.4): flags, 23-byte identifier, 8-byte suffix.
void regid(uint8_t* d, const char* id, const uint8_t suffix[8]) {
    memset(d, 0, 32);
    memcpy(d + 1, id, std::min<size_t>(strlen(id), 23));
    if (suffix) memcpy(d + 24, suffix, 8);
}
// Suffix for "*OSTA UDF Compliant": UDF revision 1.02 + domain flags.
const uint8_t kDomainSuffix[8] = {0x02, 0x01, 0x00, 0, 0, 0, 0, 0};
// Suffix for UDF-defined ids ("*UDF LV Info"): revision + OS class/id.
const uint8_t kUdfSuffix[8] = {0x02, 0x01, 0x00, 0x00, 0, 0, 0, 0};
// Suffix for developer ids ("*TyraX ESR"): OS class/id + impl use.
const uint8_t kImplSuffix[8] = {0, 0, 0, 0, 0, 0, 0, 0};
const char* kImplId = "*TyraX ESR";

// A recording timestamp (1/7.3). Fixed so images are reproducible.
void timestamp(uint8_t* d) {
    memset(d, 0, 12);
    put16(d + 0, 0x1000);  // type = local time, offset 0
    put16(d + 2, 2024);    // year
    d[4] = 1;              // month
    d[5] = 1;              // day
}

// --- descriptor builders (each returns one 2048-byte sector) --------------
using Buf = std::array<uint8_t, SECTOR>;

// Volume Structure Descriptor for the volume-recognition sequence (2/9.1).
Buf vrs(const char* id) {
    Buf s{};
    s[0] = 0;  // structure type
    memcpy(&s[1], id, 5);
    s[6] = 1;  // structure version
    return s;
}

// Primary Volume Descriptor (3/10.1). 512 bytes, CRC over 496.
Buf pvd(uint32_t tagLoc) {
    Buf s{};
    uint8_t* p = s.data();
    put32(p + 16, 0);  // volume descriptor sequence number
    put32(p + 20, 0);  // primary volume descriptor number
    dstring(p + 24, 32, "DVDVIDEO");
    put16(p + 56, 1);  // volume sequence number
    put16(p + 58, 1);  // maximum volume sequence number
    put16(p + 60, 2);  // interchange level
    put16(p + 62, 3);  // maximum interchange level
    put32(p + 64, 1);  // character set list
    put32(p + 68, 1);  // maximum character set list
    dstring(p + 72, 128, "DVDVIDEO");
    charspec(p + 200);  // descriptor character set
    charspec(p + 264);  // explanatory character set
    timestamp(p + 376);
    regid(p + 388, kImplId, kImplSuffix);
    setTag(p, 1, tagLoc, 496);
    return s;
}

// Implementation Use Volume Descriptor (3/10.4) - "*UDF LV Info". Present only
// to occupy the sector between the PVD and the partition descriptor, so the
// latter lands on the LBA esrtool expects (34 main / 50 reserve).
Buf iuvd(uint32_t tagLoc) {
    Buf s{};
    uint8_t* p = s.data();
    put32(p + 16, 1);  // volume descriptor sequence number
    regid(p + 20, "*UDF LV Info", kUdfSuffix);
    charspec(p + 52);            // LVI character set
    dstring(p + 116, 128, "DVDVIDEO");  // logical volume identifier
    regid(p + 352, kImplId, kImplSuffix);
    setTag(p, 4, tagLoc, 496);
    return s;
}

// Partition Descriptor (3/10.5). Partition contents "+NSR02" is what esrtool
// reads back at byte 25 to recognise a patched disc. Starting location + length
// point at the fake DVD-Video partition; the ESR patch re-stamps them anyway.
Buf pd(uint32_t tagLoc) {
    Buf s{};
    uint8_t* p = s.data();
    put32(p + 16, 2);           // volume descriptor sequence number
    put16(p + 20, 1);           // partition flags: allocated
    put16(p + 22, 0);           // partition number
    regid(p + 24, "+NSR02", nullptr);  // partition contents
    put32(p + 184, 1);          // access type: read-only
    put32(p + 188, 128);        // partition starting location (= fake DVD data)
    put32(p + 192, 12);         // partition length (fake DVD data is 12 sectors)
    regid(p + 196, kImplId, kImplSuffix);
    setTag(p, 5, tagLoc, 496);
    return s;
}

// Logical Volume Descriptor (3/10.6). 446 bytes (440 + one type-1 map), CRC 430.
Buf lvd(uint32_t tagLoc) {
    Buf s{};
    uint8_t* p = s.data();
    put32(p + 16, 3);  // volume descriptor sequence number
    charspec(p + 20);
    dstring(p + 84, 128, "DVDVIDEO");
    put32(p + 212, SECTOR);  // logical block size
    regid(p + 216, "*OSTA UDF Compliant", kDomainSuffix);
    // Logical volume contents use: long_ad of the File Set Descriptor at
    // partition block 0 (= LBA 128 after the partition is repointed there).
    put32(p + 248, SECTOR);  // extent length
    put32(p + 252, 0);       // logical block number within the partition
    put16(p + 256, 0);       // partition reference number
    put32(p + 264, 6);       // map table length (one type-1 map)
    put32(p + 268, 1);       // number of partition maps
    regid(p + 272, kImplId, kImplSuffix);
    put32(p + 432, 2 * SECTOR);  // integrity sequence extent length (2 sectors)
    put32(p + 436, 64);          // integrity sequence extent location
    // Partition map 1 (type 1): type, length, volume seq number, partition num.
    p[440] = 1;
    p[441] = 6;
    put16(p + 442, 1);
    put16(p + 444, 0);
    setTag(p, 6, tagLoc, 430);
    return s;
}

// Unallocated Space Descriptor (3/10.8), empty. 24 bytes, CRC 8.
Buf usd(uint32_t tagLoc) {
    Buf s{};
    uint8_t* p = s.data();
    put32(p + 16, 4);  // volume descriptor sequence number
    put32(p + 20, 0);  // number of allocation descriptors
    setTag(p, 7, tagLoc, 8);
    return s;
}

// Terminating Descriptor (3/10.9). 512 bytes, CRC 496.
Buf td(uint32_t tagLoc) {
    Buf s{};
    setTag(s.data(), 8, tagLoc, 496);
    return s;
}

// Anchor Volume Descriptor Pointer (3/10.2). Points at the main + reserve
// volume-descriptor sequences (16 sectors each at LBA 32 / 48).
Buf avdp(uint32_t tagLoc) {
    Buf s{};
    uint8_t* p = s.data();
    put32(p + 16, 16 * SECTOR);  // main VDS extent length
    put32(p + 20, 32);           // main VDS extent location
    put32(p + 24, 16 * SECTOR);  // reserve VDS extent length
    put32(p + 28, 48);           // reserve VDS extent location
    setTag(p, 2, tagLoc, 496);
    return s;
}

// Logical Volume Integrity Descriptor (3/10.10) for a closed, read-only
// volume with one partition. 134 bytes, CRC 118.
Buf lvid(uint32_t tagLoc) {
    Buf s{};
    uint8_t* p = s.data();
    timestamp(p + 16);
    put32(p + 28, 1);   // integrity type: close
    // next integrity extent (32..39) = none
    put32(p + 40, 16);  // logical volume header descriptor: next unique id
    put32(p + 72, 1);   // number of partitions
    put32(p + 76, 46);  // length of implementation use
    put32(p + 80, 0);   // free space table[0]
    put32(p + 84, 12);  // size table[0] = partition length
    regid(p + 88, kImplId, kImplSuffix);
    put32(p + 120, 3);      // number of files
    put32(p + 124, 2);      // number of directories
    put16(p + 128, 0x0102);  // minimum UDF read revision
    put16(p + 130, 0x0102);  // minimum UDF write revision
    put16(p + 132, 0x0102);  // maximum UDF write revision
    setTag(p, 9, tagLoc, 118);
    return s;
}

// --- random-access sector I/O over the ISO --------------------------------
struct SectorFile {
    std::fstream f;
    std::string err;

    explicit SectorFile(const fs::path& path) {
        f.open(path, std::ios::binary | std::ios::in | std::ios::out);
        if (!f) err = "cannot open for patching: " + path.string();
    }
    void write(uint32_t lba, const uint8_t* data, size_t n) {
        f.seekp((std::streamoff)lba * SECTOR);
        f.write((const char*)data, n);
    }
    std::vector<uint8_t> read(uint32_t lba) {
        std::vector<uint8_t> b(SECTOR);
        f.seekg((std::streamoff)lba * SECTOR);
        f.read((char*)b.data(), SECTOR);
        return b;
    }
};

void writeBridge(SectorFile& io, uint32_t totalSectors) {
    io.write(18, vrs("BEA01").data(), SECTOR);
    io.write(19, vrs("NSR02").data(), SECTOR);
    io.write(20, vrs("TEA01").data(), SECTOR);
    // Main VDS at 32, reserve at 48 - identical but for the tag location.
    for (uint32_t base : {32u, 48u}) {
        io.write(base + 0, pvd(base + 0).data(), SECTOR);
        io.write(base + 1, iuvd(base + 1).data(), SECTOR);
        io.write(base + 2, pd(base + 2).data(), SECTOR);
        io.write(base + 3, lvd(base + 3).data(), SECTOR);
        io.write(base + 4, usd(base + 4).data(), SECTOR);
        io.write(base + 5, td(base + 5).data(), SECTOR);
    }
    io.write(64, lvid(64).data(), SECTOR);
    io.write(65, td(65).data(), SECTOR);
    io.write(256, avdp(256).data(), SECTOR);
    io.write(totalSectors - 1, avdp(totalSectors - 1).data(), SECTOR);
}

// --- ESR patch (ported from esrtool lib.rs, ali-raheem, MIT) --------------
bool checkUdf(SectorFile& io) {
    for (uint32_t i = 17; i < 80; ++i) {
        auto s = io.read(i);
        if (s[1] == 'N' && s[2] == 'S' && s[3] == 'R') return true;
    }
    return false;
}
bool checkPatched(SectorFile& io) {
    auto s = io.read(14);
    return s[25] == '+' && s[26] == 'N' && s[27] == 'S' && s[28] == 'R';
}
void copyLba(SectorFile& io, uint32_t src, uint32_t dst) {
    auto b = io.read(src);
    io.write(dst, b.data(), SECTOR);
}
void patchLba(SectorFile& io, uint32_t lba) {
    auto s = io.read(lba);
    s[188] = 128;  // partition starting location -> fake DVD-Video partition
    s[189] = 0;
    uint16_t crcLen = (uint16_t)(s[10] | (s[11] << 8));
    uint16_t crc = udfCrc(s.data() + 16, crcLen);
    s[8] = crc & 0xff;
    s[9] = (crc >> 8) & 0xff;
    uint8_t checksum = 0;
    for (int i = 0; i < 16; ++i) checksum += s[i];
    checksum -= s[4];
    s[4] = checksum;
    io.write(lba, s.data(), SECTOR);
}

}  // namespace

std::string makeEsrCompatible(const fs::path& isoFile, uint32_t totalSectors, const LogFn& log) {
    if (totalSectors < 258) return "image too small for an ESR UDF bridge";
    SectorFile io(isoFile);
    if (!io.err.empty()) return io.err;

    writeBridge(io, totalSectors);
    if (!checkUdf(io)) return "internal error: UDF bridge not recognised after write";

    // ESR patch, mirroring esrtool: back the partition descriptors up into the
    // reserved sectors, repoint them at the fake partition, then drop it in.
    copyLba(io, 34, 14);
    copyLba(io, 50, 15);
    patchLba(io, 34);
    patchLba(io, 50);
    io.write(128, DVD_DATA, sizeof(DVD_DATA));

    io.f.flush();
    if (!io.f) return "write failed while patching " + isoFile.string();
    if (!checkPatched(io)) return "internal error: disc did not verify as ESR-patched";

    if (log) {
        log("[editor] UDF bridge + ESR patch applied (based on esrtool by "
            "ali-raheem, MIT).");
        log("[editor] Partition descriptors (LBA 34/50) repointed to the fake "
            "DVD-Video partition at LBA 128; originals backed up to LBA 14/15.");
    }
    return "";
}

}  // namespace esrudf
