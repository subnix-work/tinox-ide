// The `model` namespace: item models fed from Tinox (docs/model.md).
#ifndef TINOXKDE_MODEL_H
#define TINOXKDE_MODEL_H

namespace tkde
{
class Bridge;
namespace model
{
/// Registers every `model.*` command and adds the namespace to core.capabilities. UI thread.
void registerCommands(Bridge &bridge);
} // namespace model
} // namespace tkde

#endif // TINOXKDE_MODEL_H
