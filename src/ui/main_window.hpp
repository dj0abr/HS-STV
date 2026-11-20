// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "mm_sstv/transmitter.hpp"
#include "image_codec.hpp"
#include "receiver.hpp"
#include "received_archive.hpp"
#include <QMainWindow>
#include <QSettings>
#include <optional>
#include <bitset>
class QLabel;
class QPushButton;
class QComboBox;
class QCheckBox;
class QAction;
class QProgressBar;
class QTimer;
namespace mm_sstv {
class ImagePanel;
class ReceiveMonitor;
class MainWindow final : public QMainWindow {
public:
    MainWindow(std::unique_ptr<AudioOutput> output, QSettings& settings, std::unique_ptr<AudioInput> input = nullptr);
    ~MainWindow() override;
    bool loadImage(const QString& filename);
private:
    void refreshDevices();
    void setup();
    void toggle();
    void poll();
    void updateButton();
    void showReady();
    void prepare();
    void chooseImage();
    void editTextOverlay();
    void refreshOverlayChoice();
    void restartReceiver();
    void receivedImage(const ReceivedImage& image);
    void openReceiveFolder();
    void archiveSstv();
    void receivedSstv(const SstvUpdate& update);
    void showReceiveStatistics(const ReceiveStatus& status);
    void updateFormats();
    bool deviceAvailable() const;
    Transmitter transmitter_;
    QSettings& settings_;
    Receiver receiver_;
    ReceivedArchive archive_;
    bool sstvDirty_ = false;
    std::string inputDevice_;
    std::vector<std::uint8_t> receivedJpeg_;
    QString receivedName_;
    QImage receivedSstv_;
    std::bitset<256> receivedRows_;
    bool receivedPartial_ = false;
    std::size_t previewBytes_ = 0;
    ReceiveMonitor* monitor_ = nullptr;
    ImagePanel* receivePreview_ = nullptr;
    QLabel* receiveStatus_ = nullptr;
    QLabel* receiveInfo_ = nullptr;
    QLabel* receiveStatistics_ = nullptr;
    QProgressBar* receiveProgress_ = nullptr;
    QTimer* receiveRetry_ = nullptr;
    std::vector<AudioDevice> devices_;
    std::string device_;
    unsigned level_ = 35;
    TextOverlay overlay_;
    QComboBox* overlaySlots_ = nullptr;
    QPushButton* overlayEdit_ = nullptr;
    QAction* overlayAction_ = nullptr;
    QImage original_;
    QString filename_;
    std::optional<ImageTransmission> image_;
    std::unique_ptr<SstvImage> sstvImage_;
    ImagePanel* preview_ = nullptr;
    Modulation mode_ = Modulation::Apsk8;
    QComboBox* modes_ = nullptr;
    QCheckBox* waterfallId_ = nullptr;
    QComboBox* sizes_ = nullptr;
    QPushButton* start_ = nullptr;
    QLabel* status_ = nullptr;
    QLabel* imageInfo_ = nullptr;
    QProgressBar* progress_ = nullptr;
    QAction* open_ = nullptr;
};
}
