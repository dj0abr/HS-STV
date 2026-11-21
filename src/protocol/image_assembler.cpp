// SPDX-License-Identifier: GPL-3.0-or-later
// Block buffering and incremental JPEG preview adapted from DJ0ABR's
// receivefile.cs / ArraySend.cs. Only incomplete transfers may contribute cached blocks.
#include "mm_sstv/image_assembler.hpp"
#include <algorithm>
namespace mm_sstv {
std::optional<ReceivedImage> ImageAssembler::accept(const ReceivedFrame& frame) {
    if (frame.type != 2 || frame.counter > 1023 || frame.status > 3) return std::nullopt;
    const bool header = frame.status == 0 || frame.status == 3;
    bool newTransmission = false;
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
        if (!partials_.count(key)) {
            if (partials_.size() == 8) {
                auto oldest = std::min_element(partials_.begin(), partials_.end(), [](const auto& a, const auto& b) { return a.second.used < b.second.used; });
                partials_.erase(oldest);
            }
            Partial state; state.filename = name; state.id = id; state.size = size;
            state.blocks.resize(total); state.valid.resize(total); state.fresh.resize(total);
            partials_.emplace(key, std::move(state));
        }
        auto& state = partials_.at(key);
        // Single-frame repeats already contain the entire file; byte-identical
        // copies need no new display event (save retries are independent).
        if (frame.status == 3 && key == active_ && state.fresh[0] && state.blocks[0] == p) return std::nullopt;
        // The initial burst repeats the header. A header after data, EOF, or a
        // stream/file switch starts another transmission, even with the same ID.
        newTransmission = key != active_ || !state.receiving || state.dataSeen ||
            (state.valid[0] && state.blocks[0] != p);
        if (newTransmission) {
            if (state.completed) std::fill(state.valid.begin(), state.valid.end(), false);
            std::fill(state.fresh.begin(), state.fresh.end(), false);
            state.dataSeen = false; state.completed = false;
        }
        active_ = key; state.base = frame.counter; state.receiving = true;
    }
    if (active_.empty()) return std::nullopt;
    auto& state = partials_.at(active_);
    if (!header && !state.receiving) return std::nullopt;
    const auto index = header ? 0 : (frame.counter + 1024 - state.base) % 1024;
    if (index >= state.blocks.size() || (!header && index == 0)) return std::nullopt;
    if (!header) state.dataSeen = true;
    if (!newTransmission && state.fresh[index] && state.blocks[index] == frame.payload && frame.status != 2 && frame.status != 3)
        return std::nullopt;
    if (state.valid[index] && state.blocks[index] != frame.payload) {
        // Conflicting bytes prove that this is not the cached transmission.
        // Keep already confirmed current-round blocks, discard older evidence.
        for (std::size_t n = 0; n < state.valid.size(); ++n) if (!state.fresh[n]) state.valid[n] = false;
        newTransmission = true;
    }
    state.used = ++clock_; state.blocks[index] = frame.payload;
    state.valid[index] = true; state.fresh[index] = true;
    ReceivedImage image; image.filename = state.filename; image.id = state.id; image.size = state.size;
    image.totalBlocks = state.blocks.size(); image.blocks = std::count(state.valid.begin(), state.valid.end(), true);
    const auto fresh = std::size_t(std::count(state.fresh.begin(), state.fresh.end(), true));
    image.newImage = newTransmission;
    // A merged candidate stays provisional until the new transmission ends:
    // later blocks may still contradict cached bytes, even with equal CRC IDs.
    image.complete = image.blocks == image.totalBlocks &&
        (fresh == image.totalBlocks || frame.status == 2 || frame.status == 3);
    for (std::size_t n = 0; n < state.blocks.size() && state.valid[n]; ++n) {
        const auto& bytes = state.blocks[n];
        image.jpeg.insert(image.jpeg.end(), bytes.begin() + (n == 0 ? 55 : 0), bytes.end());
    }
    if (image.jpeg.size() > image.size) image.jpeg.resize(image.size);
    // IDs can be arbitrary or collide; neither equal IDs nor a CRC match may
    // turn a fully cached old file into a completed new transmission.
    if (image.complete) state.completed = true;
    if (image.complete || frame.status == 2 || frame.status == 3) state.receiving = false;
    image.matchesId = image.complete && crc16(image.jpeg.data(), image.jpeg.size()) == image.id;
    return image;
}
}
