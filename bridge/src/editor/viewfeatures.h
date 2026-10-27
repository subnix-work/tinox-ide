// What a view adds to a document: cursor, selection and focus events, hover tooltips
// and code completion fed by the host asynchronously.
#ifndef TINOXKDE_EDITOR_VIEWFEATURES_H
#define TINOXKDE_EDITOR_VIEWFEATURES_H

#include <KTextEditor/View>

#include <cstdint>

namespace tkde
{
class Bridge;
namespace editor
{
/// Called for every new view: wires its events and the hover provider.
void installViewFeatures(Bridge &bridge, KTextEditor::View *view, int64_t id);
/// editor.enableCompletion, editor.setCompletions, editor.showTooltip, editor.completionState.
void registerViewFeatureCommands(Bridge &bridge);
} // namespace editor
} // namespace tkde

#endif // TINOXKDE_EDITOR_VIEWFEATURES_H
