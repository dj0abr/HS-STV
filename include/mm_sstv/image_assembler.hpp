// SPDX-License-Identifier: GPL-3.0-or-later
// C++ adaptation of HS-Modem oscardata receivefile.cs / ArraySend.cs, DJ0ABR.
#pragma once
#include "protocol.hpp"
#include <map>
#include <optional>
namespace mm_sstv {
struct ReceivedFrame { unsigned type = 0, counter = 0, status = 0; Payload payload{}; };
struct ReceivedImage {
    std::string filename;
    unsigned id = 0;
    std::size_t size = 0, blocks = 0, totalBlocks = 0;
    std::vector<std::uint8_t> jpeg; // Contiguous bytes only, no padded holes.
    bool complete = false, newImage = false, matchesId = false;
};
class ImageAssembler {
public:
    std::optional<ReceivedImage> accept(const ReceivedFrame& frame);
    void resetStream() { active_.clear(); } // Keep cached blocks; require a new header.
private:
    struct Partial {
        std::string filename;
        unsigned id = 0, base = 0;
        std::size_t size = 0;
        std::vector<Payload> blocks;
        std::vector<bool> valid;
        std::uint64_t used = 0;
        bool receiving = false;
    };
    std::map<std::string, Partial> partials_;
    std::string active_;
    std::uint64_t clock_ = 0;
};
}
