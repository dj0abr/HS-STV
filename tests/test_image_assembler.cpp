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
                require(!assembler.accept(input[0]), "Repeated first frame cleared cached blocks");
                image = assembler.accept(input[2]);
            } else for (std::size_t n = 1; n < input.size(); ++n) image = assembler.accept(input[n]);
            require(image && image->complete && image->jpeg == jpeg && image->matchesId, "Image reassembly failed");
            if (input.size() > 1) {
                auto stray = input.back(); stray.payload[0] ^= 1;
                require(!assembler.accept(stray), "Headerless data corrupted a finished image");
            }
            const auto other = frames(std::vector<std::uint8_t>(200, 42), "different.jpg");
            assembler.accept(other[0]); image = assembler.accept(input[0]);
            require(image && image->newImage && image->complete, "Cached image lost after another transmission");
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
        std::cout << "Partial JPEG prefix, duplicate headers, missing-block repair, cache switching and legacy ID/counter pass\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
