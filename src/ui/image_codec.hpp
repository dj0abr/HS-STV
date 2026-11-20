// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "mm_sstv/sstv_audio.hpp"
#include <QImage>
#include <QSize>
#include <cstdint>
#include <vector>
namespace mm_sstv {
struct PreparedPicture { QImage preview; std::vector<std::uint8_t> jpeg; int quality = 0; };
QImage fitPicture(const QImage& original, QSize target);
SstvImage sstvRaster(const QImage& picture);
QImage decodeReceivedJpeg(const std::vector<std::uint8_t>& bytes, bool complete);
PreparedPicture preparePicture(const QImage& original, QSize target);
}
