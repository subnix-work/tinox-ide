#include "semantics.h"
#include "../bridge.h"
#include "editor.h"
#include "positions.h"
#include "tinoxkde.h"
#include <KSyntaxHighlighting/Theme>
#include <KTextEditor/Attribute>
#include <KTextEditor/View>
#include <QJsonArray>
#include <QRegularExpression>
#include <limits>

namespace tkde::editor
{
using namespace KTextEditor;
using Theme = KSyntaxHighlighting::Theme;
namespace
{
Theme::TextStyle role(const QString &type)
{
    if (type == QLatin1String("class") || type == QLatin1String("type") || type == QLatin1String("enum") ||
        type == QLatin1String("interface") || type == QLatin1String("struct") || type == QLatin1String("typeParameter"))
        return Theme::DataType;
    if (type == QLatin1String("method") || type == QLatin1String("function"))
        return Theme::Function;
    if (type == QLatin1String("property") || type == QLatin1String("decorator"))
        return Theme::Attribute;
    if (type == QLatin1String("enumMember"))
        return Theme::Constant;
    if (type == QLatin1String("namespace"))
        return Theme::Import;
    if (type == QLatin1String("parameter"))
        return Theme::Variable;
    if (type == QLatin1String("keyword") || type == QLatin1String("modifier"))
        return Theme::Keyword;
    if (type == QLatin1String("comment"))
        return Theme::Comment;
    if (type == QLatin1String("string") || type == QLatin1String("regexp"))
        return Theme::String;
    if (type == QLatin1String("number"))
        return Theme::DecVal;
    if (type == QLatin1String("operator"))
        return Theme::Operator;
    if (type == QLatin1String("macro"))
        return Theme::Preprocessor;
    if (type == QLatin1String("event"))
        return Theme::Function;
    return Theme::Normal;
}
} // namespace
Semantics::Semantics(Document *doc) : QObject(doc), m_doc(doc)
{
    setObjectName(QStringLiteral("semanticOverlay"));
    connect(doc, &Document::aboutToInvalidateMovingInterfaceContent, this, [this] { clear(); });
    connect(doc, &Document::viewCreated, this, [this](Document *, View *view) {
        connect(view, &View::configChanged, this, [this] { refresh(); });
        refresh();
    });
}
void Semantics::clear()
{
    for (const auto &item : std::as_const(m_items))
        delete item.range;
    m_items.clear();
}
bool Semantics::set(const QJsonObject &cmd, QString &error)
{
    const auto data = cmd.value(QStringLiteral("data"));
    const auto legendValue = cmd.value(QStringLiteral("legend"));
    if (!data.isArray() || !legendValue.isArray() || data.toArray().size() % 5) {
        error = QStringLiteral("semantic data must contain five-integer tokens and a legend");
        return false;
    }
    const auto legend = legendValue.toArray();
    for (const auto &type : legend)
        if (!type.isString()) {
            error = QStringLiteral("invalid semantic legend");
            return false;
        }
    const auto colorValue = cmd.value(QStringLiteral("colors"));
    if (!colorValue.isUndefined() && !colorValue.isObject()) {
        error = QStringLiteral("semantic colors must be an object");
        return false;
    }
    const auto colors = colorValue.toObject();
    static const QRegularExpression hex(QStringLiteral("^#[0-9a-fA-F]{6}$"));
    for (auto it = colors.begin(); it != colors.end(); ++it) {
        if (!it.value().isString() || !hex.match(it.value().toString()).hasMatch()) {
            error = QStringLiteral("semantic colors must be #RRGGBB");
            return false;
        }
    }
    QList<QPair<Range, QString>> parsed;
    const auto values = data.toArray();
    qint64 line = 0, column = 0, previousEnd = 0;
    for (qsizetype i = 0; i < values.size(); i += 5) {
        qint64 n[5];
        for (int j = 0; j < 5; ++j) {
            const auto value = values[i + j];
            if (!value.isDouble() || value.toDouble() < 0 || value.toDouble() > 2147483647 ||
                value.toDouble() != double(value.toInteger())) {
                error = QStringLiteral("invalid semantic integer");
                return false;
            }
            n[j] = value.toInteger();
        }
        line += n[0];
        column = n[0] ? n[1] : column + n[1];
        if (!n[2] || n[3] >= legend.size() || line > std::numeric_limits<int>::max() || column + n[2] > std::numeric_limits<int>::max() ||
            (!n[0] && column < previousEnd)) {
            error = QStringLiteral("invalid or overlapping semantic token");
            return false;
        }
        const Range range(int(line), int(column), int(line), int(column + n[2]));
        Range checked;
        if (!readRange(toJson(range), m_doc, checked, error))
            return false;
        const auto text = m_doc->line(int(line));
        for (const auto boundary : {column, column + n[2]}) {
            if (boundary > 0 && boundary < text.size() && text[int(boundary)].isLowSurrogate() &&
                text[int(boundary) - 1].isHighSurrogate()) {
                error = QStringLiteral("semantic token splits a UTF-16 surrogate pair");
                return false;
            }
        }
        parsed.append({range, legend[n[3]].toString()});
        previousEnd = column + n[2];
    }
    // Build the replacement before dropping the old ranges; Qt paints only after this command.
    QList<Item> replacement;
    for (const auto &token : std::as_const(parsed)) {
        auto *range = m_doc->newMovingRange(token.first, MovingRange::DoNotExpand);
        range->setZDepth(-10); // diagnostics/selection keep their own formatting
        replacement.append({range, token.second});
    }
    clear();
    m_items = replacement;
    m_colors = colors;
    refresh();
    return true;
}
void Semantics::refresh()
{
    if (m_doc->views().isEmpty())
        return;
    const auto theme = m_doc->views().first()->theme();
    for (auto &item : m_items) {
        QColor color = QColor::fromRgba(theme.textColor(role(item.type)));
        if (!color.alpha())
            color = QColor::fromRgba(theme.textColor(Theme::Normal));
        // Some themes give Attribute and DataType the same colour. Keep fields,
        // methods and types distinguishable using another colour from that theme.
        if (item.type == QLatin1String("property") &&
            (color.rgba() == theme.textColor(Theme::DataType) || color.rgba() == theme.textColor(Theme::Function))) {
            for (auto alternative : {Theme::Variable, Theme::Constant, Theme::String, Theme::Normal}) {
                const auto candidate = theme.textColor(alternative);
                if (qAlpha(candidate) && candidate != theme.textColor(Theme::DataType) && candidate != theme.textColor(Theme::Function)) {
                    color = QColor::fromRgba(candidate);
                    break;
                }
            }
        }
        if (m_colors.contains(item.type))
            color = QColor(m_colors.value(item.type).toString());
        Attribute::Ptr attr(new Attribute);
        attr->setForeground(color);
        item.range->setAttribute(attr);
    }
}
QJsonArray Semantics::items() const
{
    QJsonArray result;
    for (const auto &item : m_items)
        result.append(QJsonObject{
            {QStringLiteral("type"), item.type},
            {QStringLiteral("range"), toJson(item.range->toRange())},
            {QStringLiteral("color"), item.range->attribute() ? item.range->attribute()->foreground().color().name() : QString()}});
    return result;
}
void registerSemanticCommands(Bridge &bridge)
{
    bridge.registerCommand(QStringLiteral("editor.setSemanticTokens"), [&bridge](const QJsonObject &cmd) {
        auto *doc = qobject_cast<Document *>(bridge.object(cmd.value(QStringLiteral("target")).toInteger()));
        if (!doc)
            return Reply::failure(TKDE_E_INVALID_ARG, QStringLiteral("target must be a document"));
        const auto version = cmd.value(QStringLiteral("version"));
        if (!version.isDouble() || version.toDouble() < 0 || version.toDouble() != double(version.toInteger()))
            return Reply::failure(TKDE_E_INVALID_ARG, QStringLiteral("version must be a nonnegative integer"));
        if (version.toInteger() != documentVersion(doc))
            return Reply::failure(TKDE_E_FAILED, QStringLiteral("document changed before semantic tokens could be applied"));
        auto *overlay = doc->findChild<Semantics *>(QStringLiteral("semanticOverlay"));
        QString error;
        if (!overlay->set(cmd, error))
            return Reply::failure(TKDE_E_INVALID_ARG, error);
        return Reply::success(QJsonObject{{QStringLiteral("count"), overlay->items().size()}});
    });
    if (bridge.debugCommandsEnabled())
        bridge.registerCommand(QStringLiteral("editor.debug.semanticTokens"), [&bridge](const QJsonObject &cmd) {
            auto *doc = qobject_cast<Document *>(bridge.object(cmd.value(QStringLiteral("target")).toInteger()));
            if (!doc)
                return Reply::failure(TKDE_E_INVALID_ARG, QStringLiteral("target must be a document"));
            return Reply::success(
                QJsonObject{{QStringLiteral("items"), doc->findChild<Semantics *>(QStringLiteral("semanticOverlay"))->items()}});
        });
}
} // namespace tkde::editor
