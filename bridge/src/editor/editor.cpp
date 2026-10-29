#include "editor.h"

#include "../bridge.h"
#include "breakpoints.h"
#include "diagnostics.h"
#include "positions.h"
#include "semantics.h"
#include "tinoxkde.h"
#include "viewfeatures.h"

#include <KTextEditor/Document>
#include <KTextEditor/Editor>
#include <KTextEditor/Message>
#include <KTextEditor/View>

#include <KSyntaxHighlighting/Repository>
#include <KSyntaxHighlighting/Theme>

#include <QAction>
#include <QApplication>
#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QKeyEvent>
#include <QMainWindow>
#include <QSet>
#include <QTimer>
#include <QUrl>

#include <algorithm>
#include <memory>

using namespace KTextEditor;

namespace tkde::editor
{
namespace
{
QString qs(const char *s)
{
    return QString::fromUtf8(s);
}

// -------------------------------------------------------------------- state

/// Per-document bookkeeping. A child of the Document, so it lives and dies with it.
class DocState : public QObject
{
    Q_OBJECT
public:
    DocState(Bridge &bridge, Document *doc, int64_t id) : QObject(doc), m_bridge(bridge), m_doc(doc), m_id(id)
    {
        for (int line = 0; line < doc->lines(); ++line) {
            m_lineLengths.append(doc->lineLength(line));
        }
        // One notification per editing transaction, carrying every change in order.
        // Each change's range refers to the text AFTER the previous one (LSP didChange).
        connect(doc, &Document::editingStarted, this, [this] { m_changes = QJsonArray(); });
        connect(doc, &Document::textInserted, this, [this](Document *, Cursor position, const QString &text) {
            if (m_silent) {
                return;
            }
            m_lineLengths[position.line()] += text.size();
            QJsonObject c;
            c.insert(QStringLiteral("range"), toJson(Range(position, position)));
            c.insert(QStringLiteral("text"), text);
            m_changes.append(c);
        });
        connect(doc, &Document::textRemoved, this, [this](Document *, Range range, const QString &) {
            if (m_silent) {
                return;
            }
            m_lineLengths[range.start().line()] -= range.columnWidth();
            QJsonObject c;
            c.insert(QStringLiteral("range"), toJson(range));
            c.insert(QStringLiteral("text"), QString());
            m_changes.append(c);
        });
        // KTextEditor reports newline edits separately from textInserted/textRemoved.
        connect(doc, &Document::lineWrapped, this, [this](Document *, Cursor position) {
            if (m_silent) {
                return;
            }
            const int tail = m_lineLengths[position.line()] - position.column();
            m_lineLengths[position.line()] = position.column();
            m_lineLengths.insert(position.line() + 1, tail);
            m_changes.append(
                QJsonObject{{QStringLiteral("range"), toJson(Range(position, position))}, {QStringLiteral("text"), QStringLiteral("\n")}});
        });
        connect(doc, &Document::lineUnwrapped, this, [this](Document *, int line) {
            if (m_silent) {
                return;
            }
            const int column = m_lineLengths[line - 1];
            m_lineLengths[line - 1] += m_lineLengths[line];
            m_lineLengths.removeAt(line);
            m_changes.append(
                QJsonObject{{QStringLiteral("range"), toJson(Range(line - 1, column, line, 0))}, {QStringLiteral("text"), QString()}});
        });
        connect(doc, &Document::editingFinished, this, [this] {
            if (m_changes.isEmpty() || m_silent) {
                m_changes = QJsonArray();
                return;
            }
            ++m_version;
            QJsonObject f;
            f.insert(QStringLiteral("version"), double(m_version));
            f.insert(QStringLiteral("origin"), m_apiDepth > 0 ? QStringLiteral("command") : QStringLiteral("user"));
            f.insert(QStringLiteral("changes"), m_changes);
            m_changes = QJsonArray();
            m_bridge.emitEvent(QStringLiteral("editor.changed"), f, m_id);
        });
        connect(doc, &Document::modifiedChanged, this, [this] {
            QJsonObject f;
            f.insert(QStringLiteral("modified"), m_doc->isModified());
            m_bridge.emitEvent(QStringLiteral("editor.modifiedChanged"), f, m_id);
        });
        connect(doc, &Document::documentUrlChanged, this, [this] {
            QJsonObject f;
            f.insert(QStringLiteral("url"), m_doc->url().toString());
            m_bridge.emitEvent(QStringLiteral("editor.urlChanged"), f, m_id);
        });
        connect(doc, &Document::documentSavedOrUploaded, this, [this](Document *, bool saveAs) {
            QJsonObject f;
            f.insert(QStringLiteral("url"), m_doc->url().toString());
            f.insert(QStringLiteral("saveAs"), saveAs);
            m_bridge.emitEvent(QStringLiteral("editor.saved"), f, m_id);
        });
        connect(doc, &Document::modifiedOnDisk, this, [this](Document *, bool isModified, Document::ModifiedOnDiskReason reason) {
            static const char *const names[] = {"unmodified", "modified", "created", "deleted"};
            QJsonObject f;
            f.insert(QStringLiteral("modified"), isModified);
            f.insert(QStringLiteral("reason"), qs(names[std::clamp(int(reason), 0, 3)]));
            m_bridge.emitEvent(QStringLiteral("editor.externallyModified"), f, m_id);
        });
        connect(doc, &Document::highlightingModeChanged, this, [this] {
            QJsonObject f;
            f.insert(QStringLiteral("mode"), m_doc->highlightingMode());
            m_bridge.emitEvent(QStringLiteral("editor.highlightingModeChanged"), f, m_id);
        });
        connect(doc, &Document::aboutToClose, this, [this] { m_bridge.emitEvent(QStringLiteral("editor.closing"), {}, m_id); });

        m_diagnostics = new Diagnostics(doc, this);
        new Semantics(doc);
    }

