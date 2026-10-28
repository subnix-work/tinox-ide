// The `editor` namespace: KTextEditor documents and views (docs/abi.md, "editor").
#ifndef TINOXKDE_EDITOR_H
#define TINOXKDE_EDITOR_H

#include <cstdint>

namespace KTextEditor
{
class Document;
}

namespace tkde
{
class Bridge;
namespace editor
{
/// Registers every `editor.*` command and adds the namespace to core.capabilities. UI thread.
void registerCommands(Bridge &bridge);
/// Current editor document version, used to reject stale asynchronous decorations on the UI thread.
int64_t documentVersion(KTextEditor::Document *document);
} // namespace editor
} // namespace tkde

#endif // TINOXKDE_EDITOR_H
