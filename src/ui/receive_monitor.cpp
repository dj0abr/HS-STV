// SPDX-License-Identifier: GPL-3.0-or-later
#include "receive_monitor.hpp"
#include "mm_sstv/modem_mode.hpp"
#include <QFontDatabase>
#include <QPainter>
#include <QPolygonF>
#include <algorithm>
#include <cmath>
namespace mm_sstv {
namespace {
const QColor background("#09141c"), grid("#243b48"), label("#97b0bf"), trace("#69d4e9"), pilot("#70efb5"), band("#e6bd6a");
constexpr double maxHz = 4000, minDb = -140;
}
ReceiveMonitor::ReceiveMonitor(QWidget* parent) : QWidget(parent) {
    setObjectName("receiveMonitor"); setMinimumSize(300, 96); setMaximumHeight(144);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    setToolTip("Empfangsaudio vor AFC und AGC · 0–4000 Hz\nGrün: erkannter Pilot · Fläche: Pilotsuche 20–900 Hz\nGelb: Datenband der aktuellen Empfangsmitte\n8APSK: tatsächlich empfangene I/Q-Symbole nach dem Symboltracking");
    clear();
}
void ReceiveMonitor::clear(const QString& message) {
    status_ = {}; status_.state = message; points_.clear(); update();
}
void ReceiveMonitor::setStatus(const ReceiveStatus& status) {
    if (status.locked != status_.locked || std::abs(status.center - status_.center) > 20 || status.points.empty()) points_.clear();
    status_ = status;
    for (const auto& point : status.points) points_.emplace_back(point[0], point[1]);
    while (points_.size() > 600) points_.pop_front();
    update();
}
void ReceiveMonitor::paintEvent(QPaintEvent*) {
    QPainter p(this); p.fillRect(rect(), background);
    auto font = QFontDatabase::systemFont(QFontDatabase::FixedFont); font.setPointSize(8); p.setFont(font);
    const double constellationWidth = std::min({height() + 6.0, 160.0, width() * .28});
    const QRectF spectrum(34, 24, width() - constellationWidth - 48, height() - 44);
    const QRectF constellation(width() - constellationWidth, 22, constellationWidth - 8, height() - 28);
    const auto x = [&](double hz) { return spectrum.left() + hz / maxHz * spectrum.width(); };
    const auto y = [&](double db) { return spectrum.top() + std::clamp(db / minDb, 0.0, 1.0) * spectrum.height(); };
    p.setPen(label); p.drawText(QRectF(8, 4, spectrum.right() - 8, 20), Qt::AlignLeft | Qt::AlignVCenter, "Empfangsaudio · dBFS/Bin");
    if (!status_.spectrum.empty())
        p.drawText(QRectF(8, 4, spectrum.right() - 8, 20), Qt::AlignRight | Qt::AlignVCenter, QString("%1 dBFS").arg(status_.level, 0, 'f', 1));
    p.drawText(QRectF(constellation.left() + 4, 4, constellation.width(), 20), Qt::AlignLeft | Qt::AlignVCenter, "8APSK");
    p.fillRect(QRectF(x(20), spectrum.top(), x(900) - x(20), spectrum.height()), QColor(20, 49, 49));
    for (int db = 0, step = spectrum.height() < 70 ? 60 : 40; db >= -120; db -= step) {
        p.setPen(grid); p.drawLine(QPointF(spectrum.left(), y(db)), QPointF(spectrum.right(), y(db)));
        p.setPen(label); p.drawText(QRectF(0, y(db) - 8, 28, 16), Qt::AlignRight | Qt::AlignVCenter, QString::number(db));
    }
    for (int hz = 0; hz <= 4000; hz += 1000) {
        p.setPen(grid); p.drawLine(QPointF(x(hz), spectrum.top()), QPointF(x(hz), spectrum.bottom()));
        p.setPen(label);
        if (hz == 4000) p.drawText(QRectF(spectrum.right() - 68, spectrum.bottom() + 3, 68, 18), Qt::AlignRight | Qt::AlignVCenter, "4000 Hz");
        else if (hz == 0) p.drawText(QRectF(spectrum.left(), spectrum.bottom() + 3, 30, 18), Qt::AlignLeft | Qt::AlignVCenter, "0");
        else p.drawText(QRectF(x(hz) - 22, spectrum.bottom() + 3, 44, 18), Qt::AlignHCenter | Qt::AlignVCenter, QString::number(hz));
    }
    p.setPen(grid); p.drawRect(spectrum);
    p.save(); p.setClipRect(spectrum.adjusted(0, 0, 1, 1));
    if (!status_.spectrum.empty() && status_.spectrumBinHz > 0) {
        // Keep narrow carriers visible when several FFT bins occupy one pixel.
        QPolygonF line; int column = -1; double maximum = minDb;
        const auto append = [&] { if (column >= 0) line << QPointF(spectrum.left() + column + .5, y(maximum)); };
        for (std::size_t i = 0; i < status_.spectrum.size(); ++i) {
            const auto hz = i * status_.spectrumBinHz; if (hz > maxHz) break;
            const int next = int(x(hz) - spectrum.left());
            if (next != column) { append(); column = next; maximum = minDb; }
            maximum = std::max(maximum, double(status_.spectrum[i]));
        }
        append(); p.setPen(QPen(trace, 1)); p.drawPolyline(line);
        p.setPen(QPen(band, 1, Qt::DashLine));
        for (const auto edge : {status_.center - ModemMode::halfBandwidthHz, status_.center + ModemMode::halfBandwidthHz})
            p.drawLine(QPointF(x(edge), spectrum.top()), QPointF(x(edge), spectrum.bottom()));
    }
    if (status_.pilot >= 0 && status_.pilot <= maxHz) {
        p.setPen(QPen(pilot, 1, Qt::DashLine)); p.drawLine(QPointF(x(status_.pilot), spectrum.top()), QPointF(x(status_.pilot), spectrum.bottom()));
        const auto text = QString("Pilot %1 Hz").arg(status_.pilot, 0, 'f', 1);
        const auto textWidth = p.fontMetrics().horizontalAdvance(text) + 8;
        const auto left = std::clamp(x(status_.pilot) + 5, spectrum.left() + 4, spectrum.right() - textWidth);
        const QRectF box(left, spectrum.top() + 3, textWidth, 18);
        p.fillRect(box, QColor(9, 20, 28, 220)); p.setPen(pilot); p.drawText(box, Qt::AlignVCenter, text);
    }
    p.restore();
    if (status_.spectrum.empty()) {
        p.setPen(label); p.drawText(spectrum, Qt::AlignCenter, status_.state.isEmpty() ? QStringLiteral("Warte auf Audiodaten") : status_.state);
    }
    const auto origin = constellation.center();
    const double radius = std::min(constellation.width(), constellation.height()) * .46;
    p.setPen(grid);
    p.drawLine(QPointF(origin.x() - radius, origin.y()), QPointF(origin.x() + radius, origin.y()));
    p.drawLine(QPointF(origin.x(), origin.y() - radius), QPointF(origin.x(), origin.y() + radius));
    p.setPen(label); p.drawText(QPointF(origin.x() + radius - 9, origin.y() - 5), "I"); p.drawText(QPointF(origin.x() + 5, origin.y() - radius + 10), "Q");
    p.save(); p.setClipRect(constellation); p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    const auto color = status_.locked ? pilot : trace;
    for (std::size_t i = 0; i < points_.size(); ++i) {
        auto faded = color; faded.setAlpha(60 + int(190 * (i + 1) / points_.size())); p.setBrush(faded);
        const auto& point = points_[i];
        p.drawEllipse(QPointF(origin.x() + point.x() * radius / 1.5, origin.y() - point.y() * radius / 1.5), 1.15, 1.15);
    }
    p.restore();
    p.setPen(grid); p.drawLine(QPointF(constellation.left() - 8, 8), QPointF(constellation.left() - 8, height() - 8));
}
}
