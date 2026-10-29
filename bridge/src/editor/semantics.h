#ifndef TINOXKDE_EDITOR_SEMANTICS_H
#define TINOXKDE_EDITOR_SEMANTICS_H

#include <KTextEditor/Document>
#include <KTextEditor/MovingRange>
#include <QJsonObject>
#include <QObject>

namespace tkde
{
class Bridge;
namespace editor
{
// Document-owned semantic overlay. Ranges survive edits until the next validated snapshot.
class Semantics : public QObject
{
    Q_OBJECT
public:
    explicit Semantics(KTextEditor::Document *doc);
    void clear();
    bool set(const QJsonObject &cmd, QString &error);
    void refresh();
    QJsonArray items() const;

private:
    struct Item {
        KTextEditor::MovingRange *range;
        QString type;
    };
    KTextEditor::Document *m_doc;
    QList<Item> m_items;
    QJsonObject m_colors;
};
void registerSemanticCommands(Bridge &bridge);
} // namespace editor
} // namespace tkde
#endif
