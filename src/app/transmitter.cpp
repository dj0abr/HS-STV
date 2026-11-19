// SPDX-License-Identifier: GPL-3.0-or-later
#include "mm_sstv/transmitter.hpp"
#include <stdexcept>
namespace mm_sstv {
Transmitter::Transmitter(std::unique_ptr<AudioOutput> output) : audio_(std::move(output)) {
    if (!audio_) throw std::invalid_argument("Missing audio output");
}
Transmitter::~Transmitter() { stop(); }
void Transmitter::startTest(const std::string& device, bool validFrames, Modulation mode) {
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
void Transmitter::startImage(const std::string& device, ImageTransmission transmission) {
    if (running_) return;
    if (device.empty()) throw std::invalid_argument("No output device selected");
    image_ = std::make_unique<ImageAudio>(std::move(transmission));
    try {
        audio_->start(device, [this](float* buffer, std::size_t frames) {
            return image_->render(buffer, frames, level_.load(std::memory_order_relaxed));
        }, image_->sampleRateHz());
        running_ = true;
    } catch (...) { stop(); throw; }
}
void Transmitter::stop() noexcept {
    audio_->stop(); // join callbacks before releasing DSP and image storage
    modulator_.reset(); source_.reset(); image_.reset(); running_ = false;
}
void Transmitter::setLevel(unsigned percent) {
    if (percent > 100) throw std::invalid_argument("Level outside 0..100 percent");
    level_.store(percent / 100.f, std::memory_order_relaxed);
}
}
