// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "mm_sstv/audio_input.hpp"
#include "mm_sstv/modem_mode.hpp"
#include "mm_sstv/image_assembler.hpp"
#include "beacon_messages.hpp"
#include "bulletin_receiver.hpp"
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
    Modulation modulation = Modulation::Apsk8;
    bool locked = false;
    double pilot = -1, offset = 0, level = -200;
    std::uint64_t frames = 0;
    std::uint64_t headerCandidates = 0, crcFailures = 0, fecFailures = 0;
    std::uint64_t fecCorrectedFrames = 0, fecCorrectedBytes = 0;
    std::uint64_t syncDetected = 0, syncMissed = 0;
    QString state;
    double center = 1500, spectrumBinHz = 0;
    std::vector<float> spectrum;
    double waterfallBinHz = 0;
    std::vector<std::vector<float>> waterfall; // Consecutive 10-ms rows, oldest first.
    std::vector<std::array<float, 2>> points;
};
struct SstvUpdate {
    enum class Kind { Begin, Line, End } kind = Kind::Begin;
    Modulation modulation = Modulation::Martin1;
    bool partial = false, complete = false, synchronized = false;
    unsigned index = 0;
    std::array<std::uint8_t, 960> rgb{};
};
class Receiver final : public QObject {
public:
    explicit Receiver(std::unique_ptr<AudioInput> input, QObject* parent = nullptr);
    ~Receiver() override;
    bool available() const { return bool(input_); }
    bool running() const { return active_; }
    std::vector<AudioDevice> devices();
    void start(const std::string& device, Modulation mode = Modulation::Apsk8);
    void stop();
    void listBulletinArchive() { bulletins_.listArchive(); }
    void loadBulletinArchive(const QString& id) { bulletins_.loadArchive(id); }
    std::function<void(const ReceivedImage&)> imageReceived;
    std::function<void(const SstvUpdate&)> sstvReceived;
    std::function<void(const BeaconMessage&)> beaconMessageReceived;
    std::function<void(const Bulletin&)> bulletinReceived;
    std::function<void(const BulletinProgress&)> bulletinProgressChanged;
    std::function<void()> bulletinArchiveChanged;
    std::function<void(const std::vector<BulletinArchiveEntry>&)> bulletinArchiveListed;
    std::function<void(const QString&, const Bulletin&)> bulletinArchiveLoaded;
    std::function<void(const QString&, const QString&)> bulletinArchiveError;
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
    BeaconMessages beaconMessages_;
    BulletinReceiver bulletins_;
    Modulation mode_ = Modulation::Apsk8;
    std::mutex mutex_;
    std::deque<std::vector<float>> queued_;
    std::size_t queuedSamples_ = 0;
    bool overflow_ = false, active_ = false, stale_ = false;
    std::atomic<bool> accepting_{false};
};
}
