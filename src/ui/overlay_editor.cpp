// SPDX-License-Identifier: GPL-3.0-or-later
#include "overlay_editor.hpp"
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGuiApplication>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QVBoxLayout>
#include <algorithm>
namespace mm_sstv {
namespace {
QPointF mousePosition(const QMouseEvent* event) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    return event->position();
#else
    return event->localPos();
#endif
}
}

OverlayPreview::OverlayPreview(QImage base, TextOverlay& overlay, QWidget* parent)
    : QWidget(parent), base_(std::move(base)), overlay_(overlay) {
    setObjectName("overlayPreview"); setMinimumSize(160, 128); setFocusPolicy(Qt::StrongFocus); setMouseTracking(true);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setToolTip("Text anklicken und ziehen · Pfeiltasten: 1 Bildpixel · Umschalt: 5 Bildpixel");
}
QRectF OverlayPreview::imageRect() const {
    const auto available = QRectF(rect()).adjusted(6, 6, -6, -6);
    const auto size = QSizeF(base_.size()).scaled(available.size(), Qt::KeepAspectRatio);
    return QRectF(available.center() - QPointF(size.width()/2, size.height()/2), size);
}
QPointF OverlayPreview::imagePoint(QPointF point) const {
    const auto area = imageRect();
    return {(point.x()-area.left())*base_.width()/area.width(), (point.y()-area.top())*base_.height()/area.height()};
}
void OverlayPreview::selectLayer(int index) { layer_ = std::clamp(index, 0, 2); update(); }
void OverlayPreview::paintEvent(QPaintEvent*) {
    QPainter painter(this); painter.fillRect(rect(), QColor("#26323b")); const auto area = imageRect();
    painter.setRenderHint(QPainter::SmoothPixmapTransform); painter.drawImage(area, drawTextOverlay(base_, overlay_));
    const auto box = overlayGeometry(overlay_, base_.size())[layer_].bounds;
    if (!box.isEmpty()) {
        const QRectF selection(area.left()+box.x()*area.width()/base_.width(), area.top()+box.y()*area.height()/base_.height(),
            box.width()*area.width()/base_.width(), box.height()*area.height()/base_.height());
        painter.setPen(QPen(QColor("#ffd56a"), 1, Qt::DashLine)); painter.drawRect(selection);
    }
}
void OverlayPreview::moveLayer(QPointF point) {
    const auto box = overlayGeometry(overlay_, base_.size())[layer_].bounds; if (box.isEmpty()) return;
    auto& layer = overlay_.layers[layer_];
    layer.x = std::clamp(point.x(), 0., std::max(0., base_.width()-box.width())) / base_.width();
    layer.y = std::clamp(point.y(), 0., std::max(0., base_.height()-box.height())) / base_.height(); update();
}
void OverlayPreview::mousePressEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton) return;
    const auto point = imagePoint(mousePosition(event)); const auto boxes = overlayGeometry(overlay_, base_.size());
    for (int i = 2; i >= 0; --i) if (!boxes[i].bounds.isEmpty() && boxes[i].bounds.contains(point)) {
        selectLayer(i); if (selected) selected(i); dragging_ = true; dragOffset_ = point-boxes[i].bounds.topLeft();
        setFocus(Qt::MouseFocusReason); setCursor(Qt::ClosedHandCursor); event->accept(); return;
    }
}
void OverlayPreview::mouseMoveEvent(QMouseEvent* event) {
    if (dragging_) { moveLayer(imagePoint(mousePosition(event))-dragOffset_); event->accept(); return; }
    const auto point = imagePoint(mousePosition(event)); bool hit = false;
    for (const auto& item : overlayGeometry(overlay_, base_.size())) hit |= !item.bounds.isEmpty() && item.bounds.contains(point);
    setCursor(hit ? Qt::OpenHandCursor : Qt::ArrowCursor);
}
void OverlayPreview::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        if (dragging_) moveLayer(imagePoint(mousePosition(event))-dragOffset_);
        dragging_ = false; setCursor(Qt::OpenHandCursor); event->accept();
    }
}
void OverlayPreview::keyPressEvent(QKeyEvent* event) {
    QPointF delta;
    switch (event->key()) {
    case Qt::Key_Left: delta.setX(-1); break; case Qt::Key_Right: delta.setX(1); break;
    case Qt::Key_Up: delta.setY(-1); break; case Qt::Key_Down: delta.setY(1); break;
    default: QWidget::keyPressEvent(event); return;
    }
    if (event->modifiers() & Qt::ShiftModifier) delta *= 5;
    moveLayer(overlayGeometry(overlay_, base_.size())[layer_].bounds.topLeft()+delta); event->accept();
}
OverlayEditor::OverlayEditor(QImage base, TextOverlay overlay, QWidget* parent)
    : QDialog(parent), overlay_(std::move(overlay)) {
    setObjectName("textOverlayEditor"); setWindowTitle("Sendebild · Textüberlagerung"); setMinimumSize(520, 320);
    auto* layout = new QVBoxLayout(this); layout->setContentsMargins(8, 8, 8, 8); layout->setSpacing(6);
    auto* content = new QHBoxLayout; content->setSpacing(8);
    auto* picture = new QVBoxLayout; preview_ = new OverlayPreview(std::move(base), overlay_, this); picture->addWidget(preview_, 1);
    auto* help = new QLabel("Texte im Bild verschieben.\nPfeiltasten: 1 Pixel · Umschalt: 5 Pixel", this); help->setWordWrap(true);
    auto helpFont = help->font(); helpFont.setPointSizeF(8); help->setFont(helpFont); picture->addWidget(help); content->addLayout(picture, 1);
    auto* scroll = new QScrollArea(this); scroll->setWidgetResizable(true); scroll->setMinimumWidth(250); scroll->setFrameShape(QFrame::NoFrame);
    auto* controls = new QWidget(scroll); auto* column = new QVBoxLayout(controls); column->setContentsMargins(0, 0, 2, 0); column->setSpacing(6);
    auto* station = new QFormLayout;
    for (unsigned i = 0; i < fields_.size(); ++i) {
        auto* field = new QLineEdit(overlay_.texts[i], controls); fields_[i] = field; field->setMaxLength(i < 2 ? 24 : 100);
        field->setObjectName(i == 0 ? "overlayOwnCall" : i == 1 ? "overlayOtherCall" : QString("overlayFreeText%1").arg(i-1));
        field->installEventFilter(this);
        connect(field, &QLineEdit::textChanged, this, [this, i](const QString& text) { overlay_.texts[i] = text; selectField(i); });
        if (i < 2) station->addRow(i == 0 ? "Eigenes Rufzeichen" : "Gegenstation", field);
    }
    column->addLayout(station);
    textsGroup_ = new QGroupBox(QString("Freitexte · Auswahl %1").arg(overlay_.selected+1), controls); auto* texts = new QFormLayout(textsGroup_);
    for (unsigned i = 2; i < fields_.size(); ++i) texts->addRow(QString::number(i-1), fields_[i]);
    column->addWidget(textsGroup_);
    auto* formatting = new QFormLayout;
    layer_ = new QComboBox(controls); layer_->setObjectName("overlayLayer"); layer_->addItems({"Eigenes Rufzeichen", "Gegenstation", "Freitext"});
    font_ = new QComboBox(controls); font_->setObjectName("overlayFont"); font_->addItems({"Serifenlos", "Serifen", "Monospace"});
    size_ = new QSpinBox(controls); size_->setObjectName("overlaySize"); size_->setRange(10, 48); size_->setSuffix(" px"); size_->setToolTip("Schriftgröße bezogen auf 320 × 256 Pixel; größere Bildformate skalieren mit.");
    bold_ = new QCheckBox("Fett", controls); bold_->setObjectName("overlayBold");
    formatting->addRow("Text im Bild", layer_); formatting->addRow("Schrift", font_); formatting->addRow("Größe", size_); formatting->addRow("", bold_);
    column->addLayout(formatting); column->addStretch(); scroll->setWidget(controls); content->addWidget(scroll); layout->addLayout(content, 1);
    connect(layer_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) { selectLayer(index); });
    const auto styleChanged = [this] { auto& item = overlay_.layers[layer_->currentIndex()]; item.font = font_->currentIndex(); item.size = size_->value(); item.bold = bold_->isChecked(); preview_->update(); };
    connect(font_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, styleChanged);
    connect(size_, QOverload<int>::of(&QSpinBox::valueChanged), this, styleChanged); connect(bold_, &QCheckBox::toggled, this, styleChanged);
    preview_->selected = [this](int index) { selectLayer(index); };
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText("Übernehmen"); buttons->button(QDialogButtonBox::Cancel)->setText("Abbrechen");
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept); connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject); layout->addWidget(buttons);
    selectLayer(0); QSize initial(760, 450);
    if (const auto* screen = QGuiApplication::primaryScreen()) initial = initial.boundedTo(screen->availableGeometry().size()-QSize(24, 48));
    resize(initial.expandedTo(minimumSize()));
}
void OverlayEditor::selectLayer(int index) {
    const QSignalBlocker layerBlock(layer_), fontBlock(font_), sizeBlock(size_), boldBlock(bold_);
    layer_->setCurrentIndex(index); const auto& item = overlay_.layers[index];
    font_->setCurrentIndex(item.font); size_->setValue(item.size); bold_->setChecked(item.bold); preview_->selectLayer(index);
}
void OverlayEditor::selectField(unsigned index) {
    if (!layer_) return; // Fields may receive events while the form is built.
    if (index >= 2) overlay_.selected = int(index - 2);
    textsGroup_->setTitle(QString("Freitexte · Auswahl %1").arg(overlay_.selected+1));
    selectLayer(index < 2 ? int(index) : 2); preview_->update();
}
bool OverlayEditor::eventFilter(QObject* object, QEvent* event) {
    if (event->type() == QEvent::FocusIn) for (unsigned i = 0; i < fields_.size(); ++i) if (object == fields_[i]) { selectField(i); break; }
    return QDialog::eventFilter(object, event);
}
}
