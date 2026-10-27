// Positions and ranges of the `editor` namespace (docs/abi.md): LSP shaped,
// {"line": 0-based, "character": UTF-16 code units}. KTextEditor columns are
// QString indices, i.e. also UTF-16, so the two coincide: no conversion.
#ifndef TINOXKDE_EDITOR_POSITIONS_H
#define TINOXKDE_EDITOR_POSITIONS_H

#include <KTextEditor/Cursor>
#include <KTextEditor/Document>
#include <KTextEditor/Range>

#include <QJsonObject>
#include <QJsonValue>
#include <QString>

namespace tkde::editor
{
inline QJsonObject toJson(KTextEditor::Cursor c)
{
    QJsonObject o;
    o.insert(QStringLiteral("line"), c.line());
    o.insert(QStringLiteral("character"), c.column());
    return o;
}

inline QJsonObject toJson(KTextEditor::Range r)
{
    QJsonObject o;
    o.insert(QStringLiteral("start"), toJson(r.start()));
    o.insert(QStringLiteral("end"), toJson(r.end()));
    return o;
}

/// Strict: the position must exist in `doc` (line < line count, character <= line length).
/// A bad position is an error, never silently clamped.
inline bool readPosition(const QJsonValue &v, KTextEditor::Document *doc, KTextEditor::Cursor &out, QString &error)
{
    if (!v.isObject()) {
        error = QStringLiteral("a position is {\"line\": n, \"character\": n}");
        return false;
    }
    const QJsonObject o = v.toObject();
    const QJsonValue l = o.value(QStringLiteral("line"));
    const QJsonValue c = o.value(QStringLiteral("character"));
    if (!l.isDouble() || !c.isDouble() || l.toDouble() != int(l.toDouble()) || c.toDouble() != int(c.toDouble())) {
        error = QStringLiteral("line and character must be integers");
        return false;
    }
    const int line = int(l.toDouble());
    const int character = int(c.toDouble());
    if (line < 0 || line >= doc->lines()) {
        error = QStringLiteral("line %1 is outside the document (%2 lines)").arg(line).arg(doc->lines());
        return false;
    }
    if (character < 0 || character > doc->lineLength(line)) {
        error = QStringLiteral("character %1 is outside line %2 (length %3)").arg(character).arg(line).arg(doc->lineLength(line));
        return false;
    }
    out = KTextEditor::Cursor(line, character);
    return true;
}

inline bool readRange(const QJsonValue &v, KTextEditor::Document *doc, KTextEditor::Range &out, QString &error)
{
    if (!v.isObject()) {
        error = QStringLiteral("a range is {\"start\": position, \"end\": position}");
        return false;
    }
    KTextEditor::Cursor start;
    KTextEditor::Cursor end;
    if (!readPosition(v.toObject().value(QStringLiteral("start")), doc, start, error) ||
        !readPosition(v.toObject().value(QStringLiteral("end")), doc, end, error)) {
        return false;
    }
    if (end < start) {
        error = QStringLiteral("the end of a range is before its start");
        return false;
    }
    out = KTextEditor::Range(start, end);
    return true;
}
} // namespace tkde::editor

#endif // TINOXKDE_EDITOR_POSITIONS_H
