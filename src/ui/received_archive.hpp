// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QHash>
#include <QByteArray>
#include <QImage>
#include <QString>
#include <cstdint>
#include <vector>
namespace mm_sstv {
// Qt filesystem adapter; received names never determine the output directory.
class ReceivedArchive final {
public:
    explicit ReceivedArchive(QString directory = {});
    const QString& directory() const { return directory_; }
    void ensureDirectory() const;
    QString saveJpeg(const QString& name, const std::vector<std::uint8_t>& jpeg);
    QString savePng(const QString& name, const QImage& picture);
private:
    QString save(const QString& name, const QByteArray& data, const QString& extension);
    QString directory_;
    QHash<QByteArray, QString> savedJpegs_;
};
}
