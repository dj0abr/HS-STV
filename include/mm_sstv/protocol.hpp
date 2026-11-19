// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "tx_modulator.hpp"
#include <array>
#include <string>
#include <vector>
namespace mm_sstv {
using Payload = std::array<std::uint8_t, 219>;
using AirFrame = std::array<std::uint8_t, 258>;
std::uint16_t crc16(const std::uint8_t* data, std::size_t size);
class FrameEncoder {
public:
    FrameEncoder();
    AirFrame encode(const Payload& payload, unsigned type, unsigned status, unsigned counter) const;
private:
    std::array<std::uint8_t, 510> exp_{};
    std::array<unsigned, 256> log_{};
    std::array<std::uint8_t, 33> generator_{};
    std::uint8_t multiply(std::uint8_t a, std::uint8_t b) const;
};
struct ImageTransmission {
    static constexpr std::size_t maxJpegBytes = 200000;
    Modulation modulation = Modulation::Apsk8;
    std::vector<AirFrame> frames; // With acquisition and final-frame repeats.
    std::size_t fileFrames = 0;
    std::size_t jpegBytes = 0;
    std::uint16_t fileId = 0;
    std::string filename;
    std::size_t symbols() const { return frames.size() * parameters(modulation).frameSymbols(); }
    std::size_t audioFrames() const { return (symbols() + 30) * parameters(modulation).txSamplesPerSymbol; }
};
std::string jpegFileName(std::string input);
ImageTransmission prepareImage(const std::vector<std::uint8_t>& jpeg, std::string filename, Modulation mode = Modulation::Apsk8);
class FrameSymbols final : public SymbolSource {
public:
    explicit FrameSymbols(const std::vector<AirFrame>& frames, Modulation mode = Modulation::Apsk8) : frames_(frames), mode_(parameters(mode)) {}
    unsigned next() override;
private:
    const std::vector<AirFrame>& frames_;
    std::size_t symbol_ = 0;
    ModeParameters mode_;
};
// Valid HS-Modem BER frames, used for synchronization tests.
class BerSymbols final : public SymbolSource {
public:
    explicit BerSymbols(Modulation mode = Modulation::Apsk8) : mode_(parameters(mode)), symbol_(mode_.frameSymbols()) {}
    unsigned next() override;
private:
    FrameEncoder encoder_;
    AirFrame frame_{};
    unsigned counter_ = 0;
    ModeParameters mode_;
    unsigned symbol_;
};
}
