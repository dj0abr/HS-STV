// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "modem_mode.hpp"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <random>

namespace mm_sstv {
class SymbolSource {
public:
    virtual ~SymbolSource() = default;
    static constexpr unsigned silence = ~0u;
    virtual unsigned next() = 0; // MSB-first logical symbols, or zero-baseband silence.
};
class RandomSymbols final : public SymbolSource {
public:
    explicit RandomSymbols(std::uint32_t seed, Modulation mode = Modulation::Apsk8) : engine_(seed), mask_((1u << parameters(mode).bitsPerSymbol) - 1) {}
    unsigned next() override { return engine_() & mask_; }
private:
    std::mt19937 engine_;
    unsigned mask_;
};
// No GUI, audio device, thread or operating-system dependencies.
// Keep one instance for the whole transmission, including partial symbols.
class TxModulator {
public:
    static constexpr unsigned sampleRate = ModemMode::sampleRate;
    static constexpr unsigned symbolRate = ModemMode::symbolRate;
    static constexpr unsigned samplesPerSymbol = sampleRate / symbolRate;
    static constexpr unsigned bitRate = ModemMode::bitRate;
    static constexpr unsigned carrierHz = 1500;
    static constexpr unsigned pilotHz = 100;
    static_assert(sampleRate % symbolRate == 0, "Symbol rate must divide sample rate");
    explicit TxModulator(Modulation mode = Modulation::Apsk8);
    unsigned sampleRateHz() const;
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
