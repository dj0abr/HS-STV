// SPDX-License-Identifier: GPL-3.0-or-later
#include "image_codec.hpp"
#include "mm_sstv/protocol.hpp"
#include <QBuffer>
#include <QPainter>
#include <QImageReader>
#include <stdexcept>
#include <atomic>
#include <cstdio>
#include <mutex>
namespace mm_sstv {
namespace {
// Qt's JPEG plugin reports a synthetic EOI inside a scan as a warning. That is
// expected only for our partial previews. Keep all other logging, including the
// same warning from other threads or complete-image reads, on its original path.
thread_local bool readingPartialJpeg = false;
std::atomic<QtMessageHandler> previousImageMessageHandler{nullptr};
void imageMessageHandler(QtMsgType type, const QMessageLogContext& context, const QString& message) {
    if (readingPartialJpeg && type == QtWarningMsg &&
        message == QLatin1String("Corrupt JPEG data: premature end of data segment")) return;
    if (const auto previous = previousImageMessageHandler.load()) previous(type, context, message);
    else {
        // Another thread may log during the first handler installation.
        const auto formatted = qFormatLogMessage(type, context, message).toLocal8Bit();
        if (!formatted.isEmpty()) { std::fwrite(formatted.constData(), 1, std::size_t(formatted.size()), stderr); std::fputc('\n', stderr); }
    }
}
class PartialJpegRead final {
public:
    explicit PartialJpegRead(bool partial) : previous_(readingPartialJpeg) {
        if (partial) {
            static std::once_flag installed;
            std::call_once(installed, [] { previousImageMessageHandler.store(qInstallMessageHandler(imageMessageHandler)); });
        }
        readingPartialJpeg = partial;
    }
    ~PartialJpegRead() { readingPartialJpeg = previous_; }
    PartialJpegRead(const PartialJpegRead&) = delete;
    PartialJpegRead& operator=(const PartialJpegRead&) = delete;
private:
    bool previous_;
};
}
QImage decodeReceivedJpeg(const std::vector<std::uint8_t>& bytes, bool complete) {
    if (bytes.size() < (complete ? 4u : 1024u)) return {};
    const PartialJpegRead previewRead(!complete);
    QByteArray jpeg(reinterpret_cast<const char*>(bytes.data()), static_cast<int>(bytes.size()));
    if (!complete && !jpeg.endsWith(QByteArray::fromHex("ffd9"))) jpeg.append(QByteArray::fromHex("ffd9"));
    QBuffer buffer(&jpeg); buffer.open(QIODevice::ReadOnly); QImageReader reader(&buffer, "JPEG");
    const auto size = reader.size();
    if (!size.isValid() || qint64(size.width()) * size.height() > 40000000) return {};
    return reader.read();
}

PreparedPicture preparePicture(const QImage& original, QSize target) {
    if (original.isNull() || !target.isValid() || target.width() > 4096 || target.height() > 4096)
        throw std::invalid_argument("Ungültiges Bild oder Zielformat");
    QImage canvas(target, QImage::Format_RGB32); canvas.fill(Qt::white);
    {
        QPainter painter(&canvas); painter.setRenderHint(QPainter::SmoothPixmapTransform);
        const auto size = original.size().scaled(target, Qt::KeepAspectRatio);
        painter.drawImage(QRect(QPoint((target.width() - size.width()) / 2, (target.height() - size.height()) / 2), size), original);
    }
    for (int quality = 90; quality >= 20; quality -= 5) {
        QByteArray encoded; QBuffer buffer(&encoded); buffer.open(QIODevice::WriteOnly);
        if (!canvas.save(&buffer, "JPEG", quality)) throw std::runtime_error("JPEG-Kodierung nicht verfügbar");
        if (static_cast<std::size_t>(encoded.size()) > ImageTransmission::maxJpegBytes) continue;
        const auto* begin = reinterpret_cast<const std::uint8_t*>(encoded.constData());
        PreparedPicture result; result.jpeg.assign(begin, begin + encoded.size());
        result.preview = QImage::fromData(encoded, "JPEG"); result.quality = quality; return result;
    }
    throw std::runtime_error("Bild bleibt größer als 200 kB. Bitte ein kleineres Zielformat wählen.");
}
}
