// SPDX-License-Identifier: GPL-3.0-or-later
#include "language.hpp"
#include <QAbstractButton>
#include <QAction>
#include <QComboBox>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPlainTextEdit>
#include <QSignalBlocker>
#include <QWidget>
namespace mm_sstv {
void retranslateUi(QWidget* root) {
    // Only presentation properties are translated. Received text documents,
    // editable fields, device names, filenames and combo item data are excluded.
    auto widgets = root->findChildren<QWidget*>(); widgets.prepend(root);
    for (auto* widget : widgets) {
        widget->setWindowTitle(uiMessage(widget->windowTitle()));
        widget->setToolTip(uiMessage(widget->toolTip()));
        widget->setStatusTip(uiMessage(widget->statusTip()));
        widget->setAccessibleName(uiMessage(widget->accessibleName()));
        widget->setAccessibleDescription(uiMessage(widget->accessibleDescription()));
        if (auto* label = qobject_cast<QLabel*>(widget); label && label->objectName() != "bulletinReaderInfo" && !label->text().isEmpty())
            label->setText(uiMessage(label->text()));
        if (auto* button = qobject_cast<QAbstractButton*>(widget)) button->setText(uiMessage(button->text()));
        if (auto* group = qobject_cast<QGroupBox*>(widget)) group->setTitle(uiMessage(group->title()));
        if (auto* menu = qobject_cast<QMenu*>(widget)) menu->setTitle(uiMessage(menu->title()));
        if (auto* text = qobject_cast<QPlainTextEdit*>(widget)) text->setPlaceholderText(uiMessage(text->placeholderText()));
        if (auto* field = qobject_cast<QLineEdit*>(widget)) field->setPlaceholderText(uiMessage(field->placeholderText()));
        if (auto* combo = qobject_cast<QComboBox*>(widget); combo && (combo->objectName() == "modulation" || combo->objectName() == "overlayFont" || combo->objectName() == "overlayLayer")) {
            const QSignalBlocker blocked(combo);
            for (int i = 0; i < combo->count(); ++i) combo->setItemText(i, uiMessage(combo->itemText(i)));
        }
        widget->update();
    }
    for (auto* action : root->findChildren<QAction*>()) {
        action->setText(uiMessage(action->text())); action->setToolTip(uiMessage(action->toolTip()));
        action->setStatusTip(uiMessage(action->statusTip()));
    }
}
}
