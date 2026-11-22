// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QFile>
#include <QString>
#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <vector>
namespace psk_test {
struct Wave { unsigned rate; std::vector<std::int16_t> pcm; };
inline Wave wave(const QString& path) {
    QFile file(path); if (!file.open(QIODevice::ReadOnly)) throw std::runtime_error("Cannot open PSK recording");
    const auto bytes = file.readAll();
    const auto u16 = [&](int at) { return unsigned(static_cast<unsigned char>(bytes.at(at))) | unsigned(static_cast<unsigned char>(bytes.at(at + 1))) << 8; };
    const auto u32 = [&](int at) { return u16(at) | (std::uint32_t(u16(at + 2)) << 16); };
    if (bytes.size() < 44 || bytes.left(4) != "RIFF" || bytes.mid(8, 4) != "WAVE") throw std::runtime_error("Invalid PSK WAV");
    unsigned rate = 0, channels = 0; int start = 0, count = 0;
    for (int at = 12; at + 8 <= bytes.size();) {
        const auto size = u32(at + 4); const int payload = at + 8;
        if (bytes.mid(at, 4) == "fmt ") {
            if (size < 16 || payload + 16 > bytes.size() || u16(payload) != 1 || u16(payload + 14) != 16) throw std::runtime_error("Expected PCM16");
            channels = u16(payload + 2); rate = u32(payload + 4);
        } else if (bytes.mid(at, 4) == "data") { start = payload; count = int(std::min(size, unsigned(bytes.size() - payload))); }
        if (size > unsigned(bytes.size() - payload)) break;
        at = payload + int(size) + int(size & 1);
    }
    if (!start || (channels != 1 && channels != 2) || rate != 8000 || count % (2 * channels)) throw std::runtime_error("Unexpected PSK WAV format");
    Wave result{rate, {}};
    for (int at = start; at < start + count; at += 2 * int(channels)) result.pcm.push_back(std::int16_t(u16(at)));
    return result;
}
// Independent linear interpolation of the 8-kHz recording to soundcard float32.
inline std::vector<float> audio48(const Wave& wave, unsigned seconds) {
    const auto size = std::min(wave.pcm.size(), std::size_t(seconds) * wave.rate);
    std::vector<float> result; result.reserve(size * 6);
    for (std::size_t i = 0; i < size; ++i) for (unsigned j = 0; j < 6; ++j) {
        const double a = wave.pcm[i], b = wave.pcm[std::min(i + 1, size - 1)];
        result.push_back(float((a + (b - a) * j / 6.) / 32768));
    }
    return result;
}
}
