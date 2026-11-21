// SPDX-License-Identifier: GPL-3.0-or-later
#include "beacon_messages.hpp"
#include "beacon_text.hpp"
#include <QByteArray>
#include <QStringList>
#include <algorithm>
#include <array>
#include <cstdint>

namespace mm_sstv {
namespace {
QString decodeText(const QByteArray& bytes) {
    QString result = decodeBeaconText(bytes);
    // A spot occupies one display line; never interpret control bytes or HTML.
    for (int i = 0; i < result.size(); ++i)
        if (result[i].unicode() < 0x20 || result[i].unicode() == 0x7f ||
            result[i].unicode() == 0x2028 || result[i].unicode() == 0x2029) result[i] = QLatin1Char(' ');
    return result.trimmed();
}
}
std::optional<BeaconMessage> BeaconMessages::accept(const ReceivedFrame& frame, const QDateTime& receivedAt) {
    if (frame.type != 8 || frame.counter > 1023 || frame.status > 3 || !receivedAt.isValid()) return std::nullopt;
    const auto subtype = frame.payload[0];
    if (subtype != 0 && subtype != 2) return std::nullopt;
    const unsigned start = subtype == 0 ? 2 : 1;
    const unsigned length = subtype == 0 ? frame.payload[1] : 218;
    if (subtype == 0 && length > 217) return std::nullopt;
    QByteArray bytes(reinterpret_cast<const char*>(frame.payload.data() + start), int(length));
    const auto nul = bytes.indexOf('\0'); if (nul >= 0) bytes.truncate(nul);
    BeaconMessage message;
    message.channel = subtype == 0 ? BeaconChannel::DxCluster : BeaconChannel::CwSkimmer;
    message.receivedAt = receivedAt.toUTC(); message.raw = decodeText(bytes);
    if (message.raw.isEmpty()) return std::nullopt;
    for (const auto& seen : recent_)
        if (seen.channel == message.channel && seen.raw == message.raw &&
            seen.at.msecsTo(message.receivedAt) >= -qint64(duplicateSeconds) * 1000 &&
            seen.at.msecsTo(message.receivedAt) <= qint64(duplicateSeconds) * 1000) return std::nullopt;
    const QStringList keys = subtype == 0 ? QStringList{"reporter", "frequency", "callsign", "comment", "utc", "locator"} :
                                           QStringList{"reporter", "frequency", "callsign", "signal_db", "wpm", "comment", "utc"};
    const auto parts = message.raw.split('~');
    if (parts.size() >= keys.size()) {
        for (int i = 0; i < keys.size(); ++i) message.fields.insert(keys[i], parts[i].trimmed());
        auto& reporter = message.fields["reporter"];
        while (reporter.startsWith(':') || reporter.startsWith(' ')) reporter.remove(0, 1);
        while (reporter.endsWith(':') || reporter.endsWith(' ')) reporter.chop(1);
        auto detail = message.fields["comment"];
        if (subtype == 2) detail = QString("%1 dB · %2 WPM · %3").arg(message.fields["signal_db"], message.fields["wpm"], detail);
        message.text = QString("%1 %2 %3%4 · %5 · %6")
            .arg(message.fields["utc"].leftJustified(6), message.fields["callsign"].leftJustified(12),
                 message.fields["frequency"], subtype == 0 ? QStringLiteral(" kHz") : QString(), reporter, detail);
        if (!message.fields.value("locator").isEmpty()) message.text += " · " + message.fields["locator"];
        message.text = message.text.trimmed();
    } else message.text = message.raw; // Future formats remain readable.
    recent_.push_back({message.channel, message.raw, message.receivedAt});
    while (recent_.size() > maximumRecent) recent_.pop_front();
    return message;
}
}
