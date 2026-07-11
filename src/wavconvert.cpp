#include "wavconvert.hpp"

#include <cstdint>
#include <cstring>
#include <fstream>
#include <vector>

namespace wavconvert {

bool readFormat(const std::string& path, int& audioFormat, int& channels,
                int& sampleRate, int& bitsPerSample) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    char riff[12] = {};
    if (!f.read(riff, 12) || std::memcmp(riff, "RIFF", 4) != 0 ||
        std::memcmp(riff + 8, "WAVE", 4) != 0)
        return false;
    char header[8];
    while (f.read(header, 8)) {
        const uint32_t size = (uint8_t)header[4] | ((uint8_t)header[5] << 8) |
                              ((uint8_t)header[6] << 16) | ((uint8_t)header[7] << 24);
        if (std::memcmp(header, "fmt ", 4) == 0 && size >= 16) {
            char fmt[40] = {};
            const uint32_t want = size < sizeof(fmt) ? size : (uint32_t)sizeof(fmt);
            if (!f.read(fmt, want)) return false;
            audioFormat = (uint8_t)fmt[0] | ((uint8_t)fmt[1] << 8);
            channels = (uint8_t)fmt[2] | ((uint8_t)fmt[3] << 8);
            sampleRate = (uint8_t)fmt[4] | ((uint8_t)fmt[5] << 8) | ((uint8_t)fmt[6] << 16) |
                         ((uint8_t)fmt[7] << 24);
            bitsPerSample = (uint8_t)fmt[14] | ((uint8_t)fmt[15] << 8);
            // WAVE_FORMAT_EXTENSIBLE: real format tag leads the sub-format GUID
            if (audioFormat == 0xFFFE && size >= 40)
                audioFormat = (uint8_t)fmt[24] | ((uint8_t)fmt[25] << 8);
            return true;
        }
        f.seekg(size + (size & 1), std::ios::cur);
    }
    return false;
}

