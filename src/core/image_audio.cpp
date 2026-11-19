// SPDX-License-Identifier: GPL-3.0-or-later
#include "mm_sstv/image_audio.hpp"
#include <algorithm>
#include <utility>
namespace mm_sstv {
ImageAudio::ImageAudio(ImageTransmission transmission)
    : transmission_(std::move(transmission)), source_(transmission_.frames, transmission_.modulation), modulator_(transmission_.modulation) {}
std::size_t ImageAudio::render(float* output, std::size_t capacity, float gain) {
    const auto at = rendered_.load(std::memory_order_relaxed);
    const auto count = std::min(capacity, frames() - at);
    const auto fadeStart = frames() - sampleRateHz() / 200;
    const auto normal = at < fadeStart ? std::min(count, fadeStart - at) : 0;
    modulator_.render(output, normal, source_, gain);
    if (count > normal) modulator_.render(output + normal, count - normal, source_, 0);
    rendered_.store(at + count, std::memory_order_relaxed);
    return count;
}
unsigned ImageAudio::progress() const {
    return static_cast<unsigned>(rendered_.load(std::memory_order_relaxed) * 100 / frames());
}
}
