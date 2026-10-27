#include "dialog.h"

#include "../bridge.h"
#include "configutil.h"
#include "tinoxkde.h"

#include <KMessageBox>
#include <KPageDialog>
#include <KPageWidgetItem>

#include <QAbstractButton>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QIcon>
#include <QInputDialog>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QProgressDialog>
#include <QPushButton>
#include <QSpinBox>
#include <QUrl>

#include <functional>
#include <map>
#include <set>

namespace tkde::dialog
{
namespace
{
QString qs(const char *s)
{
    return QString::fromLatin1(s);
}

Reply invalid(const QString &message)
{
    return Reply::failure(TKDE_E_INVALID_ARG, message);
}

/// Every dialog is shown with open(): the call returns at once and the answer comes as an event,
/// so Tinox (and the rest of the bridge) keeps running while a dialog is up.
struct Opened {
    QWidget *parent = nullptr;
    QJsonValue req;
};

QWidget *parentOf(Bridge &b, const QJsonObject &cmd, Reply &fail)
{
    if (!cmd.contains(QStringLiteral("parent"))) {
        return nullptr;
    }
    const QJsonValue p = cmd.value(QStringLiteral("parent"));
    QObject *o = p.isDouble() ? b.object(int64_t(p.toDouble())) : nullptr;
    if (o && o->property("tkdeRoot").isValid()) {
        o = o->property("tkdeRoot").value<QWidget *>();
    }
    auto *w = qobject_cast<QWidget *>(o);
    if (!w) {
        fail =
            Reply::failure(p.isDouble() ? TKDE_E_NOT_FOUND : TKDE_E_INVALID_ARG, QStringLiteral("\"parent\" must be the id of a widget"));
    }
    return w;
}

QJsonObject resultFields(const QJsonValue &req, int64_t id)
{
    QJsonObject f;
    f.insert(QStringLiteral("dialog"), double(id));
    if (!req.isUndefined()) {
        f.insert(QStringLiteral("req"), req);
    }
    return f;
}

Reply opened(int64_t id)
{
    QJsonObject f;
    f.insert(QStringLiteral("id"), double(id));
    return Reply::success(f);
}

QString localOrUrl(const QUrl &u)
{
    return u.isLocalFile() ? u.toLocalFile() : u.toString();
}

QStringList stringList(const QJsonValue &v)
{
    QStringList l;
    for (const QJsonValue &i : v.toArray()) {
        l << i.toString();
    }
    return l;
}

// ---- files

Reply fileDialog(Bridge &b, const QJsonObject &cmd, QFileDialog::AcceptMode mode, QFileDialog::FileMode fileMode)
{
    Reply fail;
    QWidget *parent = parentOf(b, cmd, fail);
    if (!fail.ok) {
        return fail;
    }
    for (const char *name : {"title", "dir", "defaultSuffix", "name"}) {
        if (cmd.contains(qs(name)) && !cmd.value(qs(name)).isString()) {
            return invalid(QStringLiteral("\"%1\" must be a string").arg(qs(name)));
        }
    }
    if (cmd.contains(QStringLiteral("filters")) && !cmd.value(QStringLiteral("filters")).isArray()) {
        return invalid(QStringLiteral("\"filters\" is an array such as [\"Tinox (*.tnx)\", \"All files (*)\"]"));
    }
    auto *dlg = new QFileDialog(parent, cmd.value(QStringLiteral("title")).toString(), cmd.value(QStringLiteral("dir")).toString());
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->setAttribute(Qt::WA_QuitOnClose, false); // closing a dialog is never "the last window closed"
    dlg->setAcceptMode(mode);
    dlg->setFileMode(fileMode);
    if (fileMode == QFileDialog::Directory) {
        dlg->setOption(QFileDialog::ShowDirsOnly, true);
    }
    const QStringList filters = stringList(cmd.value(QStringLiteral("filters")));
    if (!filters.isEmpty()) {
        dlg->setNameFilters(filters);
    }
    if (cmd.contains(QStringLiteral("defaultSuffix"))) {
        dlg->setDefaultSuffix(cmd.value(QStringLiteral("defaultSuffix")).toString());
    }
    if (cmd.contains(QStringLiteral("name"))) {
        dlg->selectFile(cmd.value(QStringLiteral("name")).toString());
    }
    dlg->setWindowModality(parent ? Qt::WindowModal : Qt::ApplicationModal);
    const int64_t id = b.registerObject(dlg);
    const QJsonValue req = cmd.contains(QStringLiteral("req")) ? cmd.value(QStringLiteral("req")) : QJsonValue(QJsonValue::Undefined);
    QObject::connect(dlg, &QDialog::finished, dlg, [&b, dlg, id, req](int result) {
        QJsonObject f = resultFields(req, id);
        f.insert(QStringLiteral("accepted"), result == QDialog::Accepted);
        QJsonArray files;
        if (result == QDialog::Accepted) {
            for (const QUrl &u : dlg->selectedUrls()) {
                files.append(localOrUrl(u));
            }
        }
        f.insert(QStringLiteral("files"), files);
        f.insert(QStringLiteral("filter"), dlg->selectedNameFilter());
        b.emitEvent(QStringLiteral("dialog.result"), f);
    });
    dlg->show();
    return opened(id);
}

Reply openFile(Bridge &b, const QJsonObject &cmd)
{
    return fileDialog(b, cmd, QFileDialog::AcceptOpen,
                      cmd.value(QStringLiteral("multiple")).toBool() ? QFileDialog::ExistingFiles : QFileDialog::ExistingFile);
}

Reply saveFile(Bridge &b, const QJsonObject &cmd)
{
    return fileDialog(b, cmd, QFileDialog::AcceptSave, QFileDialog::AnyFile);
}

Reply openDirectory(Bridge &b, const QJsonObject &cmd)
{
    return fileDialog(b, cmd, QFileDialog::AcceptOpen, QFileDialog::Directory);
}

// ---- message boxes

struct Choice {
    QString id;
    QString text;
    QMessageBox::ButtonRole role;
    QMessageBox::StandardButton standard = QMessageBox::NoButton;
};

Reply message(Bridge &b, const QJsonObject &cmd)
{
    Reply fail;
    QWidget *parent = parentOf(b, cmd, fail);
    if (!fail.ok) {
        return fail;
    }
    const QString kind = cmd.value(QStringLiteral("kind")).toString(QStringLiteral("info"));
    static const std::map<QString, QMessageBox::Icon> icons{
        {qs("info"), QMessageBox::Information},      {qs("warning"), QMessageBox::Warning}, {qs("error"), QMessageBox::Critical},
        {qs("question"), QMessageBox::Question},     {qs("yesNo"), QMessageBox::Question},  {qs("yesNoCancel"), QMessageBox::Question},
        {qs("continueCancel"), QMessageBox::Warning}};
    const auto icon = icons.find(kind);
    if (icon == icons.end()) {
        return invalid(QStringLiteral("\"kind\" is info, warning, error, question, yesNo, yesNoCancel or continueCancel"));
    }
    if (!cmd.value(QStringLiteral("text")).isString()) {
        return invalid(QStringLiteral("\"text\" is required"));
    }
    std::vector<Choice> choices;
    if (cmd.contains(QStringLiteral("buttons"))) {
        std::set<QString> seen;
        for (const QJsonValue &v : cmd.value(QStringLiteral("buttons")).toArray()) {
            const QJsonObject o = v.toObject();
            Choice c;
            c.id = o.value(QStringLiteral("id")).toString();
            c.text = o.value(QStringLiteral("text")).toString();
            const QString role = o.value(QStringLiteral("role")).toString(QStringLiteral("accept"));
            if (c.id.isEmpty() || c.text.isEmpty() || !seen.insert(c.id).second) {
                return invalid(QStringLiteral("buttons are {id, text, role?} with unique ids"));
            }
            if (role == QLatin1String("accept")) {
                c.role = QMessageBox::AcceptRole;
            } else if (role == QLatin1String("reject")) {
                c.role = QMessageBox::RejectRole;
            } else if (role == QLatin1String("destructive")) {
                c.role = QMessageBox::DestructiveRole;
            } else if (role == QLatin1String("action")) {
                c.role = QMessageBox::ActionRole;
            } else {
                return invalid(QStringLiteral("a button role is accept, reject, destructive or action"));
            }
            choices.push_back(c);
        }
        if (choices.empty()) {
            return invalid(QStringLiteral("\"buttons\" is empty"));
        }
    } else if (kind == QLatin1String("yesNo") || kind == QLatin1String("question")) {
        choices = {{qs("yes"), QString(), QMessageBox::YesRole, QMessageBox::Yes},
                   {qs("no"), QString(), QMessageBox::NoRole, QMessageBox::No}};
    } else if (kind == QLatin1String("yesNoCancel")) {
        choices = {{qs("yes"), QString(), QMessageBox::YesRole, QMessageBox::Yes},
                   {qs("no"), QString(), QMessageBox::NoRole, QMessageBox::No},
                   {qs("cancel"), QString(), QMessageBox::RejectRole, QMessageBox::Cancel}};
    } else if (kind == QLatin1String("continueCancel")) {
        choices = {{qs("continue"), QStringLiteral("Continue"), QMessageBox::AcceptRole},
                   {qs("cancel"), QString(), QMessageBox::RejectRole, QMessageBox::Cancel}};
    } else {
        choices = {{qs("ok"), QString(), QMessageBox::AcceptRole, QMessageBox::Ok}};
    }
    const QJsonValue req = cmd.contains(QStringLiteral("req")) ? cmd.value(QStringLiteral("req")) : QJsonValue(QJsonValue::Undefined);
    const QString again = cmd.value(QStringLiteral("dontAskAgain")).toString(); // KMessageBox's key
    const bool twoActions = kind == QLatin1String("yesNo") || kind == QLatin1String("question");
    // a question the user asked never to see again is answered from the config
    if (!again.isEmpty() && !cmd.contains(QStringLiteral("buttons"))) {
        QJsonObject f = resultFields(req, 0);
        f.insert(QStringLiteral("suppressed"), true);
        if (twoActions) {
            KMessageBox::ButtonCode saved;
            if (!KMessageBox::shouldBeShownTwoActions(again, saved)) {
                f.insert(QStringLiteral("button"), saved == KMessageBox::PrimaryAction ? QStringLiteral("yes") : QStringLiteral("no"));
                f.insert(QStringLiteral("accepted"), saved == KMessageBox::PrimaryAction);
                f.insert(QStringLiteral("dontAskAgain"), true);
                b.emitEvent(QStringLiteral("dialog.result"), f);
                QJsonObject r;
                r.insert(QStringLiteral("suppressed"), true);
                return Reply::success(r);
            }
        } else if (kind == QLatin1String("continueCancel") && !KMessageBox::shouldBeShownContinue(again)) {
            f.insert(QStringLiteral("button"), QStringLiteral("continue"));
            f.insert(QStringLiteral("accepted"), true);
            f.insert(QStringLiteral("dontAskAgain"), true);
            b.emitEvent(QStringLiteral("dialog.result"), f);
            QJsonObject r;
            r.insert(QStringLiteral("suppressed"), true);
            return Reply::success(r);
        }
    }
    auto *box = new QMessageBox(icon->second, cmd.value(QStringLiteral("title")).toString(), cmd.value(QStringLiteral("text")).toString(),
                                QMessageBox::NoButton, parent);
    box->setAttribute(Qt::WA_DeleteOnClose);
    box->setAttribute(Qt::WA_QuitOnClose, false);
    box->setWindowModality(parent ? Qt::WindowModal : Qt::ApplicationModal);
    if (cmd.contains(QStringLiteral("details"))) {
        box->setDetailedText(cmd.value(QStringLiteral("details")).toString());
    }
    std::map<QAbstractButton *, QString> ids;
    for (const Choice &c : choices) {
        QAbstractButton *button = c.standard != QMessageBox::NoButton ? box->addButton(c.standard) : box->addButton(c.text, c.role);
        if (!c.text.isEmpty() && c.standard != QMessageBox::NoButton) {
            button->setText(c.text);
        }
        button->setObjectName(c.id);
        ids[button] = c.id;
    }
    const QString def = cmd.value(QStringLiteral("default")).toString();
    QAbstractButton *escape = nullptr;
    // Escape and the window's close button answer: the named "cancel" button, else "cancel", else "no", else a reject button
    const QString named = cmd.value(QStringLiteral("cancel")).toString();
    int escapeRank = 99;
    for (const Choice &c : choices) {
        QAbstractButton *button = box->findChild<QAbstractButton *>(c.id);
        if (!button) {
            continue;
        }
        if (c.id == def) {
            box->setDefaultButton(qobject_cast<QPushButton *>(button));
        }
        const int rank = (!named.isEmpty() && c.id == named) ? 0
                         : c.id == QLatin1String("cancel")   ? 1
                         : c.id == QLatin1String("no")       ? 2
                         : c.role == QMessageBox::RejectRole ? 3
                                                             : 99;
        if (rank < escapeRank) {
            escapeRank = rank;
            escape = button;
        }
    }
    if (escape) {
        box->setEscapeButton(escape);
    }
    QCheckBox *check = nullptr;
    if (!again.isEmpty() && !cmd.contains(QStringLiteral("buttons")) && (twoActions || kind == QLatin1String("continueCancel"))) {
        check = new QCheckBox(QStringLiteral("Do not ask again"), box);
        box->setCheckBox(check);
    }
    const int64_t id = b.registerObject(box);
    QObject::connect(box, &QDialog::finished, box, [&b, box, id, req, ids, check, again, twoActions](int) {
        QAbstractButton *clicked = box->clickedButton() ? box->clickedButton() : box->escapeButton(); // Escape or the window's close button
        const auto it = ids.find(clicked);
        const QString button = it == ids.end() ? QStringLiteral("cancel") : it->second; // closed with the window button or Escape
        const bool dontAsk = check && check->isChecked();
        if (dontAsk && twoActions && (button == QLatin1String("yes") || button == QLatin1String("no"))) {
            KMessageBox::saveDontShowAgainTwoActions(again, button == QLatin1String("yes") ? KMessageBox::PrimaryAction
                                                                                           : KMessageBox::SecondaryAction);
        } else if (dontAsk && button == QLatin1String("continue")) {
            KMessageBox::saveDontShowAgainContinue(again);
        }
        QJsonObject f = resultFields(req, id);
        f.insert(QStringLiteral("button"), button);
        f.insert(QStringLiteral("accepted"), button != QLatin1String("cancel") && button != QLatin1String("no"));
        f.insert(QStringLiteral("dontAskAgain"), dontAsk);
        b.emitEvent(QStringLiteral("dialog.result"), f);
    });
    box->show();
    return opened(id);
}

Reply resetDontAskAgain(Bridge &, const QJsonObject &cmd)
{
    if (cmd.contains(QStringLiteral("key"))) {
        KMessageBox::enableMessage(cmd.value(QStringLiteral("key")).toString());
    } else {
        KMessageBox::enableAllMessages();
    }
    return Reply::success();
}

// ---- input

Reply input(Bridge &b, const QJsonObject &cmd)
{
    Reply fail;
    QWidget *parent = parentOf(b, cmd, fail);
    if (!fail.ok) {
        return fail;
    }
    const QString kind = cmd.value(QStringLiteral("kind")).toString(QStringLiteral("text"));
    auto *dlg = new QInputDialog(parent);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->setAttribute(Qt::WA_QuitOnClose, false); // closing a dialog is never "the last window closed"
    dlg->setWindowTitle(cmd.value(QStringLiteral("title")).toString());
    dlg->setLabelText(cmd.value(QStringLiteral("label")).toString());
    if (kind == QLatin1String("text") || kind == QLatin1String("password")) {
        dlg->setInputMode(QInputDialog::TextInput);
        dlg->setTextEchoMode(kind == QLatin1String("password") ? QLineEdit::Password : QLineEdit::Normal);
        dlg->setTextValue(cmd.value(QStringLiteral("value")).toString());
    } else if (kind == QLatin1String("int")) {
        dlg->setInputMode(QInputDialog::IntInput);
        dlg->setIntRange(cmd.value(QStringLiteral("min")).toInt(-2147483647), cmd.value(QStringLiteral("max")).toInt(2147483647));
        dlg->setIntValue(cmd.value(QStringLiteral("value")).toInt());
    } else if (kind == QLatin1String("double")) {
        dlg->setInputMode(QInputDialog::DoubleInput);
        dlg->setDoubleRange(cmd.value(QStringLiteral("min")).toDouble(-1e300), cmd.value(QStringLiteral("max")).toDouble(1e300));
        dlg->setDoubleValue(cmd.value(QStringLiteral("value")).toDouble());
    } else if (kind == QLatin1String("item")) {
        const QStringList items = stringList(cmd.value(QStringLiteral("items")));
        if (items.isEmpty()) {
            delete dlg;
            return invalid(QStringLiteral("kind \"item\" needs a non-empty \"items\" array"));
        }
        dlg->setInputMode(QInputDialog::TextInput);
        dlg->setComboBoxItems(items);
        dlg->setComboBoxEditable(cmd.value(QStringLiteral("editable")).toBool());
        dlg->setTextValue(cmd.value(QStringLiteral("value")).toString(items.first()));
    } else {
        delete dlg;
        return invalid(QStringLiteral("\"kind\" is text, password, int, double or item"));
    }
    dlg->setWindowModality(parent ? Qt::WindowModal : Qt::ApplicationModal);
    const int64_t id = b.registerObject(dlg);
    const QJsonValue req = cmd.contains(QStringLiteral("req")) ? cmd.value(QStringLiteral("req")) : QJsonValue(QJsonValue::Undefined);
    QObject::connect(dlg, &QDialog::finished, dlg, [&b, dlg, id, req, kind](int result) {
        QJsonObject f = resultFields(req, id);
        f.insert(QStringLiteral("accepted"), result == QDialog::Accepted);
        QJsonValue v;
        if (kind == QLatin1String("int")) {
            v = dlg->intValue();
        } else if (kind == QLatin1String("double")) {
            v = dlg->doubleValue();
        } else {
            v = dlg->textValue();
        }
        f.insert(QStringLiteral("value"), result == QDialog::Accepted ? v : QJsonValue(QJsonValue::Null));
        b.emitEvent(QStringLiteral("dialog.result"), f);
    });
    dlg->show();
    return opened(id);
}

// ---- progress

Reply progressOpen(Bridge &b, const QJsonObject &cmd)
{
    Reply fail;
    QWidget *parent = parentOf(b, cmd, fail);
    if (!fail.ok) {
        return fail;
    }
    const bool cancelable = cmd.value(QStringLiteral("cancelable")).toBool(true);
    auto *dlg = new QProgressDialog(cmd.value(QStringLiteral("text")).toString(), cancelable ? QStringLiteral("Cancel") : QString(),
                                    cmd.value(QStringLiteral("min")).toInt(0), cmd.value(QStringLiteral("max")).toInt(0), parent);
    if (!cancelable) {
        dlg->setCancelButton(nullptr);
    }
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->setAttribute(Qt::WA_QuitOnClose, false); // closing a dialog is never "the last window closed"
    dlg->setWindowTitle(cmd.value(QStringLiteral("title")).toString());
    dlg->setMinimumDuration(0);
    dlg->setAutoClose(false);
    dlg->setAutoReset(false);
    dlg->setWindowModality(parent ? Qt::WindowModal : Qt::NonModal); // progress of a background job should not lock the IDE
    const int64_t id = b.registerObject(dlg);
    QObject::connect(dlg, &QProgressDialog::canceled, dlg, [&b, id] {
        QJsonObject f;
        f.insert(QStringLiteral("dialog"), double(id));
        b.emitEvent(QStringLiteral("dialog.progressCanceled"), f);
    });
    dlg->show();
    return opened(id);
}

QProgressDialog *progressOf(Bridge &b, const QJsonObject &cmd, Reply &fail)
{
    const QJsonValue t = cmd.value(QStringLiteral("id"));
    QObject *o = t.isDouble() ? b.object(int64_t(t.toDouble())) : nullptr;
    auto *p = qobject_cast<QProgressDialog *>(o);
    if (!p) {
        fail = Reply::failure(t.isDouble() && !o ? TKDE_E_NOT_FOUND : TKDE_E_INVALID_ARG,
                              QStringLiteral("\"id\" must be the id of a progress dialog"));
    }
    return p;
}

Reply progressUpdate(Bridge &b, const QJsonObject &cmd)
{
    Reply fail;
    QProgressDialog *p = progressOf(b, cmd, fail);
    if (!p) {
        return fail;
    }
    if (cmd.contains(QStringLiteral("text"))) {
        p->setLabelText(cmd.value(QStringLiteral("text")).toString());
    }
    if (cmd.contains(QStringLiteral("max"))) {
        p->setMaximum(cmd.value(QStringLiteral("max")).toInt());
    }
    if (cmd.contains(QStringLiteral("value"))) {
        p->setValue(cmd.value(QStringLiteral("value")).toInt());
    }
    return Reply::success();
}

Reply progressClose(Bridge &b, const QJsonObject &cmd)
{
    Reply fail;
    QProgressDialog *p = progressOf(b, cmd, fail);
    if (!p) {
        return fail;
    }
    p->disconnect(p, &QProgressDialog::canceled, nullptr, nullptr); // closing it is not the user cancelling
    p->close();
    return Reply::success();
}

// ---- settings dialog from a description

struct Field {
    QString group;
    QString key;
    QString type; // bool, string, int, double, choice, stringList
    QJsonValue def;
    QWidget *widget = nullptr;
    QStringList choiceValues;
};

QJsonValue valueOf(const Field &f)
{
    if (auto *c = qobject_cast<QCheckBox *>(f.widget)) {
        return c->isChecked();
    }
    if (auto *e = qobject_cast<QLineEdit *>(f.widget)) {
        if (f.type == QLatin1String("stringList")) {
            QJsonArray a;
            for (const QString &s : e->text().split(QLatin1Char(','), Qt::SkipEmptyParts)) {
                a.append(s.trimmed());
            }
            return a;
        }
        return e->text();
    }
    if (auto *s = qobject_cast<QSpinBox *>(f.widget)) {
        return s->value();
    }
    if (auto *d = qobject_cast<QDoubleSpinBox *>(f.widget)) {
        return d->value();
    }
    if (auto *c = qobject_cast<QComboBox *>(f.widget)) {
        return f.choiceValues.value(c->currentIndex());
    }
    return QJsonValue();
}

void setValue(Field &f, const QJsonValue &v)
{
    if (auto *c = qobject_cast<QCheckBox *>(f.widget)) {
        c->setChecked(v.toBool());
    } else if (auto *e = qobject_cast<QLineEdit *>(f.widget)) {
        if (f.type == QLatin1String("stringList")) {
            QStringList l;
            for (const QJsonValue &i : v.toArray()) {
                l << i.toString();
            }
            e->setText(l.join(QStringLiteral(", ")));
        } else {
            e->setText(v.toString());
        }
    } else if (auto *s = qobject_cast<QSpinBox *>(f.widget)) {
        s->setValue(v.toInt());
    } else if (auto *d = qobject_cast<QDoubleSpinBox *>(f.widget)) {
        d->setValue(v.toDouble());
    } else if (auto *c = qobject_cast<QComboBox *>(f.widget)) {
        c->setCurrentIndex(std::max(0, int(f.choiceValues.indexOf(v.toString()))));
    }
}

class SettingsDialog : public KPageDialog
{
    Q_OBJECT
public:
    SettingsDialog(Bridge &b, QWidget *parent, const QString &file) : KPageDialog(parent), m_bridge(b), m_config(config::openConfig(file))
    {
    }

