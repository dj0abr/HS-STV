// SPDX-License-Identifier: GPL-3.0-or-later
#include "main_window.hpp"
#include "image_panel.hpp"
#include "receive_monitor.hpp"
#include "overlay_editor.hpp"
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
#include <QLineEdit>
#include <QSpinBox>
#include <QCheckBox>
#include <QBuffer>
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
#include <atomic>
#include <chrono>
#include <thread>
#include <random>
#include <cmath>
using namespace mm_sstv;
struct State {
    std::vector<AudioDevice> devices{{"other", "Other output"}, {"toQO100", "toQO100"}};
    std::string chosen, error;
    bool open = false, failStart = false, complete = false;
    unsigned queryDelayMs = 0;
    unsigned starts = 0, sampleRate = 48000;
    AudioOutput::Render render;
};
class FakeOutput final : public AudioOutput {
public:
    explicit FakeOutput(State& state) : state_(state) {}
    std::vector<AudioDevice> devices() override {
        std::this_thread::sleep_for(std::chrono::milliseconds(state_.queryDelayMs)); return state_.devices;
    }
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
struct ContinuousInput final : public AudioInput {
    std::atomic<bool> active{false}; std::thread worker;
    std::atomic<unsigned> starts{0}, callbacks{0}; unsigned queryDelayMs = 0;
    ~ContinuousInput() override { stop(); }
    std::vector<AudioDevice> devices() override {
        std::this_thread::sleep_for(std::chrono::milliseconds(queryDelayMs)); return {{"fromQO100", "Synthetic input"}};
    }
    void start(const std::string&, Consume consume, unsigned rate) override {
        stop(); active = true; ++starts;
        worker = std::thread([this, consume=std::move(consume), rate] {
            std::vector<float> silence(rate/100); auto next = std::chrono::steady_clock::now();
            while (active) {
                consume(silence.data(), silence.size()); ++callbacks;
                next += std::chrono::milliseconds(10); std::this_thread::sleep_until(next);
            }
        });
    }
    void stop() noexcept override { active = false; if (worker.joinable()) worker.join(); }
    std::string error() const override { return {}; }
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
    void saveRetrySurvivesDuplicateReception() {
        QTemporaryDir dir; QSettings settings(dir.filePath("prefs.ini"), QSettings::IniFormat); State state; CaptureState capture;
        MainWindow window(std::make_unique<FakeOutput>(state), settings, std::make_unique<FakeCapture>(capture));
        QTRY_VERIFY(capture.open); QVERIFY(QDir().rmdir("rxpic"));
        QFile blocked("rxpic"); QVERIFY(blocked.open(QIODevice::WriteOnly)); blocked.write("blocked"); blocked.close();
        QImage picture(320,240,QImage::Format_RGB32); picture.fill(Qt::blue);
        const auto encoded = preparePicture(picture,picture.size()); const auto transmission = prepareImage(encoded.jpeg,"retry.jpg");
        const auto send = [&](const ImageTransmission& value) {
            ImageAudio signal(value); std::array<float,4800> buffer{};
            while (const auto n = signal.render(buffer.data(),buffer.size(),1)) { capture.consume(buffer.data(),n); QTest::qWait(5); }
        };
        send(transmission); auto* info = window.findChild<QLabel*>("receiveInfo");
        QTRY_VERIFY(info->text().contains("konnte nicht"));
        picture.fill(Qt::green); const auto other = preparePicture(picture,picture.size());
        send(prepareImage(other.jpeg,"second.jpg"));
        QTRY_VERIFY(info->text().contains("konnte nicht"));
        QVERIFY(QFile::remove("rxpic")); QVERIFY(QDir().mkdir("rxpic"));
        // No new RF or receiver callback is required for the pending write.
        QTRY_COMPARE_WITH_TIMEOUT(QDir("rxpic").entryList({"*.jpg"},QDir::Files).size(),2,4000);
        QTRY_COMPARE(info->text(),QString("Empfang abgeschlossen"));
        QList<QByteArray> files;
        for (const auto& name : QDir("rxpic").entryList({"*.jpg"},QDir::Files)) {
            QFile file(QDir("rxpic").filePath(name)); QVERIFY(file.open(QIODevice::ReadOnly)); files.push_back(file.readAll());
        }
        QVERIFY(files.contains(QByteArray(reinterpret_cast<const char*>(encoded.jpeg.data()),int(encoded.jpeg.size()))));
        QVERIFY(files.contains(QByteArray(reinterpret_cast<const char*>(other.jpeg.data()),int(other.jpeg.size()))));
        send(transmission); QTest::qWait(200);
        QCOMPARE(QDir("rxpic").entryList({"*.jpg"},QDir::Files).size(),2);
    }
    void deviceQueriesDoNotInterruptCapture_data() {
        QTest::addColumn<bool>("slowInput");
        QTest::newRow("output") << false; QTest::newRow("input") << true;
    }
    void deviceQueriesDoNotInterruptCapture() {
        QFETCH(bool, slowInput);
        QTemporaryDir dir; QSettings settings(dir.filePath("prefs.ini"), QSettings::IniFormat); State state;
        auto input = std::make_unique<ContinuousInput>(); auto* capture = input.get();
        MainWindow window(std::make_unique<FakeOutput>(state),settings,std::move(input));
        QTRY_VERIFY(capture->active.load()); QTest::qWait(200);
        if (slowInput) capture->queryDelayMs = 2600; else state.queryDelayMs = 2600;
        const auto startCount = capture->starts.load(); const auto callbacks = capture->callbacks.load();
        QTimer::singleShot(100,&window,[&] {
            auto* dialog = window.findChild<QDialog*>(); QVERIFY(dialog);
            QVERIFY(capture->active.load()); QVERIFY(!dialog->findChild<QPushButton*>("refreshDevices")->isEnabled());
        });
        QTimer::singleShot(3100,&window,[&] {
            auto* dialog = window.findChild<QDialog*>(); QVERIFY(dialog); dialog->reject();
            QVERIFY(capture->active.load()); QCOMPARE(capture->starts.load(),startCount);
            QVERIFY(capture->callbacks.load() > callbacks + 240);
            QVERIFY(!window.findChild<QLabel*>("receiveStatus")->text().contains("übergelaufen"));
            QVERIFY(dialog->findChild<QPushButton*>("refreshDevices")->isEnabled());
        });
        window.findChild<QAction*>("audioSetup")->trigger();
        QVERIFY(capture->active.load()); QCOMPARE(capture->starts.load(),startCount);
    }
    void closeSetupDuringDeviceQuery() {
        QTemporaryDir dir; QSettings settings(dir.filePath("prefs.ini"), QSettings::IniFormat); State state;
        auto input = std::make_unique<ContinuousInput>(); auto* capture = input.get();
        MainWindow window(std::make_unique<FakeOutput>(state),settings,std::move(input));
        QTRY_VERIFY(capture->active.load()); state.queryDelayMs = 500;
        QTimer::singleShot(50,&window,[&] { auto* dialog=window.findChild<QDialog*>(); QVERIFY(dialog); dialog->reject(); });
        window.findChild<QAction*>("audioSetup")->trigger();
        QTest::qWait(700); QVERIFY(capture->active.load()); QCOMPARE(capture->starts.load(),1u);
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
    void overlayModelRenderingAndStorage() {
        QTemporaryDir dir; QSettings settings(dir.filePath("prefs.ini"), QSettings::IniFormat);
        QImage base(320, 256, QImage::Format_RGB32); base.fill(QColor(80, 90, 100));
        TextOverlay overlay;
        QCOMPARE(drawTextOverlay(base, overlay), base);
        const auto plain = preparePicture(base, base.size());
        QByteArray previous; QBuffer buffer(&previous); QVERIFY(buffer.open(QIODevice::WriteOnly));
        QVERIFY(fitPicture(base, base.size()).save(&buffer, "JPEG", 90));
        QCOMPARE(QByteArray(reinterpret_cast<const char*>(plain.jpeg.data()), int(plain.jpeg.size())), previous);
        overlay.texts = {{"DJ0ABR", "DL1TEST", "CQ QO100", "73 AND THANKS", "TEST 3", "TEST 4", "TEST 5"}};
        overlay.selected = 1; const auto original = base.copy(); const auto rendered = drawTextOverlay(base, overlay);
        QVERIFY(rendered != base); QCOMPARE(base, original);
        auto alternate = overlay; alternate.texts[2] = "INACTIVE"; QCOMPARE(drawTextOverlay(base, alternate), rendered);
        alternate.texts[3] = "ACTIVE CHANGED"; QVERIFY(drawTextOverlay(base, alternate) != rendered);
        unsigned white = 0, black = 0;
        for (int y=0;y<rendered.height();++y) for (int x=0;x<rendered.width();++x) {
            white += rendered.pixelColor(x,y) == QColor(Qt::white); black += rendered.pixelColor(x,y) == QColor(Qt::black);
        }
        QVERIFY(white > 20); QVERIFY(black > 20);
        const auto small = overlayGeometry(overlay, QSize(320, 256)); const auto large = overlayGeometry(overlay, QSize(640, 512));
        QCOMPARE(large[0].bounds.x(), small[0].bounds.x()*2); QCOMPARE(large[0].bounds.y(), small[0].bounds.y()*2);
        overlay.texts[3] = QString(100, 'W'); overlay.layers[2].x = 1; overlay.layers[2].y = 1;
        for (const auto size : {QSize(320,256), QSize(640,480), QSize(1024,768)})
            for (const auto& item : overlayGeometry(overlay, size)) QVERIFY(QRectF(QPointF(0,0), QSizeF(size)).contains(item.bounds));
        overlay.layers[1].font = 2; overlay.layers[1].size = 32; overlay.layers[1].bold = false;
        saveTextOverlay(settings, overlay); const auto restored = loadTextOverlay(settings);
        QCOMPARE(restored.texts, overlay.texts); QCOMPARE(restored.selected, 1);
        QCOMPARE(restored.layers[1].font, 2); QCOMPARE(restored.layers[1].size, 32); QVERIFY(!restored.layers[1].bold);
        QCOMPARE(restored.layers[2].x, 1.); QCOMPARE(restored.layers[2].y, 1.);
        settings.setValue("overlay/selected", 99); settings.setValue("overlay/layer0/size", 500);
        QCOMPARE(loadTextOverlay(settings).selected, 4); QCOMPARE(loadTextOverlay(settings).layers[0].size, 48);
    }
    void overlayEditorKeyboardMouseAndFormatting() {
        TextOverlay original; original.texts[0] = "DJ0ABR";
        QImage base(320,256,QImage::Format_RGB32); base.fill(Qt::darkGray);
        OverlayEditor editor(base, original); editor.resize(600,380); editor.show(); QTest::qWait(30);
        editor.findChild<QLineEdit*>("overlayOtherCall")->setText("DL1TEST");
        editor.findChild<QLineEdit*>("overlayFreeText4")->setText("CQ QO100");
        QCOMPARE(editor.overlay().selected, 3);
        editor.findChild<QComboBox*>("overlayFont")->setCurrentIndex(2);
        editor.findChild<QSpinBox*>("overlaySize")->setValue(30);
        editor.findChild<QCheckBox*>("overlayBold")->setChecked(false);
        QCOMPARE(editor.overlay().layers[2].font, 2); QCOMPARE(editor.overlay().layers[2].size, 30); QVERIFY(!editor.overlay().layers[2].bold);
        editor.findChild<QComboBox*>("overlayLayer")->setCurrentIndex(0);
        auto* canvas = editor.findChild<OverlayPreview*>("overlayPreview"); QVERIFY(canvas);
        const double before = editor.overlay().layers[0].x;
        QTest::keyClick(canvas, Qt::Key_Right, Qt::ShiftModifier);
        QVERIFY(std::abs(editor.overlay().layers[0].x - before - 5./320) < 1e-6);
        const auto box = overlayGeometry(editor.overlay(), base.size())[0].bounds; const auto area = canvas->imageRect();
        const QPoint point = (area.topLeft() + QPointF(box.center().x()*area.width()/320, box.center().y()*area.height()/256)).toPoint();
        const auto old = editor.overlay().layers[0];
        QTest::mousePress(canvas, Qt::LeftButton, Qt::NoModifier, point);
        QTest::mouseMove(canvas, point+QPoint(20,10));
        QTest::mouseRelease(canvas, Qt::LeftButton, Qt::NoModifier, point+QPoint(20,10));
        QVERIFY(editor.overlay().layers[0].x > old.x); QVERIFY(editor.overlay().layers[0].y > old.y);
        editor.reject(); QCOMPARE(original.texts[1], QString()); QCOMPARE(original.layers[0].x, 12./320);
    }
    void overlayMainWindowSelectionCancelAndTxLock() {
        QTemporaryDir dir; QSettings settings(dir.filePath("prefs.ini"), QSettings::IniFormat); State state;
        settings.setValue("overlay/text0", "DJ0ABR");
        MainWindow window(std::make_unique<FakeOutput>(state), settings); window.resize(640,440); window.show();
        QVERIFY(window.loadImage(picture(dir))); auto* choices = window.findChild<QComboBox*>("overlayTextSlot"); QVERIFY(choices); QCOMPARE(choices->count(),5);
        auto* edit = window.findChild<QPushButton*>("overlayEditButton"); auto* action = window.findChild<QAction*>("editTextOverlay"); QVERIFY(edit); QVERIFY(action);
        QTimer::singleShot(30, &window, [&] {
            auto* dialog = window.findChild<QDialog*>("textOverlayEditor"); QVERIFY(dialog);
            dialog->findChild<QLineEdit*>("overlayOwnCall")->setText("CANCELLED"); dialog->reject();
        });
        action->trigger(); QCOMPARE(settings.value("overlay/text0").toString(), QString("DJ0ABR"));
        QTimer::singleShot(30, &window, [&] {
            auto* dialog = window.findChild<QDialog*>("textOverlayEditor"); QVERIFY(dialog);
            dialog->resize(600,380);
            dialog->findChild<QLineEdit*>("overlayOtherCall")->setText("DL1TEST");
            dialog->findChild<QLineEdit*>("overlayFreeText1")->setText("CQ QO100");
            dialog->findChild<QLineEdit*>("overlayFreeText5")->setText("73 AND THANKS");
            QTest::qWait(20);
            if(qEnvironmentVariableIsSet("MM_SSTV_SCREENSHOT")) QVERIFY(dialog->grab().save(qEnvironmentVariable("MM_SSTV_SCREENSHOT")+".editor.png"));
            dialog->accept();
        });
        edit->click(); QCOMPARE(choices->currentIndex(),4); QCOMPARE(settings.value("overlay/selected").toInt(),4);
        QCOMPARE(settings.value("overlay/text1").toString(),QString("DL1TEST"));
        const auto previous = window.findChild<QWidget*>("imagePreview")->grab().toImage(); choices->setCurrentIndex(0);
        QVERIFY(window.findChild<QWidget*>("imagePreview")->grab().toImage() != previous);
        auto* send = window.findChild<QPushButton*>("transmitButton"); send->click(); QVERIFY(state.open);
        QVERIFY(!edit->isEnabled()); QVERIFY(!action->isEnabled()); QVERIFY(!choices->isEnabled()); QVERIFY(window.findChild<QSlider*>("outputLevel")->isEnabled());
        choices->setCurrentIndex(4); QCOMPARE(choices->currentIndex(),0);
        send->click(); QVERIFY(edit->isEnabled()); QVERIFY(choices->isEnabled());
        QTest::qWait(20); QCOMPARE(window.size(),QSize(640,440));
        if(qEnvironmentVariableIsSet("MM_SSTV_SCREENSHOT")) QVERIFY(window.grab().save(qEnvironmentVariable("MM_SSTV_SCREENSHOT")+".overlay.png"));
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
            QCOMPARE(lower->minimum(), isSstv(mode) ? -6 : -100); QCOMPARE(lower->maximum(), isSstv(mode) ? 6 : -60); QCOMPARE(lower->value(), isSstv(mode) ? 0 : -90);
            QCOMPARE(upper->minimum(), isSstv(mode) ? -6 : -50); QCOMPARE(upper->maximum(), isSstv(mode) ? 6 : 0); QCOMPARE(upper->value(), isSstv(mode) ? 0 : -20);
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
            if (isSstv(mode)) {
                lower->setValue(-6); upper->setValue(6); QVERIFY(monitor->autoLevel());
                QCOMPARE(monitor->minimumDb(), -96); QCOMPARE(monitor->maximumDb(), -14);
                QCOMPARE(settings.value("display/sstvMinOffset").toInt(), -6);
                QCOMPARE(settings.value("display/sstvMaxOffset").toInt(), 6);
            } else {
                lower->setValue(-60); upper->setValue(-50); QCOMPARE(monitor->minimumDb(), -60); QCOMPARE(monitor->maximumDb(), -50);
                lower->setValue(-100); upper->setValue(0); QCOMPARE(monitor->minimumDb(), -100); QCOMPARE(monitor->maximumDb(), 0);
                QCOMPARE(settings.value("display/minDb").toInt(), -100); QCOMPARE(settings.value("display/maxDb").toInt(), 0);
            }
            button->click(); QVERIFY(!state.open); QVERIFY(level->isEnabled());
            QTest::qWait(20); QCOMPARE(window.size(), QSize(640, 440));
            if (qEnvironmentVariableIsSet("MM_SSTV_SCREENSHOT"))
                QVERIFY(window.grab().save(qEnvironmentVariable("MM_SSTV_SCREENSHOT") + ".controls." + modeKey(mode) + ".png"));
        }
        MainWindow restored(std::make_unique<FakeOutput>(state), settings);
        QCOMPARE(restored.findChild<QSlider*>("outputLevel")->value(), 100);
        QCOMPARE(restored.findChild<QSlider*>("spectrumMin")->value(), isSstv(mode) ? -6 : -100);
        QCOMPARE(restored.findChild<QSlider*>("spectrumMax")->value(), isSstv(mode) ? 6 : 0);
        auto* monitor = restored.findChild<ReceiveMonitor*>("receiveMonitor");
        QCOMPARE(monitor->minimumDb(), isSstv(mode) ? -96 : -100); QCOMPARE(monitor->maximumDb(), isSstv(mode) ? -14 : 0);
    }
    void automaticWaterfallControls() {
        QTemporaryDir dir; QSettings settings(dir.filePath("prefs.ini"), QSettings::IniFormat); State state; CaptureState capture;
        settings.setValue("modem/modulation", "martin1");
        settings.setValue("display/minDb", -85); settings.setValue("display/maxDb", -15);
        settings.setValue("display/sstvAutoLevel", false);
        settings.setValue("display/sstvMinDb", -120); settings.setValue("display/sstvMaxDb", -35);
        {
            MainWindow window(std::make_unique<FakeOutput>(state), settings, std::make_unique<FakeCapture>(capture));
            window.resize(640, 440); window.show(); QTRY_VERIFY(capture.open);
            QVERIFY(!window.findChild<QCheckBox*>("waterfallAutoLevel")); QVERIFY(!settings.contains("display/sstvAutoLevel"));
            auto* lower = window.findChild<QSlider*>("spectrumMin"); auto* upper = window.findChild<QSlider*>("spectrumMax");
            auto* monitor = window.findChild<ReceiveMonitor*>("receiveMonitor"); QVERIFY(monitor->autoLevel());
            QCOMPARE(lower->value(), 0); QCOMPARE(upper->value(), 0);
            QCOMPARE(lower->singleStep(), 1); QCOMPARE(lower->pageStep(), 1); QCOMPARE(upper->pageStep(), 1);
            std::mt19937 random(127); std::normal_distribution<float> noise(0, .0005f);
            std::array<float, 4800> audio{}; std::size_t time = 0;
            for (int block = 0; block < 40; ++block) {
                for (auto& sample : audio) { sample = noise(random) + float(.01 * std::sin(2 * 3.141592653589793 * 1800 * time / 48000)); ++time; }
                capture.consume(audio.data(), audio.size()); QTest::qWait(3);
            }
            QTRY_VERIFY(monitor->maximumDb() < -40); QVERIFY(monitor->minimumDb() < -90);
            QCOMPARE(lower->value(), 0); QCOMPARE(upper->value(), 0);
            QCOMPARE(window.findChild<QLabel*>("spectrumMinValue")->text(), QString("0 dB"));
            QCOMPARE(window.findChild<QLabel*>("spectrumMaxValue")->text(), QString("0 dB"));
            const int automaticMin = monitor->minimumDb(), automaticMax = monitor->maximumDb();
            lower->setValue(-3); QCOMPARE(monitor->minimumDb(), automaticMin - 3); QCOMPARE(monitor->maximumDb(), automaticMax);
            upper->setValue(4); QCOMPARE(monitor->maximumDb(), automaticMax + 4); QVERIFY(monitor->autoLevel());
            QCOMPARE(settings.value("display/sstvMinOffset").toInt(), -3); QCOMPARE(settings.value("display/sstvMaxOffset").toInt(), 4);
            QCOMPARE(window.findChild<QLabel*>("spectrumMinValue")->text(), QString("-3 dB"));
            QCOMPARE(window.findChild<QLabel*>("spectrumMaxValue")->text(), QString("+4 dB"));
            // A new stronger signal must still move the automatic limit, while
            // the handles remain at the user's correction values.
            for (int block = 0; block < 30; ++block) {
                for (auto& sample : audio) { sample = noise(random) + float(.03 * std::sin(2 * 3.141592653589793 * 1800 * time / 48000)); ++time; }
                capture.consume(audio.data(), audio.size()); QTest::qWait(3);
            }
            QTRY_VERIFY(monitor->maximumDb() > automaticMax + 7);
            QCOMPARE(lower->value(), -3); QCOMPARE(upper->value(), 4); QVERIFY(monitor->autoLevel());
            if (qEnvironmentVariableIsSet("MM_SSTV_SCREENSHOT"))
                QVERIFY(window.grab().save(qEnvironmentVariable("MM_SSTV_SCREENSHOT") + ".auto-level.png"));
            auto* modes = window.findChild<QComboBox*>("modulation"); modes->setCurrentIndex(0);
            QVERIFY(!monitor->autoLevel());
            QCOMPARE(lower->value(), -85); QCOMPARE(upper->value(), -15);
            QCOMPARE(lower->minimum(), -100); QCOMPARE(upper->minimum(), -50);
            modes->setCurrentIndex(modes->findData(int(Modulation::Scottie2)));
            QVERIFY(monitor->autoLevel()); QCOMPARE(lower->value(), -3); QCOMPARE(upper->value(), 4);
            QCOMPARE(window.size(), QSize(640, 440));
        }
        MainWindow restored(std::make_unique<FakeOutput>(state), settings);
        QVERIFY(!restored.findChild<QCheckBox*>("waterfallAutoLevel"));
        QCOMPARE(restored.findChild<QSlider*>("spectrumMin")->value(), -3);
        QCOMPARE(restored.findChild<QSlider*>("spectrumMax")->value(), 4);
        QVERIFY(restored.findChild<ReceiveMonitor*>("receiveMonitor")->autoLevel());
    }
    void savedWaterfallAdjustmentsClamped() {
        QTemporaryDir dir; QSettings settings(dir.filePath("prefs.ini"), QSettings::IniFormat); State state;
        settings.setValue("modem/modulation", "martin2");
        settings.setValue("display/sstvMinOffset", 99); settings.setValue("display/sstvMaxOffset", -99);
        MainWindow window(std::make_unique<FakeOutput>(state), settings);
        QCOMPARE(window.findChild<QSlider*>("spectrumMin")->value(), 6);
        QCOMPARE(window.findChild<QSlider*>("spectrumMax")->value(), -6);
        QCOMPARE(settings.value("display/sstvMinOffset").toInt(), 6);
        QCOMPARE(settings.value("display/sstvMaxOffset").toInt(), -6);
    }
    void waterfallAdjustmentLabelsAcrossModes() {
        QTemporaryDir dir; QSettings settings(dir.filePath("prefs.ini"), QSettings::IniFormat); State state;
        settings.setValue("display/maxDb", 0);
        MainWindow window(std::make_unique<FakeOutput>(state), settings);
        auto* modes = window.findChild<QComboBox*>("modulation");
        auto* number = window.findChild<QLabel*>("spectrumMaxValue");
        QCOMPARE(number->text(), QString("0"));
        for (int at = 0; at < 2; ++at) {
            modes->setCurrentIndex(modes->findData(int(Modulation::Martin1)));
            QCOMPARE(number->text(), QString("0 dB"));
            modes->setCurrentIndex(0); QCOMPARE(number->text(), QString("0"));
        }
    }
    void automaticWaterfallAdjustmentsAndReset() {
        ReceiveMonitor monitor; monitor.clear({}, Modulation::Martin1); monitor.resize(800, 164);
        ReceiveStatus status; status.modulation = Modulation::Martin1;
        status.spectrumBinHz = 48000. / 32768; status.waterfallBinHz = 48000. / 2048;
        std::vector<float> row(171, -90.f); row[77] = -45;
        status.waterfall.assign(10, row);
        for (int at = 0; at < 8; ++at) monitor.setStatus(status);
        QCOMPARE(monitor.minimumDb(), -110); QCOMPARE(monitor.maximumDb(), -51);
        const auto background = monitor.grab().toImage().pixelColor(220, 85);
        QVERIFY(background.value() > 0); QVERIFY(background.value() < 100);
        monitor.setLevelAdjustments(-6, 6);
        QCOMPARE(monitor.minimumDb(), -116); QCOMPARE(monitor.maximumDb(), -45);
        monitor.setLevelAdjustments(6, -6);
        QCOMPARE(monitor.minimumDb(), -104); QCOMPARE(monitor.maximumDb(), -57);
        for (int at = 0; at < 30; ++at) monitor.setStatus(status);
        QCOMPARE(monitor.minimumDb(), -104); QCOMPARE(monitor.maximumDb(), -57); // No cumulative drift.
        monitor.setLevelAdjustments(0, 0);
        QCOMPARE(monitor.minimumDb(), -110); QCOMPARE(monitor.maximumDb(), -51); // Accepted brightness at zero.
        monitor.setLevelAdjustments(3, -2); monitor.clear({}, Modulation::Martin1);
        monitor.setStatus(status); QCOMPARE(monitor.minimumDb(), -87); QCOMPARE(monitor.maximumDb(), -22);
        for (int at = 0; at < 6; ++at) monitor.setStatus(status);
        QCOMPARE(monitor.minimumDb(), -107); QCOMPARE(monitor.maximumDb(), -53);
        // A new FFT grid also resets history without feeding back corrections.
        status.waterfallBinHz = 20; status.waterfall.assign(10, std::vector<float>(201, -90));
        status.waterfall[0][90] = -40;
        monitor.setStatus(status);
        QCOMPARE(monitor.minimumDb(), -107); QCOMPARE(monitor.maximumDb(), -53);
        monitor.clear({}, Modulation::Qpsk4410); status.modulation = Modulation::Qpsk4410;
        monitor.setDbRange(-85, -20); monitor.setLevelAdjustments(-99, 99);
        for (int at = 0; at < 8; ++at) monitor.setStatus(status);
        QCOMPARE(monitor.minimumDb(), -85); QCOMPARE(monitor.maximumDb(), -20);
        QVERIFY(!monitor.autoLevel());
    }
    void waterfallNoiseFloorMatchesSpectrum() {
        ReceiveMonitor monitor; monitor.resize(800, 164);
        monitor.clear(QString(), Modulation::Martin1);
        monitor.setDbRange(-68, -35);
        ReceiveStatus status; status.modulation = Modulation::Martin1;
        status.spectrumBinHz = 48000. / 32768;
        status.waterfallBinHz = 48000. / 2048;
        status.spectrum.assign(2731, -76.f);
        // The same broadband noise reads 12.04 dB higher in a 2048-point FFT
        // than in a 32768-point FFT. Both plots must share the selected floor.
        std::vector<float> row(171, -63.9588f);
        for (int bin = 80; bin < 100; ++bin) row[bin] = -10.f;
        status.waterfall.assign(600, row); monitor.setStatus(status);
        monitor.setDbRange(-68, -35); // Isolate renderer calibration from the live estimator.
        auto frame = monitor.grab().toImage();
        QCOMPARE(frame.pixelColor(220, 120), QColor(Qt::black));
        QCOMPARE(frame.pixelColor(430, 120), QColor(Qt::red));
        // Moving Min below the noise must reveal it; changing Max must not
        // change the cutoff. Existing history is recolored immediately.
        monitor.setDbRange(-90, -35); frame = monitor.grab().toImage();
        QVERIFY(frame.pixelColor(220, 120) != QColor(Qt::black));
        monitor.setDbRange(-68, 0); frame = monitor.grab().toImage();
        QCOMPARE(frame.pixelColor(220, 120), QColor(Qt::black));
    }
    void optionalWaterfallId() {
        QTemporaryDir dir; QSettings settings(dir.filePath("prefs.ini"), QSettings::IniFormat); State state;
        {
            MainWindow window(std::make_unique<FakeOutput>(state), settings); window.resize(640, 440); window.show();
            auto* enabled = window.findChild<QCheckBox*>("waterfallId"); QVERIFY(enabled); QVERIFY(!enabled->isChecked());
            QVERIFY(window.loadImage(picture(dir))); auto* modes = window.findChild<QComboBox*>("modulation");
            auto* button = window.findChild<QPushButton*>("transmitButton");
            enabled->setChecked(true); QVERIFY(settings.value("transmit/waterfallId").toBool());
            for (const auto mode : {Modulation::Apsk8, Modulation::Qpsk4410, Modulation::Martin1, Modulation::Martin2, Modulation::Scottie1, Modulation::Scottie2}) {
                modes->setCurrentIndex(modes->findData(int(mode))); button->click();
                QVERIFY(!enabled->isEnabled()); QCOMPARE(state.sampleRate, modeSampleRate(mode));
                QVERIFY(window.findChild<QLabel*>("statusText")->text().contains(QString::fromStdString(waterfallIdText(mode))));
                QVERIFY(window.findChild<QSlider*>("outputLevel")->isEnabled());
                // Cancel while the ID is still playing; no image is sent afterwards.
                button->click(); QVERIFY(!state.open); QVERIFY(enabled->isEnabled());
            }
            QTest::qWait(20); QCOMPARE(window.size(), QSize(640,440));
            if (qEnvironmentVariableIsSet("MM_SSTV_SCREENSHOT"))
                QVERIFY(window.grab().save(qEnvironmentVariable("MM_SSTV_SCREENSHOT") + ".waterfall-id.png"));
        }
        MainWindow restored(std::make_unique<FakeOutput>(state), settings);
        QVERIFY(restored.findChild<QCheckBox*>("waterfallId")->isChecked());
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
            QTRY_VERIFY(dialog->findChild<QPushButton*>("refreshDevices")->isEnabled());
            QCOMPARE(outputs->currentData().toString(), QString("toQO100"));
            state.devices.push_back({"new-output", "New output"});
            dialog->findChild<QPushButton*>("refreshDevices")->click();
            QTRY_VERIFY(dialog->findChild<QPushButton*>("refreshDevices")->isEnabled());
            QVERIFY(outputs->findData("new-output") >= 0);
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
                QTRY_VERIFY(inputs->isEnabled() && inputs->findData("other-input") >= 0);
                inputs->setCurrentIndex(inputs->findData("other-input")); dialog->reject();
            });
            window.findChild<QAction*>("audioSetup")->trigger();
            QVERIFY(capture.open); QCOMPARE(capture.chosen, std::string("other-input"));
        }
        QVERIFY(!capture.open);
    }
    void qpskSelectionAndNativeRateRoundTrip() {
        QTemporaryDir dir; QSettings settings(dir.filePath("prefs.ini"), QSettings::IniFormat); State state; CaptureState capture;
        settings.setValue("overlay/text0", "DJ0ABR"); settings.setValue("overlay/text2", "CQ QO100");
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
        QTRY_COMPARE(QDir("rxpic").entryList({"*.jpg"}, QDir::Files).size(), 1);
        const auto expected = preparePicture(QImage(picture(dir)), QSize(640,480), loadTextOverlay(settings));
        QFile received(QDir("rxpic").absoluteFilePath(QDir("rxpic").entryList({"*.jpg"}, QDir::Files).first())); QVERIFY(received.open(QIODevice::ReadOnly));
        QCOMPARE(received.readAll(), QByteArray(reinterpret_cast<const char*>(expected.jpeg.data()), int(expected.jpeg.size())));
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
        settings.setValue("overlay/text0", "DJ0ABR"); settings.setValue("overlay/text2", "CQ QO100");
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
        unsigned textPixels = 0;
        for(int y=10;y<44;++y) for(int x=10;x<155;++x) textPixels += received.pixelColor(x,y).green() < 220;
        QVERIFY(textPixels > 15); // The white letterbox now contains the received callsign outline.
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
