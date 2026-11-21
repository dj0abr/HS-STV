// SPDX-License-Identifier: GPL-3.0-or-later
#include "mm_sstv/transmitter.hpp"
#include <stdexcept>
namespace mm_sstv {
namespace {
// +10 dB is an amplitude factor of 10^(10/20), applied only to the SSTV ID.
constexpr float sstvWaterfallBoost = 3.162277660168379f;
}
Transmitter::Transmitter(std::unique_ptr<AudioOutput> output) : audio_(std::move(output)) {
    if (!audio_) throw std::invalid_argument("Missing audio output");
}
Transmitter::~Transmitter() { stop(); }
void Transmitter::startTest(const std::string& device, bool validFrames, Modulation mode) {
    if (isBeacon(mode)) throw std::invalid_argument("MM-Bake ist ein reiner Empfangsmodus");
    if (running_) return;
    if (device.empty()) throw std::invalid_argument("No output device selected");
    if (validFrames) source_ = std::make_unique<BerSymbols>(mode);
    else source_ = std::make_unique<RandomSymbols>(std::random_device{}(), mode);
    modulator_ = std::make_unique<TxModulator>(mode);
    try {
        audio_->start(device, [this](float* buffer, std::size_t frames) {
            modulator_->render(buffer, frames, *source_, level_.load(std::memory_order_relaxed)); return frames;
        }, modulator_->sampleRateHz());
        running_ = true;
    } catch (...) { stop(); throw; }
}
void Transmitter::startImage(const std::string& device, ImageTransmission transmission, bool waterfallId) {
    if (isBeacon(transmission.modulation)) throw std::invalid_argument("MM-Bake ist ein reiner Empfangsmodus");
    if (running_) return;
    if (device.empty()) throw std::invalid_argument("No output device selected");
    try {
        const auto mode = transmission.modulation;
        image_ = std::make_unique<ImageAudio>(std::move(transmission));
        if (waterfallId) waterfall_ = std::make_unique<WaterfallTextAudio>(waterfallIdText(mode), modeSampleRate(mode));
        renderedFrames_.store(0, std::memory_order_relaxed);
        totalFrames_ = image_->frames() + (waterfall_ ? waterfall_->frames() : 0);
        audio_->start(device, [this](float* buffer, std::size_t frames) {
            return renderImageAudio(buffer, frames);
        }, image_->sampleRateHz());
        running_ = true;
    } catch (...) { stop(); throw; }
}
void Transmitter::startSstv(const std::string& device, Modulation mode, const SstvImage& image, bool waterfallId) {
    if (running_) return;
    if (device.empty()) throw std::invalid_argument("No output device selected");
    try {
        sstv_ = std::make_unique<SstvAudio>(mode, image);
        if (waterfallId) waterfall_ = std::make_unique<WaterfallTextAudio>(waterfallIdText(mode), modeSampleRate(mode));
        renderedFrames_.store(0, std::memory_order_relaxed);
        totalFrames_ = sstv_->frames() + (waterfall_ ? waterfall_->frames() : 0);
        audio_->start(device, [this](float* buffer, std::size_t frames) {
            return renderImageAudio(buffer, frames);
        }, modeSampleRate(mode));
        running_ = true;
    } catch (...) { stop(); throw; }
}
std::size_t Transmitter::renderImageAudio(float* output, std::size_t frames) {
    const float gain = level_.load(std::memory_order_relaxed);
    const auto prefix = waterfall_ ? waterfall_->render(output, frames, sstv_ ? gain * sstvMaximumGain * sstvWaterfallBoost : gain) : 0;
    std::size_t count = prefix;
    if (prefix < frames) {
        if (sstv_) count += sstv_->render(output + prefix, frames - prefix, gain);
        else count += image_->render(output + prefix, frames - prefix, gain);
    }
    renderedFrames_.fetch_add(count, std::memory_order_relaxed);
    return count;
}
void Transmitter::stop() noexcept {
    audio_->stop(); // join callbacks before releasing DSP and image storage
    modulator_.reset(); source_.reset(); image_.reset(); sstv_.reset(); waterfall_.reset();
    renderedFrames_.store(0, std::memory_order_relaxed); totalFrames_ = 0; running_ = false;
}
void Transmitter::setLevel(unsigned percent) {
    if (percent > 100) throw std::invalid_argument("Level outside 0..100 percent");
    level_.store(percent / 100.f, std::memory_order_relaxed);
}
}