    QDialogButtonBox *box() { return buttonBox(); } // KPageDialog keeps it protected

    std::vector<Field> fields;
    int64_t id = 0;
    QJsonValue req;
    bool applied = false;

    void load()
    {
        for (Field &f : fields) {
            const KConfigGroup g = config::groupOf(m_config, f.group);
            setValue(f, g.hasKey(f.key)
                            ? config::toJson(g, f.key, f.type == QLatin1String("choice") ? QStringLiteral("string") : f.type, f.def)
                            : f.def);
        }
    }

    void defaults()
    {
        for (Field &f : fields) {
            setValue(f, f.def);
        }
    }

    /// Writes the fields whose value differs from what is stored; tells the logic what changed.
    void apply()
    {
        QJsonObject changes;
        for (Field &f : fields) {
            KConfigGroup g = config::groupOf(m_config, f.group);
            const QString type = f.type == QLatin1String("choice") ? QStringLiteral("string") : f.type;
            const QJsonValue now = valueOf(f);
            const QJsonValue stored = config::toJson(g, f.key, type, f.def);
            if (g.hasKey(f.key) && stored == now) {
                continue;
            }
            if (!g.hasKey(f.key) && now == f.def) {
                continue;
            }
            QString error;
            g.writeEntry(f.key, config::fromJson(now, type, error));
            changes.insert(f.group + QLatin1Char('/') + f.key, now);
        }
        m_config->sync();
        applied = true;
        QJsonObject e = resultFields(req, id);
        e.insert(QStringLiteral("changes"), changes);
        m_bridge.emitEvent(QStringLiteral("dialog.settingsApplied"), e);
    }

