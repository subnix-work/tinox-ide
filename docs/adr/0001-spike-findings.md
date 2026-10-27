# Spike findings (issue #7)

Question: does the architecture of [ADR 0001](0001-architecture.md) work end to
end: a Qt window and a KTextEditor view on the main thread, Tinox logic on a
spawned task, two queues in between, a Tinox function linked to a C++ library
through `[native]`?

**Result: go.** Nothing in the ADR had to change.

## What was built

* `bridge/src/spike.cpp` (about 190 lines): `QApplication`, a `QMainWindow` with
  a toolbar button and a `KTextEditor` view using the Tinox highlighting, a
  command queue (`tkde_post`, JSON, applied on the UI thread) and an event
  queue (`tkde_next_event`, JSON, GC-allocated copies), `tkde_exec()` blocking
  the main thread.
* `spike/Main.tnx`: `main` initialises Qt, `spawn`s the logic task, and calls
  `tkde_exec()`. The task waits for `ready`, then clicks the button 2000 times
  by command (each round trip timed with the same monotonic clock the bridge
  stamps events with), replaces the editor text, asks for memory use and quits.
  Every round allocates 300 strings on the Tinox side as GC pressure.

The spike was replaced by the real bridge core (issue #8) and removed from the
tree; it is kept at the git tag `spike-v1`. To reproduce: `git checkout spike-v1`,
then `make spike` (offscreen), `make spike-platforms` (virtual KWin on Wayland
plus XWayland on X11, nothing appears on your desktop), or
`TKDE_SPIKE_ROUNDS=15000000 scripts/run-spike.sh` for the soak.

## Measurements

Machine: Arch Linux, Qt 6.12, KDE Frameworks 6.31, tinox 2.8.0, release builds.

| Platform | Startup to window ready | Round trip (avg / min / max) | 2000 rounds |
|---|---|---|---|
| offscreen | 27-33 ms | 5-6 / 3 / 220-570 us | all passed |
| Wayland (virtual KWin) | 32 ms | 6 / 3 / 1.6 ms | all passed |
| X11 (XWayland) | 508 ms (X server connect) | 11 / 3 / 10 ms | all passed |

Startup is measured from `main` to the `ready` event, queued after the first
pass of the event loop, so the window is shown.

**GC soak:** 15,000,000 round trips with 4.5 billion short-lived Tinox strings
allocated alongside, on the offscreen platform, 3 min 46 s: 0 failures,
round trip 5 us on average and 3.5 ms at worst, resident memory 85.9 MB at the
start and 87.1 MB at the end (+1.2 MB, no growth trend).

## What this confirms

* **Linking works.** `[native]` with `lib_dirs` and `rpath` links
  `libtinoxkde.so`, and `extern fn` calls the `extern "C"` functions with
  `Int64`, `String` arguments and `String` results.
* **Threading model holds.** `exec()` blocks the main thread for the whole run
  while the spawned task allocates heavily; the collector stops and scans both
  without deadlock or crash. No Tinox code is ever called from C++, so no
  foreign-thread attach was needed.
* **KTextEditor embeds in a few lines** and honours the Tinox syntax definition
  (`highlightingMode()` reports `Tinox` when the definition is installed).
* **The queues are cheap.** A full cycle (post, queued call on the UI thread,
  signal, event, wake the waiting task) costs microseconds, far below anything
  a user can feel; JSON was not a bottleneck.

## Findings and follow-ups

* **Compiler bug, worked around:** an `extern fn` of a symbol the codegen
  already declares (`tinox_clock_nanos`) produced invalid LLVM IR
  (subnix-work/tinox#504). The spike exports its own clock from the bridge.
* `await` is a keyword: a helper method of that name does not parse.
* Tinox has no JSON module in the always-available tier, so the spike parses
  flat events with `indexOf`/`substring`. The IDE core will depend on
  `tinox.core.json` (extended tier) for events and commands.
* The tail latency is dominated by the scheduler and, on X11, XWayland; the
  worst cases are still orders of magnitude under a frame. Not a concern.
* Not exercised yet and left to the real bridge: window close while the logic
  task is busy (shutdown ordering), more than one document, models, clipboard
  and IME on Wayland.
