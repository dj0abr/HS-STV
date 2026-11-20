// SPDX-License-Identifier: GPL-3.0-or-later
#include "main_window.hpp"
#include "image_panel.hpp"
#include "receive_monitor.hpp"
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include "mm_sstv/image_audio.hpp"
#include <QDialog>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFile>
#include <QDir>
#include <QDesktopServices>
#include <QLabel>
#include <QMimeData>
#include <QProgressBar>
#include <QPushButton>
#include <QSlider>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QUrl>
#include <array>
#include <stdexcept>
using namespace mm_sstv;
struct State {
    std::vector<AudioDevice> devices{{"other", "Other output"}, {"toQO100", "toQO100"}};
    std::string chosen, error;
    bool open = false, failStart = false, complete = false;
    unsigned starts = 0, sampleRate = 48000;
    AudioOutput::Render render;
};
class FakeOutput final : public AudioOutput {
public:
    explicit FakeOutput(State& state) : state_(state) {}
    std::vector<AudioDevice> devices() override { return state_.devices; }
    void start(const std::string& device, Render render, unsigned sampleRate) override {
        if (state_.failStart) throw std::runtime_error("Device unplugged");
        state_.sampleRate = sampleRate; state_.chosen = device; state_.open = true; state_.complete = false; state_.error.clear(); ++state_.starts;
        state_.render = std::move(render); std::array<float, 513> audio{}; state_.render(audio.data(), audio.size());
    }
    void stop() noexcept override { state_.open = false; state_.render = {}; }
    bool finished() const override { return state_.complete; }
    std::string error() const override { return state_.error; }
    std::uint64_t underruns() const override { return 0; }
private:
    State& state_;
};
struct CaptureState { bool open = false, failStart = false; unsigned starts = 0, sampleRate = 48000; AudioInput::Consume consume; std::string chosen, error; };
class FakeCapture final : public AudioInput {
public:
    explicit FakeCapture(CaptureState& state) : state_(state) {}
    std::vector<AudioDevice> devices() override { return {{"fromQO100", "fromQO100"}, {"other-input", "Other input"}}; }
    void start(const std::string& device, Consume consume, unsigned sampleRate) override {
        state_.sampleRate = sampleRate; ++state_.starts; if (state_.failStart) throw std::runtime_error("Input disconnected");
        state_.error.clear(); state_.open = true; state_.chosen = device; state_.consume = std::move(consume);
    }
    void stop() noexcept override { state_.open = false; state_.consume = {}; }
    std::string error() const override { return state_.error; }
private:
    CaptureState& state_;
};
QString picture(QTemporaryDir& dir) {
    QImage image(400, 200, QImage::Format_RGB32); image.fill(QColor(20, 40, 230));
    const auto path = dir.filePath("Rundfunkmuseum_Besichtigung_Cham_Radio_Sender.png"); if (!image.save(path)) throw std::runtime_error("Cannot write test image");
    return path;
}
class UiTest : public QObject {
    Q_OBJECT
    QString originalDirectory_;
    std::unique_ptr<QTemporaryDir> receiveRoot_;
    QUrl openedFolder_;
public slots:
    void captureFolderUrl(const QUrl& url) { openedFolder_ = url; }
private slots:
    void init() {
        originalDirectory_ = QDir::currentPath(); receiveRoot_ = std::make_unique<QTemporaryDir>();
        QVERIFY(receiveRoot_->isValid()); QVERIFY(QDir::setCurrent(receiveRoot_->path())); openedFolder_ = QUrl();
    }
    void cleanup() {
        QDesktopServices::unsetUrlHandler("file");
        QVERIFY(QDir::setCurrent(originalDirectory_)); receiveRoot_.reset();
    }
    void archiveNamesBytesAndWriteErrors() {
        ReceivedArchive archive; archive.ensureDirectory(); QVERIFY(QDir("rxpic").exists());
        QImage original(320, 256, QImage::Format_RGB32); original.fill(Qt::blue);
        const auto first = preparePicture(original, original.size());
        const auto saved = archive.saveJpeg("../../outside.jpg", first.jpeg);
        QCOMPARE(QFileInfo(saved).absolutePath(), QDir("rxpic").absolutePath());
        QFile file(saved); QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), QByteArray(reinterpret_cast<const char*>(first.jpeg.data()), int(first.jpeg.size())));
        QCOMPARE(archive.saveJpeg("../../outside.jpg", first.jpeg), saved);
        original.fill(Qt::green); const auto second = preparePicture(original, original.size());
        const auto other = archive.saveJpeg("../../outside.jpg", second.jpeg); QVERIFY(other != saved);
        QCOMPARE(QDir("rxpic").entryList({"*.jpg"}, QDir::Files).size(), 2);
        QVERIFY(QFileInfo::exists(saved));
        const auto png = archive.savePng("image.png", original); QCOMPARE(QImage(png), original);
        QFile blocked("blocked"); QVERIFY(blocked.open(QIODevice::WriteOnly)); blocked.write("x"); blocked.close();
        ReceivedArchive invalid(QDir::current().filePath("blocked/rxpic"));
        bool failed = false;
        try { invalid.saveJpeg("image.jpg", first.jpeg); } catch (const std::exception&) { failed = true; }
        QVERIFY(failed);
    }
    void openReceiveFolderUsesDesktopServices() {
        QTemporaryDir dir; QSettings settings(dir.filePath("prefs.ini"), QSettings::IniFormat); State state;
        MainWindow window(std::make_unique<FakeOutput>(state), settings);
        QVERIFY(window.windowTitle().startsWith("HS-STV")); QVERIFY(!window.findChild<QAction*>("saveReceived"));
        auto* action = window.findChild<QAction*>("openReceiveFolder"); QVERIFY(action); QVERIFY(action->isEnabled());
        QCOMPARE(action->text(), QString("Empfangsordner öffnen"));
        QDesktopServices::setUrlHandler("file", this, "captureFolderUrl"); action->trigger();
        QCOMPARE(openedFolder_, QUrl::fromLocalFile(QDir("rxpic").absolutePath()));
        QVERIFY(QDir(openedFolder_.toLocalFile()).exists());
    }
    void partialSstvSavedOnClose() {
        QTemporaryDir dir; QSettings settings(dir.filePath("prefs.ini"), QSettings::IniFormat); State state; CaptureState capture;
        settings.setValue("modem/modulation", "martin1");
        {
            MainWindow window(std::make_unique<FakeOutput>(state), settings, std::make_unique<FakeCapture>(capture));
            QTRY_VERIFY(capture.open);
            QImage picture(320, 256, QImage::Format_RGB32); picture.fill(QColor(20, 40, 230));
            SstvAudio signal(Modulation::Martin1, sstvRaster(picture)); std::array<float, 4800> buffer{};
            for (unsigned block = 0; block < 100; ++block) {
                const auto count = signal.render(buffer.data(), buffer.size(), 1);
                capture.consume(buffer.data(), count); QTest::qWait(3);
            }
            QTRY_VERIFY(window.findChild<QProgressBar*>("receiveProgress")->value() > 0);
            QCOMPARE(QDir("rxpic").entryList({"*.png"}, QDir::Files).size(), 0);
        }
        QCOMPARE(QDir("rxpic").entryList({"*.png"}, QDir::Files).size(), 1);
        QImage saved(QDir("rxpic").absoluteFilePath(QDir("rxpic").entryList({"*.png"}, QDir::Files).first()));
        QCOMPARE(saved.size(), QSize(320, 256)); QVERIFY(saved.pixelColor(160, 0).blue() > 210);
        QCOMPARE(saved.pixelColor(160, 255), QColor(Qt::black));
    }
    void defaultImageStartStopAndClose() {
        QTemporaryDir dir; QSettings settings(dir.filePath("prefs.ini"), QSettings::IniFormat); State state;
        {
            MainWindow window(std::make_unique<FakeOutput>(state), settings); window.show();
            auto* button = window.findChild<QPushButton*>("transmitButton"); QVERIFY(button); QVERIFY(!button->isEnabled());
            QVERIFY(window.loadImage(picture(dir))); QVERIFY(button->isEnabled());
            QVERIFY(window.findChild<QComboBox*>("modulation")->currentText().contains("6000 bit/s"));
            QTest::qWait(50);
            const auto screenshot = qEnvironmentVariable("MM_SSTV_SCREENSHOT", dir.filePath("window.png")); QVERIFY(window.grab().save(screenshot));
            QTest::mouseClick(button, Qt::LeftButton); QCOMPARE(state.chosen, std::string("toQO100"));
            QCOMPARE(button->text(), QString("STOP")); QVERIFY(state.open);
            QVERIFY(!window.findChild<QComboBox*>("imageSize")->isEnabled());
            QTest::mouseClick(button, Qt::LeftButton); QCOMPARE(button->text(), QString("Senden")); QVERIFY(!state.open);
            QTest::mouseClick(button, Qt::LeftButton); QVERIFY(state.open);
        }
        QVERIFY(!state.open); QCOMPARE(state.starts, 2u);
    }
    void liveLevelAndDisplayRange_data() {
        QTest::addColumn<int>("selectedMode");
        QTest::newRow("8APSK") << int(Modulation::Apsk8);
        QTest::newRow("QPSK") << int(Modulation::Qpsk4410);
        QTest::newRow("Martin1") << int(Modulation::Martin1);
        QTest::newRow("Scottie1") << int(Modulation::Scottie1);
        QTest::newRow("Martin2") << int(Modulation::Martin2);
        QTest::newRow("Scottie2") << int(Modulation::Scottie2);
    }
    void liveLevelAndDisplayRange() {
        QFETCH(int, selectedMode); const auto mode = Modulation(selectedMode);
        QTemporaryDir dir; QSettings settings(dir.filePath("prefs.ini"), QSettings::IniFormat); State state;
        settings.setValue("modem/modulation", modeKey(mode));
        {
            MainWindow window(std::make_unique<FakeOutput>(state), settings); window.resize(640, 440); window.show();
            auto* level = window.findChild<QSlider*>("outputLevel"); QVERIFY(level); QCOMPARE(level->orientation(), Qt::Vertical);
            auto* lower = window.findChild<QSlider*>("spectrumMin"); auto* upper = window.findChild<QSlider*>("spectrumMax"); QVERIFY(lower); QVERIFY(upper);
            QCOMPARE(lower->minimum(), -100); QCOMPARE(lower->maximum(), -60); QCOMPARE(lower->value(), -90);
            QCOMPARE(upper->minimum(), -50); QCOMPARE(upper->maximum(), 0); QCOMPARE(upper->value(), -20);
            QVERIFY(window.loadImage(picture(dir))); auto* button = window.findChild<QPushButton*>("transmitButton"); button->click();
            QVERIFY(state.open); QVERIFY(level->isEnabled()); QVERIFY(lower->isEnabled()); QVERIFY(upper->isEnabled());
            std::array<float, 4800> buffer{};
            level->setValue(0); QCOMPARE(state.render(buffer.data(), buffer.size()), buffer.size());
            QVERIFY(std::all_of(buffer.begin() + 2400, buffer.end(), [](float sample) { return std::abs(sample) < 1e-7; }));
            QCOMPARE(button->text(), QString("STOP"));
            level->setValue(100); QCOMPARE(state.render(buffer.data(), buffer.size()), buffer.size());
            QVERIFY(std::any_of(buffer.begin() + 2400, buffer.end(), [](float sample) { return std::abs(sample) > .1f; }));
            QCOMPARE(settings.value("audio/level").toInt(), 100);
            QCOMPARE(window.findChild<QLabel*>("outputLevelValue")->text(), QString("100 %"));
            auto* monitor = window.findChild<ReceiveMonitor*>("receiveMonitor"); QVERIFY(monitor);
            lower->setValue(-60); upper->setValue(-50); QCOMPARE(monitor->minimumDb(), -60); QCOMPARE(monitor->maximumDb(), -50);
            lower->setValue(-100); upper->setValue(0); QCOMPARE(monitor->minimumDb(), -100); QCOMPARE(monitor->maximumDb(), 0);
            QCOMPARE(settings.value("display/minDb").toInt(), -100); QCOMPARE(settings.value("display/maxDb").toInt(), 0);
            button->click(); QVERIFY(!state.open); QVERIFY(level->isEnabled());
            QTest::qWait(20); QCOMPARE(window.size(), QSize(640, 440));
            if (qEnvironmentVariableIsSet("MM_SSTV_SCREENSHOT"))
                QVERIFY(window.grab().save(qEnvironmentVariable("MM_SSTV_SCREENSHOT") + ".controls." + modeKey(mode) + ".png"));
        }
        MainWindow restored(std::make_unique<FakeOutput>(state), settings);
        QCOMPARE(restored.findChild<QSlider*>("outputLevel")->value(), 100);
        QCOMPARE(restored.findChild<QSlider*>("spectrumMin")->value(), -100);
        QCOMPARE(restored.findChild<QSlider*>("spectrumMax")->value(), 0);
        auto* monitor = restored.findChild<ReceiveMonitor*>("receiveMonitor");
        QCOMPARE(monitor->minimumDb(), -100); QCOMPARE(monitor->maximumDb(), 0);
    }
    void noImplicitFallback() {
        QTemporaryDir dir; QSettings settings(dir.filePath("prefs.ini"), QSettings::IniFormat); State state;
        state.devices = {{"other", "Other output"}};
        MainWindow window(std::make_unique<FakeOutput>(state), settings); QVERIFY(window.loadImage(picture(dir)));
        QVERIFY(!window.findChild<QPushButton*>("transmitButton")->isEnabled());
        QCOMPARE(state.starts, 0u);
    }
    void setupRefreshKeepsSelectionAndPersistsChoice() {
        QTemporaryDir dir; QSettings settings(dir.filePath("prefs.ini"), QSettings::IniFormat); State state;
        MainWindow window(std::make_unique<FakeOutput>(state), settings); window.show(); QVERIFY(window.loadImage(picture(dir)));
        QTimer::singleShot(30, &window, [&] {
            auto* dialog = window.findChild<QDialog*>(); QVERIFY(dialog);
            auto* outputs = dialog->findChild<QComboBox*>("outputDevice");
            QCOMPARE(outputs->currentData().toString(), QString("toQO100"));
            dialog->findChild<QPushButton*>("refreshDevices")->click();
            QCOMPARE(outputs->currentData().toString(), QString("toQO100"));
            QVERIFY(!dialog->findChild<QSlider*>("outputLevel"));
            window.findChild<QSlider*>("outputLevel")->setValue(60);
            outputs->setCurrentIndex(outputs->findData("other")); dialog->reject();
        });
        window.findChild<QAction*>("audioSetup")->trigger();
        QCOMPARE(settings.value("audio/level").toInt(), 60); QCOMPARE(settings.value("audio/output").toString(), QString("other"));
        window.findChild<QPushButton*>("transmitButton")->click(); QCOMPARE(state.chosen, std::string("other"));
    }
    void audioFailureReturnsToSend() {
        QTemporaryDir dir; QSettings settings(dir.filePath("prefs.ini"), QSettings::IniFormat); State state;
        MainWindow window(std::make_unique<FakeOutput>(state), settings); QVERIFY(window.loadImage(picture(dir)));
        auto* button = window.findChild<QPushButton*>("transmitButton"); button->click(); state.error = "Audio connection lost";
        QTRY_COMPARE(button->text(), QString("Senden")); QVERIFY(!state.open);
        QCOMPARE(window.findChild<QLabel*>("statusText")->text(), QString::fromStdString(state.error));
    }
    void failedStartIsRecoverable() {
        QTemporaryDir dir; QSettings settings(dir.filePath("prefs.ini"), QSettings::IniFormat); State state;
        state.failStart = true; MainWindow window(std::make_unique<FakeOutput>(state), settings); QVERIFY(window.loadImage(picture(dir)));
        auto* button = window.findChild<QPushButton*>("transmitButton"); button->click();
        QCOMPARE(button->text(), QString("Senden")); QVERIFY(!state.open);
        state.failStart = false; button->click(); QCOMPARE(button->text(), QString("STOP"));
    }
    void finiteTransmissionCompletes() {
        QTemporaryDir dir; QSettings settings(dir.filePath("prefs.ini"), QSettings::IniFormat); State state;
        MainWindow window(std::make_unique<FakeOutput>(state), settings); QVERIFY(window.loadImage(picture(dir)));
        auto* button = window.findChild<QPushButton*>("transmitButton"); button->click();
        std::array<float, 4096> samples{}; unsigned blocks = 0;
        while (state.render(samples.data(), samples.size())) { QVERIFY(++blocks < 1000); }
        state.complete = true;
        QTRY_COMPARE(button->text(), QString("Senden")); QVERIFY(!state.open);
        QCOMPARE(window.findChild<QProgressBar*>("sendProgress")->value(), 100);
    }
    void imageScaleLetterboxAndBudget() {
        QImage image(400, 200, QImage::Format_RGB32); image.fill(QColor(20, 40, 230));
        const auto prepared = preparePicture(image, QSize(640, 480));
        QCOMPARE(prepared.preview.size(), QSize(640, 480)); QVERIFY(prepared.preview.pixelColor(320, 10).red() > 245);
        QVERIFY(prepared.preview.pixelColor(320, 240).blue() > 200);
        QImage noise(1024, 768, QImage::Format_RGB32); std::mt19937 random(42);
        for (int y = 0; y < noise.height(); ++y) for (int x = 0; x < noise.width(); ++x) noise.setPixel(x, y, random() | 0xff000000);
        const auto compressed = preparePicture(noise, noise.size());
        QVERIFY(compressed.jpeg.size() <= 200000); QVERIFY(compressed.quality < 90);
    }
    void dragDropAndFormatSelection() {
        QTemporaryDir dir; QSettings settings(dir.filePath("prefs.ini"), QSettings::IniFormat); State state;
        MainWindow window(std::make_unique<FakeOutput>(state), settings); window.show();
        auto* panel = window.findChild<ImagePanel*>("imagePreview"); QVERIFY(panel);
        QMimeData mime; mime.setUrls({QUrl::fromLocalFile(picture(dir))});
        QDragEnterEvent enter(QPoint(10, 10), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(panel, &enter); QVERIFY(enter.isAccepted());
        QDropEvent drop(QPointF(10, 10), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(panel, &drop); QVERIFY(drop.isAccepted());
        auto* sizes = window.findChild<QComboBox*>("imageSize"); sizes->setCurrentIndex(0);
        QVERIFY(window.findChild<QLabel*>("imageInfo")->text().contains("320 × 240"));
        QVERIFY(window.findChild<QPushButton*>("transmitButton")->isEnabled());
    }
    void receiveImageWhileSenderOperates() {
        QTemporaryDir dir; QSettings settings(dir.filePath("prefs.ini"), QSettings::IniFormat); State state; CaptureState capture;
        settings.setValue("receive/enabled", false); // Old pause preference must no longer disable reception.
        {
            MainWindow window(std::make_unique<FakeOutput>(state), settings, std::make_unique<FakeCapture>(capture)); window.show();
            QVERIFY(window.loadImage(picture(dir))); QTRY_VERIFY(capture.open); QCOMPARE(capture.chosen, std::string("fromQO100"));
            auto* send = window.findChild<QPushButton*>("transmitButton"); send->click(); QVERIFY(state.open); QVERIFY(capture.open);
            QImage image(400, 200, QImage::Format_RGB32); image.fill(QColor(20, 40, 230));
            const auto prepared = preparePicture(image, QSize(640, 480)); ImageAudio signal(prepareImage(prepared.jpeg, "Rundfunkmuseum_Besichtigung_Cham_Radio_Sender.jpg"));
            std::array<float, 4800> buffer{};
            while (const auto count = signal.render(buffer.data(), buffer.size(), .6f)) {
                QVERIFY(capture.open); capture.consume(buffer.data(), count);
                std::array<float, 4800> txBuffer{}; state.render(txBuffer.data(), count); QTest::qWait(2);
            }
            QTRY_COMPARE_WITH_TIMEOUT(window.findChild<QProgressBar*>("receiveProgress")->value(), 100, 5000);
            QVERIFY(window.findChild<QAction*>("openReceiveFolder")->isEnabled());
            QVERIFY(window.findChild<QLabel*>("receivedPreview")->text().isEmpty()); QVERIFY(state.open);
            QTRY_COMPARE(window.findChild<QLabel*>("receiveInfo")->text(), QString("Empfang abgeschlossen"));
            QTRY_VERIFY(window.findChild<QLabel*>("receiveStatistics")->text().contains("FEC korr."));
            QTRY_COMPARE(QDir("rxpic").entryList({"*.jpg"}, QDir::Files).size(), 1);
            QFile saved(QDir("rxpic").absoluteFilePath(QDir("rxpic").entryList({"*.jpg"}, QDir::Files).first()));
            QVERIFY(saved.open(QIODevice::ReadOnly));
            QCOMPARE(saved.readAll(), QByteArray(reinterpret_cast<const char*>(prepared.jpeg.data()), int(prepared.jpeg.size())));
            if (qEnvironmentVariableIsSet("MM_SSTV_SCREENSHOT")) {
                QVERIFY(window.grab().save(qEnvironmentVariable("MM_SSTV_SCREENSHOT")));
                window.resize(640, 440); QTest::qWait(30);
                QVERIFY(window.grab().save(qEnvironmentVariable("MM_SSTV_SCREENSHOT") + ".small.png"));
            }
            send->click(); QVERIFY(!state.open); QVERIFY(capture.open);
            // Input changes keep the always-on receiver active without a checkbox.
            QTimer::singleShot(30, &window, [&] {
                auto* dialog = window.findChild<QDialog*>(); QVERIFY(dialog);
                auto* inputs = dialog->findChild<QComboBox*>("inputDevice");
                inputs->setCurrentIndex(inputs->findData("other-input")); dialog->reject();
            });
            window.findChild<QAction*>("audioSetup")->trigger();
            QVERIFY(capture.open); QCOMPARE(capture.chosen, std::string("other-input"));
        }
        QVERIFY(!capture.open);
    }
    void qpskSelectionAndNativeRateRoundTrip() {
        QTemporaryDir dir; QSettings settings(dir.filePath("prefs.ini"), QSettings::IniFormat); State state; CaptureState capture;
        MainWindow window(std::make_unique<FakeOutput>(state), settings, std::make_unique<FakeCapture>(capture)); window.show();
        QTRY_VERIFY(capture.open); QVERIFY(window.loadImage(picture(dir)));
        auto* modes = window.findChild<QComboBox*>("modulation"); QVERIFY(modes); QCOMPARE(modes->currentIndex(), 0);
        modes->setCurrentIndex(1); QCOMPARE(capture.sampleRate, 44100u); QVERIFY(capture.open);
        QCOMPARE(settings.value("modem/modulation").toString(), QString("qpsk4410"));
        auto* send = window.findChild<QPushButton*>("transmitButton"); send->click();
        QCOMPARE(state.sampleRate, 44100u); QVERIFY(!modes->isEnabled());
        std::array<float, 4410> buffer{};
        while (const auto count = state.render(buffer.data(), buffer.size())) { capture.consume(buffer.data(), count); QTest::qWait(2); }
        state.complete = true;
        QTRY_COMPARE_WITH_TIMEOUT(window.findChild<QProgressBar*>("receiveProgress")->value(), 100, 5000);
        QTRY_COMPARE(send->text(), QString("Senden")); QVERIFY(modes->isEnabled()); QVERIFY(capture.open);
        QVERIFY(window.findChild<QAction*>("openReceiveFolder")->isEnabled());
        if (qEnvironmentVariableIsSet("MM_SSTV_SCREENSHOT")) {
            window.resize(640, 440); QTest::qWait(30);
            QVERIFY(window.grab().save(qEnvironmentVariable("MM_SSTV_SCREENSHOT") + ".qpsk.png"));
        }
        modes->setCurrentIndex(0); QCOMPARE(capture.sampleRate, 48000u); QVERIFY(capture.open);
        QCOMPARE(settings.value("modem/modulation").toString(), QString("8apsk"));
    }
    void sstvModesAndProgressiveReceive_data() {
        QTest::addColumn<int>("selectedMode");
        QTest::newRow("Martin1") << int(Modulation::Martin1);
        QTest::newRow("Scottie1") << int(Modulation::Scottie1);
        QTest::newRow("Martin2") << int(Modulation::Martin2);
        QTest::newRow("Scottie2") << int(Modulation::Scottie2);
    }
    void sstvModesAndProgressiveReceive() {
        QFETCH(int, selectedMode);
        const auto mode = Modulation(selectedMode);
        QTemporaryDir dir; QSettings settings(dir.filePath("prefs.ini"), QSettings::IniFormat); State state; CaptureState capture;
        settings.setValue("image/sizeIndex", 2);
        MainWindow window(std::make_unique<FakeOutput>(state), settings, std::make_unique<FakeCapture>(capture));
        window.resize(640, 440); window.show(); QTRY_VERIFY(capture.open); QVERIFY(window.loadImage(picture(dir)));
        auto* modes = window.findChild<QComboBox*>("modulation");
        auto* sizes = window.findChild<QComboBox*>("imageSize");
        auto* send = window.findChild<QPushButton*>("transmitButton");
        modes->setCurrentIndex(modes->findData(selectedMode));
        QVERIFY(capture.open); QCOMPARE(capture.sampleRate, 48000u);
        QCOMPARE(settings.value("modem/modulation").toString(), QString(modeKey(mode)));
        QCOMPARE(sizes->currentData().toSize(), QSize(320, 256)); QVERIFY(!sizes->isEnabled());
        QCOMPARE(settings.value("image/sizeIndex").toInt(), 2);
        QVERIFY(window.findChild<QLabel*>("imageInfo")->text().contains("320 × 256"));
        send->click(); QCOMPARE(send->text(), QString("STOP")); QCOMPARE(state.sampleRate, 48000u);
        QVERIFY(!modes->isEnabled()); QVERIFY(capture.open);
        send->click(); QVERIFY(!state.open); QVERIFY(capture.open); QVERIFY(!sizes->isEnabled());
        // Start a fresh complete signal and pass the actual TX samples through the real RX subprocess.
        send->click(); bool progressive = false; std::array<float, 4800> buffer{};
        auto* receive = window.findChild<QProgressBar*>("receiveProgress");
        while (const auto count = state.render(buffer.data(), buffer.size())) {
            // Simulate receiver attenuation of the calibrated SSTV output.
            for (std::size_t i = 0; i < count; ++i) buffer[i] *= .03f;
            capture.consume(buffer.data(), count); QTest::qWait(3);
            QVERIFY2(capture.open, qPrintable(window.findChild<QLabel*>("receiveStatus")->text()));
            if (receive->value() > 0 && receive->value() < 100) {
                progressive = true; QVERIFY(window.findChild<QAction*>("openReceiveFolder")->isEnabled());
            }
        }
        state.complete = true;
        QTRY_COMPARE_WITH_TIMEOUT(receive->value(), 100, 5000); QVERIFY(progressive);
        QTRY_COMPARE(window.findChild<QLabel*>("receiveInfo")->text(), QString("Empfang abgeschlossen"));
        QTRY_VERIFY(window.findChild<QLabel*>("receiveStatistics")->text().contains("Sync erkannt"));
        QTRY_COMPARE(send->text(), QString("Senden")); QVERIFY(capture.open); QVERIFY(!sizes->isEnabled());
        QTRY_COMPARE(QDir("rxpic").entryList({"*.png"}, QDir::Files).size(), 1);
        const auto saved = QDir("rxpic").absoluteFilePath(QDir("rxpic").entryList({"*.png"}, QDir::Files).first());
        QImage received(saved); QCOMPARE(received.size(), QSize(320, 256));
        QVERIFY(received.pixelColor(160, 5).red() > 240); QVERIFY(received.pixelColor(160, 128).blue() > 210);
        if (qEnvironmentVariableIsSet("MM_SSTV_SCREENSHOT")) {
            QVERIFY(window.grab().save(qEnvironmentVariable("MM_SSTV_SCREENSHOT") + "." + modeKey(mode) + ".png"));
        }
        modes->setCurrentIndex(0); QVERIFY(sizes->isEnabled()); QCOMPARE(sizes->currentIndex(), 2);
        QCOMPARE(sizes->currentData().toSize(), QSize(800, 600));
    }
    void savedSstvModeRestored() {
        QTemporaryDir dir; QSettings settings(dir.filePath("prefs.ini"), QSettings::IniFormat); State state;
        settings.setValue("modem/modulation", "scottie1");
        MainWindow window(std::make_unique<FakeOutput>(state), settings);
        QCOMPARE(window.findChild<QComboBox*>("modulation")->currentData().toInt(), int(Modulation::Scottie1));
        QCOMPARE(window.findChild<QComboBox*>("imageSize")->currentData().toSize(), QSize(320, 256));
        QVERIFY(window.loadImage(picture(dir))); window.findChild<QPushButton*>("transmitButton")->click(); QVERIFY(state.open);
    }
    void receiveRetriesAfterInputFailure() {
        QTemporaryDir dir; QSettings settings(dir.filePath("prefs.ini"), QSettings::IniFormat); State state; CaptureState capture;
        capture.failStart = true;
        MainWindow window(std::make_unique<FakeOutput>(state), settings, std::make_unique<FakeCapture>(capture));
        QTRY_COMPARE(capture.starts, 1u); QVERIFY(!capture.open);
        capture.failStart = false; QTRY_VERIFY_WITH_TIMEOUT(capture.open, 5000); QCOMPARE(capture.starts, 2u);
        QVERIFY(window.loadImage(picture(dir))); window.findChild<QPushButton*>("transmitButton")->click(); QVERIFY(state.open);
        capture.error = "Input disconnected"; QTRY_VERIFY(!capture.open); QVERIFY(state.open);
        QTRY_VERIFY_WITH_TIMEOUT(capture.open, 5000); QCOMPARE(capture.starts, 3u); QVERIFY(state.open);
    }

};
QTEST_MAIN(UiTest)
#include "test_ui.moc"
