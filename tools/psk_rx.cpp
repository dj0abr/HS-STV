// SPDX-License-Identifier: GPL-3.0-or-later
// Portable float32 soundcard adapter for QO100_TRX2's native PSK decoder.
#include "beacon_dsp.hpp"
#include "mm_sstv/audio_spectrum.hpp"
#include <liquid/liquid.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <deque>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif
namespace {
constexpr unsigned rate = 12000;
constexpr std::uint64_t dial = 10489748500ULL; // Nominal 1500-Hz USB audio, +/-500-Hz acquisition.
struct Job { std::uint64_t start; std::vector<std::int16_t> pcm; };
struct Output { std::uint64_t start; qo100::beacon::Result result; std::string error; };
// One running window plus the newest pending one. Audio capture never waits for DSP.
class Worker {
public:
    Worker() : thread_([this] { run(); }) {}
    ~Worker() {
        { std::lock_guard<std::mutex> lock(mutex_); stopping_ = true; pending_.reset(); }
        ready_.notify_all(); thread_.join();
    }
    void submit(Job job) {
        { std::lock_guard<std::mutex> lock(mutex_); pending_ = std::move(job); }
        ready_.notify_all();
    }
    std::deque<Output> take(bool finish = false) {
        std::unique_lock<std::mutex> lock(mutex_);
        if (finish) ready_.wait(lock, [this] { return !pending_ && !busy_; });
        std::deque<Output> result; result.swap(outputs_); return result;
    }
private:
    void run() {
        for (;;) {
            Job job;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                ready_.wait(lock, [this] { return stopping_ || pending_.has_value(); });
                if (stopping_) return;
                job = std::move(*pending_); pending_.reset(); busy_ = true;
            }
            Output output{job.start, {}, {}};
            try { output.result = qo100::beacon::decode(job.pcm, rate, dial); }
            catch (const std::exception& error) { output.error = error.what(); }
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (outputs_.size() == 4) outputs_.pop_front();
                outputs_.push_back(std::move(output)); busy_ = false;
            }
            ready_.notify_all();
        }
    }
    std::mutex mutex_; std::condition_variable ready_;
    std::optional<Job> pending_; std::deque<Output> outputs_;
    bool stopping_ = false, busy_ = false; std::thread thread_;
};
class Decimator {
public:
    Decimator() : filter_(firdecim_rrrf_create_kaiser(4, 8, 60)) { firdecim_rrrf_set_scale(filter_, .25f); }
    ~Decimator() { firdecim_rrrf_destroy(filter_); }
    float execute(float* samples) { float value = 0; firdecim_rrrf_execute(filter_, samples, &value); return value; }
private:
    firdecim_rrrf filter_;
};
}
int main(int argc, char** argv) {
#ifdef _WIN32
    _setmode(_fileno(stdin), _O_BINARY);
#endif
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--monitor") continue;
        if (arg == "--input-rate" && i + 1 < argc && std::string(argv[++i]) == "48000") continue;
        if (arg == "--modulation" && i + 1 < argc && std::string(argv[++i]) == "psk-beacon") continue;
        std::fprintf(stderr, "Usage: hs-stv-psk-rx-core --input-rate 48000 --monitor --modulation psk-beacon < mono-float32.raw\n"); return 2;
    }
    try {
        mm_sstv::AudioSpectrum spectrum;
        Decimator decimator; Worker worker;
        std::vector<std::int16_t> history(40 * rate); std::size_t position = 0, filled = 0;
        std::uint64_t samples = 0, next = 20 * rate, frames = 0, correctedFrames = 0, correctedBytes = 0, lastVerifiedEnd = 0;
        double carrier = 1500, level = -200;
        std::deque<qo100::beacon::Frame> recent;
        const auto publish = [&](std::deque<Output> outputs) {
            for (auto& output : outputs) {
                if (!output.error.empty()) throw std::runtime_error(output.error);
                if (!output.result.frames.empty()) carrier = output.result.carrier;
                for (auto& frame : output.result.frames) {
                    frame.sample += output.start;
                    lastVerifiedEnd = std::max(lastVerifiedEnd, frame.sample + (frame.fec ? 13 * rate : 11 * rate));
                    if (std::any_of(recent.begin(), recent.end(), [&](const auto& previous) {
                        return previous.fec == frame.fec && previous.payload == frame.payload &&
                            std::abs(std::int64_t(previous.sample) - std::int64_t(frame.sample)) < rate / 2;
                    })) continue;
                    ++frames;
                    if (frame.corrections) { ++correctedFrames; correctedBytes += unsigned(frame.corrections); }
                    const auto text = qo100::beacon::format(frame, carrier);
                    std::printf("{\"event\":\"psk_text\",\"text_hex\":\"");
                    for (const unsigned char c : text) std::printf("%02x", c);
                    std::printf("\"}\n");
                    recent.push_back(std::move(frame)); if (recent.size() > 64) recent.pop_front();
                }
            }
        };
        std::array<float, 4800> block{}; std::array<float, 4> group{}; unsigned pending = 0;
        for (;;) {
            const auto bytes = std::fread(block.data(), 1, sizeof(block), stdin);
            if (bytes % sizeof(float)) throw std::runtime_error("Truncated float32 audio sample");
            const auto count = bytes / sizeof(float); double power = 0;
            for (std::size_t i = 0; i < count; ++i) {
                const float value = std::isfinite(block[i]) ? std::clamp(block[i], -1.f, 1.f) : 0;
                block[i] = value; power += double(value) * value; group[pending++] = value;
                if (pending != 4) continue;
                pending = 0;
                const auto filtered = decimator.execute(group.data());
                history[position] = std::int16_t(std::lround(std::clamp(double(filtered), -1., 32767. / 32768) * 32768));
                position = (position + 1) % history.size(); filled = std::min(filled + 1, history.size()); ++samples;
                if (samples >= next) {
                    Job job{samples - filled, {}}; job.pcm.reserve(filled);
                    const auto first = (position + history.size() - filled) % history.size();
                    for (std::size_t j = 0; j < filled; ++j) job.pcm.push_back(history[(first + j) % history.size()]);
                    worker.submit(std::move(job)); next += 10 * rate;
                }
            }
            publish(worker.take(bytes < sizeof(block)));
            if (count) {
                spectrum.push(block.data(), count);
                level = 10 * std::log10(std::max(power / count, 1e-20));
            }
            if (count || bytes < sizeof(block)) {
                const auto db = spectrum.levels();
                const bool locked = lastVerifiedEnd && samples < lastVerifiedEnd + 20 * rate;
                const char* state = samples < 20 * rate ? "Sammle Audio · erster Text nach 20–40 s" : "Suche PSK-Rahmen";
                std::printf("{\"event\":\"status\",\"locked\":%s,\"decode_state\":\"%s\",\"valid_frames\":%llu,\"fec_corrected_frames\":%llu,\"fec_corrected_bytes\":%llu,\"center_hz\":%.3f,\"frequency_offset_hz\":%.3f,\"rms_dbfs\":%.2f,\"spectrum_bin_hz\":%.8f,\"spectrum_dbfs\":[",
                    locked ? "true" : "false", state, static_cast<unsigned long long>(frames), static_cast<unsigned long long>(correctedFrames),
                    static_cast<unsigned long long>(correctedBytes), carrier, carrier - 1500,
                    level, spectrum.frequencyStep());
                for (std::size_t i = 0; i < db.size(); ++i) std::printf("%s%.1f", i ? "," : "", db[i]);
                std::printf("]}\n"); std::fflush(stdout);
            }
            if (bytes < sizeof(block)) break;
        }
        std::fflush(stdout); return std::ferror(stdin) ? 1 : 0;
    } catch (const std::exception& error) { std::fprintf(stderr, "PSK decoder: %s\n", error.what()); return 1; }
}
