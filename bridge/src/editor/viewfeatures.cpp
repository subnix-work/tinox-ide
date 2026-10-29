#include "viewfeatures.h"

#include "../bridge.h"
#include "completion.h"
#include "diagnostics.h"
#include "editor.h"
#include "positions.h"
#include "tinoxkde.h"

#include <KTextEditor/Document>
#include <KTextEditor/TextHintInterface>
#include <KTextEditor/View>

#include <QEvent>
#include <QJsonArray>
#include <QTextDocument>
#include <QToolTip>
#include <algorithm>

using namespace KTextEditor;

namespace tkde::editor
{
QString assistanceMarkup(const QJsonValue &value)
{
    if (value.isNull() || value.isUndefined()) {
        return {};
    }
    if (value.isArray()) {
        QStringList parts;
        for (const auto &part : value.toArray()) {
            parts << assistanceMarkup(part);
        }
        return parts.join(QStringLiteral("<hr>"));
    }
    const auto object = value.toObject();
    const QString text = value.isString() ? value.toString() : object.value(QStringLiteral("value")).toString();
    if (object.contains(QStringLiteral("language"))) {
        return QStringLiteral("<pre>%1</pre>").arg(text.toHtmlEscaped());
    }
    if (value.isString() || object.value(QStringLiteral("kind")) == QStringLiteral("markdown")) {
        QTextDocument document;
        document.setMarkdown(text, QTextDocument::MarkdownNoHTML);
        return document.toHtml();
    }
    return QStringLiteral("<p>%1</p>").arg(text.toHtmlEscaped().replace(QLatin1Char('\n'), QStringLiteral("<br>")));
}

QString signatureMarkup(const QJsonObject &result)
{
    const auto signatures = result.value(QStringLiteral("signatures")).toArray();
    if (signatures.isEmpty()) {
        return {};
    }
    int active = result.value(QStringLiteral("activeSignature")).toInt();
    if (active < 0 || active >= signatures.size()) {
        active = 0;
    }
    const auto signature = signatures.at(active).toObject();
    const QString label = signature.value(QStringLiteral("label")).toString();
    const auto parameters = signature.value(QStringLiteral("parameters")).toArray();
    const int parameter = signature.value(QStringLiteral("activeParameter")).toInt(result.value(QStringLiteral("activeParameter")).toInt());
    int start = -1, end = -1;
    QJsonObject info;
    if (parameter >= 0 && parameter < parameters.size()) {
        info = parameters.at(parameter).toObject();
        const auto span = info.value(QStringLiteral("label"));
        if (span.isArray() && span.toArray().size() == 2) {
            start = span.toArray().at(0).toInt(-1);
            end = span.toArray().at(1).toInt(-1);
        } else if (span.isString() && !span.toString().isEmpty()) {
            // Find repeated parameter labels in their declared order.
            int from = 0;
            for (int i = 0; i <= parameter; ++i) {
                const auto name = parameters.at(i).toObject().value(QStringLiteral("label")).toString();
                start = label.indexOf(name, from);
                end = start + name.size();
                if (start < 0) {
                    break;
                }
                from = end;
            }
        }
    }
    QString html = label.toHtmlEscaped();
    if (start >= 0 && end >= start && end <= label.size()) {
        html = label.left(start).toHtmlEscaped() + QStringLiteral("<b>") + label.mid(start, end - start).toHtmlEscaped() +
               QStringLiteral("</b>") + label.mid(end).toHtmlEscaped();
    }
    return QStringLiteral("<pre>%1</pre>").arg(html) + assistanceMarkup(signature.value(QStringLiteral("documentation"))) +
           assistanceMarkup(info.value(QStringLiteral("documentation")));
}

namespace
{
class ViewState;

/// Hover text for a view: the diagnostics under the mouse (synchronously) and an
/// `editor.hoverRequested` event so the host can add more with `editor.showTooltip`.
class HoverProvider : public TextHintProvider
{
public:
    HoverProvider(Bridge &bridge, int64_t viewId) : m_bridge(bridge), m_viewId(viewId) {}

    QString textHint(View *view, const Cursor &position) override
    {
        QJsonObject f;
        f.insert(QStringLiteral("req"), double(++m_request));
        f.insert(QStringLiteral("position"), toJson(position));
        m_bridge.emitEvent(QStringLiteral("editor.hoverRequested"), f, m_viewId);
        if (auto *d = view->document()->findChild<Diagnostics *>(QString(), Qt::FindChildrenRecursively)) {
            return d->hintAt(position);
        }
        return QString();
    }

