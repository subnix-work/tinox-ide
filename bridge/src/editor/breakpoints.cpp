#include "breakpoints.h"

#include "../bridge.h"
#include "tinoxkde.h"

#include <QIcon>
#include <QJsonArray>
#include <QJsonObject>

#include <algorithm>
#include <optional>

using namespace KTextEditor;

namespace tkde::editor
{
namespace
{
constexpr uint kBreakpoint = Document::BreakpointActive;
constexpr uint kExecution = Document::Execution;

/// Reports clicks in the gutter. Our own changes (`setBreakpoints`) are not reported.
class Watcher : public QObject
{
    Q_OBJECT
public:
    Watcher(Bridge &bridge, Document *doc, int64_t id) : QObject(doc), m_bridge(bridge), m_id(id)
    {
        connect(doc, &Document::markChanged, this, [this](Document *, Mark mark, Document::MarkChangeAction action) {
            if (m_silent || !(mark.type & kBreakpoint)) {
                return;
            }
            if (action != Document::MarkAdded && action != Document::MarkRemoved) {
                return;
            }
            QJsonObject f;
            f.insert(QStringLiteral("line"), mark.line);
            f.insert(QStringLiteral("added"), action == Document::MarkAdded);
            m_bridge.emitEvent(QStringLiteral("editor.breakpointToggled"), f, m_id);
        });
    }

