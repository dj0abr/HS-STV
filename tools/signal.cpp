// SPDX-License-Identifier: GPL-3.0-or-later
// File output only. This tool never creates an audio output or sends RF.
#include "mm_sstv/tx_modulator.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
void word(std::ostream& out, std::uint32_t value, unsigned bytes) {
    for (unsigned n = 0; n < bytes; ++n) out.put(static_cast<char>((value >> (8 * n)) & 255));
}
}
int main(int argc, char** argv) {
    try {
        if (argc < 2 || argc > 6) {
            std::cerr << "Usage: hs-stv-signal OUTPUT.wav [SECONDS=10] [LEVEL_PERCENT=35] [SEED=1] [8apsk|qpsk4410]\n"; return 2;
        }
        const auto number = [](const char* s) {
            std::size_t end; auto result = std::stod(s, &end);
            if (end != std::string(s).size() || !std::isfinite(result)) throw std::invalid_argument("Invalid number");
            return result;
        };
        const double seconds = argc > 2 ? number(argv[2]) : 10;
        const double level = argc > 3 ? number(argv[3]) : 35;
        const double seed = argc > 4 ? number(argv[4]) : 1;
        if (seconds <= 0 || seconds > 3600 || level < 0 || level > 100 || seed < 0 || seed > UINT32_MAX || seed != std::floor(seed))
            throw std::invalid_argument("Duration 0..3600 s, level 0..100 %, integer seed 0..4294967295 required");
        auto mode = mm_sstv::Modulation::Apsk8;
        if (argc == 6) {
            const std::string value(argv[5]);
            if (value == "qpsk4410") mode = mm_sstv::Modulation::Qpsk4410;
            else if (value != "8apsk") throw std::invalid_argument("Modulation must be 8apsk or qpsk4410");
        }
        const auto sampleRate = mm_sstv::parameters(mode).sampleRate;
        const auto frames = static_cast<std::uint32_t>(seconds * sampleRate);
        if (!frames) throw std::invalid_argument("Duration too short");
        std::ofstream out(argv[1], std::ios::binary); out.exceptions(std::ios::badbit | std::ios::failbit);
        out.write("RIFF", 4); word(out, 36 + frames * 2, 4); out.write("WAVEfmt ", 8);
        word(out, 16, 4); word(out, 1, 2); word(out, 1, 2); word(out, sampleRate, 4); word(out, sampleRate * 2, 4);
        word(out, 2, 2); word(out, 16, 2); out.write("data", 4); word(out, frames * 2, 4);
        mm_sstv::TxModulator modulator(mode); mm_sstv::RandomSymbols random(static_cast<std::uint32_t>(seed), mode);
        std::array<float, 960> buffer{};
        for (std::uint32_t at = 0; at < frames;) {
            const auto n = std::min<std::size_t>(buffer.size(), frames - at);
            modulator.render(buffer.data(), n, random, static_cast<float>(level / 100));
            for (std::size_t i = 0; i < n; ++i) {
                if (!std::isfinite(buffer[i]) || std::abs(buffer[i]) >= 1) throw std::runtime_error("Audio exceeds full scale");
                word(out, static_cast<std::uint16_t>(static_cast<std::int16_t>(std::lround(buffer[i] * 32767))), 2);
            }
            at += n;
        }
        out.close();
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
