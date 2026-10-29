#include "action.h"

#include "../bridge.h"
#include "../i18n.h"
#include "tinoxkde.h"

#include <KActionCollection>
#include <KConfigGroup>
#include <KLocalizedString>
#include <KRecentFilesAction>
#include <KSharedConfig>
#include <KShortcutsDialog>
#include <KStandardAction>
#include <KToolBar>
#include <KXMLGUIBuilder>
#include <KXMLGUIClient>
#include <KXMLGUIFactory>

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QDomElement>
#include <QIcon>
#include <QJsonArray>
#include <QMainWindow>
#include <QMenu>
#include <QMenuBar>
#include <QPointer>
#include <QToolBar>
#include <QWidgetAction>

#include <map>
#include <memory>
#include <set>

namespace tkde::action
{
namespace
{
QString qs(const char *s)
{
    return QString::fromLatin1(s);
}

/// The one action collection of the application. It is the collection of a KXMLGUIClient so
/// that a shipped .rc file (action.loadUi) and the shortcut dialog work on the same actions.
class Actions : public QObject, public KXMLGUIClient
{
    Q_OBJECT
public:
    explicit Actions(Bridge &bridge) : QObject(qApp), m_bridge(bridge)
    {
        setComponentName(QCoreApplication::applicationName(), QGuiApplication::applicationDisplayName());
        collection()->setParent(this);
    }

    KActionCollection *collection() { return actionCollection(); }

    QAction *find(const QString &id) const { return const_cast<Actions *>(this)->collection()->action(id); }

    QStringList ids() const
    {
        QStringList l;
        for (QAction *a : const_cast<Actions *>(this)->collection()->actions()) {
            l << a->objectName();
        }
        return l;
    }

    /// Wires the signals every action needs; called once per new action.
    void adopt(const QString &id, QAction *a)
    {
        connect(a, &QAction::triggered, this, [this, id](bool checked) {
            QJsonObject f;
            f.insert(QStringLiteral("id"), id);
            f.insert(QStringLiteral("checked"), checked);
            m_bridge.emitEvent(QStringLiteral("action.triggered"), f);
        });
    }

    KConfigGroup configGroup() const
    {
        KSharedConfigPtr config = m_file.isEmpty() ? KSharedConfig::openConfig() : KSharedConfig::openConfig(m_file, KConfig::SimpleConfig);
        return KConfigGroup(config, m_group);
    }

    /// Recent files live next to the shortcuts, in the group RecentFiles of the same file.
    KConfigGroup recentGroup() const
    {
        KSharedConfigPtr config = m_file.isEmpty() ? KSharedConfig::openConfig() : KSharedConfig::openConfig(m_file, KConfig::SimpleConfig);
        return KConfigGroup(config, QStringLiteral("RecentFiles"));
    }

    void configure(const QString &file, const QString &group)
    {
        m_file = file;
        m_group = group;
        readShortcuts();
    }

    void readShortcuts()
    {
        KConfigGroup g = configGroup();
        collection()->readSettings(&g);
    }

    void saveShortcuts(QAction *only = nullptr)
    {
        KConfigGroup g = configGroup();
        collection()->writeSettings(&g, false, only);
        g.sync();
    }

    // KXMLGUIClient keeps its XML setters protected
    void loadXmlFile(const QString &path) { setXMLFile(path, true); }
    void loadXmlText(const QString &xml) { setXML(xml); }

