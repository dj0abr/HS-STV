// SPDX-License-Identifier: GPL-3.0-or-later
#include "beacon_view.hpp"
#include <QFontDatabase>
#include <QGroupBox>
#include <QLabel>
#include <QPlainTextEdit>
#include <QScrollBar>
#include <QSplitter>
#include <QTextDocument>
#include <QVBoxLayout>
#include <algorithm>
namespace mm_sstv {
namespace {
QString plainToolTip(const QString& text) {
    auto escaped = text.toHtmlEscaped(); escaped.replace('\n', "<br>");
    return "<qt>" + escaped + "</qt>";
}
}
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
    bulletin_ = addPane(columns, "Bulletins", "beaconBulletinText");
    bulletin_->setMaximumBlockCount(0);
    bulletin_->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    bulletin_->setPlaceholderText("Warte auf ein vollständiges Bulletin…");
    bulletinStatus_ = new QLabel("Warte auf Bulletin-Dateikopf", bulletin_->parentWidget());
    bulletinStatus_->setObjectName("beaconBulletinStatus"); bulletinStatus_->setTextFormat(Qt::PlainText);
    auto statusFont = bulletinStatus_->font(); statusFont.setPointSizeF(9); bulletinStatus_->setFont(statusFont);
    bulletinStatus_->setWordWrap(true); bulletinStatus_->setMinimumWidth(0);
    bulletinStatus_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    bulletinStatus_->setFixedHeight(bulletinStatus_->fontMetrics().height() * 2 + 2);
    bulletin_->parentWidget()->layout()->addWidget(bulletinStatus_);
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
void BeaconView::showBulletin(const Bulletin& bulletin) {
    if (bulletin.filename == bulletinFilename_ && bulletin.digest == bulletinDigest_) return;
    bulletinFilename_ = bulletin.filename; bulletinDigest_ = bulletin.digest;
    auto text = bulletin.text;
    if (text.size() > 65536) {
        text.truncate(65536); if (!text.isEmpty() && text.at(text.size() - 1).isHighSurrogate()) text.chop(1);
        text += "\n[…] Anzeige gekürzt";
    }
    bulletin_->setPlainText(text); bulletin_->moveCursor(QTextCursor::Start);
    bulletin_->verticalScrollBar()->setValue(0);
    bulletin_->setToolTip(plainToolTip(bulletin.filename + "\n" + bulletin.title + "\nEmpfangen: " + bulletin.receivedAt.toUTC().toString(Qt::ISODate)));
}
void BeaconView::showBulletinProgress(const BulletinProgress& progress) {
    const auto text = progress.message.isEmpty() ? QString("%1 · %2/%3 Blöcke").arg(progress.filename).arg(progress.received).arg(progress.expected) :
                                                 progress.filename + " · " + progress.message;
    bulletinStatus_->setText(text);
    bulletinStatus_->setToolTip(plainToolTip(text));
}
}
