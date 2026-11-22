// SPDX-License-Identifier: GPL-3.0-or-later
#include "beacon_dsp.hpp"
#include "psk_fixture.hpp"
#include <QCryptographicHash>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>
#include <QProcess>
#include <QCoreApplication>
#include <QSet>
#include <cmath>
class PskDecoderTest final : public QObject {
    Q_OBJECT
private slots:
    void recordingMatchesOriginalDecoder() {
        const QString dir = QString::fromUtf8(MM_SSTV_PSK_DATA);
        QFile wav(dir + "/sample.wav"); QVERIFY(wav.open(QIODevice::ReadOnly));
        QCOMPARE(QCryptographicHash::hash(wav.readAll(), QCryptographicHash::Sha256).toHex(), QByteArray("8788f0385db5ca201e8e113a6f5bb0acb1fb2fe763f9c96ddf7533e6a6a79b6f"));
        const auto input = psk_test::wave(dir + "/sample.wav");
        const auto decoded = qo100::beacon::decode(input.pcm, input.rate, 10489748500ULL);
        QFile reference(dir + "/recording.json"); QVERIFY(reference.open(QIODevice::ReadOnly));
        const auto json = QJsonDocument::fromJson(reference.readAll()).object(); const auto frames = json["frames"].toArray();
        QCOMPARE(decoded.frames.size(), std::size_t(frames.size()));
        unsigned crc = 0, fec = 0;
        for (int i = 0; i < frames.size(); ++i) {
            const auto& frame = decoded.frames[std::size_t(i)]; const auto golden = frames[i].toObject();
            QCOMPARE(QByteArray(reinterpret_cast<const char*>(frame.payload.data()), int(frame.payload.size())).toHex(), golden["payload_hex"].toString().toLatin1());
            QCOMPARE(frame.fec, golden["kind"].toString() == "FEC");
            if (frame.fec) ++fec; else ++crc;
            QVERIFY(qo100::beacon::format(frame, decoded.carrier).find(frame.fec ? "FEC/RS OK" : "CRC OK") != std::string::npos);
        }
        QCOMPARE(crc, 6u); QCOMPARE(fec, 5u);
        QVERIFY(std::abs(decoded.carrier - json["carrier_hz"].toDouble()) < .05);
    }
    void streamingCoreDecodesAndSuppressesOverlap() {
        const auto audio = psk_test::audio48(psk_test::wave(QString::fromUtf8(MM_SSTV_PSK_DATA) + "/sample.wav"), 40);
        QString binary = QCoreApplication::applicationDirPath() + "/hs-stv-psk-rx-core";
#ifdef Q_OS_WIN
        binary += ".exe";
#endif
        QProcess core; core.start(binary, {"--input-rate", "48000", "--monitor", "--modulation", "psk-beacon"});
        QVERIFY(core.waitForStarted());
        QCOMPARE(core.write(reinterpret_cast<const char*>(audio.data()), qint64(audio.size() * sizeof(float))), qint64(audio.size() * sizeof(float)));
        core.closeWriteChannel(); QVERIFY(core.waitForFinished(15000)); QCOMPARE(core.exitCode(), 0);
        QSet<QString> messages; unsigned count = 0; QJsonObject status;
        for (const auto& line : core.readAllStandardOutput().split('\n')) {
            if (line.isEmpty()) continue;
            const auto json = QJsonDocument::fromJson(line).object(); QVERIFY(!json.isEmpty());
            if (json["event"] == "psk_text") {
                const auto text = QString::fromUtf8(QByteArray::fromHex(json["text_hex"].toString().toLatin1()));
                const auto body = text.section('\n', 1); QVERIFY(!messages.contains(body)); messages.insert(body); ++count;
            } else if (json["event"] == "status") status = json;
        }
        QVERIFY(count >= 3); QCOMPARE(status["valid_frames"].toInt(), int(count));
        QVERIFY(status["locked"].toBool()); QVERIFY(status["spectrum_dbfs"].toArray().size() > 2000);
        QVERIFY(std::abs(status["center_hz"].toDouble() - 1506.4) < 2);
    }
    void silenceToneAndInvalidInputDoNotPublish() {
        using namespace qo100::beacon;
        QVERIFY(decode(std::vector<std::int16_t>(20 * 12000), 12000, 10489748500ULL).frames.empty());
        std::vector<std::int16_t> tone(20 * 12000);
        for (std::size_t i = 0; i < tone.size(); ++i) tone[i] = std::int16_t(10000 * std::cos(2 * 3.141592653589793 * 1500 * i / 12000));
        QVERIFY(decode(tone, 12000, 10489748500ULL).frames.empty());
        QVERIFY_EXCEPTION_THROWN(decode(tone, 48000, 10489748500ULL), std::runtime_error);
    }
};
QTEST_GUILESS_MAIN(PskDecoderTest)
#include "test_psk_decoder.moc"
