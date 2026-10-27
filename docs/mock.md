# libtinoxkde-mock

A headless implementation of the whole bridge ABI ([abi.md](abi.md)): no Qt, no
KDE, no display. The Tinox side of the IDE (bindings, core, LSP client) is built
and tested against it, in CI and on any machine; the real bridge is only needed
to see pixels. `bridge/mock/mock.cpp` is about 700 lines of C++17 and depends on
libgc only (strings it returns are GC allocations, like the real bridge's).

## Behaviour

It follows the contract for everything in the `core` namespace: `core.ping`
(echo), `core.capabilities`, `core.release`, `core.quit`, `core.log`; events
get strictly increasing `seq` and a `t_ns` stamp; the event queue is bounded
with a visible `core.overflow` marker; `tkde_exec` blocks until quit and emits
`core.ready` / `core.quit` / `core.shutdown`; use after shutdown returns
`TKDE_E_NOT_INITIALIZED`. Commands run synchronously in the caller's thread, so
the mock behaves like a UI thread that is never busy. Feature namespaces
(`widget`, `editor`, ...) do not exist in the mock; tests script them.

## Scripting API

`bridge/mock/tinoxkde_mock.h` (not part of the ABI):

| Function | Use |
|---|---|
| `mock_reset()` | Fresh state between tests |
| `mock_push_event(json)` | Inject an event as if the UI produced it (`seq`/`t_ns` added) |
| `mock_next_command(timeout_ms)` | Read the commands the code under test posted, in order |
| `mock_pending_commands()` | How many are waiting |
| `mock_on_command(cmd, reply, event)` | Answer `cmd` with `{"ok":true, ...reply}` and push `event` afterwards |
| `mock_create_object()` | Make an id that `core.release` accepts |
| `mock_add_namespace(name)` | Advertise a namespace in `core.capabilities` |
| `mock_dropped_events()` | Overflow statistics |
| `mock_close_last_window()` | End `tkde_exec` as if the last window was closed |

## Record and replay

```sh
TKDE_MOCK_RECORD=/tmp/session.jsonl ./app     # every command, query, reply and event, one JSON object per line
TKDE_MOCK_REPLAY=/tmp/session.jsonl ./app     # delivers the recorded events verbatim; nothing else is generated
```

A transcript recorded from the real bridge replays against the mock, which
makes recorded UI sessions usable as deterministic test input.

## Using it from Tinox

Link `tinoxkde-mock` instead of `tinoxkde` in a manifest's `[native]` section:

```toml
[native]
libs     = ["tinoxkde-mock"]
lib_dirs = ["bridge/build/bin"]
rpath    = ["bridge/build/bin"]
```

The scripting functions are ordinary `extern fn`s in the tests. The bindings'
test suite (issue #17) uses exactly this.

## Tests

`bridge/mock/tests/mock_test.cpp` (run by `ctest`): lifecycle and error codes,
the core commands, ordering, ids and release, scripted replies, overflow
accounting, shutdown, record and replay. It is also clean under
AddressSanitizer, UBSan and ThreadSanitizer.
