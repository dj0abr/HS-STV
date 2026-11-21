// SPDX-License-Identifier: GPL-3.0-or-later
#include "bulletin_receiver.hpp"
#include "bulletin_zip.hpp"
#include "bulletin_fixture.hpp"
#include <QFile>
#include <QTemporaryDir>
#include <QThread>
#include <QTest>
using namespace mm_sstv;
using namespace beacon_test;
class BulletinTest final : public QObject {
    Q_OBJECT
    QDateTime at_ = QDateTime::fromString("2026-11-21T13:00:00Z", Qt::ISODate);
    BulletinUpdate feed(BulletinAssembler& rx, const std::vector<ReceivedFrame>& input) {
        BulletinUpdate result;
        for (const auto& frame : input) { auto update = rx.accept(frame, at_); if (update.bulletin || update.progress) result = std::move(update); }
        return result;
    }
    QByteArray longText() {
        QByteArray text = "~N:2:Lang~\n";
        std::uint32_t state = 42;
        for (unsigned i = 0; i < 8000; ++i) { state = 1664525 * state + 1013904223; text.append(char('a' + state % 26)); }
        return text;
    }
private slots:
    void storedDeflatedAndDescriptorZip() {
        const auto text = content();
        for (const bool stored : {false, true}) for (const bool descriptor : {false, true})
            QCOMPARE(readBulletinZip(zip(text, "nested/news.blt", stored, descriptor)), text);
        auto commented = zip(text); set16(commented, commented.size() - 2, 7); commented += "comment";
        QCOMPARE(readBulletinZip(commented), text);
        auto unsignedDescriptor = zip(text, "news.blt", false, true);
        const auto central = get32(unsignedDescriptor, unsignedDescriptor.size() - 6);
        unsignedDescriptor.remove(unsignedDescriptor.indexOf("PK\x07\x08"), 4);
        // The central-directory offset moves with the removed descriptor signature.
        const int offset = unsignedDescriptor.size() - 6;
        for (unsigned i = 0; i < 4; ++i) unsignedDescriptor[offset + int(i)] = char((central - 4) >> (8 * i));
        QCOMPARE(readBulletinZip(unsignedDescriptor), text);
    }
    void malformedZipIsRejected() {
        const auto original = zip(content());
        const auto central = int(get32(original, original.size() - 6));
        std::vector<QByteArray> invalid{QByteArray("not a zip"), original.left(original.size() - 1), zip(content(), "bad.html")};
        auto bad = original; set16(bad, central + 8, 1); invalid.push_back(bad); // encrypted
        bad = original; set32(bad, central + 24, maximumBulletinTextBytes + 1); invalid.push_back(bad);
        bad = original; set32(bad, central + 24, 1); set32(bad, 22, 1); invalid.push_back(bad); // understated output size
        bad = original; set16(bad, central + 10, 99); invalid.push_back(bad);
        bad = original; set32(bad, central + 42, 0xfffffff0); invalid.push_back(bad);
        bad = original; set32(bad, central + 16, 123); set32(bad, 14, 123); invalid.push_back(bad); // CRC32
        bad = original; bad[45] = char(static_cast<unsigned char>(bad[45]) ^ 0x80); invalid.push_back(bad); // compressed data
        bad = original; set16(bad, bad.size() - 12, 2); set16(bad, bad.size() - 14, 2); invalid.push_back(bad); // entry count
        bad = zip(content(), "news.blt", false, true);
        const int descriptor = bad.indexOf("PK\x07\x08");
        bad[descriptor + 4] = char(static_cast<unsigned char>(bad[descriptor + 4]) ^ 1); invalid.push_back(bad);
        for (const auto& data : invalid) { bool rejected = false; try { readBulletinZip(data); } catch (const std::exception&) { rejected = true; } QVERIFY(rejected); }
    }
    void reconstructsMetadataAndPlainText() {
        for (const unsigned kind : {3u, 4u, 5u}) {
            BulletinAssembler rx; const auto result = feed(rx, frames(zip(content()), "../news.blt", kind));
            QVERIFY(result.bulletin); QVERIFY(result.progress->complete);
            QCOMPARE(result.bulletin->filename, QString("news.blt")); QCOMPARE(result.bulletin->title, QString("MM-Bake Test"));
            QCOMPARE(result.bulletin->category, QString("B")); QCOMPARE(result.bulletin->page, 1u);
            QVERIFY(result.bulletin->text.contains(QStringLiteral("Grüße aus München & 73.")));
            QVERIFY(!result.bulletin->text.contains("evil")); QVERIFY(!result.bulletin->text.contains("<h3>"));
        }
    }
    void htmlBulletinsPreserveSourceLineBreaks() {
        const QString expected = QStringLiteral("Rundspruch\n\nErste Zeile & Grüße\nZweite Zeile\n\n- Thema eins\n- Thema zwei\n\nInfo (https://example.org/info)\nEnde");
        for (const auto& newline : {QByteArray("\n"), QByteArray("\r\n"), QByteArray("\r")}) {
            auto source = QStringLiteral("~R:8:37~\n<h3>Rundspruch</h3>\n<p style=\"font-size:12px\">\n\n"
                "Erste Zeile &amp; Grüße\nZweite Zeile\n\n- Thema eins\n- Thema zwei\n\n"
                "<a href=\"https://example.org/info\">Info</a><br>Ende\n</p>"
                "<script>versteckt\nversteckt</script><style>p {color:red;}</style>").toUtf8();
            source.replace("\n", newline);
            BulletinAssembler rx; const auto result = feed(rx, frames(zip(source)));
            QVERIFY(result.bulletin); QCOMPARE(result.bulletin->text, expected);
            QTemporaryDir dir; BulletinArchive archive(dir.path());
            QCOMPARE(archive.load(archive.save(*result.bulletin)).text, expected);
        }
    }
    void plainTextAndPreformattedLineBreaks() {
        for (const auto& source : {QByteArray("Zeile eins\r\n\r\nZeile zwei\rZeile drei"),
                                  QByteArray("<pre>Zeile eins\n\nZeile zwei\nZeile drei</pre>")}) {
            BulletinAssembler rx; const auto result = feed(rx, frames(zip(source)));
            QVERIFY(result.bulletin); QCOMPARE(result.bulletin->text, QString("Zeile eins\n\nZeile zwei\nZeile drei"));
        }
    }
    void missingBlocksRepetitionsAndStreamReset() {
        BulletinAssembler rx; const auto input = frames(zip(longText())); QVERIFY(input.size() > 3);
        for (unsigned n = 0; n < input.size(); ++n) if (n != 2) QVERIFY(!rx.accept(input[n], at_).bulletin);
        rx.resetStream(); QVERIFY(!rx.accept(input[2], at_).bulletin); // A new stream needs its header.
        QVERIFY(!rx.accept(input[0], at_).bulletin);
        const auto result = rx.accept(input[2], at_); QVERIFY(result.bulletin); QVERIFY(result.progress->complete);
        QCOMPARE(result.bulletin->text, QString::fromUtf8(longText()).section('\n', 1));
    }
    void lastBlockMayPrecedeMiddleAndSpotsDoNotInterrupt() {
        BulletinAssembler rx; const auto input = frames(zip(longText()));
        QVERIFY(!rx.accept(input[0], at_).bulletin); QVERIFY(!rx.accept(input.back(), at_).bulletin);
        ReceivedFrame spot; spot.type = 8; spot.status = 3; spot.payload[0] = 1;
        QVERIFY(!rx.accept(spot, at_).bulletin);
        BulletinUpdate result;
        for (unsigned n = 1; n + 1 < input.size(); ++n) result = rx.accept(input[n], at_);
        QVERIFY(result.bulletin);
    }
    void singleFrameCounterAndDuplicateVersions() {
        BulletinAssembler rx; auto input = frames(zip("Kurz")); QCOMPARE(input.size(), std::size_t(1));
        input[0].counter = 77; QVERIFY(feed(rx, input).bulletin);
        QVERIFY(!feed(rx, input).bulletin);
        const auto changed = feed(rx, frames(zip("Neue Version"))); QVERIFY(changed.bulletin); QCOMPARE(changed.bulletin->text, QString("Neue Version"));
    }
    void invalidFilesAndHeaderNeverPublish() {
        BulletinAssembler rx;
        auto bad = frames(zip(content()), "news.blt", 3, 123); auto result = feed(rx, bad);
        QVERIFY(!result.bulletin); QVERIFY(result.progress->error); QVERIFY(result.progress->message.contains("CRC"));
        result = feed(rx, frames("not a zip")); QVERIFY(!result.bulletin); QVERIFY(result.progress->error);
        auto header = frames(zip(longText()))[0]; header.counter = 1;
        result = rx.accept(header, at_); QVERIFY(!result.bulletin); QVERIFY(result.progress->error);
        const auto input = frames(zip(longText())); rx.accept(input[0], at_);
        rx.accept(frames(zip("ignored"), "other.html")[0], at_);
        for (unsigned n = 1; n < input.size(); ++n) QVERIFY(!rx.accept(input[n], at_).bulletin);
    }
    void inconsistentAssemblyIsRepairedByRepeat() {
        BulletinAssembler rx; const auto input = frames(zip(longText()));
        auto corrupt = input; corrupt[1].payload[30] ^= 1;
        const auto failed = feed(rx, corrupt); QVERIFY(!failed.bulletin); QVERIFY(failed.progress->error);
        const auto repaired = feed(rx, input); QVERIFY(repaired.bulletin);
        QCOMPARE(repaired.bulletin->text, QString::fromUtf8(longText()).section('\n', 1));
    }
    void legacyEncodingAndHiddenResources() {
        BulletinAssembler rx;
        const auto bytes = QByteArray("~N:2:Legacy~\n<p>") + QByteArray::fromHex("4772fcdf652080") +
            "</p><style>hidden-style</style><script>hidden-script</script><img src='file:///etc/passwd'>";
        const auto result = feed(rx, frames(zip(bytes))); QVERIFY(result.bulletin);
        QVERIFY(result.bulletin->text.contains(QStringLiteral("Grüße €")));
        QVERIFY(!result.bulletin->text.contains("hidden")); QVERIFY(!result.bulletin->text.contains("root:"));
        const auto link = feed(rx, frames(zip("<p><a href='https://example.org/info'>Info</a></p>")));
        QVERIFY(link.bulletin); QVERIFY(link.bulletin->text.contains("Info (https://example.org/info)"));
    }
    void workerDeliversOnOwnerThreadAndResetsStaleWork() {
        QTemporaryDir archive; BulletinReceiver receiver(nullptr, archive.path()); unsigned count = 0; Bulletin latest; bool ownerThread = true;
        receiver.bulletinReceived = [&](const Bulletin& bulletin) { ownerThread &= QThread::currentThread() == thread(); latest = bulletin; ++count; };
        const auto input = frames(zip(content()));
        for (const auto& frame : input) receiver.submit(frame);
        receiver.resetStream();
        for (const auto& frame : input) receiver.submit(frame);
        QTRY_COMPARE(count, 1u); QVERIFY(ownerThread); QVERIFY(latest.text.contains("Test-Bulletin"));
        for (const auto& frame : input) receiver.submit(frame);
        QTest::qWait(50); QCOMPARE(count, 1u);
    }
    void failedSaveIsRetriedOnValidRepeat() {
        QTemporaryDir dir; QFile blocked(dir.filePath("blocked")); QVERIFY(blocked.open(QIODevice::WriteOnly)); blocked.write("file"); blocked.close();
        const auto path = dir.filePath("blocked/bulletins"); BulletinReceiver receiver(nullptr, path);
        bool error = false, saved = false;
        receiver.progressChanged = [&](const auto& progress) {
            error |= progress.error && progress.message.contains("Speichern");
            saved |= !progress.error && progress.message.contains("gespeichert");
        };
        const auto input = frames(zip(content())); for (const auto& frame : input) receiver.submit(frame);
        QTRY_VERIFY(error); QVERIFY(QFile::remove(blocked.fileName()));
        for (const auto& frame : input) receiver.submit(frame);
        QTRY_VERIFY(saved); QCOMPARE(BulletinArchive(path).entries().size(), std::size_t(1));
    }
};
QTEST_MAIN(BulletinTest)
#include "test_bulletins.moc"
