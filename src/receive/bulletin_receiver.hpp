// SPDX-License-Identifier: GPL-3.0-or-later
// Protocol adapted from QO100_TRX2/external/mmdecoder/data_collector.py.
#pragma once
#include "mm_sstv/image_assembler.hpp"
#include <QByteArray>
#include <QDateTime>
#include <QObject>
#include <QString>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <thread>
namespace mm_sstv {
struct Bulletin {
    QString filename, title, category, text;
    unsigned page = 0;
    QByteArray digest;
    QDateTime receivedAt;
};
struct BulletinProgress {
    QString filename, message;
    unsigned received = 0, expected = 0;
    bool complete = false, error = false;
};
struct BulletinUpdate {
    std::optional<Bulletin> bulletin;
    std::optional<BulletinProgress> progress;
};
// Owned by a single worker thread; retains at most 32 partial radio files.
class BulletinAssembler {
public:
    BulletinUpdate accept(const ReceivedFrame& frame, const QDateTime& at = QDateTime::currentDateTimeUtc());
    void resetStream() { active_.clear(); delivered_.clear(); lastPublished_ = {}; }
private:
    struct Transfer {
        QString filename;
        unsigned kind = 0, fileId = 0, size = 0, expected = 0, received = 0;
        std::vector<std::optional<Payload>> blocks;
        std::uint64_t used = 0;
    };
    std::map<QByteArray, Transfer> partials_;
    QByteArray active_;
    std::deque<QByteArray> delivered_;
    QDateTime lastPublished_;
    std::uint64_t clock_ = 0;
};
// All ZIP/HTML processing stays off the GUI and audio threads. Callbacks are
// delivered on this object's owning Qt thread; resets invalidate stale results.
class BulletinReceiver final : public QObject {
public:
    explicit BulletinReceiver(QObject* parent = nullptr);
    ~BulletinReceiver() override;
    void submit(const ReceivedFrame& frame);
    void resetStream();
    std::function<void(const Bulletin&)> bulletinReceived;
    std::function<void(const BulletinProgress&)> progressChanged;
private:
    struct Work { std::optional<ReceivedFrame> frame; QDateTime at; std::uint64_t generation; };
    void run();
    BulletinAssembler assembler_;
    std::mutex mutex_;
    std::condition_variable ready_;
    std::deque<Work> queue_;
    std::atomic<std::uint64_t> generation_{0};
    bool stopping_ = false;
    std::thread worker_;
};
}
