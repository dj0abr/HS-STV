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
    bulletins_.archiveChanged = [this] { if (bulletinArchiveChanged) bulletinArchiveChanged(); };
    bulletins_.archiveListed = [this](const auto& entries) { if (bulletinArchiveListed) bulletinArchiveListed(entries); };
    bulletins_.archiveLoaded = [this](const auto& id, const auto& bulletin) { if (bulletinArchiveLoaded) bulletinArchiveLoaded(id, bulletin); };
    bulletins_.archiveError = [this](const auto& id, const auto& error) { if (bulletinArchiveError) bulletinArchiveError(id, error); };
    bulletins_.bulletinReceived = [this](const Bulletin& bulletin) { if (active_ && isBeacon(mode_) && bulletinReceived) bulletinReceived(bulletin); };
    bulletins_.progressChanged = [this](const BulletinProgress& progress) { if (active_ && isBeacon(mode_) && bulletinProgressChanged) bulletinProgressChanged(progress); };
    connect(&core_, &QProcess::readyReadStandardOutput, this, [this] { readOutput(); });
    connect(&core_, &QProcess::readyReadStandardError, this, [this] { core_.readAllStandardError(); });
    connect(&core_, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this, [this](int, QProcess::ExitStatus) {
        if (active_) fail("Empfangskern wurde beendet.");
    });
    connect(&timer_, &QTimer::timeout, this, [this] { pump(); }); timer_.setInterval(10);
}
Receiver::~Receiver() { stop(); }
std::vector<AudioDevice> Receiver::devices() { return input_ ? input_->devices() : std::vector<AudioDevice>{}; }
void Receiver::start(const std::string& device, Modulation mode) {
    stop();
    const auto sampleRate = modeSampleRate(mode); mode_ = mode;
    if (!input_) throw std::runtime_error("Kein Audioeingang vorhanden");
    if (device.empty()) throw std::runtime_error("Kein Audioeingang gewählt");
    QString executable = QCoreApplication::applicationDirPath() + (isSstv(mode_) ? "/hs-stv-sstv-rx-core" : "/hs-stv-rx-core");
#ifdef Q_OS_WIN
    executable += ".exe";
#endif
    if (!QFileInfo(executable).isExecutable()) throw std::runtime_error("Empfangskern hs-stv-rx-core fehlt. Bitte neu bauen.");
    lines_.clear(); core_.start(executable, {"--input-rate", QString::number(sampleRate), "--monitor", "--modulation", QString::fromLatin1(modeKey(mode_))});
    if (!core_.waitForStarted(1000)) throw std::runtime_error(core_.errorString().toStdString());
    accepting_ = true; active_ = true; stale_ = false; statusAge_.start();
    try {
        input_->start(device, [this, limit = sampleRate * 2](const float* data, std::size_t count) {
            if (!accepting_) return;
            std::lock_guard<std::mutex> lock(mutex_);
            if (count > limit || queuedSamples_ + count > limit) { overflow_ = true; return; }
            queued_.emplace_back(data, data + count); queuedSamples_ += count;
        }, sampleRate);
        timer_.start();
        if (statusChanged) { ReceiveStatus status; status.modulation = mode_; status.state = "Suche"; statusChanged(status); }
    } catch (...) { stop(); throw; }
}
void Receiver::stop() {
    bulletins_.resetStream();
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
        if (statusChanged) { ReceiveStatus status; status.modulation = mode_; status.state = "Keine aktuellen Audiodaten"; statusChanged(status); }
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
            ReceiveStatus status; status.modulation = mode_; status.locked = json.value("locked").toBool();
            status.pilot = json.value("pilot_hz").toDouble(-1); status.offset = json.value("frequency_offset_hz").toDouble();
            status.level = json.value("rms_dbfs").toDouble(-200); status.frames = json.value(isSstv(mode_) ? "received_lines" : "valid_frames").toDouble();
            status.headerCandidates = json.value("header_candidates").toDouble();
            status.crcFailures = json.value("crc_failures").toDouble(); status.fecFailures = json.value("fec_failures").toDouble();
            status.fecCorrectedFrames = json.value("fec_corrected_frames").toDouble(); status.fecCorrectedBytes = json.value("fec_corrected_bytes").toDouble();
            status.syncDetected = json.value("sync_detected").toDouble(); status.syncMissed = json.value("sync_missed").toDouble();
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
            status.waterfallBinHz = json.value("waterfall_bin_hz").toDouble();
            const auto waterfall = json.value("waterfall_dbfs").toArray();
            if (isSstv(mode_) && waterfall.size() <= 10 && status.waterfallBinHz > 0 && std::isfinite(status.waterfallBinHz)) {
                for (const auto value : waterfall) {
                    const auto bins = value.toArray();
                    if (bins.isEmpty() || bins.size() > 512) continue;
                    std::vector<float> row; row.reserve(bins.size());
                    for (const auto bin : bins) {
                        const double db = bin.toDouble(-160);
                        row.push_back(std::isfinite(db) ? float(db) : -160.f);
                    }
                    status.waterfall.push_back(std::move(row));
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
            status.state = isSstv(mode_) ? (status.locked ? "Zeilensynchron" : json.value("receiving").toBool() ? "Bildempfang · Freilauf" : "Suche SSTV") : status.locked ? "Synchronisiert" : state == "acquiring" ? "Pilot gefunden · synchronisiere" : state == "hold" ? "Nachführung" : "Suche";
            if (statusChanged) statusChanged(status);
        } else if (event.startsWith("sstv_") && isSstv(mode_)) {
            SstvUpdate update; update.modulation = mode_;
            if (event == "sstv_begin") { update.kind = SstvUpdate::Kind::Begin; update.partial = json.value("partial").toBool(); }
            else if (event == "sstv_end") { update.kind = SstvUpdate::Kind::End; update.complete = json.value("complete").toBool(); }
            else if (event == "sstv_line") {
                const int row = json.value("index").toInt(-1);
                const auto hex = json.value("rgb_hex").toString().toLatin1();
                if (row < 0 || row >= 256 || hex.size() != 1920) continue;
                const auto rgb = QByteArray::fromHex(hex); if (rgb.size() != 960) continue;
                update.kind = SstvUpdate::Kind::Line; update.index = unsigned(row); update.synchronized = json.value("synchronized").toBool();
                std::copy(rgb.begin(), rgb.end(), update.rgb.begin());
            } else continue;
            if (sstvReceived) sstvReceived(update);
        } else if (event == "frame") {
            const auto hex = json.value("payload_hex").toString().toLatin1(); if (hex.size() != 438) continue;
            const auto payload = QByteArray::fromHex(hex); if (payload.size() != 219) continue;
            ReceivedFrame frame; frame.type = json.value("frame_type").toInt(-1); frame.counter = json.value("frame_number").toInt(-1); frame.status = json.value("frame_info").toInt(-1);
            std::copy(payload.begin(), payload.end(), frame.payload.begin());
            if (isBeacon(mode_)) {
                bulletins_.submit(frame);
                if (const auto message = beaconMessages_.accept(frame); message && beaconMessageReceived) beaconMessageReceived(*message);
            } else if (const auto image = assembler_.accept(frame); image && imageReceived) imageReceived(*image);
        }
    }
    if (lines_.size() > 65536) fail("Ungültige Ausgabe des Empfangskerns");
}
}
