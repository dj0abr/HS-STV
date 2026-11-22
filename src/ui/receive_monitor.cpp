// SPDX-License-Identifier: GPL-3.0-or-later
#include "receive_monitor.hpp"
#include "mm_sstv/modem_mode.hpp"
#include "mm_sstv/audio_spectrum.hpp"
#include <QFontDatabase>
#include <QPainter>
#include <QImage>
#include <QPolygonF>
#include <algorithm>
#include <cmath>
namespace mm_sstv {
namespace {
const QColor background("#09141c"), grid("#243b48"), label("#97b0bf"), trace("#69d4e9"), pilot("#70efb5"), band("#e6bd6a");
// Explicit display limits keep low-frequency filter rolloff from affecting the scale.
constexpr double maxHz = 4000;
}
ReceiveMonitor::ReceiveMonitor(QWidget* parent) : QWidget(parent) {
    setObjectName("receiveMonitor"); setMinimumSize(300, 96); setMaximumHeight(144);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    setToolTip("Empfangsaudio vor AFC und AGC · 0–4000 Hz\nGrün: erkannter Pilot · Fläche: Pilotsuche 20–900 Hz\nGelb: Datenband der aktuellen Empfangsmitte\nKonstellation: tatsächlich empfangene I/Q-Symbole nach dem Symboltracking");
    clear();
}
void ReceiveMonitor::clear(const QString& message, Modulation mode) {
    setMaximumHeight(isSstv(mode) ? 180 : 144); setMinimumHeight(isSstv(mode) ? 144 : 96);
    setToolTip(isPskBeacon(mode) ? "PSB-Bake · Empfangsaudio 0–4000 Hz\nBPSK/Manchester · 400 bit/s · Signalmitte etwa 1500 Hz, Trägersuche ±500 Hz.\nGelbe Linien: Datenband; kein separater Pilotton." : isSstv(mode) ? "SSTV-Spektrum und Wasserfall · 0–4000 Hz\nWasserfall: neueste Werte oben · 6 Sekunden Verlauf · 23,4 Hz Frequenzauflösung · 43-ms-Analysefenster\nFarben: an und unter Min schwarz · an und über Max rot\nSync: 1200 Hz · Bildsignal: 1500–2300 Hz (schattiert)\nHilfslinien zeigen die Sollfrequenzen vor AFC; Frequenz am Funkgerät fein abstimmen." :
        "Empfangsaudio vor AFC und AGC · 0–4000 Hz\nGrün: erkannter Pilot · Fläche: Pilotsuche 20–900 Hz\nGelb: Datenband · Konstellation: empfangene I/Q-Symbole");
    status_ = {}; status_.modulation = mode; status_.state = message; points_.clear(); waterfall_.clear(); waterfallBinHz_ = 0;
    automaticMinDb_ = -90; automaticMaxDb_ = -20; autoLevel_.reset();
    if (isSstv(mode)) applyAutoLevels(); else setDbRange(minDb_, maxDb_);
    updateGeometry(); update();
}
void ReceiveMonitor::setDbRange(int minimum, int maximum) {
    const bool analog = isSstv(status_.modulation);
    minDb_ = std::clamp(minimum, analog ? -140 : -100, analog ? -10 : -60);
    maxDb_ = std::max(minDb_ + 10, std::clamp(maximum, analog ? -120 : -50, 0));
    update();
}
void ReceiveMonitor::setLevelAdjustments(int minimum, int maximum) {
    minAdjustment_ = std::clamp(minimum, -6, 6); maxAdjustment_ = std::clamp(maximum, -6, 6);
    if (isSstv(status_.modulation)) applyAutoLevels();
}
void ReceiveMonitor::applyAutoLevels() {
    // Apply user corrections only to the display, never to estimator history.
    setDbRange(automaticMinDb_ + minAdjustment_, automaticMaxDb_ + maxAdjustment_);
}
void ReceiveMonitor::setStatus(const ReceiveStatus& status) {
    if (status.modulation != status_.modulation || status.locked != status_.locked || std::abs(status.center - status_.center) > 20 || status.points.empty()) points_.clear();
    if (status.modulation != status_.modulation || (status.spectrum.empty() && status.waterfall.empty()) ||
        (!status.waterfall.empty() && (status.waterfallBinHz != waterfallBinHz_ || status.spectrumBinHz != status_.spectrumBinHz))) {
        waterfall_.clear(); autoLevel_.reset();
    }
    if (!status.waterfall.empty()) {
        waterfallBinHz_ = status.waterfallBinHz;
        for (const auto& row : status.waterfall) waterfall_.push_back(row);
        while (waterfall_.size() > 600) waterfall_.pop_front();
    }
    status_ = status;
    if (isSstv(status.modulation) && status.waterfallBinHz > 0) {
        const double referenceBinHz = status.spectrumBinHz > 0 ? status.spectrumBinHz : AudioSpectrum::binHz;
        const double bandwidthDb = 10 * std::log10(status.waterfallBinHz / referenceBinHz);
        const int rows = std::max(1, int((height() - 44) * .58 - 4));
        if (autoLevel_.update(status.waterfall, status.waterfallBinHz, bandwidthDb, status.offset,
                              std::size_t((600 + rows - 1) / rows))) {
            automaticMinDb_ = autoLevel_.minimumDb(); automaticMaxDb_ = autoLevel_.maximumDb();
            applyAutoLevels();
        }
    }
    for (const auto& point : status.points) points_.emplace_back(point[0], point[1]);
    while (points_.size() > 600) points_.pop_front();
    update();
}
void ReceiveMonitor::paintEvent(QPaintEvent*) {
    QPainter p(this); p.fillRect(rect(), background);
    auto font = QFontDatabase::systemFont(QFontDatabase::FixedFont); font.setPointSize(8); p.setFont(font);
    const bool analog = isSstv(status_.modulation);
    const double minDb = minDb_, maxDb = maxDb_;
    const bool psk = isPskBeacon(status_.modulation);
    const double constellationWidth = (analog || psk) ? 0 : std::min({height() + 6.0, 160.0, width() * .28});
    const double plotHeight = height() - 44;
    const QRectF spectrum(34, 24, width() - constellationWidth - 48, analog ? plotHeight * .42 : plotHeight);
    const QRectF waterfall(spectrum.left(), spectrum.bottom() + 4, spectrum.width(), analog ? plotHeight - spectrum.height() - 4 : 0);
    const double frequencyLabelTop = analog ? waterfall.bottom() + 3 : spectrum.bottom() + 3;
    const QRectF constellation(width() - constellationWidth, 22, constellationWidth - 8, height() - 28);
    const auto x = [&](double hz) { return spectrum.left() + hz / maxHz * spectrum.width(); };
    const auto y = [&](double db) { return spectrum.top() + std::clamp((maxDb - db) / (maxDb - minDb), 0.0, 1.0) * spectrum.height(); };
    p.setPen(label); p.drawText(QRectF(8, 4, spectrum.right() - 8, 20), Qt::AlignLeft | Qt::AlignVCenter,
        analog ? QString("SSTV %1 · AFC %2 Hz").arg(modulationName(status_.modulation)).arg(status_.offset, 0, 'f', 1) : QStringLiteral("Empfangsaudio · dBFS/Bin"));
    if (!status_.spectrum.empty())
        p.drawText(QRectF(8, 4, spectrum.right() - 8, 20), Qt::AlignRight | Qt::AlignVCenter, QString("%1 dBFS").arg(status_.level, 0, 'f', 1));
    if (analog) {
        p.fillRect(QRectF(x(1500), spectrum.top(), x(2300) - x(1500), spectrum.height()), QColor(20, 49, 49));
    } else if (!psk) {
        p.drawText(QRectF(constellation.left() + 4, 4, constellation.width(), 20), Qt::AlignLeft | Qt::AlignVCenter, QString::fromLatin1(modulationName(status_.modulation)));
        p.fillRect(QRectF(x(20), spectrum.top(), x(900) - x(20), spectrum.height()), QColor(20, 49, 49));
    }
    std::vector<int> ticks{maxDb_};
    const double labelGap = p.fontMetrics().height() + 1;
    for (int db = int(std::floor((maxDb - 1) / 20)) * 20; db > minDb_; db -= 20) {
        if (y(db) - y(ticks.back()) >= labelGap && y(minDb) - y(db) >= labelGap) ticks.push_back(db);
    }
    ticks.push_back(minDb_);
    for (const int db : ticks) {
        p.setPen(grid); p.drawLine(QPointF(spectrum.left(), y(db)), QPointF(spectrum.right(), y(db)));
        p.setPen(label); p.drawText(QRectF(0, y(db) - 8, 28, 16), Qt::AlignRight | Qt::AlignVCenter, QString::number(db));
    }
    for (int hz = 0; hz <= 4000; hz += 1000) {
        p.setPen(grid); p.drawLine(QPointF(x(hz), spectrum.top()), QPointF(x(hz), spectrum.bottom()));
        p.setPen(label);
        if (hz == 4000) p.drawText(QRectF(spectrum.right() - 68, frequencyLabelTop, 68, 18), Qt::AlignRight | Qt::AlignVCenter, "4000 Hz");
        else if (hz == 0) p.drawText(QRectF(spectrum.left(), frequencyLabelTop, 30, 18), Qt::AlignLeft | Qt::AlignVCenter, "0");
        else p.drawText(QRectF(x(hz) - 22, frequencyLabelTop, 44, 18), Qt::AlignHCenter | Qt::AlignVCenter, QString::number(hz));
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
        if (!analog) for (const auto edge : {status_.center - (psk ? 800 : parameters(status_.modulation).halfBandwidthHz()), status_.center + (psk ? 800 : parameters(status_.modulation).halfBandwidthHz())})
            p.drawLine(QPointF(x(edge), spectrum.top()), QPointF(x(edge), spectrum.bottom()));
    }
    if (analog) {
        for (const int hz : {1200, 1500, 2300}) {
            p.setPen(QPen(hz == 1200 ? pilot : band, 1, Qt::DashLine));
            p.drawLine(QPointF(x(hz), spectrum.top()), QPointF(x(hz), spectrum.bottom()));
            const QString text = hz == 1200 ? "Sync 1200" : QString::number(hz);
            const int textWidth = p.fontMetrics().horizontalAdvance(text) + 6;
            // Put the sync label left of its line to leave room for 1500 Hz.
            const QRectF box(hz == 1200 ? x(hz) - textWidth - 3 : x(hz) + 3, spectrum.top() + 3, textWidth, 16);
            p.fillRect(box, QColor(9, 20, 28, 220)); p.drawText(box, Qt::AlignVCenter, text);
        }
    }
    if (!analog && status_.pilot >= 0 && status_.pilot <= maxHz) {
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
    if (analog) {
        if (!waterfall_.empty() && waterfallBinHz_ > 0) {
            const int bins = int(waterfall_.back().size()), rows = std::max(1, int(waterfall.height()));
            QImage image(bins, rows, QImage::Format_RGB32);
            // Short FFT windows collect more broadband noise in each wider bin.
            // Refer the colors to the spectrum's bin bandwidth so its noise floor
            // and the waterfall respond consistently to the shared Min slider.
            const double referenceBinHz = status_.spectrumBinHz > 0 ? status_.spectrumBinHz : AudioSpectrum::binHz;
            const double bandwidthDb = 10 * std::log10(waterfallBinHz_ / referenceBinHz);
            for (int row = 0; row < rows; ++row) {
                const std::size_t first = std::size_t(row) * 600 / rows;
                const std::size_t last = std::min(waterfall_.size(), (std::size_t(row + 1) * 600 + rows - 1) / rows);
                for (int bin = 0; bin < bins; ++bin) {
                    float peak = minDb;
                    // Peak reduction keeps a 5-ms sync pulse visible when time rows share a pixel.
                    for (auto at = first; at < last; ++at) {
                        const auto& history = waterfall_[waterfall_.size() - 1 - at];
                        if (std::size_t(bin) < history.size()) peak = std::max(peak, float(history[bin] - bandwidthDb));
                    }
                    const double level = std::clamp((peak - minDb) / (maxDb - minDb), 0., 1.);
                    const auto color = QColor::fromHsvF((1 - level) * .67, 1., std::min(1., level * 2));
                    image.setPixel(bin, row, color.rgb());
                }
            }
            p.drawImage(waterfall, image, QRectF(.5, 0, maxHz / waterfallBinHz_, rows));
        }
        p.setPen(grid); p.drawRect(waterfall);
        for (const int hz : {1200, 1500, 2300}) {
            auto color = hz == 1200 ? pilot : band; color.setAlpha(120);
            p.setPen(QPen(color, 1, Qt::DashLine));
            p.drawLine(QPointF(x(hz), waterfall.top()), QPointF(x(hz), waterfall.bottom()));
        }
        p.setPen(label);
        p.drawText(QRectF(0, waterfall.top(), 30, 14), Qt::AlignRight | Qt::AlignVCenter, "0 s");
        p.drawText(QRectF(0, waterfall.bottom() - 14, 30, 14), Qt::AlignRight | Qt::AlignVCenter, "−6 s");
        return;
    }
    if (psk) return;
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
