// The `widget` namespace: a declarative widget tree (docs/widget.md).
#ifndef TINOXKDE_WIDGET_H
#define TINOXKDE_WIDGET_H

namespace tkde
{
class Bridge;
namespace widget
{
/// Registers every `widget.*` command and adds the namespace to core.capabilities. UI thread.
void registerCommands(Bridge &bridge);
} // namespace widget
} // namespace tkde

#endif // TINOXKDE_WIDGET_H
