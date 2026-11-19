// SPDX-License-Identifier: GPL-3.0-or-later
#include "mm_sstv/protocol.hpp"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace mm_sstv;
void require(bool ok, const char* text) { if (!ok) throw std::runtime_error(text); }
int main(int argc, char** argv) {
    try {
        require(argc == 2, "Fixture path missing"); std::ifstream input(argv[1], std::ios::binary);
        require(bool(input), "Cannot open original fixture");
        FrameEncoder encoder; std::array<std::uint8_t, 221> mask{};
        for (unsigned n = 0; n < 64; ++n) {
            std::array<std::uint8_t, 4> meta{}; Payload payload{}; AirFrame expected{};
            input.read(reinterpret_cast<char*>(meta.data()), meta.size());
            input.read(reinterpret_cast<char*>(payload.data()), payload.size());
            input.read(reinterpret_cast<char*>(expected.data()), expected.size());
            require(bool(input), "Short fixture");
            const auto actual = encoder.encode(payload, meta[0], meta[1], (meta[2] << 8) | meta[3]);
            require(actual == expected, "Frame differs from original Pack/CRC/Schifra/scrambler");
            if (!n) {
                mask[0] = expected[3] ^ meta[3];
                mask[1] = expected[4] ^ ((meta[2] << 6) | (meta[1] << 4) | meta[0]);
                for (unsigned i = 0; i < 219; ++i) mask[i + 2] = expected[5 + i] ^ payload[i];
            }
        }
        const auto extract = [&](const AirFrame& f) {
            std::array<std::uint8_t, 221> raw{};
            for (std::size_t i = 0; i < raw.size(); ++i) raw[i] = f[3 + i] ^ mask[i];
            return raw;
        };
        const char* check = "123456789";
        require(crc16(reinterpret_cast<const std::uint8_t*>(check), 9) == 0x6f91, "CRC16 convention wrong");
        for (std::size_t length : {4u, 164u, 165u, 383u, 384u, 56010u, 200000u}) {
            std::vector<std::uint8_t> jpeg(length);
            for (std::size_t i = 0; i < length; ++i) jpeg[i] = (i * 47 + 19) & 255;
            const auto tx = prepareImage(jpeg, "C:\\photos\\a name.png");
            require(tx.filename == "a_name.jpg", "Bad JPEG filename");
            require(tx.fileFrames == (length + 55 + 218) / 219, "Wrong number of file fragments");
            require(tx.frames.size() == (tx.fileFrames == 1 ? 22 : tx.fileFrames + 26), "Missing acquisition/final repeats");
            std::vector<std::uint8_t> recovered; unsigned previous = 1024;
            for (const auto& f : tx.frames) {
                const auto raw = extract(f); const unsigned counter = ((raw[1] >> 6) << 8) | raw[0];
                require((raw[1] & 15) == 2, "Not an image frame");
                if (counter == previous) continue;
                require(counter == recovered.size() / 219, "Counter discontinuity");
                const auto status = (raw[1] >> 4) & 3;
                require(status == (tx.fileFrames == 1 ? 3 : counter == 0 ? 0 : counter + 1 == tx.fileFrames ? 2 : 1), "Wrong file status");
                recovered.insert(recovered.end(), raw.begin() + 2, raw.end()); previous = counter;
            }
            const auto id = (recovered[50] << 8) | recovered[51];
            const unsigned size = (recovered[52] << 16) | (recovered[53] << 8) | recovered[54];
            require(id == crc16(jpeg.data(), jpeg.size()) && size == jpeg.size(), "Wrong file ID/size byte order");
            require(std::equal(jpeg.begin(), jpeg.end(), recovered.begin() + 55), "Reconstructed JPEG differs");
            require(std::all_of(recovered.begin() + 55 + length, recovered.end(), [](auto x) { return x == 0; }), "Bad padding");
            FrameSymbols symbols(tx.frames); const unsigned header[]{2, 4, 7, 6, 0, 6, 4, 6};
            for (auto value : header) require(symbols.next() == value, "Wrong MSB-first symbol packing");
        }
        const std::vector<std::uint8_t> content(56010, 0x42);
        const auto tx8 = prepareImage(content, "mode.jpg");
        const auto qpsk = prepareImage(content, "mode.jpg", Modulation::Qpsk4410);
        require(qpsk.frames == tx8.frames, "QPSK changed the protected frame bytes");
        require(qpsk.symbols() == qpsk.frames.size() * 1032, "Wrong QPSK frame length");
        FrameSymbols packed(qpsk.frames, Modulation::Qpsk4410);
        for (const auto& frame : qpsk.frames) for (const auto value : frame)
            for (int shift = 6; shift >= 0; shift -= 2) require(packed.next() == ((value >> shift) & 3), "Wrong QPSK bit packing");
        require(packed.next() == SymbolSource::silence, "QPSK RRC tail is not zero baseband");
        BerSymbols ber;
        Payload berPayload{}; unsigned character = 'A';
        for (auto& value : berPayload) { value = character++; if (character == 'z') character = 'A'; }
        for (unsigned counter = 0; counter < 2; ++counter) {
            AirFrame air{};
            for (unsigned symbol = 0; symbol < 688; ++symbol) {
                const auto value = ber.next();
                for (unsigned bit = 0; bit < 3; ++bit) {
                    const auto position = symbol * 3 + bit;
                    air[position / 8] |= ((value >> (2 - bit)) & 1) << (7 - position % 8);
                }
            }
            require(air == encoder.encode(berPayload, 1, counter ? 1 : 0, counter), "BER payload/status not compatible");
        }
        bool rejected = false; try { prepareImage(std::vector<std::uint8_t>(200001), "large"); }
        catch (const std::invalid_argument&) { rejected = true; } require(rejected, "Oversize image accepted");
        std::cout << "64 original HS-Modem frame vectors match; image boundaries, repeats, counter rollover bits and JPEG reconstruction pass\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
