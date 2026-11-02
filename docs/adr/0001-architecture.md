# ADR 0001: Architecture of the Tinox IDE

Status: accepted (confirmed by the spike, see [spike findings](0001-spike-findings.md))
Date: 2026-10-27
Issue: #2 (spike #7)

## Context

Goal: a Linux IDE for the Tinox language built from real KDE Plasma components
(Breeze look, KDE shortcuts and dialogs, the Kate editor engine), and written in
Tinox itself.

Constraints:

* Tinox compiles to native code and can call **C** functions through
  `extern fn`; it cannot call C++. Qt and KDE Frameworks are C++.
* Tinox functions are not C function pointers, so C code cannot call back into
  Tinox (subnix-work/tinox#488: evaluated, deferred).
* Tinox runs on the Boehm GC. A thread the runtime did not create must attach
  with `tinox_thread_attach()`, and pointers into GC memory must not live only
  in memory the collector does not scan (C++ objects, `malloc`).
* Qt requires its event loop on the process main thread.

## Decisions

### 1. UI technology: Qt 6 Widgets with KDE Frameworks 6

Widgets plus KXmlGui, KTextEditor, KConfig, KIO, KNotifications and
KSyntaxHighlighting. Reasons:

* **KTextEditor is a QWidget component.** It is the editor engine of Kate and
  KDevelop (highlighting, folding, multi-cursor, vi mode, search, minimap,
  accessibility). Writing an editor is years of work; embedding it is a few
  lines (the spike does it).
* A multi-pane desktop IDE (docks, tabs, trees, toolbars, dialogs) is exactly
  what Widgets and KXmlGui are for. Kirigami targets convergent/mobile apps;
  embedding the QWidget editor into QML would be the awkward part of the stack.

### 2. The bridge: a thin C ABI over Qt and KDE

`libtinoxkde` (C++, CMake + ECM) is the only place that touches Qt. It exports
`extern "C"` functions (`bridge/include/tinoxkde.h`, specified in `docs/abi.md`),
all using primitive types and UTF-8 strings.

Style: a **declarative widget tree** (Tinox describes the UI as data, like
`tinox.core.ui`; the C++ side builds and diffs QWidgets) for the chrome, plus
**specialised handle-based objects** for what needs real APIs: the editor
(KTextEditor documents and views), item models, actions and dialogs.

Objects are addressed by opaque `Int64` ids owned by the bridge.

### 3. Threading: Qt on the main thread, Tinox logic on a spawned task

```
 Tinox main thread                  Tinox logic task (spawned)
 ───────────────────                ──────────────────────────
 tkde_init()                        loop:
 spawn logic task ───────────────►    ev = tkde_next_event(timeout)   ◄── event queue ──┐
 tkde_exec()  ◄─ blocks, owns Qt      handle(ev) ... tkde_post(json)  ── command queue ─┼─►
        │                                                                                │
        └── Qt event loop, widgets, KTextEditor; signals push events ────────────────────┘
```

* **Commands** (Tinox to UI): `tkde_post(json)` copies the string and queues it
  with `QMetaObject::invokeMethod(..., Qt::QueuedConnection)`; it runs on the
  UI thread.
* **Events** (UI to Tinox): Qt signals push JSON onto a mutex-protected queue;
  `tkde_next_event(timeout)` blocks the logic task and returns a GC-allocated
  copy.
* **No callbacks into Tinox.** Nothing in C++ ever calls Tinox code, so no
  foreign thread needs GC registration and no GC pointer is stored on the C++
  side.
* **Synchronous queries** (e.g. "current selection") are a command whose reply
  arrives as a correlated event; a blocking `tkde_query` may be added later.
* The main thread is blocked inside `exec()`. This works with the collector:
  it suspends the thread by signal and scans its stack (verified under load in
  the spike).

### 4. Process model

The IDE is one process. It spawns `tinox-lsp` (LSP over stdio) and
`tinox build/run/test --message-format=json` as children through
`tinox.core.process` (pipe mode, own process group, `waitReadable` for stdout +
stderr). A debugger runs as `tinox dap` (gdb's DAP server).

### 5. Dependencies

* Qt >= 6.6, KDE Frameworks >= 6.5, ECM >= 6.5, CMake >= 3.20 (checked by
  `scripts/check-env.sh`), `tinox` >= 2.9.0, libgc (Boehm GC).
* Licensing: the IDE and bridge are MIT OR Apache-2.0. Qt and KDE Frameworks
  are LGPL; they are linked dynamically only. Qt modules that are GPL-only
  (for example Qt Charts) are not used.

## Alternatives rejected

| Alternative | Why not |
|---|---|
| **Web UI in a QtWebEngine shell** (`tinox.core.ui` over WebSocket) | Fastest to build, but not native Plasma widgets, which is a hard requirement. |
| **UI written in C++, Tinox as a backend process** | Robust, but the IDE would not be written in Tinox. |
| **A Kate plugin / Kate configuration with `tinox-lsp`** | A day of work and a good fallback for users, but not an IDE of its own. |
| **Qt Quick + Kirigami** | Wrong fit for a docked desktop IDE; the editor is a QWidget. |
| **C callbacks into Tinox** | Needs exported C-ABI thunks and foreign-thread GC attach (#488); a polled queue achieves the same without it. |
| **Hand-written Qt bindings per class** | Hundreds of functions to keep in sync; the declarative tree plus a few specialised objects keeps the ABI small. |

## Consequences and open risks

| Risk | Mitigation | Owner |
|---|---|---|
| Per-event JSON parsing cost | Measured in the spike: round trips in microseconds; optimise only if profiling says so | bridge |
| Logic task blocked on a long operation delays event handling | Keep handlers short; run builds and searches as separate tasks | core |
| GC pointers captured by Qt objects | Rule: the bridge copies strings in, returns GC copies out, uses ids not pointers | bridge review |
| ABI drift between bridge and bindings | `docs/abi.md`, additive-only after v1, contract tests against a mock bridge (#16) | abi |
| Wayland/X11 differences (focus, clipboard, IME) | Spike runs on both; CI uses offscreen | ci |
| Single-threaded Tinox logic is a bottleneck | Tasks are real pthreads: split work across tasks, post commands from any thread | core |
