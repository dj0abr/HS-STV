// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace mm_sstv {
// Resolve the OS default when opening a stream; keep that device for its lifetime.
inline constexpr const char* systemDefaultAudioDevice = "@system-default";
struct AudioDevice { std::string id; std::string name; };
class AudioOutput {
public:
    using Render = std::function<std::size_t(float*, std::size_t)>;
    virtual ~AudioOutput() = default;
    virtual std::vector<AudioDevice> devices() = 0;
    // Exact device ID or systemDefaultAudioDevice; no fallback for an explicit ID.
    // Render runs serially on the audio thread, mono float at the requested sample rate.
    // Return 0..capacity frames. A short block ends input and drains playback.
    virtual void start(const std::string& device, Render render, unsigned sampleRate = 48000) = 0;
    // Joins audio callbacks and discards queued samples. Safe if already stopped.
    virtual void stop() noexcept = 0;
    virtual bool finished() const { return false; } // True after final samples played.
    virtual std::string error() const = 0;
    virtual std::uint64_t underruns() const = 0;
};
std::unique_ptr<AudioOutput> makeAudioOutput();
}
