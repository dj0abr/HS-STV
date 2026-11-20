// SPDX-License-Identifier: GPL-3.0-or-later
// Independent waveforms: sync holdover must preserve video and raster position.
#include "sstv/sstv.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <vector>

using namespace qo100::sstv;
namespace {
void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
struct Spec { Mode mode; const char* name; unsigned vis; double scan, porch, sync, period; bool scottie; };
struct Signal {
    std::vector<std::int16_t> pcm;
    double end = 0, phase = 0;
    static constexpr double scale = 1.002; // independent transmitter/receiver clocks
    void tone(double hz, double ms) {
        end += ms;
        const auto until = std::size_t(std::llround(end * 12 * scale));
        while (pcm.size() < until) {
            pcm.push_back(hz ? std::int16_t(std::lround(16000 * std::sin(phase))) : 0);
            phase = std::remainder(phase + 2 * std::acos(-1.) * (hz + 120) / sample_rate, 2 * std::acos(-1.));
        }
    }
};
Image image() {
    Image result{};
    for (unsigned y = 0; y < height; ++y) for (unsigned x = 0; x < width; ++x) for (unsigned c = 0; c < 3; ++c)
        result[y][3*x+c] = 25 + (x/80*44 + y/16*19 + c*71) % 202;
    return result;
}
struct Recording {
    std::vector<Frame> frames;
    std::vector<Line> lines;
    std::vector<bool> endings;
    Decoder::Callbacks callbacks() {
        return {[&](Frame frame) { frames.push_back(frame); },
                [&](const Line& line) { lines.push_back(line); },
                [&](bool complete) { endings.push_back(complete); }};
    }
};
void check_rgb(const Line& line, const RgbLine& expected) {
    for (unsigned x : {35u, 115u, 195u, 275u}) for (unsigned c = 0; c < 3; ++c)
        require(std::abs(int(line.rgb[3*x+c]) - expected[3*x+c]) < 9, "video shifted or lost during holdover/recovery");
}
void holdover(const Spec& spec, bool header, double shift_ms, bool sparse) {
    constexpr unsigned first = 50, restored = 74; // exceeds the old partial timeout
    const auto pixels = image();
    Signal signal;
    signal.tone(0, 43);
    if (header) {
        signal.tone(1900, 300); signal.tone(1200, 10); signal.tone(1900, 300); signal.tone(1200, 30);
        unsigned parity = 0;
        for (unsigned bit = 0; bit < 7; ++bit) {
            const auto value = (spec.vis >> bit) & 1; parity ^= value;
            signal.tone(value ? 1100 : 1300, 30);
        }
        signal.tone(parity ? 1100 : 1300, 30); signal.tone(1200, 30);
        if (spec.scottie) signal.tone(1200, spec.sync);
    }
    std::size_t shift_at = 0, outage_check_at = 0;
    for (unsigned y = 0; y < height; ++y) {
        if (y == restored-2) outage_check_at = signal.pcm.size();
        if (y == restored) shift_at = signal.pcm.size();
        const bool missing = (y >= first && y < restored) || (sparse && (y == restored+1 || y == restored+2));
        for (unsigned color : {1u, 2u, 0u}) {
            if ((spec.scottie && color == 0) || (!spec.scottie && color == 1))
                signal.tone(missing ? 0 : 1200, spec.sync);
            signal.tone(1500, spec.porch);
            for (unsigned x = 0; x < width; ++x)
                signal.tone(1500 + 800. * pixels[y][3*x+color] / 255, spec.scan / width);
        }
        if (!spec.scottie) signal.tone(1500, spec.porch);
    }
    const auto shift = std::ptrdiff_t(std::llround(shift_ms * 12));
    if (shift > 0) signal.pcm.insert(signal.pcm.begin() + shift_at, shift, 0);
    if (shift < 0) signal.pcm.erase(signal.pcm.begin() + shift_at + shift, signal.pcm.begin() + shift_at);
    Recording recording;
    Decoder decoder(recording.callbacks(), spec.mode);
    for (std::size_t at = 0; at < signal.pcm.size(); at += 173) {
        decoder.push(signal.pcm.data() + at, std::min<std::size_t>(173, signal.pcm.size() - at));
        // Verify delivery during the outage, not just after the signal returns.
        if (at >= outage_check_at && at < shift_at)
            require(!recording.lines.empty() && recording.lines.back().index >= first+16,
                "decoder withheld video until sync returned");
    }
    decoder.finish();
    require(recording.frames.size() == 1 && recording.frames[0].partial == !header, "sync loss restarted the image");
    require(recording.lines.size() >= 250 && recording.lines.size() <= height, "holdover dropped raster rows");
    if (header) require(recording.lines.size() == height, "VIS raster did not preserve all 256 rows");
    require(recording.endings == std::vector<bool>{header}, "incorrect completion status");
    const unsigned offset = height - unsigned(recording.lines.size());
    for (unsigned i = 0; i < recording.lines.size(); ++i)
        require(recording.lines[i].index == i, "raster row skipped or repeated");
    for (unsigned y = first; y < restored - 1; ++y) {
        const auto& line = recording.lines.at(y - offset);
        require(!line.synchronized, "missing sync was reported as detected");
        check_rgb(line, pixels[y]);
    }
    const unsigned resumed = restored + (std::abs(shift_ms) > 25 ? (sparse ? 3 : 1) : 0);
    require(recording.lines.at(resumed-offset).synchronized, "returning sync did not recover promptly");
    if (std::abs(shift_ms) > 25)
        require(!recording.lines.at(restored-offset).synchronized, "large phase jump was accepted without confirmation");
    for (unsigned y = resumed; y < height; ++y) {
        if (sparse && (y == restored+1 || y == restored+2)) continue;
        require(recording.lines.at(y-offset).synchronized, "sync drifted again after phase recovery");
        check_rgb(recording.lines.at(y-offset), pixels[y]);
    }
}
void loss_of_signal(const Spec& spec) {
    const auto pixels = image();
    Encoder encoder(spec.mode, pixels);
    std::vector<std::int16_t> pcm;
    std::int16_t block[240];
    for (std::size_t n; (n = encoder.read(block, 240));) pcm.insert(pcm.end(), block, block+n);
    // Start without VIS and then lose both video and sync permanently.
    const auto begin = std::size_t(std::llround((910 + (spec.scottie ? 9 : 0)) * 12));
    pcm.erase(pcm.begin(), pcm.begin() + begin);
    pcm.resize(std::size_t(std::llround(100 * spec.period * 12)));
    pcm.resize(pcm.size() + std::size_t(std::llround(24 * spec.period * 12)), 0);
    Recording recording;
    Decoder decoder(recording.callbacks(), spec.mode);
    decoder.push(pcm.data(), pcm.size());
    require(recording.endings == std::vector<bool>{false}, "late-entry reception did not stop after signal loss");
    require(recording.lines.size() < 120, "late-entry decoder kept running after signal loss");
    decoder.finish();
    require(recording.frames.size() == 1 && recording.endings.size() == 1, "signal loss caused spurious frames");
}
}
int main() {
    for (const auto& spec : {Spec{Mode::martin_m1,"M1",44,146.432,.572,4.862,446.446,false},
                             Spec{Mode::martin_m2,"M2",40,73.216,.572,4.862,226.798,false},
                             Spec{Mode::scottie_1,"S1",60,138.240,1.5,9,428.220,true},
                             Spec{Mode::scottie_2,"S2",56,88.064,1.5,9,277.692,true}}) {
        try {
            holdover(spec, true, 0, false);
            holdover(spec, true, 12, false);
            holdover(spec, true, 40, false);
            holdover(spec, true, 40, true);
            holdover(spec, true, -40, true);
            holdover(spec, false, 0, true);
            loss_of_signal(spec);
            std::cout << spec.name << ": continuous video, clock drift, phase jumps, sparse syncs, partial RX and signal loss passed\n";
        } catch (const std::exception& error) {
            std::cerr << spec.name << ": " << error.what() << '\n';
            return 1;
        }
    }
}
