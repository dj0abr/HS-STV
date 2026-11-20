// SPDX-License-Identifier: GPL-3.0-or-later
// Independent 12-kHz waveforms for the added modes, without the production timing table.
#include "sstv/sstv.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <vector>
using namespace qo100::sstv;
namespace {
void require(bool ok, const char* text) { if (!ok) throw std::runtime_error(text); }
struct Spec { Mode mode; unsigned vis; double scan, porch, sync, offset; bool scottie; };
struct Signal {
    std::vector<std::int16_t> audio;
    double end = 0, phase = 0, offset;
    unsigned random = 17;
    explicit Signal(double shift) : offset(shift) {}
    void tone(double hz, double ms) {
        end += ms;
        const auto until = std::size_t(std::llround(end * 12));
        while (audio.size() < until) {
            random = random * 1664525u + 1013904223u;
            const int noise = int(random >> 24) - 128;
            audio.push_back(hz ? std::int16_t(std::lround(20000 * std::sin(phase) + noise)) : 0);
            phase = std::remainder(phase + 2 * std::acos(-1.0) * (hz + offset) / sample_rate, 2 * std::acos(-1.0));
        }
    }
};
void reference(const Spec& spec, bool header) {
    Image image{};
    for (unsigned y = 0; y < height; ++y) for (unsigned x = 0; x < width; ++x) for (unsigned c = 0; c < 3; ++c)
        image[y][3*x+c] = 25 + (x/80*44 + y/16*19 + c*71) % 202;
    Signal signal(spec.offset); signal.tone(0, 43);
    if (header) {
        signal.tone(1900, 300); signal.tone(1200, 10); signal.tone(1900, 300); signal.tone(1200, 30);
        unsigned parity = 0;
        for (unsigned bit = 0; bit < 7; ++bit) { const auto value = (spec.vis >> bit) & 1; parity ^= value; signal.tone(value ? 1100 : 1300, 30); }
        signal.tone(parity ? 1100 : 1300, 30); signal.tone(1200, 30);
        if (spec.scottie) signal.tone(1200, 9);
    }
    for (unsigned y = 0; y < height; ++y) {
        for (unsigned color : {1u, 2u, 0u}) {
            if ((!spec.scottie && color == 1) || (spec.scottie && color == 0))
                signal.tone(header && (y == 90 || y == 91) ? 0 : 1200, spec.sync);
            signal.tone(1500, spec.porch);
            for (unsigned x = 0; x < width; ++x) signal.tone(1500 + 800. * image[y][3*x+color] / 255, spec.scan / width);
        }
        if (!spec.scottie) signal.tone(1500, spec.porch);
    }
    std::vector<Frame> frames; std::vector<Line> lines; std::vector<bool> endings;
    Decoder decoder({[&](Frame frame) { frames.push_back(frame); },
                     [&](const Line& line) { lines.push_back(line); },
                     [&](bool complete) { endings.push_back(complete); }});
    for (std::size_t at = 0; at < signal.audio.size(); at += 173)
        decoder.push(signal.audio.data() + at, std::min<std::size_t>(173, signal.audio.size() - at));
    decoder.finish();
    require(frames.size() == 1 && frames[0].mode == spec.mode && frames[0].partial == !header, "Wrong fast SSTV mode / VIS / late entry");
    require(lines.size() >= 248 && lines.size() <= height, "Fast SSTV reference lost too many rows");
    if (header) {
        require(lines.size() == height && endings == std::vector<bool>{true}, "Fast SSTV reference did not finish all 256 rows");
        require(!lines[90].synchronized && !lines[91].synchronized && lines[92].synchronized, "Fast SSTV failed to recover missing syncs");
    }
    const unsigned first = height - unsigned(lines.size()); double error = 0; unsigned count = 0;
    for (unsigned y = 0; y < lines.size(); ++y) for (unsigned x : {35u, 115u, 195u, 275u}) for (unsigned c = 0; c < 3; ++c) {
        error += std::abs(int(lines[y].rgb[3*x+c]) - image[y+first][3*x+c]); ++count;
    }
    require(error/count < 6, "Fast SSTV reference RGB/color order/timing differs");
    require(std::abs(lines[50].frequency_offset_hz - spec.offset) < 8, "Fast SSTV reference AFC differs");
}
}
int main() {
    try {
        for (const auto& spec : {Spec{Mode::martin_m2,40,73.216,.572,4.862,180,false},
                                  Spec{Mode::scottie_2,56,88.064,1.5,9,-190,true}}) {
            reference(spec, true); reference(spec, false);
        }
        std::cout << "Martin2/Scottie2 independent waveforms: VIS, late entry, AFC, RGB, missing sync recovery passed\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
