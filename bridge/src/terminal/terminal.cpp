#include "terminal.h"
#include "../bridge.h"
#include "tinoxkde.h"

#include <KParts/ReadOnlyPart>
#include <KPluginFactory>
#include <KPluginMetaData>
#include <kde_terminal_interface.h>

#include <QDir>
#include <QFontDatabase>
#include <QJsonArray>
#include <QPlainTextEdit>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextFragment>
#include <QVBoxLayout>

namespace tkde::terminal
{
namespace
{
// The container outlives a Konsole session so a redraw never embeds a dead id.
class Terminal : public QWidget
{
public:
    QPointer<KParts::ReadOnlyPart> part;
    QPlainTextEdit *console = nullptr;
    QString directory;
    QString pending;
    QTextCharFormat format;
    bool closing = false;
    bool finished = false;

    ~Terminal() override
    {
        closing = true;
        delete part.data();
    }

    TerminalInterface *interface() const { return part ? qobject_cast<TerminalInterface *>(part.data()) : nullptr; }

    // Incremental SGR decoder: preserve escape sequences split across writes.
    void append(const QString &text)
    {
        pending += text;
        QTextCursor cursor(console->document());
        cursor.movePosition(QTextCursor::End);
        qsizetype pos = 0;
        while (pos < pending.size()) {
            if (pending.at(pos) != QChar(0x1b)) {
                const qsizetype end = pending.indexOf(QChar(0x1b), pos);
                const qsizetype count = (end < 0 ? pending.size() : end) - pos;
                cursor.insertText(pending.mid(pos, count), format);
                pos += count;
                continue;
            }
            if (pos + 1 == pending.size()) {
                break;
            }
            if (pending.at(pos + 1) != QLatin1Char('[')) {
                pos += 2;
                continue;
            }
            qsizetype end = pos + 2;
            while (end < pending.size() && !(pending.at(end).unicode() >= 0x40 && pending.at(end).unicode() <= 0x7e)) {
                ++end;
            }
            if (end == pending.size()) {
                break;
            }
            if (pending.at(end) == QLatin1Char('m')) {
                const QStringList codes = pending.mid(pos + 2, end - pos - 2).split(QLatin1Char(';'));
                const QColor colors[] = {QColor(0, 0, 0),     QColor(170, 0, 0),    QColor(0, 170, 0),    QColor(170, 85, 0),
                                         QColor(0, 0, 170),   QColor(170, 0, 170),  QColor(0, 170, 170),  QColor(170, 170, 170),
                                         QColor(85, 85, 85),  QColor(255, 85, 85),  QColor(85, 255, 85),  QColor(255, 255, 85),
                                         QColor(85, 85, 255), QColor(255, 85, 255), QColor(85, 255, 255), QColor(255, 255, 255)};
                for (qsizetype i = 0; i < codes.size(); ++i) {
                    const int code = codes.at(i).toInt();
                    if (code == 0) {
                        format = QTextCharFormat();
                    } else if (code == 1 || code == 22) {
                        format.setFontWeight(code == 1 ? QFont::Bold : QFont::Normal);
                    } else if (code == 39) {
                        format.clearForeground();
                    } else if (code == 49) {
                        format.clearBackground();
                    } else if ((code >= 30 && code <= 37) || (code >= 90 && code <= 97)) {
                        format.setForeground(colors[code >= 90 ? code - 90 + 8 : code - 30]);
                    } else if ((code >= 40 && code <= 47) || (code >= 100 && code <= 107)) {
                        format.setBackground(colors[code >= 100 ? code - 100 + 8 : code - 40]);
                    } else if ((code == 38 || code == 48) && i + 1 < codes.size()) {
                        // Consume unsupported extended colours as one sequence, never as SGR resets.
                        const int mode = codes.at(++i).toInt();
                        i += mode == 2 ? 3 : (mode == 5 ? 1 : 0);
                    }
                }
            }
            pos = end + 1;
        }
        pending.remove(0, pos);
        if (pending.size() > 1024) {
            pending.clear();
        }
        console->setTextCursor(cursor);
        console->ensureCursorVisible();
    }
};

Reply invalid(const QString &message)
{
    return Reply::failure(TKDE_E_INVALID_ARG, message);
}

bool validDirectory(const QString &directory)
{
    if (!QDir::isAbsolutePath(directory) || !QDir(directory).exists()) {
        return false;
    }
    for (const QChar ch : directory) {
        if (ch.unicode() < 0x20 || ch.unicode() == 0x7f) {
            return false;
        }
    }
    return true;
}

Reply create(Bridge &b, const QJsonObject &cmd)
{
    for (const char *field : {"directory", "title", "backend"}) {
        const QString key = QString::fromLatin1(field);
        if (cmd.contains(key) && !cmd.value(key).isString()) {
            return invalid(key + QStringLiteral(" must be a string"));
        }
    }
    const QString backend = cmd.value(QStringLiteral("backend")).toString(QStringLiteral("auto"));
    if (backend != QLatin1String("auto") && backend != QLatin1String("console")) {
        return invalid(QStringLiteral("backend must be auto or console"));
    }
    const QString directory = cmd.value(QStringLiteral("directory")).toString(QDir::homePath());
    if (!validDirectory(directory)) {
        return invalid(QStringLiteral("directory must be an existing absolute directory without control characters"));
    }
    auto *t = new Terminal;
    t->directory = directory;
    t->setWindowTitle(cmd.value(QStringLiteral("title")).toString(QStringLiteral("Terminal")));
    auto *layout = new QVBoxLayout(t);
    layout->setContentsMargins(0, 0, 0, 0);
    QString reason;
    if (backend == QLatin1String("auto")) {
        const auto result =
            KPluginFactory::instantiatePlugin<KParts::ReadOnlyPart>(KPluginMetaData(QStringLiteral("kf6/parts/konsolepart")), t);
        t->part = result.plugin;
        reason = result.errorString;
        if (t->part && !t->interface()) {
            delete t->part.data();
            reason = QStringLiteral("Konsole part has no TerminalInterface");
        }
    }
    const int64_t id = b.registerObject(t);
    if (t->part) {
        layout->addWidget(t->part->widget());
        QObject::connect(t->part, &QObject::destroyed, t, [&b, t, id] {
            if (!t->closing) {
                t->finished = true;
                b.emitEvent(QStringLiteral("terminal.finished"), {}, id);
            }
        });
        t->interface()->showShellInDir(directory);
    } else {
        t->console = new QPlainTextEdit(t);
        t->console->setReadOnly(true);
        t->console->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
        t->console->setMaximumBlockCount(10000);
        layout->addWidget(t->console);
        t->console->setPlaceholderText(QStringLiteral("Konsole is unavailable. Read-only output console."));
    }
    return Reply::success({{QStringLiteral("id"), double(id)},
                           {QStringLiteral("backend"), t->part ? QStringLiteral("konsole") : QStringLiteral("console")},
                           {QStringLiteral("reason"), reason}});
}
} // namespace

void registerCommands(Bridge &b)
{
    b.addNamespace(QStringLiteral("terminal"));
    b.registerCommand(QStringLiteral("terminal.create"), [&b](const QJsonObject &cmd) { return create(b, cmd); });
    for (const QString &verb :
         {QStringLiteral("sendText"), QStringLiteral("setTitle"), QStringLiteral("setDirectory"), QStringLiteral("info")}) {
        b.registerCommand(QStringLiteral("terminal.") + verb, [&b, verb](const QJsonObject &cmd) {
            const auto target = cmd.value(QStringLiteral("target"));
            if (!target.isDouble() || target.toDouble() != double(target.toInteger())) {
                return invalid(QStringLiteral("target must be an integer"));
            }
            auto *t = dynamic_cast<Terminal *>(b.object(target.toInteger()));
            if (!t) {
                return Reply::failure(TKDE_E_NOT_FOUND, QStringLiteral("unknown terminal"));
            }
            if (verb == QLatin1String("info")) {
                return Reply::success({{QStringLiteral("directory"), t->directory},
                                       {QStringLiteral("title"), t->windowTitle()},
                                       {QStringLiteral("backend"), t->console ? QStringLiteral("console") : QStringLiteral("konsole")},
                                       {QStringLiteral("finished"), t->finished}});
            }
            const QString key = verb == QLatin1String("setDirectory") ? QStringLiteral("directory")
                                : verb == QLatin1String("setTitle")   ? QStringLiteral("title")
                                                                      : QStringLiteral("text");
            if (!cmd.value(key).isString()) {
                return invalid(key + QStringLiteral(" must be a string"));
            }
            const QString value = cmd.value(key).toString();
            if (verb == QLatin1String("setTitle")) {
                t->setWindowTitle(value);
            } else if (verb == QLatin1String("setDirectory")) {
                if (!validDirectory(value)) {
                    return invalid(QStringLiteral("directory must be an existing absolute directory without control characters"));
                }
                if (auto *iface = t->interface()) {
                    if (iface->foregroundProcessId() != -1) {
                        return Reply::failure(TKDE_E_FAILED, QStringLiteral("terminal is busy; directory was not changed"));
                    }
                    QString quoted = value;
                    quoted.replace(QLatin1Char('\''), QStringLiteral("'\\''"));
                    iface->sendInput(QStringLiteral("\x05\x15 cd -- '") + quoted + QStringLiteral("'\n"));
                }
                t->directory = value;
            } else if (t->console) {
                t->append(value);
            } else if (auto *iface = t->interface()) {
                iface->sendInput(value);
            } else {
                return Reply::failure(TKDE_E_FAILED, QStringLiteral("terminal process has finished"));
            }
            return Reply::success();
        });
    }
    if (b.debugCommandsEnabled()) {
        b.registerCommand(QStringLiteral("terminal.debug.dump"), [&b](const QJsonObject &cmd) {
            auto *t = dynamic_cast<Terminal *>(b.object(cmd.value(QStringLiteral("target")).toInteger()));
            if (!t || !t->console) {
                return Reply::failure(TKDE_E_NOT_FOUND, QStringLiteral("not a console"));
            }
            QJsonArray runs;
            for (QTextBlock block = t->console->document()->begin(); block.isValid(); block = block.next()) {
                for (auto it = block.begin(); !it.atEnd(); ++it) {
                    const auto fragment = it.fragment();
                    runs.append(QJsonObject{{QStringLiteral("text"), fragment.text()},
                                            {QStringLiteral("color"), fragment.charFormat().foreground().color().name()}});
                }
            }
            return Reply::success({{QStringLiteral("text"), t->console->toPlainText()},
                                   {QStringLiteral("readOnly"), t->console->isReadOnly()},
                                   {QStringLiteral("runs"), runs}});
        });
    }
    // Releasing a terminal is not a process-exit notification.
    b.addReleaseHook([](QObject *object) {
        if (auto *t = dynamic_cast<Terminal *>(object)) {
            t->closing = true;
        }
    });
}
} // namespace tkde::terminal
