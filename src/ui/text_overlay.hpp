// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QImage>
#include <QPainterPath>
#include <QRectF>
#include <QString>
#include <array>
class QSettings;
namespace mm_sstv {
struct OverlayLayer {
    double x = 12. / 320, y = 12. / 256;
    int size = 28, font = 0; // Pixel size at 320x256; sans/serif/monospace.
    bool bold = true;
};
struct TextOverlay {
    std::array<QString, 7> texts{}; // Own call, other call, five alternative texts.
    std::array<OverlayLayer, 3> layers{{
        {12./320, 12./256, 28, 0, true},
        {12./320, 80./256, 24, 0, true},
        {12./320, 218./256, 24, 0, true}}};
    int selected = 0;
    QString text(unsigned layer) const;
};
struct OverlayGeometry { QPainterPath path; QRectF bounds; };
TextOverlay loadTextOverlay(QSettings& settings);
void saveTextOverlay(QSettings& settings, const TextOverlay& overlay);
std::array<OverlayGeometry, 3> overlayGeometry(const TextOverlay& overlay, QSize target);
QImage drawTextOverlay(QImage image, const TextOverlay& overlay);
}
