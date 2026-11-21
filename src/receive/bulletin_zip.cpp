// SPDX-License-Identifier: GPL-3.0-or-later
// ZIP structures: PKWARE APPNOTE 6.3.10, sections 4.3.7, 4.3.12 and 4.3.16.
#include "bulletin_zip.hpp"
#include <zlib.h>
#include <cstdint>
#include <stdexcept>
namespace mm_sstv {
namespace {
void check(bool ok, const char* reason) { if (!ok) throw std::runtime_error(reason); }
bool range(const QByteArray& data, std::uint64_t at, std::uint64_t count) {
    return at <= std::uint64_t(data.size()) && count <= std::uint64_t(data.size()) - at;
}
std::uint32_t u16(const QByteArray& data, std::uint64_t at) {
    check(range(data, at, 2), "ZIP-Datei ist unvollständig");
    const auto* p = reinterpret_cast<const unsigned char*>(data.constData() + at);
    return p[0] | (std::uint32_t(p[1]) << 8);
}
std::uint32_t u32(const QByteArray& data, std::uint64_t at) {
    return u16(data, at) | (u16(data, at + 2) << 16);
}
}
QByteArray readBulletinZip(const QByteArray& data) {
    // The radio format itself limits compressed files to 1024 payload blocks.
    check(data.size() >= 22 && data.size() <= 219 * 1024 - 55, "Ungültige Bulletin-ZIP-Größe");
    int end = -1;
    for (int at = int(data.size()) - 22; at >= 0 && at >= data.size() - 65557; --at) {
        if (u32(data, at) == 0x06054b50 && std::uint64_t(at) + 22 + u16(data, at + 20) == std::uint64_t(data.size())) {
            end = at; break;
        }
    }
    check(end >= 0, "ZIP-Abschluss fehlt");
    check(u16(data, end + 4) == 0 && u16(data, end + 6) == 0, "Mehrteilige ZIP-Dateien werden nicht unterstützt");
    const auto entries = u16(data, end + 10);
    check(entries > 0 && entries <= 256 && u16(data, end + 8) == entries, "Ungültiges ZIP-Verzeichnis");
    const std::uint64_t central = u32(data, end + 16), size = u32(data, end + 12);
    check(range(data, central, size) && central + size == std::uint64_t(end), "Ungültige ZIP-Verzeichnisgrenzen");
    std::uint64_t at = central;
    QByteArray result;
    bool found = false;
    for (unsigned entry = 0; entry < entries; ++entry) {
        check(range(data, at, 46) && u32(data, at) == 0x02014b50, "Ungültiger ZIP-Verzeichniseintrag");
        const auto flags = u16(data, at + 8), method = u16(data, at + 10);
        const auto crc = u32(data, at + 16), compressed = u32(data, at + 20), unpacked = u32(data, at + 24);
        const auto nameSize = u16(data, at + 28), extraSize = u16(data, at + 30), commentSize = u16(data, at + 32);
        const std::uint64_t next = at + 46 + nameSize + extraSize + commentSize, local = u32(data, at + 42);
        check(nameSize > 0 && next <= central + size && u16(data, at + 34) == 0, "Ungültiger ZIP-Dateiname");
        const auto name = data.mid(int(at + 46), int(nameSize));
        check(!name.contains('\0'), "Ungültiger ZIP-Dateiname");
        check((flags & ~0x080eU) == 0 && (method == 0 || method == 8), "ZIP-Kompression oder Verschlüsselung wird nicht unterstützt");
        check(local < central && range(data, local, 30) && u32(data, local) == 0x04034b50, "ZIP-Dateikopf fehlt");
        check(u16(data, local + 6) == flags && u16(data, local + 8) == method, "Widersprüchliche ZIP-Dateiköpfe");
        const auto localName = u16(data, local + 26), localExtra = u16(data, local + 28);
        const std::uint64_t payload = local + 30 + localName + localExtra;
        check(payload <= central && compressed <= central - payload &&
              data.mid(int(local + 30), int(localName)) == name, "Ungültige ZIP-Nutzdatengrenzen");
        if (!(flags & 8)) {
            check(u32(data, local + 14) == crc && u32(data, local + 18) == compressed &&
                  u32(data, local + 22) == unpacked, "Widersprüchliche ZIP-Größen oder Prüfsummen");
        } else {
            const std::uint64_t descriptor = payload + compressed;
            const auto matches = [&](std::uint64_t pos) {
                return pos + 12 <= central && u32(data, pos) == crc &&
                       u32(data, pos + 4) == compressed && u32(data, pos + 8) == unpacked;
            };
            check(matches(descriptor) || (descriptor + 16 <= central && u32(data, descriptor) == 0x08074b50 && matches(descriptor + 4)),
                  "Ungültiger ZIP-Datendeskriptor");
        }
        const bool directory = name.endsWith('/');
        if (directory) check(unpacked == 0 && crc == 0, "Ungültiger ZIP-Verzeichniseintrag");
        else {
            check(!found && name.toLower().endsWith(".blt"), "ZIP muss genau eine Bulletin-Datei enthalten");
            check(unpacked <= maximumBulletinTextBytes, "Bulletin ist größer als 4 MiB");
            found = true;
            if (method == 0) {
                check(compressed == unpacked, "Ungültige unkomprimierte ZIP-Datei");
                result = data.mid(int(payload), int(compressed));
            } else {
                result.resize(int(unpacked) + 1); // One spare byte detects an understated size.
                z_stream stream{};
                check(inflateInit2(&stream, -MAX_WBITS) == Z_OK, "ZIP-Entpacker konnte nicht gestartet werden");
                stream.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(data.constData() + payload));
                stream.avail_in = compressed;
                stream.next_out = reinterpret_cast<Bytef*>(result.data()); stream.avail_out = unsigned(result.size());
                const int status = inflate(&stream, Z_FINISH);
                const bool valid = status == Z_STREAM_END && stream.total_in == compressed && stream.total_out == unpacked;
                inflateEnd(&stream);
                check(valid, "ZIP-Daten sind beschädigt oder überschreiten die angegebene Größe");
                result.resize(int(unpacked));
            }
            check(::crc32(0, reinterpret_cast<const Bytef*>(result.constData()), unsigned(result.size())) == crc,
                  "ZIP-Prüfsumme ist falsch");
        }
        at = next;
    }
    check(at == central + size && found, "Bulletin fehlt im ZIP-Verzeichnis");
    return result;
}
}
