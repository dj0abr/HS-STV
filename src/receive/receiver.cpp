// SPDX-License-Identifier: GPL-3.0-or-later
#include "receiver.hpp"
#include <QCoreApplication>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <cmath>
#include <stdexcept>
namespace mm_sstv {
Receiver::Receiver(std::unique_ptr<AudioInput> input, QObject* parent) : QObject(parent), input_(std::move(input)) {
    connect(&core_, &QProcess::readyReadStandardOutput, this, [this] { readOutput(); });
    connect(&core_, &QProcess::readyReadStandardError, this, [this] { core_.readAllStandardError(); });
    connect(&core_, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this, [this](int, QProcess::ExitStatus) {
        if (active_) fail("Empfangskern wurde beendet.");
    });
    connect(&timer_, &QTimer::timeout, this, [this] { pump(); }); timer_.setInterval(10);
}
Receiver::~Receiver() { stop(); }
std::vector<AudioDevice> Receiver::devices() { return input_ ? input_->devices() : std::vector<AudioDevice>{}; }
void Receiver::start(const std::string& device) {
    stop();
    if (!input_) throw std::runtime_error("Kein Audioeingang vorhanden");
    if (device.empty()) throw std::runtime_error("Kein Audioeingang gewählt");
#ifdef Q_OS_WIN
    const QString executable = QCoreApplication::applicationDirPath() + "/mm-sstv-rx-core.exe";
#else
    const QString executable = QCoreApplication::applicationDirPath() + "/mm-sstv-rx-core";
#endif
    if (!QFileInfo(executable).isExecutable()) throw std::runtime_error("Empfangskern mm-sstv-rx-core fehlt. Bitte neu bauen.");
    lines_.clear(); core_.start(executable, {"--input-rate", "48000", "--monitor"});
    if (!core_.waitForStarted(1000)) throw std::runtime_error(core_.errorString().toStdString());
    accepting_ = true; active_ = true; stale_ = false; statusAge_.start();
    try {
        input_->start(device, [this](const float* data, std::size_t count) {
            if (!accepting_) return;
            std::lock_guard<std::mutex> lock(mutex_);
            if (count > 96000 || queuedSamples_ + count > 96000) { overflow_ = true; return; }
            queued_.emplace_back(data, data + count); queuedSamples_ += count;
        });
        timer_.start();
        if (statusChanged) { ReceiveStatus status; status.state = "Suche"; statusChanged(status); }
    } catch (...) { stop(); throw; }
}
void Receiver::stop() {
    accepting_ = false; active_ = false; timer_.stop();
    if (input_) input_->stop();
    if (core_.state() != QProcess::NotRunning) {
        core_.closeWriteChannel();
        if (!core_.waitForFinished(500)) { core_.kill(); core_.waitForFinished(500); }
    }
    { std::lock_guard<std::mutex> lock(mutex_); queued_.clear(); queuedSamples_ = 0; overflow_ = false; }
    lines_.clear(); assembler_.resetStream();
}
void Receiver::fail(QString message) { stop(); if (failed) failed(message); }
void Receiver::pump() {
    if (!active_) return;
    if (const auto error = input_->error(); !error.empty()) { fail(QString::fromStdString(error)); return; }
    std::deque<std::vector<float>> blocks; bool overflow;
    {
        std::lock_guard<std::mutex> lock(mutex_); overflow = overflow_;
        blocks.swap(queued_); queuedSamples_ = 0;
    }
    if (overflow || core_.bytesToWrite() > 192000) { fail("Empfangspuffer übergelaufen."); return; }
    for (const auto& block : blocks) {
        const auto bytes = qint64(block.size() * sizeof(float));
        if (core_.write(reinterpret_cast<const char*>(block.data()), bytes) != bytes) { fail("Audio konnte nicht an den Empfangskern übergeben werden."); return; }
    }
    if (!stale_ && statusAge_.elapsed() > 2000) {
        stale_ = true;
        if (statusChanged) { ReceiveStatus status; status.state = "Keine aktuellen Audiodaten"; statusChanged(status); }
    }
}
void Receiver::readOutput() {
    const auto output = core_.readAllStandardOutput();
    if (!active_) return;
    lines_ += output;
    while (true) {
        const auto end = lines_.indexOf('\n'); if (end < 0) break;
        const auto line = lines_.left(end); lines_.remove(0, end + 1);
        const auto json = QJsonDocument::fromJson(line).object();
        const auto event = json.value("event").toString();
        if (event == "status") {
            statusAge_.restart(); stale_ = false;
            ReceiveStatus status; status.locked = json.value("locked").toBool();
            status.pilot = json.value("pilot_hz").toDouble(-1); status.offset = json.value("frequency_offset_hz").toDouble();
            status.level = json.value("rms_dbfs").toDouble(-200); status.frames = json.value("valid_frames").toDouble();
            status.center = json.value("center_hz").toDouble(1500);
            status.spectrumBinHz = json.value("spectrum_bin_hz").toDouble();
            const auto spectrum = json.value("spectrum_dbfs").toArray();
            if (spectrum.size() <= 4096 && status.spectrumBinHz > 0 && std::isfinite(status.spectrumBinHz)) {
                status.spectrum.reserve(spectrum.size());
                for (const auto value : spectrum) {
                    const auto db = value.toDouble(-160);
                    status.spectrum.push_back(std::isfinite(db) ? float(db) : -160.f);
                }
            }
            const auto points = json.value("points").toArray();
            for (const auto value : points) {
                if (status.points.size() >= 96) break;
                const auto point = value.toArray();
                if (point.size() != 2 || !point[0].isDouble() || !point[1].isDouble()) continue;
                const double x = point[0].toDouble(), y = point[1].toDouble();
                if (std::isfinite(x) && std::isfinite(y) && std::abs(x) <= 10 && std::abs(y) <= 10)
                    status.points.push_back({float(x), float(y)});
            }
            const auto state = json.value("afc_state").toString();
            status.state = status.locked ? "Synchronisiert" : state == "acquiring" ? "Pilot gefunden · synchronisiere" : state == "hold" ? "Nachführung" : "Suche";
            if (statusChanged) statusChanged(status);
        } else if (event == "frame") {
            const auto hex = json.value("payload_hex").toString().toLatin1(); if (hex.size() != 438) continue;
            const auto payload = QByteArray::fromHex(hex); if (payload.size() != 219) continue;
            ReceivedFrame frame; frame.type = json.value("frame_type").toInt(-1); frame.counter = json.value("frame_number").toInt(-1); frame.status = json.value("frame_info").toInt(-1);
            std::copy(payload.begin(), payload.end(), frame.payload.begin());
            if (const auto image = assembler_.accept(frame); image && imageReceived) imageReceived(*image);
        }
    }
    if (lines_.size() > 65536) fail("Ungültige Ausgabe des Empfangskerns");
}
}
