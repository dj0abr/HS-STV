// SPDX-License-Identifier: GPL-3.0-or-later
#include "language.hpp"
#include <QCoreApplication>
#include <QFile>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QTranslator>
#include <QLibraryInfo>
#include <algorithm>
#include <atomic>
#include <vector>
#include <stdexcept>
static void initializeTranslations() { Q_INIT_RESOURCE(translations); }
namespace mm_sstv {
namespace {
std::atomic<bool> english{false};
struct Entry { QString de, en; QRegularExpression dePattern, enPattern; std::vector<int> deArgs, enArgs; };
QRegularExpression pattern(const QString& text, std::vector<int>& args) {
    static const QRegularExpression placeholder("%([1-9][0-9]*)");
    QString regex = "\\A"; int at = 0;
    auto matches = placeholder.globalMatch(text);
    while (matches.hasNext()) {
        const auto match = matches.next();
        regex += QRegularExpression::escape(text.mid(at, match.capturedStart() - at)) + "([\\s\\S]*?)";
        args.push_back(match.captured(1).toInt()); at = match.capturedEnd();
    }
    regex += QRegularExpression::escape(text.mid(at)) + "\\z";
    return QRegularExpression(regex);
}
struct Catalog {
    QHash<QString, QString> en, de;
    std::vector<Entry> entries;
    Catalog() {
        initializeTranslations(); QFile file(":/translations/en.json");
        if (!file.open(QIODevice::ReadOnly)) throw std::runtime_error("Missing embedded language catalog");
        const auto document = QJsonDocument::fromJson(file.readAll());
        if (!document.isObject()) throw std::runtime_error("Invalid embedded language catalog");
        const auto object = document.object();
        for (auto it = object.begin(); it != object.end(); ++it) {
            Entry item{it.key(), it.value().toString(), {}, {}, {}, {}};
            en.insert(item.de, item.en); de.insert(item.en, item.de);
            if (item.de != item.en) {
                item.dePattern = pattern(item.de, item.deArgs); item.enPattern = pattern(item.en, item.enArgs);
                entries.push_back(std::move(item));
            }
        }
        // Specific full status templates take precedence over short fragments.
        std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) { return a.de.size() > b.de.size(); });
    }
};
const Catalog& catalog() { static const Catalog result; return result; }
QString substitute(const QString& target, const QHash<int, QString>& args) {
    static const QRegularExpression placeholder("%([1-9][0-9]*)");
    QString result; int at = 0; auto matches = placeholder.globalMatch(target);
    while (matches.hasNext()) {
        const auto match = matches.next(); result += target.mid(at, match.capturedStart() - at);
        result += args.value(match.captured(1).toInt(), match.captured()); at = match.capturedEnd();
    }
    return result + target.mid(at);
}
// A small fallback for Qt's standard buttons. Optional Qt base translations
// supply the remaining file-dialog strings when shipped with the Qt runtime.
class StandardTranslator final : public QTranslator {
public:
    StandardTranslator() {
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
        const auto path = QLibraryInfo::path(QLibraryInfo::TranslationsPath);
#else
        const auto path = QLibraryInfo::location(QLibraryInfo::TranslationsPath);
#endif
        german_.load("qtbase_de", path);
    }
    QString translate(const char* context, const char* source, const char* disambiguation, int n) const override {
        if (english.load()) return {};
        const auto key = QString::fromUtf8(source);
        const auto found = catalog().de.constFind(key);
        return found == catalog().de.cend() ? german_.translate(context, source, disambiguation, n) : *found;
    }
    bool isEmpty() const override { return false; }
private:
    QTranslator german_;
};
}
QString uiText(const char* german) {
    const auto text = QString::fromUtf8(german);
    return english.load() ? catalog().en.value(text, text) : text;
}
QString uiMessage(const QString& text) {
    const auto& dict = catalog(); const bool en = english.load();
    const auto& target = en ? dict.en : dict.de;
    if (const auto found = target.constFind(text); found != target.cend()) return *found;
    // Already in the chosen language: keep user/diagnostic text untouched.
    if ((en ? dict.de : dict.en).contains(text)) return text;
    for (const auto& entry : dict.entries) {
        const auto& args = en ? entry.deArgs : entry.enArgs;
        if (args.empty()) continue;
        const auto match = (en ? entry.dePattern : entry.enPattern).match(text);
        if (!match.hasMatch()) continue;
        QHash<int, QString> values;
        for (std::size_t i = 0; i < args.size(); ++i) values.insert(args[i], match.captured(int(i) + 1));
        return substitute(en ? entry.en : entry.de, values);
    }
    // Portable audio/ZIP layers deliver diagnostics with a static prefix and
    // an untranslated filename/device/server error. Preserve that payload.
    for (const auto& entry : dict.entries) {
        const auto& from = en ? entry.de : entry.en; const auto& to = en ? entry.en : entry.de;
        if (from.isEmpty() || from.contains('%')) continue;
        if ((from.endsWith(' ') || from.endsWith(QChar(0x201e)) || from.endsWith(QChar(0x201c)))) {
            if (text.startsWith(from)) {
                for (const auto& suffix : dict.entries) {
                    const auto& ending = en ? suffix.de : suffix.en;
                    if (!ending.isEmpty() && !ending.contains('%') && (ending.startsWith(' ') || ending.startsWith(QChar(0x201d)) || ending.startsWith(QChar(0x201c))) &&
                        text.size() >= from.size() + ending.size() && text.endsWith(ending))
                        return to + text.mid(from.size(), text.size() - from.size() - ending.size()) + (en ? suffix.en : suffix.de);
                }
                return to + text.mid(from.size());
            }
        }
    }
    for (const auto& entry : dict.entries) {
        const auto& from = en ? entry.de : entry.en; const auto& to = en ? entry.en : entry.de;
        if (!from.isEmpty() && !from.contains('%') && (from.startsWith(' ') || from.startsWith(QChar(0x201d)) || from.startsWith(QChar(0x201c))) && text.endsWith(from))
            return text.left(text.size() - from.size()) + to;
    }
    const int separator = text.indexOf(" · ");
    if (separator >= 0) {
        const auto tail = text.mid(separator + 3); const auto translated = uiMessage(tail);
        if (translated != tail) return text.left(separator + 3) + translated;
    }
    return text;
}
Language::Language() {
    static StandardTranslator translator;
    if (auto* app = QCoreApplication::instance()) app->installTranslator(&translator);
}
Language& Language::instance() { static Language language; return language; }
QString Language::code() { return english.load() ? QStringLiteral("en") : QStringLiteral("de"); }
void Language::setCode(const QString& code) {
    const bool next = code == "en"; if (english.exchange(next) == next) return;
    emit changed();
}
}
