// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "receiver.hpp"
#include "mm_sstv/waterfall_auto_level.hpp"
#include <QPointF>
#include <QWidget>
#include <deque>
namespace mm_sstv {
class ReceiveMonitor final : public QWidget {
public:
    explicit ReceiveMonitor(QWidget* parent = nullptr);
    void setStatus(const ReceiveStatus& status);
    void setDbRange(int minimum, int maximum);
    void setLevelAdjustments(int minimum, int maximum);
    int minimumAdjustment() const { return minAdjustment_; }
    int maximumAdjustment() const { return maxAdjustment_; }
    bool autoLevel() const { return isSstv(status_.modulation); }
    int minimumDb() const { return minDb_; }
    int maximumDb() const { return maxDb_; }
    void clear(const QString& message = QStringLiteral("Warte auf Audiodaten"), Modulation mode = Modulation::Apsk8);
    QSize sizeHint() const override { return {720, isSstv(status_.modulation) ? 164 : 120}; }
protected:
    void paintEvent(QPaintEvent*) override;
private:
    void applyAutoLevels();
    ReceiveStatus status_;
    int minDb_ = -90, maxDb_ = -20;
    std::deque<QPointF> points_;
    std::deque<std::vector<float>> waterfall_;
    double waterfallBinHz_ = 0;
    int minAdjustment_ = 0, maxAdjustment_ = 0;
    int automaticMinDb_ = -90, automaticMaxDb_ = -20;
    WaterfallAutoLevel autoLevel_;
};
}