    std::map<QString, QActionGroup *> groups;
    std::map<QToolBar *, QList<QPointer<QAction>>> barItems; // what action.setToolBar put into each bar
    Bridge &bridge() { return m_bridge; }

private:
    Bridge &m_bridge;
    QString m_file;
    QString m_group = QStringLiteral("Shortcuts");
};

Actions *g_actions = nullptr;

Reply invalid(const QString &message)
{
    return Reply::failure(TKDE_E_INVALID_ARG, message);
}

// ---- standard actions by their KDE name ("file_open", "options_configure", ...)

const std::map<QString, KStandardAction::StandardAction> &standardActions()
{
    static const std::map<QString, KStandardAction::StandardAction> table = [] {
        std::map<QString, KStandardAction::StandardAction> m;
        for (int i = int(KStandardAction::ActionNone) + 1; i <= int(KStandardAction::HamburgerMenu); ++i) {
            const auto id = KStandardAction::StandardAction(i);
            const QString name = KStandardAction::name(id);
            if (!name.isEmpty()) {
                m.emplace(name, id);
            }
        }
        return m;
    }();
    return table;
}

QKeySequence sequenceOf(const QString &text, QString &error)
{
    const QKeySequence s = QKeySequence::fromString(text, QKeySequence::PortableText);
    bool unknownKey = false;
    for (int i = 0; i < s.count(); ++i) {
        unknownKey = unknownKey || s[i].key() == Qt::Key_unknown;
    }
    if ((s.isEmpty() || unknownKey) && !text.isEmpty()) {
        error = QStringLiteral("'%1' is not a key sequence (examples: Ctrl+O, Ctrl+K, Ctrl+P, F5, Ctrl+Shift+F)").arg(text);
    }
    return s;
}

QList<QKeySequence> sequencesOf(const QJsonValue &v, QString &error)
{
    QList<QKeySequence> list;
    const QJsonArray a = v.isArray() ? v.toArray() : QJsonArray{v};
    for (const QJsonValue &s : a) {
        if (!s.isString()) {
            error = QStringLiteral("shortcuts are strings like \"Ctrl+O\"");
            return {};
        }
        const QKeySequence seq = sequenceOf(s.toString(), error);
        if (!error.isEmpty()) {
            return {};
        }
        if (!seq.isEmpty()) {
            list << seq;
        }
    }
    return list;
}

/// Another action that already uses one of the sequences, or null.
QAction *conflictWith(QAction *self, const QList<QKeySequence> &sequences)
{
    for (QAction *other : g_actions->collection()->actions()) {
        if (other == self) {
            continue;
        }
        for (const QKeySequence &mine : sequences) {
            for (const QKeySequence &theirs : other->shortcuts()) {
                if (mine == theirs) {
                    return other;
                }
            }
        }
    }
    return nullptr;
}

QJsonArray shortcutList(QAction *a)
{
    QJsonArray l;
    for (const QKeySequence &s : a->shortcuts()) {
        l.append(s.toString(QKeySequence::PortableText));
    }
    return l;
}

QJsonObject describe(QAction *a)
{
    QJsonObject o;
    o.insert(QStringLiteral("id"), a->objectName());
    o.insert(QStringLiteral("text"), a->text());
    o.insert(QStringLiteral("label"), KLocalizedString::removeAcceleratorMarker(a->text()));
    o.insert(QStringLiteral("icon"), a->icon().name());
    o.insert(QStringLiteral("shortcut"), a->shortcut().toString(QKeySequence::PortableText));
    o.insert(QStringLiteral("shortcuts"), shortcutList(a));
    QStringList defaults;
    for (const QKeySequence &s : a->property("defaultShortcuts").value<QList<QKeySequence>>()) {
        defaults << s.toString(QKeySequence::PortableText);
    }
    o.insert(QStringLiteral("defaultShortcut"), defaults.join(QLatin1String(", ")));
    o.insert(QStringLiteral("enabled"), a->isEnabled());
    o.insert(QStringLiteral("visible"), a->isVisible());
    o.insert(QStringLiteral("checkable"), a->isCheckable());
    o.insert(QStringLiteral("checked"), a->isChecked());
    o.insert(QStringLiteral("toolTip"), a->toolTip());
    o.insert(QStringLiteral("group"), a->actionGroup() ? a->actionGroup()->objectName() : QString());
    o.insert(QStringLiteral("standard"), standardActions().count(a->objectName()) > 0);
    return o;
}

// ---- define / set

struct Descriptor {
    QString id;
    bool standard = false;
    QJsonObject fields;
};

bool parseDescriptors(const QJsonValue &v, std::vector<Descriptor> &out, QString &error)
{
    static const std::set<QString> known{qs("id"),        qs("standard"),  qs("text"),     qs("icon"),    qs("shortcut"),
                                         qs("shortcuts"), qs("checkable"), qs("checked"),  qs("enabled"), qs("visible"),
                                         qs("group"),     qs("toolTip"),   qs("statusTip")};
    if (!v.isArray()) {
        error = QStringLiteral("\"actions\" must be an array of descriptors");
        return false;
    }
    std::set<QString> seen;
    int n = 0;
    for (const QJsonValue &item : v.toArray()) {
        const QJsonObject o = item.toObject();
        Descriptor d;
        d.id = o.value(QStringLiteral("id")).toString();
        if (d.id.isEmpty()) {
            error = QStringLiteral("action %1 has no string \"id\"").arg(n);
            return false;
        }
        if (!seen.insert(d.id).second) {
            error = QStringLiteral("action id '%1' appears twice").arg(d.id);
            return false;
        }
        d.standard = o.value(QStringLiteral("standard")).toBool();
        if (d.standard && standardActions().count(d.id) == 0) {
            error = QStringLiteral(
                        "'%1' is not a KDE standard action name (file_open, file_save, file_quit, options_configure, edit_find, ...)")
                        .arg(d.id);
            return false;
        }
        for (auto it = o.begin(); it != o.end(); ++it) {
            if (known.count(it.key()) == 0) {
                error = QStringLiteral("action '%1': unknown field '%2'").arg(d.id, it.key());
                return false;
            }
        }
        for (const char *name : {"text", "icon", "group", "toolTip", "statusTip", "shortcut"}) {
            if (o.contains(qs(name)) && !o.value(qs(name)).isString()) {
                error = QStringLiteral("action '%1': \"%2\" must be a string").arg(d.id, qs(name));
                return false;
            }
        }
        for (const char *name : {"checkable", "checked", "enabled", "visible", "standard"}) {
            if (o.contains(qs(name)) && !o.value(qs(name)).isBool()) {
                error = QStringLiteral("action '%1': \"%2\" must be a boolean").arg(d.id, qs(name));
                return false;
            }
        }
        QString shortcutError;
        if (o.contains(QStringLiteral("shortcut"))) {
            sequenceOf(o.value(QStringLiteral("shortcut")).toString(), shortcutError);
        }
        if (o.contains(QStringLiteral("shortcuts"))) {
            sequencesOf(o.value(QStringLiteral("shortcuts")), shortcutError);
        }
        if (!shortcutError.isEmpty()) {
            error = QStringLiteral("action '%1': %2").arg(d.id, shortcutError);
            return false;
        }
        d.fields = o;
        out.push_back(std::move(d));
        ++n;
    }
    return true;
}

void applyFields(QAction *a, const QJsonObject &o)
{
    if (o.contains(QStringLiteral("text"))) {
        a->setText(tr(o.value(QStringLiteral("text")).toString()));
    }
    if (o.contains(QStringLiteral("icon"))) {
        const QString icon = o.value(QStringLiteral("icon")).toString();
        a->setIcon(icon.isEmpty() ? QIcon() : QIcon::fromTheme(icon));
    }
    if (o.contains(QStringLiteral("toolTip"))) {
        a->setToolTip(tr(o.value(QStringLiteral("toolTip")).toString()));
    }
    if (o.contains(QStringLiteral("statusTip"))) {
        a->setStatusTip(tr(o.value(QStringLiteral("statusTip")).toString()));
    }
    if (o.contains(QStringLiteral("checkable"))) {
        a->setCheckable(o.value(QStringLiteral("checkable")).toBool());
    }
    if (o.contains(QStringLiteral("checked"))) {
        QSignalBlocker blocker(a); // a state pushed by the logic is not a user action
        a->setChecked(o.value(QStringLiteral("checked")).toBool());
    }
    if (o.contains(QStringLiteral("enabled"))) {
        a->setEnabled(o.value(QStringLiteral("enabled")).toBool());
    }
    if (o.contains(QStringLiteral("visible"))) {
        a->setVisible(o.value(QStringLiteral("visible")).toBool());
    }
    if (o.contains(QStringLiteral("group"))) {
        const QString group = o.value(QStringLiteral("group")).toString();
        if (group.isEmpty()) {
            a->setActionGroup(nullptr);
        } else {
            QActionGroup *&g = g_actions->groups[group];
            if (!g) {
                g = new QActionGroup(g_actions);
                g->setObjectName(group);
                g->setExclusive(true);
            }
            a->setCheckable(true);
            a->setActionGroup(g);
        }
    }
}

Reply define(Bridge &, const QJsonObject &cmd)
{
    std::vector<Descriptor> list;
    QString error;
    if (!parseDescriptors(cmd.value(QStringLiteral("actions")), list, error)) {
        return invalid(error);
    }
    // a default shortcut is a default: it may not take a shortcut another action already has
    for (const Descriptor &d : list) {
        QAction *existing = g_actions->find(d.id);
        if (existing && d.standard != standardActions().count(d.id) > 0 && d.fields.contains(QStringLiteral("standard"))) {
            return invalid(QStringLiteral("action '%1' exists with a different kind: remove it first").arg(d.id));
        }
    }
    for (const Descriptor &d : list) {
        QAction *a = g_actions->find(d.id);
        const bool created = !a;
        if (created) {
            if (d.standard) {
                a = KStandardAction::create(standardActions().at(d.id), nullptr, nullptr, g_actions);
                g_actions->collection()->addAction(d.id, a);
                if (auto *recent = qobject_cast<KRecentFilesAction *>(a)) {
                    recent->loadEntries(g_actions->recentGroup());
                    QObject::connect(recent, &KRecentFilesAction::urlSelected, g_actions, [id = d.id](const QUrl &url) {
                        QJsonObject f;
                        f.insert(QStringLiteral("id"), id);
                        f.insert(QStringLiteral("url"), url.isLocalFile() ? url.toLocalFile() : url.toString());
                        g_actions->bridge().emitEvent(QStringLiteral("action.recentSelected"), f);
                    });
                }
            } else {
                a = new QAction(g_actions);
                g_actions->collection()->addAction(d.id, a);
            }
            g_actions->adopt(d.id, a);
        }
        applyFields(a, d.fields);
        QString ignored;
        QList<QKeySequence> shortcuts;
        if (d.fields.contains(QStringLiteral("shortcuts"))) {
            shortcuts = sequencesOf(d.fields.value(QStringLiteral("shortcuts")), ignored);
        } else if (d.fields.contains(QStringLiteral("shortcut"))) {
            shortcuts = sequencesOf(d.fields.value(QStringLiteral("shortcut")), ignored);
        }
        if (d.fields.contains(QStringLiteral("shortcut")) || d.fields.contains(QStringLiteral("shortcuts"))) {
            KActionCollection::setDefaultShortcuts(a, shortcuts);
            a->setProperty("defaultShortcuts", QVariant::fromValue(shortcuts));
        }
    }
    g_actions->readShortcuts(); // what the user changed earlier wins over the defaults
    QJsonObject f;
    f.insert(QStringLiteral("count"), int(list.size()));
    return Reply::success(f);
}

QAction *actionOf(const QJsonObject &cmd, Reply &fail)
{
    const QString id = cmd.value(QStringLiteral("id")).toString();
    QAction *a = id.isEmpty() ? nullptr : g_actions->find(id);
    if (!a) {
        fail =
            Reply::failure(id.isEmpty() ? TKDE_E_INVALID_ARG : TKDE_E_NOT_FOUND,
                           id.isEmpty() ? QStringLiteral("this command needs an action \"id\"") : QStringLiteral("no action '%1'").arg(id));
    }
    return a;
}

Reply set(Bridge &, const QJsonObject &cmd)
{
    Reply fail;
    QAction *a = actionOf(cmd, fail);
    if (!a) {
        return fail;
    }
    QJsonObject fields = cmd;
    fields.remove(QStringLiteral("cmd"));
    fields.remove(QStringLiteral("req"));
    fields.remove(QStringLiteral("id"));
    for (const char *forbidden : {"standard", "shortcut", "shortcuts"}) {
        if (fields.contains(qs(forbidden))) {
            return invalid(QStringLiteral("action.set does not change '%1' (shortcuts: action.setShortcut)").arg(qs(forbidden)));
        }
    }
    std::vector<Descriptor> list;
    QString error;
    fields.insert(QStringLiteral("id"), a->objectName());
    if (!parseDescriptors(QJsonArray{fields}, list, error)) {
        return invalid(error);
    }
    fields.remove(QStringLiteral("id"));
    applyFields(a, fields);
    return Reply::success();
}

Reply remove(Bridge &, const QJsonObject &cmd)
{
    Reply fail;
    QAction *a = actionOf(cmd, fail);
    if (!a) {
        return fail;
    }
    g_actions->collection()->takeAction(a);
    delete a;
    return Reply::success();
}

Reply list(Bridge &, const QJsonObject &)
{
    QJsonArray all;
    for (QAction *a : g_actions->collection()->actions()) {
        all.append(describe(a));
    }
    QJsonObject f;
    f.insert(QStringLiteral("actions"), all);
    return Reply::success(f);
}

Reply trigger(Bridge &, const QJsonObject &cmd)
{
    Reply fail;
    QAction *a = actionOf(cmd, fail);
    if (!a) {
        return fail;
    }
    if (!a->isEnabled()) {
        return Reply::failure(TKDE_E_FAILED, QStringLiteral("action '%1' is disabled").arg(a->objectName()));
    }
    a->trigger();
    return Reply::success();
}

Reply setShortcut(Bridge &, const QJsonObject &cmd)
{
    Reply fail;
    QAction *a = actionOf(cmd, fail);
    if (!a) {
        return fail;
    }
    QString error;
    QList<QKeySequence> sequences;
    if (cmd.value(QStringLiteral("shortcuts")).isNull() || !cmd.contains(QStringLiteral("shortcuts"))) {
        sequences = a->property("defaultShortcuts").value<QList<QKeySequence>>(); // back to the default
    } else {
        sequences = sequencesOf(cmd.value(QStringLiteral("shortcuts")), error);
        if (!error.isEmpty()) {
            return invalid(error);
        }
    }
    if (QAction *other = conflictWith(a, sequences)) {
        return Reply::failure(TKDE_E_INVALID_ARG, QStringLiteral("that shortcut is already used by '%1'").arg(other->objectName()));
    }
    a->setShortcuts(sequences);
    g_actions->saveShortcuts(a);
    QJsonObject f;
    f.insert(QStringLiteral("id"), a->objectName());
    f.insert(QStringLiteral("shortcuts"), shortcutList(a));
    g_actions->bridge().emitEvent(QStringLiteral("action.shortcutChanged"), f);
    return Reply::success(f);
}

KRecentFilesAction *recentOf(const QJsonObject &cmd, Reply &fail)
{
    QAction *a = actionOf(cmd, fail);
    if (!a) {
        return nullptr;
    }
    auto *r = qobject_cast<KRecentFilesAction *>(a);
    if (!r) {
        fail = Reply::failure(
            TKDE_E_INVALID_ARG,
            QStringLiteral("'%1' is not the recent-files action (define \"file_open_recent\" with standard:true)").arg(a->objectName()));
    }
    return r;
}

Reply recentAdd(Bridge &, const QJsonObject &cmd)
{
    Reply fail;
    KRecentFilesAction *r = recentOf(cmd, fail);
    if (!r) {
        return fail;
    }
    const QString text = cmd.value(QStringLiteral("url")).toString();
    if (text.isEmpty()) {
        return invalid(QStringLiteral("\"url\" is a path or a URL"));
    }
    const QUrl url = QUrl::fromUserInput(text, QString(), QUrl::AssumeLocalFile);
    r->addUrl(url, cmd.value(QStringLiteral("name")).toString());
    KConfigGroup g = g_actions->recentGroup();
    r->saveEntries(g);
    g.sync();
    return Reply::success();
}

Reply recentList(Bridge &, const QJsonObject &cmd)
{
    Reply fail;
    KRecentFilesAction *r = recentOf(cmd, fail);
    if (!r) {
        return fail;
    }
    QJsonArray urls;
    for (const QUrl &u : r->urls()) {
        urls.append(u.isLocalFile() ? u.toLocalFile() : u.toString());
    }
    QJsonObject f;
    f.insert(QStringLiteral("urls"), urls);
    return Reply::success(f);
}

Reply recentClear(Bridge &, const QJsonObject &cmd)
{
    Reply fail;
    KRecentFilesAction *r = recentOf(cmd, fail);
    if (!r) {
        return fail;
    }
    r->clear();
    KConfigGroup g = g_actions->recentGroup();
    r->saveEntries(g);
    g.sync();
    return Reply::success();
}

Reply configure(Bridge &, const QJsonObject &cmd)
{
    g_actions->configure(cmd.value(QStringLiteral("file")).toString(),
                         cmd.value(QStringLiteral("group")).toString(QStringLiteral("Shortcuts")));
    return Reply::success();
}

// ---- menus and toolbars from a description

bool checkItems(const QJsonValue &items, QString &error, int depth = 0)
{
    if (!items.isArray()) {
        error = QStringLiteral("\"items\" must be an array");
        return false;
    }
    if (depth > 8) {
        error = QStringLiteral("menus nested deeper than 8 levels");
        return false;
    }
    for (const QJsonValue &i : items.toArray()) {
        if (i.isString()) {
            if (i.toString() != QLatin1String("-") && !g_actions->find(i.toString())) {
                error = QStringLiteral("no action '%1' (define it first)").arg(i.toString());
                return false;
            }
        } else if (i.isObject() && i.toObject().contains(QStringLiteral("menu"))) {
            const QJsonObject m = i.toObject().value(QStringLiteral("menu")).toObject();
            if (m.value(QStringLiteral("title")).toString().isEmpty()) {
                error = QStringLiteral("a submenu needs a \"title\"");
                return false;
            }
            if (!checkItems(m.value(QStringLiteral("items")), error, depth + 1)) {
                return false;
            }
        } else if (i.isObject() && i.toObject().contains(QStringLiteral("section"))) {
            continue;
        } else {
            error = QStringLiteral("an item is an action id, \"-\", {\"section\":title} or {\"menu\":{title,items}}");
            return false;
        }
    }
    return true;
}

void fillMenu(QMenu *menu, const QJsonArray &items)
{
    for (const QJsonValue &i : items) {
        if (i.isString()) {
            if (i.toString() == QLatin1String("-")) {
                menu->addSeparator();
            } else {
                menu->addAction(g_actions->find(i.toString()));
            }
        } else if (i.toObject().contains(QStringLiteral("section"))) {
            menu->addSection(tr(i.toObject().value(QStringLiteral("section")).toString()));
        } else {
            const QJsonObject m = i.toObject().value(QStringLiteral("menu")).toObject();
            QMenu *sub = menu->addMenu(tr(m.value(QStringLiteral("title")).toString()));
            sub->setObjectName(m.value(QStringLiteral("id")).toString());
            const QString icon = m.value(QStringLiteral("icon")).toString();
            if (!icon.isEmpty()) {
                sub->setIcon(QIcon::fromTheme(icon));
            }
            fillMenu(sub, m.value(QStringLiteral("items")).toArray());
        }
    }
}

QWidget *widgetOf(Bridge &b, const QJsonObject &cmd, Reply &fail)
{
    const QJsonValue t = cmd.value(QStringLiteral("target"));
    QObject *o = t.isDouble() ? b.object(int64_t(t.toDouble())) : nullptr;
    // a mounted widget tree stands for its root widget
    if (o && o->property("tkdeRoot").isValid()) {
        o = o->property("tkdeRoot").value<QWidget *>();
    }
    auto *w = qobject_cast<QWidget *>(o);
    if (!w) {
        fail = Reply::failure(t.isDouble() ? TKDE_E_NOT_FOUND : TKDE_E_INVALID_ARG,
                              QStringLiteral("this command needs the \"target\" id of a widget"));
    }
    return w;
}

Reply setMenuBar(Bridge &b, const QJsonObject &cmd)
{
    Reply fail;
    QWidget *w = widgetOf(b, cmd, fail);
    if (!w) {
        return fail;
    }
    auto *window = qobject_cast<QMainWindow *>(w);
    if (!window) {
        return invalid(QStringLiteral("action.setMenuBar needs a QMainWindow"));
    }
    const QJsonValue menus = cmd.value(QStringLiteral("menus"));
    if (!menus.isArray()) {
        return invalid(QStringLiteral("\"menus\" must be an array of {title, items}"));
    }
    QString error;
    for (const QJsonValue &m : menus.toArray()) {
        if (m.toObject().value(QStringLiteral("title")).toString().isEmpty()) {
            return invalid(QStringLiteral("every menu needs a \"title\" such as \"&File\""));
        }
        if (!checkItems(m.toObject().value(QStringLiteral("items")), error)) {
            return invalid(error);
        }
    }
    QMenuBar *bar = window->menuBar();
    for (QMenu *old : bar->findChildren<QMenu *>(QString(), Qt::FindDirectChildrenOnly)) {
        delete old;
    }
    bar->clear();
    for (const QJsonValue &mv : menus.toArray()) {
        const QJsonObject m = mv.toObject();
        QMenu *menu = bar->addMenu(tr(m.value(QStringLiteral("title")).toString()));
        menu->setObjectName(m.value(QStringLiteral("id")).toString());
        fillMenu(menu, m.value(QStringLiteral("items")).toArray());
    }
    return Reply::success();
}

Reply setToolBar(Bridge &b, const QJsonObject &cmd)
{
    Reply fail;
    QWidget *w = widgetOf(b, cmd, fail);
    if (!w) {
        return fail;
    }
    auto *bar = qobject_cast<QToolBar *>(w);
    if (!bar) {
        return invalid(QStringLiteral("action.setToolBar needs a QToolBar node"));
    }
    QString error;
    if (!checkItems(cmd.value(QStringLiteral("items")), error)) {
        return invalid(error);
    }
    for (const QJsonValue &i : cmd.value(QStringLiteral("items")).toArray()) {
        if (!i.isString()) {
            return invalid(QStringLiteral("toolbars take action ids and \"-\" only"));
        }
    }
    // only what this command added goes: widgets the widget tree put into the bar stay
    // (KDE's own Open action is a QWidgetAction too, so the type cannot tell them apart)
    QList<QPointer<QAction>> &mine = g_actions->barItems[bar];
    for (const QPointer<QAction> &a : mine) {
        if (a) {
            bar->removeAction(a);
            if (a->isSeparator()) {
                delete a;
            }
        }
    }
    mine.clear();
    for (const QJsonValue &i : cmd.value(QStringLiteral("items")).toArray()) {
        QAction *a = i.toString() == QLatin1String("-") ? bar->addSeparator() : g_actions->find(i.toString());
        if (i.toString() != QLatin1String("-")) {
            bar->addAction(a);
        }
        mine.append(a);
    }
    return Reply::success();
}

Reply setContextMenu(Bridge &b, const QJsonObject &cmd)
{
    Reply fail;
    QWidget *w = widgetOf(b, cmd, fail);
    if (!w) {
        return fail;
    }
    QString error;
    if (!checkItems(cmd.value(QStringLiteral("items")), error)) {
        return invalid(error);
    }
    for (QAction *a : w->actions()) {
        w->removeAction(a);
    }
    for (const QJsonValue &i : cmd.value(QStringLiteral("items")).toArray()) {
        if (!i.isString()) {
            return invalid(QStringLiteral("context menus take action ids and \"-\" here"));
        }
        if (i.toString() == QLatin1String("-")) {
            QAction *sep = new QAction(w);
            sep->setSeparator(true);
            w->addAction(sep);
        } else {
            w->addAction(g_actions->find(i.toString()));
        }
    }
    w->setContextMenuPolicy(Qt::ActionsContextMenu);
    return Reply::success();
}

/// KXMLGUIBuilder only knows how to fill a KMainWindow; the widget tree builds plain
/// QMainWindows, so the two top-level containers are created here.
class WindowBuilder : public KXMLGUIBuilder
{
public:
    explicit WindowBuilder(QMainWindow *window) : KXMLGUIBuilder(window), m_window(window) {}

