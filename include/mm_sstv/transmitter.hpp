// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "audio_output.hpp"
#include "image_audio.hpp"
#include "sstv_audio.hpp"
#include <atomic>
namespace mm_sstv {
class Transmitter {
public:
    explicit Transmitter(std::unique_ptr<AudioOutput> output);
    ~Transmitter();
    AudioOutput& audio() { return *audio_; }
    void startTest(const std::string& device, bool validFrames = false, Modulation mode = Modulation::Apsk8);
    void startImage(const std::string& device, ImageTransmission transmission);
    void startSstv(const std::string& device, Modulation mode, const SstvImage& image);
    void stop() noexcept;
    void setLevel(unsigned percent);
    bool running() const { return running_; }
    unsigned progress() const { return sstv_ ? sstv_->progress() : image_ ? image_->progress() : 0; }
private:
    std::unique_ptr<AudioOutput> audio_;
    std::unique_ptr<SymbolSource> source_;
    std::unique_ptr<TxModulator> modulator_;
    std::unique_ptr<ImageAudio> image_;
    std::unique_ptr<SstvAudio> sstv_;
    std::atomic<float> level_{.35f};
    bool running_ = false;
};
}
