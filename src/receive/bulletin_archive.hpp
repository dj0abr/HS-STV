// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QDateTime>
#include <QString>
#include <vector>
namespace mm_sstv {
struct Bulletin;
struct BulletinArchiveEntry {
    QString id, filename, title;
    QDateTime receivedAt;
};
// Full decoded text and original radio filename in atomic, versioned records.
// Invoke on a worker thread; archive paths never come from received directories.
class BulletinArchive final {
public:
    explicit BulletinArchive(QString directory = {});
    const QString& directory() const { return directory_; }
    static QString idFor(const Bulletin& bulletin);
    QString save(const Bulletin& bulletin, bool* added = nullptr);
    Bulletin load(const QString& id) const;
    std::vector<BulletinArchiveEntry> entries() const;
private:
    QString directory_;
};
}
