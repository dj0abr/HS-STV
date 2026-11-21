// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "beacon_messages.hpp"
#include <QWidget>
class QPlainTextEdit;
namespace mm_sstv {
class BeaconView final : public QWidget {
public:
    static constexpr int maximumMessages = 1000;
    explicit BeaconView(QWidget* parent = nullptr);
    void appendMessage(const BeaconMessage& message);
private:
    QPlainTextEdit* cw_ = nullptr;
    QPlainTextEdit* dx_ = nullptr;
};
}
