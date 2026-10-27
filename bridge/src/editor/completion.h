// Code completion for a view whose items come from the host (the language server) after
// an asynchronous round trip: completionInvoked() raises `editor.completionRequested`,
// the host answers with `editor.setCompletions`.
#ifndef TINOXKDE_EDITOR_COMPLETION_H
#define TINOXKDE_EDITOR_COMPLETION_H

#include <KTextEditor/CodeCompletionModel>
#include <KTextEditor/CodeCompletionModelControllerInterface>
#include <KTextEditor/Range>

#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QString>

#include <cstdint>

namespace tkde
{
class Bridge;
namespace editor
{
class CompletionModel : public KTextEditor::CodeCompletionModel, public KTextEditor::CodeCompletionModelControllerInterface
{
    Q_OBJECT
    Q_INTERFACES(KTextEditor::CodeCompletionModelControllerInterface)
public:
    struct Item {
        QString label;
        QString detail; // shown right of the label, e.g. a signature
        QString documentation;
        QString kind;               // function, method, class, variable, ...
        QString insertText;         // defaults to the label
        QJsonArray additionalEdits; // LSP-shaped {range, newText}, applied with the item
    };

    CompletionModel(Bridge &bridge, KTextEditor::View *view, int64_t viewId);

    /// Characters that start a completion when typed (e.g. "." and "::").
    void setTriggers(const QStringList &triggers, int minWordLength);
    /// Accepts the answer for request `req`; an answer to an older request is ignored.
    bool setItems(int64_t req, const QList<Item> &items);
    int64_t currentRequest() const { return m_request; }
    int itemCount() const { return m_items.size(); }

    // CodeCompletionModel
    void completionInvoked(KTextEditor::View *view, const KTextEditor::Range &range, InvocationType type) override;
    bool shouldStartCompletion(KTextEditor::View *view, const QString &insertedText, bool userInsertion,
                               const KTextEditor::Cursor &position) override;
    void executeCompletionItem(KTextEditor::View *view, const KTextEditor::Range &word, const QModelIndex &index) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QModelIndex index(int row, int column, const QModelIndex &parent = QModelIndex()) const override;
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;

private:
    Bridge &m_bridge;
    int64_t m_viewId;
    int64_t m_request = 0;
    QStringList m_triggers;
    int m_minWordLength = 2;
    QList<Item> m_items;
    KTextEditor::Range m_word;
    KTextEditor::View *m_view;
    bool m_answerPending = false; // an answer opened the popup itself: do not ask again
};
} // namespace editor
} // namespace tkde

#endif // TINOXKDE_EDITOR_COMPLETION_H
