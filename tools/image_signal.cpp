// SPDX-License-Identifier: GPL-3.0-or-later
// Offline export only: never opens an audio output.
#include "image_codec.hpp"
#include "mm_sstv/image_audio.hpp"
#include <QCoreApplication>
#include <QImageReader>
#include <QFileInfo>
#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <iostream>
#include <stdexcept>
namespace {
void word(std::ostream& out, std::uint32_t value, unsigned bytes) {
    for (unsigned n = 0; n < bytes; ++n) out.put(static_cast<char>((value >> (8 * n)) & 255));
}
}
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    try {
        if (argc != 3 && argc != 6) {
            std::cerr << "Usage: mm-sstv-image-signal INPUT OUTPUT.wav [WIDTH HEIGHT LEVEL_PERCENT]\n"; return 2;
        }
        const int width = argc == 6 ? std::stoi(argv[3]) : 640, height = argc == 6 ? std::stoi(argv[4]) : 480;
        const int level = argc == 6 ? std::stoi(argv[5]) : 100;
        if (level < 0 || level > 100) throw std::invalid_argument("Level must be 0..100");
        QImageReader reader(QString::fromLocal8Bit(argv[1])); reader.setAutoTransform(true);
        const auto original = reader.read();
        const auto picture = mm_sstv::preparePicture(original, QSize(width, height));
        auto transmission = mm_sstv::prepareImage(picture.jpeg, QFileInfo(QString::fromLocal8Bit(argv[1])).fileName().toUtf8().toStdString());
        const auto count = transmission.audioFrames();
        mm_sstv::ImageAudio signal(std::move(transmission));
        std::ofstream jpeg(std::string(argv[2]) + ".jpg", std::ios::binary);
        jpeg.exceptions(std::ios::badbit | std::ios::failbit);
        jpeg.write(reinterpret_cast<const char*>(picture.jpeg.data()), picture.jpeg.size()); jpeg.close();
        std::ofstream out(argv[2], std::ios::binary); out.exceptions(std::ios::badbit | std::ios::failbit);
        out.write("RIFF", 4); word(out, 36 + count * 2, 4); out.write("WAVEfmt ", 8);
        word(out, 16, 4); word(out, 1, 2); word(out, 1, 2); word(out, 48000, 4); word(out, 96000, 4);
        word(out, 2, 2); word(out, 16, 2); out.write("data", 4); word(out, count * 2, 4);
        std::array<float, 960> buffer{};
        while (const auto frames = signal.render(buffer.data(), buffer.size(), level / 100.f)) {
            for (std::size_t n = 0; n < frames; ++n) {
                if (!std::isfinite(buffer[n]) || std::abs(buffer[n]) >= 1) throw std::runtime_error("Clipped image audio");
                word(out, static_cast<std::uint16_t>(static_cast<std::int16_t>(std::lround(buffer[n] * 32767))), 2);
            }
        }
        out.close(); std::cout << "JPEG " << picture.jpeg.size() << " bytes; audio " << count << " samples\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
