// SPDX-License-Identifier: GPL-3.0-or-later
#include "beacon_view.hpp"
#include <QFontDatabase>
#include <QGroupBox>
#include <QPlainTextEdit>
#include <QScrollBar>
#include <QSplitter>
#include <QTextDocument>
#include <QVBoxLayout>
#include <algorithm>
namespace mm_sstv {
BeaconView::BeaconView(QWidget* parent) : QWidget(parent) {
    setObjectName("beaconPage");
    auto* layout = new QVBoxLayout(this); layout->setContentsMargins(0, 0, 0, 0);
    auto* columns = new QSplitter(Qt::Horizontal, this); columns->setObjectName("beaconColumns");
    columns->setChildrenCollapsible(false);
    auto* spots = new QSplitter(Qt::Vertical, columns); spots->setObjectName("beaconSpots");
    spots->setChildrenCollapsible(false);
    const auto addPane = [](QSplitter* splitter, const QString& title, const QString& name) {
        auto* group = new QGroupBox(title, splitter); auto* column = new QVBoxLayout(group);
        column->setContentsMargins(6, 6, 6, 6);
        auto* view = new QPlainTextEdit(group); view->setObjectName(name);
        view->setReadOnly(true); view->setUndoRedoEnabled(false);
        auto font = QFontDatabase::systemFont(QFontDatabase::FixedFont); font.setPointSizeF(9);
        view->setFont(font);
        view->setLineWrapMode(QPlainTextEdit::NoWrap); view->setMinimumSize(0, 40);
        view->setMaximumBlockCount(maximumMessages);
        column->addWidget(view); splitter->addWidget(group); return view;
    };
    cw_ = addPane(spots, "CW-Skimmer", "beaconCwText");
    dx_ = addPane(spots, "DX-Cluster", "beaconDxText");
    for (auto* view : {cw_, dx_}) {
        view->setPlaceholderText("Warte auf Meldungen der MM-Bake…");
        view->setToolTip("Empfangszeit in UTC · bis zu 1000 Meldungen\n"
            "Gleiche Meldungen innerhalb von zwei Minuten werden ausgeblendet.\n"
            "Zum Lesen zurückscrollen; neue Meldungen versetzen die Ansicht dann nicht.");
    }
    auto* bulletin = addPane(columns, "Bulletins", "beaconBulletinText");
    bulletin->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    bulletin->setPlaceholderText("Bulletin-Empfang folgt in der nächsten Teststufe.");
    columns->setSizes({550, 350}); spots->setSizes({180, 180}); layout->addWidget(columns);
}
void BeaconView::appendMessage(const BeaconMessage& message) {
    auto* view = message.channel == BeaconChannel::CwSkimmer ? cw_ : dx_;
    auto* scroll = view->verticalScrollBar();
    const bool follow = scroll->value() >= scroll->maximum() - 1 && !view->textCursor().hasSelection();
    const int oldPosition = scroll->value(), horizontal = view->horizontalScrollBar()->value();
    const int before = view->document()->isEmpty() ? 0 : view->document()->blockCount();
    view->appendPlainText(message.receivedAt.toUTC().toString("HH:mm:ss'Z'") + "  " + message.text);
    if (follow) scroll->setValue(scroll->maximum());
    else {
        const int removed = std::max(0, before + 1 - view->document()->blockCount());
        scroll->setValue(std::max(scroll->minimum(), oldPosition - removed));
    }
    view->horizontalScrollBar()->setValue(horizontal);
}
}
