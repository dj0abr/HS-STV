// SPDX-License-Identifier: GPL-3.0-or-later
// Independent offline ZIP writer and radio-file fixture; no production ZIP code.
#pragma once
#include "mm_sstv/image_assembler.hpp"
#include <QByteArray>
#include <QString>
#include <zlib.h>
#include <algorithm>
#include <stdexcept>
#include <vector>
namespace beacon_test {
inline void put16(QByteArray& out, unsigned n) { out.append(char(n)); out.append(char(n >> 8)); }
inline void put32(QByteArray& out, unsigned n) { put16(out, n); put16(out, n >> 16); }
inline void set16(QByteArray& out, int at, unsigned n) { out[at] = char(n); out[at + 1] = char(n >> 8); }
inline void set32(QByteArray& out, int at, unsigned n) { set16(out, at, n); set16(out, at + 2, n >> 16); }
inline unsigned get32(const QByteArray& out, int at) {
    unsigned value = 0; for (unsigned i = 0; i < 4; ++i) value |= unsigned(static_cast<unsigned char>(out[at + int(i)])) << (8 * i);
    return value;
}
inline QByteArray zip(const QByteArray& text, const QByteArray& name = "news.blt", bool stored = false, bool descriptor = false) {
    QByteArray compressed = text;
    if (!stored) {
        z_stream stream{};
        if (deflateInit2(&stream, 6, Z_DEFLATED, -MAX_WBITS, 8, Z_DEFAULT_STRATEGY) != Z_OK) throw std::runtime_error("Test compression failed");
        compressed.resize(int(compressBound(unsigned(text.size()))) + 32);
        stream.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(text.constData())); stream.avail_in = unsigned(text.size());
        stream.next_out = reinterpret_cast<Bytef*>(compressed.data()); stream.avail_out = unsigned(compressed.size());
        const int result = deflate(&stream, Z_FINISH); const auto size = stream.total_out; deflateEnd(&stream);
        if (result != Z_STREAM_END) throw std::runtime_error("Test compression failed");
        compressed.resize(int(size));
    }
    const auto crc = unsigned(::crc32(0, reinterpret_cast<const Bytef*>(text.constData()), unsigned(text.size())));
    const unsigned flags = descriptor ? 8 : 0, method = stored ? 0 : 8;
    QByteArray out;
    put32(out, 0x04034b50); put16(out, 20); put16(out, flags); put16(out, method); put16(out, 0); put16(out, 0);
    put32(out, descriptor ? 0 : crc); put32(out, descriptor ? 0 : unsigned(compressed.size())); put32(out, descriptor ? 0 : unsigned(text.size()));
    put16(out, unsigned(name.size())); put16(out, 0); out += name; out += compressed;
    if (descriptor) { put32(out, 0x08074b50); put32(out, crc); put32(out, unsigned(compressed.size())); put32(out, unsigned(text.size())); }
    const auto central = unsigned(out.size());
    put32(out, 0x02014b50); put16(out, 20); put16(out, 20); put16(out, flags); put16(out, method); put16(out, 0); put16(out, 0);
    put32(out, crc); put32(out, unsigned(compressed.size())); put32(out, unsigned(text.size())); put16(out, unsigned(name.size()));
    put16(out, 0); put16(out, 0); put16(out, 0); put16(out, 0); put32(out, 0); put32(out, 0); out += name;
    const auto centralSize = unsigned(out.size()) - central;
    put32(out, 0x06054b50); put16(out, 0); put16(out, 0); put16(out, 1); put16(out, 1);
    put32(out, centralSize); put32(out, central); put16(out, 0); return out;
}
inline std::vector<mm_sstv::ReceivedFrame> frames(const QByteArray& data, const QByteArray& name = "news.blt", unsigned kind = 3, int fileId = -1) {
    QByteArray header = name.left(50).leftJustified(50, '\0');
    const unsigned crc = fileId < 0 ? mm_sstv::crc16(reinterpret_cast<const std::uint8_t*>(data.constData()), std::size_t(data.size())) : unsigned(fileId);
    header.append(char(crc >> 8)); header.append(char(crc));
    header.append(char(data.size() >> 16)); header.append(char(data.size() >> 8)); header.append(char(data.size()));
    const auto stream = header + data;
    const unsigned count = unsigned((stream.size() + 218) / 219);
    std::vector<mm_sstv::ReceivedFrame> result;
    for (unsigned n = 0; n < count; ++n) {
        mm_sstv::ReceivedFrame frame; frame.type = kind; frame.counter = n;
        frame.status = count == 1 ? 3 : n == 0 ? 0 : n + 1 == count ? 2 : 1;
        const auto part = stream.mid(int(n * 219), 219);
        std::copy(part.begin(), part.end(), frame.payload.begin()); result.push_back(frame);
    }
    return result;
}
inline QByteArray content() {
    return QStringLiteral("~B:1:MM-Bake Test~\r\n<h3>Test-Bulletin</h3><p>Grüße aus München &amp; 73.</p><script>evil()</script>").toUtf8();
}
}
