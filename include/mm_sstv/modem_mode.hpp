// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <stdexcept>
namespace mm_sstv {
enum class Modulation { Apsk8 = 8, Qpsk4410 = 4 };
struct ModeParameters {
    unsigned sampleRate, bitsPerSymbol, txSamplesPerSymbol, rxDecimation;
    constexpr unsigned symbolRate() const { return sampleRate / txSamplesPerSymbol; }
    constexpr unsigned bitRate() const { return symbolRate() * bitsPerSymbol; }
    constexpr unsigned frameSymbols() const { return 258 * 8 / bitsPerSymbol; }
    constexpr double halfBandwidthHz() const { return symbolRate() * 1.2 / 2; }
    constexpr double pllSampleRate() const { return 2 * symbolRate(); }
};
constexpr ModeParameters parameters(Modulation mode) {
    return mode == Modulation::Apsk8 ? ModeParameters{48000, 3, 24, 6} :
           mode == Modulation::Qpsk4410 ? ModeParameters{44100, 2, 20, 5} :
           throw std::invalid_argument("Unsupported modulation");
}
constexpr const char* modulationName(Modulation mode) { return parameters(mode).bitsPerSymbol == 3 ? "8APSK" : "QPSK"; }
// Backwards-compatible default parameters; selected modes use parameters(mode).
struct ModemMode {
    static constexpr unsigned sampleRate = 48000, bitsPerSymbol = 3, symbolRate = 2000, bitRate = 6000;
    static constexpr unsigned txSamplesPerSymbol = 24, rxSamplesPerSymbol = 4, rxDecimation = 6;
    static constexpr double pllSampleRate = 4000, halfBandwidthHz = 1200;
};
}
