// SPDX-License-Identifier: GPL-3.0-or-later
#include "mm_sstv/protocol.hpp"
#include <algorithm>
#include <utility>
#include <stdexcept>
namespace mm_sstv {
namespace {
constexpr std::array<std::uint8_t, 255> scramble = {
#include "scrambler_table.inc"
};
unsigned symbolAt(const AirFrame& frame, std::size_t symbol) {
    const auto bit = symbol * 3;
    unsigned value = 0;
    for (unsigned n = 0; n < 3; ++n) value = (value << 1) | ((frame[(bit + n) / 8] >> (7 - (bit + n) % 8)) & 1);
    return value;
}
}
std::uint16_t crc16(const std::uint8_t* data, std::size_t size) {
    std::uint16_t crc = 0xffff;
    for (std::size_t n = 0; n < size; ++n) {
        crc ^= data[n];
        for (unsigned bit = 0; bit < 8; ++bit) crc = (crc >> 1) ^ ((crc & 1) ? 0x8408 : 0);
    }
    return crc;
}
FrameEncoder::FrameEncoder() {
    unsigned value = 1;
    for (unsigned n = 0; n < 255; ++n) {
        exp_[n] = static_cast<std::uint8_t>(value); log_[value] = n;
        value <<= 1; if (value & 0x100) value ^= 0x187;
    }
    for (unsigned n = 255; n < exp_.size(); ++n) exp_[n] = exp_[n - 255];
    // Descending powers, product (x + alpha^120) ... (x + alpha^151).
    generator_[0] = 1;
    for (unsigned root = 0; root < 32; ++root) {
        std::array<std::uint8_t, 33> next{};
        for (unsigned n = 0; n <= root; ++n) {
            next[n] ^= generator_[n];
            next[n + 1] ^= multiply(generator_[n], exp_[120 + root]);
        }
        generator_ = next;
    }
}
std::uint8_t FrameEncoder::multiply(std::uint8_t a, std::uint8_t b) const {
    return a && b ? exp_[log_[a] + log_[b]] : 0;
}
AirFrame FrameEncoder::encode(const Payload& payload, unsigned type, unsigned status, unsigned counter) const {
    if (type > 15 || status > 3 || counter > 1023) throw std::invalid_argument("Invalid HS-Modem frame fields");
    std::array<std::uint8_t, 255> block{};
    block[0] = static_cast<std::uint8_t>(counter);
    block[1] = static_cast<std::uint8_t>(((counter >> 8) << 6) | (status << 4) | type);
    std::copy(payload.begin(), payload.end(), block.begin() + 2);
    const auto crc = crc16(block.data(), 221); block[221] = crc >> 8; block[222] = crc & 255;
    auto parity = block;
    for (unsigned n = 0; n < 223; ++n) {
        const auto coefficient = parity[n];
        for (unsigned j = 1; j <= 32; ++j) parity[n + j] ^= multiply(generator_[j], coefficient);
    }
    std::copy(parity.begin() + 223, parity.end(), block.begin() + 223);
    AirFrame air{}; air[0] = 0x53; air[1] = 0xe1; air[2] = 0xa6;
    for (std::size_t n = 0; n < block.size(); ++n) air[3 + n] = block[n] ^ scramble[n];
    return air;
}
std::string jpegFileName(std::string input) {
    const auto slash = input.find_last_of("/\\"); if (slash != std::string::npos) input.erase(0, slash + 1);
    const auto dot = input.find_last_of('.'); if (dot != std::string::npos) input.resize(dot);
    std::string name;
    for (const unsigned char c : input) {
        if (name.size() == 46) break;
        name += ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                 (c >= '0' && c <= '9') || c == '-' || c == '_') ? static_cast<char>(c) : '_';
    }
    if (name.empty()) name = "image";
    return name + ".jpg";
}
ImageTransmission prepareImage(const std::vector<std::uint8_t>& jpeg, std::string filename) {
    if (jpeg.empty() || jpeg.size() > ImageTransmission::maxJpegBytes)
        throw std::invalid_argument("JPEG must contain 1..200000 bytes");
    ImageTransmission result;
    result.filename = jpegFileName(std::move(filename)); result.jpegBytes = jpeg.size();
    result.fileId = crc16(jpeg.data(), jpeg.size());
    std::vector<std::uint8_t> content(55 + jpeg.size());
    std::copy(result.filename.begin(), result.filename.end(), content.begin());
    content[50] = result.fileId >> 8; content[51] = result.fileId & 255;
    content[52] = (jpeg.size() >> 16) & 255; content[53] = (jpeg.size() >> 8) & 255; content[54] = jpeg.size() & 255;
    std::copy(jpeg.begin(), jpeg.end(), content.begin() + 55);
    result.fileFrames = (content.size() + 218) / 219;
    result.frames.reserve(result.fileFrames + 26);
    FrameEncoder encoder;
    for (std::size_t frame = 0; frame < result.fileFrames; ++frame) {
        Payload payload{};
        const auto at = frame * 219; const auto count = std::min<std::size_t>(219, content.size() - at);
        std::copy_n(content.begin() + at, count, payload.begin());
        const unsigned status = result.fileFrames == 1 ? 3 : frame == 0 ? 0 : frame + 1 == result.fileFrames ? 2 : 1;
        const auto air = encoder.encode(payload, 2, status, frame);
        // Original cold-start acquisition: first send plus 21 repeats (~7.6 s at 6000 bit/s).
        const unsigned copies = frame == 0 ? 22 : frame + 1 == result.fileFrames ? 6 : 1;
        for (unsigned copy = 0; copy < copies; ++copy) result.frames.push_back(air);
    }
    return result;
}
unsigned FrameSymbols::next() {
    if (symbol_ >= frames_.size() * 688) return 0; // RRC tail; center symbols, no more frames.
    const auto at = symbol_++;
    return symbolAt(frames_[at / 688], at % 688);
}
unsigned BerSymbols::next() {
    if (symbol_ == 688) {
        Payload payload{}; unsigned character = 'A';
        for (auto& value : payload) { value = character++; if (character == 'z') character = 'A'; }
        frame_ = encoder_.encode(payload, 1, counter_ == 0 ? 0 : 1, counter_); counter_ = (counter_ + 1) % 1024; symbol_ = 0;
    }
    return symbolAt(frame_, symbol_++);
}
}
