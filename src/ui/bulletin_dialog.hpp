// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "bulletin_receiver.hpp"
#include <QDialog>
#include <functional>
#include <optional>
class QComboBox;
class QLabel;
class QPlainTextEdit;
namespace mm_sstv {
class BulletinDialog final : public QDialog {
public:
    explicit BulletinDialog(std::optional<Bulletin> current, QWidget* parent = nullptr);
    void setEntries(const std::vector<BulletinArchiveEntry>& entries);
    void showArchived(const QString& id, const Bulletin& bulletin);
    void showError(const QString& id, const QString& message);
    std::function<void(const QString&)> loadRequested;
private:
    void select();
    void display(const Bulletin& bulletin);
    std::optional<Bulletin> current_;
    QComboBox* choices_ = nullptr;
    QPlainTextEdit* text_ = nullptr;
    QLabel* info_ = nullptr;
    QString displayedId_;
    QString displayedInfo_;
};
}
