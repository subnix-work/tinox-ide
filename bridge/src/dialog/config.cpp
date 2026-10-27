#include "configutil.h"
#include "dialog.h"

#include "../bridge.h"
#include "tinoxkde.h"

#include <KConfigGroup>
#include <KConfigWatcher>
#include <KSharedConfig>

#include <QColor>
#include <QDir>
#include <QJsonArray>

#include <map>
#include <memory>

namespace tkde::config
{
KSharedConfigPtr openConfig(const QString &file)
{
    if (file.isEmpty()) {
        return KSharedConfig::openConfig();
    }
    return QDir::isAbsolutePath(file) ? KSharedConfig::openConfig(file, KConfig::SimpleConfig) : KSharedConfig::openConfig(file);
}

KConfigGroup groupOf(const KSharedConfigPtr &config, const QString &path)
{
    // "Editor/Fonts" is the group Fonts inside the group Editor
    const QStringList parts = path.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    KConfigGroup g(config, parts.value(0));
    for (int i = 1; i < parts.size(); ++i) {
        g = g.group(parts.at(i));
    }
    return g;
}

QVariant fromJson(const QJsonValue &v, const QString &type, QString &error)
{
    if (type == QLatin1String("string") || (type.isEmpty() && v.isString())) {
        return v.toString();
    }
    if (type == QLatin1String("bool") || (type.isEmpty() && v.isBool())) {
        return v.toBool();
    }
    if (type == QLatin1String("int") || (type.isEmpty() && v.isDouble() && v.toDouble() == double(qint64(v.toDouble())))) {
        return qint64(v.toDouble());
    }
    if (type == QLatin1String("double") || (type.isEmpty() && v.isDouble())) {
        return v.toDouble();
    }
    if (type == QLatin1String("stringList") || (type.isEmpty() && v.isArray())) {
        QStringList l;
        for (const QJsonValue &i : v.toArray()) {
            if (!i.isString()) {
                error = QStringLiteral("a list holds strings only");
                return QVariant();
            }
            l << i.toString();
        }
        return l;
    }
    if (type == QLatin1String("color")) {
        if (!QColor::isValidColorName(v.toString())) {
            error = QStringLiteral("'%1' is not a colour").arg(v.toString());
            return QVariant();
        }
        return QColor::fromString(v.toString());
    }
    error = QStringLiteral("cannot store this value as '%1'").arg(type.isEmpty() ? QStringLiteral("this JSON type") : type);
    return QVariant();
}

QJsonValue toJson(const KConfigGroup &g, const QString &key, const QString &type, const QJsonValue &def)
{
    QString t = type;
    if (t.isEmpty()) {
        t = def.isBool()     ? QStringLiteral("bool")
            : def.isDouble() ? (def.toDouble() == double(qint64(def.toDouble())) ? QStringLiteral("int") : QStringLiteral("double"))
            : def.isArray()  ? QStringLiteral("stringList")
                             : QStringLiteral("string");
    }
    if (t == QLatin1String("bool")) {
        return g.readEntry(key, def.toBool());
    }
    if (t == QLatin1String("int")) {
        return double(g.readEntry(key, qint64(def.toDouble())));
    }
    if (t == QLatin1String("double")) {
        return g.readEntry(key, def.toDouble());
    }
    if (t == QLatin1String("stringList")) {
        QStringList defaults;
        for (const QJsonValue &i : def.toArray()) {
            defaults << i.toString();
        }
        QJsonArray out;
        for (const QString &s : g.readEntry(key, defaults)) {
            out.append(s);
        }
        return out;
    }
    if (t == QLatin1String("color")) {
        return g.readEntry(key, QColor::fromString(def.toString())).name();
    }
    return g.readEntry(key, def.toString());
}

namespace
{
Reply invalid(const QString &message)
{
    return Reply::failure(TKDE_E_INVALID_ARG, message);
}

struct Watch {
    KSharedConfigPtr config;
    KConfigWatcher::Ptr watcher;
};

std::map<QString, Watch> g_watches;

QString groupName(const QJsonObject &cmd, QString &error)
{
    const QString g = cmd.value(QStringLiteral("group")).toString();
    if (g.isEmpty() || g.startsWith(QLatin1Char('/')) || g.endsWith(QLatin1Char('/'))) {
        error = QStringLiteral("\"group\" is a name such as \"Editor\" or \"Editor/Fonts\"");
    }
    return g;
}

Reply read(Bridge &, const QJsonObject &cmd)
{
    QString error;
    const QString group = groupName(cmd, error);
    const QString key = cmd.value(QStringLiteral("key")).toString();
    if (!error.isEmpty() || key.isEmpty()) {
        return invalid(error.isEmpty() ? QStringLiteral("\"key\" is required") : error);
    }
    const KConfigGroup g = groupOf(openConfig(cmd.value(QStringLiteral("file")).toString()), group);
    const QJsonValue def = cmd.value(QStringLiteral("default"));
    const QString type = cmd.value(QStringLiteral("type")).toString();
    if (type.isEmpty() && def.isUndefined()) {
        return invalid(QStringLiteral("name the \"type\" (string, bool, int, double, stringList, color) or give a \"default\""));
    }
    QJsonObject f;
    const bool exists = g.hasKey(key);
    f.insert(QStringLiteral("exists"), exists);
    f.insert(QStringLiteral("value"), exists || !def.isUndefined() ? toJson(g, key, type, def) : QJsonValue(QJsonValue::Null));
    return Reply::success(f);
}

Reply readGroup(Bridge &, const QJsonObject &cmd)
{
    QString error;
    const QString group = groupName(cmd, error);
    if (!error.isEmpty()) {
        return invalid(error);
    }
    const KConfigGroup g = groupOf(openConfig(cmd.value(QStringLiteral("file")).toString()), group);
    QJsonObject values;
    const QMap<QString, QString> entries = g.entryMap(); // as stored text: a settings page shows it, a typed read is config.read
    for (auto it = entries.begin(); it != entries.end(); ++it) {
        values.insert(it.key(), it.value());
    }
    QJsonObject f;
    f.insert(QStringLiteral("values"), values);
    f.insert(QStringLiteral("exists"), g.exists());
    return Reply::success(f);
}

struct Entry {
    QString group;
    QString key;
    QVariant value;
};

bool parseEntries(const QJsonArray &list, std::vector<Entry> &out, QString &error)
{
    int n = 0;
    for (const QJsonValue &v : list) {
        const QJsonObject o = v.toObject();
        Entry e;
        e.group = groupName(o, error);
        e.key = o.value(QStringLiteral("key")).toString();
        if (!error.isEmpty() || e.key.isEmpty()) {
            if (error.isEmpty()) {
                error = QStringLiteral("entry %1 has no \"key\"").arg(n);
            }
            return false;
        }
        e.value = fromJson(o.value(QStringLiteral("value")), o.value(QStringLiteral("type")).toString(), error);
        if (!error.isEmpty()) {
            error = QStringLiteral("%1/%2: %3").arg(e.group, e.key, error);
            return false;
        }
        out.push_back(std::move(e));
        ++n;
    }
    return true;
}

Reply writeEntries(const QString &file, const QJsonArray &list)
{
    std::vector<Entry> entries;
    QString error;
    if (!parseEntries(list, entries, error)) {
        return invalid(error);
    }
    KSharedConfigPtr config = openConfig(file);
    for (const Entry &e : entries) {
        KConfigGroup g = groupOf(config, e.group);
        g.writeEntry(e.key, e.value);
    }
    if (!config->sync()) {
        return Reply::failure(TKDE_E_FAILED, QStringLiteral("could not write %1").arg(config->name()));
    }
    return Reply::success();
}

Reply write(Bridge &, const QJsonObject &cmd)
{
    return writeEntries(cmd.value(QStringLiteral("file")).toString(), QJsonArray{cmd});
}

Reply writeMany(Bridge &, const QJsonObject &cmd)
{
    if (!cmd.value(QStringLiteral("entries")).isArray()) {
        return invalid(QStringLiteral("\"entries\" must be an array of {group, key, value, type?}"));
    }
    return writeEntries(cmd.value(QStringLiteral("file")).toString(), cmd.value(QStringLiteral("entries")).toArray());
}

Reply remove(Bridge &, const QJsonObject &cmd)
{
    QString error;
    const QString group = groupName(cmd, error);
    if (!error.isEmpty()) {
        return invalid(error);
    }
    KSharedConfigPtr config = openConfig(cmd.value(QStringLiteral("file")).toString());
    KConfigGroup g = groupOf(config, group);
    const QString key = cmd.value(QStringLiteral("key")).toString();
    if (key.isEmpty()) {
        g.deleteGroup();
    } else {
        g.deleteEntry(key);
    }
    config->sync();
    return Reply::success();
}

Reply groups(Bridge &, const QJsonObject &cmd)
{
    KSharedConfigPtr config = openConfig(cmd.value(QStringLiteral("file")).toString());
    QStringList names = cmd.contains(QStringLiteral("group")) ? groupOf(config, cmd.value(QStringLiteral("group")).toString()).groupList()
                                                              : config->groupList();
    QJsonArray out;
    for (const QString &n : names) {
        out.append(n);
    }
    QJsonObject f;
    f.insert(QStringLiteral("groups"), out);
    f.insert(QStringLiteral("file"), config->name());
    return Reply::success(f);
}

Reply watch(Bridge &b, const QJsonObject &cmd)
{
    const QString file = cmd.value(QStringLiteral("file")).toString();
    KSharedConfigPtr config = openConfig(file);
    const QString name = config->name();
    if (g_watches.count(name)) {
        return Reply::success();
    }
    Watch w;
    w.config = config;
    w.watcher = KConfigWatcher::create(config);
    QObject::connect(w.watcher.data(), &KConfigWatcher::configChanged, w.watcher.data(),
                     [&b, name](const KConfigGroup &group, const QByteArrayList &keys) {
                         QJsonObject f;
                         f.insert(QStringLiteral("file"), name);
                         f.insert(QStringLiteral("group"), group.name());
                         QJsonArray ks;
                         for (const QByteArray &k : keys) {
                             ks.append(QString::fromUtf8(k));
                         }
                         f.insert(QStringLiteral("keys"), ks);
                         b.emitEvent(QStringLiteral("config.changed"), f);
                     });
    g_watches.emplace(name, std::move(w));
    return Reply::success();
}

Reply unwatch(Bridge &, const QJsonObject &cmd)
{
    g_watches.erase(openConfig(cmd.value(QStringLiteral("file")).toString())->name());
    return Reply::success();
}

Reply info(Bridge &, const QJsonObject &cmd)
{
    KSharedConfigPtr config = openConfig(cmd.value(QStringLiteral("file")).toString());
    QJsonObject f;
    f.insert(QStringLiteral("name"), config->name());
    f.insert(QStringLiteral("path"),
             config->locationType() == QStandardPaths::GenericConfigLocation
                 ? QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QLatin1Char('/') + config->name()
                 : config->name());
    return Reply::success(f);
}
} // namespace

void registerCommands(Bridge &bridge)
{
    bridge.addNamespace(QStringLiteral("config"));
    auto reg = [&bridge](const char *name, Reply (*fn)(Bridge &, const QJsonObject &)) {
        bridge.registerCommand(QString::fromLatin1(name), [&bridge, fn](const QJsonObject &cmd) { return fn(bridge, cmd); });
    };
    reg("config.read", read);
    reg("config.readGroup", readGroup);
    reg("config.write", write);
    reg("config.writeMany", writeMany);
    reg("config.delete", remove);
    reg("config.groups", groups);
    reg("config.watch", watch);
    reg("config.unwatch", unwatch);
    reg("config.info", info);
}
} // namespace tkde::config
