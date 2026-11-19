// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
namespace mm_sstv {
// HS-Modem's 48-kHz / 6000-bit/s 8APSK mode. Shared by TX, RX and display.
struct ModemMode {
    static constexpr unsigned sampleRate = 48000;
    static constexpr unsigned bitsPerSymbol = 3;
    static constexpr unsigned symbolRate = 2000;
    static constexpr unsigned bitRate = symbolRate * bitsPerSymbol;
    static constexpr unsigned txSamplesPerSymbol = sampleRate / symbolRate;
    static constexpr unsigned rxSamplesPerSymbol = 4;
    static constexpr unsigned rxDecimation = txSamplesPerSymbol / rxSamplesPerSymbol;
    static constexpr double pllSampleRate = 2 * symbolRate;
    static constexpr double halfBandwidthHz = symbolRate * 1.2 / 2;
    static_assert(sampleRate % symbolRate == 0 && txSamplesPerSymbol % rxSamplesPerSymbol == 0,
                  "Modem mode requires integer TX interpolation and RX decimation");
};
}
