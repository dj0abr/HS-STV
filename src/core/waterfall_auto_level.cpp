// SPDX-License-Identifier: GPL-3.0-or-later
#include "mm_sstv/waterfall_auto_level.hpp"
#include <algorithm>
#include <cmath>

namespace mm_sstv {
namespace {
double percentile(std::vector<float>& values, double fraction) {
    const auto index = std::size_t(fraction * (values.size() - 1));
    std::nth_element(values.begin(), values.begin() + index, values.end());
    return values[index];
}
bool usable(float value) { return std::isfinite(value) && value > -150; }
}
void WaterfallAutoLevel::reset(double minimum, double maximum) {
    history_.clear(); minimum_ = minimum; maximum_ = maximum;
    binHz_ = bandwidthDb_ = 0; pending_ = 0; initialized_ = signalSeen_ = false;
}
int WaterfallAutoLevel::minimumDb() const {
    // Lower only the displayed black threshold by nine slider steps. Keep this
    // visibility bias out of the estimator so Max and its hold do not move.
    const double displayed = initialized_ ? std::clamp(minimum_ - 9, -140., -20.) : minimum_;
    return int(std::lround(displayed));
}
int WaterfallAutoLevel::maximumDb() const { return int(std::lround(maximum_)); }
bool WaterfallAutoLevel::update(const std::vector<std::vector<float>>& rows, double binHz,
                              double bandwidthDb, double offsetHz, std::size_t rowsPerPixel) {
    if (rows.empty() || !std::isfinite(binHz) || binHz <= 0 ||
        !std::isfinite(bandwidthDb) || !std::isfinite(offsetHz)) return false;
    if (binHz != binHz_ || bandwidthDb != bandwidthDb_ ||
        (!history_.empty() && rows.front().size() != history_.back().size())) {
        reset(minimum_, maximum_); binHz_ = binHz; bandwidthDb_ = bandwidthDb;
    }
    for (const auto& row : rows) {
        if (row.empty()) continue;
        if (!history_.empty() && row.size() != history_.back().size()) {
            reset(minimum_, maximum_); binHz_ = binHz; bandwidthDb_ = bandwidthDb;
        }
        history_.push_back(row); ++pending_;
        while (history_.size() > 200) history_.pop_front();
    }
    // Use audio time, so batched/delayed UI updates do not change the response.
    if (history_.size() < 50 || pending_ < 10) return false;
    const double elapsed = pending_ * .01;
    const auto newRows = std::min(pending_, history_.size()); pending_ = 0;
    const auto bins = history_.back().size();
    const auto first = std::min(bins, std::size_t(std::max(0., 700. + offsetHz) / binHz));
    const auto last = std::min(bins, std::size_t(std::max(0., 2700. + offsetHz) / binHz) + 1);
    if (last <= first || last - first < 16) return false;
    const double signalLow = 1000 + offsetHz, signalHigh = 2500 + offsetHz;
    std::vector<float> noisePixels, noiseRows, signalBlocks;
    std::vector<float> rowNoise(history_.size(), -160);
    std::vector<float> rowPeaks(history_.size(), -160);
    for (std::size_t at = 0; at < history_.size(); ++at) {
        std::vector<float> background;
        const auto& row = history_[at];
        for (auto bin = first; bin < last; ++bin) {
            const float value = float(row[bin] - bandwidthDb);
            if (!usable(value)) continue;
            background.push_back(value);
            if (bin * binHz >= signalLow && bin * binHz <= signalHigh)
                rowPeaks[at] = std::max(rowPeaks[at], value);
        }
        if (background.size() < 16) continue; // Includes digital silence.
        rowNoise[at] = float(percentile(background, .5));
        noiseRows.push_back(rowNoise[at]);
    }
    if (noiseRows.size() < 30) return false;
    const double noise = percentile(noiseRows, .5);
    bool recentSignal = false;
    for (auto at = history_.size() - newRows; at < history_.size(); ++at)
        if (usable(rowNoise[at]) && rowNoise[at] <= noise + 12 && rowPeaks[at] > rowNoise[at] + 14)
            recentSignal = true;
    // A peak per 100 ms preserves brief, recurring sync. The 90th percentile
    // over two seconds rejects isolated clicks and transient strong carriers.
    for (std::size_t start = 0; start < history_.size(); start += 10) {
        float peak = -160;
        for (auto at = start; at < std::min(start + 10, history_.size()); ++at)
            if (usable(rowNoise[at]) && rowNoise[at] <= noise + 12)
                peak = std::max(peak, rowPeaks[at]);
        if (usable(peak)) signalBlocks.push_back(peak);
    }
    rowsPerPixel = std::clamp<std::size_t>(rowsPerPixel, 1, 60);
    // Measure the same temporal maxima used to draw a pixel. Mask bins touched
    // by strong tones anywhere in the group, using each row's robust floor.
    // This keeps sync, swept image tones and narrow interferers out of Min.
    for (std::size_t start = 0; start + rowsPerPixel <= history_.size(); start += rowsPerPixel) {
        for (auto bin = first; bin < last; ++bin) {
            float peak = -160; bool tone = false;
            for (auto at = start; at < start + rowsPerPixel; ++at) {
                const float value = float(history_[at][bin] - bandwidthDb);
                if (!usable(value) || !usable(rowNoise[at]) || rowNoise[at] > noise + 12) continue;
                tone = tone || value > rowNoise[at] + 12;
                peak = std::max(peak, value);
            }
            if (!tone && usable(peak)) noisePixels.push_back(peak);
        }
    }
    if (noisePixels.size() < 16) return false;
    const double floor = std::clamp(percentile(noisePixels, .90) + 1, -140., -20.);
    const auto follow = [elapsed](double current, double target, double seconds) {
        return current + (target - current) * (1 - std::exp(-elapsed / seconds));
    };
    minimum_ = initialized_ ? follow(minimum_, floor, 3.) : floor;
    const double signal = signalBlocks.empty() ? -160 : percentile(signalBlocks, .90);
    if (recentSignal && signal > noise + 14) {
        const double ceiling = std::clamp(signal + 6, -120., 0.);
        maximum_ = signalSeen_ ? follow(maximum_, ceiling, ceiling > maximum_ ? .35 : 8.) : ceiling;
        signalSeen_ = true;
    }
    // Before detecting a signal, a conservative span also exposes weak tones.
    // The independent black threshold prevents noise filling the color scale.
    if (!signalSeen_) maximum_ = std::clamp(minimum_ + 36, -120., 0.);
    // No signal: retain Max instead of stretching ordinary noise to full color.
    maximum_ = std::clamp(std::max(maximum_, minimum_ + 24), -120., 0.);
    minimum_ = std::clamp(std::min(minimum_, maximum_ - 20), -140., -20.);
    initialized_ = true;
    return true;
}
}
