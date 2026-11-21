// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <stdexcept>
namespace mm_sstv {
enum class Modulation { Apsk8 = 8, Qpsk4410 = 4, Martin1 = 44, Scottie1 = 60, Martin2 = 40, Scottie2 = 56, MmBeacon = 7200 };
constexpr bool isBeacon(Modulation mode) { return mode == Modulation::MmBeacon; }
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
           isBeacon(mode) ? ModeParameters{48000, 3, 20, 5} :
           mode == Modulation::Qpsk4410 ? ModeParameters{44100, 2, 20, 5} :
           throw std::invalid_argument("Unsupported modulation");
}
constexpr bool isSstv(Modulation mode) { return mode == Modulation::Martin1 || mode == Modulation::Scottie1 || mode == Modulation::Martin2 || mode == Modulation::Scottie2; }
constexpr unsigned modeSampleRate(Modulation mode) { return isSstv(mode) ? 48000 : parameters(mode).sampleRate; }
constexpr const char* modeKey(Modulation mode) {
    return mode == Modulation::Martin1 ? "martin1" : mode == Modulation::Scottie1 ? "scottie1" :
           mode == Modulation::Martin2 ? "martin2" : mode == Modulation::Scottie2 ? "scottie2" :
           isBeacon(mode) ? "mm-beacon" : mode == Modulation::Qpsk4410 ? "qpsk4410" : mode == Modulation::Apsk8 ? "8apsk" : throw std::invalid_argument("Unsupported mode");
}
constexpr const char* modulationName(Modulation mode) { return mode == Modulation::Martin1 ? "Martin 1" : mode == Modulation::Scottie1 ? "Scottie 1" : mode == Modulation::Martin2 ? "Martin 2" : mode == Modulation::Scottie2 ? "Scottie 2" : parameters(mode).bitsPerSymbol == 3 ? "8APSK" : "QPSK"; }
// Backwards-compatible default parameters; selected modes use parameters(mode).
struct ModemMode {
    static constexpr unsigned sampleRate = 48000, bitsPerSymbol = 3, symbolRate = 2000, bitRate = 6000;
    static constexpr unsigned txSamplesPerSymbol = 24, rxSamplesPerSymbol = 4, rxDecimation = 6;
    static constexpr double pllSampleRate = 4000, halfBandwidthHz = 1200;
};
}
