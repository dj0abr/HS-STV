// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "mm_sstv/audio_input.hpp"
#include "mm_sstv/image_assembler.hpp"
#include <QByteArray>
#include <QElapsedTimer>
#include <QObject>
#include <QProcess>
#include <QTimer>
#include <atomic>
#include <array>
#include <deque>
#include <mutex>
namespace mm_sstv {
struct ReceiveStatus {
    bool locked = false;
    double pilot = -1, offset = 0, level = -200;
    std::uint64_t frames = 0;
    QString state;
    double center = 1500, spectrumBinHz = 0;
    std::vector<float> spectrum;
    std::vector<std::array<float, 2>> points;
};
class Receiver final : public QObject {
public:
    explicit Receiver(std::unique_ptr<AudioInput> input, QObject* parent = nullptr);
    ~Receiver() override;
    bool available() const { return bool(input_); }
    bool running() const { return active_; }
    std::vector<AudioDevice> devices();
    void start(const std::string& device);
    void stop();
    std::function<void(const ReceivedImage&)> imageReceived;
    std::function<void(const ReceiveStatus&)> statusChanged;
    std::function<void(const QString&)> failed;
private:
    void pump();
    void readOutput();
    void fail(QString message);
    std::unique_ptr<AudioInput> input_;
    QProcess core_;
    QTimer timer_;
    QElapsedTimer statusAge_;
    QByteArray lines_;
    ImageAssembler assembler_;
    std::mutex mutex_;
    std::deque<std::vector<float>> queued_;
    std::size_t queuedSamples_ = 0;
    bool overflow_ = false, active_ = false, stale_ = false;
    std::atomic<bool> accepting_{false};
};
}
