// SPDX-License-Identifier: GPL-3.0-or-later
#include "mm_sstv/audio_input.hpp"
#include <portaudio.h>
#include <algorithm>
#include <array>
#include <mutex>
#include <stdexcept>
namespace mm_sstv {
namespace {
void check(PaError e) { if (e < 0) throw std::runtime_error(Pa_GetErrorText(e)); }
class PortAudioInput final : public AudioInput {
public:
    PortAudioInput() { check(Pa_Initialize()); }
    ~PortAudioInput() override { stop(); Pa_Terminate(); }
    std::vector<AudioDevice> devices() override {
        std::vector<AudioDevice> result; const auto count = Pa_GetDeviceCount(); check(count);
        for (int n = 0; n < count; ++n) {
            const auto* d = Pa_GetDeviceInfo(n);
            if (d && d->maxInputChannels) {
                const auto id = std::string(Pa_GetHostApiInfo(d->hostApi)->name) + ":" + d->name;
                result.push_back({id, id});
            }
        }
        return result;
    }
    void start(const std::string& device, Consume consume, unsigned sampleRate) override {
        stop(); { std::lock_guard<std::mutex> lock(mutex_); error_.clear(); }
        int found = -1; const auto count = Pa_GetDeviceCount(); check(count);
        for (int n = 0; n < count; ++n) {
            const auto* d = Pa_GetDeviceInfo(n);
            if (d && d->maxInputChannels && device == std::string(Pa_GetHostApiInfo(d->hostApi)->name) + ":" + d->name) {
                if (found != -1) throw std::runtime_error("Ambiguous audio input");
                found = n;
            }
        }
        if (found < 0) throw std::runtime_error("Audio input unavailable: " + device);
        PaStreamParameters format{found, 1, paFloat32, .04, nullptr};
        if (Pa_IsFormatSupported(&format, nullptr, sampleRate) != paFormatIsSupported) {
            format.channelCount = 2; check(Pa_IsFormatSupported(&format, nullptr, sampleRate));
        }
        channels_ = format.channelCount; consume_ = std::move(consume);
        try {
            check(Pa_OpenStream(&stream_, &format, nullptr, sampleRate, sampleRate / 100, paClipOff | paDitherOff, callback, this));
            check(Pa_StartStream(stream_));
        } catch (...) { stop(); throw; }
    }
    void stop() noexcept override {
        if (stream_) { Pa_AbortStream(stream_); Pa_CloseStream(stream_); stream_ = nullptr; } consume_ = {};
    }
    std::string error() const override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!error_.empty()) return error_;
        if (stream_ && Pa_IsStreamActive(stream_) != 1) return "Audio input stopped";
        return {};
    }
private:
    static int callback(const void* data, void*, unsigned long frames, const PaStreamCallbackTimeInfo*, PaStreamCallbackFlags, void* context) noexcept {
        auto& self = *static_cast<PortAudioInput*>(context); const auto* input = static_cast<const float*>(data);
        try {
            std::array<float, 480> mono{};
            for (std::size_t at = 0; at < frames;) {
                const auto count = std::min<std::size_t>(mono.size(), frames - at);
                for (std::size_t n = 0; n < count; ++n) {
                    float value = 0;
                    if (input) for (int ch = 0; ch < self.channels_; ++ch) value += input[(at + n) * self.channels_ + ch];
                    mono[n] = value / self.channels_;
                }
                self.consume_(mono.data(), count); at += count;
            }
            return paContinue;
        } catch (...) { std::lock_guard<std::mutex> lock(self.mutex_); self.error_ = "Audio input processing failed"; return paAbort; }
    }
    PaStream* stream_ = nullptr;
    Consume consume_;
    int channels_ = 1;
    mutable std::mutex mutex_;
    std::string error_;
};
}
std::unique_ptr<AudioInput> makeAudioInput() { return std::make_unique<PortAudioInput>(); }
}
