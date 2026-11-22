// SPDX-License-Identifier: GPL-3.0-or-later
#include "theme.hpp"
#include <QApplication>
#include <QColor>
#include <QFile>
#include <QFont>
#include <QPalette>
#include <QStyleFactory>
#include <stdexcept>

static void initializeTheme() { Q_INIT_RESOURCE(theme); }

namespace mm_sstv {
void applyDarkTheme(QApplication& app) {
    initializeTheme();
    app.setStyle(QStyleFactory::create("Fusion"));
    QPalette palette;
    palette.setColor(QPalette::Window, QColor("#0c1016"));
    palette.setColor(QPalette::WindowText, QColor("#e2e9f1"));
    palette.setColor(QPalette::Base, QColor("#10161e"));
    palette.setColor(QPalette::AlternateBase, QColor("#18212b"));
    palette.setColor(QPalette::Text, QColor("#e2e9f1"));
    palette.setColor(QPalette::Button, QColor("#232d39"));
    palette.setColor(QPalette::ButtonText, QColor("#e2e9f1"));
    palette.setColor(QPalette::BrightText, QColor("#ffffff"));
    palette.setColor(QPalette::Highlight, QColor("#285b78"));
    palette.setColor(QPalette::HighlightedText, QColor("#ffffff"));
    palette.setColor(QPalette::Link, QColor("#69c9f0"));
    palette.setColor(QPalette::LinkVisited, QColor("#b6a4ef"));
    palette.setColor(QPalette::ToolTipBase, QColor("#253241"));
    palette.setColor(QPalette::ToolTipText, QColor("#eef4fa"));
    palette.setColor(QPalette::Light, QColor("#3c4b5c"));
    palette.setColor(QPalette::Midlight, QColor("#2d3947"));
    palette.setColor(QPalette::Mid, QColor("#26313e"));
    palette.setColor(QPalette::Dark, QColor("#090d12"));
    palette.setColor(QPalette::Shadow, QColor("#05080c"));
    palette.setColor(QPalette::PlaceholderText, QColor("#8595a9"));
    for (const auto role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText})
        palette.setColor(QPalette::Disabled, role, QColor("#718094"));
    palette.setColor(QPalette::Disabled, QPalette::Highlight, QColor("#2b3a4a"));
    palette.setColor(QPalette::Disabled, QPalette::HighlightedText, QColor("#a7b6c7"));
    app.setPalette(palette);
    // Point sizes follow screen scaling on both platforms; no bundled font needed.
    auto font = app.font(); font.setPointSizeF(10); app.setFont(font);

    QFile style(":/theme/dark.qss");
    if (!style.open(QIODevice::ReadOnly)) throw std::runtime_error("Cannot load embedded GUI theme");
    app.setStyleSheet(QString::fromUtf8(style.readAll()));
}
}
