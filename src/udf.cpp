#include "udf.hpp"

#include <array>
#include <cstring>
#include <ctime>
#include <fstream>
#include <vector>

namespace fs = std::filesystem;

namespace udf {
namespace {

// --- Fixed homes for the volume structures ------------------------------
// Sectors 0..15 are the system area and 16..17 the ISO9660 descriptors, so
// the UDF Volume Recognition Sequence continues the same sequence at 18.
constexpr uint32_t kVrsLba = 18;         // BEA01, NSR02, TEA01
constexpr uint32_t kMainVdsLba = 32;     // 16 sectors
constexpr uint32_t kReserveVdsLba = 48;  // 16 sectors
constexpr uint32_t kVdsSectors = 16;
constexpr uint32_t kLvidLba = 64;    // + a terminator at 65
constexpr uint32_t kAnchorLba = 256; // the second anchor goes on the last sector

// Descriptor tag identifiers (ECMA-167 3/7.2.1 and 4/7.2.1).
enum : uint16_t {
    kTagPrimaryVolume = 1,
    kTagAnchor = 2,
    kTagPartition = 5,
    kTagLogicalVolume = 6,
    kTagUnallocatedSpace = 7,
    kTagTerminating = 8,
    kTagLogicalVolumeIntegrity = 9,
    kTagImplUseVolume = 4,
    kTagFileSet = 256,
    kTagFileIdentifier = 257,
    kTagFileEntry = 261,
};

enum : uint8_t { kFileTypeDirectory = 4, kFileTypeRegular = 5 };

using Sector = std::array<uint8_t, SECTOR>;

void put16(uint8_t* p, uint16_t v) {
    p[0] = (uint8_t)(v & 0xff);
    p[1] = (uint8_t)(v >> 8);
}
void put32(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)(v & 0xff);
    p[1] = (uint8_t)((v >> 8) & 0xff);
    p[2] = (uint8_t)((v >> 16) & 0xff);
    p[3] = (uint8_t)(v >> 24);
}
void put64(uint8_t* p, uint64_t v) {
    put32(p, (uint32_t)(v & 0xffffffffu));
    put32(p + 4, (uint32_t)(v >> 32));
}

// CRC-16/CCITT (polynomial 0x1021, zero seed) - the descriptor CRC of
// ECMA-167 7.2.6. The table is derived here rather than embedded so this file
// carries no third-party data.
uint16_t crc16(const uint8_t* data, size_t len) {
    static const std::array<uint16_t, 256> table = [] {
        std::array<uint16_t, 256> t{};
        for (int i = 0; i < 256; ++i) {
            uint16_t c = (uint16_t)(i << 8);
            for (int b = 0; b < 8; ++b)
                c = (uint16_t)((c & 0x8000) ? ((c << 1) ^ 0x1021) : (c << 1));
            t[(size_t)i] = c;
        }
        return t;
    }();
    uint16_t crc = 0;
    for (size_t i = 0; i < len; ++i)
        crc = (uint16_t)((crc << 8) ^ table[(size_t)(((crc >> 8) ^ data[i]) & 0xff)]);
    return crc;
}

// Stamps the 16-byte descriptor tag once the descriptor body is complete:
// CRC over the bytes after the tag, then the checksum over the tag itself.
//
// `lba` is the descriptor's OWN address, and which address that is depends on
// where it lives: volume structures (anchors, the VDS, the LVID) sit outside
// any partition and use the absolute sector, while everything inside the
// partition - the File Set Descriptor, File Entries, File Identifiers - uses
// the block number RELATIVE to the partition start. Readers check the field
// against the address they used to reach the descriptor, so getting this wrong
// makes the descriptor invisible even though its CRC is perfect (Linux's
// udf_read_ptagged passes the logical block, not the physical one).
void finishTag(uint8_t* d, uint16_t id, uint32_t lba, size_t descriptorLen) {
    put16(d + 0, id);
    put16(d + 2, 2);  // descriptor version (UDF 1.02 uses 2)
    d[4] = 0;         // checksum, filled in below
    d[5] = 0;
    put16(d + 6, 1);  // tag serial number
    const uint16_t crcLen = (uint16_t)(descriptorLen - 16);
    put16(d + 8, crc16(d + 16, crcLen));
    put16(d + 10, crcLen);
    put32(d + 12, lba);
    uint8_t sum = 0;
    for (int i = 0; i < 16; ++i)
        if (i != 4) sum = (uint8_t)(sum + d[i]);
    d[4] = sum;
}

// OSTA CS0 "dstring" (UDF 2.1.3): a compression byte, the characters, and the
// used length in the LAST byte of the fixed-size field. All-zero means empty.
void putDstring(uint8_t* p, size_t fieldLen, const std::string& s) {
    memset(p, 0, fieldLen);
    if (s.empty()) return;
    const size_t maxChars = fieldLen - 2;  // compression byte + trailing length
    const size_t n = s.size() < maxChars ? s.size() : maxChars;
    p[0] = 8;  // 8-bit characters
    memcpy(p + 1, s.data(), n);
    p[fieldLen - 1] = (uint8_t)(n + 1);
}

// charspec (ECMA-167 1/7.2.1): CS0, named by a fixed string.
void putCharspec(uint8_t* p) {
    memset(p, 0, 64);
    p[0] = 0;  // CS0
    memcpy(p + 1, "OSTA Compressed Unicode", 23);
}

// regid / EntityID (ECMA-167 1/7.4).
void putRegid(uint8_t* p, const char* id, const uint8_t* suffix = nullptr,
              size_t suffixLen = 0) {
    memset(p, 0, 32);
    p[0] = 0;  // flags
    memcpy(p + 1, id, strlen(id));
    if (suffix && suffixLen) memcpy(p + 24, suffix, suffixLen);
}

// The suffix every "*OSTA UDF Compliant" identifier carries: the UDF revision
// this image claims (1.02), then the domain flags (0 = no restrictions).
void putUdfDomainRegid(uint8_t* p, const char* id) {
    const uint8_t suffix[4] = {0x02, 0x01, 0x00, 0x00};  // UDF 1.02, flags 0
    putRegid(p, id, suffix, sizeof(suffix));
}

// The editor's own identifier, stamped into the "who wrote this" fields.
void putImplRegid(uint8_t* p) {
    const uint8_t suffix[2] = {0x00, 0x00};  // OS class / OS identifier: unset
    putRegid(p, "*TyraX Editor", suffix, sizeof(suffix));
}

// timestamp (ECMA-167 1/7.3). Local time, with the type field set to "local".
void putTimestamp(uint8_t* p, const std::tm& t) {
    memset(p, 0, 12);
    put16(p + 0, 0x1000);  // type 1 (local time), zero offset
    put16(p + 2, (uint16_t)(t.tm_year + 1900));
    p[4] = (uint8_t)(t.tm_mon + 1);
    p[5] = (uint8_t)t.tm_mday;
    p[6] = (uint8_t)t.tm_hour;
    p[7] = (uint8_t)t.tm_min;
    p[8] = (uint8_t)(t.tm_sec > 59 ? 59 : t.tm_sec);
}

// long_ad (ECMA-167 4/14.14.2): length + (block, partition) + implementation use.
void putLongAd(uint8_t* p, uint32_t length, uint32_t block) {
    memset(p, 0, 16);
    put32(p + 0, length);
    put32(p + 4, block);
    put16(p + 8, 0);  // partition reference number
}

// short_ad (ECMA-167 4/14.14.1). The top two bits of the length are the extent
// type; 0 = recorded and allocated, which is all a read-only image needs.
void putShortAd(uint8_t* p, uint32_t length, uint32_t block) {
    put32(p + 0, length);
    put32(p + 4, block);
}

// extent_ad (ECMA-167 3/7.1).
void putExtentAd(uint8_t* p, uint32_t length, uint32_t location) {
    put32(p + 0, length);
    put32(p + 4, location);
}

// icbtag (ECMA-167 4/14.6). Strategy 4, one entry, short_ad allocation
// descriptors - the plain shape every reader supports.
void putIcbTag(uint8_t* p, uint8_t fileType, uint32_t parentBlock) {
    memset(p, 0, 20);
    put32(p + 0, 0);   // prior recorded number of direct entries
    put16(p + 4, 4);   // strategy type
    put16(p + 8, 1);   // maximum number of entries
    p[11] = fileType;
    put32(p + 12, parentBlock);
    put16(p + 16, 0);  // parent partition reference number
    put16(p + 18, 0);  // flags: short_ad, no bits set
}

// Read + execute for owner, group and other (ECMA-167 4/14.9.5).
constexpr uint32_t kPermissions = 0x14A5;

// --- Descriptor builders ------------------------------------------------

Sector makeVrs(const char* standardId) {
    Sector s{};
    s[0] = 0;  // structure type
    memcpy(&s[1], standardId, 5);
    s[6] = 1;  // structure version
    return s;
}

Sector makeAnchor(uint32_t lba) {
    Sector s{};
    putExtentAd(&s[16], kVdsSectors * SECTOR, kMainVdsLba);
    putExtentAd(&s[24], kVdsSectors * SECTOR, kReserveVdsLba);
    finishTag(s.data(), kTagAnchor, lba, 32);
    return s;
}

Sector makeTerminating(uint32_t lba) {
    Sector s{};
    finishTag(s.data(), kTagTerminating, lba, 512);
    return s;
}

Sector makePrimaryVolume(uint32_t lba, const std::string& volumeId, const std::tm& now) {
    Sector s{};
    put32(&s[16], 0);  // volume descriptor sequence number
    put32(&s[20], 0);  // primary volume descriptor number
    putDstring(&s[24], 32, volumeId);
    put16(&s[56], 1);  // volume sequence number
    put16(&s[58], 1);  // maximum volume sequence number
    put16(&s[60], 2);  // interchange level
    put16(&s[62], 2);  // maximum interchange level
    put32(&s[64], 1);  // character set list
    put32(&s[68], 1);  // maximum character set list
    putDstring(&s[72], 128, volumeId);
    putCharspec(&s[200]);  // descriptor character set
    putCharspec(&s[264]);  // explanatory character set
    putExtentAd(&s[328], 0, 0);  // volume abstract
    putExtentAd(&s[336], 0, 0);  // volume copyright notice
    putRegid(&s[344], "");       // application identifier
    putTimestamp(&s[376], now);
    putImplRegid(&s[388]);
    put32(&s[484], 0);  // predecessor VDS location
    put16(&s[488], 0);  // flags
    finishTag(s.data(), kTagPrimaryVolume, lba, 512);
    return s;
}

Sector makeImplUseVolume(uint32_t lba, const std::string& volumeId) {
    Sector s{};
    put32(&s[16], 1);  // volume descriptor sequence number
    putUdfDomainRegid(&s[20], "*UDF LV Info");
    putCharspec(&s[52]);  // LVI character set
    putDstring(&s[116], 128, volumeId);
    putDstring(&s[244], 36, "");  // LVInfo1
    putDstring(&s[280], 36, "");  // LVInfo2
    putDstring(&s[316], 36, "");  // LVInfo3
    putImplRegid(&s[352]);
    finishTag(s.data(), kTagImplUseVolume, lba, 512);
    return s;
}

Sector makePartition(uint32_t lba, uint32_t startLba, uint32_t length) {
    Sector s{};
    put32(&s[16], 2);  // volume descriptor sequence number
    put16(&s[20], 1);  // partition flags: allocated
    put16(&s[22], 0);  // partition number
    putRegid(&s[24], "+NSR02");
    // Partition Contents Use holds a Partition Header Descriptor; every field
    // of it describes a space table, and a read-only partition has none.
    memset(&s[56], 0, 128);
    put32(&s[184], 1);  // access type: read only
    put32(&s[188], startLba);
    put32(&s[192], length);
    putImplRegid(&s[196]);
    finishTag(s.data(), kTagPartition, lba, 512);
    return s;
}

Sector makeLogicalVolume(uint32_t lba, const std::string& volumeId, uint32_t fsdBlock) {
    Sector s{};
    put32(&s[16], 3);  // volume descriptor sequence number
    putCharspec(&s[20]);
    putDstring(&s[84], 128, volumeId);
    put32(&s[212], SECTOR);  // logical block size
    putUdfDomainRegid(&s[216], "*OSTA UDF Compliant");
    // Logical Volume Contents Use: a long_ad locating the File Set Descriptor.
    putLongAd(&s[248], SECTOR, fsdBlock);
    put32(&s[264], 6);  // map table length
    put32(&s[268], 1);  // number of partition maps
    putImplRegid(&s[272]);
    putExtentAd(&s[432], 2 * SECTOR, kLvidLba);  // integrity sequence extent
    // Type 1 partition map (ECMA-167 3/10.7.2).
    s[440] = 1;
    s[441] = 6;
    put16(&s[442], 1);  // volume sequence number
    put16(&s[444], 0);  // partition number
    finishTag(s.data(), kTagLogicalVolume, lba, 446);
    return s;
}

Sector makeUnallocatedSpace(uint32_t lba) {
    Sector s{};
    put32(&s[16], 4);  // volume descriptor sequence number
    put32(&s[20], 0);  // number of allocation descriptors
    finishTag(s.data(), kTagUnallocatedSpace, lba, 24);
    return s;
}

Sector makeLvid(uint32_t lba, uint32_t partitionLength, uint32_t files, uint32_t dirs,
                uint64_t nextUniqueId, const std::tm& now) {
    Sector s{};
    putTimestamp(&s[16], now);
    put32(&s[28], 1);            // integrity type: close
    putExtentAd(&s[32], 0, 0);   // next integrity extent: none
    put64(&s[40], nextUniqueId); // logical volume contents use: next unique ID
    put32(&s[72], 1);            // number of partitions
    put32(&s[76], 46);           // length of implementation use
    put32(&s[80], 0);            // free space table: read-only, nothing free
    put32(&s[84], partitionLength);
    putImplRegid(&s[88]);
    put32(&s[120], files);
    put32(&s[124], dirs);
    put16(&s[128], 0x0102);  // minimum UDF read revision
    put16(&s[130], 0x0102);  // minimum UDF write revision
    put16(&s[132], 0x0102);  // maximum UDF write revision
    finishTag(s.data(), kTagLogicalVolumeIntegrity, lba, 134);
    return s;
}

Sector makeFileSet(uint32_t lba, const std::string& volumeId, uint32_t rootIcbBlock,
                   const std::tm& now) {
    Sector s{};
    putTimestamp(&s[16], now);
    put16(&s[28], 3);  // interchange level
    put16(&s[30], 3);  // maximum interchange level
    put32(&s[32], 1);  // character set list
    put32(&s[36], 1);  // maximum character set list
    put32(&s[40], 0);  // file set number
    put32(&s[44], 0);  // file set descriptor number
    putCharspec(&s[48]);
    putDstring(&s[112], 128, volumeId);
    putCharspec(&s[240]);
    putDstring(&s[304], 32, volumeId);
    putDstring(&s[336], 32, "");  // copyright file identifier
    putDstring(&s[368], 32, "");  // abstract file identifier
    putLongAd(&s[400], SECTOR, rootIcbBlock);
    putUdfDomainRegid(&s[416], "*OSTA UDF Compliant");
    putLongAd(&s[448], 0, 0);  // next extent: none
    finishTag(s.data(), kTagFileSet, lba, 512);
    return s;
}

// A File Entry with exactly one allocation descriptor - every file and
// directory in this image is a single contiguous extent.
Sector makeFileEntry(uint32_t lba, uint8_t fileType, uint32_t parentBlock,
                     uint64_t infoLength, uint32_t dataBlock, uint16_t linkCount,
                     uint64_t uniqueId, const std::tm& now) {
    Sector s{};
    putIcbTag(&s[16], fileType, parentBlock);
    put32(&s[36], 0xffffffffu);  // uid: unset
    put32(&s[40], 0xffffffffu);  // gid: unset
    put32(&s[44], kPermissions);
    put16(&s[48], linkCount);
    put32(&s[52], 0);  // record length
    put64(&s[56], infoLength);
    put64(&s[64], (infoLength + SECTOR - 1) / SECTOR);  // logical blocks recorded
    putTimestamp(&s[72], now);   // access time
    putTimestamp(&s[84], now);   // modification time
    putTimestamp(&s[96], now);   // attribute time
    put32(&s[108], 1);           // checkpoint
    putLongAd(&s[112], 0, 0);    // extended attribute ICB: none
    putImplRegid(&s[128]);
    put64(&s[160], uniqueId);
    put32(&s[168], 0);  // length of extended attributes
    put32(&s[172], 8);  // length of allocation descriptors (one short_ad)
    putShortAd(&s[176], (uint32_t)infoLength, dataBlock);
    finishTag(s.data(), kTagFileEntry, lba, 184);
    return s;
}

// Appends one File Identifier Descriptor to a directory extent. Records are
// padded to a 4-byte boundary (ECMA-167 4/14.4.9).
void appendFid(std::vector<uint8_t>& out, uint32_t dirLba, const std::string& name,
               bool isDirectory, bool isParent, uint32_t icbBlock) {
    const size_t nameLen = isParent ? 0 : name.size() + 1;  // + compression byte
    const size_t len = 38 + nameLen;
    const size_t padded = (len + 3) & ~size_t(3);

    std::vector<uint8_t> r(padded, 0);
    put16(&r[16], 1);  // file version number
    r[18] = (uint8_t)((isDirectory ? 0x02 : 0x00) | (isParent ? 0x08 : 0x00));
    r[19] = (uint8_t)nameLen;
    putLongAd(&r[20], SECTOR, icbBlock);
    put16(&r[36], 0);  // length of implementation use
    if (!isParent) {
        r[38] = 8;  // 8-bit characters
        memcpy(&r[39], name.data(), name.size());
    }
    // The CRC covers the record including its padding, but the tag location is
    // the sector the DIRECTORY starts at - every FID in an extent shares it.
    finishTag(r.data(), kTagFileIdentifier, dirLba, padded);
    out.insert(out.end(), r.begin(), r.end());
}

}  // namespace

