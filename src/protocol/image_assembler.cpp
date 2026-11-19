// SPDX-License-Identifier: GPL-3.0-or-later
// Block buffering, file-header removal, retransmission merging and incremental
// JPEG reconstruction adapted from DJ0ABR's receivefile.cs and ArraySend.cs.
#include "mm_sstv/image_assembler.hpp"
#include <algorithm>
namespace mm_sstv {
std::optional<ReceivedImage> ImageAssembler::accept(const ReceivedFrame& frame) {
    if (frame.type != 2 || frame.counter > 1023 || frame.status > 3) return std::nullopt;
    const bool header = frame.status == 0 || frame.status == 3;
    bool switched = false;
    if (header) {
        const auto& p = frame.payload;
        const std::size_t size = (std::size_t(p[52]) << 16) | (std::size_t(p[53]) << 8) | p[54];
        if (!size || size > 1024 * 219 - 55) { active_.clear(); return std::nullopt; }
        const auto total = (size + 55 + 218) / 219;
        if (frame.status == 3 && total != 1) { active_.clear(); return std::nullopt; }
        std::string name;
        for (unsigned n = 0; n < 50 && p[n]; ++n) name += static_cast<char>(p[n]);
        name = jpegFileName(name);
        const unsigned id = (p[50] << 8) | p[51];
        const auto key = name + ":" + std::to_string(id) + ":" + std::to_string(size);
        switched = key != active_;
        if (!partials_.count(key)) {
            if (partials_.size() == 8) {
                auto oldest = std::min_element(partials_.begin(), partials_.end(), [](const auto& a, const auto& b) { return a.second.used < b.second.used; });
                partials_.erase(oldest);
            }
            Partial state; state.filename = name; state.id = id; state.size = size;
            state.blocks.resize(total); state.valid.resize(total); partials_.emplace(key, std::move(state));
        }
        active_ = key; partials_.at(key).base = frame.counter; partials_.at(key).receiving = true;
    }
    if (active_.empty()) return std::nullopt;
    auto& state = partials_.at(active_);
    if (!header && !state.receiving) return std::nullopt;
    const auto index = header ? 0 : (frame.counter + 1024 - state.base) % 1024;
    if (index >= state.blocks.size()) return std::nullopt;
    if (!switched && state.valid[index] && state.blocks[index] == frame.payload) {
        if (frame.status == 2 || frame.status == 3) state.receiving = false;
        return std::nullopt;
    }
    state.used = ++clock_; state.blocks[index] = frame.payload; state.valid[index] = true;
    ReceivedImage image; image.filename = state.filename; image.id = state.id; image.size = state.size;
    image.totalBlocks = state.blocks.size(); image.blocks = std::count(state.valid.begin(), state.valid.end(), true);
    image.newImage = switched; image.complete = image.blocks == image.totalBlocks;
    for (std::size_t n = 0; n < state.blocks.size() && state.valid[n]; ++n) {
        const auto& bytes = state.blocks[n];
        image.jpeg.insert(image.jpeg.end(), bytes.begin() + (n == 0 ? 55 : 0), bytes.end());
    }
    if (image.jpeg.size() > image.size) image.jpeg.resize(image.size);
    // ID is an identification field in the protocol; older senders may use
    // arbitrary IDs, so an ID/contents-CRC mismatch must not reject the image.
    if (image.complete || frame.status == 2 || frame.status == 3) state.receiving = false;
    image.matchesId = image.complete && crc16(image.jpeg.data(), image.jpeg.size()) == image.id;
    return image;
}
}