bool convertTo16(const std::filesystem::path& path, int targetRate,
                 std::string& error, bool toMono) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        error = "cannot open file";
        return false;
    }
    std::vector<char> bytes((std::istreambuf_iterator<char>(in)),
                            std::istreambuf_iterator<char>());
    in.close();
    if (bytes.size() < 44 || std::memcmp(bytes.data(), "RIFF", 4) != 0 ||
        std::memcmp(bytes.data() + 8, "WAVE", 4) != 0) {
        error = "not a RIFF/WAVE file";
        return false;
    }

    auto u16at = [&](size_t o) {
        return (uint32_t)(uint8_t)bytes[o] | ((uint32_t)(uint8_t)bytes[o + 1] << 8);
    };
    auto u32at = [&](size_t o) { return u16at(o) | (u16at(o + 2) << 16); };

    int audioFormat = 0, channels = 0, srcRate = 0, bits = 0;
    size_t dataOff = 0, dataLen = 0;
    for (size_t off = 12; off + 8 <= bytes.size();) {
        const uint32_t size = u32at(off + 4);
        if (std::memcmp(bytes.data() + off, "fmt ", 4) == 0 && size >= 16) {
            audioFormat = (int)u16at(off + 8);
            channels = (int)u16at(off + 10);
            srcRate = (int)u32at(off + 12);
            bits = (int)u16at(off + 22);
            if (audioFormat == 0xFFFE && size >= 40)  // WAVE_FORMAT_EXTENSIBLE
                audioFormat = (int)u16at(off + 32);
        } else if (std::memcmp(bytes.data() + off, "data", 4) == 0) {
            dataOff = off + 8;
            dataLen = size;
        }
        off += 8 + size + (size & 1);
    }
    if (dataOff && dataOff + dataLen > bytes.size())
        dataLen = bytes.size() - dataOff;  // tolerate a truncated data chunk
    if (!audioFormat || !dataOff) {
        error = "missing fmt/data chunk";
        return false;
    }
    if (channels < 1 || channels > 2) {
        error = std::to_string(channels) + " channels (only mono/stereo)";
        return false;
    }
    const int bytesPer = bits / 8;
    if ((audioFormat == 1 && bits != 8 && bits != 16 && bits != 24 && bits != 32) ||
        (audioFormat == 3 && bits != 32) || (audioFormat != 1 && audioFormat != 3)) {
        error = "unsupported encoding (format " + std::to_string(audioFormat) + ", " +
                std::to_string(bits) + "-bit)";
        return false;
    }
    if (srcRate < 4000 || srcRate > 192000) {
        error = "implausible sample rate " + std::to_string(srcRate);
        return false;
    }
    if (targetRate <= 0) targetRate = srcRate;

    const size_t frameBytes = (size_t)bytesPer * channels;
    const size_t srcFrames = frameBytes ? dataLen / frameBytes : 0;
    if (srcFrames == 0) {
        error = "no audio data";
        return false;
    }

    auto sampleAt = [&](size_t frame, int ch) -> float {
        const uint8_t* p = (const uint8_t*)bytes.data() + dataOff +
                           frame * frameBytes + (size_t)ch * bytesPer;
        if (audioFormat == 3) {  // float32
            float f;
            std::memcpy(&f, p, 4);
            return f;
        }
        switch (bits) {
            case 8: return ((int)p[0] - 128) / 128.0f;  // WAV 8-bit is unsigned
            case 16: return (int16_t)(p[0] | (p[1] << 8)) / 32768.0f;
            case 24: {
                int v = p[0] | (p[1] << 8) | (p[2] << 16);
                if (v & 0x800000) v -= 0x1000000;
                return v / 8388608.0f;
            }
            default: {  // 32-bit int
                int32_t v;
                std::memcpy(&v, p, 4);
                return v / 2147483648.0f;
            }
        }
    };

    const size_t outFrames =
        (size_t)((uint64_t)srcFrames * (uint64_t)targetRate / (uint64_t)srcRate);
    if (outFrames == 0) {
        error = "conversion would produce no samples";
        return false;
    }
    const int outChannels = toMono ? 1 : channels;
    // Mono downmix: average the source channels before resampling.
    auto srcSample = [&](size_t frame, int outCh) -> float {
        if (!toMono || channels == 1) return sampleAt(frame, outCh);
        return (sampleAt(frame, 0) + sampleAt(frame, 1)) * 0.5f;
    };
    std::vector<int16_t> out(outFrames * outChannels);
    const double step = (double)srcRate / (double)targetRate;
    for (size_t i = 0; i < outFrames; ++i) {
        const double s0 = i * step, s1 = s0 + step;
        for (int ch = 0; ch < outChannels; ++ch) {
            float v;
            if (step > 1.5) {
                // downsample: average the source span (box low-pass)
                size_t a = (size_t)s0, b = (size_t)s1;
                if (b >= srcFrames) b = srcFrames - 1;
                float acc = 0;
                int n = 0;
                for (size_t f = a; f <= b; ++f, ++n) acc += srcSample(f, ch);
                v = n ? acc / n : 0.0f;
            } else {
                // near-1:1 or upsample: linear interpolation
                const size_t a = (size_t)s0;
                const size_t b = a + 1 < srcFrames ? a + 1 : a;
                const float t = (float)(s0 - a);
                v = srcSample(a, ch) * (1.0f - t) + srcSample(b, ch) * t;
            }
            if (v > 1.0f) v = 1.0f;
            if (v < -1.0f) v = -1.0f;
            out[i * outChannels + ch] = (int16_t)(v * 32767.0f);
        }
    }

    // 16-bit PCM RIFF out, written to a temp sibling then swapped in place
    const std::filesystem::path tmp = path.string() + ".tmp";
    {
        std::ofstream o(tmp, std::ios::binary | std::ios::trunc);
        if (!o) {
            error = "cannot write next to the file";
            return false;
        }
        const uint32_t dataBytes = (uint32_t)(out.size() * 2);
        const uint32_t byteRate = (uint32_t)targetRate * outChannels * 2;
        const uint16_t blockAlign = (uint16_t)(outChannels * 2);
        auto w16 = [&](uint16_t v) { o.write((const char*)&v, 2); };
        auto w32 = [&](uint32_t v) { o.write((const char*)&v, 4); };
        o.write("RIFF", 4);
        w32(36 + dataBytes);
        o.write("WAVE", 4);
        o.write("fmt ", 4);
        w32(16);
        w16(1);  // integer PCM
        w16((uint16_t)outChannels);
        w32((uint32_t)targetRate);
        w32(byteRate);
        w16(blockAlign);
        w16(16);
        o.write("data", 4);
        w32(dataBytes);
        o.write((const char*)out.data(), dataBytes);
        if (!o) {
            error = "write failed";
            return false;
        }
    }
    std::error_code ec;
    std::filesystem::rename(tmp, path, ec);
    if (ec) {
        std::filesystem::remove(tmp, ec);
        error = "cannot replace the original file";
        return false;
    }
    return true;
}

}  // namespace wavconvert
