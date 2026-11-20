// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "audio_output.hpp"
#include "image_audio.hpp"
#include "sstv_audio.hpp"
#include "waterfall_text.hpp"
#include <atomic>
namespace mm_sstv {
class Transmitter {
public:
    explicit Transmitter(std::unique_ptr<AudioOutput> output);
    ~Transmitter();
    AudioOutput& audio() { return *audio_; }
    void startTest(const std::string& device, bool validFrames = false, Modulation mode = Modulation::Apsk8);
    void startImage(const std::string& device, ImageTransmission transmission, bool waterfallId = false);
    void startSstv(const std::string& device, Modulation mode, const SstvImage& image, bool waterfallId = false);
    void stop() noexcept;
    void setLevel(unsigned percent);
    bool running() const { return running_; }
    unsigned progress() const { return totalFrames_ ? unsigned(renderedFrames_.load(std::memory_order_relaxed) * 100 / totalFrames_) : 0; }
    bool sendingWaterfallId() const { return waterfall_ && waterfall_->renderedFrames() < waterfall_->frames(); }
private:
    std::size_t renderImageAudio(float* output, std::size_t frames);
    std::unique_ptr<WaterfallTextAudio> waterfall_;
    std::atomic<std::size_t> renderedFrames_{0};
    std::size_t totalFrames_ = 0;
    std::unique_ptr<AudioOutput> audio_;
    std::unique_ptr<SymbolSource> source_;
    std::unique_ptr<TxModulator> modulator_;
    std::unique_ptr<ImageAudio> image_;
    std::unique_ptr<SstvAudio> sstv_;
    std::atomic<float> level_{.35f};
    bool running_ = false;
};
}
