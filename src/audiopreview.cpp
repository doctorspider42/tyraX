#include "audiopreview.hpp"

#include <cstring>
#include <vector>
#include <cmath>
#include <algorithm>

// Raw output for the synthesizer and decoding for vehicle engine audition.
// No encoders, resource manager, node graph or miniaudio engine are needed.
#define MA_NO_ENCODING
#define MA_NO_GENERATION
#define MA_NO_RESOURCE_MANAGER
#define MA_NO_NODE_GRAPH
#define MA_NO_ENGINE
#define MA_IMPLEMENTATION
#include "miniaudio.h"

namespace audiopreview {

struct Device::Impl {
    ma_device dev{};
    bool inited = false;
    bool started = false;
    int rate = 0;
    PullFn pull;
};

namespace {

void dataCallback(ma_device* dev, void* out, const void* in, ma_uint32 frames) {
    (void)in;
    Device::Impl* d = (Device::Impl*)dev->pUserData;
    float* f = (float*)out;
    if (!d || !d->pull) {
        std::memset(f, 0, (size_t)frames * 2 * sizeof(float));
        return;
    }
    d->pull(f, (int)frames);
}

}  // namespace

Device::Device() : d_(new Impl) {}

Device::~Device() { stop(); }

bool Device::start(int sampleRate, PullFn pull) {
    stop();
    error_.clear();
    name_.clear();

    ma_device_config cfg = ma_device_config_init(ma_device_type_playback);
    cfg.playback.format = ma_format_f32;
    cfg.playback.channels = 2;
    cfg.sampleRate = (ma_uint32)(sampleRate > 0 ? sampleRate : 44100);
    cfg.dataCallback = dataCallback;
    cfg.pUserData = d_.get();
    // A drone has no transients, so latency is free: a bigger buffer is cheaper
    // insurance against an xrun while the UI thread renders a 4K viewport.
    cfg.periodSizeInMilliseconds = 40;
    cfg.periods = 3;

    d_->pull = std::move(pull);
    if (ma_device_init(nullptr, &cfg, &d_->dev) != MA_SUCCESS) {
        error_ = "no audio output device (silent preview; rendering still works)";
        d_->pull = nullptr;
        return false;
    }
    d_->inited = true;
    d_->rate = (int)d_->dev.sampleRate;
    if (d_->dev.playback.name[0]) name_ = d_->dev.playback.name;

    if (ma_device_start(&d_->dev) != MA_SUCCESS) {
        error_ = "audio device found but refused to start";
        ma_device_uninit(&d_->dev);
        d_->inited = false;
        d_->pull = nullptr;
        return false;
    }
    d_->started = true;
    return true;
}

void Device::stop() {
    if (d_->started) {
        ma_device_stop(&d_->dev);
        d_->started = false;
    }
    if (d_->inited) {
        // uninit joins the audio thread, so the callback (and the LiveSynth it
        // captured) is provably done before the caller destroys anything.
        ma_device_uninit(&d_->dev);
        d_->inited = false;
    }
    d_->pull = nullptr;
    d_->rate = 0;
}

bool Device::running() const { return d_->started; }
int Device::sampleRate() const { return d_->rate; }

struct EngineLoop::Impl {
    Device device;
    std::vector<float> idle, high;
    double lowPos = 0, highPos = 0;
    std::atomic<float> pitch{1}, revs{0}, volume{0.7f};
    std::string error;
};
EngineLoop::EngineLoop() : d_(new Impl) {}
EngineLoop::~EngineLoop() { stop(); }
void EngineLoop::stop() { d_->device.stop(); }
const std::string& EngineLoop::error() const { return d_->error; }
void EngineLoop::update(float pitch, float revs, float volume) {
    d_->pitch.store(std::clamp(pitch, 0.05f, 4.0f));
    d_->revs.store(std::clamp(revs, 0.0f, 1.0f));
    d_->volume.store(std::clamp(volume / 100.0f, 0.0f, 1.0f));
}
bool EngineLoop::start(const std::string& idle, const std::string& high) {
    stop();
    d_->error.clear();
    d_->idle.clear(); d_->high.clear();
    d_->lowPos = d_->highPos = 0;
    auto load = [&](const std::string& path, std::vector<float>& out) {
        if (path.empty()) return true;
        auto cfg = ma_decoder_config_init(ma_format_f32, 2, 44100);
        ma_uint64 frames = 0;
        void* pcm = nullptr;
        if (ma_decode_file(path.c_str(), &cfg, &frames, &pcm) != MA_SUCCESS || !frames) {
            if (pcm) ma_free(pcm, nullptr);
            d_->error = "Cannot decode engine loop: " + path;
            return false;
        }
        out.assign((float*)pcm, (float*)pcm + frames * 2);
        ma_free(pcm, nullptr);
        return true;
    };
    if (!load(idle, d_->idle) || !load(high, d_->high)) return false;
    if (d_->idle.empty()) { d_->error = "Choose an idle loop in Sounds."; return false; }
    const bool ok = d_->device.start(44100, [d = d_.get()](float* out, int count) {
        const float step = d->pitch.load(), f = d->revs.load(), gain = d->volume.load();
        auto sample = [](const std::vector<float>& pcm, double pos, int ch) {
            if (pcm.empty()) return 0.0f;
            const size_t n = pcm.size() / 2, a = (size_t)pos % n, b = (a + 1) % n;
            const float t = (float)(pos - std::floor(pos));
            return pcm[a * 2 + ch] * (1 - t) + pcm[b * 2 + ch] * t;
        };
        const float lowGain = d->high.empty() ? gain : gain * (1 - f * .85f);
        for (int i = 0; i < count; ++i) {
            for (int ch = 0; ch < 2; ++ch)
                out[i * 2 + ch] = std::clamp(sample(d->idle, d->lowPos, ch) * lowGain +
                    sample(d->high, d->highPos, ch) * gain * f, -1.0f, 1.0f);
            d->lowPos = std::fmod(d->lowPos + step, (double)d->idle.size() / 2);
            if (!d->high.empty()) d->highPos = std::fmod(d->highPos + step, (double)d->high.size() / 2);
        }
    });
    if (!ok) d_->error = d_->device.error();
    return ok;
}

}  // namespace audiopreview
