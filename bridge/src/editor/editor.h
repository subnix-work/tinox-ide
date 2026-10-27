// The `editor` namespace: KTextEditor documents and views (docs/abi.md, "editor").
#ifndef TINOXKDE_EDITOR_H
#define TINOXKDE_EDITOR_H

namespace tkde
{
class Bridge;
namespace editor
{
/// Registers every `editor.*` command and adds the namespace to core.capabilities. UI thread.
void registerCommands(Bridge &bridge);
} // namespace editor
} // namespace tkde

#endif // TINOXKDE_EDITOR_H
