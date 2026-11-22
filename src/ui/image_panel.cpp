// SPDX-License-Identifier: GPL-3.0-or-later
#include "language.hpp"
#include "image_panel.hpp"
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QMouseEvent>
#include <QUrl>
namespace mm_sstv {
ImagePanel::ImagePanel(QWidget* parent, bool interactive) : QLabel(parent), interactive_(interactive) {
    setAcceptDrops(interactive); setAlignment(Qt::AlignCenter); setMinimumSize(160, 90);
    setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    setStyleSheet("QLabel { background: #e9edf0; border: 1px solid #aab4bc; border-radius: 4px; padding: 0px; }");
    setMargin(3); updateImage(); setEditable(true);
    connect(&Language::instance(), &Language::changed, this, [this] { if (image_.isNull()) updateImage(); });
}
void ImagePanel::setEditable(bool editable) {
    editable_ = editable; setAcceptDrops(interactive_ && editable_);
    setCursor(interactive_ && editable_ ? Qt::PointingHandCursor : Qt::ArrowCursor);
}
void ImagePanel::setImage(QImage image) { image_ = std::move(image); updateImage(); }
void ImagePanel::updateImage() {
    if (image_.isNull()) { clear(); setText(interactive_ ? uiText("Bild hier ablegen\noder zum Öffnen anklicken") : uiText("Warte auf Bildempfang")); }
    else setPixmap(QPixmap::fromImage(image_).scaled(contentsRect().size() - QSize(6, 6), Qt::KeepAspectRatio, Qt::SmoothTransformation));
}
void ImagePanel::resizeEvent(QResizeEvent* event) { QLabel::resizeEvent(event); updateImage(); }
void ImagePanel::mousePressEvent(QMouseEvent* event) {
    if (interactive_ && editable_ && event->button() == Qt::LeftButton && isEnabled() && clicked) clicked();
    else QLabel::mousePressEvent(event);
}
void ImagePanel::dragEnterEvent(QDragEnterEvent* event) {
    if (interactive_ && editable_ && isEnabled() && event->mimeData()->hasUrls() && event->mimeData()->urls().size() == 1 && event->mimeData()->urls().first().isLocalFile())
        event->acceptProposedAction();
}
void ImagePanel::dropEvent(QDropEvent* event) {
    if (interactive_ && editable_ && isEnabled() && event->mimeData()->hasUrls() && event->mimeData()->urls().size() == 1 && event->mimeData()->urls().first().isLocalFile() && dropped) {
        dropped(event->mimeData()->urls().first().toLocalFile()); event->acceptProposedAction();
    }
}
}
