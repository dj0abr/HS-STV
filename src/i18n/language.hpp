// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QObject>
#include <QString>
class QWidget;
namespace mm_sstv {
// Catalog keys are the original German UI text. Protocol keys and user data
// never pass through this API. The embedded catalog needs no external tools.
QString uiText(const char* german);
QString uiMessage(const QString& message);
void retranslateUi(QWidget* root);
class Language final : public QObject {
    Q_OBJECT
public:
    static Language& instance();
    static QString code();
    void setCode(const QString& code);
signals:
    void changed();
private:
    Language();
};
}
