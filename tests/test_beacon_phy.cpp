// SPDX-License-Identifier: GPL-3.0-or-later
// Native, offline receive regression: no sound card, Python or station access.
#include "mm_sstv/modem_mode.hpp"
#include "mm_sstv/tx_modulator.hpp"
#include <QCoreApplication>
#include <QProcess>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
void require(bool ok, const char* why) { if (!ok) throw std::runtime_error(why); }
QByteArray run(const QString& executable, const QStringList& args, const QByteArray& input = {}) {
    QProcess process; process.start(executable, args);
    require(process.waitForStarted(), "Cannot start RX test process");
    require(process.write(input) == input.size(), "Cannot queue test audio");
    process.closeWriteChannel();
    require(process.waitForFinished(30000), "RX test process timed out");
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0)
        throw std::runtime_error(process.readAllStandardError().toStdString());
    return process.readAllStandardOutput();
}
struct Result { QJsonObject last; unsigned frames = 0, locked = 0; };
Result decode(const QString& receiver, const QByteArray& audio) {
    Result result;
    const auto output = run(receiver, {"--modulation", "mm-beacon", "--input-rate", "48000", "--monitor"}, audio);
    for (const auto& line : output.split('\n')) {
        if (line.isEmpty()) continue;
        QJsonParseError error;
        const auto event = QJsonDocument::fromJson(line, &error).object();
        require(error.error == QJsonParseError::NoError, "Invalid receiver JSON");
        if (event.value("event") == "frame") {
            const auto payload = QByteArray::fromHex(event.value("payload_hex").toString().toLatin1());
            require(event.value("frame_type").toInt() == 8 && payload.size() == 219 && payload[0] == 2,
                    "Wrong beacon frame format");
            require(payload.mid(1).startsWith("TEST CW-SKIMMER RX VALIDATION"), "Wrong decoded beacon payload");
            ++result.frames;
        } else if (event.value("event") == "status") {
            require(event.value("modulation") == "mm-beacon" && event.value("bit_rate").toInt() == 7200,
                    "Wrong modem profile in status");
            if (event.value("locked").toBool()) ++result.locked;
            result.last = event;
        }
    }
    require(!result.last.isEmpty(), "Missing RX status");
    return result;
}
}
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    try {
        require(argc == 3, "Expected receiver and independent fixture paths");
        const auto p = mm_sstv::parameters(mm_sstv::Modulation::MmBeacon);
        require(p.bitRate() == 7200 && p.symbolRate() == 2400 && p.rxDecimation == 5 && p.pllSampleRate() == 4800,
                "Incorrect beacon PHY parameters");
        bool rejected = false;
        try { mm_sstv::TxModulator forbidden(mm_sstv::Modulation::MmBeacon); }
        catch (const std::invalid_argument&) { rejected = true; }
        require(rejected, "Beacon mode must not transmit");
        const QString receiver = QString::fromLocal8Bit(argv[1]), fixture = QString::fromLocal8Bit(argv[2]);
        QByteArray clean;
        for (const auto& offset : {QString("0"), QString("400")}) {
            const auto audio = run(fixture, {offset, "clean", "65", "mm-beacon"});
            const auto result = decode(receiver, audio);
            require(result.frames >= 40 && result.last.value("locked").toBool(), "7200-bit/s beacon did not lock");
            require(std::abs(result.last.value("center_hz").toDouble() + result.last.value("pll_hz").toDouble()
                             - (1500 + offset.toDouble())) < 1, "Incorrect AFC correction");
            require(result.last.value("points").toArray().size() > 0 && result.last.value("spectrum_dbfs").toArray().size() > 100,
                    "Missing constellation or spectrum");
            if (offset == "400") clean = audio;
        }
        const auto drift = decode(receiver, run(fixture, {"400", "drift", "100", "mm-beacon"}));
        require(drift.frames >= 60 && drift.last.value("locked").toBool(), "Beacon drift tracking failed");
        const auto corrupt = decode(receiver, run(fixture, {"400", "corrupt", "65", "mm-beacon"}));
        require(corrupt.frames == 0 && corrupt.locked == 0, "Invalid CRC produced a false lock");
        const QByteArray silence(48000 * 4 * int(sizeof(float)), '\0');
        const auto lost = decode(receiver, clean + silence);
        require(lost.locked > 0 && !lost.last.value("locked").toBool(), "Lock did not expire after signal loss");
        std::vector<float> tone(48000 * 8);
        for (std::size_t i = 0; i < tone.size(); ++i) tone[i] = .04f * std::cos(2 * std::acos(-1.) * 500 * i / 48000);
        const auto noData = decode(receiver, QByteArray(reinterpret_cast<const char*>(tone.data()), int(tone.size() * sizeof(float))));
        require(noData.frames == 0 && noData.locked == 0, "A pilot alone produced a false lock");
        std::cout << "MM beacon PHY: 7200 bit/s, 100/500-Hz pilot, AFC drift, valid payload, spectrum/constellation, CRC rejection and signal loss passed\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
