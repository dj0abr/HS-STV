// SPDX-License-Identifier: GPL-3.0-or-later
#include "beacon_view.hpp"
#include <QPlainTextEdit>
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
