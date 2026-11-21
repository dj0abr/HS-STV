// SPDX-License-Identifier: GPL-3.0-or-later
#include "mm_sstv/transmitter.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
using namespace mm_sstv;
namespace {
constexpr double pi = 3.14159265358979323846;
void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
class Output final : public AudioOutput {
public:
    Render render;
    unsigned rate = 0, starts = 0;
    std::vector<AudioDevice> devices() override { return {{"test", "Offline test"}}; }
    void start(const std::string&, Render callback, unsigned sampleRate) override { render = std::move(callback); rate = sampleRate; ++starts; }
    void stop() noexcept override { render = {}; }
    std::string error() const override { return {}; }
    std::uint64_t underruns() const override { return 0; }
};
void checkRaster(unsigned rate) {
    WaterfallTextAudio signal(" r_f ", rate);
    // Independent known columns of the reference glyphs, top row in bit zero.
    // Five active dot columns and one spacer give audio-triggered PTT 600 ms
    // before the first complete R; no leading blank glyph columns are allowed.
    const std::array<unsigned, 29> masks{384,384,384,384,384,0,511,33,33,33,97,146,268,0,256,256,256,256,256,256,256,0,511,17,17,17,17,17,1};
    const auto rowSamples = rate / 10, period = rate / 25;
    require(signal.frames() == masks.size() * rowSamples + rate * 3 / 10, "Wrong text duration");
    std::vector<float> audio(signal.frames());
    for (std::size_t at = 0; at < audio.size();) {
        const auto n = signal.render(audio.data() + at, std::min<std::size_t>(517, audio.size() - at), 1);
        require(n > 0, "Waterfall ended prematurely"); at += n;
    }
    float unused = 1; require(signal.render(&unused, 1, 1) == 0 && unused == 1, "End of text wrote past boundary");
    const auto firstAudio = std::find_if(audio.begin(), audio.end(), [](float x) { return std::abs(x) > .001; });
    require(firstAudio < audio.begin() + rate / 100, "PTT lead-in starts with silence");
    // A station that loses the first 400 ms still gets the entire first letter.
    require(std::size_t(firstAudio - audio.begin()) + rate * 4 / 10 < 6 * rowSamples, "Insufficient PTT lead-in");
    for (std::size_t row = 0; row < masks.size(); ++row) {
        const auto begin = row * rowSamples;
        require(audio[begin] == 0 && audio[begin + rowSamples - 1] == 0, "Text row has hard edges");
        for (unsigned band = 0; band < 9; ++band) for (unsigned tone = 0; tone < 7; ++tone) {
            const auto hz = 525 + band * 225 + tone * 25;
            std::complex<double> sum{};
            for (unsigned i = 0; i < period; ++i) {
                const double angle = -2 * pi * hz * i / rate;
                sum += double(audio[begin + rate * 3 / 100 + i]) * std::complex<double>(std::cos(angle), std::sin(angle));
            }
            const auto amplitude = std::abs(sum) / period;
            require((amplitude > .0001) == bool(masks[row] & (1u << band)), "Wrong glyph orientation or tone band");
        }
    }
    require(std::all_of(audio.begin() + masks.size() * rowSamples, audio.end(), [](float x) { return x == 0; }), "Missing silence before picture");
    const auto peak = std::abs(*std::max_element(audio.begin(), audio.end(), [](float a, float b) { return std::abs(a) < std::abs(b); }));
    require(peak > .9 && peak < 1, "Unexpected text peak or clipping");
    WaterfallTextAudio live("R", rate); std::vector<float> block(rate / 20);
    live.render(block.data(), block.size(), 1); live.render(block.data(), block.size(), 0);
    require(std::all_of(block.begin() + rate / 200, block.end(), [](float x) { return x == 0; }), "Live level change did not mute ID");
    live.render(block.data(), block.size(), 1);
    require(std::any_of(block.begin() + rate / 200, block.end(), [](float x) { return std::abs(x) > .001; }), "Live level change did not restore ID");
}
void checkTransmission(Modulation mode, const std::filesystem::path& outputFolder, unsigned percent) {
    const auto text = waterfallIdText(mode); const auto idFrames = waterfallIdSamples(mode);
    auto output = std::make_unique<Output>(); auto* device = output.get(); Transmitter tx(std::move(output)); tx.setLevel(percent);
    const float gain = percent / 100.f;
    const std::vector<std::uint8_t> jpeg{0xff,0xd8,0xff,0xd9}; SstvImage picture{};
    auto start = [&](bool enabled) {
        if (isSstv(mode)) tx.startSstv("test", mode, picture, enabled);
        else tx.startImage("test", prepareImage(jpeg, "test.jpg", mode), enabled);
    };
    start(true); require(tx.sendingWaterfallId() && tx.running(), "ID not before picture");
    require(device->rate == modeSampleRate(mode), "ID changed native sample rate");
    std::vector<float> actual(idFrames + 4096);
    for (std::size_t at = 0; at < actual.size();) {
        const auto count = std::min<std::size_t>(1009, actual.size() - at);
        require(device->render(actual.data() + at, count) == count, "Audio ended at ID/picture boundary"); at += count;
    }
    require(!tx.sendingWaterfallId() && device->starts == 1, "ID/picture transition reopened audio");
    WaterfallTextAudio expectedId(text, device->rate); std::vector<float> expected(idFrames);
    const float boost = float(std::pow(10., 10. / 20.));
    expectedId.render(expected.data(), expected.size(), isSstv(mode) ? gain * sstvMaximumGain * boost : gain);
    require(std::equal(expected.begin(), expected.end(), actual.begin()), "Wrong prefix or SSTV ID boost");
    WaterfallTextAudio unboostedId(text, device->rate); std::vector<float> unboosted(idFrames);
    unboostedId.render(unboosted.data(), unboosted.size(), isSstv(mode) ? gain * sstvMaximumGain : gain);
    double actualPower = 0, referencePower = 0;
    // Skip the initial gain ramp; measure the complete text, including lead-in.
    for (std::size_t at = device->rate / 100; at < idFrames; ++at) {
        require(std::isfinite(actual[at]) && std::abs(actual[at]) < 1, "ID clipped at configured TX level");
        actualPower += double(actual[at]) * actual[at];
        referencePower += double(unboosted[at]) * unboosted[at];
    }
    const double differenceDb = 10 * std::log10(actualPower / referencePower);
    require(std::abs(differenceDb - (isSstv(mode) ? 10 : 0)) < 1e-4, "Measured ID gain is not +10 dB for SSTV / 0 dB for digital");
    std::array<float,4096> expectedPicture{};
    std::size_t pictureFrames = 0;
    if (isSstv(mode)) {
        SstvAudio reference(mode, picture); reference.render(expectedPicture.data(), expectedPicture.size(), gain); pictureFrames = reference.frames();
    } else {
        ImageAudio reference(prepareImage(jpeg, "test.jpg", mode)); reference.render(expectedPicture.data(), expectedPicture.size(), gain); pictureFrames = reference.frames();
    }
    require(std::equal(expectedPicture.begin(), expectedPicture.end(), actual.begin() + idFrames), "ID changed picture samples");
    require(tx.progress() == (idFrames + 4096) * 100 / (idFrames + pictureFrames), "Progress ignores prefix");
    if (!outputFolder.empty() && percent == 100) {
        std::ofstream file(outputFolder / (std::string("id-") + modeKey(mode) + ".f32"), std::ios::binary);
        file.write(reinterpret_cast<const char*>(actual.data()), std::streamsize(idFrames * sizeof(float)));
        require(bool(file), "Cannot export test ID");
    }
    if (!isSstv(mode)) {
        std::array<float,777> tail{}; std::size_t remaining = 0;
        while (const auto n = device->render(tail.data(), tail.size())) remaining += n;
        require(remaining + 4096 == pictureFrames && tx.progress() == 100, "Final samples or completion lost");
    }
    tx.stop(); require(!tx.running() && !device->render, "STOP did not release audio");
    start(false); std::array<float,4096> without{};
    device->render(without.data(), without.size());
    require(!tx.sendingWaterfallId() && without == expectedPicture, "Disabled ID changed legacy TX");
    tx.stop(); start(true);
    std::array<float,4096> live{}; device->render(live.data(), live.size()); tx.setLevel(0);
    device->render(live.data(), live.size());
    require(std::all_of(live.begin() + device->rate / 200, live.end(), [](float x) { return x == 0; }), "TX level did not mute ID");
    tx.setLevel(percent); device->render(live.data(), live.size());
    require(std::any_of(live.begin() + device->rate / 200, live.end(), [](float x) { return std::abs(x) > .001; }), "TX level did not restore ID");
    tx.stop(); require(!device->render, "STOP during ID left callback active");
}
}
int main(int argc, char** argv) {
    try {
        checkRaster(48000); checkRaster(44100);
        for (auto text : {"", "   ", "TEST!", "ABCDEFGHIJKLMNOPQRSTUVWXYZ"}) {
            bool rejected = false; try { WaterfallTextAudio bad(text, 48000); } catch (const std::invalid_argument&) { rejected = true; }
            require(rejected, "Invalid text accepted");
        }
        std::filesystem::path outputFolder;
        if (argc == 2) { outputFolder = argv[1]; std::filesystem::create_directories(outputFolder); }
        const std::array<Modulation,6> modes{Modulation::Apsk8,Modulation::Qpsk4410,Modulation::Martin1,Modulation::Martin2,Modulation::Scottie1,Modulation::Scottie2};
        const std::array<std::string,6> labels{"HS-STV 8APSK","HS-STV QPSK","HS-STV MARTIN1","HS-STV MARTIN2","HS-STV SCOTTIE1","HS-STV SCOTTIE2"};
        for (std::size_t i=0;i<modes.size();++i) {
            require(waterfallIdText(modes[i]) == labels[i], "Wrong mode label");
            for (const auto percent : {35u, 100u}) checkTransmission(modes[i], outputFolder, percent);
        }
        std::cout << "Waterfall ID: PTT lead-in, glyphs, 63 tones, 44.1/48 kHz, SSTV +10 dB / digital unchanged at 35/100%, live gain, clipping, picture transition, progress, STOP and disabled path passed\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
