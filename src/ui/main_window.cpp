// SPDX-License-Identifier: GPL-3.0-or-later
#include "main_window.hpp"
#include "image_panel.hpp"
#include "receive_monitor.hpp"
#include "overlay_editor.hpp"
#include <QFileDialog>
#include <QDateTime>
#include <QFileInfo>
#include <QDesktopServices>
#include <QUrl>
#include <QImageReader>
#include <QProgressBar>
#include <QPlainTextEdit>
#include <QScrollBar>
#include <QFontDatabase>
#include <QGroupBox>
#include <QAction>
#include <QComboBox>
#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QGuiApplication>
#include <QHelpEvent>
#include <QPainter>
#include <QScreen>
#include <QStyle>
#include <QToolTip>
#include <QLabel>
#include <QMenuBar>
#include <QPushButton>
#include <QSlider>
#include <QSignalBlocker>
#include <QScopedValueRollback>
#include <QTimer>
#include <QStackedWidget>
#include "beacon_view.hpp"
#include "bulletin_dialog.hpp"
#include "about_dialog.hpp"
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

namespace mm_sstv {
namespace {
// File names and live status must never force the two image columns wider.
// Keep the full QLabel text for accessibility and show it in the tooltip.
class CompactLabel final : public QLabel {
public:
    explicit CompactLabel(QWidget* parent, Qt::TextElideMode mode = Qt::ElideMiddle)
        : QLabel(parent), mode_(mode) {
        setTextFormat(Qt::PlainText); setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    }
    QSize minimumSizeHint() const override { return {0, fontMetrics().height() + 2}; }
    QSize sizeHint() const override { return {std::min(240, QLabel::sizeHint().width()), fontMetrics().height() + 2}; }
protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        style()->drawItemText(&painter, contentsRect(), alignment(), palette(), isEnabled(),
            fontMetrics().elidedText(text(), mode_, contentsRect().width()), foregroundRole());
    }
    bool event(QEvent* event) override {
        if (event->type() == QEvent::ToolTip) {
            QToolTip::showText(static_cast<QHelpEvent*>(event)->globalPos(), toolTip().isEmpty() ? text() : toolTip(), this); return true;
        }
        return QLabel::event(event);
    }
private:
    Qt::TextElideMode mode_;
};
}
MainWindow::MainWindow(std::unique_ptr<AudioOutput> output, QSettings& settings, std::unique_ptr<AudioInput> input)
    : transmitter_(std::move(output)), settings_(settings), receiver_(std::move(input)) {
    setWindowTitle("HS_STV - QO100 Bildübertragung und Bakenmonitor"); setMinimumSize(640, 440);
    const auto savedMode = settings_.value("modem/modulation", "8apsk").toString();
    for (const auto mode : {Modulation::Apsk8, Modulation::Qpsk4410, Modulation::Martin1, Modulation::Martin2, Modulation::Scottie1, Modulation::Scottie2, Modulation::MmBeacon, Modulation::PskBeacon})
        if (savedMode == modeKey(mode)) mode_ = mode;
    overlay_ = loadTextOverlay(settings_);
    inputDevice_ = settings_.value("audio/input", systemDefaultAudioDevice).toString().toStdString();
    device_ = settings_.value("audio/output", systemDefaultAudioDevice).toString().toStdString();
    level_ = std::clamp(settings_.value("audio/level", 35).toInt(), 0, 100);
    transmitter_.setLevel(level_);
    auto* file = menuBar()->addMenu("Bild");
    open_ = file->addAction("Bild öffnen…"); open_->setObjectName("openImage");
    connect(open_, &QAction::triggered, this, [this] { chooseImage(); });
    overlayAction_ = file->addAction("Textüberlagerung…"); overlayAction_->setObjectName("editTextOverlay");
    connect(overlayAction_, &QAction::triggered, this, [this] { editTextOverlay(); });
    auto* folder = file->addAction("Empfangsordner öffnen"); folder->setObjectName("openReceiveFolder");
    connect(folder, &QAction::triggered, this, [this] { openReceiveFolder(); });
    auto* menu = menuBar()->addMenu("Setup");
    auto* audio = menu->addAction("Soundkarten…"); audio->setObjectName("audioSetup");
    connect(audio, &QAction::triggered, this, [this] { setup(); });
    auto* help = menuBar()->addAction("&Hilfe"); help->setObjectName("helpAbout"); help->setMenuRole(QAction::NoRole);
    help->setShortcut(QKeySequence::HelpContents); help->setToolTip("Über HS_STV · Version, Bibliotheken und Lizenzen");
    connect(help, &QAction::triggered, this, [this] { showHelp(); });
    auto* central = new QWidget(this); auto* layout = new QVBoxLayout(central);
    layout->setContentsMargins(6, 6, 6, 6); layout->setSpacing(6);
    contentPages_ = new QStackedWidget(central);
    auto* imagePage = new QWidget(contentPages_);
    auto* panes = new QGridLayout(imagePage); panes->setContentsMargins(0, 0, 0, 0); panes->setHorizontalSpacing(6); panes->setVerticalSpacing(4);
    panes->setColumnStretch(0, 1); panes->setColumnStretch(1, 1); panes->setRowStretch(1, 1);
    auto* sendHeader = new QHBoxLayout; sendHeader->setSpacing(4);
    overlayEdit_ = new QPushButton("Text…", central); overlayEdit_->setObjectName("overlayEditButton");
    overlayEdit_->setFixedWidth(overlayEdit_->fontMetrics().horizontalAdvance("Text…") + 18);
    overlayEdit_->setToolTip("Sendebild: Rufzeichen und Freitexte bearbeiten");
    connect(overlayEdit_, &QPushButton::clicked, this, [this] { editTextOverlay(); });
    auto titleFont = overlayEdit_->font(); titleFont.setBold(true); sendHeader->addWidget(overlayEdit_);
    overlaySlots_ = new QComboBox(central); overlaySlots_->setObjectName("overlayTextSlot"); overlaySlots_->setAccessibleName("Freitext auswählen");
    overlaySlots_->addItems({"1", "2", "3", "4", "5"}); overlaySlots_->setFixedWidth(42); sendHeader->addWidget(overlaySlots_);
    refreshOverlayChoice();
    connect(overlaySlots_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
        if (transmitter_.running()) { refreshOverlayChoice(); return; }
        overlay_.selected = std::clamp(index, 0, 4); saveTextOverlay(settings_, overlay_); refreshOverlayChoice(); prepare();
    });
    sizes_ = new QComboBox(central); sizes_->setObjectName("imageSize"); sizes_->setToolTip("Bildgröße der übertragenen JPEG-Datei; die Vorschau passt sich dem Fenster an.");
    sendHeader->addWidget(sizes_);
    connect(sizes_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
        if (!isSstv(mode_)) settings_.setValue("image/sizeIndex", index);
        prepare();
    });
    start_ = new QPushButton("Senden", central); start_->setObjectName("transmitButton"); start_->setMinimumWidth(80);
    auto buttonFont = start_->font(); buttonFont.setBold(true); start_->setFont(buttonFont); sendHeader->addWidget(start_); sendHeader->addStretch();
    panes->addLayout(sendHeader, 0, 0);
    auto* receiveHeader = new QHBoxLayout; receiveHeader->setSpacing(6);
    auto* receiveTitle = new QLabel("Empfang", central); receiveTitle->setFont(titleFont); receiveHeader->addWidget(receiveTitle);
    receiveStatus_ = new CompactLabel(central, Qt::ElideRight); receiveStatus_->setText(receiver_.available() ? "Empfang startet…" : "Kein Audioeingang"); receiveStatus_->setObjectName("receiveStatus");
    receiveHeader->addWidget(receiveStatus_, 1); panes->addLayout(receiveHeader, 0, 1);
    preview_ = new ImagePanel(central); preview_->setObjectName("imagePreview");
    preview_->clicked = [this] { chooseImage(); };
    preview_->dropped = [this](const QString& path) { loadImage(path); };
    receivePreview_ = new ImagePanel(central, false); receivePreview_->setObjectName("receivedPreview");
    auto* txPane = new QHBoxLayout; txPane->setContentsMargins(0, 0, 0, 0); txPane->setSpacing(4);
    txPane->addWidget(preview_, 1);
    auto* levelControl = new QWidget(central); levelControl->setObjectName("transmitLevelControl");
    auto* levelColumn = new QVBoxLayout(levelControl); levelColumn->setContentsMargins(0, 0, 0, 0); levelColumn->setSpacing(2);
    auto* levelTitle = new QLabel("Pegel", levelControl); levelTitle->setAlignment(Qt::AlignCenter);
    auto* slider = new QSlider(Qt::Vertical, levelControl); slider->setObjectName("outputLevel"); slider->setAccessibleName("Sendepegel");
    slider->setRange(0, 100); slider->setValue(level_);
    slider->setToolTip("Sendepegel 0–100 % · auch während einer Sendung einstellbar");
    auto* percent = new QLabel(QString::number(level_) + " %", levelControl); percent->setObjectName("outputLevelValue"); percent->setAlignment(Qt::AlignCenter);
    levelControl->setFixedWidth(std::max(percent->fontMetrics().horizontalAdvance("100 %"), levelTitle->sizeHint().width()) + 6);
    levelColumn->addWidget(levelTitle); levelColumn->addWidget(slider, 1, Qt::AlignHCenter); levelColumn->addWidget(percent);
    txPane->addWidget(levelControl); panes->addLayout(txPane, 1, 0); panes->addWidget(receivePreview_, 1, 1);
    connect(slider, &QSlider::valueChanged, this, [this, percent](int value) {
        level_ = static_cast<unsigned>(value); transmitter_.setLevel(level_);
        percent->setText(QString::number(value) + " %"); settings_.setValue("audio/level", value);
        if (!transmitter_.running()) showReady();
    });
    imageInfo_ = new CompactLabel(central); imageInfo_->setObjectName("imageInfo"); panes->addWidget(imageInfo_, 2, 0);
    receiveInfo_ = new CompactLabel(central); receiveInfo_->setObjectName("receiveInfo"); panes->addWidget(receiveInfo_, 2, 1);
    progress_ = new QProgressBar(central); progress_->setObjectName("sendProgress"); progress_->setValue(0); progress_->setFormat("TX %p%");
    receiveProgress_ = new QProgressBar(central); receiveProgress_->setObjectName("receiveProgress"); receiveProgress_->setValue(0); receiveProgress_->setFormat("RX %p%");
    const auto styleProgress = [](QProgressBar* bar, const QString& color) {
        auto font = bar->font(); font.setPointSizeF(8); bar->setFont(font);
        bar->setFixedHeight(std::max(14, bar->fontMetrics().height() + 2));
        bar->setStyleSheet(QString("QProgressBar { border: 1px solid #b6c0c8; border-radius: 3px; background: #eef1f4; color: #172331; text-align: center; padding: 0px; } QProgressBar::chunk { background: %1; border-radius: 2px; }").arg(color));
    };
    styleProgress(progress_, "#e76969"); styleProgress(receiveProgress_, "#5ab875");
    panes->addWidget(progress_, 3, 0); panes->addWidget(receiveProgress_, 3, 1);
    contentPages_->addWidget(imagePage);
    beaconView_ = new BeaconView(contentPages_);
    contentPages_->addWidget(beaconView_);
    auto* pskPage = new QGroupBox("PSB-Bake · dekodierter Text", contentPages_); pskPage->setObjectName("pskBeaconPage");
    auto* pskLayout = new QVBoxLayout(pskPage);
    pskText_ = new QPlainTextEdit(pskPage); pskText_->setObjectName("pskBeaconText");
    pskText_->setReadOnly(true); pskText_->setUndoRedoEnabled(false); pskText_->setMaximumBlockCount(2000);
    auto pskFont = QFontDatabase::systemFont(QFontDatabase::FixedFont); pskFont.setPointSizeF(10); pskText_->setFont(pskFont);
    pskText_->setLineWrapMode(QPlainTextEdit::NoWrap);
    pskText_->setPlaceholderText("Warte auf PSK-Bakentext…\nSignalmitte im Audio etwa 1500 Hz (Suchbereich ±500 Hz).\nErste vollständige Texte nach etwa 20–40 Sekunden.");
    pskLayout->addWidget(pskText_); contentPages_->addWidget(pskPage); layout->addWidget(contentPages_, 1);
    receiveStatistics_ = new CompactLabel(central, Qt::ElideRight); receiveStatistics_->setObjectName("receiveStatistics");
    auto statisticsFont = receiveStatistics_->font(); statisticsFont.setPointSizeF(9); receiveStatistics_->setFont(statisticsFont);
    receiveStatistics_->setText("RX · Warte auf Audiodaten"); layout->addWidget(receiveStatistics_);
    monitor_ = new ReceiveMonitor(central); monitor_->clear("Warte auf Audiodaten", mode_);
    auto* monitorRow = new QHBoxLayout; monitorRow->setSpacing(4); monitorRow->setContentsMargins(0, 0, 0, 0);
    const auto addRangeControl = [&](const QString& title, const QString& name, int minimum, int maximum, int value, const QString& help) {
        auto* control = new QWidget(central); control->setToolTip(help);
        auto* column = new QVBoxLayout(control); column->setContentsMargins(0, 0, 0, 0); column->setSpacing(2);
        auto* caption = new QLabel(title, control); caption->setAlignment(Qt::AlignCenter);
        auto* number = new QLabel(QString::number(value), control); number->setObjectName(name + "Value"); number->setAlignment(Qt::AlignCenter);
        auto font = caption->font(); font.setPointSizeF(8); caption->setFont(font); number->setFont(font);
        control->setFixedWidth(std::max({number->fontMetrics().horizontalAdvance("−100"), number->fontMetrics().horizontalAdvance("+6 dB"), caption->sizeHint().width()}) + 6);
        auto* range = new QSlider(Qt::Vertical, control); range->setObjectName(name); range->setAccessibleName(help); range->setToolTip(help);
        range->setRange(minimum, maximum); range->setValue(value);
        column->addWidget(caption); column->addWidget(range, 1, Qt::AlignHCenter); column->addWidget(number);
        connect(range, &QSlider::valueChanged, this, [this, number](int db) {
            number->setText(isSstv(mode_) ? (db > 0 ? "+" : "") + QString::number(db) + " dB" : QString::number(db));
        });
        monitorRow->addWidget(control); return range;
    };
    minimumDb_ = addRangeControl("Min", "spectrumMin", -140, -10, -90, {});
    maximumDb_ = addRangeControl("Max", "spectrumMax", -120, 0, -20, {});
    minimumDbValue_ = minimumDb_->parentWidget()->findChild<QLabel*>("spectrumMinValue");
    maximumDbValue_ = maximumDb_->parentWidget()->findChild<QLabel*>("spectrumMaxValue");
    settings_.remove("display/sstvAutoLevel"); // SSTV display levels are always automatic.
    const auto updateRange = [this] {
        if (updatingDisplayRange_) return;
        if (isSstv(mode_)) monitor_->setLevelAdjustments(minimumDb_->value(), maximumDb_->value());
        else monitor_->setDbRange(minimumDb_->value(), maximumDb_->value());
        syncDisplayRange(); saveDisplayRange();
    };
    connect(minimumDb_, &QSlider::valueChanged, this, updateRange); connect(maximumDb_, &QSlider::valueChanged, this, updateRange);
    restoreDisplayRange();
    monitorRow->addWidget(monitor_, 1); layout->addLayout(monitorRow);
    auto* footer = new QHBoxLayout; footer->setSpacing(8);
    status_ = new CompactLabel(central, Qt::ElideRight); status_->setObjectName("statusText"); footer->addWidget(status_, 1);
    modes_ = new QComboBox(central); modes_->setObjectName("modulation");
    for (const auto mode : {Modulation::Apsk8, Modulation::Qpsk4410, Modulation::Martin1, Modulation::Martin2, Modulation::Scottie1, Modulation::Scottie2, Modulation::MmBeacon, Modulation::PskBeacon})
        modes_->addItem(isPskBeacon(mode) ? QStringLiteral("PSB-Bake") : isBeacon(mode) ? QStringLiteral("QO100 MM-Bake") : isSstv(mode) ? QString("SSTV %1").arg(modulationName(mode)) :
            QString("%1 · %2 bit/s").arg(modulationName(mode)).arg(parameters(mode).bitRate()), int(mode));
    modes_->setCurrentIndex(modes_->findData(int(mode_)));
    modes_->setToolTip("Modus für Senden und Empfang; beide Stationen gleich einstellen.\n8APSK/QPSK: HS-Modem-kompatibel mit 100-Hz-Referenzträger.\nSSTV Martin 1/2 und Scottie 1/2: 320 × 256 Pixel.\nQO100 MM-Bake: nur Empfang, 8APSK mit 7200 bit/s.\nPSB-Bake: nur Empfang, BPSK/Manchester mit 400 bit/s; Audio-Träger etwa 1500 Hz.");
    waterfallId_ = new QCheckBox("Waterfall-ID", central); waterfallId_->setObjectName("waterfallId");
    waterfallId_->setChecked(settings_.value("transmit/waterfallId", false).toBool());
    waterfallId_->setToolTip("Vor jeder Bildsendung HS-STV und die gewählte Betriebsart als Wasserfalltext senden.\nSSTV: Kennung 10 dB über dem eingestellten Bildpegel · Digital: keine Pegelanhebung.\nDer Pegelregler wirkt auch auf die Kennung; STOP bricht die gesamte Sendung ab.");
    connect(waterfallId_, &QCheckBox::toggled, this, [this](bool enabled) {
        settings_.setValue("transmit/waterfallId", enabled); prepare();
    });
    footer->addWidget(waterfallId_); footer->addWidget(modes_); layout->addLayout(footer);
    updateFormats();
    connect(modes_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this] {
        mode_ = static_cast<Modulation>(modes_->currentData().toInt());
        settings_.setValue("modem/modulation", modeKey(mode_));
        updateFormats(); prepare(); restartReceiver();
    });
    setCentralWidget(central);
    QSize initialSize(900, 560);
    if (const auto* screen = QGuiApplication::primaryScreen()) initialSize = initialSize.boundedTo(screen->availableGeometry().size() - QSize(24, 48));
    resize(initialSize.expandedTo(minimumSize()));
    connect(start_, &QPushButton::clicked, this, [this] { toggle(); });
    auto* timer = new QTimer(this); timer->setInterval(100);
    connect(timer, &QTimer::timeout, this, [this] { poll(); }); timer->start();
    receiveRetry_ = new QTimer(this); receiveRetry_->setSingleShot(true); receiveRetry_->setInterval(3000);
    connect(receiveRetry_, &QTimer::timeout, this, [this] { restartReceiver(); });
    settings_.remove("receive/enabled"); // Migrate the obsolete manual-pause preference.
    receiver_.sstvReceived = [this](const SstvUpdate& update) { receivedSstv(update); };
    receiver_.imageReceived = [this](const ReceivedImage& image) { receivedImage(image); };
    receiver_.pskTextReceived = [this](const QString& text) {
        auto* scroll = pskText_->verticalScrollBar();
        const bool follow = scroll->value() >= scroll->maximum() - 1 && !pskText_->textCursor().hasSelection();
        const int oldPosition = scroll->value(), horizontal = pskText_->horizontalScrollBar()->value();
        const int before = pskText_->document()->isEmpty() ? 0 : pskText_->document()->blockCount();
        pskText_->appendPlainText(text.trimmed());
        if (follow) scroll->setValue(scroll->maximum());
        else scroll->setValue(std::max(0, oldPosition - std::max(0, before + text.trimmed().count('\n') + 1 - pskText_->document()->blockCount())));
        pskText_->horizontalScrollBar()->setValue(horizontal);
    };
    receiver_.beaconMessageReceived = [this](const BeaconMessage& message) { beaconView_->appendMessage(message); };
    receiver_.bulletinReceived = [this](const Bulletin& bulletin) { beaconView_->showBulletin(bulletin); };
    receiver_.bulletinProgressChanged = [this](const BulletinProgress& progress) { beaconView_->showBulletinProgress(progress); };
    beaconView_->archiveRequested = [this] { openBulletinArchive(); };
    receiver_.bulletinArchiveChanged = [this] { if (bulletinDialog_) receiver_.listBulletinArchive(); };
    receiver_.bulletinArchiveListed = [this](const auto& entries) { if (bulletinDialog_) bulletinDialog_->setEntries(entries); };
    receiver_.bulletinArchiveLoaded = [this](const auto& id, const auto& bulletin) { if (bulletinDialog_) bulletinDialog_->showArchived(id, bulletin); };
    receiver_.bulletinArchiveError = [this](const auto& id, const auto& error) { if (bulletinDialog_) bulletinDialog_->showError(id, error); };
    receiver_.statusChanged = [this](const ReceiveStatus& status) {
        monitor_->setStatus(status); syncDisplayRange(); showReceiveStatistics(status);
        QString details = status.state;
        if (status.pilot >= 0) details += QString(" · Pilot %1 Hz").arg(status.pilot, 0, 'f', 1);
        if (status.frames) details += QString(isSstv(status.modulation) ? " · AFC %1 Hz · %2 Bildzeilen" : " · AFC %1 Hz · %2 gültige Frames").arg(status.offset, 0, 'f', 1).arg(qulonglong(status.frames));
        QString text = status.locked ? "Sync" : status.state;
        receiveStatus_->setText(text); receiveStatus_->setToolTip(details);
    };
    receiver_.failed = [this](const QString& text) {
        archiveSstv();
        monitor_->clear("Empfang unterbrochen", mode_); receiveStatus_->setToolTip({});
        receiveStatus_->setText(text); receiveRetry_->start();
    };
    refreshDevices(); jpegRetryAge_.start();
    try { archive_.ensureDirectory(); }
    catch (const std::exception& e) { receiveInfo_->setText(QString::fromUtf8(e.what())); }
    if (receiver_.available()) QTimer::singleShot(0, this, [this] { restartReceiver(); });
}
MainWindow::~MainWindow() {
    receiveRetry_->stop();
    // A closed setup dialog does not cancel an in-flight backend call. Keep
    // both adapters alive until it returns; no callback accesses a dead dialog.
    if (deviceScan_.valid()) deviceScan_.wait();
    transmitter_.stop(); receiver_.stop(); archiveSstv();
}
bool MainWindow::deviceAvailable() const {
    if (device_ == systemDefaultAudioDevice) return !devices_.empty();
    return std::any_of(devices_.begin(), devices_.end(), [this](const auto& d) { return d.id == device_; });
}
void MainWindow::updateButton() {
    const bool running = transmitter_.running();
    const bool beacon = isReceiveOnly(mode_);
    contentPages_->setCurrentIndex(isPskBeacon(mode_) ? 2 : isBeacon(mode_) ? 1 : 0);
    start_->setText(running ? "STOP" : "Senden");
    start_->setEnabled(!beacon && (running || (!deviceScan_.valid() && deviceAvailable() && (image_.has_value() || bool(sstvImage_)))));
    overlayEdit_->setEnabled(!running); overlayAction_->setEnabled(!running); overlaySlots_->setEnabled(!running);
    waterfallId_->setEnabled(!running && !beacon); waterfallId_->setVisible(!beacon);
    modes_->setEnabled(!running && !deviceScan_.valid()); sizes_->setEnabled(!running && !isSstv(mode_) && !beacon);
    preview_->setEditable(!running && !beacon); open_->setEnabled(!running && !beacon);
    overlayAction_->setEnabled(!running && !beacon);
}
void MainWindow::showReady() {
    if (isPskBeacon(mode_)) status_->setText("PSB-Bake · BPSK/Manchester · 400 bit/s · nur Empfang");
    else if (isBeacon(mode_)) status_->setText("QO100 MM-Bake · 8APSK · 7200 bit/s · nur Empfang");
    else if (deviceAvailable()) status_->setText("Bereit · " + (device_ == systemDefaultAudioDevice ? QStringLiteral("System-Standard") : QString::fromStdString(device_)) + " · " + QString::number(level_) + " %");
    else status_->setText("Ausgang „" + QString::fromStdString(device_) + "“ nicht verfügbar. Bitte unter Setup auswählen oder aktualisieren.");
    updateButton();
}
void MainWindow::refreshDevices() {
    try { devices_ = transmitter_.audio().devices(); showReady(); }
    catch (const std::exception& e) { devices_.clear(); status_->setText(QString::fromUtf8(e.what())); updateButton(); }
}
void MainWindow::requestDeviceScan() {
    if (deviceScan_.valid()) return;
    auto* output = &transmitter_.audio(); auto* input = &receiver_;
    const bool includeOutputs = !transmitter_.running();
    try {
        deviceScan_ = std::async(std::launch::async, [output, input, includeOutputs] {
            DeviceScan result; result.updateOutputs = includeOutputs;
            if (includeOutputs) {
                try { result.outputs = output->devices(); }
                catch (const std::exception& e) { result.outputError = e.what(); }
            }
            try { result.inputs = input->devices(); }
            catch (const std::exception& e) { result.inputError = e.what(); }
            return result;
        });
    } catch (const std::exception& e) { status_->setText(QString::fromUtf8(e.what())); }
    updateButton();
}
void MainWindow::finishDeviceScan() {
    if (!deviceScan_.valid() || deviceScan_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
    const auto result = deviceScan_.get();
    if (result.updateOutputs) devices_ = result.outputs;
    inputDevices_ = result.inputs; inputDeviceError_ = QString::fromStdString(result.inputError);
    if (result.updateOutputs && !transmitter_.running()) {
        showReady();
        if (!result.outputError.empty()) status_->setText(QString::fromStdString(result.outputError));
    }
    updateButton();
}
void MainWindow::retryJpegSaves() {
    if (pendingJpegs_.empty() || jpegRetryAge_.elapsed() < 2000) return;
    jpegRetryAge_.restart();
    for (auto it = pendingJpegs_.begin(); it != pendingJpegs_.end();) {
        try {
            const auto path = archive_.saveJpeg(it->name, it->bytes);
            if (receivedName_ == it->name && receivedJpeg_ == it->bytes) {
                receiveInfo_->setText("Empfang abgeschlossen");
                receiveInfo_->setToolTip("Automatisch gespeichert: " + path);
            }
            it = pendingJpegs_.erase(it);
        } catch (const std::exception& e) {
            if (receivedName_ == it->name && receivedJpeg_ == it->bytes) receiveInfo_->setText(QString::fromUtf8(e.what()));
            ++it;
        }
    }
}
void MainWindow::toggle() {
    if (isReceiveOnly(mode_)) return;
    if (transmitter_.running()) { transmitter_.stop(); showReady(); return; }
    try {
        if (isSstv(mode_)) {
            if (!sstvImage_) return;
            transmitter_.startSstv(device_, mode_, *sstvImage_, waterfallId_->isChecked());
        } else {
            if (!image_) return;
            transmitter_.startImage(device_, *image_, waterfallId_->isChecked());
        }
        progress_->setValue(0); updateButton();
        status_->setText(transmitter_.sendingWaterfallId() ? "Sendet Waterfall-ID · " + QString::fromStdString(waterfallIdText(mode_)) : "Sendet Bild · " + QString::fromStdString(device_));
    } catch (const std::exception& e) {
        updateButton(); status_->setText("Start fehlgeschlagen: " + QString::fromUtf8(e.what()));
    }
}
void MainWindow::poll() {
    finishDeviceScan(); retryJpegSaves();
    if (!transmitter_.running()) return;
    const auto error = transmitter_.audio().error();
    if (!error.empty()) {
        transmitter_.stop(); updateButton(); status_->setText(QString::fromStdString(error));
    } else if (transmitter_.audio().finished()) {
        transmitter_.stop(); updateButton(); progress_->setValue(100); status_->setText("Sendung beendet.");
    } else if (const auto underruns = transmitter_.audio().underruns()) {
        status_->setText("Sendet · Audioaussetzer: " + QString::number(underruns) + " · Audioverbindung prüfen");
    } else {
        status_->setText(transmitter_.sendingWaterfallId() ? "Sendet Waterfall-ID · " + QString::fromStdString(waterfallIdText(mode_)) : "Sendet Bild · " + QString::fromStdString(device_));
    }
    if (transmitter_.running()) progress_->setValue(transmitter_.progress());
}
void MainWindow::chooseImage() {
    const auto path = QFileDialog::getOpenFileName(this, "Bild öffnen", settings_.value("image/directory").toString(),
        "Bilder (*.jpg *.jpeg *.png *.bmp *.gif *.webp);;Alle Dateien (*)");
    if (!path.isEmpty()) loadImage(path);
}
bool MainWindow::loadImage(const QString& filename) {
    if (transmitter_.running() || isReceiveOnly(mode_)) return false;
    QImageReader reader(filename); reader.setAutoTransform(true);
    const auto size = reader.size();
    if (size.isValid() && qint64(size.width()) * size.height() > 40000000) {
        status_->setText("Bild ist zu groß zum Laden (maximal 40 Megapixel)."); return false;
    }
    const auto image = reader.read();
    if (image.isNull()) { status_->setText("Bild konnte nicht geladen werden: " + reader.errorString()); return false; }
    original_ = image; filename_ = QFileInfo(filename).fileName();
    settings_.setValue("image/directory", QFileInfo(filename).absolutePath()); prepare(); return image_.has_value() || bool(sstvImage_);
}
void MainWindow::prepare() {
    image_.reset(); sstvImage_.reset(); progress_->setValue(0);
    if (isReceiveOnly(mode_)) { showReady(); return; }
    if (original_.isNull()) { updateButton(); return; }
    const double idSeconds = waterfallId_->isChecked() ? double(waterfallIdSamples(mode_)) / modeSampleRate(mode_) : 0;
    try {
        if (isSstv(mode_)) {
            const auto picture = drawTextOverlay(fitPicture(original_, QSize(320, 256)), overlay_);
            sstvImage_ = std::make_unique<SstvImage>(sstvRaster(picture)); preview_->setImage(picture);
            imageInfo_->setText(QString("%1 · 320 × 256 · %2 s").arg(filename_).arg(sstvImageSamples(mode_) / 48000. + idSeconds, 0, 'f', 1));
            showReady(); return;
        }
        const auto picture = preparePicture(original_, sizes_->currentData().toSize(), overlay_);
        image_ = prepareImage(picture.jpeg, filename_.toUtf8().toStdString(), mode_); preview_->setImage(picture.preview);
        const auto seconds = std::size_t(std::ceil(double(image_->audioFrames()) / parameters(mode_).sampleRate + idSeconds));
        imageInfo_->setText(QString("%1 · %2 × %3 · %4 kB · %5 s")
            .arg(QString::fromStdString(image_->filename)).arg(picture.preview.width()).arg(picture.preview.height())
            .arg(image_->jpegBytes / 1000., 0, 'f', 1).arg(seconds));
        showReady();
    } catch (const std::exception& e) { imageInfo_->clear(); status_->setText(QString::fromUtf8(e.what())); updateButton(); }
}
void MainWindow::refreshOverlayChoice() {
    const QSignalBlocker block(overlaySlots_); overlaySlots_->setCurrentIndex(overlay_.selected);
    const auto text = overlay_.texts[2 + overlay_.selected];
    overlaySlots_->setToolTip(QString("Freitext %1: %2").arg(overlay_.selected + 1).arg(text.isEmpty() ? "(leer)" : text));
}
void MainWindow::editTextOverlay() {
    if (transmitter_.running()) return;
    const auto target = isSstv(mode_) ? QSize(320, 256) : sizes_->currentData().toSize();
    QImage base;
    if (original_.isNull()) { base = QImage(target, QImage::Format_RGB32); base.fill(QColor("#404950")); }
    else base = fitPicture(original_, target);
    OverlayEditor editor(std::move(base), overlay_, this);
    if (editor.exec() != QDialog::Accepted) return;
    overlay_ = editor.overlay(); saveTextOverlay(settings_, overlay_); refreshOverlayChoice(); prepare();
}
void MainWindow::restartReceiver() {
    archiveSstv();
    monitor_->clear("Warte auf Audiodaten", mode_); receiveStatus_->setToolTip({});
    restoreDisplayRange();
    receiveRetry_->stop();
    if (!receiver_.available()) { receiveStatus_->setText("Kein Audioeingang"); return; }
    try { receiver_.start(inputDevice_, mode_); }
    catch (const std::exception& e) {
        receiveStatus_->setText(QString::fromUtf8(e.what())); receiveRetry_->start();
    }
}
void MainWindow::syncDisplayRange() {
    const QScopedValueRollback<bool> updating(updatingDisplayRange_, true);
    const bool analog = isSstv(mode_);
    minimumDb_->setValue(analog ? monitor_->minimumAdjustment() : monitor_->minimumDb());
    maximumDb_->setValue(analog ? monitor_->maximumAdjustment() : monitor_->maximumDb());
    // Zero is valid in both modes: refresh formatting even without valueChanged.
    const auto labelText = [analog](int db) {
        return analog ? (db > 0 ? "+" : "") + QString::number(db) + " dB" : QString::number(db);
    };
    minimumDbValue_->setText(labelText(minimumDb_->value())); maximumDbValue_->setText(labelText(maximumDb_->value()));
}
void MainWindow::saveDisplayRange() {
    const bool analog = isSstv(mode_);
    settings_.setValue(analog ? "display/sstvMinOffset" : "display/minDb", analog ? monitor_->minimumAdjustment() : monitor_->minimumDb());
    settings_.setValue(analog ? "display/sstvMaxOffset" : "display/maxDb", analog ? monitor_->maximumAdjustment() : monitor_->maximumDb());
}
void MainWindow::restoreDisplayRange() {
    const QScopedValueRollback<bool> updating(updatingDisplayRange_, true);
    const bool analog = isSstv(mode_);
    minimumDb_->setRange(analog ? -6 : -100, analog ? 6 : -60);
    maximumDb_->setRange(analog ? -6 : -50, analog ? 6 : 0);
    minimumDb_->setSingleStep(1); maximumDb_->setSingleStep(1);
    minimumDb_->setPageStep(analog ? 1 : 10); maximumDb_->setPageStep(analog ? 1 : 10);
    minimumDb_->setToolTip(analog ? "Untere Automatikgrenze feinjustieren: −6 bis +6 dB · 0 = automatische Abstimmung\nNegativ: hellerer Hintergrund · positiv: dunklerer Hintergrund · Automatik läuft immer" :
        "Untere Grenze für Spektrum: −100 bis −60 dBFS");
    maximumDb_->setToolTip(analog ? "Obere Automatikgrenze feinjustieren: −6 bis +6 dB · 0 = automatische Abstimmung\nPositiv: mehr Reserve, dunkleres Signal · negativ: helleres Signal · Automatik läuft immer" :
        "Obere Grenze für Spektrum: −50 bis 0 dBFS");
    minimumDb_->setAccessibleName(minimumDb_->toolTip()); maximumDb_->setAccessibleName(maximumDb_->toolTip());
    if (analog) {
        monitor_->setLevelAdjustments(settings_.value("display/sstvMinOffset", 0).toInt(), settings_.value("display/sstvMaxOffset", 0).toInt());
    } else {
        monitor_->setDbRange(settings_.value("display/minDb", -90).toInt(), settings_.value("display/maxDb", -20).toInt());
    }
    syncDisplayRange(); saveDisplayRange();
}
void MainWindow::receivedImage(const ReceivedImage& image) {
    if (isReceiveOnly(mode_)) return;
    if (image.newImage) {
        archiveSstv(); receivedSstv_ = {}; receivedRows_.reset(); receivedJpeg_.clear(); previewJpeg_.clear(); receivePreview_->setImage({});
    }
    receivedName_ = QString::fromStdString(image.filename);
    receiveProgress_->setValue(static_cast<int>(image.blocks * 100 / image.totalBlocks));
    receiveInfo_->setText(QString("%1 · %2/%3 Blöcke%4").arg(receivedName_).arg(image.blocks).arg(image.totalBlocks)
        .arg(!image.complete && image.blocks == image.totalBlocks ? " · Abschlussprüfung" : ""));
    if (image.jpeg == previewJpeg_ && !image.complete) return;
    const auto picture = decodeReceivedJpeg(image.jpeg, image.complete);
    if (!picture.isNull()) {
        receivePreview_->setImage(picture); previewJpeg_ = image.jpeg;
        if (image.complete) {
            receivedJpeg_ = image.jpeg; receiveInfo_->setText("Empfang abgeschlossen");
            try { receiveInfo_->setToolTip("Automatisch gespeichert: " + archive_.saveJpeg(receivedName_, receivedJpeg_)); }
            catch (const std::exception& e) {
                // Keep the completed bytes independently of assembler duplicate
                // suppression and subsequent pictures. Retry without needing RF.
                const auto found = std::find_if(pendingJpegs_.begin(), pendingJpegs_.end(), [this](const auto& p) {
                    return p.name == receivedName_ && p.bytes == receivedJpeg_;
                });
                if (found == pendingJpegs_.end()) pendingJpegs_.push_back({receivedName_, receivedJpeg_});
                receiveInfo_->setToolTip("Bild bleibt im Speicher; automatischer Speicherversuch alle 2 Sekunden.");
                receiveInfo_->setText(QString::fromUtf8(e.what()));
            }
        }
    } else if (image.complete) receiveInfo_->setText(receivedName_ + " · Blöcke vollständig, JPEG nicht darstellbar");
}
void MainWindow::updateFormats() {
    const QSignalBlocker blocker(sizes_); sizes_->clear();
    if (isSstv(mode_)) {
        sizes_->addItem("320 × 256", QSize(320, 256));
        sizes_->setToolTip("Alle SSTV-Modi verwenden fest 320 × 256 Pixel.");
    } else {
        for (const auto size : {QSize(320, 240), QSize(640, 480), QSize(800, 600), QSize(1024, 768)})
            sizes_->addItem(QString("%1 × %2").arg(size.width()).arg(size.height()), size);
        sizes_->setCurrentIndex(std::clamp(settings_.value("image/sizeIndex", 1).toInt(), 0, 3));
        sizes_->setToolTip("Bildgröße der übertragenen JPEG-Datei; die Vorschau passt sich dem Fenster an.");
    }
    sizes_->setEnabled(!isSstv(mode_));
}
void MainWindow::receivedSstv(const SstvUpdate& update) {
    if (update.kind == SstvUpdate::Kind::Begin) {
        archiveSstv();
        receivedJpeg_.clear(); receivedRows_.reset(); receivedPartial_ = update.partial;
        receivedSstv_ = QImage(320, 256, QImage::Format_RGB888); receivedSstv_.fill(Qt::black);
        receivedName_ = QString("SSTV_%1_%2.png").arg(modeKey(update.modulation), QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss"));
        sstvDirty_ = false; receiveProgress_->setValue(0); receivePreview_->setImage(receivedSstv_);
    } else if (update.kind == SstvUpdate::Kind::Line) {
        if (receivedSstv_.isNull() || update.index >= 256) return;
        std::copy(update.rgb.begin(), update.rgb.end(), receivedSstv_.scanLine(int(update.index)));
        receivedRows_.set(update.index); sstvDirty_ = true; receivePreview_->setImage(receivedSstv_);
        receiveProgress_->setValue(int(receivedRows_.count() * 100 / 256));
    }
    const bool complete = update.kind == SstvUpdate::Kind::End && update.complete && !receivedPartial_ && receivedRows_.all();
    receiveInfo_->setText(complete ? QStringLiteral("Empfang abgeschlossen") : QString("%1 · %2/256%3").arg(modulationName(update.modulation)).arg(receivedRows_.count())
        .arg(update.kind == SstvUpdate::Kind::End ? " · unvollständig" : receivedPartial_ ? " · Teilempfang" : ""));
    if (update.kind == SstvUpdate::Kind::End) archiveSstv();
}
void MainWindow::showReceiveStatistics(const ReceiveStatus& status) {
    if (isSstv(status.modulation)) {
        receiveStatistics_->setText(QString("RX · AFC %1 Hz · Sync erkannt %2 · nicht erkannt %3")
            .arg(status.offset, 0, 'f', 1).arg(qulonglong(status.syncDetected)).arg(qulonglong(status.syncMissed)));
        receiveStatistics_->setToolTip("Zeilensyncs des aktuellen/letzten SSTV-Bildes.\n"
            "Im Freilauf dargestellte Zeilen ohne bestätigten Sync zählen als nicht erkannt.\n"
            "Bei VIS-Empfang werden am Bildende alle 256 erwarteten Zeilensyncs berücksichtigt;\n"
            "beim Teileinstieg ohne VIS nur die seit dem Einstieg ausgewerteten Zeilen.");
    } else if (isPskBeacon(status.modulation)) {
        receiveStatistics_->setText(QString("RX · %1 · Träger %2 Hz · OK %3 · FEC korr. %4")
            .arg(status.locked ? QStringLiteral("Sync") : status.state).arg(status.center, 0, 'f', 1)
            .arg(qulonglong(status.frames)).arg(qulonglong(status.fecCorrectedFrames)));
        receiveStatistics_->setToolTip("Nur CRC-/RS-geprüfte Rahmen. Überlappende Empfangsfenster werden zusammengeführt.\n"
            "BPSK/Manchester · 400 bit/s · erster Text nach 20–40 s · kein Pilotton erforderlich.");
    } else {
        const auto prefix = isBeacon(status.modulation) ? QString("RX · %1").arg(status.locked ? QStringLiteral("Sync") : status.state) : QStringLiteral("RX");
        receiveStatistics_->setText(prefix + QString(" · AFC %1 Hz · OK %2 · CRC %3 · FEC korr. %4")
            .arg(status.offset, 0, 'f', 1).arg(qulonglong(status.frames)).arg(qulonglong(status.crcFailures))
            .arg(qulonglong(status.fecCorrectedFrames)));
        receiveStatistics_->setToolTip(QString("Seit dem Start des Empfangs bzw. Eingangs-/Moduswechsel:\n"
            "Gültige Frames: %1\nCRC-Fehler nach FEC: %2\nFEC-korrigierte gültige Frames: %3\n"
            "Korrigierte RS-Bytes (einschließlich Paritätsbytes): %4\nNicht dekodierbare FEC-Kandidaten: %5\n"
            "Headerkandidaten insgesamt: %6\n"
            "Nicht dekodierbare FEC-Kandidaten umfassen auch falsche Headerkandidaten bei Suche/Rauschen und entsprechen nicht der Zahl verlorener Bildblöcke.")
            .arg(qulonglong(status.frames)).arg(qulonglong(status.crcFailures)).arg(qulonglong(status.fecCorrectedFrames))
            .arg(qulonglong(status.fecCorrectedBytes)).arg(qulonglong(status.fecFailures)).arg(qulonglong(status.headerCandidates)));
    }
}
void MainWindow::archiveSstv() {
    if (!sstvDirty_ || receivedSstv_.isNull() || receivedRows_.none()) return;
    try {
        receiveInfo_->setToolTip("Automatisch gespeichert: " + archive_.savePng(receivedName_, receivedSstv_));
        sstvDirty_ = false;
    } catch (const std::exception& e) { receiveInfo_->setText(QString::fromUtf8(e.what())); }
}
void MainWindow::showHelp() {
    if (aboutDialog_) { aboutDialog_->show(); aboutDialog_->raise(); aboutDialog_->activateWindow(); return; }
    aboutDialog_ = new AboutDialog(this); aboutDialog_->show();
}
void MainWindow::openBulletinArchive() {
    if (bulletinDialog_) { bulletinDialog_->show(); bulletinDialog_->raise(); bulletinDialog_->activateWindow(); return; }
    bulletinDialog_ = new BulletinDialog(beaconView_->currentBulletin(), this);
    bulletinDialog_->loadRequested = [this](const QString& id) { receiver_.loadBulletinArchive(id); };
    bulletinDialog_->show(); receiver_.listBulletinArchive();
}
void MainWindow::openReceiveFolder() {
    try {
        archive_.ensureDirectory();
        if (!QDesktopServices::openUrl(QUrl::fromLocalFile(archive_.directory())))
            receiveInfo_->setText("Empfangsordner konnte nicht geöffnet werden: " + archive_.directory());
    } catch (const std::exception& e) { receiveInfo_->setText(QString::fromUtf8(e.what())); }
}
void MainWindow::setup() {
    finishDeviceScan();
    QDialog dialog(this); dialog.setWindowTitle("Audio-Setup"); dialog.setMinimumWidth(500);
    auto* layout = new QVBoxLayout(&dialog); auto* form = new QFormLayout;
    auto* inputs = new QComboBox(&dialog); inputs->setObjectName("inputDevice");
    inputs->setMinimumContentsLength(25); inputs->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    auto populateInputs = [&] {
        const QSignalBlocker blocker(inputs); inputs->clear();
        inputs->addItem("System-Standard", QString::fromLatin1(systemDefaultAudioDevice));
        try {
            for (const auto& device : inputDevices_) {
                auto name = QString::fromStdString(device.name);
                if (device.name != device.id) name += " [" + QString::fromStdString(device.id) + "]";
                inputs->addItem(name, QString::fromStdString(device.id));
            }
        } catch (const std::exception& e) { receiveStatus_->setToolTip({}); receiveStatus_->setText(QString::fromUtf8(e.what())); }
        int selected = inputs->findData(QString::fromStdString(inputDevice_));
        if (selected < 0) { inputs->insertItem(0, QString::fromStdString(inputDevice_) + " (nicht verfügbar)", QString::fromStdString(inputDevice_)); selected = 0; }
        inputs->setCurrentIndex(selected); inputs->setEnabled(receiver_.available());
    };
    populateInputs(); form->addRow("Audioeingang", inputs);
    auto* row = new QHBoxLayout; auto* outputs = new QComboBox(&dialog); outputs->setObjectName("outputDevice");
    outputs->setMinimumContentsLength(25); outputs->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    auto* refresh = new QPushButton("Aktualisieren", &dialog); refresh->setObjectName("refreshDevices");
    row->addWidget(outputs, 1); row->addWidget(refresh); form->addRow("Audioausgang", row);
    auto populate = [&] {
        const QSignalBlocker blocker(outputs);
        outputs->clear();
        outputs->addItem("System-Standard", QString::fromLatin1(systemDefaultAudioDevice));
        for (const auto& d : devices_) {
            auto label = QString::fromStdString(d.name);
            if (d.name != d.id) label += " [" + QString::fromStdString(d.id) + "]";
            outputs->addItem(label, QString::fromStdString(d.id));
        }
        int selected = outputs->findData(QString::fromStdString(device_));
        if (selected < 0) { outputs->insertItem(0, QString::fromStdString(device_) + " (nicht verfügbar)", QString::fromStdString(device_)); selected = 0; }
        outputs->setCurrentIndex(selected);
    };
    populate();
    const bool running = transmitter_.running(); outputs->setEnabled(!running); refresh->setEnabled(!running);
    layout->addLayout(form);
    const auto description = isPskBeacon(mode_) ? QStringLiteral("48 kHz · Mono · BPSK/Manchester 400 bit/s · nur Empfang\nSignalmitte etwa 1500 Hz, automatische Trägersuche ±500 Hz.\nErste vollständige Texte nach 20–40 Sekunden; kein Pilotton erforderlich.") : isBeacon(mode_) ? QStringLiteral("48 kHz · Mono · 8APSK 7200 bit/s · nur Empfang\n"
        "Audioeingang auf die Bake abstimmen; vollständiges Signal samt Pilot erfassen.\n"
        "System-Standard wird beim Öffnen des Audiostreams aufgelöst.") :
        isSstv(mode_) ? QString("48 kHz · Mono · SSTV %1 · 320 × 256 Pixel\n100 % = angepasster SSTV-Maximalpegel (−12,5 dBFS Spitze).\nSync 1200 Hz · Bildsignal 1500–2300 Hz.").arg(modulationName(mode_)) :
        QString("%1 kHz · Mono · %2 %3 bit/s\n100 % = digitale Vollaussteuerung.\nDaten und %4-Hz-Referenzträger werden gemeinsam geregelt.")
        .arg(parameters(mode_).sampleRate / 1000., 0, 'f', 1).arg(modulationName(mode_)).arg(parameters(mode_).bitRate()).arg(TxModulator::pilotHz);
    auto* info = new QLabel(description, &dialog);
    info->setWordWrap(true); layout->addWidget(info);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog); layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    auto updateScanControls = [&] {
        const bool busy = deviceScan_.valid();
        refresh->setEnabled(!busy && !transmitter_.running());
        refresh->setText(busy ? "Suche…" : "Aktualisieren");
        outputs->setEnabled(!busy && !transmitter_.running());
        inputs->setEnabled(!busy && receiver_.available());
    };
    QTimer scanTimer; scanTimer.setInterval(10);
    bool scanWasPending = deviceScan_.valid();
    connect(refresh, &QPushButton::clicked, &dialog, [&] {
        requestDeviceScan(); scanWasPending = deviceScan_.valid(); updateScanControls();
    });
    connect(&scanTimer, &QTimer::timeout, &dialog, [&] {
        finishDeviceScan();
        if (scanWasPending && !deviceScan_.valid()) {
            populate(); populateInputs();
            if (!inputDeviceError_.isEmpty()) receiveStatus_->setToolTip(inputDeviceError_);
        }
        scanWasPending = deviceScan_.valid(); updateScanControls();
    });
    requestDeviceScan(); scanWasPending = true; updateScanControls(); scanTimer.start();
    connect(outputs, QOverload<int>::of(&QComboBox::currentIndexChanged), &dialog, [&](int index) {
        if (index < 0 || transmitter_.running()) return;
        device_ = outputs->itemData(index).toString().toStdString();
        settings_.setValue("audio/output", QString::fromStdString(device_)); showReady();
    });
    connect(inputs, QOverload<int>::of(&QComboBox::currentIndexChanged), &dialog, [&](int index) {
        if (index < 0) return;
        inputDevice_ = inputs->itemData(index).toString().toStdString(); settings_.setValue("audio/input", QString::fromStdString(inputDevice_));
        restartReceiver();
    });
    dialog.exec(); settings_.sync();
}
}
