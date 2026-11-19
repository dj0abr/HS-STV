// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "protocol.hpp"
#include <atomic>
namespace mm_sstv {
class ImageAudio {
public:
    explicit ImageAudio(ImageTransmission transmission);
    std::size_t render(float* output, std::size_t capacity, float gain);
    unsigned progress() const;
    unsigned sampleRateHz() const { return parameters(transmission_.modulation).sampleRate; }
    std::size_t frames() const { return transmission_.audioFrames(); }
private:
    ImageTransmission transmission_;
    FrameSymbols source_;
    TxModulator modulator_;
    std::atomic<std::size_t> rendered_{0};
};
}
