// SPDX-License-Identifier: GPL-3.0-or-later
#include "beacon_view.hpp"
#include "language.hpp"
#include "theme.hpp"
#include <QApplication>
#include <QGroupBox>
#include <QSplitter>
#include <QPlainTextEdit>
#include <QLabel>
#include <QScrollBar>
#include <QTextCursor>
#include <QTextDocument>
#include <QTest>
using namespace mm_sstv;
class BeaconViewTest final : public QObject {
    Q_OBJECT
    BeaconMessage message(int n, BeaconChannel channel = BeaconChannel::CwSkimmer) {
        BeaconMessage result; result.channel = channel;
        result.receivedAt = QDateTime::fromString("2026-11-21T13:00:00Z", Qt::ISODate);
        result.text = QString("SPOT-%1  14025.0 kHz · DL0ABC · CQ ").arg(n) + QString(180, 'x');
        return result;
    }
private slots:
    void initTestCase() { applyDarkTheme(*qApp); }
    void bulletinHeadingHasRoomInBothLanguages() {
        BeaconView view; view.resize(900, 350); view.show();
        auto* text = view.findChild<QPlainTextEdit*>("beaconBulletinText");
        auto* group = qobject_cast<QGroupBox*>(text->parentWidget()); QVERIFY(group);
        auto* heading = view.findChild<QLabel*>("beaconBulletinHeading"); QVERIFY(heading);
        QVERIFY(group->title().isEmpty());
        auto* columns = view.findChild<QSplitter*>("beaconColumns"); QVERIFY(columns);
        for (const auto& language : {QString("de"), QString("en")}) {
            Language::instance().setCode(language);
            for (int extra : {0, 5}) {
                auto font = view.font(); font.setPointSize(9 + extra); heading->setFont(font);
                columns->setSizes({10000, 1}); QCoreApplication::processEvents();
                const int ink = heading->fontMetrics().boundingRect(heading->text()).width();
                QVERIFY(heading->isVisible()); QVERIFY(heading->contentsRect().width() >= ink + 8);
                const QRect headingArea(heading->mapTo(&view, QPoint()), heading->size());
                const QRect frameArea(group->mapTo(&view, QPoint()), group->size());
                QVERIFY(!headingArea.intersects(frameArea));
                QCOMPARE(heading->text(), uiText("Bulletins – zum Vergrößern anklicken"));
            }
        }
        Language::instance().setCode("de");
    }
    void channelsAreSeparateAndTextIsPlain() {
        BeaconView view; view.resize(900, 350); view.show();
        auto cw = message(1); cw.text = "<b>DL1TEST & CQ</b>"; view.appendMessage(cw);
        view.appendMessage(message(2, BeaconChannel::DxCluster));
        auto* cwText = view.findChild<QPlainTextEdit*>("beaconCwText");
        auto* dxText = view.findChild<QPlainTextEdit*>("beaconDxText");
        auto* bulletin = view.findChild<QPlainTextEdit*>("beaconBulletinText");
        QVERIFY(cwText->isReadOnly() && dxText->isReadOnly() && bulletin->isReadOnly());
        QCOMPARE(cwText->toPlainText(), QString("13:00:00Z  <b>DL1TEST & CQ</b>"));
        QVERIFY(dxText->toPlainText().contains("SPOT-2")); QVERIFY(!dxText->toPlainText().contains("DL1TEST"));
        QVERIFY(bulletin->toPlainText().isEmpty());
        view.resize(640, 240); QTest::qWait(20); QCOMPARE(view.width(), 640);
    }
    void messageHistoryIsBounded() {
        BeaconView view;
        for (int n = 0; n < 1100; ++n) view.appendMessage(message(n));
        auto* text = view.findChild<QPlainTextEdit*>("beaconCwText");
        QCOMPARE(text->document()->blockCount(), BeaconView::maximumMessages);
        QVERIFY(text->toPlainText().startsWith("13:00:00Z  SPOT-100 "));
        QVERIFY(text->toPlainText().contains("SPOT-1099 "));
    }
    void bulletinIsKeptDuringIncompleteReceptionAndDuplicatesDoNotScroll() {
        BeaconView view; view.resize(700, 350); view.show();
        Bulletin bulletin; bulletin.filename = "news.blt"; bulletin.digest = "first";
        bulletin.receivedAt = QDateTime::currentDateTimeUtc(); bulletin.text = QString("<b>Plain text</b>\n").repeated(100);
        view.showBulletin(bulletin); QTest::qWait(20);
        auto* text = view.findChild<QPlainTextEdit*>("beaconBulletinText");
        QCOMPARE(text->toPlainText(), bulletin.text);
        auto* scroll = text->verticalScrollBar(); scroll->setValue(20); const int reading = scroll->value();
        view.showBulletinProgress({"next.blt", {}, 2, 8, false, false});
        QCOMPARE(text->toPlainText(), bulletin.text);
        QVERIFY(view.findChild<QLabel*>("beaconBulletinStatus")->text().contains("2/8"));
        view.showBulletinProgress({"next.blt", "ZIP-Prüfsumme ist falsch", 8, 8, false, true});
        QCOMPARE(text->toPlainText(), bulletin.text);
        view.showBulletin(bulletin); QCOMPARE(scroll->value(), reading);
        bulletin.digest = "second"; bulletin.text = "Neues Bulletin";
        view.showBulletin(bulletin); QCOMPARE(text->toPlainText(), bulletin.text); QCOMPARE(scroll->value(), 0);
    }
    void longBulletinShowsBeginningAndTruncationMarker() {
        BeaconView view; Bulletin bulletin; bulletin.filename = "long.blt"; bulletin.digest = "long";
        bulletin.title = "<img src='file:///not-a-resource'>";
        bulletin.text = "Anfang\n" + QString(80000, 'x'); view.showBulletin(bulletin);
        const auto text = view.findChild<QPlainTextEdit*>("beaconBulletinText")->toPlainText();
        QVERIFY(text.startsWith("Anfang\n")); QVERIFY(text.endsWith("Anzeige gekürzt")); QVERIFY(text.size() < 66000);
        const auto tooltip = view.findChild<QPlainTextEdit*>("beaconBulletinText")->toolTip();
        QVERIFY(tooltip.contains("&lt;img")); QVERIFY(!tooltip.contains("<img"));
    }
    void readingSelectionAndHorizontalPositionSurviveNewMessages() {
        BeaconView view; view.resize(700, 350); view.show();
        for (int n = 0; n < 200; ++n) view.appendMessage(message(n));
        QTest::qWait(20);
        auto* text = view.findChild<QPlainTextEdit*>("beaconCwText");
        auto selection = text->document()->find("SPOT-80"); QVERIFY(!selection.isNull()); text->setTextCursor(selection);
        auto* vertical = text->verticalScrollBar(); vertical->setValue(40);
        auto* horizontal = text->horizontalScrollBar(); horizontal->setValue(50);
        const int before = vertical->value(), left = horizontal->value();
        view.appendMessage(message(200)); QTest::qWait(20);
        QCOMPARE(vertical->value(), before); QCOMPARE(horizontal->value(), left);
        QCOMPARE(text->textCursor().selectedText(), QString("SPOT-80"));
        selection.clearSelection(); text->setTextCursor(selection); vertical->setValue(vertical->maximum());
        view.appendMessage(message(201)); QTest::qWait(20);
        QCOMPARE(vertical->value(), vertical->maximum());
        for (int n = 202; n < 1000; ++n) view.appendMessage(message(n));
        vertical->setValue(100); view.appendMessage(message(1000)); QTest::qWait(20);
        QCOMPARE(vertical->value(), 99);
    }
};
QTEST_MAIN(BeaconViewTest)
#include "test_beacon_view.moc"
