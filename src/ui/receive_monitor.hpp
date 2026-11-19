// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "receiver.hpp"
#include <QPointF>
#include <QWidget>
#include <deque>
namespace mm_sstv {
class ReceiveMonitor final : public QWidget {
public:
    explicit ReceiveMonitor(QWidget* parent = nullptr);
    void setStatus(const ReceiveStatus& status);
    void clear(const QString& message = QStringLiteral("Empfang aus"));
    QSize sizeHint() const override { return {1000, 210}; }
protected:
    void paintEvent(QPaintEvent*) override;
private:
    ReceiveStatus status_;
    std::deque<QPointF> points_;
};
}
