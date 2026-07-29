#include "audiopreview.hpp"

#include <cstring>

// Trim miniaudio down to the one thing the editor needs: a raw playback device.
// No decoders, no encoders, no resource manager, no node graph, no engine - it
// is a 4 MB single header and compiling all of it costs build time for features
// we would never call. The synthesizer generates the samples; miniaudio only has
// to hand them to the OS.
#define MA_NO_DECODING
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

}  // namespace audiopreview
