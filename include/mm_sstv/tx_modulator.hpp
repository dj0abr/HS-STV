// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <random>

namespace mm_sstv {
class SymbolSource {
public:
    virtual ~SymbolSource() = default;
    virtual unsigned next() = 0; // Three bits, in transmission order; value 0..7.
};
class RandomSymbols final : public SymbolSource {
public:
    explicit RandomSymbols(std::uint32_t seed) : engine_(seed) {}
    unsigned next() override { return engine_() & 7u; }
private:
    std::mt19937 engine_;
};
// No GUI, audio device, thread or operating-system dependencies.
// Keep one instance for the whole transmission, including partial symbols.
class TxModulator {
public:
    static constexpr unsigned sampleRate = 48000;
    static constexpr unsigned symbolRate = 2400;
    static constexpr unsigned samplesPerSymbol = sampleRate / symbolRate;
    static constexpr unsigned bitRate = symbolRate * 3;
    static constexpr unsigned carrierHz = 1500;
    static constexpr unsigned pilotHz = 100;
    static_assert(sampleRate % symbolRate == 0, "Symbol rate must divide sample rate");
    TxModulator();
    ~TxModulator();
    TxModulator(const TxModulator&) = delete;
    TxModulator& operator=(const TxModulator&) = delete;
    // Mono float audio; gain 1 maps the maximum possible data+pilot peak to
    // digital full scale. Fixed normalization, no AGC/clipping; 5-ms gain ramp.
    void render(float* output, std::size_t frames, SymbolSource& source, float gain);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
