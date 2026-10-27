# The bridge core

`libtinoxkde` (`bridge/`) is the only code that touches Qt and KDE Frameworks.
This page describes how its core is built; the contract it implements is
[abi.md](abi.md), its headless twin is [mock.md](mock.md).

## Layout

| File | Role |
|---|---|
| `include/tinoxkde.h` | The public C header (the ABI) |
| `src/abi.cpp` | The `extern "C"` functions: argument checks and GC string ownership only |
| `src/bridge.h`, `src/bridge.cpp` | `tkde::Bridge`: lifecycle, queues, object registry, command dispatch, the `core` namespace |
| `common/event_queue.h` | The bounded event queue, shared with the mock so both behave identically |

Feature namespaces (`widget`, `editor`, ...) plug in with
`Bridge::registerCommand("ns.verb", handler)` and `Bridge::emitEvent(...)`; they
do not touch the queues or the threading directly.

## How a call travels

* **`tkde_post(json)`** (any thread) parses and validates the command on the
  caller's thread (`TKDE_E_INVALID_ARG` on the spot), then queues a call to the
  UI thread with `QMetaObject::invokeMethod(..., Qt::QueuedConnection)`. The UI
  thread dispatches to the registered handler and, if the command carried a
  `req`, emits a `core.reply` event (a failure without `req` emits `core.error`).
  Qt delivers queued calls in posting order.
* **`tkde_query(json)`** does the same but waits for the answer on the calling
  thread. It checks every 50 ms whether the loop has ended and then fails with
  `TKDE_E_NOT_INITIALIZED` instead of hanging. Called on the UI thread it
  returns `TKDE_E_WRONG_THREAD`.
* **Events** are produced on any thread with `Bridge::emitEvent`. The queue
  assigns `seq` under its lock, so delivery order is `seq` order. The envelope
  is written by hand: `seq` and `t_ns` stay exact integers (a `QJsonObject`
  stores numbers as doubles).
* **`tkde_next_event(timeout)`** pops from the queue and returns a GC-allocated
  copy of the JSON. After `core.shutdown` the queue is closed and it returns `""`
  immediately.

## Objects

`registerObject(QObject*)` assigns the next id and keeps a `QPointer`. The
object's `destroyed` signal removes the entry, so an object deleted from the Qt
side stops resolving (`TKDE_E_NOT_FOUND`) rather than dangling. `tkde_release`
and `core.release` drop the entry and `deleteLater()` the object. Ids are never
reused during a run.

## KDE integration done at `tkde_init`

`KAboutData` (name, version, MIT licence, desktop file name
`org.subnix.tinoxide`), `KLocalizedString` domain `tinox-ide`, `KCrash`
(disable with `TKDE_NO_KCRASH=1`). `QApplication` is created for the whole
process lifetime. If no display is reachable the call fails with a message
instead of letting Qt abort the process: set `WAYLAND_DISPLAY`/`DISPLAY`, or
`QT_QPA_PLATFORM=offscreen` for headless runs.

`core.singleInstance {"enabled": true}` (before `tkde_exec`) registers a unique
`KDBusService`; a second start hands its arguments to the first, which gets a
`core.activated` event. The reply says whether this process is the registered
one.

## Shutdown

The loop ends on `core.quit` / `tkde_quit` or when the last window closes
(`lastWindowClosed` turns into `requestQuit(0)`). Order: `core.quit` event, the
loop exits, `core.shutdown` event, the queue closes, `tkde_exec` returns the
code. Quitting is idempotent.

## Test hooks

With `TKDE_DEBUG_COMMANDS=1` the bridge also registers `core.debug.emit`,
`core.debug.create`, `core.debug.destroy`, `core.debug.objects` and
`core.debug.selfquery` for the tests (floods, object churn, Qt-side destruction,
the wrong-thread query). They are not in `core.capabilities`.

## Tests and tools

| Command | What |
|---|---|
| `make test` | ctest: header (C11 and C++), version, **core** (real bridge, offscreen), **mock** |
| `make test-asan` | the same under AddressSanitizer + UBSan + LeakSanitizer; no suppressions |
| `make test-tsan` | under ThreadSanitizer; `bridge/tests/tsan.supp` documents why Qt's own event hand-off is suppressed and nothing else |
| `make bench` | queue throughput |

`core_test` runs `tkde_exec` on the main thread and the contract checks on a
second thread, as the IDE does. Test threads that call the bridge must attach to
the Boehm GC (`bridge/tests/gc_thread.h`), like Tinox's own tasks.

### Throughput

`make bench`, offscreen, Arch Linux, release build, Qt 6.12:

| | Rate |
|---|---|
| `tkde_post` + wait for the `core.reply` event | about 170,000 per second (5.9 us each) |
| `tkde_query` | about 215,000 per second (4.6 us each) |
| `tkde_post` without waiting | about 780,000 commands per second |
| events from the UI thread to a consumer | about 830,000 per second; a 1,000,000 event burst overflowed the 65,536 slots and the loss was reported as `core.overflow` |

A human-visible action costs microseconds in the bridge, far below a frame.
