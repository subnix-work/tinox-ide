#include "dialog.h"

#include "../bridge.h"
#include "tinoxkde.h"

#include <KNotification>

#include <QCoreApplication>
#include <QJsonArray>
#include <QUrl>

namespace tkde::notify
{
namespace
{
Reply invalid(const QString &message)
{
    return Reply::failure(TKDE_E_INVALID_ARG, message);
}

int64_t g_nextId = 1;

Reply send(Bridge &b, const QJsonObject &cmd)
{
    const QString event = cmd.value(QStringLiteral("event")).toString();
    if (event.isEmpty()) {
        return invalid(QStringLiteral("\"event\" names an event of tinoxide.notifyrc (buildFinished, buildFailed, ...)"));
    }
    for (const char *name : {"title", "text", "icon", "component", "urgency"}) {
        const QString n = QString::fromLatin1(name);
        if (cmd.contains(n) && !cmd.value(n).isString()) {
            return invalid(QStringLiteral("\"%1\" must be a string").arg(n));
        }
    }
    KNotification::Urgency urgency = KNotification::DefaultUrgency;
    if (cmd.contains(QStringLiteral("urgency"))) {
        const QString u = cmd.value(QStringLiteral("urgency")).toString();
        if (u == QLatin1String("low")) {
            urgency = KNotification::LowUrgency;
        } else if (u == QLatin1String("normal")) {
            urgency = KNotification::NormalUrgency;
        } else if (u == QLatin1String("high")) {
            urgency = KNotification::HighUrgency;
        } else if (u == QLatin1String("critical")) {
            urgency = KNotification::CriticalUrgency;
        } else {
            return invalid(QStringLiteral("\"urgency\" is low, normal, high or critical"));
        }
    }
    QStringList actionIds;
    QStringList actionLabels;
    for (const QJsonValue &a : cmd.value(QStringLiteral("actions")).toArray()) {
        const QString id = a.toObject().value(QStringLiteral("id")).toString();
        const QString text = a.toObject().value(QStringLiteral("text")).toString();
        if (id.isEmpty() || text.isEmpty()) {
            return invalid(QStringLiteral("an action is {id, text}"));
        }
        actionIds << id;
        actionLabels << text;
    }
    const int64_t id = g_nextId++;
    auto *n = new KNotification(event); // CloseOnTimeout: deleted by KNotification itself
    n->setComponentName(cmd.value(QStringLiteral("component")).toString(QStringLiteral("tinoxide")));
    n->setTitle(cmd.value(QStringLiteral("title")).toString());
    n->setText(cmd.value(QStringLiteral("text")).toString());
    n->setIconName(cmd.value(QStringLiteral("icon")).toString());
    n->setUrgency(urgency);
    for (int i = 0; i < actionIds.size(); ++i) {
        KNotificationAction *action = n->addAction(actionLabels.at(i));
        const QString actionId = actionIds.at(i);
        QObject::connect(action, &KNotificationAction::activated, n, [&b, id, actionId] {
            QJsonObject f;
            f.insert(QStringLiteral("id"), double(id));
            f.insert(QStringLiteral("action"), actionId);
            b.emitEvent(QStringLiteral("notify.action"), f);
        });
    }
    QObject::connect(n, &KNotification::closed, n, [&b, id] {
        QJsonObject f;
        f.insert(QStringLiteral("id"), double(id));
        b.emitEvent(QStringLiteral("notify.closed"), f);
    });
    n->sendEvent();
    QJsonObject f;
    f.insert(QStringLiteral("id"), double(id));
    return Reply::success(f);
}
} // namespace

void registerCommands(Bridge &bridge)
{
    bridge.addNamespace(QStringLiteral("notify"));
    bridge.registerCommand(QStringLiteral("notify.send"), [&bridge](const QJsonObject &cmd) { return send(bridge, cmd); });
}
} // namespace tkde::notify
