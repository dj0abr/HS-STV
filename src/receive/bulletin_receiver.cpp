// SPDX-License-Identifier: GPL-3.0-or-later
#include "bulletin_receiver.hpp"
#include "beacon_text.hpp"
#include "bulletin_zip.hpp"
#include <QCryptographicHash>
#include <QMetaObject>
#include <QRegularExpression>
#include <QTextDocument>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextFragment>
#include <QUrl>
#include <QVariant>
#include <algorithm>
#include <stdexcept>
namespace mm_sstv {
namespace {
class PlainDocument final : public QTextDocument {
protected:
    QVariant loadResource(int, const QUrl&) override { return {}; }
};
Bulletin decodeBulletin(const QString& filename, const QByteArray& zip, const QDateTime& at) {
    const auto bytes = readBulletinZip(zip);
    Bulletin result; result.filename = filename; result.receivedAt = at.toUTC();
    result.digest = QCryptographicHash::hash(bytes, QCryptographicHash::Sha256);
    auto text = decodeBeaconText(bytes); text.replace("\r\n", "\n"); text.replace('\r', '\n');
    const auto metadata = QRegularExpression("^~([A-Za-z]):(\\d+):([^~\\n]*)~\\n").match(text);
    if (metadata.hasMatch()) {
        result.category = metadata.captured(1); result.page = metadata.captured(2).toUInt();
        result.title = metadata.captured(3); text.remove(0, metadata.capturedLength());
    }
    if (text.contains(QRegularExpression("</?[A-Za-z][^>]*>"))) {
        // Drop hidden content before parsing, and refuse image/style resources.
        text.remove(QRegularExpression("<(script|style)\\b[^>]*>[\\s\\S]*?(?:</\\1\\s*>|$)",
                                       QRegularExpression::CaseInsensitiveOption));
        PlainDocument document;
        // Beacon bulletins mix HTML headings/links with line-oriented text
        // inside <p>. Normal HTML whitespace handling would flatten the source
        // line breaks and blank lines before they ever reach the archive/UI.
        document.setDefaultStyleSheet("* { white-space: pre-wrap; }");
        document.setHtml(text);
        struct Link { int first, last; QString href; };
        std::vector<Link> links;
        for (auto block = document.begin(); block.isValid(); block = block.next()) {
            for (auto it = block.begin(); !it.atEnd(); ++it) {
                const auto fragment = it.fragment(); if (!fragment.isValid()) continue;
                const auto href = fragment.charFormat().anchorHref();
                if (!href.startsWith("https://") && !href.startsWith("http://")) continue;
                if (!links.empty() && links.back().href == href && links.back().last == fragment.position())
                    links.back().last += fragment.length();
                else links.push_back({fragment.position(), fragment.position() + fragment.length(), href});
            }
        }
        for (auto it = links.rbegin(); it != links.rend(); ++it) {
            QTextCursor cursor(&document); cursor.setPosition(it->first); cursor.setPosition(it->last, QTextCursor::KeepAnchor);
            if (!cursor.selectedText().contains(it->href)) { cursor.clearSelection(); cursor.insertText(" (" + it->href + ")"); }
        }
        text = document.toPlainText();
        text.remove(QChar(0xfffc));
    }
    auto lines = text.split('\n');
    for (auto& line : lines) { while (!line.isEmpty() && line.back().isSpace()) line.chop(1); }
    text = lines.join('\n').trimmed(); text.replace(QRegularExpression("\\n{3,}"), "\n\n");
    result.text = text; return result;
}
unsigned fileSize(const Payload& p) { return (unsigned(p[52]) << 16) | (unsigned(p[53]) << 8) | p[54]; }
}
BulletinUpdate BulletinAssembler::accept(const ReceivedFrame& frame, const QDateTime& at) {
    if (frame.type < 2 || frame.type > 5 || frame.status > 3 || frame.counter > 1023 || !at.isValid()) return {};
    BulletinUpdate update;
    if (frame.status == 0 || frame.status == 3) {
        active_.clear();
        QByteArray name(reinterpret_cast<const char*>(frame.payload.data()), 50);
        const auto nul = name.indexOf('\0'); if (nul >= 0) name.truncate(nul);
        auto filename = decodeBeaconText(name); filename.replace('\\', '/'); filename = filename.section('/', -1);
        if (frame.type == 2 || !filename.endsWith(".blt", Qt::CaseInsensitive)) return {};
        const unsigned size = fileSize(frame.payload), expected = (size + 55 + 218) / 219;
        if (!size || size > 219 * 1024 - 55 || (frame.status == 0 && frame.counter != 0) ||
            (frame.status == 3 && expected != 1)) {
            update.progress = BulletinProgress{filename, "Ungültiger Bulletin-Dateikopf", 0, 0, false, true}; return update;
        }
        QByteArray key(reinterpret_cast<const char*>(frame.payload.data()), 55); key.append(char(frame.type));
        if (!partials_.count(key)) {
            if (partials_.size() >= 32) {
                const auto oldest = std::min_element(partials_.begin(), partials_.end(),
                    [](const auto& a, const auto& b) { return a.second.used < b.second.used; });
                partials_.erase(oldest);
            }
            Transfer transfer; transfer.filename = filename; transfer.kind = frame.type;
            transfer.fileId = (unsigned(frame.payload[50]) << 8) | frame.payload[51];
            transfer.size = size; transfer.expected = expected; transfer.blocks.resize(expected);
            partials_.emplace(key, std::move(transfer));
        }
        active_ = key;
    }
    if (active_.isEmpty()) return update;
    auto& transfer = partials_.at(active_);
    if (frame.type != transfer.kind) return update;
    const unsigned index = frame.status == 3 ? 0 : frame.counter;
    if (index >= transfer.expected || (frame.status == 2 && index != transfer.expected - 1)) return update;
    transfer.used = ++clock_;
    if (!transfer.blocks[index]) ++transfer.received;
    transfer.blocks[index] = frame.payload;
    update.progress = BulletinProgress{transfer.filename, {}, transfer.received, transfer.expected, false, false};
    if (transfer.received != transfer.expected) return update;
    QByteArray zip; zip.reserve(int(transfer.size + 219));
    for (unsigned n = 0; n < transfer.expected; ++n) {
        const auto& block = *transfer.blocks[n]; const unsigned offset = n == 0 ? 55 : 0;
        zip.append(reinterpret_cast<const char*>(block.data() + offset), int(block.size() - offset));
    }
    zip.truncate(int(transfer.size));
    if (crc16(reinterpret_cast<const std::uint8_t*>(zip.constData()), std::size_t(zip.size())) != transfer.fileId) {
        for (unsigned n = 1; n < transfer.expected; ++n) transfer.blocks[n].reset();
        transfer.received = 1; update.progress->received = 1;
        update.progress->error = true; update.progress->message = "Datei-CRC falsch · warte auf Wiederholung"; return update;
    }
    try {
        auto bulletin = decodeBulletin(transfer.filename, zip, at);
        const auto key = transfer.filename.toUtf8() + '\0' + bulletin.digest;
        if (std::find(delivered_.begin(), delivered_.end(), key) == delivered_.end()) {
            delivered_.push_back(key); while (delivered_.size() > 256) delivered_.pop_front();
            if (!lastPublished_.isValid() || bulletin.receivedAt >= lastPublished_) {
                lastPublished_ = bulletin.receivedAt; update.bulletin = std::move(bulletin);
            }
        }
        update.progress->complete = true;
        update.progress->message = "Bulletin vollständig geprüft";
        partials_.erase(active_); active_.clear();
    } catch (const std::exception& error) {
        update.progress->error = true; update.progress->message = QString::fromUtf8(error.what());
    }
    return update;
}
void BulletinAssembler::retryDelivery(const Bulletin& bulletin) {
    const auto key = bulletin.filename.toUtf8() + '\0' + bulletin.digest;
    delivered_.erase(std::remove(delivered_.begin(), delivered_.end(), key), delivered_.end());
}
BulletinReceiver::BulletinReceiver(QObject* parent, QString archiveDirectory)
    : QObject(parent), archive_(std::move(archiveDirectory)), worker_([this] { run(); }) {}
BulletinReceiver::~BulletinReceiver() {
    { std::lock_guard<std::mutex> lock(mutex_); stopping_ = true; queue_.clear(); ++generation_; }
    ready_.notify_one(); if (worker_.joinable()) worker_.join();
}
void BulletinReceiver::resetStream() {
    {
        std::lock_guard<std::mutex> lock(mutex_); const auto next = ++generation_;
        queue_.erase(std::remove_if(queue_.begin(), queue_.end(), [](const Work& w) { return w.kind == WorkKind::Frame || w.kind == WorkKind::Reset; }), queue_.end());
        queue_.push_front({{}, {}, next, WorkKind::Reset, {}});
    }
    ready_.notify_one();
}
void BulletinReceiver::listArchive() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        queue_.erase(std::remove_if(queue_.begin(), queue_.end(), [](const Work& w) { return w.kind == WorkKind::List; }), queue_.end());
        queue_.push_back({{}, {}, 0, WorkKind::List, {}});
    }
    ready_.notify_one();
}
void BulletinReceiver::loadArchive(const QString& id) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        queue_.erase(std::remove_if(queue_.begin(), queue_.end(), [](const Work& w) { return w.kind == WorkKind::Load; }), queue_.end());
        queue_.push_back({{}, {}, 0, WorkKind::Load, id});
    }
    ready_.notify_one();
}
void BulletinReceiver::submit(const ReceivedFrame& frame) {
    if (frame.type < 2 || frame.type > 5) return;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (queue_.size() >= 512) {
            const auto next = ++generation_;
            queue_.erase(std::remove_if(queue_.begin(), queue_.end(), [](const Work& w) { return w.kind == WorkKind::Frame || w.kind == WorkKind::Reset; }), queue_.end());
            queue_.push_front({{}, {}, next, WorkKind::Reset, {}});
        }
        queue_.push_back({frame, QDateTime::currentDateTimeUtc(), generation_.load(), WorkKind::Frame, {}});
    }
    ready_.notify_one();
}
void BulletinReceiver::run() {
    for (;;) {
        Work work;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            ready_.wait(lock, [this] { return stopping_ || !queue_.empty(); });
            if (stopping_) return;
            work = std::move(queue_.front()); queue_.pop_front();
        }
        if (work.kind == WorkKind::List || work.kind == WorkKind::Load) {
            try {
                if (work.kind == WorkKind::List) {
                    auto entries = archive_.entries();
                    QMetaObject::invokeMethod(this, [this, entries = std::move(entries)] { if (archiveListed) archiveListed(entries); }, Qt::QueuedConnection);
                } else {
                    auto bulletin = archive_.load(work.id);
                    QMetaObject::invokeMethod(this, [this, id = work.id, bulletin = std::move(bulletin)] { if (archiveLoaded) archiveLoaded(id, bulletin); }, Qt::QueuedConnection);
                }
            } catch (const std::exception& error) {
                const auto message = QString::fromUtf8(error.what());
                QMetaObject::invokeMethod(this, [this, id = work.id, message] { if (archiveError) archiveError(id, message); }, Qt::QueuedConnection);
            }
            continue;
        }
        if (work.generation != generation_.load()) continue;
        if (work.kind == WorkKind::Reset) { assembler_.resetStream(); continue; }
        auto update = assembler_.accept(*work.frame, work.at);
        if (update.bulletin) {
            try {
                bool added = false; archive_.save(*update.bulletin, &added);
                if (update.progress) update.progress->message = "Bulletin vollständig geprüft und gespeichert";
                if (added) QMetaObject::invokeMethod(this, [this] { if (archiveChanged) archiveChanged(); }, Qt::QueuedConnection);
            } catch (const std::exception& error) {
                assembler_.retryDelivery(*update.bulletin);
                if (!update.progress) update.progress = BulletinProgress{};
                update.progress->error = true;
                update.progress->message = "Speichern fehlgeschlagen: " + QString::fromUtf8(error.what());
            }
        }
        if (!update.bulletin && !update.progress) continue;
        QMetaObject::invokeMethod(this, [this, update = std::move(update), generation = work.generation] {
            if (generation != generation_.load()) return;
            if (update.bulletin && bulletinReceived) bulletinReceived(*update.bulletin);
            if (update.progress && progressChanged) progressChanged(*update.progress);
        }, Qt::QueuedConnection);
    }
}
}