    static DocState *of(Document *d) { return d->findChild<DocState *>(QString(), Qt::FindDirectChildrenOnly); }
    Document *document() const { return m_doc; }
    int64_t id() const { return m_id; }
    int64_t version() const { return m_version; }
    Diagnostics &diagnostics() { return *m_diagnostics; }

    /// Initial content (a new document's text) is not a change: no event, no version.
    void setSilent(bool silent)
    {
        m_silent = silent;
        if (!silent) {
            m_lineLengths.clear();
            for (int line = 0; line < m_doc->lines(); ++line) {
                m_lineLengths.append(m_doc->lineLength(line));
            }
        }
    }

    /// The text was replaced wholesale from a file: one event, and a new version.
    void reloaded()
    {
        ++m_version;
        QJsonObject f;
        f.insert(QStringLiteral("version"), double(m_version));
        f.insert(QStringLiteral("url"), m_doc->url().toString());
        m_bridge.emitEvent(QStringLiteral("editor.reloaded"), f, m_id);
    }

    /// While alive, document changes are reported with origin "command".
    struct ApiScope {
        explicit ApiScope(DocState *s) : state(s) { ++state->m_apiDepth; }
        ~ApiScope() { --state->m_apiDepth; }
        DocState *state;
    };

private:
    Bridge &m_bridge;
    Document *m_doc;
    int64_t m_id;
    int64_t m_version = 0;
    int m_apiDepth = 0;
    bool m_silent = false;
    QJsonArray m_changes;
    QList<int> m_lineLengths;
    Diagnostics *m_diagnostics = nullptr;
};

// -------------------------------------------------------------------- lookup

Document *documentOf(Bridge &b, const QJsonObject &cmd, Reply &fail)
{
    const QJsonValue t = cmd.value(QStringLiteral("target"));
    if (!t.isDouble()) {
        fail = Reply::failure(TKDE_E_INVALID_ARG, QStringLiteral("this command needs the \"target\" id of a document"));
        return nullptr;
    }
    QObject *o = b.object(int64_t(t.toDouble()));
    if (!o) {
        fail = Reply::failure(TKDE_E_NOT_FOUND, QStringLiteral("no object with id %1").arg(int64_t(t.toDouble())));
        return nullptr;
    }
    auto *doc = qobject_cast<Document *>(o);
    if (!doc) {
        fail = Reply::failure(TKDE_E_INVALID_ARG, QStringLiteral("object %1 is not a document").arg(int64_t(t.toDouble())));
    }
    return doc;
}

View *viewOf(Bridge &b, const QJsonObject &cmd, Reply &fail)
{
    const QJsonValue t = cmd.value(QStringLiteral("target"));
    if (!t.isDouble()) {
        fail = Reply::failure(TKDE_E_INVALID_ARG, QStringLiteral("this command needs the \"target\" id of a view"));
        return nullptr;
    }
    QObject *o = b.object(int64_t(t.toDouble()));
    if (!o) {
        fail = Reply::failure(TKDE_E_NOT_FOUND, QStringLiteral("no object with id %1").arg(int64_t(t.toDouble())));
        return nullptr;
    }
    auto *view = qobject_cast<View *>(o);
    if (!view) {
        fail = Reply::failure(TKDE_E_INVALID_ARG, QStringLiteral("object %1 is not a view").arg(int64_t(t.toDouble())));
    }
    return view;
}

Document *newDocument(Bridge &b, int64_t &idOut)
{
    Document *doc = Editor::instance()->createDocument(nullptr);
    idOut = b.registerObject(doc);
    new DocState(b, doc, idOut);
    installMarkDefaults(doc);
    installBreakpointDefaults(doc);
    return doc;
}

QUrl urlFrom(const QString &text)
{
    const QUrl u(text);
    return (u.scheme().isEmpty() || u.scheme().length() == 1) ? QUrl::fromLocalFile(text) : u;
}

// -------------------------------------------------------------------- commands

Reply createDocument(Bridge &b, const QJsonObject &cmd)
{
    int64_t id = 0;
    Document *doc = newDocument(b, id);
    if (cmd.contains(QStringLiteral("text"))) {
        DocState *state = DocState::of(doc);
        state->setSilent(true);
        doc->setText(cmd.value(QStringLiteral("text")).toString());
        state->setSilent(false);
        doc->setModified(false);
    }
    QJsonObject f;
    f.insert(QStringLiteral("id"), double(id));
    return Reply::success(f);
}

Reply openDocument(Bridge &b, const QJsonObject &cmd)
{
    const QString url = cmd.value(QStringLiteral("url")).toString();
    if (url.isEmpty()) {
        return Reply::failure(TKDE_E_INVALID_ARG, QStringLiteral("editor.openDocument needs a \"url\""));
    }
    const QUrl u = urlFrom(url);
    if (u.isLocalFile() && !QFile::exists(u.toLocalFile())) {
        return Reply::failure(TKDE_E_NOT_FOUND, QStringLiteral("no such file: %1").arg(u.toLocalFile()));
    }
    int64_t id = 0;
    Document *doc = newDocument(b, id);
    const QString encoding = cmd.value(QStringLiteral("encoding")).toString();
    if (!encoding.isEmpty() && !doc->setEncoding(encoding)) {
        b.release(id);
        return Reply::failure(TKDE_E_INVALID_ARG, QStringLiteral("unknown encoding: %1").arg(encoding));
    }
    DocState::of(doc)->setSilent(true);
    const bool opened = doc->openUrl(u);
    DocState::of(doc)->setSilent(false);
    if (!opened || doc->openingError()) {
        b.release(id);
        return Reply::failure(TKDE_E_FAILED, QStringLiteral("cannot open %1 (the file could not be read or decoded)").arg(u.toString()));
    }
    QJsonObject f;
    f.insert(QStringLiteral("id"), double(id));
    f.insert(QStringLiteral("url"), doc->url().toString());
    f.insert(QStringLiteral("encoding"), doc->encoding());
    return Reply::success(f);
}

/// Reads the document again from its file, or from another one (`url`: the document now belongs to that
/// file, e.g. after a rename). Unsaved changes are replaced. Raises `editor.reloaded` (not `editor.changed`):
/// whoever tracks the text must take the whole text again.
Reply loadDocument(Bridge &b, const QJsonObject &cmd)
{
    Reply fail;
    Document *doc = documentOf(b, cmd, fail);
    if (!doc) {
        return fail;
    }
    const QString url = cmd.value(QStringLiteral("url")).toString();
    const QUrl target = url.isEmpty() ? doc->url() : urlFrom(url);
    if (target.isEmpty()) {
        return Reply::failure(TKDE_E_INVALID_ARG, QStringLiteral("the document has no file to read: pass \"url\""));
    }
    if (target.isLocalFile() && !QFile::exists(target.toLocalFile())) {
        return Reply::failure(TKDE_E_NOT_FOUND, QStringLiteral("no such file: %1").arg(target.toLocalFile()));
    }
    DocState *state = DocState::of(doc);
    state->setSilent(true);
    doc->setModified(false); // openUrl would otherwise ask what to do with the changes
    const bool ok = url.isEmpty() ? doc->documentReload() : doc->openUrl(target);
    state->setSilent(false);
    if (!ok || doc->openingError()) {
        return Reply::failure(TKDE_E_FAILED, QStringLiteral("cannot read %1").arg(target.toString()));
    }
    state->reloaded();
    QJsonObject f;
    f.insert(QStringLiteral("url"), doc->url().toString());
    return Reply::success(f);
}

Reply saveDocument(Bridge &b, const QJsonObject &cmd)
{
    Reply fail;
    Document *doc = documentOf(b, cmd, fail);
    if (!doc) {
        return fail;
    }
    const QString url = cmd.value(QStringLiteral("url")).toString();
    bool ok;
    if (!url.isEmpty()) {
        ok = doc->saveAs(urlFrom(url));
    } else if (doc->url().isEmpty()) {
        // documentSave() would open a file dialog on an untitled document
        return Reply::failure(TKDE_E_INVALID_ARG, QStringLiteral("the document has no URL: pass \"url\""));
    } else {
        ok = doc->documentSave();
    }
    if (!ok) {
        return Reply::failure(TKDE_E_FAILED, QStringLiteral("saving %1 failed").arg(doc->url().toString()));
    }
    QJsonObject f;
    f.insert(QStringLiteral("url"), doc->url().toString());
    return Reply::success(f);
}

Reply closeDocument(Bridge &b, const QJsonObject &cmd)
{
    Reply fail;
    Document *doc = documentOf(b, cmd, fail);
    if (!doc) {
        return fail;
    }
    const int64_t id = int64_t(cmd.value(QStringLiteral("target")).toDouble());
    doc->closeUrl(false);
    const int64_t status = b.release(id);
    return status == TKDE_OK ? Reply::success() : Reply::failure(status, QString::fromStdString(lastError()));
}

Reply createView(Bridge &b, const QJsonObject &cmd)
{
    Reply fail;
    Document *doc = documentOf(b, QJsonObject{{QStringLiteral("target"), cmd.value(QStringLiteral("document"))}}, fail);
    if (!doc) {
        return fail;
    }
    View *view = doc->createView(nullptr);
    const int64_t id = b.registerObject(view);
    installViewFeatures(b, view, id);
    QJsonObject f;
    f.insert(QStringLiteral("id"), double(id));
    return Reply::success(f);
}

Reply setText(Bridge &b, const QJsonObject &cmd)
{
    Reply fail;
    Document *doc = documentOf(b, cmd, fail);
    if (!doc) {
        return fail;
    }
    DocState::ApiScope scope(DocState::of(doc));
    // One transaction (replace the whole range) so a single undo restores the old text exactly.
    Document::EditingTransaction transaction(doc);
    doc->removeText(doc->documentRange());
    doc->insertText(Cursor(0, 0), cmd.value(QStringLiteral("text")).toString());
    return Reply::success();
}

Reply getText(Bridge &b, const QJsonObject &cmd)
{
    Reply fail;
    Document *doc = documentOf(b, cmd, fail);
    if (!doc) {
        return fail;
    }
    QString text;
    if (cmd.contains(QStringLiteral("range"))) {
        Range r;
        QString error;
        if (!readRange(cmd.value(QStringLiteral("range")), doc, r, error)) {
            return Reply::failure(TKDE_E_INVALID_ARG, error);
        }
        text = doc->text(r);
    } else {
        text = doc->text();
    }
    QJsonObject f;
    f.insert(QStringLiteral("text"), text);
    f.insert(QStringLiteral("version"), double(DocState::of(doc)->version()));
    return Reply::success(f);
}

struct Edit {
    Range range;
    QString text;
    int order;
};

// Applies every edit or none: the whole list is validated first, then applied inside
// one editing transaction, i.e. one undo step. Ranges refer to the ORIGINAL text.
Reply applyEdits(Bridge &b, const QJsonObject &cmd)
{
    Reply fail;
    Document *doc = documentOf(b, cmd, fail);
    if (!doc) {
        return fail;
    }
    const QJsonValue list = cmd.value(QStringLiteral("edits"));
    if (!list.isArray()) {
        return Reply::failure(TKDE_E_INVALID_ARG, QStringLiteral("editor.edit needs an \"edits\" array of {range, newText}"));
    }
    std::vector<Edit> edits;
    int order = 0;
    for (const QJsonValue &v : list.toArray()) {
        const QJsonObject o = v.toObject();
        Range r;
        QString error;
        if (!readRange(o.value(QStringLiteral("range")), doc, r, error)) {
            return Reply::failure(TKDE_E_INVALID_ARG, QStringLiteral("edit %1: %2").arg(order).arg(error));
        }
        if (!o.value(QStringLiteral("newText")).isString()) {
            return Reply::failure(TKDE_E_INVALID_ARG, QStringLiteral("edit %1 has no string \"newText\"").arg(order));
        }
        edits.push_back({r, o.value(QStringLiteral("newText")).toString(), order++});
    }
    std::stable_sort(edits.begin(), edits.end(), [](const Edit &x, const Edit &y) { return x.range.start() < y.range.start(); });
    for (size_t i = 1; i < edits.size(); ++i) {
        if (edits[i].range.start() < edits[i - 1].range.end()) {
            return Reply::failure(TKDE_E_INVALID_ARG,
                                  QStringLiteral("edits %1 and %2 overlap").arg(edits[i - 1].order).arg(edits[i].order));
        }
    }
    if (cmd.value(QStringLiteral("validateOnly")).toBool()) {
        return Reply::success();
    }
    if (!doc->isReadWrite()) {
        return Reply::failure(TKDE_E_FAILED, QStringLiteral("document is read-only"));
    }
    {
        DocState::ApiScope scope(DocState::of(doc));
        Document::EditingTransaction transaction(doc);
        // back to front, so earlier ranges are not moved by later edits; inserts at one
        // position end up in array order
        for (auto it = edits.rbegin(); it != edits.rend(); ++it) {
            doc->replaceText(it->range, it->text);
        }
    }
    QJsonObject f;
    f.insert(QStringLiteral("edits"), int(edits.size()));
    return Reply::success(f);
}

// Validates the complete WorkspaceEdit before any buffer changes. Qt dispatch cannot
// interleave user input between validation and application of this synchronous command.
Reply workspaceEdit(Bridge &b, const QJsonObject &cmd)
{
    if (!cmd.value(QStringLiteral("documents")).isArray()) {
        return Reply::failure(TKDE_E_INVALID_ARG, QStringLiteral("documents must be an array"));
    }
    QSet<Document *> seen;
    std::vector<QJsonObject> commands;
    QHash<Document *, QString> original;
    for (const auto &entry : cmd.value(QStringLiteral("documents")).toArray()) {
        QJsonObject edit = entry.toObject();
        Reply fail;
        Document *doc = documentOf(b, edit, fail);
        if (!doc) {
            return fail;
        }
        if (seen.contains(doc)) {
            return Reply::failure(TKDE_E_INVALID_ARG, QStringLiteral("duplicate document"));
        }
        seen.insert(doc);
        original.insert(doc, doc->text());
        const auto version = edit.value(QStringLiteral("version"));
        if (!version.isDouble() || version.toDouble() != double(DocState::of(doc)->version())) {
            return Reply::failure(TKDE_E_FAILED, QStringLiteral("workspace edit is stale"));
        }
        if (!doc->isReadWrite()) {
            return Reply::failure(TKDE_E_FAILED, QStringLiteral("document is read-only"));
        }
        edit.insert(QStringLiteral("validateOnly"), true);
        const Reply checked = applyEdits(b, edit);
        if (!checked.ok) {
            return checked;
        }
        edit.remove(QStringLiteral("validateOnly"));
        commands.push_back(edit);
    }
    for (const auto &edit : commands) {
        const Reply applied = applyEdits(b, edit);
        if (!applied.ok) {
            return applied;
        }
    }
    QJsonArray inverse;
    for (const auto &edit : commands) {
        Reply fail;
        Document *doc = documentOf(b, edit, fail);
        inverse.append(QJsonObject{{QStringLiteral("target"), edit.value(QStringLiteral("target"))},
                                   {QStringLiteral("version"), double(DocState::of(doc)->version())},
                                   {QStringLiteral("edits"), QJsonArray{QJsonObject{{QStringLiteral("range"), toJson(doc->documentRange())},
                                                                                    {QStringLiteral("newText"), original.value(doc)}}}}});
    }
    return Reply::success({{QStringLiteral("inverse"), inverse}});
}

Reply undoRedo(Bridge &b, const QJsonObject &cmd, const char *action)
{
    Reply fail;
    View *view = viewOf(b, cmd, fail);
    if (!view) {
        return fail;
    }
    QAction *a = view->action(QString::fromLatin1(action));
    if (!a || !a->isEnabled()) {
        return Reply::failure(TKDE_E_FAILED, QStringLiteral("nothing to %1").arg(qs(action).mid(5)));
    }
    DocState::ApiScope scope(DocState::of(view->document()));
    a->trigger();
    return Reply::success();
}

Reply setCursor(Bridge &b, const QJsonObject &cmd)
{
    Reply fail;
    View *view = viewOf(b, cmd, fail);
    if (!view) {
        return fail;
    }
    Cursor c;
    QString error;
    if (!readPosition(cmd.value(QStringLiteral("position")), view->document(), c, error)) {
        return Reply::failure(TKDE_E_INVALID_ARG, error);
    }
    view->setCursorPosition(c);
    return Reply::success();
}

Reply getCursor(Bridge &b, const QJsonObject &cmd)
{
    Reply fail;
    View *view = viewOf(b, cmd, fail);
    if (!view) {
        return fail;
    }
    QJsonObject f;
    f.insert(QStringLiteral("position"), toJson(view->cursorPosition()));
    return Reply::success(f);
}

Reply setSelection(Bridge &b, const QJsonObject &cmd)
{
    Reply fail;
    View *view = viewOf(b, cmd, fail);
    if (!view) {
        return fail;
    }
    const QJsonValue r = cmd.value(QStringLiteral("range"));
    if (r.isNull() || r.isUndefined()) {
        view->removeSelection();
        return Reply::success();
    }
    Range range;
    QString error;
    if (!readRange(r, view->document(), range, error)) {
        return Reply::failure(TKDE_E_INVALID_ARG, error);
    }
    view->setSelection(range);
    return Reply::success();
}

Reply getSelection(Bridge &b, const QJsonObject &cmd)
{
    Reply fail;
    View *view = viewOf(b, cmd, fail);
    if (!view) {
        return fail;
    }
    QJsonObject f;
    f.insert(QStringLiteral("range"), view->selection() ? QJsonValue(toJson(view->selectionRange())) : QJsonValue(QJsonValue::Null));
    f.insert(QStringLiteral("text"), view->selectionText());
    return Reply::success(f);
}

Reply setHighlightingMode(Bridge &b, const QJsonObject &cmd)
{
    Reply fail;
    Document *doc = documentOf(b, cmd, fail);
    if (!doc) {
        return fail;
    }
    const QString mode = cmd.value(QStringLiteral("mode")).toString();
    if (!doc->setHighlightingMode(mode)) {
        return Reply::failure(TKDE_E_NOT_FOUND,
                              QStringLiteral("unknown highlighting mode: %1 (is its syntax definition installed?)").arg(mode));
    }
    return Reply::success();
}

Reply highlightingModes(Bridge &b, const QJsonObject &cmd)
{
    Reply fail;
    Document *doc = documentOf(b, cmd, fail);
    if (!doc) {
        return fail;
    }
    QJsonObject f;
    f.insert(QStringLiteral("modes"), QJsonArray::fromStringList(doc->highlightingModes()));
    f.insert(QStringLiteral("current"), doc->highlightingMode());
    return Reply::success(f);
}

Reply setTheme(Bridge &b, const QJsonObject &cmd)
{
    Reply fail;
    View *view = viewOf(b, cmd, fail);
    if (!view) {
        return fail;
    }
    const QString theme = cmd.value(QStringLiteral("theme")).toString();
    if (!theme.isEmpty() && !Editor::instance()->repository().theme(theme).isValid()) {
        return Reply::failure(TKDE_E_NOT_FOUND, QStringLiteral("unknown theme: %1").arg(theme));
    }
    // empty = follow the Plasma colour scheme (KTextEditor's automatic theme)
    view->setConfigValue(QStringLiteral("theme"), theme);
    return Reply::success();
}

Reply themes(Bridge &, const QJsonObject &)
{
    QJsonArray names;
    for (const auto &t : Editor::instance()->repository().themes()) {
        names.append(t.name());
    }
    QJsonObject f;
    f.insert(QStringLiteral("themes"), names);
    f.insert(QStringLiteral("current"), Editor::instance()->theme().name());
    return Reply::success(f);
}

/// 0 lf, 1 crlf, 2 cr: asked from a view (the document has no public accessor).
int eolOf(Document *doc)
{
    View *view = doc->views().isEmpty() ? nullptr : doc->views().first();
    std::unique_ptr<View> temporary;
    if (!view) {
        temporary.reset(doc->createView(nullptr));
        view = temporary.get();
    }
    // set_eol is a KSelectAction (0 unix, 1 dos, 2 mac) that follows the document
    QAction *action = view->action(QStringLiteral("set_eol"));
    const int value = action ? action->property("currentItem").toInt() : 0;
    return value;
}

Reply setEndOfLine(Bridge &b, const QJsonObject &cmd)
{
    Reply fail;
    Document *doc = documentOf(b, cmd, fail);
    if (!doc) {
        return fail;
    }
    const QString eol = cmd.value(QStringLiteral("eol")).toString();
    int value;
    if (eol == QLatin1String("lf")) {
        value = 0;
    } else if (eol == QLatin1String("crlf")) {
        value = 1;
    } else if (eol == QLatin1String("cr")) {
        value = 2;
    } else {
        return Reply::failure(TKDE_E_INVALID_ARG, QStringLiteral("\"eol\" must be lf, crlf or cr"));
    }
    // Not a public config key: the view's slot setEol(int) is the way (it applies to the document).
    View *view = doc->views().isEmpty() ? nullptr : doc->views().first();
    std::unique_ptr<View> temporary;
    if (!view) {
        temporary.reset(doc->createView(nullptr));
        view = temporary.get();
    }
    if (!QMetaObject::invokeMethod(view, "setEol", Qt::DirectConnection, Q_ARG(int, value))) {
        return Reply::failure(TKDE_E_FAILED, QStringLiteral("this KTextEditor does not offer setEol"));
    }
    return Reply::success();
}

Reply setEncoding(Bridge &b, const QJsonObject &cmd)
{
    Reply fail;
    Document *doc = documentOf(b, cmd, fail);
    if (!doc) {
        return fail;
    }
    const QString encoding = cmd.value(QStringLiteral("encoding")).toString();
    if (!doc->setEncoding(encoding)) {
        return Reply::failure(TKDE_E_INVALID_ARG, QStringLiteral("unknown encoding: %1").arg(encoding));
    }
    return Reply::success();
}

Reply info(Bridge &b, const QJsonObject &cmd)
{
    Reply fail;
    Document *doc = documentOf(b, cmd, fail);
    if (!doc) {
        return fail;
    }
    static const char *const eols[] = {"lf", "crlf", "cr"};
    QJsonObject f;
    f.insert(QStringLiteral("url"), doc->url().toString());
    f.insert(QStringLiteral("modified"), doc->isModified());
    f.insert(QStringLiteral("encoding"), doc->encoding());
    f.insert(QStringLiteral("endOfLine"), qs(eols[std::clamp(eolOf(doc), 0, 2)]));
    f.insert(QStringLiteral("lines"), doc->lines());
    f.insert(QStringLiteral("version"), double(DocState::of(doc)->version()));
    f.insert(QStringLiteral("highlightingMode"), doc->highlightingMode());
    f.insert(QStringLiteral("readOnly"), !doc->isReadWrite());
    return Reply::success(f);
}

Reply showConfigDialog(Bridge &, const QJsonObject &)
{
    Editor::instance()->configDialog(QApplication::activeWindow());
    return Reply::success();
}

// A plain top-level window around a view: for development and for hosts that have not
// built their widget tree yet. The real IDE embeds views through the `widget` namespace.
Reply showViewWindow(Bridge &b, const QJsonObject &cmd)
{
    Reply fail;
    View *view = viewOf(b, cmd, fail);
    if (!view) {
        return fail;
    }
    auto *window = new QMainWindow;
    window->setAttribute(Qt::WA_DeleteOnClose);
    window->setWindowTitle(cmd.value(QStringLiteral("title")).toString(QStringLiteral("Tinox IDE")));
    window->setCentralWidget(view);
    window->resize(cmd.value(QStringLiteral("width")).toInt(900), cmd.value(QStringLiteral("height")).toInt(600));
    window->show();
    QJsonObject f;
    f.insert(QStringLiteral("id"), double(b.registerObject(window)));
    return Reply::success(f);
}

// Test hook: feeds key events to a view as if the user typed (printable text, \n, \b).
Reply debugType(Bridge &b, const QJsonObject &cmd)
{
    Reply fail;
    View *view = viewOf(b, cmd, fail);
    if (!view) {
        return fail;
    }
    QWidget *target = view->focusProxy() ? view->focusProxy() : view;
    const QString text = cmd.value(QStringLiteral("text")).toString();
    for (const QChar ch : text) {
        int key = Qt::Key_unknown;
        QString typed = QString(ch);
        if (ch == QLatin1Char('\n')) {
            key = Qt::Key_Return;
            typed = QStringLiteral("\r");
        } else if (ch == QLatin1Char('\b')) {
            key = Qt::Key_Backspace;
            typed = QString(QChar(8));
        }
        if (ch == QLatin1Char('\b')) {
            // ViewPrivate::backspace(): key presses reach it only through the real input path
            if (QMetaObject::invokeMethod(view, "backspace", Qt::DirectConnection)) {
                continue;
            }
        }
        QKeyEvent press(QEvent::KeyPress, key, Qt::NoModifier, typed);
        QKeyEvent release(QEvent::KeyRelease, key, Qt::NoModifier, typed);
        QCoreApplication::sendEvent(target, &press);
        QCoreApplication::sendEvent(target, &release);
    }
    return Reply::success();
}
} // namespace

int64_t documentVersion(KTextEditor::Document *document)
{
    return DocState::of(document)->version();
}

void registerCommands(Bridge &bridge)
{
    bridge.addNamespace(QStringLiteral("editor"));
    // MovingRanges belong to their creator (KF 6.9+) and must go while the document is alive.
    bridge.addReleaseHook([](QObject *o) {
        if (auto *d = qobject_cast<Document *>(o)) {
            if (DocState *state = DocState::of(d)) {
                state->diagnostics().clearAll();
                d->findChild<Semantics *>(QStringLiteral("semanticOverlay"))->clear();
            }
        }
    });
    auto doc = [&bridge](const char *name, Reply (*fn)(Bridge &, const QJsonObject &)) {
        bridge.registerCommand(qs(name), [&bridge, fn](const QJsonObject &cmd) { return fn(bridge, cmd); });
    };
    doc("editor.createDocument", createDocument);
    doc("editor.openDocument", openDocument);
    doc("editor.saveDocument", saveDocument);
    doc("editor.loadDocument", loadDocument);
    doc("editor.closeDocument", closeDocument);
    doc("editor.createView", createView);
    doc("editor.setText", setText);
    doc("editor.getText", getText);
    doc("editor.edit", applyEdits);
    doc("editor.workspaceEdit", workspaceEdit);
    bridge.registerCommand(QStringLiteral("editor.undo"), [&bridge](const QJsonObject &cmd) { return undoRedo(bridge, cmd, "edit_undo"); });
    bridge.registerCommand(QStringLiteral("editor.redo"), [&bridge](const QJsonObject &cmd) { return undoRedo(bridge, cmd, "edit_redo"); });
    doc("editor.setCursor", setCursor);
    doc("editor.getCursor", getCursor);
    doc("editor.setSelection", setSelection);
    doc("editor.getSelection", getSelection);
    doc("editor.setHighlightingMode", setHighlightingMode);
    doc("editor.highlightingModes", highlightingModes);
    doc("editor.setTheme", setTheme);
    doc("editor.themes", themes);
    doc("editor.setEndOfLine", setEndOfLine);
    doc("editor.setEncoding", setEncoding);
    doc("editor.info", info);
    doc("editor.showConfigDialog", showConfigDialog);
    doc("editor.showViewWindow", showViewWindow);
    registerDiagnosticCommands(bridge);
    registerBreakpointCommands(bridge);
    registerSemanticCommands(bridge);
    registerViewFeatureCommands(bridge);
    if (bridge.debugCommandsEnabled()) {
        doc("editor.debug.type", debugType);
    }
}
} // namespace tkde::editor

#include "editor.moc"
