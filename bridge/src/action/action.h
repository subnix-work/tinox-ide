// The `action` namespace: KDE actions, menus, toolbars and shortcuts (docs/action.md).
#ifndef TINOXKDE_ACTION_H
#define TINOXKDE_ACTION_H

namespace tkde
{
class Bridge;
namespace action
{
/// Registers every `action.*` command and adds the namespace to core.capabilities. UI thread.
void registerCommands(Bridge &bridge);
} // namespace action
} // namespace tkde

#endif // TINOXKDE_ACTION_H
