// SPDX-License-Identifier: GPL-3.0-or-later
// Native Qt equivalent of QO100_TRX2's canvas overlay: three independent layers.
#include "text_overlay.hpp"
#include <QFont>
#include <QFontDatabase>
#include <QPainter>
#include <QSettings>
#include <QTransform>
#include <algorithm>
#include <cmath>
namespace mm_sstv {
namespace {
double position(double value, double fallback) { return std::isfinite(value) ? std::clamp(value, 0., 1.) : fallback; }
QFont layerFont(const OverlayLayer& layer, double scale) {
    QFont font;
    if (layer.font == 2) font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    else {
        font.setFamily(layer.font == 1 ? "serif" : "sans-serif");
        font.setStyleHint(layer.font == 1 ? QFont::Serif : QFont::SansSerif);
    }
    font.setBold(layer.bold); font.setPixelSize(std::max(1, qRound(std::clamp(layer.size, 10, 48) * scale)));
    return font;
}
}
QString TextOverlay::text(unsigned layer) const {
    const auto index = layer < 2 ? layer : unsigned(2 + std::clamp(selected, 0, 4));
    return texts[index].left(layer < 2 ? 24 : 100).trimmed();
}
TextOverlay loadTextOverlay(QSettings& settings) {
    TextOverlay result;
    for (unsigned i = 0; i < result.texts.size(); ++i)
        result.texts[i] = settings.value(QString("overlay/text%1").arg(i)).toString().left(i < 2 ? 24 : 100);
    result.selected = std::clamp(settings.value("overlay/selected", 0).toInt(), 0, 4);
    for (unsigned i = 0; i < result.layers.size(); ++i) {
        auto& layer = result.layers[i]; const auto prefix = QString("overlay/layer%1/").arg(i);
        layer.x = position(settings.value(prefix + "x", layer.x).toDouble(), layer.x);
        layer.y = position(settings.value(prefix + "y", layer.y).toDouble(), layer.y);
        layer.size = std::clamp(settings.value(prefix + "size", layer.size).toInt(), 10, 48);
        layer.font = std::clamp(settings.value(prefix + "font", 0).toInt(), 0, 2);
        layer.bold = settings.value(prefix + "bold", true).toBool();
    }
    return result;
}
void saveTextOverlay(QSettings& settings, const TextOverlay& overlay) {
    for (unsigned i = 0; i < overlay.texts.size(); ++i) settings.setValue(QString("overlay/text%1").arg(i), overlay.texts[i]);
    settings.setValue("overlay/selected", overlay.selected);
    for (unsigned i = 0; i < overlay.layers.size(); ++i) {
        const auto& layer = overlay.layers[i]; const auto prefix = QString("overlay/layer%1/").arg(i);
        settings.setValue(prefix + "x", layer.x); settings.setValue(prefix + "y", layer.y);
        settings.setValue(prefix + "size", layer.size); settings.setValue(prefix + "font", layer.font); settings.setValue(prefix + "bold", layer.bold);
    }
}
std::array<OverlayGeometry, 3> overlayGeometry(const TextOverlay& overlay, QSize target) {
    std::array<OverlayGeometry, 3> result{};
    if (target.isEmpty()) return result;
    const double scale = std::min(target.width()/320., target.height()/256.);
    const double margin = 1 + 1.5 * scale;
    for (unsigned i = 0; i < result.size(); ++i) {
        const auto text = overlay.text(i); if (text.isEmpty()) continue;
        const auto& layer = overlay.layers[i]; QPainterPath path;
        path.addText(0, 0, layerFont(layer, scale), text);
        const auto glyph = path.boundingRect(); if (glyph.isEmpty()) continue;
        // Fit long single-line text to the width without dropping characters.
        path = QTransform::fromTranslate(-glyph.left(), -glyph.top()).map(path);
        const double horizontal = std::min(1., std::max(0., target.width() - 2*margin) / glyph.width());
        path = QTransform::fromScale(horizontal, 1).map(path);
        const QSizeF size(path.boundingRect().width() + 2*margin, glyph.height() + 2*margin);
        const double x = std::clamp(position(layer.x, 0) * target.width(), 0., std::max(0., target.width() - size.width()));
        const double y = std::clamp(position(layer.y, 0) * target.height(), 0., std::max(0., target.height() - size.height()));
        result[i].bounds = QRectF(QPointF(x, y), size).intersected(QRectF(QPointF(0, 0), QSizeF(target)));
        result[i].path = QTransform::fromTranslate(x + margin, y + margin).map(path);
    }
    return result;
}
QImage drawTextOverlay(QImage image, const TextOverlay& overlay) {
    // Keep the empty/default path unchanged, including headless offline tools.
    if (image.isNull() || (overlay.text(0).isEmpty() && overlay.text(1).isEmpty() && overlay.text(2).isEmpty())) return image;
    const auto geometry = overlayGeometry(overlay, image.size()); QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing); painter.setRenderHint(QPainter::TextAntialiasing);
    const double scale = std::min(image.width()/320., image.height()/256.);
    const QPen outline(Qt::black, 3*scale, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    // Match the original canvas: contour first, then the full white glyph.
    for (const auto& item : geometry) {
        painter.strokePath(item.path, outline); painter.fillPath(item.path, QBrush(Qt::white));
    }
    return image;
}
}
