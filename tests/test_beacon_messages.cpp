// SPDX-License-Identifier: GPL-3.0-or-later
// Protocol cases adapted from QO100_TRX2's collector tests; no Python runtime.
#include "beacon_messages.hpp"
#include <QCoreApplication>
#include <QByteArray>
#include <algorithm>
#include <iostream>
#include <stdexcept>
using namespace mm_sstv;
namespace {
void require(bool ok, const char* why) { if (!ok) throw std::runtime_error(why); }
ReceivedFrame spot(unsigned subtype, const QByteArray& raw, int declaredLength = -1) {
    ReceivedFrame frame; frame.type = 8; frame.status = 3; frame.payload.fill(0xee);
    frame.payload[0] = static_cast<std::uint8_t>(subtype);
    const unsigned start = subtype == 0 ? 2 : 1;
    if (subtype == 0) frame.payload[1] = static_cast<std::uint8_t>(declaredLength >= 0 ? declaredLength : raw.size());
    const auto size = std::min(int(frame.payload.size() - start), int(raw.size()));
    std::copy_n(raw.constData(), size, frame.payload.begin() + start);
    if (size < int(frame.payload.size() - start)) frame.payload[start + size] = 0;
    return frame;
}
}
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    try {
        const auto now = QDateTime::fromString("2026-11-21T13:00:00Z", Qt::ISODate);
        BeaconMessages messages;
        auto dx = messages.accept(spot(0, "DL1AAA~14025.0~VK9TEST~CQ DX~1300Z~JO40", 217), now);
        require(dx && dx->channel == BeaconChannel::DxCluster, "DX channel missing");
        require(dx->fields["callsign"] == "VK9TEST" && dx->fields["frequency"] == "14025.0", "Wrong DX fields");
        require(dx->text.contains("14025.0 kHz") && dx->text.endsWith("JO40"), "Wrong DX formatting");
        require(!dx->raw.contains(QChar(0xfffd)), "DX padding leaked into text");
        const auto cwFrame = spot(2, "DL0ABC:~10489750~DL1TEST~18~23~CQ~1301Z");
        auto cw = messages.accept(cwFrame, now);
        require(cw && cw->channel == BeaconChannel::CwSkimmer, "CW channel missing");
        require(cw->fields["reporter"] == "DL0ABC" && cw->fields["signal_db"] == "18" && cw->fields["wpm"] == "23", "Wrong CW fields");
        require(cw->text.contains("18 dB · 23 WPM") && cw->text.contains("DL1TEST"), "Wrong CW formatting");
        require(!messages.accept(cwFrame, now.addSecs(60)) && !messages.accept(cwFrame, now.addSecs(120)), "Duplicate was displayed");
        require(messages.accept(cwFrame, now.addSecs(121)).has_value(), "Later repeat was suppressed");
        require(!messages.accept(cwFrame, now.addSecs(-60)), "Duplicate across clock rollback was displayed");
        auto raw = messages.accept(spot(2, "NEW CW FORMAT"), now);
        require(raw && raw->text == "NEW CW FORMAT" && raw->fields.isEmpty(), "Unknown format was lost");
        require(messages.accept(spot(0, "NEW CW FORMAT"), now).has_value(), "Different channel was suppressed");
        auto unicode = messages.accept(spot(2, QByteArray::fromHex("efbbbf") + QStringLiteral("Grüße aus München 📻").toUtf8()), now);
        require(unicode && unicode->raw == QStringLiteral("Grüße aus München 📻"), "UTF-8 or BOM decoding failed");
        auto legacy = messages.accept(spot(2, QByteArray::fromHex("4772fcdf6520802093445894")), now);
        require(legacy && legacy->raw == QStringLiteral("Grüße € “DX”"), "Windows-1252 decoding failed");
        auto controls = messages.accept(spot(2, "Alpha\r\nBeta\tEnd"), now);
        require(controls && controls->text == "Alpha  Beta End", "Control characters created display rows");
        auto markup = messages.accept(spot(2, "<b>CALL</b>"), now);
        require(markup && markup->text == "<b>CALL</b>", "Raw text was altered as markup");
        require(!messages.accept(spot(0, "bad", 218), now), "Invalid DX length accepted");
        require(!messages.accept(spot(2, ""), now) && !messages.accept(spot(0, ""), now), "Empty spot accepted");
        require(!messages.accept(spot(1, "spectrum"), now) && !messages.accept(spot(3, "spectrum"), now), "Spectrum mistaken for text");
        auto wrong = cwFrame; wrong.type = 3; require(!messages.accept(wrong, now), "File mistaken for spot");
        wrong = cwFrame; wrong.counter = 1024; require(!messages.accept(wrong, now), "Invalid counter accepted");
        wrong = cwFrame; wrong.status = 4; require(!messages.accept(wrong, now), "Invalid status accepted");
        require(!messages.accept(cwFrame, QDateTime()), "Invalid timestamp accepted");
        require(messages.accept(spot(0, QByteArray(217, 'X')), now)->raw.size() == 217, "Maximum DX length truncated");
        require(messages.accept(spot(2, QByteArray(218, 'Y')), now)->raw.size() == 218, "Maximum CW length truncated");
        std::cout << "Beacon text: DX/CW formats, padding, Unicode/CP1252, duplicate window, unknown formats and malformed frames passed\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
