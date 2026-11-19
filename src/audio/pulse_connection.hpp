// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "mm_sstv/audio_output.hpp"
#include <pulse/pulseaudio.h>
#include <chrono>
#include <stdexcept>
#include <thread>
namespace mm_sstv::detail {
using Clock = std::chrono::steady_clock;
struct PulseConnection {
    pa_mainloop* loop = nullptr;
    pa_context* context = nullptr;
    PulseConnection() {
        try {
            loop = pa_mainloop_new();
            if (!loop) throw std::runtime_error("Audio: cannot allocate event loop");
            context = pa_context_new(pa_mainloop_get_api(loop), "mm-sstv");
            if (!context) throw std::runtime_error("Audio: cannot allocate context");
            if (pa_context_connect(context, nullptr, PA_CONTEXT_NOAUTOSPAWN, nullptr) < 0) fail();
            wait([&] { return pa_context_get_state(context) == PA_CONTEXT_READY; });
        } catch (...) { close(); throw; }
    }
    PulseConnection(const PulseConnection&) = delete;
    PulseConnection& operator=(const PulseConnection&) = delete;
    ~PulseConnection() { close(); }
    void close() {
        if (context) { pa_context_disconnect(context); pa_context_unref(context); context = nullptr; }
        if (loop) { pa_mainloop_free(loop); loop = nullptr; }
    }
    [[noreturn]] void fail() const {
        throw std::runtime_error(std::string("Audioserver: ") + pa_strerror(pa_context_errno(context)));
    }
    template<class Predicate> void wait(Predicate ready, unsigned milliseconds = 2500) {
        const auto deadline = Clock::now() + std::chrono::milliseconds(milliseconds);
        while (!ready()) {
            const auto state = pa_context_get_state(context);
            if (!PA_CONTEXT_IS_GOOD(state)) fail();
            if (Clock::now() >= deadline) throw std::runtime_error("Audioserver antwortet nicht (Zeitüberschreitung)");
            if (pa_mainloop_iterate(loop, 0, nullptr) < 0) fail();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    std::vector<AudioDevice> devices(bool capture = false) {
        struct Query { std::vector<AudioDevice> result; int done = 0; } query;
        auto sink = [](pa_context*, const pa_sink_info* info, int end, void* data) {
            auto& q = *static_cast<Query*>(data);
            if (info && info->name) q.result.push_back({info->name, info->description ? info->description : info->name});
            if (end) q.done = end;
        };
        auto source = [](pa_context*, const pa_source_info* info, int end, void* data) {
            auto& q = *static_cast<Query*>(data);
            if (info && info->name) q.result.push_back({info->name, info->description ? info->description : info->name});
            if (end) q.done = end;
        };
        auto* operation = capture ? pa_context_get_source_info_list(context, source, &query)
                                  : pa_context_get_sink_info_list(context, sink, &query);
        if (!operation) fail();
        try { wait([&] { return query.done != 0; }); }
        catch (...) { pa_operation_cancel(operation); pa_operation_unref(operation); throw; }
        pa_operation_unref(operation);
        if (query.done < 0) fail();
        return query.result;
    }
};
}
