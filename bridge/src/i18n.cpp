#include "i18n.h"

#include "bridge.h"
#include "tinoxkde.h"

#include <QJsonArray>
#include <QJsonObject>

namespace tkde::translation
{
namespace
{
/// {text, context?, plural?, n?, args?} gives {text}: `%1`.. are replaced by the arguments; with a plural form
/// `%1` is the count `n` and the arguments start at `%2`.
Reply translate(const QJsonObject &cmd)
{
    const QJsonValue text = cmd.value(QStringLiteral("text"));
    if (!text.isString() || text.toString().isEmpty()) {
        return Reply::failure(TKDE_E_INVALID_ARG, QStringLiteral("i18n.translate needs a non-empty \"text\""));
    }
    const QByteArray singular = text.toString().toUtf8();
    const QByteArray context = cmd.value(QStringLiteral("context")).toString().toUtf8();
    const QByteArray plural = cmd.value(QStringLiteral("plural")).toString().toUtf8();
    const QJsonValue count = cmd.value(QStringLiteral("n"));
    if (!plural.isEmpty() && !count.isDouble()) {
        return Reply::failure(TKDE_E_INVALID_ARG, QStringLiteral("a plural form needs the count \"n\""));
    }
    KLocalizedString s;
    if (!plural.isEmpty()) {
        s = context.isEmpty() ? ki18np(singular.constData(), plural.constData())
                              : ki18ncp(context.constData(), singular.constData(), plural.constData());
        s = s.subs(qlonglong(count.toDouble()));
    } else {
        s = context.isEmpty() ? ki18n(singular.constData()) : ki18nc(context.constData(), singular.constData());
    }
    for (const QJsonValue &arg : cmd.value(QStringLiteral("args")).toArray()) {
        s = s.subs(arg.toString());
    }
    QJsonObject f;
    f.insert(QStringLiteral("text"), s.toString());
    return Reply::success(f);
}
} // namespace

void registerCommands(Bridge &bridge)
{
    bridge.addNamespace(QStringLiteral("i18n"));
    bridge.registerCommand(QStringLiteral("i18n.translate"), [](const QJsonObject &cmd) { return translate(cmd); });
}
} // namespace tkde::translation
