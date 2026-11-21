// SPDX-License-Identifier: GPL-3.0-or-later
// Portable adapter for Windows/macOS; Linux uses PulseAudio by default so that
// named virtual sinks such as toQO100 can be addressed directly.
#include "mm_sstv/audio_output.hpp"
#include <portaudio.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <mutex>
#include <stdexcept>

namespace mm_sstv {
namespace {
void check(PaError error) { if (error < 0) throw std::runtime_error(Pa_GetErrorText(error)); }
class PortAudioOutput final : public AudioOutput {
public:
    PortAudioOutput() { check(Pa_Initialize()); }
    ~PortAudioOutput() override { stop(); Pa_Terminate(); }
    std::vector<AudioDevice> devices() override {
        std::vector<AudioDevice> result;
        const auto count = Pa_GetDeviceCount(); check(count);
        for (int i = 0; i < count; ++i) {
            const auto* d = Pa_GetDeviceInfo(i);
            if (d && d->maxOutputChannels > 0) {
                const auto id = std::string(Pa_GetHostApiInfo(d->hostApi)->name) + ":" + d->name;
                result.push_back({id, id});
            }
        }
        return result;
    }
    void start(const std::string& device, Render render, unsigned sampleRate) override {
        stop();
        { std::lock_guard<std::mutex> lock(errorMutex_); error_.clear(); }
        underruns_ = 0; finished_ = false; ending_ = false;
        const bool systemDefault = device == systemDefaultAudioDevice;
        int found = systemDefault ? Pa_GetDefaultOutputDevice() : paNoDevice;
        const auto count = Pa_GetDeviceCount(); check(count);
        for (int i = 0; i < count; ++i) {
            if (systemDefault) break;
            const auto* d = Pa_GetDeviceInfo(i);
            if (!d || d->maxOutputChannels < 1) continue;
            if (device == std::string(Pa_GetHostApiInfo(d->hostApi)->name) + ":" + d->name) {
                if (found != -1) throw std::runtime_error("Ambiguous audio device name");
                found = i;
            }
        }
        if (found < 0) throw std::runtime_error("Audio device unavailable: " + device);
        PaStreamParameters format{found, 1, paFloat32, .04, nullptr};
        if (Pa_IsFormatSupported(nullptr, &format, sampleRate) != paFormatIsSupported) {
            format.channelCount = 2;
            check(Pa_IsFormatSupported(nullptr, &format, sampleRate));
        }
        channels_ = format.channelCount; render_ = std::move(render);
        try {
            check(Pa_OpenStream(&stream_, nullptr, &format, sampleRate, sampleRate / 100, paClipOff | paDitherOff, callback, this));
            check(Pa_SetStreamFinishedCallback(stream_, [](void* self) {
                static_cast<PortAudioOutput*>(self)->finished_ = true;
            }));
            check(Pa_StartStream(stream_));
        } catch (...) { stop(); throw; }
    }
    void stop() noexcept override {
        if (stream_) { Pa_AbortStream(stream_); Pa_CloseStream(stream_); stream_ = nullptr; }
        render_ = {};
    }
    std::string error() const override {
        std::lock_guard<std::mutex> lock(errorMutex_);
        if (!error_.empty()) return error_;
        if (stream_ && !finished_ && !ending_ && Pa_IsStreamActive(stream_) != 1) return "Audio stream stopped; check output device";
        return {};
    }
    bool finished() const override { return finished_.load(); }
    std::uint64_t underruns() const override { return underruns_.load(); }
private:
    static int callback(const void*, void* data, unsigned long frames, const PaStreamCallbackTimeInfo*,
                        PaStreamCallbackFlags flags, void* self) noexcept {
        auto& d = *static_cast<PortAudioOutput*>(self);
        auto* out = static_cast<float*>(data);
        if (flags & paOutputUnderflow) ++d.underruns_;
        try {
            std::array<float, 480> mono{};
            unsigned long position = 0;
            while (position < frames) {
                const auto n = std::min<std::size_t>(mono.size(), frames - position);
                const auto rendered = d.render_(mono.data(), n);
                if (rendered > n) throw std::runtime_error("Invalid rendered frame count");
                for (std::size_t i = 0; i < rendered; ++i)
                    for (int ch = 0; ch < d.channels_; ++ch) out[(position + i) * d.channels_ + ch] = mono[i];
                position += rendered;
                if (rendered < n) {
                    std::fill(out + position * d.channels_, out + frames * d.channels_, 0.f);
                    d.ending_ = true; return paComplete;
                }
            }
            return paContinue;
        } catch (...) {
            std::fill_n(out, frames * d.channels_, 0.f);
            std::lock_guard<std::mutex> lock(d.errorMutex_); d.error_ = "Audio rendering failed";
            return paAbort;
        }
    }
    PaStream* stream_ = nullptr;
    Render render_;
    int channels_ = 1;
    std::atomic<std::uint64_t> underruns_{0};
    std::atomic<bool> finished_{false}, ending_{false};
    mutable std::mutex errorMutex_;
    std::string error_;
};
}
std::unique_ptr<AudioOutput> makeAudioOutput() { return std::make_unique<PortAudioOutput>(); }
}
