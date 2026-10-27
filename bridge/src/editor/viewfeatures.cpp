#include "viewfeatures.h"

#include "../bridge.h"
#include "completion.h"
#include "diagnostics.h"
#include "positions.h"
#include "tinoxkde.h"

#include <KTextEditor/Document>
#include <KTextEditor/TextHintInterface>
#include <KTextEditor/View>

#include <QEvent>
#include <QJsonArray>
#include <QToolTip>

using namespace KTextEditor;

namespace tkde::editor
{
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
        connect(view, &View::focusOut, this, [this] {
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
            item.documentation = o.value(QStringLiteral("documentation")).toString();
            item.kind = o.value(QStringLiteral("kind")).toString();
            item.insertText = o.value(QStringLiteral("insertText")).toString();
            item.additionalEdits = o.value(QStringLiteral("additionalTextEdits")).toArray();
            items.append(item);
        }
        const bool accepted = s->completion()->setItems(int64_t(cmd.value(QStringLiteral("req")).toDouble(-1)), items);
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
    if (bridge.debugCommandsEnabled()) {
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
            return Reply::success(f);
        });
    }
}
} // namespace tkde::editor

#include "viewfeatures.moc"
