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
#include <future>
#include <QElapsedTimer>
#include <QPointer>
class QLabel;
class QPlainTextEdit;
class QPushButton;
class QComboBox;
class QCheckBox;
class QSlider;
class QAction;
class QProgressBar;
class QTimer;
class QStackedWidget;
namespace mm_sstv {
class ImagePanel;
class ReceiveMonitor;
class BeaconView;
class BulletinDialog;
class AboutDialog;
class MainWindow final : public QMainWindow {
public:
    MainWindow(std::unique_ptr<AudioOutput> output, QSettings& settings, std::unique_ptr<AudioInput> input = nullptr);
    ~MainWindow() override;
    bool loadImage(const QString& filename);
private:
    void refreshDevices(); // Initial enumeration, before the receiver starts.
    void requestDeviceScan();
    void finishDeviceScan();
    void retryJpegSaves();
    void setup();
    void showHelp();
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
    void openBulletinArchive();
    void archiveSstv();
    void receivedSstv(const SstvUpdate& update);
    void showReceiveStatistics(const ReceiveStatus& status);
    void updateFormats();
    void restoreDisplayRange();
    void syncDisplayRange();
    void saveDisplayRange();
    bool deviceAvailable() const;
    Transmitter transmitter_;
    QSettings& settings_;
    Receiver receiver_;
    std::optional<ReceiveStatus> lastReceiveStatus_;
    ReceivedArchive archive_;
    bool sstvDirty_ = false;
    std::string inputDevice_;
    std::vector<std::uint8_t> receivedJpeg_;
    QString receivedName_;
    QImage receivedSstv_;
    std::bitset<256> receivedRows_;
    bool receivedPartial_ = false;
    std::vector<std::uint8_t> previewJpeg_;
    struct PendingJpeg { QString name; std::vector<std::uint8_t> bytes; };
    std::vector<PendingJpeg> pendingJpegs_;
    QElapsedTimer jpegRetryAge_;
    struct DeviceScan {
        std::vector<AudioDevice> inputs, outputs;
        std::string inputError, outputError;
        bool updateOutputs = false;
    };
    std::future<DeviceScan> deviceScan_;
    std::vector<AudioDevice> inputDevices_;
    QString inputDeviceError_;
    ReceiveMonitor* monitor_ = nullptr;
    QSlider* minimumDb_ = nullptr;
    QSlider* maximumDb_ = nullptr;
    QLabel* minimumDbValue_ = nullptr;
    QLabel* maximumDbValue_ = nullptr;
    bool updatingDisplayRange_ = false;
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
    QStackedWidget* contentPages_ = nullptr;
    QPointer<BulletinDialog> bulletinDialog_;
    QPointer<AboutDialog> aboutDialog_;
    BeaconView* beaconView_ = nullptr;
    QPlainTextEdit* pskText_ = nullptr;
    QAction* open_ = nullptr;
};
}
