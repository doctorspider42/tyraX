#include "elfsym.hpp"

#include "platform.hpp"

#include <cstring>
#include <cstdio>
#include <fstream>
#include <sstream>

namespace elfsym {
namespace {

uint16_t rd16(const unsigned char* p) {
    return (uint16_t)(p[0] | (p[1] << 8));
}
uint32_t rd32(const unsigned char* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

std::string cstrAt(const std::vector<unsigned char>& b, size_t off) {
    if (off >= b.size()) return "";
    const size_t end = b.size();
    std::string s;
    for (size_t i = off; i < end && b[i]; ++i) s += (char)b[i];
    return s;
}

std::vector<unsigned char> readFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return {};
    return std::vector<unsigned char>((std::istreambuf_iterator<char>(f)),
                                      std::istreambuf_iterator<char>());
}

}  // namespace

const Section* Image::section(const std::string& name) const {
    for (const Section& s : sections)
        if (s.name == name) return &s;
    return nullptr;
}

const Symbol* Image::symbol(const std::string& name) const {
    for (const Symbol& s : symbols)
        if (s.name == name) return &s;
    return nullptr;
}

std::vector<const Symbol*> Image::symbolsContaining(
    const std::string& needle) const {
    std::vector<const Symbol*> out;
    for (const Symbol& s : symbols)
        if (s.name.find(needle) != std::string::npos) out.push_back(&s);
    return out;
}

uint32_t Image::sizeOfSymbolsContaining(const std::string& needle) const {
    uint32_t total = 0;
    for (const Symbol* s : symbolsContaining(needle)) total += s->size;
    return total;
}

Image read(const std::string& elfPath) {
    Image img;
    const std::vector<unsigned char> b = readFile(elfPath);
    if (b.size() < 52) {
        img.error = "cannot read " + elfPath;
        return img;
    }
    img.fileSize = b.size();
    if (!(b[0] == 0x7F && b[1] == 'E' && b[2] == 'L' && b[3] == 'F')) {
        img.error = "not an ELF: " + elfPath;
        return img;
    }
    if (b[4] != 1 || b[5] != 1) {  // ELFCLASS32 + ELFDATA2LSB
        img.error = "not a 32-bit little-endian ELF (PS2 EE) : " + elfPath;
        return img;
    }
    img.entry = rd32(&b[24]);
    const uint32_t shoff = rd32(&b[32]);
    const uint16_t shentsize = rd16(&b[46]);
    const uint16_t shnum = rd16(&b[48]);
    const uint16_t shstrndx = rd16(&b[50]);
    if (shoff == 0 || shnum == 0 ||
        shoff + (size_t)shnum * shentsize > b.size()) {
        img.error = "section table out of range";
        return img;
    }

    // Section headers first (the names live in one of them).
    struct Raw {
        uint32_t nameOff, type, addr, offset, size, link, entsize;
    };
    std::vector<Raw> raws(shnum);
    for (uint16_t i = 0; i < shnum; ++i) {
        const unsigned char* p = &b[shoff + (size_t)i * shentsize];
        raws[i] = {rd32(p),       rd32(p + 4),  rd32(p + 12),
                   rd32(p + 16), rd32(p + 20), rd32(p + 40), rd32(p + 36)};
    }
    const uint32_t strTabOff =
        shstrndx < shnum ? raws[shstrndx].offset : 0;
    for (uint16_t i = 0; i < shnum; ++i) {
        Section s;
        s.name = strTabOff ? cstrAt(b, strTabOff + raws[i].nameOff) : "";
        s.type = raws[i].type;
        s.addr = raws[i].addr;
        s.size = raws[i].size;
        s.offset = raws[i].offset;
        img.sections.push_back(std::move(s));
    }

    // Symbols: SHT_SYMTAB (2), names in the section its sh_link points at.
    for (uint16_t i = 0; i < shnum; ++i) {
        if (raws[i].type != 2) continue;
        const uint32_t linkIdx = raws[i].link;
        if (linkIdx >= shnum) continue;
        const uint32_t names = raws[linkIdx].offset;
        const uint32_t entsize = raws[i].entsize ? raws[i].entsize : 16;
        const uint32_t count = entsize ? raws[i].size / entsize : 0;
        for (uint32_t k = 0; k < count; ++k) {
            const size_t off = raws[i].offset + (size_t)k * entsize;
            if (off + 16 > b.size()) break;
            const unsigned char* p = &b[off];
            Symbol sym;
            sym.name = cstrAt(b, names + rd32(p));
            sym.addr = rd32(p + 4);
            sym.size = rd32(p + 8);
            sym.type = (unsigned char)(p[12] & 0xF);
            if (!sym.name.empty()) img.symbols.push_back(std::move(sym));
        }
    }
    img.loaded = true;
    return img;
}

std::vector<unsigned char> sectionBytes(const std::string& elfPath,
                                        const Section& s) {
    const std::vector<unsigned char> b = readFile(elfPath);
    if (s.offset + (size_t)s.size > b.size()) return {};
    return std::vector<unsigned char>(b.begin() + s.offset,
                                      b.begin() + s.offset + s.size);
}

// ------------------------------------------------------------ release audit ---

std::string Audit::summary() const {
    std::ostringstream o;
    if (!ran) return "Release audit: not run";
    if (!error.empty()) return "Release audit: " + error;
    o << "Release audit: " << (clean ? "clean" : "FOUND DEVKIT CODE") << " - ELF "
      << (elfBytes / 1024) << " KiB (text " << (textBytes / 1024) << " KiB, data "
      << (dataBytes / 1024) << " KiB, bss " << (bssBytes / 1024) << " KiB), "
      << (symbolCount ? std::to_string(symbolCount) + " symbols"
                      : std::string("stripped (marker + string scan)"));
    if (!clean) {
        o << "; " << findings.size() << " finding(s): ";
        for (size_t i = 0; i < findings.size() && i < 6; ++i)
            o << (i ? ", " : "") << findings[i].what;
        if (findings.size() > 6) o << ", ...";
    }
    return o.str();
}

Audit auditRelease(const std::string& elfPath) {
    Audit a;
    a.ran = true;
    const Image img = read(elfPath);
    if (!img.loaded) {
        a.error = img.error;
        a.clean = false;
        return a;
    }
    a.elfBytes = img.fileSize;
    a.symbolCount = (int)img.symbols.size();
    if (const Section* s = img.section(".text")) a.textBytes = s->size;
    if (const Section* s = img.section(".data")) a.dataBytes = s->size;
    if (const Section* s = img.section(".bss")) a.bssBytes = s->size;
    if (const Section* s = img.section(".rodata")) a.rodataBytes = s->size;

    // Symbols the debugging layers would leave behind. Namespace names survive
    // C++ mangling, so a substring match on the mangled name is enough.
    static const char* const kSymbolNeedles[] = {
        "livedbg", "livelogic", "livepad", "LiveLink", "LiveDebug", "LiveLogic",
        "LiveTex", "flowDbg", "flowLive"};
    for (const char* needle : kSymbolNeedles)
        for (const Symbol* s : img.symbolsContaining(needle))
            a.findings.push_back({s->name, "symbol", s->size});

    // ...and the file names they use, which would sit in the string data even
    // if the code around them were somehow optimized out.
    // "TXDEVKIT-" is a marker each generated devkit runtime plants on purpose
    // (the PS2 toolchain strips the symbol table, so an incidental signal is
    // not enough); the file names are the second, independent signal - the
    // polling code cannot exist without them.
    // "frame.tga" is the self-screenshot's output (docs/devkit.md). It rides
    // the livedbg runtime, so the marker above already covers the layer - but
    // the file name is the independent signal, exactly as for the others.
    static const char* const kStringNeedles[] = {
        "TXDEVKIT-", "livedbg.bin", "livedbg.cmd", "livelink.bin",
        "livelogic.bin", "livepad.bin", "frame.tga"};
    for (const char* sectionName : {".rodata", ".data", ".sdata"}) {
        const Section* s = img.section(sectionName);
        if (!s || !s->size || s->type == 8 /* SHT_NOBITS */) continue;
        const std::vector<unsigned char> bytes = sectionBytes(elfPath, *s);
        const std::string blob(reinterpret_cast<const char*>(bytes.data()),
                               bytes.size());
        for (const char* needle : kStringNeedles)
            if (blob.find(needle) != std::string::npos)
                a.findings.push_back(
                    {needle, std::string("string in ") + sectionName, 0});
    }
    a.clean = a.findings.empty();
    return a;
}


// ------------------------------------------------------------ symbolization ---

std::vector<Location> symbolize(const std::string& projectDir,
                                const std::string& symElfBinRelative,
                                const std::vector<uint32_t>& addrs,
                                std::string* error) {
    std::vector<Location> out;
    out.reserve(addrs.size());
    for (uint32_t a : addrs) {
        Location l;
        l.addr = a;
        out.push_back(l);
    }
    if (addrs.empty()) return out;

    // addr2line takes every address in one go and answers with two lines per
    // address (function, then file:line), so one container round-trip is enough.
    std::string list;
    char buf[16];
    for (uint32_t a : addrs) {
        std::snprintf(buf, sizeof(buf), "0x%08x", a);
        list += " ";
        list += buf;
    }
    const std::string cmd =
        "docker compose exec -T compiler sh -c \"cd /src && "
        "mips64r5900el-ps2-elf-addr2line -f -C -e " + symElfBinRelative + list +
        "\" 2>&1";

    // Run it in the project directory (that is where docker-compose.yml lives).
    // platform::Process takes the working directory as an option, so this needs
    // no chdir of our own process - and no _popen/_chdir/_getcwd, which are
    // Windows spellings that do not exist on Linux (platform.cpp is the one
    // place OS differences live; see the tyra-editor-dev skill).
    platform::Process::Options opts;
    opts.cwd = projectDir;
    opts.capture = true;
    std::string text;
    if (auto proc = platform::Process::start(cmd, opts)) {
        text = proc->readAll();
        proc->wait();
    } else if (error) {
        *error = "cannot run the toolchain in " + projectDir;
        return out;
    }

    if (text.empty()) {
        if (error)
            *error =
                "no output from the toolchain - is the build container running "
                "(Build once) and does bin/*.elf.sym exist (debug profile)?";
        return out;
    }
    // Parse pairs of lines. Anything unexpected (an error message from docker)
    // leaves the locations empty and is reported as-is.
    std::vector<std::string> lines;
    {
        std::istringstream ls(text);
        std::string l;
        while (std::getline(ls, l)) {
            if (!l.empty() && l.back() == '\r') l.pop_back();
            if (!l.empty()) lines.push_back(l);
        }
    }
    if (lines.size() < addrs.size() * 2) {
        if (error) *error = lines.empty() ? "toolchain returned nothing" : lines[0];
        return out;
    }
    for (size_t i = 0; i < addrs.size(); ++i) {
        out[i].func = lines[i * 2];
        out[i].source = lines[i * 2 + 1];
        if (out[i].func == "??") out[i].func.clear();
        if (out[i].source.rfind("??", 0) == 0) out[i].source.clear();
    }
    return out;
}

}  // namespace elfsym
