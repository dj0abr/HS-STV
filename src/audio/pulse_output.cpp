// SPDX-License-Identifier: GPL-3.0-or-later
#include "mm_sstv/audio_output.hpp"
#include "pulse_connection.hpp"
#include "mm_sstv/tx_modulator.hpp"
#include <pulse/pulseaudio.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <mutex>
#include <stdexcept>
#include <thread>

namespace mm_sstv {
namespace {
using Clock = std::chrono::steady_clock;
using Connection = detail::PulseConnection;

class PulseOutput final : public AudioOutput {
public:
    ~PulseOutput() override { stop(); }
    std::vector<AudioDevice> devices() override { Connection c; return c.devices(); }
    void start(const std::string& device, Render render, unsigned sampleRate) override {
        stop();
        { std::lock_guard<std::mutex> lock(errorMutex_); error_.clear(); }
        underruns_ = 0; finished_ = false; ending_ = false; stopping_ = false; render_ = std::move(render);
        try {
            connection_ = std::make_unique<Connection>();
            const auto selected = device == systemDefaultAudioDevice ? connection_->defaultDevice(false) : device;
            const auto available = connection_->devices();
            if (std::none_of(available.begin(), available.end(), [&](const auto& d) { return d.id == selected; }))
                throw std::runtime_error("Audioausgang nicht verfügbar: " + selected);
            const pa_sample_spec format{PA_SAMPLE_FLOAT32NE, sampleRate, 1};
            const std::string streamName = "hs-stv Bildsendung";
            stream_ = pa_stream_new(connection_->context, streamName.c_str(), &format, nullptr);
            if (!stream_) connection_->fail();
            pa_stream_set_write_callback(stream_, write, this);
            pa_stream_set_underflow_callback(stream_, [](pa_stream*, void* self) {
                auto& output = *static_cast<PulseOutput*>(self);
                if (!output.ending_) ++output.underruns_;
            }, this);
            // Bounded 50-ms stream buffer, requested target 40 ms. The server
            // may negotiate hardware latency separately; never drain on STOP.
            const pa_buffer_attr buffer{sampleRate / 5, sampleRate * 4 / 25, sampleRate * 2 / 25, sampleRate / 25, UINT32_MAX};
            pa_cvolume volume; pa_cvolume_set(&volume, 1, PA_VOLUME_NORM);
            const auto flags = static_cast<pa_stream_flags_t>(PA_STREAM_ADJUST_LATENCY |
                PA_STREAM_DONT_MOVE | PA_STREAM_START_CORKED);
            if (pa_stream_connect_playback(stream_, selected.c_str(), &buffer, flags, &volume, nullptr) < 0)
                connection_->fail();
            connection_->wait([&] {
                if (!error().empty()) throw std::runtime_error(error());
                const auto state = pa_stream_get_state(stream_);
                if (!PA_STREAM_IS_GOOD(state)) connection_->fail();
                return state == PA_STREAM_READY;
            });
            // No fallback/migration to another sink, even if the device vanishes.
            const char* actual = pa_stream_get_device_name(stream_);
            if (!actual || selected != actual) throw std::runtime_error("Unerwarteter Audioausgang");
            auto* op = pa_stream_cork(stream_, 0, nullptr, nullptr);
            if (!op) connection_->fail();
            pa_operation_unref(op);
            worker_ = std::thread([this] { run(); });
        } catch (...) { stop(); throw; }
    }
    void stop() noexcept override {
        stopping_ = true;
        if (connection_) pa_mainloop_wakeup(connection_->loop);
        if (worker_.joinable()) worker_.join();
        if (stream_) {
            pa_stream_set_write_callback(stream_, nullptr, nullptr);
            pa_stream_set_underflow_callback(stream_, nullptr, nullptr);
            if (pa_stream_get_state(stream_) == PA_STREAM_READY) {
                discard(pa_stream_cork(stream_, 1, nullptr, nullptr));
                discard(pa_stream_flush(stream_, nullptr, nullptr));
            }
            pa_stream_disconnect(stream_); pa_stream_unref(stream_); stream_ = nullptr;
        }
        connection_.reset(); render_ = {};
    }
    bool finished() const override { return finished_.load(); }
    std::string error() const override { std::lock_guard<std::mutex> lock(errorMutex_); return error_; }
    std::uint64_t underruns() const override { return underruns_.load(); }
private:
    void discard(pa_operation* operation) noexcept {
        if (!operation) return;
        try { connection_->wait([&] { return pa_operation_get_state(operation) != PA_OPERATION_RUNNING; }, 100); }
        catch (...) { pa_operation_cancel(operation); }
        pa_operation_unref(operation);
    }
    void fail(std::string message) noexcept {
        { std::lock_guard<std::mutex> lock(errorMutex_); if (error_.empty()) error_ = std::move(message); }
        stopping_ = true;
    }
    void run() noexcept {
        while (!stopping_) {
            if (pa_context_get_state(connection_->context) != PA_CONTEXT_READY ||
                pa_stream_get_state(stream_) != PA_STREAM_READY) {
                fail("Audioverbindung unterbrochen. Ausgang prüfen und Sendung erneut starten."); break;
            }
            if (pa_mainloop_iterate(connection_->loop, 1, nullptr) < 0) {
                fail("Audioserver wurde beendet."); break;
            }
        }
    }
    static void write(pa_stream* stream, std::size_t bytes, void* self) noexcept {
        auto& output = *static_cast<PulseOutput*>(self);
        std::array<float, 480> buffer{};
        try {
            if (bytes % sizeof(float)) throw std::runtime_error("Ungültige Audioblockgröße");
            while (bytes && !output.stopping_ && !output.ending_) {
                const auto frames = std::min(bytes / sizeof(float), buffer.size());
                const auto rendered = output.render_(buffer.data(), frames);
                if (rendered > frames) throw std::runtime_error("Invalid rendered frame count");
                const auto count = rendered * sizeof(float);
                if (count && pa_stream_write(stream, buffer.data(), count, nullptr, 0, PA_SEEK_RELATIVE) < 0)
                    throw std::runtime_error("Audiodaten konnten nicht ausgegeben werden");
                bytes -= count;
                if (rendered < frames) {
                    output.ending_ = true;
                    auto* drain = pa_stream_drain(stream, [](pa_stream*, int success, void* self) {
                        auto& d = *static_cast<PulseOutput*>(self);
                        if (success) { d.finished_ = true; d.stopping_ = true; }
                        else d.fail("Audioausgabe konnte nicht vollständig abgespielt werden");
                    }, &output);
                    if (!drain) throw std::runtime_error("Cannot drain audio stream");
                    pa_operation_unref(drain);
                }
            }
        } catch (const std::exception& e) { output.fail(e.what()); }
        catch (...) { output.fail("Unbekannter Audiofehler"); }
    }
    std::unique_ptr<Connection> connection_;
    pa_stream* stream_ = nullptr;
    Render render_;
    std::thread worker_;
    std::atomic<bool> stopping_{true};
    std::atomic<bool> finished_{false};
    bool ending_ = false; // audio thread only, reset before it starts
    std::atomic<std::uint64_t> underruns_{0};
    mutable std::mutex errorMutex_;
    std::string error_;
};
}
std::unique_ptr<AudioOutput> makeAudioOutput() { return std::make_unique<PulseOutput>(); }
}
