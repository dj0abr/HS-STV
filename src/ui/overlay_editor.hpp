// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "text_overlay.hpp"
#include <QDialog>
#include <QWidget>
#include <functional>
class QComboBox;
class QSpinBox;
class QCheckBox;
class QLineEdit;
class QGroupBox;
namespace mm_sstv {
class OverlayPreview final : public QWidget {
public:
    OverlayPreview(QImage base, TextOverlay& overlay, QWidget* parent = nullptr);
    void selectLayer(int index);
    QRectF imageRect() const;
    std::function<void(int)> selected;
protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void keyPressEvent(QKeyEvent*) override;
private:
    QPointF imagePoint(QPointF position) const;
    void moveLayer(QPointF topLeft);
    QImage base_;
    TextOverlay& overlay_;
    int layer_ = 0;
    bool dragging_ = false;
    QPointF dragOffset_;
};
class OverlayEditor final : public QDialog {
public:
    OverlayEditor(QImage base, TextOverlay overlay, QWidget* parent = nullptr);
    const TextOverlay& overlay() const { return overlay_; }
protected:
    bool eventFilter(QObject* object, QEvent* event) override;
private:
    void selectField(unsigned index);
    void selectLayer(int index);
    TextOverlay overlay_;
    OverlayPreview* preview_ = nullptr;
    std::array<QLineEdit*, 7> fields_{};
    QComboBox* layer_ = nullptr;
    QComboBox* font_ = nullptr;
    QSpinBox* size_ = nullptr;
    QCheckBox* bold_ = nullptr;
    QGroupBox* textsGroup_ = nullptr;
};
}
