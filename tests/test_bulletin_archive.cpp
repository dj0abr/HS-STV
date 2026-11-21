// SPDX-License-Identifier: GPL-3.0-or-later
#include "bulletin_archive.hpp"
#include "bulletin_dialog.hpp"
#include "beacon_view.hpp"
#include <QComboBox>
#include <QCryptographicHash>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QScrollBar>
#include <QTemporaryDir>
#include <QTest>
using namespace mm_sstv;
class BulletinArchiveTest final : public QObject {
    Q_OBJECT
    Bulletin bulletin(const QString& name = "ANS-319.blt", const QString& text = QStringLiteral("Grüße & 73\nBulletintext"), int seconds = 0) {
        Bulletin result; result.filename = name; result.text = text; result.title = "ANS News"; result.category = "N"; result.page = 2;
        result.digest = QCryptographicHash::hash(text.toUtf8(), QCryptographicHash::Sha256);
        result.receivedAt = QDateTime::fromString("2026-11-21T13:00:00Z", Qt::ISODate).addSecs(seconds); return result;
    }
private slots:
    void fullTextOriginalNameAndRestart() {
        QTemporaryDir dir; const auto path = dir.filePath("bulletins"); BulletinArchive archive(path);
        const auto original = bulletin("ANS-319.blt", "Anfang\n" + QString(100000, 'x'));
        bool added = false; const auto id = archive.save(original, &added); QVERIFY(added);
        QCOMPARE(QFileInfo(QDir(path).filePath(id)).absolutePath(), path);
        BulletinArchive restored(path); const auto entries = restored.entries(); QCOMPARE(entries.size(), std::size_t(1));
        QCOMPARE(entries[0].filename, QString("ANS-319.blt"));
        const auto loaded = restored.load(id); QCOMPARE(loaded.text, original.text); QCOMPARE(loaded.title, original.title);
        QCOMPARE(loaded.receivedAt, original.receivedAt); QCOMPARE(loaded.digest, original.digest);
    }
    void duplicatesAndChangedVersions() {
        QTemporaryDir dir; BulletinArchive archive(dir.path()); auto original = bulletin();
        const auto first = archive.save(original); original.receivedAt = original.receivedAt.addSecs(60);
        bool added = true; QCOMPARE(archive.save(original, &added), first); QVERIFY(!added);
        const auto changed = bulletin("ANS-319.blt", "Neue Fassung", 120); const auto second = archive.save(changed, &added);
        QVERIFY(added); QVERIFY(first != second); QCOMPARE(archive.entries().size(), std::size_t(2));
        QCOMPARE(archive.entries()[0].id, second); QCOMPARE(archive.load(first).text, bulletin().text);
    }
    void pathsCorruptionAndWriteErrors() {
        QTemporaryDir dir; BulletinArchive archive(dir.filePath("bulletins")); auto original = bulletin("../../ANS-319.blt");
        const auto id = archive.save(original); QCOMPARE(QFileInfo(QDir(archive.directory()).filePath(id)).absolutePath(), archive.directory());
        bool rejected = false; try { archive.load("../outside.json"); } catch (const std::exception&) { rejected = true; } QVERIFY(rejected);
        QFile corrupt(QDir(archive.directory()).filePath(id)); QVERIFY(corrupt.open(QIODevice::WriteOnly)); corrupt.write("{broken"); corrupt.close();
        QVERIFY(archive.entries().empty()); bool added = false; QCOMPARE(archive.save(original, &added), id); QVERIFY(added);
        QCOMPARE(archive.load(id).text, original.text);
        QFile blocked(dir.filePath("blocked")); QVERIFY(blocked.open(QIODevice::WriteOnly)); blocked.write("file"); blocked.close();
        BulletinArchive invalid(dir.filePath("blocked/bulletins")); rejected = false;
        try { invalid.save(original); } catch (const std::exception&) { rejected = true; } QVERIFY(rejected);
    }
    void workerQueriesPersistAfterStreamReset() {
        QTemporaryDir dir; BulletinArchive archive(dir.path()); const auto original = bulletin(); const auto id = archive.save(original);
        BulletinReceiver worker(nullptr, dir.path()); bool listed = false, loaded = false;
        worker.archiveListed = [&](const auto& entries) { listed = entries.size() == 1 && entries[0].filename == original.filename; };
        worker.archiveLoaded = [&](const auto& loadedId, const auto& value) { loaded = loadedId == id && value.text == original.text; };
        worker.listArchive(); worker.loadArchive(id); worker.resetStream(); QTRY_VERIFY(listed); QTRY_VERIFY(loaded);
    }
    void readerCurrentSelectionRefreshAndClose() {
        QTemporaryDir dir; BulletinArchive archive(dir.path());
        const auto old = bulletin("ANS-318.blt", "Altes Bulletin", -60), current = bulletin();
        const auto oldId = archive.save(old); archive.save(current);
        QPointer<BulletinDialog> reader = new BulletinDialog(current); reader->show();
        reader->loadRequested = [&](const QString& id) { reader->showArchived(id, archive.load(id)); };
        reader->setEntries(archive.entries());
        auto* choice = reader->findChild<QComboBox*>("bulletinArchiveChoice");
        auto* text = reader->findChild<QPlainTextEdit*>("bulletinReaderText");
        QCOMPARE(text->toPlainText(), current.text); QCOMPARE(choice->count(), 2);
        QVERIFY(choice->currentText().startsWith("ANS-319.blt"));
        choice->setCurrentIndex(choice->findData(oldId)); QCOMPARE(text->toPlainText(), old.text);
        archive.save(bulletin("ANS-320.blt", "Neu eingetroffen", 60)); reader->setEntries(archive.entries());
        QCOMPARE(choice->currentData().toString(), oldId); QCOMPARE(text->toPlainText(), old.text);
        reader->findChild<QDialogButtonBox*>("bulletinReaderButtons")->button(QDialogButtonBox::Close)->click();
        QTRY_VERIFY(reader.isNull());
    }
    void readerIgnoresStaleLoadsAndHandlesEmptyArchive() {
        QPointer<BulletinDialog> reader = new BulletinDialog({}); reader->show(); reader->setEntries({});
        auto* choice = reader->findChild<QComboBox*>("bulletinArchiveChoice");
        auto* text = reader->findChild<QPlainTextEdit*>("bulletinReaderText");
        QCOMPARE(choice->count(), 0); QVERIFY(text->toPlainText().isEmpty());
        const auto one = bulletin("one.blt", "Eins"), two = bulletin("two.blt", "Zwei", 1);
        const auto first = BulletinArchive::idFor(one), second = BulletinArchive::idFor(two);
        reader->setEntries({{first, one.filename, {}, one.receivedAt}, {second, two.filename, {}, two.receivedAt}});
        choice->setCurrentIndex(1); reader->showArchived(first, one); QVERIFY(text->toPlainText().isEmpty());
        reader->showArchived(second, two); QCOMPARE(text->toPlainText(), QString("Zwei"));
        reader->close(); QTRY_VERIFY(reader.isNull());
    }
    void clickingBulletinOpensReaderButSelectionDoesNot() {
        BeaconView view; view.resize(700, 350); view.show(); unsigned requested = 0;
        view.archiveRequested = [&] { ++requested; };
        auto* text = view.findChild<QPlainTextEdit*>("beaconBulletinText");
        QTest::mouseClick(text->viewport(), Qt::LeftButton); QCOMPARE(requested, 1u);
        QTest::mouseClick(text->viewport(), Qt::RightButton); QCOMPARE(requested, 1u);
        QTest::keyClick(text, Qt::Key_Return); QCOMPARE(requested, 2u);
        QTest::mousePress(text->viewport(), Qt::LeftButton, Qt::NoModifier, QPoint(5, 5));
        QTest::mouseRelease(text->viewport(), Qt::LeftButton, Qt::NoModifier, QPoint(80, 25)); QCOMPARE(requested, 2u);
    }
};
QTEST_MAIN(BulletinArchiveTest)
#include "test_bulletin_archive.moc"
