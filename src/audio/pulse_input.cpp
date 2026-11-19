// SPDX-License-Identifier: GPL-3.0-or-later
#include "mm_sstv/audio_input.hpp"
#include "pulse_connection.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <mutex>
namespace mm_sstv {
namespace {
class PulseInput final : public AudioInput {
public:
    ~PulseInput() override { stop(); }
    std::vector<AudioDevice> devices() override { detail::PulseConnection c; return c.devices(true); }
    void start(const std::string& device, Consume consume, unsigned sampleRate) override {
        stop(); { std::lock_guard<std::mutex> lock(mutex_); error_.clear(); }
        stopping_ = false; consume_ = std::move(consume);
        try {
            connection_ = std::make_unique<detail::PulseConnection>();
            const auto devices = connection_->devices(true);
            bool found = false; for (const auto& d : devices) found |= d.id == device;
            if (!found) throw std::runtime_error("Audioeingang nicht verfügbar: " + device);
            const pa_sample_spec format{PA_SAMPLE_FLOAT32NE, sampleRate, 1};
            stream_ = pa_stream_new(connection_->context, "mm-sstv Empfang", &format, nullptr);
            if (!stream_) connection_->fail();
            pa_stream_set_read_callback(stream_, read, this);
            const pa_buffer_attr buffer{sampleRate * 4 / 5, UINT32_MAX, UINT32_MAX, UINT32_MAX, sampleRate * 4 / 100};
            if (pa_stream_connect_record(stream_, device.c_str(), &buffer,
                    static_cast<pa_stream_flags_t>(PA_STREAM_ADJUST_LATENCY | PA_STREAM_DONT_MOVE)) < 0) connection_->fail();
            connection_->wait([&] {
                const auto state = pa_stream_get_state(stream_);
                if (!PA_STREAM_IS_GOOD(state)) connection_->fail();
                return state == PA_STREAM_READY;
            });
            if (!pa_stream_get_device_name(stream_) || device != pa_stream_get_device_name(stream_))
                throw std::runtime_error("Unerwarteter Audioeingang");
            if (!error().empty()) throw std::runtime_error(error());
            worker_ = std::thread([this] {
                while (!stopping_) {
                    if (pa_context_get_state(connection_->context) != PA_CONTEXT_READY || pa_stream_get_state(stream_) != PA_STREAM_READY) {
                        fail("Audioeingang wurde getrennt. Empfang bitte neu starten."); break;
                    }
                    if (pa_mainloop_iterate(connection_->loop, 1, nullptr) < 0) { fail("Audioserver wurde beendet."); break; }
                }
            });
        } catch (...) { stop(); throw; }
    }
    void stop() noexcept override {
        stopping_ = true;
        if (connection_) pa_mainloop_wakeup(connection_->loop);
        if (worker_.joinable()) worker_.join();
        if (stream_) {
            pa_stream_set_read_callback(stream_, nullptr, nullptr);
            pa_stream_disconnect(stream_); pa_stream_unref(stream_); stream_ = nullptr;
        }
        connection_.reset(); consume_ = {};
    }
    std::string error() const override { std::lock_guard<std::mutex> lock(mutex_); return error_; }
private:
    void fail(std::string text) { std::lock_guard<std::mutex> lock(mutex_); error_ = std::move(text); stopping_ = true; }
    static void read(pa_stream* stream, std::size_t, void* data) noexcept {
        auto& self = *static_cast<PulseInput*>(data);
        try {
            while (!self.stopping_) {
                const void* bytes = nullptr; std::size_t size = 0;
                if (pa_stream_peek(stream, &bytes, &size) < 0) throw std::runtime_error("Audioaufnahme fehlgeschlagen");
                if (!size) break;
                if (size % sizeof(float)) throw std::runtime_error("Ungültiger Audioeingangsblock");
                if (bytes) self.consume_(static_cast<const float*>(bytes), size / sizeof(float));
                else {
                    std::array<float, 480> silence{};
                    for (std::size_t remaining = size / sizeof(float); remaining;) {
                        const auto n = std::min(remaining, silence.size()); self.consume_(silence.data(), n); remaining -= n;
                    }
                }
                if (pa_stream_drop(stream) < 0) throw std::runtime_error("Audioaufnahme unterbrochen");
            }
        } catch (const std::exception& e) { self.fail(e.what()); }
        catch (...) { self.fail("Unbekannter Aufnahmefehler"); }
    }
    std::unique_ptr<detail::PulseConnection> connection_;
    pa_stream* stream_ = nullptr;
    Consume consume_;
    std::thread worker_;
    std::atomic<bool> stopping_{true};
    mutable std::mutex mutex_;
    std::string error_;
};
}
std::unique_ptr<AudioInput> makeAudioInput() { return std::make_unique<PulseInput>(); }
}
