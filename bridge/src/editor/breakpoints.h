// Debugger marks in the gutter of a document: breakpoints the user toggles with a click, and the
// line the program is stopped at. Marks are KTextEditor marks, so they follow the text through edits.
#ifndef TINOXKDE_EDITOR_BREAKPOINTS_H
#define TINOXKDE_EDITOR_BREAKPOINTS_H

#include <KTextEditor/Document>

#include <QObject>

namespace tkde
{
class Bridge;
namespace editor
{
/// Mark descriptions and icons, and the user's right to toggle breakpoints; call once per document.
void installBreakpointDefaults(KTextEditor::Document *doc);

void registerBreakpointCommands(Bridge &bridge);
} // namespace editor
} // namespace tkde

#endif // TINOXKDE_EDITOR_BREAKPOINTS_H