    struct Silence {
        explicit Silence(Watcher *w) : watcher(w) { watcher->m_silent = true; }
        ~Silence() { watcher->m_silent = false; }
        Watcher *watcher;
    };

private:
    Bridge &m_bridge;
    int64_t m_id;
    bool m_silent = false;
};

Document *documentOf(Bridge &b, const QJsonObject &cmd, Reply &fail)
{
    const QJsonValue t = cmd.value(QStringLiteral("target"));
    QObject *o = t.isDouble() ? b.object(int64_t(t.toDouble())) : nullptr;
    auto *doc = qobject_cast<Document *>(o);
    if (!doc) {
        fail = Reply::failure(t.isDouble() ? TKDE_E_NOT_FOUND : TKDE_E_INVALID_ARG,
                              QStringLiteral("this command needs the \"target\" id of a document"));
    }
    return doc;
}

/// A line number inside the document, or -1 with `error` set.
int readLine(const QJsonValue &value, Document *doc, QString &error)
{
    if (!value.isDouble() || value.toDouble() != double(value.toInteger())) {
        error = QStringLiteral("a line must be an integer");
        return -1;
    }
    const qint64 line = value.toInteger();
    if (line < 0 || line >= doc->lines()) {
        error = QStringLiteral("line %1 is outside the document (%2 lines)").arg(line).arg(doc->lines());
        return -1;
    }
    return int(line);
}

QJsonArray linesWith(Document *doc, uint type)
{
    QList<int> lines;
    const auto marks = doc->marks();
    for (auto it = marks.begin(); it != marks.end(); ++it) {
        if (it.value()->type & type) {
            lines.append(it.key());
        }
    }
    std::sort(lines.begin(), lines.end());
    QJsonArray out;
    for (int line : lines) {
        out.append(line);
    }
    return out;
}
} // namespace

void installBreakpointDefaults(Document *doc)
{
    doc->setEditableMarks(kBreakpoint);
    doc->setMarkDescription(Document::BreakpointActive, QStringLiteral("Breakpoint"));
    doc->setMarkDescription(Document::Execution, QStringLiteral("Stopped here"));
    doc->setMarkIcon(Document::BreakpointActive, QIcon::fromTheme(QStringLiteral("media-record")));
    doc->setMarkIcon(Document::Execution, QIcon::fromTheme(QStringLiteral("go-next")));
}

void registerBreakpointCommands(Bridge &bridge)
{
    // Reports the toggles for every document; the id is looked up when the document is created.
    bridge.registerCommand(QStringLiteral("editor.watchBreakpoints"), [&bridge](const QJsonObject &cmd) {
        Reply fail;
        Document *doc = documentOf(bridge, cmd, fail);
        if (!doc) {
            return fail;
        }
        if (!doc->findChild<Watcher *>(QString(), Qt::FindDirectChildrenOnly)) {
            new Watcher(bridge, doc, int64_t(cmd.value(QStringLiteral("target")).toDouble()));
        }
        return Reply::success();
    });
    bridge.registerCommand(QStringLiteral("editor.setBreakpoints"), [&bridge](const QJsonObject &cmd) {
        Reply fail;
        Document *doc = documentOf(bridge, cmd, fail);
        if (!doc) {
            return fail;
        }
        const QJsonValue lines = cmd.value(QStringLiteral("lines"));
        if (!lines.isArray()) {
            return Reply::failure(TKDE_E_INVALID_ARG, QStringLiteral("lines must be an array of line numbers"));
        }
        QList<int> wanted;
        QString error;
        for (const QJsonValue &value : lines.toArray()) {
            const int line = readLine(value, doc, error);
            if (line < 0) {
                return Reply::failure(TKDE_E_INVALID_ARG, error);
            }
            wanted.append(line);
        }
        // Nothing changes before every line was checked. The user's clicks are not reported for this.
        auto *watcher = doc->findChild<Watcher *>(QString(), Qt::FindDirectChildrenOnly);
        std::optional<Watcher::Silence> silence;
        if (watcher) {
            silence.emplace(watcher);
        }
        const auto marks = doc->marks();
        for (auto it = marks.begin(); it != marks.end(); ++it) {
            if (it.value()->type & kBreakpoint) {
                doc->removeMark(it.key(), kBreakpoint);
            }
        }
        for (int line : wanted) {
            doc->addMark(line, kBreakpoint);
        }
        return Reply::success();
    });
    bridge.registerCommand(QStringLiteral("editor.getBreakpoints"), [&bridge](const QJsonObject &cmd) {
        Reply fail;
        Document *doc = documentOf(bridge, cmd, fail);
        if (!doc) {
            return fail;
        }
        QJsonObject f;
        f.insert(QStringLiteral("lines"), linesWith(doc, kBreakpoint));
        return Reply::success(f);
    });
    bridge.registerCommand(QStringLiteral("editor.setExecutionLine"), [&bridge](const QJsonObject &cmd) {
        Reply fail;
        Document *doc = documentOf(bridge, cmd, fail);
        if (!doc) {
            return fail;
        }
        const QJsonValue value = cmd.value(QStringLiteral("line"));
        int line = -1;
        if (!value.isNull()) {
            QString error;
            line = readLine(value, doc, error);
            if (line < 0) {
                return Reply::failure(TKDE_E_INVALID_ARG, error);
            }
        }
        const auto marks = doc->marks();
        for (auto it = marks.begin(); it != marks.end(); ++it) {
            if (it.value()->type & kExecution) {
                doc->removeMark(it.key(), kExecution);
            }
        }
        if (line >= 0) {
            doc->addMark(line, kExecution);
        }
        return Reply::success();
    });
    if (bridge.debugCommandsEnabled()) {
        // Test hook: toggles a breakpoint like a click in the gutter would (reported as an event).
        bridge.registerCommand(QStringLiteral("editor.debug.toggleBreakpoint"), [&bridge](const QJsonObject &cmd) {
            Reply fail;
            Document *doc = documentOf(bridge, cmd, fail);
            if (!doc) {
                return fail;
            }
            QString error;
            const int line = readLine(cmd.value(QStringLiteral("line")), doc, error);
            if (line < 0) {
                return Reply::failure(TKDE_E_INVALID_ARG, error);
            }
            if (doc->mark(line) & kBreakpoint) {
                doc->removeMark(line, kBreakpoint);
            } else {
                doc->addMark(line, kBreakpoint);
            }
            return Reply::success();
        });
    }
}
} // namespace tkde::editor

#include "breakpoints.moc"
