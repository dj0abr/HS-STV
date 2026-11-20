// SPDX-License-Identifier: GPL-3.0-or-later
#include "mm_sstv/audio_spectrum.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
using mm_sstv::AudioSpectrum;
void require(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
int main() {
    try {
        AudioSpectrum spectrum;
        constexpr std::size_t bin = 512; // 750 Hz, exactly at an FFT bin.
        const double pi = std::acos(-1.0);
        std::vector<float> samples(AudioSpectrum::fftSize);
        auto tone = [&](float amplitude) {
            for (std::size_t i = 0; i < samples.size(); ++i) samples[i] = amplitude * std::sin(2 * pi * bin * i / samples.size());
        };
        auto peak = [&] {
            const auto db = spectrum.levels();
            require(db.size() > 2700 && db.size() * AudioSpectrum::binHz < 4002, "Spectrum does not cover 0..4000 Hz");
            require(std::size_t(std::max_element(db.begin(), db.end()) - db.begin()) == bin, "Wrong tone frequency");
            return db[bin];
        };
        tone(1); spectrum.push(samples.data(), 503);
        require(spectrum.levels().empty(), "Unfilled FFT emitted");
        spectrum.push(samples.data() + 503, samples.size() - 503);
        require(std::abs(peak()) < .02, "Full-scale sine is not 0 dBFS/bin");
        tone(.1f);
        // Deliberately vary block boundaries and wrap the ring multiple times.
        for (std::size_t i = 0; i < samples.size(); i += 701) spectrum.push(samples.data() + i, std::min<std::size_t>(701, samples.size() - i));
        require(std::abs(peak() + 20) < .02, "Input gain not reflected in dBFS; unexpected normalization");
        std::fill(samples.begin(), samples.end(), .5f); spectrum.push(samples.data(), samples.size());
        require(std::abs(spectrum.levels()[0] + 6.0206) < .02, "DC bin incorrectly doubled");
        std::fill(samples.begin(), samples.end(), 0); samples[500] = std::numeric_limits<float>::quiet_NaN(); samples[600] = std::numeric_limits<float>::infinity();
        spectrum.push(samples.data(), samples.size());
        for (const auto db : spectrum.levels()) require(std::isfinite(db) && db <= -159, "Silence/non-finite audio retained old spectrum");
        // A Martin-1 sync lasts only 4.862 ms. Test all positions within a
        // 10-ms waterfall hop so a pulse cannot vanish between status updates.
        for (unsigned offset = 0; offset < 480; offset += 48) {
            AudioSpectrum shortSpectrum(48000, 1024);
            std::vector<float> signal(4800);
            double phase = 0; float syncPeak = -160;
            for (unsigned i = 0; i < signal.size(); ++i) {
                const bool sync = i >= 2400 + offset && i < 2400 + offset + 233;
                signal[i] = .1f * std::sin(phase);
                phase += 2 * pi * (sync ? 1200 : 1900) / 48000;
            }
            for (unsigned i = 0; i < signal.size(); i += 480) {
                shortSpectrum.push(signal.data() + i, 480);
                const auto row = shortSpectrum.levels();
                if (row.empty()) continue;
                require(row.size() == 86, "Wrong waterfall frequency coverage");
                syncPeak = std::max(syncPeak, row[std::size_t(std::lround(1200 / shortSpectrum.frequencyStep()))]);
            }
            require(syncPeak > -40, "Short SSTV sync disappeared between waterfall updates");
        }
        std::cout << "RX spectrum: calibrated full scale, -20 dB gain, DC, ring wrap, silence and non-finite input verified\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
