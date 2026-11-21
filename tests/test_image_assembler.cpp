// SPDX-License-Identifier: GPL-3.0-or-later
#include "mm_sstv/image_assembler.hpp"
#include <algorithm>
#include <iostream>
#include <stdexcept>
using namespace mm_sstv;
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
std::vector<ReceivedFrame> frames(const std::vector<std::uint8_t>& jpeg, const std::string& name) {
    std::vector<std::uint8_t> data(55 + jpeg.size());
    std::copy(name.begin(), name.end(), data.begin()); const auto id = crc16(jpeg.data(), jpeg.size());
    data[50] = id >> 8; data[51] = id; data[52] = jpeg.size() >> 16; data[53] = jpeg.size() >> 8; data[54] = jpeg.size();
    std::copy(jpeg.begin(), jpeg.end(), data.begin() + 55);
    std::vector<ReceivedFrame> result((data.size() + 218) / 219);
    for (std::size_t n = 0; n < result.size(); ++n) {
        auto& f = result[n]; f.type = 2; f.counter = n;
        f.status = result.size() == 1 ? 3 : n == 0 ? 0 : n + 1 == result.size() ? 2 : 1;
        std::copy_n(data.begin() + n * 219, std::min<std::size_t>(219, data.size() - n * 219), f.payload.begin());
    }
    return result;
}
int main() {
    try {
        for (const std::size_t size : {100u, 164u, 165u, 383u, 56010u, 200000u}) {
            std::vector<std::uint8_t> jpeg(size); for (std::size_t n = 0; n < size; ++n) jpeg[n] = (n * 19 + size) & 255;
            auto input = frames(jpeg, "test.jpg"); ImageAssembler assembler;
            require(!assembler.accept(ReceivedFrame{}), "Accepted non-image data");
            auto image = assembler.accept(input[0]); require(image && image->newImage, "Header not recognized");
            require(!assembler.accept(input[0]), "Duplicate header triggers needless reconstruction");
            if (input.size() > 3) {
                for (std::size_t n = 1; n < input.size(); ++n) if (n != 2) image = assembler.accept(input[n]);
                require(image && !image->complete && image->blocks == input.size() - 1, "Missing block was not retained");
                require(image->jpeg.size() == 2 * 219 - 55, "Preview contains a padded hole");
                image = assembler.accept(input[0]);
                require(image && !image->complete && image->blocks == input.size() - 1, "Retransmission lost cached partial blocks");
                image = assembler.accept(input[2]);
                require(image && image->blocks == input.size() && !image->complete, "Merged image completed before end-of-transmission validation");
                image = assembler.accept(input.back());
            } else for (std::size_t n = 1; n < input.size(); ++n) image = assembler.accept(input[n]);
            require(image && image->complete && image->jpeg == jpeg && image->matchesId, "Image reassembly failed");
            if (input.size() > 1) {
                auto stray = input.back(); stray.payload[0] ^= 1;
                require(!assembler.accept(stray), "Headerless data corrupted a finished image");
            }
            const auto other = frames(std::vector<std::uint8_t>(200, 42), "different.jpg");
            assembler.accept(other[0]); image = assembler.accept(input[0]);
            require(image && image->newImage, "Returning image not recognized");
            require(image->complete == (input.size() == 1), "Completed cache certified a new transmission");
            if (input.size() > 1) require(image->blocks == 1, "Completed cache retained stale blocks");
        }
        // A reused legacy ID, including changes outside the identical header.
        for (const bool changedHeader : {false, true}) {
            std::vector<std::uint8_t> before(1000, 17), after = before;
            std::fill(after.begin() + (changedHeader ? 0 : 164), after.end(), 33);
            auto first = frames(before, "same.jpg"), second = frames(after, "same.jpg");
            for (auto* collection : {&first, &second}) for (auto& frame : *collection) {
                if (frame.status == 0) { frame.payload[50] = 0; frame.payload[51] = 7; }
            }
            ImageAssembler assembler; std::optional<ReceivedImage> image;
            for (const auto& frame : first) image = assembler.accept(frame);
            require(image && image->complete, "Initial reused-ID image failed");
            for (std::size_t n = 0; n < second.size(); ++n) {
                image = assembler.accept(second[n]); require(bool(image), "New image block rejected");
                require(image->complete == (n + 1 == second.size()), "Stale blocks caused early completion");
            }
            require(image->jpeg == after, "New image contains old bytes");
        }
        // If an incomplete cache conflicts, retain only this round's evidence.
        {
            auto first = frames(std::vector<std::uint8_t>(1000, 17), "conflict.jpg");
            auto changed = first; for (auto& frame : changed) if (frame.status != 0) frame.payload.fill(33);
            ImageAssembler assembler;
            assembler.accept(first[0]); assembler.accept(first[1]); assembler.accept(first.back());
            assembler.accept(changed[0]); auto image = assembler.accept(changed[1]);
            require(image && image->blocks == 2 && !image->complete, "Conflicting cache retained unconfirmed old blocks");
            image = assembler.accept(changed.back()); require(image && !image->complete, "Mixed partial cache became complete");
        }
        // Legacy arbitrary IDs still support reconstruction from two faulty sends.
        {
            std::vector<std::uint8_t> bytes(1000, 71); auto input = frames(bytes, "legacy-partial.jpg");
            input[0].payload[50] = 0; input[0].payload[51] = 7;
            ImageAssembler assembler; std::optional<ReceivedImage> image;
            for (std::size_t n = 0; n < input.size(); ++n) if (n != 2) image = assembler.accept(input[n]);
            require(image && !image->complete, "Faulty first send unexpectedly complete");
            assembler.accept(input[0]); image = assembler.accept(input[2]);
            require(image && image->blocks == input.size() && !image->complete, "Old valid blocks did not count");
            image = assembler.accept(input.back());
            require(image && image->complete && image->jpeg == bytes && !image->matchesId, "Legacy block repair failed");
        }
        // Find a real CRC16 collision; an equal CRC must not reuse a finished file.
        {
            std::vector<std::uint8_t> before(500, 19), after = before; after[200] ^= 1;
            const auto id = crc16(before.data(), before.size()); bool collision = false;
            for (unsigned suffix = 0; suffix <= 65535; ++suffix) {
                after[498] = suffix >> 8; after[499] = suffix;
                if (crc16(after.data(), after.size()) == id) { collision = true; break; }
            }
            require(collision && after != before, "CRC collision fixture failed");
            auto first = frames(before, "collision.jpg"), second = frames(after, "collision.jpg");
            ImageAssembler assembler; std::optional<ReceivedImage> image;
            for (const auto& frame : first) image = assembler.accept(frame);
            for (std::size_t n = 0; n < second.size(); ++n) {
                image = assembler.accept(second[n]); require(bool(image), "CRC collision dropped a new block");
                require(image->complete == (n + 1 == second.size()), "CRC collision reused old validity");
            }
            require(image->jpeg == after && image->matchesId, "CRC collision image corrupted");
        }
        auto interrupted = frames(std::vector<std::uint8_t>(300, 17), "paused.jpg"); ImageAssembler gated;
        gated.accept(interrupted[0]); gated.resetStream();
        require(!gated.accept(interrupted[1]), "Stream restart accepted data before its header");
        gated.accept(interrupted[0]);
        require(gated.accept(interrupted[1])->complete, "Cached blocks lost across stream reset");
        auto single = frames(std::vector<std::uint8_t>(100, 3), "old.jpg"); single[0].counter = 73;
        single[0].payload[50] = 0; single[0].payload[51] = 0;
        ImageAssembler legacy; auto image = legacy.accept(single[0]);
        require(image && image->complete && !image->matchesId, "Legacy single frame or arbitrary file ID rejected");
        auto broken = single[0]; broken.payload[52] = 255;
        require(!legacy.accept(broken), "Oversize header accepted");
        broken = single[0]; broken.counter = 1024; require(!legacy.accept(broken), "Invalid counter accepted");
        std::cout << "Partial preview, repeated-send repair, reused IDs, CRC collision, conflict reset, cache switching and legacy IDs pass\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
