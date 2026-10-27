// The `dialog`, `config` and `notify` namespaces (docs/dialog.md).
#ifndef TINOXKDE_DIALOG_H
#define TINOXKDE_DIALOG_H

namespace tkde
{
class Bridge;
namespace dialog
{
/// Registers `dialog.*` (file, message, input, progress and settings dialogs). UI thread.
void registerCommands(Bridge &bridge);
} // namespace dialog
namespace config
{
/// Registers `config.*` (KConfig by group and key, watcher). UI thread.
void registerCommands(Bridge &bridge);
} // namespace config
namespace notify
{
/// Registers `notify.*` (KNotification). UI thread.
void registerCommands(Bridge &bridge);
} // namespace notify
} // namespace tkde

#endif // TINOXKDE_DIALOG_H
