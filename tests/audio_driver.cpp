// SPDX-License-Identifier: GPL-3.0-or-later
// Run only against the private test server created by test_audio.py.
#include "mm_sstv/transmitter.hpp"
#include "mm_sstv/audio_input.hpp"
#include <fstream>
#include <mutex>
#include <algorithm>
#include <chrono>
#include <iostream>
#include <thread>
using namespace mm_sstv;
int main(int argc, char** argv) {
    try {
        if (argc < 2) return 2;
        Transmitter tx(makeAudioOutput());
        if (std::string(argv[1]) == "list") {
            for (const auto& d : tx.audio().devices()) std::cout << d.id << '\n';
            return 0;
        }
        if (argc == 5 && std::string(argv[1]) == "capture") {
            auto input = makeAudioInput(); std::vector<float> captured; std::mutex guard;
            input->start(argv[2], [&](const float* data, std::size_t size) {
                std::lock_guard<std::mutex> lock(guard); captured.insert(captured.end(), data, data + size);
            });
            std::cout << "capturing\n" << std::flush;
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(std::stoi(argv[3]));
            while (std::chrono::steady_clock::now() < deadline) {
                if (!input->error().empty()) { input->stop(); return 3; }
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            input->stop(); std::ofstream file(argv[4], std::ios::binary);
            file.write(reinterpret_cast<const char*>(captured.data()), captured.size() * sizeof(float));
            std::cout << "captured " << captured.size() << " samples\n"; return file ? 0 : 1;
        }
        if (argc != 4) return 2;
        const auto duration = std::chrono::milliseconds(std::stoi(argv[3]));
        if (std::string(argv[1]) == "finite") {
            std::size_t at = 0; const std::size_t total = duration.count() * 48;
            tx.audio().start(argv[2], [&](float* output, std::size_t capacity) {
                const auto count = std::min(capacity, total - at);
                for (std::size_t n = 0; n < count; ++n) output[n] = at + n + 480 >= total ? .375f : .125f;
                at += count; return count;
            });
            const auto deadline = std::chrono::steady_clock::now() + duration + std::chrono::seconds(3);
            while (!tx.audio().finished() && std::chrono::steady_clock::now() < deadline) {
                if (!tx.audio().error().empty()) throw std::runtime_error(tx.audio().error());
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
            if (!tx.audio().finished()) throw std::runtime_error("Finite stream never finished");
            tx.stop(); std::cout << "drained " << at << " samples\n"; return 0;
        }
        const unsigned cycles = std::string(argv[1]) == "cycles" ? 3 : 1;
        for (unsigned cycle = 0; cycle < cycles; ++cycle) {
            tx.startTest(argv[2]); std::cout << "started\n" << std::flush;
            const auto deadline = std::chrono::steady_clock::now() + duration;
            while (std::chrono::steady_clock::now() < deadline) {
                if (const auto error = tx.audio().error(); !error.empty()) {
                    tx.stop(); std::cerr << error << '\n'; return 3;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            const auto before = std::chrono::steady_clock::now(); tx.stop(); tx.stop();
            const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - before).count();
            std::cout << "stopped " << ms << " ms; underruns " << tx.audio().underruns() << '\n' << std::flush;
            if (ms > 350) return 4;
        }
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
