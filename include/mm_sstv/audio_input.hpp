// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "audio_output.hpp"
namespace mm_sstv {
class AudioInput {
public:
    using Consume = std::function<void(const float*, std::size_t)>;
    virtual ~AudioInput() = default;
    virtual std::vector<AudioDevice> devices() = 0;
    virtual void start(const std::string& device, Consume consume, unsigned sampleRate = 48000) = 0;
    virtual void stop() noexcept = 0;
    virtual std::string error() const = 0;
};
std::unique_ptr<AudioInput> makeAudioInput();
}
