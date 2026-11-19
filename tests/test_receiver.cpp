// SPDX-License-Identifier: GPL-3.0-or-later
#include "receiver.hpp"
#include "image_codec.hpp"
#include "mm_sstv/image_audio.hpp"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QImage>
#include <QThread>
#include <array>
#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <QDebug>
#include <atomic>
#include <thread>
using namespace mm_sstv;
namespace {
std::atomic<unsigned> jpegWarnings{0}, otherWarnings{0};
QtMessageHandler originalHandler = nullptr;
void countWarnings(QtMsgType type, const QMessageLogContext& context, const QString& message) {
    if (type == QtWarningMsg && message == QLatin1String("Corrupt JPEG data: premature end of data segment")) ++jpegWarnings;
    else if (type == QtWarningMsg && message == QLatin1String("JPEG logging passthrough test")) ++otherWarnings;
    if (originalHandler) originalHandler(type, context, message);
}
void checkPreviewWarnings(const PreparedPicture& picture) {
    auto prefix = picture.jpeg; prefix.resize(prefix.size() / 2); prefix.push_back(0xff); prefix.push_back(0xd9);
    if (prefix.size() < 1024) throw std::runtime_error("Preview warning fixture too small");
    for (unsigned i = 0; i < 3; ++i)
        if (decodeReceivedJpeg(prefix, false).isNull()) throw std::runtime_error("Partial JPEG no longer displays");
    if (jpegWarnings != 0) throw std::runtime_error("Partial JPEG warnings not suppressed");
    // The identical warning from another thread must still reach the original handler.
    std::thread other([] { qWarning("Corrupt JPEG data: premature end of data segment"); }); other.join();
    if (jpegWarnings != 1) throw std::runtime_error("Warning from another thread was hidden");
    qWarning("JPEG logging passthrough test");
    if (otherWarnings != 1) throw std::runtime_error("Unrelated warning was hidden");
    if (decodeReceivedJpeg(prefix, true).isNull() || jpegWarnings != 2)
        throw std::runtime_error("Complete-image JPEG warning was hidden");
    if (decodeReceivedJpeg(picture.jpeg, true).isNull() || jpegWarnings != 2)
        throw std::runtime_error("Valid complete JPEG decoding changed");
}
}

class FakeInput final : public AudioInput {
public:
    Consume receive;
    std::vector<AudioDevice> devices() override { return {{"fromQO100", "fromQO100"}}; }
    void start(const std::string& device, Consume consumer) override {
        if (device != "fromQO100") throw std::runtime_error("Wrong device");
        receive = std::move(consumer);
    }
    void stop() noexcept override { receive = {}; }
    std::string error() const override { return {}; }
};
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    originalHandler = qInstallMessageHandler(countWarnings);
    try {
        QImage original(640, 480, QImage::Format_RGB32);
        for (int y = 0; y < original.height(); ++y) for (int x = 0; x < original.width(); ++x)
            original.setPixel(x, y, qRgb((x * 3 + y) % 256, (x + y * 2) % 256, x % 256));
        const auto picture = preparePicture(original, original.size());
        checkPreviewWarnings(picture);
        ImageAudio audio(prepareImage(picture.jpeg, "receive-test.jpg"));
        auto input = std::make_unique<FakeInput>(); auto* source = input.get(); Receiver rx(std::move(input));
        unsigned previews = 0; bool locked = false, complete = false, spectrumSeen = false, pointsSeen = false, silenceSeen = false; QString failure;
        rx.failed = [&](const auto& text) { failure = text; };
        rx.statusChanged = [&](const ReceiveStatus& status) {
            locked |= status.locked;
            pointsSeen |= status.locked && status.points.size() >= 40;
            if (status.spectrum.size() > 2700 && status.spectrumBinHz > 1 && status.spectrumBinHz < 2) {
                spectrumSeen = true;
                silenceSeen |= status.points.empty() && *std::max_element(status.spectrum.begin(), status.spectrum.end()) < -150;
            }
        };
        rx.imageReceived = [&](const ReceivedImage& image) {
            if (image.complete) {
                if (image.jpeg != picture.jpeg) throw std::runtime_error("Received JPEG differs");
                complete = true;
                if (decodeReceivedJpeg(image.jpeg, true).size() != picture.preview.size()) throw std::runtime_error("Final JPEG not decoded");
            } else if (!decodeReceivedJpeg(image.jpeg, false).isNull()) ++previews;
        };
        rx.start("fromQO100"); std::array<float, 4800> block{};
        while (const auto count = audio.render(block.data(), block.size(), .6f)) {
            source->receive(block.data(), count); QCoreApplication::processEvents(); QThread::msleep(2);
            if (!failure.isEmpty()) throw std::runtime_error(failure.toStdString());
        }
        QElapsedTimer timer; timer.start();
        while (!complete && failure.isEmpty() && timer.elapsed() < 5000) { QCoreApplication::processEvents(); QThread::msleep(2); }
        if (!complete || !locked || previews < 2 || !spectrumSeen || !pointsSeen) throw std::runtime_error("RX did not synchronize, progressively display and finish the image: " + failure.toStdString());
        // Flush the display history with digital silence; no frozen constellation/spectrum.
        block.fill(0);
        for (unsigned i = 0; i < 15; ++i) {
            source->receive(block.data(), block.size()); QCoreApplication::processEvents(); QThread::msleep(2);
        }
        timer.restart();
        while (!silenceSeen && failure.isEmpty() && timer.elapsed() < 3000) { QCoreApplication::processEvents(); QThread::msleep(2); }
        if (!silenceSeen) throw std::runtime_error("RX monitor retained old signal after silence");
        if (jpegWarnings != 2) throw std::runtime_error("Progressive RX emitted JPEG warning spam");
        rx.stop(); if (source->receive) throw std::runtime_error("Capture callback left active");
        std::cout << "Qt RX process bridge: locked; " << previews << " partial JPEG previews; final JPEG byte-identical; live spectrum and I/Q points; silence clears history\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
