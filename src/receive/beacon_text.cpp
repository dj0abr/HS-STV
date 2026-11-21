// SPDX-License-Identifier: GPL-3.0-or-later
#include "beacon_text.hpp"
#include <array>
#include <cstdint>
namespace mm_sstv {
namespace {
bool validUtf8(const QByteArray& bytes) {
    for (int i = 0; i < bytes.size();) {
        const auto lead = static_cast<unsigned char>(bytes[i++]);
        if (lead < 0x80) continue;
        unsigned remaining; std::uint32_t value, minimum;
        if (lead >= 0xc2 && lead <= 0xdf) { remaining = 1; value = lead & 0x1f; minimum = 0x80; }
        else if (lead >= 0xe0 && lead <= 0xef) { remaining = 2; value = lead & 0x0f; minimum = 0x800; }
        else if (lead >= 0xf0 && lead <= 0xf4) { remaining = 3; value = lead & 7; minimum = 0x10000; }
        else return false;
        if (bytes.size() - i < int(remaining)) return false;
        while (remaining--) {
            const auto next = static_cast<unsigned char>(bytes[i++]);
            if ((next & 0xc0) != 0x80) return false;
            value = (value << 6) | (next & 0x3f);
        }
        if (value < minimum || value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff)) return false;
    }
    return true;
}
}
QString decodeBeaconText(const QByteArray& bytes) {
    QString result;
    if (validUtf8(bytes)) {
        result = QString::fromUtf8(bytes);
        if (result.startsWith(QChar(0xfeff))) result.remove(0, 1);
    } else {
        // Explicit Windows-1252 fallback, also available in Qt 6 without Core5Compat.
        static constexpr std::array<unsigned short, 32> cp1252{
            0x20ac,0xfffd,0x201a,0x0192,0x201e,0x2026,0x2020,0x2021,
            0x02c6,0x2030,0x0160,0x2039,0x0152,0xfffd,0x017d,0xfffd,
            0xfffd,0x2018,0x2019,0x201c,0x201d,0x2022,0x2013,0x2014,
            0x02dc,0x2122,0x0161,0x203a,0x0153,0xfffd,0x017e,0x0178
        };
        for (const unsigned char c : bytes) result += QChar(c >= 0x80 && c < 0xa0 ? cp1252[c - 0x80] : c);
    }
    return result;
}
}