    void invalidate() { ++m_request; }
    int64_t request() const { return m_request; }

private:
    Bridge &m_bridge;
    int64_t m_viewId;
    int64_t m_request = 0;
};

class ViewState : public QObject
{
    Q_OBJECT
public:
    ViewState(Bridge &bridge, View *view, int64_t id) : QObject(view), m_bridge(bridge), m_view(view), m_id(id), m_hover(bridge, id)
    {
        connect(view, &View::cursorPositionChanged, this, [this](View *, Cursor c) {
            m_hover.invalidate();
            QToolTip::hideText();
            QJsonObject f;
            f.insert(QStringLiteral("position"), toJson(c));
            m_bridge.emitEvent(QStringLiteral("editor.cursorMoved"), f, m_id);
        });
        connect(view, &View::selectionChanged, this, [this] {
            QJsonObject f;
            f.insert(QStringLiteral("range"),
                     m_view->selection() ? QJsonValue(toJson(m_view->selectionRange())) : QJsonValue(QJsonValue::Null));
            m_bridge.emitEvent(QStringLiteral("editor.selectionChanged"), f, m_id);
        });
        connect(view, &View::focusIn, this, [this] {
            QJsonObject f;
            f.insert(QStringLiteral("focused"), true);
            m_bridge.emitEvent(QStringLiteral("editor.focusChanged"), f, m_id);
        });
        connect(view->document(), &Document::textChanged, this, [this](Document *) {
            m_hover.invalidate();
            QToolTip::hideText();
        });
        connect(view, &View::focusOut, this, [this] {
            m_hover.invalidate();
            QToolTip::hideText();
            QJsonObject f;
            f.insert(QStringLiteral("focused"), false);
            m_bridge.emitEvent(QStringLiteral("editor.focusChanged"), f, m_id);
        });
        view->registerTextHintProvider(&m_hover);
    }

    // No unregistering here: the state is a child of the view and dies while the view is being
    // destroyed, when the view's interface is already gone. The view drops its providers itself.
    ~ViewState() override = default;

    static ViewState *of(View *v) { return v->findChild<ViewState *>(QString(), Qt::FindDirectChildrenOnly); }

    CompletionModel *enableCompletion(const QStringList &triggers, int minWord)
    {
        if (!m_completion) {
            m_completion = new CompletionModel(m_bridge, m_view, m_id);
            m_view->registerCompletionModel(m_completion);
            m_view->setAutomaticInvocationEnabled(true);
        }
        m_completion->setTriggers(triggers, minWord);
        return m_completion;
    }

    void disableCompletion()
    {
        if (m_completion) {
            m_view->unregisterCompletionModel(m_completion);
            delete m_completion;
            m_completion = nullptr;
        }
    }

