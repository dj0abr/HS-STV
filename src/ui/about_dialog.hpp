// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QDialog>
namespace mm_sstv {
class AboutDialog final : public QDialog {
public:
    explicit AboutDialog(QWidget* parent = nullptr);
};
}
