// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "modem_mode.hpp"
#include "sstv/sstv.hpp"
#include <atomic>
namespace mm_sstv {
using SstvImage = qo100::sstv::Image;
qo100::sstv::Mode sstvMode(Modulation mode);
std::size_t sstvImageSamples(Modulation mode);
std::size_t sstvAudioSamples(Modulation mode);
class SstvAudio final {
public:
    SstvAudio(Modulation mode, const SstvImage& image);
    // Linear slider gain 0..1; 100% reaches the calibrated SSTV peak (~0.2366).
    std::size_t render(float* output, std::size_t capacity, float gain);
    unsigned progress() const;
    std::size_t frames() const { return total_; }
private:
    qo100::sstv::Encoder encoder_;
    std::size_t imageFrames_, total_;
    std::atomic<std::size_t> rendered_{0};
    float level_ = 0;
};
}
