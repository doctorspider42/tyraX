#pragma once

#include <functional>
#include <atomic>
#include <memory>
#include <string>

// The editor's only audio OUTPUT path: a playback device that pulls interleaved
// float frames from a callback (miniaudio under the hood - WASAPI on Windows,
// ALSA/PulseAudio/JACK on Linux, all dlopen'd at run time, so no new system
// package and no link-time dependency).
//
// Used by Drone Generator synthesis and Vehicle Editor engine-loop audition.
// The generated game's SPU2 playback still runs on PCSX2 or the console.
//
// Everything here is host-only, and a machine with no sound card is an expected
// state, not an error: start() returns false, `error()` says why, and the tool
// keeps working as an offline renderer.
namespace audiopreview {

// Called on the AUDIO thread: fill `frames` interleaved stereo samples. Must
// not block, allocate or touch the UI - see dronegen::LiveSynth for the
// hand-off pattern this expects.
using PullFn = std::function<void(float* out, int frames)>;

class Device {
 public:
    Device();
    ~Device();
    Device(const Device&) = delete;
    Device& operator=(const Device&) = delete;

    // Opens (or reopens) the device at `sampleRate` and starts pulling. A
    // second call with a different rate tears the old device down first.
    bool start(int sampleRate, PullFn pull);
    void stop();
    bool running() const;

    // The rate the device was opened at. miniaudio resamples for us when the
    // hardware disagrees, so this is what the callback is asked for.
    int sampleRate() const;

    const std::string& error() const { return error_; }
    const std::string& deviceName() const { return name_; }

    // Public only so the C data callback in the .cpp can cast to it; nothing
    // outside audiopreview.cpp names it (miniaudio itself stays in that TU).
    struct Impl;

 private:
    std::unique_ptr<Impl> d_;
    std::string error_;
    std::string name_;
};

// Host audition of the vehicle's two looping samples. Decoding stays off the
// audio thread; live controls are atomic and stopping joins the device thread.
class EngineLoop {
 public:
    EngineLoop();
    ~EngineLoop();
    bool start(const std::string& idle, const std::string& high);
    void update(float pitch, float revs, float volume);
    void stop();
    const std::string& error() const;
 private:
    struct Impl;
    std::unique_ptr<Impl> d_;
};

}  // namespace audiopreview
