// SPDX-License-Identifier: GPL-3.0-or-later
#include "received_archive.hpp"
#include <QBuffer>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSaveFile>
#include <QUuid>
#include <stdexcept>
namespace mm_sstv {
ReceivedArchive::ReceivedArchive(QString directory)
    : directory_(QFileInfo(directory.isEmpty() ? QDir::current().filePath("rxpic") : directory).absoluteFilePath()) {}
void ReceivedArchive::ensureDirectory() const {
    if (!QDir().mkpath(directory_))
        throw std::runtime_error(("Empfangsordner konnte nicht angelegt werden: " + directory_).toStdString());
}
QString ReceivedArchive::save(const QString& name, const QByteArray& data, const QString& extension) {
    ensureDirectory();
    auto stem = QFileInfo(name).completeBaseName();
    stem.replace(QRegularExpression("[^A-Za-z0-9_.-]"), "_");
    while (stem.startsWith('.')) stem.remove(0, 1);
    if (stem.isEmpty()) stem = "Empfangsbild";
    stem = stem.left(80);
    const auto filename = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss_zzz_") + stem + "_" +
        QUuid::createUuid().toString(QUuid::WithoutBraces) + "." + extension;
    const auto path = QDir(directory_).filePath(filename);
    QSaveFile output(path);
    if (!output.open(QIODevice::WriteOnly) || output.write(data) != data.size() || !output.commit())
        throw std::runtime_error(("Empfangsbild konnte nicht gespeichert werden: " + output.errorString()).toStdString());
    return path;
}
QString ReceivedArchive::saveJpeg(const QString& name, const std::vector<std::uint8_t>& jpeg) {
    const QByteArray bytes(reinterpret_cast<const char*>(jpeg.data()), int(jpeg.size()));
    const auto key = QCryptographicHash::hash(name.toUtf8() + '\0' + bytes, QCryptographicHash::Sha256);
    if (savedJpegs_.contains(key) && QFileInfo::exists(savedJpegs_.value(key))) return savedJpegs_.value(key);
    const auto path = save(name, bytes, "jpg"); savedJpegs_.insert(key, path); return path;
}
QString ReceivedArchive::savePng(const QString& name, const QImage& picture) {
    QByteArray bytes; QBuffer buffer(&bytes); buffer.open(QIODevice::WriteOnly);
    if (picture.isNull() || !picture.save(&buffer, "PNG")) throw std::runtime_error("SSTV-Empfangsbild konnte nicht als PNG kodiert werden.");
    return save(name, bytes, "png");
}
}
