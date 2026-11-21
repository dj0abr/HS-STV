// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstddef>
#include <deque>
#include <vector>

namespace mm_sstv {
// Display contrast only. Input rows are 10-ms FFT levels, before bandwidth correction.
class WaterfallAutoLevel final {
public:
    void reset(double minimum = -90, double maximum = -20);
    bool update(const std::vector<std::vector<float>>& rows, double binHz,
                double bandwidthDb, double offsetHz, std::size_t rowsPerPixel);
    int minimumDb() const;
    int maximumDb() const;
private:
    std::deque<std::vector<float>> history_;
    double minimum_ = -90, maximum_ = -20;
    double binHz_ = 0, bandwidthDb_ = 0;
    std::size_t pending_ = 0;
    bool initialized_ = false, signalSeen_ = false;
};
}
