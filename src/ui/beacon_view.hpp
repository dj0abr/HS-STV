// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "beacon_messages.hpp"
#include "bulletin_receiver.hpp"
#include <QWidget>
class QPlainTextEdit;
class QLabel;
namespace mm_sstv {
class BeaconView final : public QWidget {
public:
    static constexpr int maximumMessages = 1000;
    explicit BeaconView(QWidget* parent = nullptr);
    void appendMessage(const BeaconMessage& message);
    void showBulletin(const Bulletin& bulletin);
    void showBulletinProgress(const BulletinProgress& progress);
private:
    QPlainTextEdit* cw_ = nullptr;
    QPlainTextEdit* dx_ = nullptr;
    QPlainTextEdit* bulletin_ = nullptr;
    QLabel* bulletinStatus_ = nullptr;
    QByteArray bulletinDigest_;
    QString bulletinFilename_;
};
}
