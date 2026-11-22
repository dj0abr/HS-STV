// SPDX-License-Identifier: GPL-3.0-or-later
#include "language.hpp"
#include "beacon_view.hpp"
#include <QFontDatabase>
#include <QApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QFrame>
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
    const auto addPane = [](QSplitter* splitter, const QString& title, const QString& name, QLabel** heading = nullptr) {
        auto* pane = new QFrame(splitter); pane->setProperty("panel", true);
        auto* rows = new QVBoxLayout(pane); rows->setContentsMargins(0, 0, 0, 0); rows->setSpacing(0);
        // Separate headings keep their full contents independent of the frame painter.
        auto* caption = new QLabel(title, pane); caption->setProperty("panelHeading", true);
        caption->setTextFormat(Qt::PlainText); caption->setContentsMargins(12, 8, 16, 8);
        caption->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        if (heading) { *heading = caption; caption->setObjectName("beaconBulletinHeading"); }
        rows->addWidget(caption);
        auto* group = new QGroupBox(pane); group->setProperty("panelBody", true); rows->addWidget(group, 1);
        auto* column = new QVBoxLayout(group);
        column->setContentsMargins(8, 8, 8, 8);
        auto* view = new QPlainTextEdit(group); view->setObjectName(name);
        view->setReadOnly(true); view->setUndoRedoEnabled(false);
        auto font = QFontDatabase::systemFont(QFontDatabase::FixedFont); font.setPointSizeF(9);
        view->setFont(font);
        view->setLineWrapMode(QPlainTextEdit::NoWrap); view->setMinimumSize(0, 40);
        view->setMaximumBlockCount(maximumMessages);
        column->addWidget(view); splitter->addWidget(pane); return view;
    };
    cw_ = addPane(spots, "CW-Skimmer", "beaconCwText");
    dx_ = addPane(spots, "DX-Cluster", "beaconDxText");
    for (auto* view : {cw_, dx_}) {
        view->setPlaceholderText(uiText("Warte auf Meldungen der MM-Bake…"));
        view->setToolTip(uiText("Empfangszeit in UTC · bis zu 1000 Meldungen\n"
            "Gleiche Meldungen innerhalb von zwei Minuten werden ausgeblendet.\n"
            "Zum Lesen zurückscrollen; neue Meldungen versetzen die Ansicht dann nicht."));
    }
    bulletin_ = addPane(columns, uiText("Bulletins – zum Vergrößern anklicken"), "beaconBulletinText", &bulletinHeading_);
    bulletinHeading_->installEventFilter(this); bulletinHeading_->setCursor(Qt::PointingHandCursor);
    updateBulletinWidth();
    bulletin_->setMaximumBlockCount(0);
    bulletin_->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    bulletin_->setPlaceholderText(uiText("Warte auf ein vollständiges Bulletin…"));
    bulletin_->setAccessibleName(uiText("Bulletin · zum Lesen und zur Archivauswahl öffnen"));
    bulletin_->installEventFilter(this); bulletin_->viewport()->installEventFilter(this);
    bulletin_->viewport()->setCursor(Qt::PointingHandCursor);
    bulletinStatus_ = new QLabel(uiText("Warte auf Bulletin-Dateikopf"), bulletin_->parentWidget());
    bulletinStatus_->setObjectName("beaconBulletinStatus"); bulletinStatus_->setTextFormat(Qt::PlainText);
    auto statusFont = bulletinStatus_->font(); statusFont.setPointSizeF(9); bulletinStatus_->setFont(statusFont);
    bulletinStatus_->setWordWrap(true); bulletinStatus_->setMinimumWidth(0);
    bulletinStatus_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    bulletinStatus_->setFixedHeight(bulletinStatus_->fontMetrics().height() * 2 + 2);
    bulletin_->parentWidget()->layout()->addWidget(bulletinStatus_);
    connect(&Language::instance(), &Language::changed, this, [this] {
        retranslateUi(this);
        updateBulletinWidth();
        if (currentBulletin_) {
            bulletin_->setToolTip(plainToolTip(uiText("%1\n%2\nEmpfangen: %3\nKlicken: vollständig lesen und gespeicherte Bulletins auswählen")
                .arg(currentBulletin_->filename, currentBulletin_->title, currentBulletin_->receivedAt.toUTC().toString(Qt::ISODate))));
            if (currentBulletin_->text.size() > 65536) {
                const int position = bulletin_->textCursor().position(), anchor = bulletin_->textCursor().anchor();
                const int scroll = bulletin_->verticalScrollBar()->value();
                const auto current = *currentBulletin_; bulletinDigest_.clear(); showBulletin(current);
                auto cursor = bulletin_->textCursor(); const int last = bulletin_->document()->characterCount() - 1;
                cursor.setPosition(std::min(anchor, last)); cursor.setPosition(std::min(position, last), QTextCursor::KeepAnchor);
                bulletin_->setTextCursor(cursor); bulletin_->verticalScrollBar()->setValue(scroll);
            }
        }
    });
    columns->setSizes({550, 350}); spots->setSizes({180, 180}); layout->addWidget(columns);
}
void BeaconView::updateBulletinWidth() {
    if (!bulletinHeading_) return;
    const auto metrics = bulletinHeading_->fontMetrics();
    const auto title = bulletinHeading_->text();
    const int titleWidth = std::max(metrics.horizontalAdvance(title), metrics.boundingRect(title).width());
    bulletinHeading_->parentWidget()->setMinimumWidth(titleWidth + 40);
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
    currentBulletin_ = bulletin;
    if (bulletin.filename == bulletinFilename_ && bulletin.digest == bulletinDigest_) return;
    bulletinFilename_ = bulletin.filename; bulletinDigest_ = bulletin.digest;
    auto text = bulletin.text;
    if (text.size() > 65536) {
        text.truncate(65536); if (!text.isEmpty() && text.at(text.size() - 1).isHighSurrogate()) text.chop(1);
        text += uiText("\n[…] Anzeige gekürzt");
    }
    bulletin_->setPlainText(text); bulletin_->moveCursor(QTextCursor::Start);
    bulletin_->verticalScrollBar()->setValue(0);
    bulletin_->setToolTip(plainToolTip(uiText("%1\n%2\nEmpfangen: %3\nKlicken: vollständig lesen und gespeicherte Bulletins auswählen")
        .arg(bulletin.filename, bulletin.title, bulletin.receivedAt.toUTC().toString(Qt::ISODate))));
}
void BeaconView::showBulletinProgress(const BulletinProgress& progress) {
    const auto text = progress.message.isEmpty() ? uiText("%1 · %2/%3 Blöcke").arg(progress.filename).arg(progress.received).arg(progress.expected) :
                                                 progress.filename + " · " + uiMessage(progress.message);
    bulletinStatus_->setText(text);
    bulletinStatus_->setToolTip(plainToolTip(text));
}
bool BeaconView::eventFilter(QObject* watched, QEvent* event) {
    if (watched == bulletinHeading_ && (event->type() == QEvent::FontChange || event->type() == QEvent::ApplicationFontChange ||
        event->type() == QEvent::StyleChange || event->type() == QEvent::Polish)) updateBulletinWidth();
    if (watched == bulletin_->viewport() || watched == bulletinHeading_) {
        if (event->type() == QEvent::MouseButtonPress) {
            const auto* mouse = static_cast<QMouseEvent*>(event);
            bulletinPressed_ = mouse->button() == Qt::LeftButton; bulletinPress_ = mouse->pos();
        } else if (event->type() == QEvent::MouseButtonRelease) {
            const auto* mouse = static_cast<QMouseEvent*>(event);
            const bool clicked = bulletinPressed_ && mouse->button() == Qt::LeftButton &&
                (mouse->pos() - bulletinPress_).manhattanLength() < QApplication::startDragDistance();
            bulletinPressed_ = false; if (clicked && archiveRequested) archiveRequested();
        }
    } else if (watched == bulletin_ && event->type() == QEvent::KeyPress) {
        const int key = static_cast<QKeyEvent*>(event)->key();
        if (key == Qt::Key_Return || key == Qt::Key_Enter || key == Qt::Key_Space) { if (archiveRequested) archiveRequested(); return true; }
    }
    return QWidget::eventFilter(watched, event);
}
}