    QWidget *createContainer(QWidget *parent, int index, const QDomElement &element, QAction *&containerAction) override
    {
        const QString tag = element.tagName().toLower();
        if (!parent && tag == QLatin1String("menubar")) {
            containerAction = nullptr;
            return m_window->menuBar();
        }
        if (!parent && tag == QLatin1String("toolbar")) {
            containerAction = nullptr;
            auto *bar = new KToolBar(element.attribute(QStringLiteral("name")), m_window);
            bar->setObjectName(element.attribute(QStringLiteral("name")));
            m_window->addToolBar(bar);
            return bar;
        }
        return KXMLGUIBuilder::createContainer(parent, index, element, containerAction);
    }

    void removeContainer(QWidget *container, QWidget *parent, QDomElement &element, QAction *containerAction) override
    {
        if (!parent && container == m_window->menuBar()) {
            m_window->menuBar()->clear();
            return;
        }
        KXMLGUIBuilder::removeContainer(container, parent, element, containerAction);
    }

private:
    QMainWindow *m_window;
};

/// Shows a context menu of actions at a global position. Not modal: the menu is gone when an entry
/// is chosen or the user clicks elsewhere, and the chosen action raises `action.triggered`.
Reply popup(Bridge &, const QJsonObject &cmd)
{
    QString error;
    if (!checkItems(cmd.value(QStringLiteral("items")), error)) {
        return invalid(error);
    }
    if (!cmd.value(QStringLiteral("x")).isDouble() || !cmd.value(QStringLiteral("y")).isDouble()) {
        return invalid(QStringLiteral("action.popup needs the global position \"x\" and \"y\""));
    }
    auto *menu = new QMenu;
    menu->setAttribute(Qt::WA_DeleteOnClose);
    fillMenu(menu, cmd.value(QStringLiteral("items")).toArray());
    menu->popup(QPoint(cmd.value(QStringLiteral("x")).toInt(), cmd.value(QStringLiteral("y")).toInt()));
    return Reply::success();
}

/// A shipped .rc file (or XML text) builds menus and toolbars of a window from the same actions.
Reply loadUi(Bridge &b, const QJsonObject &cmd)
{
    Reply fail;
    QWidget *w = widgetOf(b, cmd, fail);
    if (!w) {
        return fail;
    }
    auto *window = qobject_cast<QMainWindow *>(w);
    if (!window) {
        return invalid(QStringLiteral("action.loadUi needs a QMainWindow"));
    }
    const QString file = cmd.value(QStringLiteral("file")).toString();
    const QString xml = cmd.value(QStringLiteral("xml")).toString();
    if (file.isEmpty() == xml.isEmpty()) {
        return invalid(QStringLiteral("action.loadUi needs either \"file\" (a path to an .rc) or \"xml\""));
    }
    if (window->property("tkdeFactory").isValid()) {
        return invalid(QStringLiteral("this window already has a loaded UI description"));
    }
    if (!file.isEmpty()) {
        g_actions->loadXmlFile(file);
    } else {
        g_actions->loadXmlText(xml);
    }
    auto *builder = new WindowBuilder(window);
    auto *factory = new KXMLGUIFactory(builder, window);
    factory->addClient(g_actions);
    window->setProperty("tkdeFactory", true);
    return Reply::success();
}

Reply showShortcutsDialog(Bridge &b, const QJsonObject &cmd)
{
    QWidget *parent = nullptr;
    if (cmd.contains(QStringLiteral("parent"))) {
        Reply fail;
        QJsonObject probe;
        probe.insert(QStringLiteral("target"), cmd.value(QStringLiteral("parent")));
        parent = widgetOf(b, probe, fail);
        if (!parent) {
            return fail;
        }
    }
    auto *dialog = new KShortcutsDialog(KShortcutsEditor::AllActions, KShortcutsEditor::LetterShortcutsAllowed, parent);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->addCollection(g_actions->collection());
    QObject::connect(dialog, &KShortcutsDialog::saved, g_actions, [] {
        g_actions->saveShortcuts();
        g_actions->bridge().emitEvent(QStringLiteral("action.shortcutsSaved"));
    });
    dialog->setModal(false);
    dialog->open(); // not exec(): the event loop must keep serving commands
    return Reply::success();
}

// ---- test hooks

QJsonArray menuStructure(const QList<QAction *> &actions)
{
    QJsonArray out;
    for (QAction *a : actions) {
        if (a->isSeparator()) {
            out.append(a->text().isEmpty() ? QStringLiteral("-") : QStringLiteral("--%1--").arg(a->text()));
        } else if (QMenu *m = a->menu()) {
            QJsonObject o;
            o.insert(QStringLiteral("menu"), m->title());
            o.insert(QStringLiteral("items"), menuStructure(m->actions()));
            out.append(o);
        } else {
            out.append(a->objectName());
        }
    }
    return out;
}

Reply debugMenus(Bridge &b, const QJsonObject &cmd)
{
    Reply fail;
    QWidget *w = widgetOf(b, cmd, fail);
    if (!w) {
        return fail;
    }
    QJsonObject f;
    if (auto *window = qobject_cast<QMainWindow *>(w)) {
        f.insert(QStringLiteral("menus"), menuStructure(window->menuBar()->actions()));
        QJsonArray bars;
        for (QToolBar *t : window->findChildren<QToolBar *>(QString(), Qt::FindDirectChildrenOnly)) {
            QJsonObject o;
            o.insert(QStringLiteral("name"), t->objectName());
            o.insert(QStringLiteral("items"), menuStructure(t->actions()));
            bars.append(o);
        }
        f.insert(QStringLiteral("toolBars"), bars);
    } else {
        f.insert(QStringLiteral("items"), menuStructure(w->actions()));
        f.insert(QStringLiteral("policy"), int(w->contextMenuPolicy()));
    }
    return Reply::success(f);
}
} // namespace

void registerCommands(Bridge &bridge)
{
    bridge.addNamespace(QStringLiteral("action"));
    g_actions = new Actions(bridge);
    auto reg = [&bridge](const char *name, Reply (*fn)(Bridge &, const QJsonObject &)) {
        bridge.registerCommand(qs(name), [&bridge, fn](const QJsonObject &cmd) { return fn(bridge, cmd); });
    };
    reg("action.configure", configure);
    reg("action.define", define);
    reg("action.set", set);
    reg("action.remove", remove);
    reg("action.list", list);
    reg("action.trigger", trigger);
    reg("action.setShortcut", setShortcut);
    reg("action.recentAdd", recentAdd);
    reg("action.recentList", recentList);
    reg("action.recentClear", recentClear);
    reg("action.setMenuBar", setMenuBar);
    reg("action.setToolBar", setToolBar);
    reg("action.setContextMenu", setContextMenu);
    reg("action.popup", popup);
    reg("action.loadUi", loadUi);
    reg("action.showShortcutsDialog", showShortcutsDialog);
    if (bridge.debugCommandsEnabled()) {
        reg("action.debug.menus", debugMenus);
    }
}
} // namespace tkde::action

#include "action.moc"
