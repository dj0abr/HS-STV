// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "mm_sstv/transmitter.hpp"
#include "image_codec.hpp"
#include "receiver.hpp"
#include <QMainWindow>
#include <QSettings>
#include <optional>
class QLabel;
class QPushButton;
class QComboBox;
class QAction;
class QProgressBar;
class QCheckBox;
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
    void startTest(bool validFrames);
    void poll();
    void updateButton();
    void showReady();
    void prepare();
    void chooseImage();
    void restartReceiver();
    void receivedImage(const ReceivedImage& image);
    void saveReceived();
    bool deviceAvailable() const;
    Transmitter transmitter_;
    QSettings& settings_;
    Receiver receiver_;
    std::string inputDevice_;
    std::vector<std::uint8_t> receivedJpeg_;
    QString receivedName_;
    std::size_t previewBytes_ = 0;
    ReceiveMonitor* monitor_ = nullptr;
    ImagePanel* receivePreview_ = nullptr;
    QLabel* receiveStatus_ = nullptr;
    QLabel* receiveInfo_ = nullptr;
    QProgressBar* receiveProgress_ = nullptr;
    QCheckBox* receiveCheck_ = nullptr;
    QAction* saveReceived_ = nullptr;
    std::vector<AudioDevice> devices_;
    std::string device_;
    unsigned level_ = 35;
    QImage original_;
    QString filename_;
    std::optional<ImageTransmission> image_;
    ImagePanel* preview_ = nullptr;
    QComboBox* sizes_ = nullptr;
    QPushButton* start_ = nullptr;
    QLabel* status_ = nullptr;
    QLabel* imageInfo_ = nullptr;
    QProgressBar* progress_ = nullptr;
    QAction* open_ = nullptr;
    QAction* berTest_ = nullptr;
    QAction* randomTest_ = nullptr;
};
}
