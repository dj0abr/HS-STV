// SPDX-License-Identifier: GPL-3.0-or-later
#include "mm_sstv/sstv_audio.hpp"
#include "sstv/timing.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>
namespace mm_sstv {
namespace {
constexpr unsigned rate = 48000, fade = rate/200, silence = rate*3/10;
constexpr double scale = (32767.0/32768)/24576;
}
qo100::sstv::Mode sstvMode(Modulation mode) {
    if (!isSstv(mode)) throw std::invalid_argument("Expected an SSTV mode");
    switch (mode) {
    case Modulation::Martin1: return qo100::sstv::Mode::martin_m1;
    case Modulation::Martin2: return qo100::sstv::Mode::martin_m2;
    case Modulation::Scottie1: return qo100::sstv::Mode::scottie_1;
    case Modulation::Scottie2: return qo100::sstv::Mode::scottie_2;
    default: throw std::invalid_argument("Expected an SSTV mode");
    }
}
std::size_t sstvImageSamples(Modulation mode) {
    const auto t = qo100::sstv::detail::timing(sstvMode(mode));
    return std::size_t(std::llround((910000.0+t.initial_sync_us+qo100::sstv::height*t.line_us)*rate/1000000));
}
std::size_t sstvAudioSamples(Modulation mode) { return sstvImageSamples(mode)+fade+silence; }
SstvAudio::SstvAudio(Modulation mode, const SstvImage& image)
    : encoder_(sstvMode(mode), image, 100, rate), imageFrames_(sstvImageSamples(mode)), total_(sstvAudioSamples(mode)) {}
std::size_t SstvAudio::render(float* output, std::size_t capacity, float gain) {
    if ((!output && capacity) || !std::isfinite(gain) || gain < 0 || gain > 1) throw std::invalid_argument("Invalid SSTV audio buffer or gain");
    // Use the entire slider range linearly, with the calibrated SSTV maximum.
    gain *= sstvMaximumGain;
    const auto at = rendered_.load(std::memory_order_relaxed), count = std::min(capacity, total_-at);
    std::array<std::int16_t, 480> pcm{};
    for (std::size_t done = 0; done < count;) {
        const auto position = at+done;
        if (position < imageFrames_) {
            const auto n = std::min({pcm.size(), count-done, imageFrames_-position});
            if (encoder_.read(pcm.data(), n) != n) throw std::runtime_error("Unexpected SSTV encoder end");
            for (std::size_t i = 0; i < n; ++i) {
                level_ += std::clamp(gain-level_, -1.f/fade, 1.f/fade);
                output[done++] = float(pcm[i]*scale)*level_;
            }
        } else {
            const auto tail = position-imageFrames_;
            if (tail < fade) {
                level_ = std::max(0.f, level_-1.f/fade);
                const double phase = encoder_.phase()+2*std::acos(-1.0)*1500*tail/rate;
                output[done++] = float(std::sin(phase)*(32767.0/32768))*level_;
            } else { output[done++] = 0; level_ = 0; }
        }
    }
    rendered_.store(at+count,std::memory_order_relaxed); return count;
}
unsigned SstvAudio::progress() const { return unsigned(rendered_.load(std::memory_order_relaxed)*100/total_); }
}
