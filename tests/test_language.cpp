// SPDX-License-Identifier: GPL-3.0-or-later
#include "language.hpp"
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QTest>
#include <algorithm>
using namespace mm_sstv;
class LanguageTest final : public QObject {
    Q_OBJECT
private slots:
    void cleanup() { Language::instance().setCode("de"); }
    void embeddedCatalogAndPlaceholders() {
        Language::instance().setCode("en"); QCOMPARE(uiText("Bild"), QString("Image"));
        QFile file(":/translations/en.json"); QVERIFY(file.open(QIODevice::ReadOnly));
        const auto catalog = QJsonDocument::fromJson(file.readAll()).object(); QVERIFY(catalog.size() >= 200);
        const QRegularExpression placeholder("%[1-9][0-9]*");
        const auto args = [&](const QString& text) { QStringList result; auto matches = placeholder.globalMatch(text);
            while (matches.hasNext()) { result.push_back(matches.next().captured()); }
            std::sort(result.begin(), result.end()); return result; };
        for (auto it = catalog.begin(); it != catalog.end(); ++it) {
            QVERIFY2(!it.value().toString().isEmpty(), qPrintable(it.key()));
            QCOMPARE(args(it.key()), args(it.value().toString()));
            QCOMPARE(uiText(it.key().toUtf8().constData()), it.value().toString());
        }
        QCOMPARE(uiText("unknown-key"), QString("unknown-key"));
    }
    void translatedDiagnosticsPreservePayloads() {
        Language::instance().setCode("en");
        const QString filename = QStringLiteral("/tmp/Bild %2 Grüße.blt");
        QCOMPARE(uiMessage(filename + " · 2/4 Blöcke"), filename + " · 2/4 blocks");
        QCOMPARE(uiMessage("Bulletin konnte nicht gespeichert werden: " + filename), "Could not save bulletin: " + filename);
        QCOMPARE(uiMessage("Ausgang „Bild“ nicht verfügbar. Bitte unter Setup auswählen oder aktualisieren."), QString("Output “Bild” unavailable. Please select or refresh it in Setup."));
        Language::instance().setCode("de");
        QCOMPARE(uiMessage(filename + " · 2/4 blocks"), filename + " · 2/4 Blöcke");
        QCOMPARE(uiMessage("Could not save bulletin: " + filename), "Bulletin konnte nicht gespeichert werden: " + filename);
        QCOMPARE(uiText("Bild"), QString("Bild"));
        Language::instance().setCode("invalid"); QCOMPARE(Language::code(), QString("de"));
    }
};
QTEST_MAIN(LanguageTest)
#include "test_language.moc"
