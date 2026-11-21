// SPDX-License-Identifier: GPL-3.0-or-later
#include "mm_sstv/waterfall_auto_level.hpp"
#include "mm_sstv/audio_spectrum.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <random>
#include <stdexcept>

using mm_sstv::WaterfallAutoLevel;
namespace {
constexpr double binHz = 48000. / 2048;
const double correction = 10 * std::log10(16.);
void require(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
struct Fixture {
    WaterfallAutoLevel levels;
    std::mt19937 random{917};
    std::exponential_distribution<float> power{1};
    std::size_t time = 0;
    std::vector<float> row(float noise = -90, float signal = -160, bool sync = false) {
        std::vector<float> result(171);
        for (auto& value : result) value = float(noise + 10 * std::log10(std::max(1e-8f, power(random))) + correction);
        // A moving narrow tone, plus a stronger one-row sync every 450 ms.
        if (signal > -150) result[64 + time % 34] = float(signal + correction);
        if (sync && time % 45 == 0) result[51] = float(signal + 8 + correction);
        ++time; return result;
    }
    void run(unsigned count, float noise = -90, float signal = -160, bool sync = false) {
        for (unsigned at = 0; at < count; ++at) levels.update({row(noise, signal, sync)}, binHz, correction, 0, 12);
    }
};
void backgroundAndSignal() {
    Fixture f; f.run(800);
    require(f.levels.minimumDb() > -94 && f.levels.minimumDb() < -89, "Background visibility correction missing");
    require(f.levels.maximumDb() > -49 && f.levels.maximumDb() < -44, "Background correction moved the startup ceiling");
    // Verify actual palette brightness on independent rendered groups. The
    // background should be dimly visible, rather than almost entirely black.
    unsigned visible = 0, saturated = 0, pixels = 0; double brightness = 0;
    for (int group = 0; group < 100; ++group) {
        std::vector<float> peak(171, -160);
        for (int at = 0; at < 12; ++at) {
            const auto row = f.row();
            for (std::size_t bin = 30; bin < 115; ++bin) peak[bin] = std::max(peak[bin], float(row[bin] - correction));
        }
        for (std::size_t bin = 30; bin < 115; ++bin) {
            ++pixels;
            const double level = std::clamp((peak[bin] - f.levels.minimumDb()) /
                double(f.levels.maximumDb() - f.levels.minimumDb()), 0., 1.);
            visible += level > 0; saturated += level >= 1; brightness += std::min(1., level * 2);
        }
    }
    require(double(visible) / pixels > .9, "Background remains almost entirely black");
    require(brightness / pixels > .1 && brightness / pixels < .4, "Background is not dimly visible");
    require(double(saturated) / pixels < .01, "Noise exhausts the palette");
    f.run(800, -90, -60, true);
    require(f.levels.maximumDb() >= -48 && f.levels.maximumDb() <= -44, "Short recurring sync did not set the ceiling with reserve");
    require(f.levels.minimumDb() < -80, "SSTV tones lifted the black level");
    const auto before = f.levels.maximumDb(); f.run(1000);
    require(f.levels.maximumDb() == before, "Signal pause caused noise pumping");
}
void impulsesFadingAndSilence() {
    Fixture f; f.run(600, -100, -65);
    const int original = f.levels.maximumDb(), floor = f.levels.minimumDb();
    auto impulse = f.row(-100, -65); std::fill(impulse.begin(), impulse.end(), 0);
    f.levels.update({impulse}, binHz, correction, 0, 12); f.run(200, -100, -65);
    require(std::abs(f.levels.maximumDb() - original) <= 1, "Isolated broadband impulse changed ceiling");
    require(std::abs(f.levels.minimumDb() - floor) <= 1, "Isolated broadband impulse changed floor");
    for (int at = 0; at < 200; ++at) {
        auto row = f.row(-100, -65); row[150] = 0; // Interference outside the SSTV band.
        f.levels.update({row}, binHz, correction, 0, 12);
    }
    require(std::abs(f.levels.maximumDb() - original) <= 1, "Out-of-band interference changed SSTV ceiling");
    f.run(100, -100, -35);
    require(f.levels.maximumDb() > -34, "Stronger signal not followed quickly");
    const int strong = f.levels.maximumDb(); f.run(100, -100, -65);
    require(f.levels.maximumDb() >= strong - 2, "Ceiling fell too quickly");
    f.run(4000, -100, -65);
    require(f.levels.maximumDb() >= -60 && f.levels.maximumDb() <= -57, "Weaker sustained signal did not settle");
    const int silentMin = f.levels.minimumDb(), silentMax = f.levels.maximumDb();
    std::vector<float> silence(171, -160);
    for (int at = 0; at < 800; ++at) f.levels.update({silence}, binHz, correction, 0, 12);
    require(f.levels.minimumDb() == silentMin && f.levels.maximumDb() == silentMax, "Digital silence changed contrast");
    std::fill(silence.begin(), silence.end(), std::numeric_limits<float>::quiet_NaN());
    require(!f.levels.update({silence}, 0, correction, 0, 12), "Invalid FFT spacing accepted");
    for (int at = 0; at < 400; ++at) f.levels.update({silence}, binHz, correction, 0, 12);
    require(f.levels.minimumDb() == silentMin && f.levels.maximumDb() == silentMax, "Invalid input changed contrast");
    f.levels.reset(-90, -20); f.run(50, -110, -80);
    require(f.levels.minimumDb() < -100 && f.levels.maximumDb() < -65, "Reset retained old contrast statistics");
}
void actualAudioAndResolutionChange() {
    mm_sstv::AudioSpectrum fft(48000, 2048);
    WaterfallAutoLevel levels;
    std::mt19937 random(11); std::normal_distribution<float> noise(0, .0005f);
    std::vector<float> samples(480);
    std::size_t time = 0;
    for (int row = 0; row < 600; ++row) {
        for (auto& value : samples) { value = noise(random) + float(.01 * std::sin(2 * 3.141592653589793 * 1800 * time / 48000)); ++time; }
        fft.push(samples.data(), samples.size());
        levels.update({fft.levels()}, binHz, correction, 0, 14);
    }
    require(levels.minimumDb() < -90, "Actual FFT noise floor too high");
    require(levels.maximumDb() >= -48 && levels.maximumDb() <= -45, "Actual FFT tone lacks expected reserve");
    // A new FFT grid/bandwidth must drop the old history immediately.
    std::vector<float> row(201, -100);
    row[60] = -70;
    require(!levels.update({row}, 20, 0, 0, 10), "Resolution change used old history");
    for (int at = 0; at < 60; ++at) levels.update({row}, 20, 0, 0, 10);
    require(levels.maximumDb() <= -63 && levels.minimumDb() < -95, "Resolution change did not recalibrate");
}
}
int main() {
    try {
        backgroundAndSignal(); impulsesFadingAndSilence(); actualAudioAndResolutionChange();
        std::cout << "SSTV auto levels: rendered noise, brief sync, impulses, fading, pauses, silence, actual FFT and reset passed\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
