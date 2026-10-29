#include "completion.h"

#include "../bridge.h"
#include "positions.h"
#include "viewfeatures.h"

#include <KTextEditor/Document>
#include <KTextEditor/MovingCursor>
#include <KTextEditor/View>

#include <QIcon>
#include <QJsonDocument>
#include <QRegularExpression>

#include <algorithm>
#include <memory>
#include <vector>

using namespace KTextEditor;

namespace tkde::editor
{
namespace
{
QString iconName(const QString &kind)
{
    static const QHash<QString, QString> icons = {
        {QStringLiteral("function"), QStringLiteral("code-function")}, {QStringLiteral("method"), QStringLiteral("code-function")},
        {QStringLiteral("class"), QStringLiteral("code-class")},       {QStringLiteral("interface"), QStringLiteral("code-class")},
        {QStringLiteral("enum"), QStringLiteral("code-class")},        {QStringLiteral("variable"), QStringLiteral("code-variable")},
        {QStringLiteral("field"), QStringLiteral("code-variable")},    {QStringLiteral("property"), QStringLiteral("code-variable")},
        {QStringLiteral("keyword"), QStringLiteral("code-context")},   {QStringLiteral("module"), QStringLiteral("code-block")},
        {QStringLiteral("snippet"), QStringLiteral("insert-text")},
    };
    return icons.value(kind, QStringLiteral("code-context"));
}

bool isWordChar(QChar c)
{
    return c.isLetterOrNumber() || c == QLatin1Char('_');
}

struct PlannedEdit {
    Range range;
    QString text;
};
} // namespace

CompletionModel::CompletionModel(Bridge &bridge, View *view, int64_t viewId)
    : CodeCompletionModel(view), m_bridge(bridge), m_viewId(viewId), m_view(view)
{
    setHasGroups(false);
    connect(view, &View::cursorPositionChanged, this, [this](View *, Cursor) { ++m_request; });
    connect(view->document(), &Document::textChanged, this, [this](Document *) { ++m_request; });
    connect(view, &View::focusOut, this, [this] { ++m_request; });
}

void CompletionModel::setTriggers(const QStringList &triggers, int minWordLength)
{
    m_triggers = triggers;
    m_minWordLength = std::max(1, minWordLength);
}

void CompletionModel::completionInvoked(View *view, const Range &range, InvocationType type)
{
    m_word = range;
    if (m_answerPending) {
        m_answerPending = false; // the popup was opened by our own answer: keep its items
        return;
    }
    beginResetModel();
    m_items.clear();
    endResetModel();
    static const char *const names[] = {"automatic", "user", "manual", "manual"};
    ask(view, range, QString::fromLatin1(names[std::clamp(int(type), 0, 3)]));
}

void CompletionModel::ask(View *view, const Range &range, const QString &trigger)
{
    ++m_request;
    m_requestedPosition = view->cursorPosition();
    m_word = range;
    QJsonObject f;
    f.insert(QStringLiteral("req"), double(m_request));
    f.insert(QStringLiteral("position"), toJson(view->cursorPosition()));
    f.insert(QStringLiteral("word"), toJson(range));
    f.insert(QStringLiteral("wordText"), view->document()->text(range));
    f.insert(QStringLiteral("trigger"), trigger);
    m_bridge.emitEvent(QStringLiteral("editor.completionRequested"), f, m_viewId);
}

bool CompletionModel::shouldStartCompletion(View *view, const QString &insertedText, bool userInsertion, const Cursor &position)
{
    if (!userInsertion || insertedText.isEmpty()) {
        return false;
    }
    const QString before = view->document()->line(position.line()).left(position.column());
    for (const QString &t : std::as_const(m_triggers)) {
        if (before.endsWith(t)) {
            return true;
        }
    }
    if (!isWordChar(insertedText.at(insertedText.size() - 1))) {
        return false;
    }
    int n = 0;
    for (int i = before.size() - 1; i >= 0 && isWordChar(before.at(i)); --i) {
        ++n;
    }
    return n >= m_minWordLength;
}

QString CompletionModel::filterString(View *view, const Range &range, const Cursor &position)
{
    if (m_incomplete && position != m_requestedPosition) {
        ask(view, range, QStringLiteral("again"));
    }
    return CodeCompletionModelControllerInterface::filterString(view, range, position);
}

bool CompletionModel::setItems(int64_t req, const QList<Item> &items, bool incomplete)
{
    if (req != m_request) {
        return false; // the user moved on; this answer is for an older request
    }
    m_incomplete = incomplete;
    beginResetModel();
    m_items = items;
    endResetModel();
    if (!m_items.isEmpty() && !m_view->isCompletionActive()) {
        m_answerPending = true;
        m_view->startCompletion(m_word, this);
        m_answerPending = false;
    }
    return true;
}

void CompletionModel::executeCompletionItem(View *view, const Range &word, const QModelIndex &index) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_items.size()) {
        return;
    }
    const Item &item = m_items.at(index.row());
    Document *doc = view->document();
    const QString text = item.insertText.isEmpty() && item.textEdit.isEmpty() ? item.label : item.insertText;
    Range replacement = word;
    if (!item.textEdit.isEmpty()) {
        QString error;
        const auto span = item.textEdit.contains(QStringLiteral("range")) ? item.textEdit.value(QStringLiteral("range"))
                                                                          : item.textEdit.value(QStringLiteral("replace"));
        if (!readRange(span, doc, replacement, error)) {
            return;
        }
    }

    // the word replaced by the item plus the item's additional edits, all against the text as it is now
    std::vector<PlannedEdit> edits{{replacement, item.snippet ? QString() : text}};
    for (const QJsonValue &v : item.additionalEdits) {
        Range r;
        QString error;
        if (!readRange(v.toObject().value(QStringLiteral("range")), doc, r, error)) {
            continue; // an invalid extra edit must not block the main insertion
        }
        if (r.start() < replacement.end() && replacement.start() < r.end()) {
            continue;
        }
        edits.push_back({r, v.toObject().value(QStringLiteral("newText")).toString()});
    }
    std::stable_sort(edits.begin(), edits.end(), [](const PlannedEdit &a, const PlannedEdit &b) {
        return a.range.start() == b.range.start() ? a.range.end() < b.range.end() : a.range.start() < b.range.start();
    });
    for (size_t i = 1; i < edits.size(); ++i) {
        if (edits[i].range.start() < edits[i - 1].range.end()) {
            edits.erase(edits.begin() + long(i)); // overlapping extra edit: drop it
            --i;
        }
    }
    // A moving cursor at the word start rides along with earlier extra edits and, moving on insert,
    // ends up behind the inserted text.
    std::unique_ptr<MovingCursor> end(doc->newMovingCursor(replacement.start(), MovingCursor::MoveOnInsert));
    {
        Document::EditingTransaction transaction(doc);
        for (auto it = edits.rbegin(); it != edits.rend(); ++it) {
            doc->replaceText(it->range, it->text);
        }
        if (item.snippet) {
            // Translate LSP tab stops/defaults to KTextEditor's editable template fields.
            QString templ = text;
            const QRegularExpression stop(QStringLiteral(R"(\$\{(\d+)(?::([^}]*))?\}|\$(\d+))"));
            auto matches = stop.globalMatch(text);
            QList<QRegularExpressionMatch> found;
            while (matches.hasNext()) {
                found.prepend(matches.next());
            }
            for (const auto &match : found) {
                const auto number = match.captured(1).isEmpty() ? match.captured(3) : match.captured(1);
                QString field;
                if (number == QStringLiteral("0")) {
                    field = QStringLiteral("${cursor}");
                } else {
                    field = QStringLiteral("${arg%1").arg(number);
                    const auto quoted = QJsonDocument(QJsonArray{match.captured(2)}).toJson(QJsonDocument::Compact);
                    field += QLatin1Char('=') + QString::fromUtf8(quoted.mid(1, quoted.size() - 2));
                    field += QLatin1Char('}');
                }
                templ.replace(match.capturedStart(), match.capturedLength(), field);
            }
            view->insertTemplate(end->toCursor(), templ);
        }
    }
    if (!item.snippet) {
        view->setCursorPosition(end->toCursor());
    }
    QJsonObject f;
    f.insert(QStringLiteral("label"), item.label);
    m_bridge.emitEvent(QStringLiteral("editor.completionAccepted"), f, m_viewId);
}

