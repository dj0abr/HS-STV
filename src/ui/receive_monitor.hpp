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
    void setDbRange(int minimum, int maximum);
    int minimumDb() const { return minDb_; }
    int maximumDb() const { return maxDb_; }
    void clear(const QString& message = QStringLiteral("Warte auf Audiodaten"), Modulation mode = Modulation::Apsk8);
    QSize sizeHint() const override { return {720, isSstv(status_.modulation) ? 164 : 120}; }
protected:
    void paintEvent(QPaintEvent*) override;
private:
    ReceiveStatus status_;
    int minDb_ = -90, maxDb_ = -20;
    std::deque<QPointF> points_;
    std::deque<std::vector<float>> waterfall_;
    double waterfallBinHz_ = 0;
};
}