    Field *field(const QString &name)
    {
        for (Field &f : fields) {
            if (f.group + QLatin1Char('/') + f.key == name) {
                return &f;
            }
        }
        return nullptr;
    }

private:
    Bridge &m_bridge;
    KSharedConfigPtr m_config;
};

Reply settings(Bridge &b, const QJsonObject &cmd)
{
    Reply fail;
    QWidget *parent = parentOf(b, cmd, fail);
    if (!fail.ok) {
        return fail;
    }
    const QJsonValue pagesValue = cmd.value(QStringLiteral("pages"));
    if (!pagesValue.isArray() || pagesValue.toArray().isEmpty()) {
        return invalid(QStringLiteral("\"pages\" is a non-empty array of {id, title, icon?, fields}"));
    }
    // validate everything before a widget exists
    std::set<QString> pageIds;
    std::set<QString> fieldNames;
    static const std::set<QString> types{qs("bool"), qs("string"), qs("int"), qs("double"), qs("choice"), qs("stringList")};
    for (const QJsonValue &pv : pagesValue.toArray()) {
        const QJsonObject p = pv.toObject();
        const QString pid = p.value(QStringLiteral("id")).toString();
        if (pid.isEmpty() || p.value(QStringLiteral("title")).toString().isEmpty() || !pageIds.insert(pid).second) {
            return invalid(QStringLiteral("every page needs a unique \"id\" and a \"title\""));
        }
        for (const QJsonValue &fv : p.value(QStringLiteral("fields")).toArray()) {
            const QJsonObject f = fv.toObject();
            const QString key = f.value(QStringLiteral("key")).toString();
            const QString type = f.value(QStringLiteral("type")).toString();
            const QString group =
                f.value(QStringLiteral("group")).toString(p.value(QStringLiteral("group")).toString(QStringLiteral("General")));
            if (key.isEmpty() || f.value(QStringLiteral("label")).toString().isEmpty()) {
                return invalid(QStringLiteral("page '%1': every field needs a \"key\" and a \"label\"").arg(pid));
            }
            if (!types.count(type)) {
                return invalid(
                    QStringLiteral("page '%1', field '%2': \"type\" is bool, string, int, double, choice or stringList").arg(pid, key));
            }
            if (!fieldNames.insert(group + QLatin1Char('/') + key).second) {
                return invalid(QStringLiteral("setting %1/%2 appears twice").arg(group, key));
            }
            const QJsonValue def = f.value(QStringLiteral("default"));
            const bool defOk = def.isUndefined()                                                   ? false
                               : type == QLatin1String("bool")                                     ? def.isBool()
                               : (type == QLatin1String("int") || type == QLatin1String("double")) ? def.isDouble()
                               : type == QLatin1String("stringList")                               ? def.isArray()
                                                                                                   : def.isString();
            if (!defOk) {
                return invalid(QStringLiteral("page '%1', field '%2': a \"default\" of the field's type is required").arg(pid, key));
            }
            if (type == QLatin1String("choice")) {
                bool has = false;
                for (const QJsonValue &c : f.value(QStringLiteral("choices")).toArray()) {
                    has = has || c.isString() || c.toObject().value(QStringLiteral("value")).isString();
                }
                if (!has) {
                    return invalid(QStringLiteral("page '%1', field '%2': \"choices\" lists strings or {value, text}").arg(pid, key));
                }
            }
        }
    }
    auto *dlg = new SettingsDialog(b, parent, cmd.value(QStringLiteral("file")).toString());
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->setAttribute(Qt::WA_QuitOnClose, false); // closing a dialog is never "the last window closed"
    dlg->setFaceType(KPageDialog::List);
    dlg->setWindowTitle(cmd.value(QStringLiteral("title")).toString(QStringLiteral("Settings")));
    dlg->setStandardButtons(QDialogButtonBox::Ok | QDialogButtonBox::Apply | QDialogButtonBox::Cancel | QDialogButtonBox::RestoreDefaults);
    for (const QJsonValue &pv : pagesValue.toArray()) {
        const QJsonObject p = pv.toObject();
        auto *page = new QWidget;
        auto *form = new QFormLayout(page);
        for (const QJsonValue &fv : p.value(QStringLiteral("fields")).toArray()) {
            const QJsonObject o = fv.toObject();
            Field f;
            f.key = o.value(QStringLiteral("key")).toString();
            f.type = o.value(QStringLiteral("type")).toString();
            f.group = o.value(QStringLiteral("group")).toString(p.value(QStringLiteral("group")).toString(QStringLiteral("General")));
            f.def = o.value(QStringLiteral("default"));
            const QString label = o.value(QStringLiteral("label")).toString();
            if (f.type == QLatin1String("bool")) {
                auto *c = new QCheckBox(label);
                f.widget = c;
                form->addRow(c);
            } else {
                QWidget *w = nullptr;
                if (f.type == QLatin1String("int")) {
                    auto *s = new QSpinBox;
                    s->setRange(o.value(QStringLiteral("min")).toInt(-1000000000), o.value(QStringLiteral("max")).toInt(1000000000));
                    w = s;
                } else if (f.type == QLatin1String("double")) {
                    auto *s = new QDoubleSpinBox;
                    s->setRange(o.value(QStringLiteral("min")).toDouble(-1e9), o.value(QStringLiteral("max")).toDouble(1e9));
                    s->setDecimals(o.value(QStringLiteral("decimals")).toInt(2));
                    w = s;
                } else if (f.type == QLatin1String("choice")) {
                    auto *c = new QComboBox;
                    for (const QJsonValue &cv : o.value(QStringLiteral("choices")).toArray()) {
                        const QString value = cv.isString() ? cv.toString() : cv.toObject().value(QStringLiteral("value")).toString();
                        c->addItem(cv.isString() ? value : cv.toObject().value(QStringLiteral("text")).toString(value));
                        f.choiceValues << value;
                    }
                    w = c;
                } else {
                    auto *e = new QLineEdit;
                    e->setPlaceholderText(o.value(QStringLiteral("placeholder")).toString());
                    w = e;
                }
                if (o.contains(QStringLiteral("toolTip"))) {
                    w->setToolTip(o.value(QStringLiteral("toolTip")).toString());
                }
                f.widget = w;
                form->addRow(label, w);
            }
            f.widget->setObjectName(f.group + QLatin1Char('/') + f.key);
            dlg->fields.push_back(f);
        }
        KPageWidgetItem *item = dlg->addPage(page, p.value(QStringLiteral("title")).toString());
        item->setObjectName(p.value(QStringLiteral("id")).toString());
        if (p.contains(QStringLiteral("icon"))) {
            item->setIcon(QIcon::fromTheme(p.value(QStringLiteral("icon")).toString()));
        }
    }
    dlg->load();
    dlg->setWindowModality(parent ? Qt::WindowModal : Qt::ApplicationModal);
    dlg->id = b.registerObject(dlg);
    dlg->req = cmd.contains(QStringLiteral("req")) ? cmd.value(QStringLiteral("req")) : QJsonValue(QJsonValue::Undefined);
    QObject::connect(dlg->box()->button(QDialogButtonBox::Apply), &QPushButton::clicked, dlg, [dlg] { dlg->apply(); });
    QObject::connect(dlg->box()->button(QDialogButtonBox::RestoreDefaults), &QPushButton::clicked, dlg, [dlg] { dlg->defaults(); });
    QObject::connect(dlg, &QDialog::accepted, dlg, [dlg] { dlg->apply(); });
    QObject::connect(dlg, &QDialog::finished, dlg, [&b, dlg](int result) {
        QJsonObject f = resultFields(dlg->req, dlg->id);
        f.insert(QStringLiteral("accepted"), result == QDialog::Accepted);
        f.insert(QStringLiteral("applied"), dlg->applied);
        b.emitEvent(QStringLiteral("dialog.result"), f);
    });
    dlg->show();
    return opened(dlg->id);
}

// ---- test hook

Reply debugAct(Bridge &b, const QJsonObject &cmd)
{
    const QJsonValue t = cmd.value(QStringLiteral("id"));
    QObject *o = t.isDouble() ? b.object(int64_t(t.toDouble())) : nullptr;
    auto *dlg = qobject_cast<QDialog *>(o);
    if (!dlg) {
        return Reply::failure(TKDE_E_NOT_FOUND, QStringLiteral("no such dialog"));
    }
    const QString act = cmd.value(QStringLiteral("act")).toString();
    const QJsonValue value = cmd.value(QStringLiteral("value"));
    QJsonObject f;
    if (act == QLatin1String("state")) {
        f.insert(QStringLiteral("class"), QString::fromLatin1(dlg->metaObject()->className()));
        f.insert(QStringLiteral("title"), dlg->windowTitle());
        f.insert(QStringLiteral("modality"), int(dlg->windowModality()));
        f.insert(QStringLiteral("visible"), dlg->isVisible());
        QJsonArray buttons;
        for (QAbstractButton *btn : dlg->findChildren<QAbstractButton *>()) {
            if (!btn->text().isEmpty()) {
                buttons.append(btn->text().remove(QLatin1Char('&')));
            }
        }
        f.insert(QStringLiteral("buttons"), buttons);
        if (auto *box = qobject_cast<QMessageBox *>(dlg)) {
            f.insert(QStringLiteral("text"), box->text());
            f.insert(QStringLiteral("details"), box->detailedText());
            f.insert(QStringLiteral("hasCheckBox"), box->checkBox() != nullptr);
        }
        if (auto *file = qobject_cast<QFileDialog *>(dlg)) {
            f.insert(QStringLiteral("fileMode"), int(file->fileMode()));
            f.insert(QStringLiteral("acceptMode"), int(file->acceptMode()));
            f.insert(QStringLiteral("filters"), QJsonArray::fromStringList(file->nameFilters()));
        }
        if (auto *p = qobject_cast<QProgressDialog *>(dlg)) {
            f.insert(QStringLiteral("value"), p->value());
            f.insert(QStringLiteral("maximum"), p->maximum());
            f.insert(QStringLiteral("label"), p->labelText());
        }
        if (auto *s = qobject_cast<SettingsDialog *>(dlg)) {
            QJsonObject values;
            for (Field &fld : s->fields) {
                values.insert(fld.group + QLatin1Char('/') + fld.key, valueOf(fld));
            }
            f.insert(QStringLiteral("values"), values);
        }
        return Reply::success(f);
    }
    if (act == QLatin1String("accept")) {
        dlg->accept();
    } else if (act == QLatin1String("reject")) {
        dlg->reject();
    } else if (act == QLatin1String("button")) {
        QAbstractButton *target = nullptr;
        for (QAbstractButton *btn : dlg->findChildren<QAbstractButton *>()) {
            if (btn->objectName() == value.toString()) {
                target = btn;
            }
        }
        if (auto *p = qobject_cast<QProgressDialog *>(dlg); p && value.toString() == QLatin1String("cancel")) {
            if (auto *cancelButton = p->findChild<QPushButton *>()) {
                cancelButton->click(); // the button emits canceled(); cancel() alone does not
                return Reply::success();
            }
            return Reply::failure(TKDE_E_NOT_FOUND, QStringLiteral("this progress dialog cannot be cancelled"));
        }
        if (auto *s = qobject_cast<SettingsDialog *>(dlg)) {
            const std::map<QString, QDialogButtonBox::StandardButton> std{{qs("ok"), QDialogButtonBox::Ok},
                                                                          {qs("apply"), QDialogButtonBox::Apply},
                                                                          {qs("cancel"), QDialogButtonBox::Cancel},
                                                                          {qs("defaults"), QDialogButtonBox::RestoreDefaults}};
            const auto it = std.find(value.toString());
            target = it == std.end() ? nullptr : s->box()->button(it->second);
        }
        if (!target) {
            return Reply::failure(TKDE_E_NOT_FOUND, QStringLiteral("no button '%1'").arg(value.toString()));
        }
        target->click();
    } else if (act == QLatin1String("select")) {
        auto *file = qobject_cast<QFileDialog *>(dlg);
        if (!file) {
            return invalid(QStringLiteral("not a file dialog"));
        }
        // like a user typing the name into the file name field (selectFile() leaves a focused field alone)
        const QFileInfo target(value.toString());
        file->setDirectory(file->fileMode() == QFileDialog::Directory ? target.absolutePath() : target.absolutePath());
        if (auto *edit = file->findChild<QLineEdit *>(QStringLiteral("fileNameEdit"))) {
            edit->setText(target.fileName());
        }
        static_cast<QDialog *>(file)->accept();
    } else if (act == QLatin1String("value")) {
        auto *in = qobject_cast<QInputDialog *>(dlg);
        if (!in) {
            return invalid(QStringLiteral("not an input dialog"));
        }
        if (in->inputMode() == QInputDialog::IntInput) {
            in->setIntValue(value.toInt());
        } else if (in->inputMode() == QInputDialog::DoubleInput) {
            in->setDoubleValue(value.toDouble());
        } else {
            in->setTextValue(value.toString());
        }
        in->accept();
    } else if (act == QLatin1String("check")) {
        auto *box = qobject_cast<QMessageBox *>(dlg);
        if (!box || !box->checkBox()) {
            return invalid(QStringLiteral("no check box"));
        }
        box->checkBox()->setChecked(value.toBool());
    } else if (act == QLatin1String("field")) {
        auto *s = qobject_cast<SettingsDialog *>(dlg);
        Field *fld = s ? s->field(cmd.value(QStringLiteral("name")).toString()) : nullptr;
        if (!fld) {
            return Reply::failure(TKDE_E_NOT_FOUND, QStringLiteral("no such field"));
        }
        setValue(*fld, value);
    } else {
        return invalid(QStringLiteral("unknown act '%1'").arg(act));
    }
    return Reply::success();
}
} // namespace

void registerCommands(Bridge &bridge)
{
    bridge.addNamespace(QStringLiteral("dialog"));
    auto reg = [&bridge](const char *name, Reply (*fn)(Bridge &, const QJsonObject &)) {
        bridge.registerCommand(qs(name), [&bridge, fn](const QJsonObject &cmd) { return fn(bridge, cmd); });
    };
    reg("dialog.openFile", openFile);
    reg("dialog.saveFile", saveFile);
    reg("dialog.openDirectory", openDirectory);
    reg("dialog.message", message);
    reg("dialog.resetDontAskAgain", resetDontAskAgain);
    reg("dialog.input", input);
    reg("dialog.progress.open", progressOpen);
    reg("dialog.progress.update", progressUpdate);
    reg("dialog.progress.close", progressClose);
    reg("dialog.settings", settings);
    if (bridge.debugCommandsEnabled()) {
        reg("dialog.debug.act", debugAct);
    }
}
} // namespace tkde::dialog

#include "dialog.moc"
