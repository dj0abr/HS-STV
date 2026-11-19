// SPDX-License-Identifier: GPL-3.0-or-later
#include "main_window.hpp"
#include "image_panel.hpp"
#include "receive_monitor.hpp"
#include <QFileDialog>
#include <QFileInfo>
#include <QCheckBox>
#include <QSaveFile>
#include <QStandardPaths>
#include <QImageReader>
#include <QProgressBar>
#include <QAction>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenuBar>
#include <QPushButton>
#include <QSlider>
#include <QSignalBlocker>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>

namespace mm_sstv {
MainWindow::MainWindow(std::unique_ptr<AudioOutput> output, QSettings& settings, std::unique_ptr<AudioInput> input)
    : transmitter_(std::move(output)), settings_(settings), receiver_(std::move(input)) {
    setWindowTitle("mm-sstv · Bilder senden und empfangen"); setMinimumSize(820, 780); resize(1120, 900);
    inputDevice_ = settings_.value("audio/input", "fromQO100").toString().toStdString();
    device_ = settings_.value("audio/output", "toQO100").toString().toStdString();
    level_ = std::clamp(settings_.value("audio/level", 35).toInt(), 0, 100);
    transmitter_.setLevel(level_);
    auto* file = menuBar()->addMenu("Bild");
    open_ = file->addAction("Bild öffnen…"); open_->setObjectName("openImage");
    connect(open_, &QAction::triggered, this, [this] { chooseImage(); });
    saveReceived_ = file->addAction("Empfangsbild speichern…"); saveReceived_->setObjectName("saveReceived"); saveReceived_->setEnabled(false);
    connect(saveReceived_, &QAction::triggered, this, [this] { saveReceived(); });
    auto* menu = menuBar()->addMenu("Setup");
    auto* audio = menu->addAction("Audioausgang und Pegel…"); audio->setObjectName("audioSetup");
    connect(audio, &QAction::triggered, this, [this] { setup(); });
    auto* tests = menuBar()->addMenu("Test");
    berTest_ = tests->addAction("Gültige HS-Modem-Testframes senden"); berTest_->setObjectName("validFrameTest");
    randomTest_ = tests->addAction("Zufallsdaten senden"); randomTest_->setObjectName("randomTest");
    connect(berTest_, &QAction::triggered, this, [this] { startTest(true); });
    connect(randomTest_, &QAction::triggered, this, [this] { startTest(false); });
    auto* central = new QWidget(this); auto* layout = new QVBoxLayout(central);
    layout->setContentsMargins(20, 16, 20, 16); layout->setSpacing(10);
    auto* description = new QLabel(QString("8APSK · %1 bit/s · Referenzträger %2 Hz")
        .arg(TxModulator::bitRate).arg(TxModulator::pilotHz), central);
    description->setObjectName("modulationInfo");
    description->setAlignment(Qt::AlignCenter); layout->addWidget(description);
    preview_ = new ImagePanel(central); preview_->setObjectName("imagePreview");
    preview_->clicked = [this] { chooseImage(); };
    preview_->dropped = [this](const QString& path) { loadImage(path); };
    auto* panes = new QHBoxLayout; auto* sendPane = new QVBoxLayout; auto* receivePane = new QVBoxLayout;
    sendPane->addWidget(new QLabel("Zu senden", central)); sendPane->addWidget(preview_, 1);
    receiveCheck_ = new QCheckBox("Empfang", central); receiveCheck_->setObjectName("receiveEnabled");
    receiveCheck_->setChecked(settings_.value("receive/enabled", true).toBool() && receiver_.available());
    receiveCheck_->setEnabled(receiver_.available()); receivePane->addWidget(receiveCheck_);
    receivePreview_ = new ImagePanel(central, false); receivePreview_->setObjectName("receivedPreview"); receivePane->addWidget(receivePreview_, 1);
    receiveStatus_ = new QLabel("Empfang aus", central); receiveStatus_->setWordWrap(true); receiveStatus_->setObjectName("receiveStatus");
    receiveInfo_ = new QLabel(central); receiveInfo_->setWordWrap(true); receiveInfo_->setObjectName("receiveInfo");
    receiveProgress_ = new QProgressBar(central); receiveProgress_->setValue(0); receiveProgress_->setObjectName("receiveProgress");
    panes->addLayout(sendPane, 1); panes->addLayout(receivePane, 1); layout->addLayout(panes, 1);
    auto* metadata = new QHBoxLayout; auto* sendMeta = new QVBoxLayout; auto* receiveMeta = new QVBoxLayout;
    auto* formatRow = new QHBoxLayout; formatRow->addWidget(new QLabel("Bildgröße", central));
    sizes_ = new QComboBox(central); sizes_->setObjectName("imageSize");
    for (const auto size : {QSize(320, 240), QSize(640, 480), QSize(800, 600), QSize(1024, 768)})
        sizes_->addItem(QString("%1 × %2").arg(size.width()).arg(size.height()), size);
    sizes_->setCurrentIndex(std::clamp(settings_.value("image/sizeIndex", 1).toInt(), 0, 3));
    formatRow->addWidget(sizes_); formatRow->addStretch(); sendMeta->addLayout(formatRow);
    connect(sizes_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int index) {
        settings_.setValue("image/sizeIndex", index); prepare();
    });
    imageInfo_ = new QLabel(central); imageInfo_->setObjectName("imageInfo"); imageInfo_->setWordWrap(true); sendMeta->addWidget(imageInfo_);
    receiveMeta->addWidget(receiveStatus_); receiveMeta->addWidget(receiveInfo_); receiveMeta->addWidget(receiveProgress_);
    metadata->addLayout(sendMeta, 1); metadata->addLayout(receiveMeta, 1); layout->addLayout(metadata);
    monitor_ = new ReceiveMonitor(central); layout->addWidget(monitor_);
    progress_ = new QProgressBar(central); progress_->setObjectName("sendProgress"); progress_->setValue(0); layout->addWidget(progress_);
    start_ = new QPushButton("Senden", central); start_->setObjectName("transmitButton");
    start_->setMinimumSize(220, 54);
    auto font = start_->font(); font.setPointSize(20); font.setBold(true); start_->setFont(font);
    layout->addWidget(start_, 0, Qt::AlignHCenter);
    status_ = new QLabel(central); status_->setObjectName("statusText");
    status_->setWordWrap(true); status_->setAlignment(Qt::AlignCenter);
    status_->setTextInteractionFlags(Qt::TextSelectableByMouse); layout->addWidget(status_);
    setCentralWidget(central);
    connect(start_, &QPushButton::clicked, this, [this] { toggle(); });
    auto* timer = new QTimer(this); timer->setInterval(100);
    connect(timer, &QTimer::timeout, this, [this] { poll(); }); timer->start();
    receiver_.imageReceived = [this](const ReceivedImage& image) { receivedImage(image); };
    receiver_.statusChanged = [this](const ReceiveStatus& status) {
        monitor_->setStatus(status);
        QString text = status.state;
        if (status.pilot >= 0) text += QString(" · Pilot %1 Hz").arg(status.pilot, 0, 'f', 1);
        if (status.frames) text += QString("\nAFC %1 Hz · %2 gültige Frames").arg(status.offset, 0, 'f', 1).arg(qulonglong(status.frames));
        receiveStatus_->setText(text);
    };
    receiver_.failed = [this](const QString& text) {
        monitor_->clear("Empfang unterbrochen");
        receiveStatus_->setText(text); const QSignalBlocker blocker(receiveCheck_); receiveCheck_->setChecked(false);
    };
    connect(receiveCheck_, &QCheckBox::toggled, this, [this](bool enabled) { settings_.setValue("receive/enabled", enabled); restartReceiver(); });
    refreshDevices();
    if (receiver_.available()) QTimer::singleShot(0, this, [this] { restartReceiver(); });
}
MainWindow::~MainWindow() { transmitter_.stop(); receiver_.stop(); }
bool MainWindow::deviceAvailable() const {
    return std::any_of(devices_.begin(), devices_.end(), [this](const auto& d) { return d.id == device_; });
}
void MainWindow::updateButton() {
    const bool running = transmitter_.running();
    start_->setText(running ? "STOP" : "Senden");
    start_->setEnabled(running || (deviceAvailable() && image_.has_value()));
    sizes_->setEnabled(!running); preview_->setEditable(!running); open_->setEnabled(!running);
    berTest_->setEnabled(!running && deviceAvailable()); randomTest_->setEnabled(!running && deviceAvailable());
}
void MainWindow::showReady() {
    if (deviceAvailable()) status_->setText("Bereit · " + QString::fromStdString(device_) + " · " + QString::number(level_) + " %");
    else status_->setText("Ausgang „" + QString::fromStdString(device_) + "“ nicht verfügbar. Bitte unter Setup auswählen oder aktualisieren.");
    updateButton();
}
void MainWindow::refreshDevices() {
    try { devices_ = transmitter_.audio().devices(); showReady(); }
    catch (const std::exception& e) { devices_.clear(); status_->setText(QString::fromUtf8(e.what())); updateButton(); }
}
void MainWindow::toggle() {
    if (transmitter_.running()) { transmitter_.stop(); showReady(); return; }
    try {
        if (!image_) return;
        transmitter_.startImage(device_, *image_); progress_->setValue(0); updateButton();
        status_->setText("Sendet Bild · " + QString::fromStdString(device_));
    } catch (const std::exception& e) {
        updateButton(); status_->setText("Start fehlgeschlagen: " + QString::fromUtf8(e.what()));
    }
}
void MainWindow::poll() {
    if (!transmitter_.running()) return;
    const auto error = transmitter_.audio().error();
    if (!error.empty()) {
        transmitter_.stop(); updateButton(); status_->setText(QString::fromStdString(error));
    } else if (transmitter_.audio().finished()) {
        transmitter_.stop(); updateButton(); progress_->setValue(100); status_->setText("Sendung beendet.");
    } else if (const auto underruns = transmitter_.audio().underruns()) {
        status_->setText("Sendet · Audioaussetzer: " + QString::number(underruns) + " · Audioverbindung prüfen");
    }
    if (transmitter_.running()) progress_->setValue(transmitter_.progress());
}
void MainWindow::startTest(bool validFrames) {
    try {
        transmitter_.startTest(device_, validFrames); updateButton(); progress_->setValue(0);
        status_->setText(validFrames ? "Sendet gültige HS-Modem-Testframes" : "Sendet Zufallsdaten");
    } catch (const std::exception& e) { updateButton(); status_->setText(QString::fromUtf8(e.what())); }
}
void MainWindow::chooseImage() {
    const auto path = QFileDialog::getOpenFileName(this, "Bild öffnen", settings_.value("image/directory").toString(),
        "Bilder (*.jpg *.jpeg *.png *.bmp *.gif *.webp);;Alle Dateien (*)");
    if (!path.isEmpty()) loadImage(path);
}
bool MainWindow::loadImage(const QString& filename) {
    if (transmitter_.running()) return false;
    QImageReader reader(filename); reader.setAutoTransform(true);
    const auto size = reader.size();
    if (size.isValid() && qint64(size.width()) * size.height() > 40000000) {
        status_->setText("Bild ist zu groß zum Laden (maximal 40 Megapixel)."); return false;
    }
    const auto image = reader.read();
    if (image.isNull()) { status_->setText("Bild konnte nicht geladen werden: " + reader.errorString()); return false; }
    original_ = image; filename_ = QFileInfo(filename).fileName();
    settings_.setValue("image/directory", QFileInfo(filename).absolutePath()); prepare(); return image_.has_value();
}
void MainWindow::prepare() {
    image_.reset(); progress_->setValue(0);
    if (original_.isNull()) { updateButton(); return; }
    try {
        const auto picture = preparePicture(original_, sizes_->currentData().toSize());
        image_ = prepareImage(picture.jpeg, filename_.toUtf8().toStdString()); preview_->setImage(picture.preview);
        const auto seconds = (image_->audioFrames() + TxModulator::sampleRate - 1) / TxModulator::sampleRate;
        imageInfo_->setText(QString("%1 · %2 × %3 · JPEG %4 kB · ca. %5 s")
            .arg(QString::fromStdString(image_->filename)).arg(picture.preview.width()).arg(picture.preview.height())
            .arg(image_->jpegBytes / 1000., 0, 'f', 1).arg(seconds));
        showReady();
    } catch (const std::exception& e) { imageInfo_->clear(); status_->setText(QString::fromUtf8(e.what())); updateButton(); }
}
void MainWindow::restartReceiver() {
    monitor_->clear();
    if (!receiveCheck_->isChecked()) { receiver_.stop(); receiveStatus_->setText("Empfang aus"); return; }
    try { receiver_.start(inputDevice_); }
    catch (const std::exception& e) {
        receiveStatus_->setText(QString::fromUtf8(e.what())); const QSignalBlocker blocker(receiveCheck_); receiveCheck_->setChecked(false);
    }
}
void MainWindow::receivedImage(const ReceivedImage& image) {
    if (image.newImage) {
        receivedJpeg_.clear(); saveReceived_->setEnabled(false); previewBytes_ = 0; receivePreview_->setImage({});
    }
    receivedName_ = QString::fromStdString(image.filename);
    receiveProgress_->setValue(static_cast<int>(image.blocks * 100 / image.totalBlocks));
    receiveInfo_->setText(QString("%1 · %2/%3 Blöcke%4").arg(receivedName_).arg(image.blocks).arg(image.totalBlocks)
        .arg(image.complete ? " · vollständig" : ""));
    if (image.jpeg.size() <= previewBytes_ && !image.complete) return;
    const auto picture = decodeReceivedJpeg(image.jpeg, image.complete);
    if (!picture.isNull()) {
        receivePreview_->setImage(picture); previewBytes_ = image.jpeg.size();
        if (image.complete) { receivedJpeg_ = image.jpeg; saveReceived_->setEnabled(true); }
    } else if (image.complete) receiveInfo_->setText(receivedName_ + " · Blöcke vollständig, JPEG nicht darstellbar");
}
void MainWindow::saveReceived() {
    if (receivedJpeg_.empty()) return;
    const auto initial = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation) + "/" + receivedName_;
    const auto filename = QFileDialog::getSaveFileName(this, "Empfangsbild speichern", initial, "JPEG (*.jpg *.jpeg)");
    if (filename.isEmpty()) return;
    QSaveFile file(filename);
    if (!file.open(QIODevice::WriteOnly) || file.write(reinterpret_cast<const char*>(receivedJpeg_.data()), receivedJpeg_.size()) != qint64(receivedJpeg_.size()) || !file.commit())
        receiveInfo_->setText("Bild konnte nicht gespeichert werden: " + file.errorString());
}
void MainWindow::setup() {
    if (!transmitter_.running()) refreshDevices();
    QDialog dialog(this); dialog.setWindowTitle("Audio-Setup"); dialog.setMinimumWidth(500);
    auto* layout = new QVBoxLayout(&dialog); auto* form = new QFormLayout;
    auto* inputs = new QComboBox(&dialog); inputs->setObjectName("inputDevice");
    inputs->setMinimumContentsLength(25); inputs->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    auto populateInputs = [&] {
        const QSignalBlocker blocker(inputs); inputs->clear();
        try {
            for (const auto& device : receiver_.devices()) {
                auto name = QString::fromStdString(device.name);
                if (device.name != device.id) name += " [" + QString::fromStdString(device.id) + "]";
                inputs->addItem(name, QString::fromStdString(device.id));
            }
        } catch (const std::exception& e) { receiveStatus_->setText(QString::fromUtf8(e.what())); }
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
    auto* sliderRow = new QHBoxLayout;
    auto* slider = new QSlider(Qt::Horizontal, &dialog); slider->setObjectName("outputLevel");
    slider->setRange(0, 100); slider->setValue(level_);
    auto* percent = new QLabel(QString::number(level_) + " %", &dialog); percent->setMinimumWidth(45);
    sliderRow->addWidget(slider, 1); sliderRow->addWidget(percent); form->addRow("Sendepegel", sliderRow);
    layout->addLayout(form);
    auto* info = new QLabel(QString("%1 kHz · Mono · 8APSK %2 bit/s\n100 % = digitale Vollaussteuerung.\nDaten und %3-Hz-Referenzträger werden gemeinsam geregelt.")
        .arg(TxModulator::sampleRate / 1000).arg(TxModulator::bitRate).arg(TxModulator::pilotHz), &dialog);
    info->setWordWrap(true); layout->addWidget(info);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog); layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    connect(refresh, &QPushButton::clicked, &dialog, [&] { refreshDevices(); populate(); populateInputs(); });
    connect(outputs, QOverload<int>::of(&QComboBox::currentIndexChanged), &dialog, [&](int index) {
        if (index < 0 || transmitter_.running()) return;
        device_ = outputs->itemData(index).toString().toStdString();
        settings_.setValue("audio/output", QString::fromStdString(device_)); showReady();
    });
    connect(inputs, QOverload<int>::of(&QComboBox::currentIndexChanged), &dialog, [&](int index) {
        if (index < 0) return;
        inputDevice_ = inputs->itemData(index).toString().toStdString(); settings_.setValue("audio/input", QString::fromStdString(inputDevice_));
        if (receiveCheck_->isChecked()) restartReceiver();
    });
    connect(slider, &QSlider::valueChanged, &dialog, [&](int value) {
        level_ = static_cast<unsigned>(value); transmitter_.setLevel(level_);
        percent->setText(QString::number(value) + " %"); settings_.setValue("audio/level", value);
        if (!transmitter_.running()) showReady();
    });
    dialog.exec(); settings_.sync();
}
}
