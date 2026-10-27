// KConfig helpers shared by the config commands and the settings dialog.
#ifndef TINOXKDE_CONFIGUTIL_H
#define TINOXKDE_CONFIGUTIL_H

#include <KConfigGroup>
#include <KSharedConfig>

#include <QJsonValue>
#include <QString>
#include <QVariant>

namespace tkde::config
{
/// "" is the application's own file (tinoxiderc for the application "tinoxide"); an absolute path
/// is used as it is; any other name lives in the user's config directory.
KSharedConfigPtr openConfig(const QString &file);
/// "Editor/Fonts" is the group Fonts inside the group Editor.
KConfigGroup groupOf(const KSharedConfigPtr &config, const QString &path);
/// `type` is string, bool, int, double, stringList or color; "" takes it from the JSON value.
QVariant fromJson(const QJsonValue &v, const QString &type, QString &error);
QJsonValue toJson(const KConfigGroup &g, const QString &key, const QString &type, const QJsonValue &def);
} // namespace tkde::config

#endif // TINOXKDE_CONFIGUTIL_H
