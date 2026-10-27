// Diagnostics shown in an editor: wavy underlines (MovingRanges that follow edits),
// gutter icons and hover text. Owned per document; several owners (the language
// server, the build) can publish independently.
#ifndef TINOXKDE_EDITOR_DIAGNOSTICS_H
#define TINOXKDE_EDITOR_DIAGNOSTICS_H

#include <KTextEditor/Attribute>
#include <KTextEditor/Document>
#include <KTextEditor/MovingRange>

#include <QColor>
#include <QJsonArray>
#include <QMap>
#include <QObject>
#include <QPalette>
#include <QString>

namespace tkde
{
class Bridge;
namespace editor
{
/// Severities follow LSP: 1 error, 2 warning, 3 information, 4 hint.
QColor severityColor(int severity, const QPalette &palette);

class Diagnostics : public QObject
{
    Q_OBJECT
public:
    struct Item {
        KTextEditor::MovingRange *range = nullptr;
        KTextEditor::Range pendingRange; // only while a list is being validated
        int severity = 1;
        QString message;
        QString code;
        QString source;
    };

    Diagnostics(KTextEditor::Document *doc, QObject *parent);
    ~Diagnostics() override;

    /// Replaces the diagnostics of `owner`. All or nothing: a bad item rejects the whole list.
    bool set(const QString &owner, const QJsonArray &items, QString &error);
    QJsonArray toJson(const QString &owner = QString()) const;
    /// Messages (rich text) of every diagnostic covering `position`.
    QString hintAt(KTextEditor::Cursor position) const;
    int count() const;
    /// Recomputes the colours, e.g. after a palette change.
    void refreshStyles();
    /// Frees every range and its marks. Needs the document alive: call it before releasing it.
    void clearAll();

    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void clear(const QString &owner);
    void refreshMarks();
    KTextEditor::Attribute::Ptr attributeFor(int severity) const;

    KTextEditor::Document *m_doc;
    QMap<QString, QList<Item>> m_items;
};

/// Mark descriptions and icons for the gutter; call once per document.
void installMarkDefaults(KTextEditor::Document *doc);

void registerDiagnosticCommands(Bridge &bridge);
} // namespace editor
} // namespace tkde

#endif // TINOXKDE_EDITOR_DIAGNOSTICS_H
