# Architecture map

The IDE is Tinox application logic above a C++ Qt/KDE bridge. Qt owns the process
main thread; the Tinox logic task submits commands and consumes events. Long-running
work uses background tasks and queues. No Qt callback enters Tinox.

```text
Tinox feature modules (workspace / editing / LSP / build)
                     ↓
app: commands → store → declarative UI; bus between modules
                     ↓
ui + kde: typed widgets, documents, commands and event loop
                     ↓  additive C ABI / JSON command and event queues
bridge: Qt 6 / KDE Frameworks 6 / KTextEditor on the main thread
```

| Layer | Read | Source / generated API entry points |
|---|---|---|
| Decisions | [ADR 0001](adr/0001-architecture.md), [spike findings](adr/0001-spike-findings.md) | Threading and go/no-go evidence |
| Bridge | [C ABI](abi.md), [bridge core](bridge-core.md), [editor protocol](editor.md) | `bridge/include/tinoxkde.h`, `bridge/src/`; ABI v1 is additive-only |
| Bindings | [Tinox bindings](bindings.md), [typed UI](ui-api.md), [mock](mock.md) | `Bridge`, `Command`, `EventLoop`, `Document`, `EditorView`, `Mount` |
| Core | [IDE core](ide-core.md), [documents](documents.md), [workspace](workspace.md) | `IdeApp`, `MainWindow`, `Store`, `EventBus`, `CommandRegistry`, `Documents` |
| Language tools | [LSP client](lsp-client.md), [diagnostics](diagnostics.md), [navigation](navigation.md), [editing](editing.md), [semantic highlighting](semantic-highlighting.md) | `LanguageService`, `NavigationService`, `EditService`, `SemanticHighlighting` |
| Execution/settings | [build/test](build.md), [terminal](terminal.md), [preferences](settings.md) | `BuildService`, `TestExplorer`, `Preferences` |

Run `make docs`, then open `build/api/index.html` for class/member documentation
extracted by `tinox doc` from `/** ... */` comments. The generated API's project
navigation links to the rendered manual, this map and the bridge/bindings/core
references. `build/docs/index.html` is the combined documentation entry point.
The [documentation index](README.md) is also readable directly in the repository.

## Boundaries to preserve

- Never retain Tinox heap pointers in C++; the collector does not scan C++ objects.
  Copy strings/values into native ownership; return static or `GC_malloc_atomic`
  strings across the ABI.
- Feature modules communicate through the bus/store. UI event handlers and logic
  pollers own application state; reader tasks send results through queues.
- UTF-16 positions are zero-based and ranges are half-open. Validate versions,
  document identity and server generation before applying asynchronous edits.
- Keep user-facing workflows in the manual and implementation contracts in these
  references. Change the source doc comments with public API changes; `make test`
  checks their presence.

See [development](development.md), [testing](testing.md) and
[dogfooding](dogfooding.md) for working on the implementation.
