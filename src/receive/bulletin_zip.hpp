// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QByteArray>
namespace mm_sstv {
// Bounded, in-memory ZIP reader for one .blt member (Stored or Deflate).
// No archive member is extracted to the filesystem.
QByteArray readBulletinZip(const QByteArray& archive);
inline constexpr unsigned maximumBulletinTextBytes = 4 * 1024 * 1024;
}
