// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QLabel>
#include <QImage>
#include <functional>
namespace mm_sstv {
class ImagePanel final : public QLabel {
public:
    explicit ImagePanel(QWidget* parent = nullptr, bool interactive = true);
    void setImage(QImage image);
    void setEditable(bool editable);
    std::function<void()> clicked;
    std::function<void(QString)> dropped;
protected:
    void resizeEvent(QResizeEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;
private:
    void updateImage();
    QImage image_;
    bool interactive_ = true;
    bool editable_ = true;
};
}
