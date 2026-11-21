// SPDX-License-Identifier: GPL-3.0-or-later
// Raster, tone comb and phase spreading adapted from DJ0ABR's
// QO100_TRX2/webbridge/waterfall_tx.cpp; see docs/PROVENANCE.md.
#include "mm_sstv/waterfall_text.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <stdexcept>
namespace mm_sstv {
namespace {
constexpr double pi = 3.14159265358979323846;
constexpr unsigned bands = 9, columns = 7, firstHz = 525, stepHz = 25;
constexpr unsigned tonesPerBand = 7, bandStep = 9, pauseMilliseconds = 300;
using Glyph = std::array<unsigned, bands>;
const std::map<char, Glyph> font{
#include "waterfall_font.inc"
};
std::vector<unsigned> rasterize(std::string_view text) {
    if (text.size() > 24) throw std::invalid_argument("Waterfall text exceeds 24 characters");
    const auto first = text.find_first_not_of(' ');
    if (first == std::string_view::npos) throw std::invalid_argument("Waterfall text is empty");
    text = text.substr(first, text.find_last_not_of(' ') - first + 1);
    std::vector<unsigned> raster;
    for (char c : text) {
        if (c >= 'a' && c <= 'z') c -= 'a' - 'A';
        const auto glyph = font.find(c);
        if (glyph == font.end()) throw std::invalid_argument("Unsupported waterfall character");
        if (!raster.empty()) raster.push_back(0);
        // First character first; the top of each glyph is at low frequencies.
        for (unsigned column = 0; column < columns; ++column) {
            unsigned mask = 0;
            for (unsigned band = 0; band < bands; ++band)
                if (glyph->second[band] & (1u << (columns - 1 - column))) mask |= 1u << band;
            raster.push_back(mask);
        }
    }
    // Start with audio immediately: the ordinary '.' glyph has leading blank
    // columns, and a long following space could let audio-triggered PTT drop.
    // A held dot at the high-frequency end gives PTT 500 ms to settle, followed
    // by just one 100-ms blank row before the first letter (at default timing).
    constexpr unsigned dot = (1u << 7) | (1u << 8);
    raster.insert(raster.begin(), {dot, dot, dot, dot, dot, 0});
    return raster;
}
void validate(unsigned rate, unsigned rowMs) {
    if (rate != 44100 && rate != 48000) throw std::invalid_argument("Unsupported waterfall sample rate");
    if (rowMs < 100 || rowMs > 400 || rowMs % 20) throw std::invalid_argument("Invalid waterfall row duration");
}
}
std::string waterfallIdText(Modulation mode) {
    switch (mode) {
    case Modulation::MmBeacon: throw std::invalid_argument("MM-Bake ist ein reiner Empfangsmodus");
    case Modulation::Apsk8: return "HS-STV 8APSK";
    case Modulation::Qpsk4410: return "HS-STV QPSK";
    case Modulation::Martin1: return "HS-STV MARTIN1";
    case Modulation::Martin2: return "HS-STV MARTIN2";
    case Modulation::Scottie1: return "HS-STV SCOTTIE1";
    case Modulation::Scottie2: return "HS-STV SCOTTIE2";
    }
    throw std::invalid_argument("Unsupported waterfall mode");
}
std::size_t waterfallIdSamples(Modulation mode) {
    return WaterfallTextAudio::sampleCount(waterfallIdText(mode), modeSampleRate(mode));
}
std::size_t WaterfallTextAudio::sampleCount(std::string_view text, unsigned rate, unsigned rowMs) {
    validate(rate, rowMs);
    return rasterize(text).size() * (rowMs * rate / 1000) + rate * pauseMilliseconds / 1000;
}
WaterfallTextAudio::WaterfallTextAudio(std::string_view text, unsigned rate, unsigned rowMs)
    : gainStep_(1.f / (rate / 200)) {
    validate(rate, rowMs);
    const auto raster = rasterize(text);
    const auto periodSamples = rate / stepHz, rowSamples = rowMs * rate / 1000, rampSamples = rate / 100;
    using Period = std::vector<double>;
    std::array<Period, bands> bandPeriods;
    constexpr unsigned toneCount = (bands - 1) * bandStep + tonesPerBand;
    for (unsigned band = 0; band < bands; ++band) {
        auto& period = bandPeriods[band]; period.resize(periodSamples);
        for (unsigned tone = 0; tone < tonesPerBand; ++tone) {
            const auto bin = band * bandStep + tone, hz = firstHz + bin * stepHz;
            const double phase = pi * bin * (double(bin) - 1) / toneCount;
            for (unsigned i = 0; i < periodSamples; ++i)
                period[i] += std::cos(2 * pi * hz * i / rate + phase);
        }
    }
    std::map<unsigned, Period> periods;
    double peak = 1;
    for (const auto mask : raster) {
        auto [it, inserted] = periods.try_emplace(mask, periodSamples, 0.0);
        if (!inserted) continue;
        auto& period = it->second;
        for (unsigned band = 0; band < bands; ++band) if (mask & (1u << band))
            for (unsigned i = 0; i < periodSamples; ++i) period[i] += bandPeriods[band][i];
        for (const auto value : period) peak = std::max(peak, std::abs(value));
    }
    // One normalization for the whole message preserves relative stroke brightness.
    const double scale = (32767.0 / 32768) / peak;
    samples_.reserve(raster.size() * rowSamples + rate * pauseMilliseconds / 1000);
    for (const auto mask : raster) {
        const auto& period = periods.at(mask);
        for (unsigned i = 0; i < rowSamples; ++i) {
            const auto edge = std::min(i, rowSamples - 1 - i);
            const double envelope = edge < rampSamples ? .5 - .5 * std::cos(pi * edge / rampSamples) : 1;
            samples_.push_back(float(scale * envelope * period[i % periodSamples]));
        }
    }
    samples_.resize(samples_.size() + rate * pauseMilliseconds / 1000, 0);
}
std::size_t WaterfallTextAudio::render(float* output, std::size_t capacity, float gain) {
    if ((!output && capacity) || !std::isfinite(gain) || gain < 0 || gain > 1)
        throw std::invalid_argument("Invalid waterfall audio buffer or gain");
    const auto at = renderedFrames(), count = std::min(capacity, samples_.size() - at);
    for (std::size_t i = 0; i < count; ++i) {
        level_ += std::clamp(gain - level_, -gainStep_, gainStep_);
        output[i] = samples_[at + i] * level_;
    }
    rendered_.store(at + count, std::memory_order_relaxed);
    return count;
}
}
