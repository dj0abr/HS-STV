// SPDX-License-Identifier: GPL-3.0-or-later
#include "mm_sstv/tx_modulator.hpp"
#include <liquid/liquid.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <vector>
using namespace mm_sstv;
namespace {
void require(bool ok, const char* reason) { if (!ok) throw std::runtime_error(reason); }
// Independent, symbol-at-a-time transcription of the original HS-Modem TX
// calculation. No mm-sstv filtering or mixer helpers are used here.
std::vector<float> reference(std::size_t symbols, unsigned seed, Modulation mode = Modulation::Apsk8) {
    const bool qpsk = mode == Modulation::Qpsk4410;
    const unsigned rate = qpsk ? 44100 : 48000, k = qpsk ? 20 : 24;
    auto mod = modemcf_create(qpsk ? LIQUID_MODEM_QPSK : LIQUID_MODEM_APSK8);
    auto carrier = nco_crcf_create(LIQUID_NCO), pilot = nco_crcf_create(LIQUID_NCO);
    constexpr float pi = 3.14159265358979323846f;
    nco_crcf_set_frequency(carrier, 2.f * pi * 1500.f / float(rate));
    nco_crcf_set_frequency(pilot, 2.f * pi * 100.f / float(rate));
    float tau = -.2f; while (tau < 0) tau += 1;
    float g = k * tau; int ds = static_cast<int>(std::floor(g)); float dt = g - ds;
    if (dt > .5f) { dt -= 1; ++ds; }
    std::vector<float> h(2*k*15+1); liquid_firdes_prototype(LIQUID_FIRFILT_RRC, k, 15, .2f, dt, h.data());
    auto filter = firinterp_crcf_create(k, h.data(), h.size());
    std::mt19937 bits(seed);
    std::vector<float> out; out.reserve(symbols * k);
    for (std::size_t i = 0; i < symbols; ++i) {
        liquid_float_complex point{}, samples[24];
        unsigned symbol = bits() & (qpsk ? 3u : 7u); if (qpsk) symbol ^= symbol >> 1;
        modemcf_modulate(mod, symbol, &point); firinterp_crcf_execute(filter, point, samples);
        for (unsigned j = 0; j < k; ++j) {
            const auto value = samples[j];
            nco_crcf_step(carrier); liquid_float_complex c{}; nco_crcf_mix_up(carrier, value, &c);
            nco_crcf_step(pilot);
            out.push_back(.1f * (c.real + c.imag + nco_crcf_sin(pilot) / 4.f));
        }
    }
    firinterp_crcf_destroy(filter); modemcf_destroy(mod); nco_crcf_destroy(carrier); nco_crcf_destroy(pilot);
    return out;
}
// Construct the symbol history that maximizes a real audio sample. This
// checks worst-case pulse-shaping overshoot rather than trusting a random seed.
std::vector<unsigned> peakSequence(std::size_t& target, Modulation mode = Modulation::Apsk8) {
    const bool qpsk = mode == Modulation::Qpsk4410;
    const unsigned rate = qpsk ? 44100 : 48000, k = qpsk ? 20 : 24, count = qpsk ? 4 : 8;
    float tau = -.2f; while (tau < 0) tau += 1;
    float dt = k * tau - std::floor(k * tau); if (dt > .5f) dt -= 1;
    std::vector<float> taps(2*k*15+1); liquid_firdes_prototype(LIQUID_FIRFILT_RRC, k, 15, .2f, dt, taps.data());
    auto modem = modemcf_create(qpsk ? LIQUID_MODEM_QPSK : LIQUID_MODEM_APSK8);
    auto carrier = nco_crcf_create(LIQUID_NCO), pilot = nco_crcf_create(LIQUID_NCO);
    constexpr float pi = 3.14159265358979323846f;
    nco_crcf_set_frequency(carrier, 2.f * pi * 1500.f / float(rate));
    nco_crcf_set_frequency(pilot, 2.f * pi * 100.f / float(rate));
    liquid_float_complex points[8];
    for (unsigned i = 0; i < count; ++i) modemcf_modulate(modem, i, &points[i]);
    double best = 0; std::vector<unsigned> sequence;
    for (std::size_t n = 0; n < (qpsk ? 12000u : 2880u); ++n) {
        nco_crcf_step(carrier); nco_crcf_step(pilot);
        if (n < 2000) continue;
        float projection[8];
        for (unsigned i = 0; i < count; ++i) {
            liquid_float_complex mixed{}; nco_crcf_mix_up(carrier, points[i], &mixed);
            projection[i] = mixed.real + mixed.imag;
        }
        for (const double direction : {-1., 1.}) {
            double amplitude = direction * .25 * nco_crcf_sin(pilot);
            std::vector<unsigned> candidate(n / k + 1, 0);
            for (std::size_t tap = n % k; tap < taps.size(); tap += k) {
                double maximum = -1e30; unsigned symbol = 0;
                for (unsigned i = 0; i < count; ++i) {
                    const double value = direction * taps[tap] * projection[i];
                    if (value > maximum) { maximum = value; symbol = i; }
                }
                candidate[n / k - tap / k] = qpsk ? (symbol ^ (symbol >> 1)) : symbol; amplitude += maximum;
            }
            if (amplitude > best) { best = amplitude; sequence = candidate; target = n; }
        }
    }
    modemcf_destroy(modem); nco_crcf_destroy(carrier); nco_crcf_destroy(pilot);
    return sequence;
}
}
int main() {
    try {
        require(TxModulator::bitRate == 6000 && TxModulator::symbolRate == 2000 &&
                TxModulator::samplesPerSymbol == 24, "Wrong HS-Modem 6000 mode");
        struct CountingSource : SymbolSource {
            unsigned count = 0;
            unsigned next() override { return count++ & 7u; }
        } counting;
        TxModulator clockCheck; std::vector<float> oneSecond(48000);
        clockCheck.render(oneSecond.data(), oneSecond.size(), counting, 1);
        require(counting.count == 2000, "Symbol clock is not 2000 symbols per second");
        const auto expected = reference(5000, 123);
        TxModulator a, b; RandomSymbols sa(123), sb(123);
        std::vector<float> whole(expected.size()), chunks(expected.size());
        a.render(whole.data(), whole.size(), sa, 1);
        for (std::size_t at = 0; at < chunks.size();) {
            const auto count = std::min<std::size_t>((at % 997) + 1, chunks.size() - at);
            b.render(chunks.data() + at, count, sb, 1); at += count;
        }
        require(whole == chunks, "Waveform changed at audio block boundaries");
        double cross = 0, energyReference = 0;
        for (std::size_t i = 1000; i < whole.size(); ++i) {
            cross += whole[i] * static_cast<double>(expected[i]);
            energyReference += expected[i] * static_cast<double>(expected[i]);
        }
        const auto amplification = static_cast<float>(cross / energyReference);
        std::cout << "Measured 6000-mode gain " << amplification << "\n";
        require(amplification > 3.1738f && amplification < 3.1741f, "Unexpected full-scale calibration");
        float maxError = 0, peak = 0;
        for (std::size_t i = 1000; i < whole.size(); ++i) {
            maxError = std::max(maxError, std::abs(whole[i] - expected[i] * amplification));
            peak = std::max(peak, std::abs(whole[i]));
        }
        require(maxError < 1e-6f, "HS-Modem reference waveform mismatch");
        require(peak < 1 && peak > .85f, "Unexpected peak or clipping");
        TxModulator halfModulator; RandomSymbols halfSource(123);
        std::vector<float> half(whole.size());
        halfModulator.render(half.data(), half.size(), halfSource, .5f);
        for (std::size_t i = 1000; i < half.size(); ++i)
            require(std::abs(half[i] - whole[i] * .5f) < 1e-6f, "Gain is not linear for data and pilot");
        for (const auto mode : {Modulation::Apsk8, Modulation::Qpsk4410}) {
            std::size_t target = 0;
            struct PeakSource : SymbolSource {
                std::vector<unsigned> symbols; std::size_t index = 0;
                unsigned next() override { return index < symbols.size() ? symbols[index++] : 0; }
            } worst;
            worst.symbols = peakSequence(target, mode);
            TxModulator worstModulator(mode); std::vector<float> adversarial(target + 1000);
            worstModulator.render(adversarial.data(), adversarial.size(), worst, 1);
            float worstPeak = 0;
            for (const float value : adversarial) worstPeak = std::max(worstPeak, std::abs(value));
            require(worstPeak > .99f && worstPeak < 1.f, "100 percent does not reach full scale without clipping");
            std::cout << modulationName(mode) << " adversarial full-scale peak " << worstPeak << "\n";
        }
        const auto qExpected = reference(5000, 123, Modulation::Qpsk4410);
        TxModulator qMod(Modulation::Qpsk4410); RandomSymbols qSymbols(123, Modulation::Qpsk4410);
        std::vector<float> qAudio(qExpected.size()); qMod.render(qAudio.data(), qAudio.size(), qSymbols, 1);
        double qCross = 0, qEnergy = 0;
        for (std::size_t i = 1000; i < qAudio.size(); ++i) { qCross += qAudio[i]*double(qExpected[i]); qEnergy += qExpected[i]*double(qExpected[i]); }
        const float qGain = qCross/qEnergy; float qError = 0;
        for (std::size_t i = 1000; i < qAudio.size(); ++i) qError = std::max(qError, std::abs(qAudio[i]-qExpected[i]*qGain));
        require(qError < 1e-6f && qGain > 2 && qGain < 5, "Original QPSK waveform/Gray mapping mismatch");
        struct QpskClock : SymbolSource { unsigned count = 0; unsigned next() override { return count++ & 3; } } qClock; TxModulator qClockMod(Modulation::Qpsk4410); std::vector<float> qSecond(44100);
        qClockMod.render(qSecond.data(), qSecond.size(), qClock, 1);
        require(qClock.count == 2205 && qClockMod.sampleRateHz() == 44100, "QPSK is not 4410 bit/s");
        std::cout << "QPSK original waveform max error " << qError << "; full-scale gain " << qGain << "\n";
        std::array<unsigned, 8> histogram{}; RandomSymbols random(19);
        for (unsigned n = 0; n < 80000; ++n) ++histogram.at(random.next());
        for (auto count : histogram) require(count > 9500 && count < 10500, "Symbol distribution is unbalanced");
        // A source held at the center point isolates the continuously added pilot.
        struct Center : SymbolSource { unsigned next() override { return 0; } } center;
        TxModulator p; std::vector<float> tone(48000); p.render(tone.data(), tone.size(), center, 1);
        double energy = 0; for (std::size_t i = 1000; i < tone.size(); ++i) energy += tone[i] * tone[i];
        const auto rms = std::sqrt(energy / (tone.size() - 1000));
        require(std::abs(rms - .025 * amplification / std::sqrt(2.)) < .0001, "Wrong pilot amplitude");
        p.render(tone.data(), tone.size(), center, 0);
        require(std::all_of(tone.begin() + 1000, tone.end(), [](float v) { return v == 0; }), "Gain zero leaves a pilot");
        bool rejected = false; try { p.render(tone.data(), 1, center, 1.1f); } catch (const std::invalid_argument&) { rejected = true; }
        require(rejected, "Invalid gain was accepted");
        std::cout << "HS-Modem waveform max error " << maxError << "; peak " << peak
                  << "; gain over original " << amplification << "; pilot RMS " << rms << "; arbitrary block boundaries identical\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
