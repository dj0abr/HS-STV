// SPDX-License-Identifier: GPL-3.0-or-later
#include "hsmodem.h"
#include "mm_sstv/protocol.hpp"
#include "fec/schifra_galois_field.hpp"
#include "fec/schifra_galois_field_polynomial.hpp"
#include "fec/schifra_sequential_root_generator_polynomial_creator.hpp"
#include "fec/schifra_reed_solomon_encoder.hpp"
#include "fec/schifra_reed_solomon_block.hpp"
#include <algorithm>
#include <iostream>
#include <stdexcept>
int bitsPerSymbol = 3, constellationSize = 8, speed = 6000;
uint8_t* getPayload(uint8_t*);
void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
int main() {
    try {
        mm_sstv::FrameEncoder encoder; mm_sstv::Payload payload{};
        for (unsigned i = 0; i < payload.size(); ++i) payload[i] = (i * 37 + 11) & 255;
        const auto clean = encoder.encode(payload, 2, 1, 0);
        auto air = clean;
        require(getPayload(air.data()), "Clean frame was rejected");
        require(rx_fec_corrected_frames == 0 && rx_fec_corrected_bytes == 0, "Clean frame counted as repaired");
        unsigned expectedBytes = 0, expectedFrames = 0;
        for (unsigned errors : {1u, 3u, 16u}) {
            air = clean;
            for (unsigned i = 0; i < errors; ++i) air[3 + i * 5] ^= 0x5a;
            const auto decoded = getPayload(air.data());
            require(decoded && std::equal(payload.begin(), payload.end(), decoded + 10), "Recoverable frame did not decode exactly");
            expectedBytes += errors; ++expectedFrames;
            require(rx_fec_corrected_frames == expectedFrames && rx_fec_corrected_bytes == expectedBytes, "Incorrect FEC repair statistics");
        }
        air = clean;
        for (unsigned i = 0; i < 32; ++i) air[3 + i * 5] ^= 0x5a;
        require(!getPayload(air.data()) && rx_fec_failures == 1, "Uncorrectable RS frame not counted");
        require(rx_fec_corrected_frames == expectedFrames && rx_fec_corrected_bytes == expectedBytes, "Rejected frame counted as repaired");
        // Construct valid RS parity around a deliberately invalid CRC, then add
        // three correctable RS errors. This must count only as a CRC failure.
        const auto raw = RX_Scramble(const_cast<uint8_t*>(clean.data()) + 3, 255);
        schifra::reed_solomon::block<255, 32> block;
        for (unsigned i = 0; i < 223; ++i) block[i] = raw[i];
        block[222] ^= 1;
        const schifra::galois::field field(8, schifra::galois::primitive_polynomial_size06, schifra::galois::primitive_polynomial06);
        schifra::galois::field_polynomial generator(field);
        schifra::make_sequential_root_generator_polynomial(field, 120, 32, generator);
        schifra::reed_solomon::encoder<255, 32, 223> rs(field, generator);
        require(rs.encode(block), "Cannot encode CRC rejection fixture");
        for (unsigned i = 0; i < 255; ++i) air[3 + i] = block[i];
        const auto scrambled = RX_Scramble(air.data() + 3, 255); std::copy_n(scrambled, 255, air.data() + 3);
        for (unsigned i = 0; i < 3; ++i) air[3 + i * 5] ^= 0x5a;
        require(!getPayload(air.data()) && rx_crc_failures == 1, "Invalid CRC not counted");
        require(rx_fec_corrected_frames == expectedFrames && rx_fec_corrected_bytes == expectedBytes, "CRC-rejected RS repairs counted as valid");
        air = clean; require(getPayload(air.data()), "Clean frame failed after errors");
        require(rx_last_fec_corrected == 0 && rx_fec_corrected_frames == expectedFrames, "Previous RS correction leaked into next frame");
        std::cout << "CRC/FEC: clean, 1/3/16 corrected bytes, uncorrectable block, CRC rejection after repair, counter lifetime passed\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
