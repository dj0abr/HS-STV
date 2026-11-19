// SPDX-License-Identifier: GPL-3.0-or-later
#include "main_window.hpp"
#include "image_panel.hpp"
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QCheckBox>
#include "mm_sstv/image_audio.hpp"
#include <QDialog>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFile>
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
    unsigned starts = 0;
    AudioOutput::Render render;
};
class FakeOutput final : public AudioOutput {
public:
    explicit FakeOutput(State& state) : state_(state) {}
    std::vector<AudioDevice> devices() override { return state_.devices; }
    void start(const std::string& device, Render render) override {
        if (state_.failStart) throw std::runtime_error("Device unplugged");
        state_.chosen = device; state_.open = true; state_.complete = false; state_.error.clear(); ++state_.starts;
        state_.render = std::move(render); std::array<float, 513> audio{}; state_.render(audio.data(), audio.size());
    }
    void stop() noexcept override { state_.open = false; state_.render = {}; }
    bool finished() const override { return state_.complete; }
    std::string error() const override { return state_.error; }
    std::uint64_t underruns() const override { return 0; }
private:
    State& state_;
};
struct CaptureState { bool open = false; AudioInput::Consume consume; std::string chosen; };
class FakeCapture final : public AudioInput {
public:
    explicit FakeCapture(CaptureState& state) : state_(state) {}
    std::vector<AudioDevice> devices() override { return {{"fromQO100", "fromQO100"}, {"other-input", "Other input"}}; }
    void start(const std::string& device, Consume consume) override { state_.open = true; state_.chosen = device; state_.consume = std::move(consume); }
    void stop() noexcept override { state_.open = false; state_.consume = {}; }
    std::string error() const override { return {}; }
private:
    CaptureState& state_;
};
QString picture(QTemporaryDir& dir) {
    QImage image(400, 200, QImage::Format_RGB32); image.fill(QColor(20, 40, 230));
    const auto path = dir.filePath("example.png"); if (!image.save(path)) throw std::runtime_error("Cannot write test image");
    return path;
}
class UiTest : public QObject {
    Q_OBJECT
private slots:
    void defaultImageStartStopAndClose() {
        QTemporaryDir dir; QSettings settings(dir.filePath("prefs.ini"), QSettings::IniFormat); State state;
        {
            MainWindow window(std::make_unique<FakeOutput>(state), settings); window.show();
            auto* button = window.findChild<QPushButton*>("transmitButton"); QVERIFY(button); QVERIFY(!button->isEnabled());
            QVERIFY(window.loadImage(picture(dir))); QVERIFY(button->isEnabled());
            QVERIFY(window.findChild<QLabel*>("modulationInfo")->text().contains("7200 bit/s"));
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
    void noImplicitFallback() {
        QTemporaryDir dir; QSettings settings(dir.filePath("prefs.ini"), QSettings::IniFormat); State state;
        state.devices = {{"other", "Other output"}};
        MainWindow window(std::make_unique<FakeOutput>(state), settings); QVERIFY(window.loadImage(picture(dir)));
        QVERIFY(!window.findChild<QPushButton*>("transmitButton")->isEnabled());
        QVERIFY(!window.findChild<QAction*>("validFrameTest")->isEnabled()); QCOMPARE(state.starts, 0u);
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
            dialog->findChild<QSlider*>("outputLevel")->setValue(60);
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
        {
            MainWindow window(std::make_unique<FakeOutput>(state), settings, std::make_unique<FakeCapture>(capture)); window.show();
            QVERIFY(window.loadImage(picture(dir))); QTRY_VERIFY(capture.open); QCOMPARE(capture.chosen, std::string("fromQO100"));
            auto* send = window.findChild<QPushButton*>("transmitButton"); send->click(); QVERIFY(state.open); QVERIFY(capture.open);
            QImage image(400, 200, QImage::Format_RGB32); image.fill(QColor(20, 40, 230));
            const auto prepared = preparePicture(image, QSize(640, 480)); ImageAudio signal(prepareImage(prepared.jpeg, "received.jpg"));
            std::array<float, 4800> buffer{};
            while (const auto count = signal.render(buffer.data(), buffer.size(), .6f)) {
                QVERIFY(capture.open); capture.consume(buffer.data(), count); QTest::qWait(2);
            }
            QTRY_COMPARE_WITH_TIMEOUT(window.findChild<QProgressBar*>("receiveProgress")->value(), 100, 5000);
            QVERIFY(window.findChild<QAction*>("saveReceived")->isEnabled());
            QVERIFY(window.findChild<QLabel*>("receivedPreview")->text().isEmpty()); QVERIFY(state.open);
            if (qEnvironmentVariableIsSet("MM_SSTV_SCREENSHOT")) {
                QVERIFY(window.grab().save(qEnvironmentVariable("MM_SSTV_SCREENSHOT")));
                window.resize(820, 780); QTest::qWait(30);
                QVERIFY(window.grab().save(qEnvironmentVariable("MM_SSTV_SCREENSHOT") + ".small.png"));
            }
            send->click(); QVERIFY(!state.open); QVERIFY(capture.open);
            window.findChild<QCheckBox*>("receiveEnabled")->setChecked(false); QVERIFY(!capture.open);
        }
        QVERIFY(!capture.open);
    }
    void validTestFramesWithoutImage() {
        QTemporaryDir dir; QSettings settings(dir.filePath("prefs.ini"), QSettings::IniFormat); State state;
        MainWindow window(std::make_unique<FakeOutput>(state), settings);
        window.findChild<QAction*>("validFrameTest")->trigger(); QVERIFY(state.open);
        auto* button = window.findChild<QPushButton*>("transmitButton"); QCOMPARE(button->text(), QString("STOP"));
        button->click(); QVERIFY(!state.open); QVERIFY(!button->isEnabled());
    }
};
QTEST_MAIN(UiTest)
#include "test_ui.moc"