uint32_t tailSectors(size_t videoTsFileCount) {
    // root FE + root extent + VIDEO_TS FE + VIDEO_TS extent + one FE per file,
    // then the closing anchor on the last sector.
    return (uint32_t)(4 + videoTsFileCount) + 1;
}

uint32_t firstFreeLba(uint32_t partitionStartLba) {
    return partitionStartLba + 2;  // File Set Descriptor + its terminator
}

std::string overlay(const fs::path& isoFile, const Options& opt) {
    if (opt.videoTs.empty()) return "UDF overlay: no VIDEO_TS files";
    if (opt.partitionStartLba <= kAnchorLba)
        return "UDF overlay: partition must start above the anchor";
    if (opt.tailStartLba < firstFreeLba(opt.partitionStartLba))
        return "UDF overlay: ISO image is smaller than the reserved UDF area";
    for (const FileRef& f : opt.videoTs)
        if (f.lba < firstFreeLba(opt.partitionStartLba))
            return "UDF overlay: '" + f.name + "' sits inside the reserved UDF area";

    std::tm now{};
    const std::time_t nowT = std::time(nullptr);
#ifdef _WIN32
    localtime_s(&now, &nowT);
#else
    now = *std::localtime(&nowT);
#endif

    const uint32_t part = opt.partitionStartLba;
    const uint32_t fsdLba = part;
    // The tail: UDF's own metadata, appended past everything ISO9660 wrote.
    const uint32_t metaBase = opt.tailStartLba;
    const uint32_t rootFeLba = metaBase + 0;
    const uint32_t rootExtLba = metaBase + 1;
    const uint32_t vtsFeLba = metaBase + 2;
    const uint32_t vtsExtLba = metaBase + 3;
    const uint32_t fileFeLba = metaBase + 4;  // one per VIDEO_TS file
    const uint32_t anchor2Lba = metaBase + tailSectors(opt.videoTs.size()) - 1;
    const uint32_t totalSectors = anchor2Lba + 1;
    // The partition covers everything from its start up to (not including) the
    // closing anchor, which by definition sits outside it.
    const uint32_t partLength = anchor2Lba - part;

    std::fstream f(isoFile, std::ios::binary | std::ios::in | std::ios::out);
    if (!f) return "cannot open for UDF overlay: " + isoFile.string();

    auto writeSector = [&](uint32_t lba, const Sector& s) {
        f.seekp((std::streamoff)lba * SECTOR, std::ios::beg);
        f.write((const char*)s.data(), SECTOR);
    };

    // --- Volume Recognition Sequence, continuing the ISO9660 descriptors ---
    writeSector(kVrsLba + 0, makeVrs("BEA01"));
    writeSector(kVrsLba + 1, makeVrs("NSR02"));
    writeSector(kVrsLba + 2, makeVrs("TEA01"));

    // --- Main and reserve Volume Descriptor Sequences ---------------------
    // Both sequences hold the same descriptors; only their tag locations
    // differ, so each one is rebuilt at its own LBA.
    for (uint32_t base : {kMainVdsLba, kReserveVdsLba}) {
        writeSector(base + 0, makePrimaryVolume(base + 0, opt.volumeId, now));
        writeSector(base + 1, makeImplUseVolume(base + 1, opt.volumeId));
        writeSector(base + 2, makePartition(base + 2, part, partLength));
        writeSector(base + 3, makeLogicalVolume(base + 3, opt.volumeId, fsdLba - part));
        writeSector(base + 4, makeUnallocatedSpace(base + 4));
        writeSector(base + 5, makeTerminating(base + 5));
    }

    // --- Logical Volume Integrity Descriptor -------------------------------
    // Unique IDs 0 and 16 are reserved for the root; ours start after it, so
    // the next free ID is one past the last file entry.
    const uint64_t nextUniqueId = 16 + opt.videoTs.size() + 1;
    writeSector(kLvidLba, makeLvid(kLvidLba, partLength, (uint32_t)opt.videoTs.size(),
                                   2, nextUniqueId, now));
    writeSector(kLvidLba + 1, makeTerminating(kLvidLba + 1));

    // --- Anchors -----------------------------------------------------------
    writeSector(kAnchorLba, makeAnchor(kAnchorLba));

    // From here down every descriptor lives inside the partition, so its tag
    // carries the partition-relative block - see finishTag().
    const uint32_t rootFeBlk = rootFeLba - part;
    const uint32_t rootExtBlk = rootExtLba - part;
    const uint32_t vtsFeBlk = vtsFeLba - part;
    const uint32_t vtsExtBlk = vtsExtLba - part;
    const uint32_t fileFeBlk = fileFeLba - part;

    // --- File Set Descriptor, at partition block 0 -------------------------
    writeSector(fsdLba, makeFileSet(fsdLba - part, opt.volumeId, rootFeBlk, now));
    writeSector(fsdLba + 1, makeTerminating(fsdLba + 1 - part));

    // --- The tail: directories, then one File Entry per file ---------------
    std::vector<uint8_t> rootFids;
    appendFid(rootFids, rootExtBlk, "", true, true, rootFeBlk);
    appendFid(rootFids, rootExtBlk, "VIDEO_TS", true, false, vtsFeBlk);

    std::vector<uint8_t> vtsFids;
    appendFid(vtsFids, vtsExtBlk, "", true, true, rootFeBlk);
    for (size_t i = 0; i < opt.videoTs.size(); ++i)
        appendFid(vtsFids, vtsExtBlk, opt.videoTs[i].name, false, false,
                  fileFeBlk + (uint32_t)i);

    if (rootFids.size() > SECTOR || vtsFids.size() > SECTOR)
        return "UDF overlay: directory extent overflows one sector";

    // Link counts: a directory is referenced by its own '..' entry plus one per
    // child directory (ECMA-167 4/14.9.6).
    writeSector(rootFeLba, makeFileEntry(rootFeBlk, kFileTypeDirectory, fsdLba - part,
                                         rootFids.size(), rootExtBlk, 2, 0, now));
    writeSector(vtsFeLba, makeFileEntry(vtsFeBlk, kFileTypeDirectory, rootFeBlk,
                                        vtsFids.size(), vtsExtBlk, 2, 16, now));
    for (size_t i = 0; i < opt.videoTs.size(); ++i) {
        const FileRef& file = opt.videoTs[i];
        writeSector(fileFeLba + (uint32_t)i,
                    makeFileEntry(fileFeBlk + (uint32_t)i, kFileTypeRegular, vtsFeBlk,
                                  file.size, file.lba - part, 1, 16 + i + 1, now));
    }

    auto writeExtent = [&](uint32_t lba, const std::vector<uint8_t>& data) {
        Sector s{};
        memcpy(s.data(), data.data(), data.size());
        writeSector(lba, s);
    };
    writeExtent(rootExtLba, rootFids);
    writeExtent(vtsExtLba, vtsFids);

    // --- The closing anchor, on the very last sector -----------------------
    writeSector(anchor2Lba, makeAnchor(anchor2Lba));

    f.flush();
    if (!f) return "UDF overlay write failed: " + isoFile.string();
    f.close();

    std::error_code ec;
    if (fs::file_size(isoFile, ec) != (uintmax_t)totalSectors * SECTOR)
        return "UDF overlay: unexpected image size after write";
    return "";
}

}  // namespace udf
