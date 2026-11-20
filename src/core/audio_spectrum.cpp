// SPDX-License-Identifier: GPL-3.0-or-later
#include "mm_sstv/audio_spectrum.hpp"
#include <liquid/liquid.h>
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace mm_sstv {
struct AudioSpectrum::Impl {
    const std::size_t size;
    std::vector<float> ring, window;
    std::vector<liquid_float_complex> input, output;
    std::size_t position = 0, available = 0;
    double windowSum = 0, frequencyStep = binHz;
    fftplan plan = nullptr;
    explicit Impl(unsigned sampleRate, std::size_t transformSize) : size(transformSize) {
        if (size < 256 || size > fftSize || (size & (size - 1))) throw std::invalid_argument("Invalid spectrum FFT size");
        ring.resize(size); window.resize(size); input.resize(size); output.resize(size);
        if (sampleRate != 48000 && sampleRate != 44100) throw std::invalid_argument("Unsupported monitor sample rate");
        frequencyStep = double(sampleRate) / size;
        const auto pi = std::acos(-1.0);
        for (std::size_t i = 0; i < size; ++i) {
            window[i] = float(.5 - .5 * std::cos(2 * pi * i / (size - 1)));
            windowSum += window[i];
        }
        plan = fft_create_plan(size, input.data(), output.data(), LIQUID_FFT_FORWARD, 0);
        if (!plan) throw std::runtime_error("Cannot create monitor FFT");
    }
    ~Impl() { if (plan) fft_destroy_plan(plan); }
};
AudioSpectrum::AudioSpectrum(unsigned sampleRate, std::size_t transformSize) : impl_(std::make_unique<Impl>(sampleRate, transformSize)) {}
double AudioSpectrum::frequencyStep() const { return impl_->frequencyStep; }
AudioSpectrum::~AudioSpectrum() = default;
void AudioSpectrum::push(const float* samples, std::size_t count) {
    auto& s = *impl_;
    for (std::size_t i = 0; i < count; ++i) {
        s.ring[s.position] = std::isfinite(samples[i]) ? samples[i] : 0;
        s.position = (s.position + 1) % s.size;
    }
    s.available = std::min(s.size, s.available + count);
}
std::vector<float> AudioSpectrum::levels() {
    auto& s = *impl_;
    if (s.available < s.size) return {};
    for (std::size_t i = 0; i < s.size; ++i) {
        s.input[i].real = s.ring[(s.position + i) % s.size] * s.window[i];
        s.input[i].imag = 0;
    }
    fft_execute(s.plan);
    std::vector<float> result(std::size_t(4000 / s.frequencyStep) + 1);
    for (std::size_t i = 0; i < result.size(); ++i) {
        const double amplitude = std::hypot(double(s.output[i].real), double(s.output[i].imag)) * (i ? 2 : 1) / s.windowSum;
        result[i] = float(std::max(-160.0, 20 * std::log10(std::max(1e-8, amplitude))));
    }
    return result;
}
}