    CompletionModel *completion() const { return m_completion; }
    int64_t hoverRequest() const { return m_hover.request(); }

private:
    Bridge &m_bridge;
    View *m_view;
    int64_t m_id;
    HoverProvider m_hover;
    CompletionModel *m_completion = nullptr;
};

ViewState *stateOf(Bridge &b, const QJsonObject &cmd, Reply &fail, View **viewOut = nullptr)
{
    const QJsonValue t = cmd.value(QStringLiteral("target"));
    QObject *o = t.isDouble() ? b.object(int64_t(t.toDouble())) : nullptr;
    auto *view = qobject_cast<View *>(o);
    if (!view) {
        fail = Reply::failure(t.isDouble() ? TKDE_E_NOT_FOUND : TKDE_E_INVALID_ARG,
                              QStringLiteral("this command needs the \"target\" id of a view"));
        return nullptr;
    }
    if (viewOut) {
        *viewOut = view;
    }
    return ViewState::of(view);
}
} // namespace

void installViewFeatures(Bridge &bridge, View *view, int64_t id)
{
    new ViewState(bridge, view, id);
}

void registerViewFeatureCommands(Bridge &bridge)
{
    bridge.registerCommand(QStringLiteral("editor.enableCompletion"), [&bridge](const QJsonObject &cmd) {
        Reply fail;
        ViewState *s = stateOf(bridge, cmd, fail);
        if (!s) {
            return fail;
        }
        QStringList triggers;
        for (const QJsonValue &v : cmd.value(QStringLiteral("triggers")).toArray()) {
            triggers.append(v.toString());
        }
        s->enableCompletion(triggers, cmd.value(QStringLiteral("minWordLength")).toInt(2));
        return Reply::success();
    });
    bridge.registerCommand(QStringLiteral("editor.disableCompletion"), [&bridge](const QJsonObject &cmd) {
        Reply fail;
        ViewState *s = stateOf(bridge, cmd, fail);
        if (!s) {
            return fail;
        }
        s->disableCompletion();
        return Reply::success();
    });
    // The host asks for the popup itself (Ctrl+Space from its own shortcut).
    bridge.registerCommand(QStringLiteral("editor.requestCompletion"), [&bridge](const QJsonObject &cmd) {
        Reply fail;
        View *view = nullptr;
        ViewState *s = stateOf(bridge, cmd, fail, &view);
        if (!s) {
            return fail;
        }
        if (!s->completion()) {
            return Reply::failure(TKDE_E_FAILED, QStringLiteral("completion is not enabled for this view (editor.enableCompletion)"));
        }
        const Cursor c = view->cursorPosition();
        const QString line = view->document()->line(c.line());
        int start = c.column();
        while (start > 0 && (line.at(start - 1).isLetterOrNumber() || line.at(start - 1) == QLatin1Char('_'))) {
            --start;
        }
        view->startCompletion(Range(c.line(), start, c.line(), c.column()), s->completion());
        return Reply::success();
    });
    bridge.registerCommand(QStringLiteral("editor.setCompletions"), [&bridge](const QJsonObject &cmd) {
        Reply fail;
        ViewState *s = stateOf(bridge, cmd, fail);
        if (!s) {
            return fail;
        }
        if (!s->completion()) {
            return Reply::failure(TKDE_E_FAILED, QStringLiteral("completion is not enabled for this view"));
        }
        QList<CompletionModel::Item> items;
        for (const QJsonValue &v : cmd.value(QStringLiteral("items")).toArray()) {
            const QJsonObject o = v.toObject();
            if (!o.value(QStringLiteral("label")).isString()) {
                return Reply::failure(TKDE_E_INVALID_ARG, QStringLiteral("every completion item needs a string \"label\""));
            }
            CompletionModel::Item item;
            item.label = o.value(QStringLiteral("label")).toString();
            item.detail = o.value(QStringLiteral("detail")).toString();
            const auto documentation = o.value(QStringLiteral("documentation"));
            item.documentation =
                assistanceMarkup(documentation.isString() ? QJsonValue(QJsonObject{{QStringLiteral("kind"), QStringLiteral("plaintext")},
                                                                                   {QStringLiteral("value"), documentation}})
                                                          : documentation);
            const auto kind = o.value(QStringLiteral("kind"));
            static const QStringList kinds = {QStringLiteral(""),          QStringLiteral("text"),        QStringLiteral("method"),
                                              QStringLiteral("function"),  QStringLiteral("constructor"), QStringLiteral("field"),
                                              QStringLiteral("variable"),  QStringLiteral("class"),       QStringLiteral("interface"),
                                              QStringLiteral("module"),    QStringLiteral("property"),    QStringLiteral("unit"),
                                              QStringLiteral("value"),     QStringLiteral("enum"),        QStringLiteral("keyword"),
                                              QStringLiteral("snippet"),   QStringLiteral("color"),       QStringLiteral("file"),
                                              QStringLiteral("reference"), QStringLiteral("folder"),      QStringLiteral("enum"),
                                              QStringLiteral("constant"),  QStringLiteral("struct"),      QStringLiteral("event"),
                                              QStringLiteral("operator"),  QStringLiteral("type")};
            item.kind = kind.isDouble() ? kinds.value(kind.toInt()) : kind.toString();
            item.insertText = o.value(QStringLiteral("insertText")).toString();
            item.snippet = o.value(QStringLiteral("insertTextFormat")).toInt(1) == 2;
            const auto edit = o.value(QStringLiteral("textEdit")).toObject();
            if (!edit.isEmpty()) {
                item.textEdit = edit;
                item.insertText = edit.value(QStringLiteral("newText")).toString();
            }
            item.sortText = o.value(QStringLiteral("sortText")).toString(item.label);
            item.additionalEdits = o.value(QStringLiteral("additionalTextEdits")).toArray();
            items.append(item);
        }
        std::stable_sort(items.begin(), items.end(), [](const auto &a, const auto &b) { return a.sortText < b.sortText; });
        const bool accepted = s->completion()->setItems(int64_t(cmd.value(QStringLiteral("req")).toDouble(-1)), items,
                                                        cmd.value(QStringLiteral("isIncomplete")).toBool());
        QJsonObject f;
        f.insert(QStringLiteral("stale"), !accepted); // an answer to an older request is normal, not an error
        return Reply::success(f);
    });
    bridge.registerCommand(QStringLiteral("editor.showTooltip"), [&bridge](const QJsonObject &cmd) {
        Reply fail;
        View *view = nullptr;
        ViewState *s = stateOf(bridge, cmd, fail, &view);
        if (!s) {
            return fail;
        }
        Cursor c;
        QString error;
        if (!readPosition(cmd.value(QStringLiteral("position")), view->document(), c, error)) {
            return Reply::failure(TKDE_E_INVALID_ARG, error);
        }
        const QPoint where = view->cursorToCoordinate(c);
        if (where.x() < 0 || where.y() < 0) {
            return Reply::failure(TKDE_E_FAILED, QStringLiteral("that position is not visible in the view"));
        }
        QToolTip::showText(view->mapToGlobal(where), cmd.value(QStringLiteral("markup")).toString(), view);
        return Reply::success();
    });
    bridge.registerCommand(QStringLiteral("editor.showAssistance"), [&bridge](const QJsonObject &cmd) {
        Reply fail;
        View *view = nullptr;
        const auto state = stateOf(bridge, cmd, fail, &view);
        if (!state) {
            return fail;
        }
        if (cmd.contains(QStringLiteral("version")) &&
            cmd.value(QStringLiteral("version")).toInteger(-1) != documentVersion(view->document())) {
            return Reply::success(QJsonObject{{QStringLiteral("stale"), true}});
        }
        if (cmd.value(QStringLiteral("kind")) == QStringLiteral("hover") &&
            cmd.value(QStringLiteral("req")).toInteger(-1) != state->hoverRequest()) {
            return Reply::success(QJsonObject{{QStringLiteral("stale"), true}});
        }
        Cursor position;
        QString error;
        if (!readPosition(cmd.value(QStringLiteral("position")), view->document(), position, error)) {
            return Reply::failure(TKDE_E_INVALID_ARG, error);
        }
        if (cmd.value(QStringLiteral("kind")) == QStringLiteral("signatureHelp") && position != view->cursorPosition()) {
            return Reply::success(QJsonObject{{QStringLiteral("stale"), true}});
        }
        const auto result = cmd.value(QStringLiteral("result")).toObject();
        const auto markup = cmd.value(QStringLiteral("kind")) == QStringLiteral("signatureHelp")
                                ? signatureMarkup(result)
                                : assistanceMarkup(result.value(QStringLiteral("contents")));
        if (result.isEmpty()) {
            QToolTip::hideText();
            return Reply::success();
        }
        const auto where = view->cursorToCoordinate(position);
        if (where.x() >= 0 && where.y() >= 0) {
            QToolTip::showText(view->mapToGlobal(where), markup, view);
        }
        return Reply::success();
    });
    if (bridge.debugCommandsEnabled()) {
        bridge.registerCommand(QStringLiteral("editor.debug.assistanceMarkup"), [](const QJsonObject &cmd) {
            const auto result = cmd.value(QStringLiteral("result"));
            return Reply::success(
                QJsonObject{{QStringLiteral("markup"), cmd.value(QStringLiteral("kind")) == QStringLiteral("signatureHelp")
                                                           ? signatureMarkup(result.toObject())
                                                           : assistanceMarkup(result)}});
        });
        // Test hooks: accept a completion item without a popup, and read the model state.
        bridge.registerCommand(QStringLiteral("editor.debug.acceptCompletion"), [&bridge](const QJsonObject &cmd) {
            Reply fail;
            View *view = nullptr;
            ViewState *s = stateOf(bridge, cmd, fail, &view);
            if (!s || !s->completion()) {
                return s ? Reply::failure(TKDE_E_FAILED, QStringLiteral("completion is not enabled")) : fail;
            }
            Cursor c = view->cursorPosition();
            const QString line = view->document()->line(c.line());
            int start = c.column();
            while (start > 0 && (line.at(start - 1).isLetterOrNumber() || line.at(start - 1) == QLatin1Char('_'))) {
                --start;
            }
            s->completion()->executeCompletionItem(view, Range(c.line(), start, c.line(), c.column()),
                                                   s->completion()->index(cmd.value(QStringLiteral("index")).toInt(), 0));
            return Reply::success();
        });
        bridge.registerCommand(QStringLiteral("editor.debug.completionState"), [&bridge](const QJsonObject &cmd) {
            Reply fail;
            ViewState *s = stateOf(bridge, cmd, fail);
            if (!s || !s->completion()) {
                return s ? Reply::failure(TKDE_E_FAILED, QStringLiteral("completion is not enabled")) : fail;
            }
            QJsonObject f;
            f.insert(QStringLiteral("req"), double(s->completion()->currentRequest()));
            f.insert(QStringLiteral("items"), s->completion()->itemCount());
            f.insert(QStringLiteral("labels"), s->completion()->labels());
            return Reply::success(f);
        });
    }
}
} // namespace tkde::editor

#include "viewfeatures.moc"