QJsonArray CompletionModel::labels() const
{
    QJsonArray result;
    for (const auto &item : m_items) {
        result.append(item.label);
    }
    return result;
}

QModelIndex CompletionModel::index(int row, int column, const QModelIndex &parent) const
{
    if (parent.isValid() || row < 0 || row >= m_items.size() || column < 0 || column >= ColumnCount) {
        return QModelIndex();
    }
    return createIndex(row, column);
}

int CompletionModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_items.size());
}

QVariant CompletionModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_items.size()) {
        return QVariant();
    }
    const Item &item = m_items.at(index.row());
    switch (role) {
    case Qt::DisplayRole:
        if (index.column() == Name) {
            return item.label;
        }
        if (index.column() == Arguments) {
            return item.detail;
        }
        return QVariant();
    case Qt::DecorationRole:
        return index.column() == Icon ? QVariant(QIcon::fromTheme(iconName(item.kind))) : QVariant();
#if KTEXTEDITOR_ENABLE_DEPRECATED_SINCE(6, 19)
    case IsExpandable:
        return !item.documentation.isEmpty();
#endif
    case ExpandingWidget:
        return item.documentation;
    case Qt::ToolTipRole:
        return item.documentation.isEmpty() ? QVariant() : QVariant(item.documentation);
    default:
        return QVariant();
    }
}
} // namespace tkde::editor
