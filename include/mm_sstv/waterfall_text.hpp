// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "modem_mode.hpp"
#include <atomic>
#include <string>
#include <string_view>
#include <vector>
namespace mm_sstv {
std::string waterfallIdText(Modulation mode);
std::size_t waterfallIdSamples(Modulation mode);
// Portable, display-readable multitone text. No GUI, soundcard or OS dependency.
class WaterfallTextAudio final {
public:
    static constexpr unsigned rowMilliseconds = 100;
    explicit WaterfallTextAudio(std::string_view text, unsigned sampleRate, unsigned rowMs = rowMilliseconds);
    static std::size_t sampleCount(std::string_view text, unsigned sampleRate, unsigned rowMs = rowMilliseconds);
    std::size_t render(float* output, std::size_t capacity, float gain);
    std::size_t frames() const { return samples_.size(); }
    std::size_t renderedFrames() const { return rendered_.load(std::memory_order_relaxed); }
private:
    std::vector<float> samples_;
    std::atomic<std::size_t> rendered_{0};
    float level_ = 0, gainStep_;
};
}
