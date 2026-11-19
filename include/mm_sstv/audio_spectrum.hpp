// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstddef>
#include <memory>
#include <vector>
namespace mm_sstv {
// Display-only analysis of unmodified mono audio; independent of AFC/AGC.
class AudioSpectrum final {
public:
    static constexpr std::size_t fftSize = 32768;
    static constexpr double binHz = 48000.0 / fftSize;
    explicit AudioSpectrum(unsigned sampleRate = 48000);
    double frequencyStep() const;
    ~AudioSpectrum();
    AudioSpectrum(const AudioSpectrum&) = delete;
    AudioSpectrum& operator=(const AudioSpectrum&) = delete;
    void push(const float* samples, std::size_t count);
    // Hann-windowed peak amplitude per FFT bin, 0..4000 Hz; empty until ready.
    std::vector<float> levels();
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
