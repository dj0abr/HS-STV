// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QByteArray>
#include <QString>
namespace mm_sstv {
QString decodeBeaconText(const QByteArray& bytes);
}
