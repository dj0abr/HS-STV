// SPDX-License-Identifier: GPL-3.0-or-later
// Waveform parameters and synthesis follow DJ0ABR's HS-Modem liquid_if.cpp
// and tuning.cpp; see docs/PROVENANCE.md for the exact source snapshots.
#include "mm_sstv/tx_modulator.hpp"
#include <liquid/liquid.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>

namespace mm_sstv {
namespace {
constexpr float pi = 3.14159265358979323846f;
void check(int status) {
    if (status != LIQUID_OK) throw std::runtime_error("liquid-dsp: DSP operation failed");
}
}
struct TxModulator::Impl {
    modemcf modem = nullptr;
    firinterp_crcf filter = nullptr;
    nco_crcf carrier = nullptr;
    nco_crcf pilot = nullptr;
    std::array<liquid_float_complex, samplesPerSymbol> interpolated{};
    unsigned position = samplesPerSymbol;
    float level = 0;
    float fullScaleFactor = 0;
    ~Impl() {
        if (modem) modemcf_destroy(modem);
        if (filter) firinterp_crcf_destroy(filter);
        if (carrier) nco_crcf_destroy(carrier);
        if (pilot) nco_crcf_destroy(pilot);
    }
};
TxModulator::TxModulator() : impl_(std::make_unique<Impl>()) {
    if (liquid_libversion_number() != LIQUID_VERSION_NUMBER)
        throw std::runtime_error("liquid-dsp: header/library version mismatch");
    auto& d = *impl_;
    d.modem = modemcf_create(LIQUID_MODEM_APSK8);
    d.carrier = nco_crcf_create(LIQUID_NCO);
    d.pilot = nco_crcf_create(LIQUID_NCO);
    if (!d.modem || !d.carrier || !d.pilot) throw std::runtime_error("Cannot create modulator");
    check(nco_crcf_set_phase(d.carrier, 0));
    check(nco_crcf_set_phase(d.pilot, 0));
    check(nco_crcf_set_frequency(d.carrier, 2.f * pi * carrierHz / sampleRate));
    check(nco_crcf_set_frequency(d.pilot, 2.f * pi * pilotHz / sampleRate));
    // Preserve the ORIGINAL computation, including float rounding and the
    // normalization of -0.2. Passing -0.2 directly would be a different filter.
    float tau = -0.2f;
    while (tau < 0) tau += 1.f;
    const float offset = samplesPerSymbol * tau;
    float dt = offset - std::floor(offset);
    if (dt > .5f) dt -= 1.f;
    std::array<float, 2 * samplesPerSymbol * 15 + 1> taps{};
    check(liquid_firdes_prototype(LIQUID_FIRFILT_RRC, samplesPerSymbol, 15, .2f, dt, taps.data()));
    d.filter = firinterp_crcf_create(samplesPerSymbol, taps.data(), taps.size());
    if (!d.filter) throw std::runtime_error("Cannot create RRC interpolator");

    // Fixed peak normalization, including pulse-shaping overshoot and pilot.
    // At each polyphase output the positive taps sum to A, negative taps to -B.
    // The maximum complex magnitude is max |A*p - B*q| over constellation
    // points p,q: the symbol choices at the individual taps are independent.
    // Re(c)+Im(c) is bounded by sqrt(2)*|c| for every carrier phase.
    // This is sequence-independent: no AGC, limiter or sample clipping.
    std::array<liquid_float_complex, 8> points{};
    for (unsigned symbol = 0; symbol < points.size(); ++symbol)
        check(modemcf_modulate(d.modem, symbol, &points[symbol]));
    double maximum = 0;
    for (unsigned phase = 0; phase < samplesPerSymbol; ++phase) {
        double positive = 0, negative = 0;
        for (std::size_t tap = phase; tap < taps.size(); tap += samplesPerSymbol) {
            if (taps[tap] >= 0) positive += taps[tap];
            else negative -= taps[tap];
        }
        for (const auto p : points) for (const auto q : points) {
            const double radius = std::hypot(positive * p.real - negative * q.real,
                                             positive * p.imag - negative * q.imag);
            maximum = std::max(maximum, std::sqrt(2.) * radius + .25);
        }
    }
    // Leave only a PCM16 LSB plus numerical rounding allowance below 0 dBFS.
    d.fullScaleFactor = static_cast<float>((32767. / 32768.) / (maximum * 1.00001));
}
TxModulator::~TxModulator() = default;
void TxModulator::render(float* output, std::size_t frames, SymbolSource& source, float gain) {
    if ((!output && frames) || !std::isfinite(gain) || gain < 0 || gain > 1)
        throw std::invalid_argument("Invalid audio buffer or gain");
    auto& d = *impl_;
    for (std::size_t n = 0; n < frames; ++n) {
        if (d.position == samplesPerSymbol) {
            const auto symbol = source.next();
            if (symbol > 7) throw std::invalid_argument("8APSK symbol outside 0..7");
            liquid_float_complex point{};
            check(modemcf_modulate(d.modem, symbol, &point));
            check(firinterp_crcf_execute(d.filter, point, d.interpolated.data()));
            d.position = 0;
        }
        liquid_float_complex mixed{};
        check(nco_crcf_step(d.carrier));
        check(nco_crcf_mix_up(d.carrier, d.interpolated[d.position++], &mixed));
        check(nco_crcf_step(d.pilot));
        const float audio = d.fullScaleFactor * (mixed.real + mixed.imag + .25f * nco_crcf_sin(d.pilot));
        constexpr float step = 1.f / (sampleRate / 200); // full-scale change in 5 ms
        d.level += std::clamp(gain - d.level, -step, step);
        output[n] = audio * d.level;
    }
}
}
