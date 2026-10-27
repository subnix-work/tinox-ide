# Tinox bindings to the bridge (`tinox.ide.kde`)

The Tinox side of the [C ABI](abi.md): typed wrappers, a command builder, an
event type and an event loop. Everything above it (the IDE core, LSP client,
views) uses these classes and never calls `tkde_*` directly.
Sources: `src/tinox/ide/kde/`.

| Class | Role |
|---|---|
| `Bridge` | One method per ABI function. Anything that can fail throws a `String` naming the call, the status and the bridge's own explanation, e.g. `tkde_init failed (already initialized): tkde_init was already called`. `requireAbi()` fails clearly on a library of another ABI version. |
| `Command` | Builds a command as JSON text without intermediate objects: `Command::named("editor.setText").target(17).str("text", s).req(42).build()`. Strings are escaped by the runtime's own JSON encoder; `raw()` embeds ready-made JSON. |
| `Event` | A parsed event: `type`, `seq`, `tNs`, `target` are read once; `has/str/int/bool/field` read the payload. Parsing a malformed event throws. |
| `EventLoop` | Dispatches events to handlers registered by type (`onType`) and by object id (`onTarget`); type handlers run first. `run()` is the body of the logic task; `pump(timeoutMs)` handles what is available (tests). |
| `App` | The program entry: `App::run(name, displayName, logic)`. |

## The shape of an IDE `main`

```tinox
class Main
{
    fnc logic(events: EventLoop) -> Int32
    {
        events.onType("core.ready", (ev: Event) => Main::onReady(events))
        events.onTarget(saveButton, (ev: Event) => Main::save())
        events.run()                       // returns after core.shutdown or events.stop()
        return 0
    }

    fnc main() -> Int32
    {
        return App::run("tinox-ide", "Tinox IDE", (events: EventLoop) => Main::logic(events))
    }
}
```

`App::run` checks the ABI version, initialises Qt (main thread), spawns the logic
task, runs Qt's loop on the main thread until quit, and waits for the task. A
failure to initialise, or a logic task that throws, is reported on stderr, ends
the Qt loop and gives a non-zero exit code; nothing hangs.

## Error handling

* **Calls** throw; catch them where a recovery makes sense.
* **Handlers** that throw are caught by the loop: recorded in
  `events.failures`, printed to stderr, and the next event is delivered. An event
  the bridge sent that cannot be parsed is dropped the same way.
* **Commands** fail asynchronously: a posted command without `req` that the
  bridge rejects comes back as a `core.error` event (handle it with
  `events.onType("core.error", ...)`); with `req` it is a `core.reply` with
  `"ok": false`.

## Threads

`Bridge::init` and `Bridge::exec` belong to the process main thread (`App::run`
does both). Everything else, and all handlers, run on the logic task. Handlers
should be short; long work belongs on its own task that posts commands.

## Language notes found while writing it

`loop` and `await` are keywords (not usable as names); a static method is not a
function value (wrap it in a lambda); `x as Int32` converts explicitly; a
lambda cannot assign to captured variables but can mutate captured objects.

## Tests

`make test` runs `tinox test`, which links the headless bridge mock instead of
the real library (`[native.test]` in `tinox.toml`, see [mock.md](mock.md)).
`tests/MockBridge.tnx` wraps the mock's scripting functions: push an event, read
back the commands the code under test posted, script replies. Covered: command
building and escaping, event parsing, every `Bridge` call and error path,
handler order, replacement, removal, exception isolation, malformed events,
shutdown, and `App::run` with a real logic task.
