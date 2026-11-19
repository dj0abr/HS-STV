// SPDX-License-Identifier: GPL-3.0-or-later
// Receive-only audio adapter for DJ0ABR's HS-Modem decoding blocks.
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif
#include "hsmodem.h"
#include "pilot_afc.h"
#include "mm_sstv/audio_spectrum.hpp"
#include "mm_sstv/modem_mode.hpp"
#include <memory>
#include <algorithm>
#include <deque>
#include <vector>
#include <string>

int bitsPerSymbol = 3, constellationSize = 8, speed = mm_sstv::ModemMode::bitRate;


int main(int argc, char** argv) {
#ifdef _WIN32
    _setmode(_fileno(stdin), _O_BINARY);
#endif
    double center = 1500;
    bool automatic = true, monitor_enabled = false;
    unsigned input_rate = 48000;
    auto modulation = mm_sstv::Modulation::Apsk8;
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--manual") automatic = false;
        else if (std::string(argv[i]) == "--monitor") monitor_enabled = true;
        else if (std::string(argv[i]) == "--modulation" && i+1 < argc) {
            const std::string value = argv[++i];
            if (value == "8apsk") modulation = mm_sstv::Modulation::Apsk8;
            else if (value == "qpsk4410") modulation = mm_sstv::Modulation::Qpsk4410;
            else { fprintf(stderr, "Modulation must be 8apsk or qpsk4410\n"); return 2; }
        }
        else if (std::string(argv[i]) == "--input-rate" && i+1 < argc) {
            std::string value = argv[++i];
            if(value != "12000" && value != "48000" && value != "44100") {
                fprintf(stderr, "Input rate must be 12000, 44100 or 48000\n"); return 2;
            }
            input_rate = static_cast<unsigned>(std::stoul(value));
        }
        else if (std::string(argv[i]) == "--center-hz" && i+1 < argc) {
            char* end = nullptr;
            const char* value = argv[++i];
            center = strtod(value, &end);
            if (end == value || *end || !std::isfinite(center) || center < 100 || center > 10000) {
                fprintf(stderr, "Invalid center frequency\n"); return 2;
            }
        } else {
            fprintf(stderr, "Usage: rx_probe [--manual] [--center-hz 1500] [--input-rate 48000|44100|12000] [--monitor] [--modulation 8apsk|qpsk4410] < mono-float32le.raw\n");
            return 2;
        }
    }
    const auto mode = mm_sstv::parameters(modulation);
    bitsPerSymbol = mode.bitsPerSymbol; constellationSize = 1u << bitsPerSymbol; speed = mode.bitRate();
    const auto preDecimation = mode.rxDecimation;
    const double rate = mode.sampleRate, pllRate = mode.pllSampleRate();
    const auto scheme = modulation == mm_sstv::Modulation::Apsk8 ? LIQUID_MODEM_APSK8 : LIQUID_MODEM_QPSK;
    PilotDetector detector(mode.sampleRate);
    auto monitor = monitor_enabled ? std::make_unique<mm_sstv::AudioSpectrum>(mode.sampleRate) : nullptr;
    double trial_start = -1, trial_hz = 0, previous_candidate = -1, lock_started = -1;
    int stable_estimates = 0;
    std::vector<std::pair<double,double>> rejected;
    init_packer();
    auto nco = nco_crcf_create(LIQUID_NCO);
    nco_crcf_set_frequency(nco, 2 * M_PI * center / rate);
    auto decim = firdecim_crcf_create_kaiser(preDecimation, 8, 40);
    firdecim_crcf_set_scale(decim, 1.f/preDecimation);
    auto tracker = km_symtrack_cccf_create(LIQUID_FIRFILT_RRC, mm_sstv::ModemMode::rxSamplesPerSymbol, 7, 0.2f, scheme);
    km_symtrack_cccf_set_bandwidth(tracker, 0.9f);
    uint64_t samples = 0, frames = 0, symbols = 0;
    int pending = 0;
    std::vector<liquid_float_complex> group(preDecimation);
    std::deque<uint64_t> recent_frames;
    std::vector<liquid_float_complex> points;
    double power = 0;
    float peak = 0;
    uint64_t last_frame = 0;
    bool was_silent = false;
    auto resampler = input_rate != mode.sampleRate ? msresamp_rrrf_create(float(mode.sampleRate)/input_rate,80.f) : nullptr;
    const size_t block = input_rate/10;
    float input_buffer[4800], buffer[19264];
    while (true) {
        // fread may wait for a full block. PulseAudio is read independently by Python.
        size_t bytes = fread(input_buffer, 1, block*sizeof(float), stdin);
        if (bytes % sizeof(float)) {
            fprintf(stderr, "Truncated float32 audio sample\n"); return 2;
        }
        unsigned count = bytes / sizeof(float);
        if(resampler) msresamp_rrrf_execute(resampler,input_buffer,count,buffer,&count);
        else std::copy(input_buffer,input_buffer+count,buffer);
        if (monitor) monitor->push(buffer, count);
        bool silent = true;
        for (size_t i = 0; i < count; ++i)
            if (std::isfinite(buffer[i]) && fabsf(buffer[i]) > 1e-12f) silent = false;
        if (silent) {
            samples += count;
            pending = 0;
            was_silent = true;
        } else if (was_silent) {
            // Do not let the original AGC integrate exact zeros indefinitely.
            km_symtrack_cccf_destroy(tracker);
            tracker = km_symtrack_cccf_create(LIQUID_FIRFILT_RRC, mm_sstv::ModemMode::rxSamplesPerSymbol, 7, 0.2f, scheme);
            was_silent = false;
        }
        for (size_t i = 0; !silent && i < count; ++i) {
            float f = std::isfinite(buffer[i]) ? buffer[i] : 0;
            ++samples;
            power += double(f) * f;
            peak = std::max(peak, fabsf(f));
            nco_crcf_step(nco);
            liquid_float_complex input = {f, f}, mixed;
            nco_crcf_mix_down(nco, input, &mixed);
            group[pending++] = mixed;
            if (pending != preDecimation) continue;
            pending = 0;
            liquid_float_complex y, out[8];
            firdecim_crcf_execute(decim, group.data(), &y);
            unsigned int nout = 0, sym = 0;
            km_symtrack_execute(tracker, y, out, &nout, &sym);
            if (!nout) continue;
            // The upstream AGC can overflow on prolonged digital silence.
            // Recover immediately and keep JSON output finite.
            bool finite = std::isfinite(nco_crcf_get_frequency(tracker->nco));
            for (unsigned int j = 0; j < nout; ++j)
                finite = finite && std::isfinite(out[j].real) && std::isfinite(out[j].imag);
            if (!finite) {
                km_symtrack_cccf_destroy(tracker);
                tracker = km_symtrack_cccf_create(LIQUID_FIRFILT_RRC, mm_sstv::ModemMode::rxSamplesPerSymbol, 7, 0.2f, scheme);
                continue;
            }
            ++symbols;
            if (symbols % 4 == 0 && points.size() < 96) points.push_back(out[0]);
            uint8_t s = sym;
            if (bitsPerSymbol == 2) s ^= s >> 1; // HS-Modem QPSK post-Gray conversion.
            uint8_t* pl = unpack_data(&s, 1);
            if (!pl) continue;
            ++frames;
            last_frame = samples;
            recent_frames.push_back(samples);
            printf("{\"event\":\"frame\",\"audio_seconds\":%.6f,\"frame_type\":%u,"
                   "\"frame_number\":%u,\"frame_info\":%u,\"payload_hex\":\"",
                   samples / rate, pl[0], (pl[1] << 8) | pl[2], pl[3]);
            for (int j = 0; j < PAYLOADLEN; ++j) printf("%02x", pl[10+j]);
            printf("\"}\n");
        }
        if (count) {
            while (!recent_frames.empty() && samples - recent_frames.front() > 2 * mode.sampleRate)
                recent_frames.pop_front();
            double age = frames ? (samples - last_frame) / rate : -1;
            // A header match is never enough to declare synchronization.
            bool locked = recent_frames.size() >= 2 && age <= 1.0;
            double now = samples / rate;
            if (locked && lock_started < 0) lock_started = now;
            if (!locked) lock_started = -1;
            bool scanned = automatic && detector.push(buffer, count);
            if (scanned && locked) {
                trial_start = -1;
                previous_candidate = -1;
                stable_estimates = 0;
                // Transfer slow PLL correction into the audio mixer without a
                // phase discontinuity. The inner PLL retains fast tracking.
                double residual = nco_crcf_get_frequency(tracker->nco)*pllRate/(2*M_PI);
                if (now-lock_started >= 1 && std::isfinite(residual) && fabs(residual) > 0.2) {
                    double step = std::max(-0.5,std::min(0.5,0.15*residual));
                    double target = std::max(1400+PilotDetector::min_hz,std::min(1400+PilotDetector::max_hz,center+step));
                    step = target-center;
                    center = target;
                    nco_crcf_set_frequency(nco,2*M_PI*center/rate);
                    nco_crcf_set_frequency(tracker->nco,2*M_PI*(residual-step)/pllRate);
                }
            } else if (scanned && (!frames || age > 2.5)) {
                // Reject an unconfirmed tone after five seconds and try the
                // next persistent candidate. A tone alone never means lock.
                if (trial_start >= 0 && now-trial_start >= 5) {
                    rejected.push_back({trial_hz, now+15});
                    trial_start = -1;
                    previous_candidate = -1;
                    stable_estimates = 0;
                }
                rejected.erase(std::remove_if(rejected.begin(),rejected.end(),[&](const std::pair<double,double>& x) {
                    return x.second < now;
                }),rejected.end());
                if (trial_start < 0) {
                    double candidate = -1;
                    for (const auto& c : detector.candidates()) {
                        bool blocked = false;
                        for (const auto& x : rejected)
                            if (fabs(c.hz-x.first) < 4) blocked = true;
                        if (!blocked) { candidate = c.hz; break; }
                    }
                    if (candidate >= 0 && fabs(candidate-previous_candidate) < 1.5) ++stable_estimates;
                    else stable_estimates = candidate >= 0 ? 1 : 0;
                    previous_candidate = candidate;
                    if (stable_estimates >= 2) {
                        center = 1400+candidate;
                        trial_hz = candidate;
                        trial_start = now;
                        stable_estimates = 0;
                        nco_crcf_set_frequency(nco,2*M_PI*center/rate);
                        km_symtrack_cccf_destroy(tracker);
                        tracker = km_symtrack_cccf_create(LIQUID_FIRFILT_RRC,mm_sstv::ModemMode::rxSamplesPerSymbol,7,0.2f,scheme);
                        // Remove the old center's filter history after a jump.
                        firdecim_crcf_destroy(decim);
                        decim = firdecim_crcf_create_kaiser(preDecimation,8,40);
                        firdecim_crcf_set_scale(decim,1.f/preDecimation);
                        pending = 0;
                        recent_frames.clear();
                        points.clear();
                        locked = false;
                    }
                }
            }
            const char* afc_state = !automatic ? "manual" : locked ? "tracking" :
                (frames && age <= 2.5) ? "hold" : trial_start >= 0 ? "acquiring" : "searching";
            double measured_pilot = detector.candidates().empty() ? -1 : detector.candidates().front().hz;
            // Display follows the confirmed/trial channel rather than an unrelated stronger tone.
            if ((locked || trial_start >= 0) && !detector.candidates().empty()) {
                const auto nearest = std::min_element(detector.candidates().begin(), detector.candidates().end(),
                    [&](const auto& a, const auto& b) { return fabs(a.hz-(center-1400)) < fabs(b.hz-(center-1400)); });
                measured_pilot = fabs(nearest->hz-(center-1400)) < 5 ? nearest->hz : -1;
            }
            printf("{\"event\":\"status\",\"audio_seconds\":%.3f,\"locked\":%s,"
                   "\"valid_frames\":%llu,\"recent_frames\":%zu,\"frame_age_s\":%.3f,"
                   "\"header_candidates\":%llu,\"fec_failures\":%llu,\"crc_failures\":%llu,"
                   "\"rms_dbfs\":%.2f,\"peak_dbfs\":%.2f,\"pll_hz\":%.3f,"
                   "\"afc_enabled\":%s,\"afc_state\":\"%s\",\"center_hz\":%.3f,"
                   "\"frequency_offset_hz\":%.3f,\"pilot_hz\":%.3f,\"points\":[",
                   samples/rate, locked ? "true" : "false", (unsigned long long)frames,
                   recent_frames.size(), age, (unsigned long long)rx_header_candidates,
                   (unsigned long long)rx_fec_failures, (unsigned long long)rx_crc_failures,
                   10*log10(std::max(power/count, 1e-20)), 20*log10(std::max(double(peak),1e-10)),
                   nco_crcf_get_frequency(tracker->nco)*pllRate/(2*M_PI),
                   automatic ? "true" : "false",afc_state,center,center-1500,measured_pilot);
            for (size_t j = 0; j < points.size(); ++j)
                printf("%s[%.4f,%.4f]", j ? "," : "", points[j].real, points[j].imag);
            printf("],\"modulation\":\"%s\",\"bit_rate\":%d", bitsPerSymbol == 2 ? "qpsk4410" : "8apsk", speed);
            if (monitor) {
                const auto spectrum = monitor->levels();
                printf(",\"spectrum_bin_hz\":%.8f,\"spectrum_dbfs\":[", monitor->frequencyStep());
                for (size_t j = 0; j < spectrum.size(); ++j) printf("%s%.1f", j ? "," : "", spectrum[j]);
                printf("]");
            }
            printf("}\n"); fflush(stdout);
            power = 0; peak = 0; points.clear();
        }
        if (bytes < block*sizeof(float)) break;
    }
    km_symtrack_cccf_destroy(tracker);
    firdecim_crcf_destroy(decim);
    nco_crcf_destroy(nco);
    if(resampler) msresamp_rrrf_destroy(resampler);
    if (ferror(stdin)) { perror("audio input"); return 1; }
    return 0;
}
