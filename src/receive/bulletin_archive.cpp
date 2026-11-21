// SPDX-License-Identifier: GPL-3.0-or-later
#include "bulletin_archive.hpp"
#include "bulletin_receiver.hpp"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <algorithm>
#include <stdexcept>
namespace mm_sstv {
namespace {
constexpr qint64 maximumRecordBytes = 32 * 1024 * 1024;
void check(bool ok, const QString& error) { if (!ok) throw std::runtime_error(error.toUtf8().constData()); }
QString textHash(const QString& text) {
    return QString::fromLatin1(QCryptographicHash::hash(text.toUtf8(), QCryptographicHash::Sha256).toHex());
}
}
BulletinArchive::BulletinArchive(QString directory)
    : directory_(QFileInfo(directory.isEmpty() ? QDir::current().filePath("bulletins") : directory).absoluteFilePath()) {}
QString BulletinArchive::idFor(const Bulletin& bulletin) {
    auto stem = QFileInfo(bulletin.filename).completeBaseName().toLower();
    stem.replace(QRegularExpression("[^a-z0-9_-]"), "_"); stem = stem.left(40);
    if (stem.isEmpty()) stem = "bulletin";
    return "bulletin_" + stem + "_" + QString::fromLatin1(bulletin.digest.toHex()) + ".json";
}
QString BulletinArchive::save(const Bulletin& bulletin, bool* added) {
    if (added) *added = false;
    check(bulletin.digest.size() == 32 && !bulletin.filename.isEmpty() && bulletin.receivedAt.isValid(), "Ungültiges Bulletin für das Archiv");
    const auto id = idFor(bulletin);
    try {
        const auto existing = load(id);
        if (existing.digest == bulletin.digest && existing.text == bulletin.text &&
            existing.filename.compare(bulletin.filename, Qt::CaseInsensitive) == 0) return id;
    } catch (const std::exception&) { /* A damaged record can be repaired by a valid repeat. */ }
    check(QDir().mkpath(directory_), "Bulletin-Archivordner konnte nicht angelegt werden: " + directory_);
    const QJsonObject object{{"version", 1}, {"filename", bulletin.filename}, {"title", bulletin.title},
        {"category", bulletin.category}, {"page", int(bulletin.page)}, {"text", bulletin.text},
        {"digest", QString::fromLatin1(bulletin.digest.toHex())}, {"text_sha256", textHash(bulletin.text)},
        {"received_at", bulletin.receivedAt.toUTC().toString(Qt::ISODateWithMs)}};
    const auto bytes = QJsonDocument(object).toJson(QJsonDocument::Compact);
    check(bytes.size() <= maximumRecordBytes, "Bulletin ist zu groß für das Archiv");
    QSaveFile file(QDir(directory_).filePath(id));
    check(file.open(QIODevice::WriteOnly), "Bulletin konnte nicht gespeichert werden: " + file.errorString());
    check(file.write(bytes) == bytes.size() && file.commit(), "Bulletin konnte nicht vollständig gespeichert werden: " + file.errorString());
    if (added) *added = true;
    return id;
}
Bulletin BulletinArchive::load(const QString& id) const {
    static const QRegularExpression validId("^bulletin_[a-z0-9_-]+_[0-9a-f]{64}\\.json$");
    check(validId.match(id).hasMatch(), "Ungültiger Bulletin-Archiveintrag");
    const auto path = QDir(directory_).filePath(id); const QFileInfo info(path);
    check(info.isFile() && !info.isSymLink() && info.size() <= maximumRecordBytes, "Bulletin-Datei fehlt oder ist ungültig");
    QFile file(path); check(file.open(QIODevice::ReadOnly), "Bulletin konnte nicht gelesen werden: " + file.errorString());
    const auto bytes = file.read(maximumRecordBytes + 1); check(bytes.size() <= maximumRecordBytes, "Bulletin-Datei ist zu groß");
    QJsonParseError error; const auto json = QJsonDocument::fromJson(bytes, &error).object();
    check(error.error == QJsonParseError::NoError && json.value("version").toInt() == 1 &&
          json.value("text").isString() && json.value("filename").isString(), "Bulletin-Archiveintrag ist beschädigt");
    Bulletin bulletin; bulletin.filename = json.value("filename").toString(); bulletin.title = json.value("title").toString();
    bulletin.text = json.value("text").toString(); bulletin.category = json.value("category").toString();
    bulletin.page = unsigned(json.value("page").toInt());
    const auto digest = json.value("digest").toString();
    check(QRegularExpression("^[0-9a-f]{64}$").match(digest).hasMatch(), "Ungültige Bulletin-Prüfsumme im Archiv");
    bulletin.digest = QByteArray::fromHex(digest.toLatin1());
    bulletin.receivedAt = QDateTime::fromString(json.value("received_at").toString(), Qt::ISODateWithMs);
    check(bulletin.receivedAt.isValid() && idFor(bulletin) == id && json.value("text_sha256").toString() == textHash(bulletin.text),
          "Bulletin-Archiveintrag ist unvollständig oder beschädigt");
    return bulletin;
}
std::vector<BulletinArchiveEntry> BulletinArchive::entries() const {
    std::vector<BulletinArchiveEntry> result;
    const QDir dir(directory_);
    for (const auto& id : dir.entryList({"bulletin_*.json"}, QDir::Files | QDir::NoSymLinks)) {
        try { const auto bulletin = load(id); result.push_back({id, bulletin.filename, bulletin.title, bulletin.receivedAt}); }
        catch (const std::exception&) { /* Ignore partial or damaged files; never show them as verified bulletins. */ }
    }
    std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) {
        return a.receivedAt == b.receivedAt ? a.id < b.id : a.receivedAt > b.receivedAt;
    });
    return result;
}
}
