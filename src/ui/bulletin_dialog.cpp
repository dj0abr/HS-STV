// SPDX-License-Identifier: GPL-3.0-or-later
#include "language.hpp"
#include "bulletin_dialog.hpp"
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScreen>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QVBoxLayout>
namespace mm_sstv {
BulletinDialog::BulletinDialog(std::optional<Bulletin> current, QWidget* parent)
    : QDialog(parent), current_(std::move(current)) {
    setObjectName("bulletinReader"); setWindowTitle(uiText("QO100 MM-Bake · Bulletins")); setAttribute(Qt::WA_DeleteOnClose);
    setMinimumSize(480, 320);
    auto* layout = new QVBoxLayout(this);
    auto* label = new QLabel(uiText("Gespeichertes Bulletin"), this); layout->addWidget(label);
    choices_ = new QComboBox(this); choices_->setObjectName("bulletinArchiveChoice"); label->setBuddy(choices_);
    choices_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon); choices_->setMinimumContentsLength(20);
    choices_->setAccessibleName(uiText("Gespeichertes Bulletin auswählen")); layout->addWidget(choices_);
    info_ = new QLabel(uiText("Archiv wird geladen…"), this); info_->setTextFormat(Qt::PlainText); info_->setWordWrap(true);
    info_->setObjectName("bulletinReaderInfo"); info_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred); layout->addWidget(info_);
    text_ = new QPlainTextEdit(this); text_->setObjectName("bulletinReaderText"); text_->setReadOnly(true); text_->setUndoRedoEnabled(false);
    auto font = QFontDatabase::systemFont(QFontDatabase::FixedFont); font.setPointSizeF(10); text_->setFont(font);
    text_->setPlaceholderText(uiText("Noch keine gespeicherten Bulletins vorhanden.")); layout->addWidget(text_, 1);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this); buttons->setObjectName("bulletinReaderButtons"); layout->addWidget(buttons);
    buttons->button(QDialogButtonBox::Close)->setText(uiText("Schließen"));
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::close);
    connect(choices_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this] { select(); });
    if (current_) {
        choices_->addItem(current_->filename + uiText(" (aktuell)"), BulletinArchive::idFor(*current_));
        display(*current_); displayedId_ = BulletinArchive::idFor(*current_);
    }
    connect(&Language::instance(), &Language::changed, this, [this] {
        retranslateUi(this);
        if (info_->text() != displayedInfo_) info_->setText(uiMessage(info_->text()));
        if (current_) {
            const auto id = BulletinArchive::idFor(*current_); const int index = choices_->findData(id);
            if (index >= 0 && (choices_->itemText(index).endsWith(" (aktuell)") || choices_->itemText(index).endsWith(" (current)"))) {
                const QSignalBlocker blocked(choices_); choices_->setItemText(index, current_->filename + uiText(" (aktuell)"));
            }
        }
    });
    QSize size(900, 650); if (auto* screen = QGuiApplication::primaryScreen()) size = size.boundedTo(screen->availableGeometry().size() - QSize(32, 48));
    resize(size.expandedTo(minimumSize()));
}
void BulletinDialog::display(const Bulletin& bulletin) {
    text_->setPlainText(bulletin.text); text_->moveCursor(QTextCursor::Start); text_->verticalScrollBar()->setValue(0);
    info_->setText(bulletin.filename + " · " + bulletin.receivedAt.toLocalTime().toString("dd.MM.yyyy HH:mm:ss") +
                   (bulletin.title.isEmpty() ? QString() : " · " + bulletin.title));
    displayedInfo_ = info_->text();
}
void BulletinDialog::setEntries(const std::vector<BulletinArchiveEntry>& entries) {
    const auto previous = choices_->currentData().toString();
    {
        const QSignalBlocker blocked(choices_); choices_->clear();
        for (const auto& entry : entries) choices_->addItem(entry.filename + " · " + entry.receivedAt.toLocalTime().toString("dd.MM.yyyy HH:mm:ss"), entry.id);
        if (current_ && choices_->findData(BulletinArchive::idFor(*current_)) < 0)
            choices_->insertItem(0, current_->filename + uiText(" (aktuell)"), BulletinArchive::idFor(*current_));
        int selected = choices_->findData(previous);
        if (selected < 0 && current_) selected = choices_->findData(BulletinArchive::idFor(*current_));
        if (selected < 0 && choices_->count()) selected = 0;
        choices_->setCurrentIndex(selected);
    }
    if (!choices_->count()) { info_->setText(uiText("Noch keine gespeicherten Bulletins vorhanden.")); return; }
    select();
}
void BulletinDialog::select() {
    const auto id = choices_->currentData().toString(); if (id.isEmpty()) return;
    if (id == displayedId_) { info_->setText(displayedInfo_); return; }
    if (current_ && id == BulletinArchive::idFor(*current_)) { display(*current_); displayedId_ = id; return; }
    info_->setText(uiText("Bulletin wird geladen…")); if (loadRequested) loadRequested(id);
}
void BulletinDialog::showArchived(const QString& id, const Bulletin& bulletin) {
    if (choices_->currentData().toString() != id) return;
    if (id == displayedId_) { info_->setText(displayedInfo_); return; }
    display(bulletin); displayedId_ = id;
}
void BulletinDialog::showError(const QString& id, const QString& message) {
    if (id.isEmpty() || choices_->currentData().toString() == id) info_->setText(uiMessage(message));
}
}
