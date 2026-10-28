# The IDE core (`tinox.ide.app`)

How the application is put together, and the rules every feature module follows. The pieces are in
`src/tinox/ide/app/`; `IdeApp.start()` creates and connects them.

## Module layout

```
src/Main.tnx                  the entry point: IdeApp::main()
src/tinox/ide/kde/            the bindings to the bridge (Bridge, Command, Event, EventLoop, App)   [bindings.md]
src/tinox/ide/ui/             typed widgets, actions, models, editor wrappers                      [ui-api.md]
src/tinox/ide/app/            this document: bus, store, commands, logging, errors, main window, IdeApp
src/tinox/ide/settings/       typed access to the user's KConfig (Settings); the preferences dialog (#22) joins it
src/tinox/ide/workspace/      (#20) project folder, tinox.toml model, file tree, file operations
src/tinox/ide/editor/         (#21) open documents, tabs, dirty tracking, session restore
src/tinox/ide/lsp/            (#24-#29) language server client and its features
src/tinox/ide/build/          (#30) build, run, check, test, output
src/tinox/ide/examples/       runnable examples of the UI API
```

Dependencies point downwards: `app` uses `ui` and `kde`; feature modules use `app` (registry, store,
bus), `ui` and `kde`; **feature modules do not call each other**: they publish on the bus and
read the store.

## The parts

| Class | Role |
|---|---|
| `EventBus` | In-process publish/subscribe by topic (`"*"` for all). A subscriber that throws is reported, the others still run. `attach(events)` makes messages from background tasks arrive as bus messages. |
| `Background` | What a `spawn`ed task calls to talk to the IDE: `Background::report(topic, json)`. It goes through the bridge's thread-safe queue (`core.message`), so no Tinox object is shared between threads. |
| `Store` | The one application state: named text values (`getBool`, `getInt`, `toggle`). `set` announces `state.<key>` and `state.changed` on the bus, only when the value changed. |
| `IdeCommand`, `CommandRegistry` | Every command: id, title, icon, default shortcut, category, enabled predicate, handler. Registering mirrors it to the bridge as an action. Menus, shortcuts and the palette all run commands through `registry.run()`. |
| `Logger` | Levels (`TINOXIDE_LOG`), a ring of recent lines, `log.line` on the bus (the Output panel), warnings/errors forwarded to the Qt log. |
| `ErrorReporter` | Unhandled errors (command handler, event handler, bus subscriber, the logic task): logged, shown in the status line and as a desktop toast (`internalError` in `tinoxide.notifyrc`). |
| `MainWindow` | The window as a function of the store: menu bar, tool bar, project dock, problems/output dock, editor tabs, status bar. |
| `IdeApp` | Wires all of it, registers the core commands (Quit, the two view toggles), persists the layout flags in the settings and the window layout through `Mount.saveState`. |

## Unidirectional flow

```
user action ──▶ command ──▶ store.set(...) ──▶ "state.changed" on the bus ──▶ MainWindow.render()
     ▲                                                                              │
     └──────────────────────────── widgets (diffed by the bridge) ◀─────────────────┘
```

* Things that change state are commands, event handlers and background results. They write the **store**.
* The UI never keeps its own copy: it describes the window from the store and the bridge changes only what
  differs. A dock the user closes with its close button reports `widget.dockVisibilityChanged`; the handler
  writes `ui.projectVisible`; the render that follows is a no-op for that dock.
* Availability of commands follows the same path: the registry re-evaluates the predicates on every state
  change and enables or disables the matching actions (only when the answer changed).

## Writing a feature module

```tinox
class WorkspaceModule
{
    fnc install(app: IdeApp) -> Nothing
    {
        app.registry.register(
            IdeCommand::create("workspace.close", "&Close Folder").category("File")
                .enabledWhen((s: Store) => s.getBool("workspace.open"))
                .run((c: CommandContext) => app.store.setBool("workspace.open", false)))
        app.bus.subscribe("workspace.opened", (m: BusMessage) => app.logger.info("opened " + m.str("path")))
    }
}
```

* Register commands before `registry.attach()` runs, or later (they are defined on the fly).
* Report failures by throwing from a handler: the registry, the bus and the event loop all route them
  to `ErrorReporter`. Do not print.
* Long work belongs on a `spawn`ed task that ends with `Background::report("topic", json)`. The task must not
  touch the store, the registry or any other object of the logic task.

## Failure handling

* Errors in command handlers, event handlers and bus subscribers do not stop the IDE: they become a log line,
  a status message and (once per distinct message in a row) a toast.
* The logic task failing reports through the same path and then ends the Qt loop.
* A crash of the process itself is below Tinox: the bridge installs KDE's crash handler (KCrash/DrKonqi)
  unless `TKDE_NO_KCRASH` is set.

## Testing

Everything above is tested against the mock bridge (`tests/EventBusTest`, `StoreTest`, `CommandRegistryTest`,
`LoggerTest`, `ErrorReporterTest`, `SettingsTest`, `IdeAppTest`). `scripts/test-example.sh` (part of `make test`)
starts the real IDE on the real bridge, headless, with `TINOXIDE_SMOKE=1`: it builds the window and docks,
triggers its own Quit command through the action layer, saves the layout to `tinoxiderc` and must exit 0
without error output.
