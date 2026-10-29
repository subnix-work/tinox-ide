# Testing the bridge

A bug in `libtinoxkde` takes the whole IDE down, so the bridge is tested without a display and
with sanitizers, at four levels. Everything below runs on Qt's `offscreen` platform.

| Command | What it runs |
|---|---|
| `make test-bridge` | the whole bridge suite (ctest, `QT_QPA_PLATFORM=offscreen`) |
| `make test-asan` | the same suite built with ASan + UBSan + LSan (halts on the first finding) |
| `make test-tsan` | the same suite built with TSan (all but `dialog`, see below) |
| `make fuzz` | libFuzzer on the command parser and every command (`FUZZ_SECONDS=600` for a long run) |
| `make test` | everything: the above (except the sanitizer and fuzz builds), the Tinox tests against the mock, syntax and environment checks |

## The suites

* **Per namespace** (`core_test`, `editor_test`, `widget_test`, `action_test`, `model_test`,
  `dialog_test`, `terminal_test`): the main thread runs `tkde_exec`, a second thread plays the Tinox task and talks
  to the library through the C ABI only (`tests/driver.h`: JSON commands, events, `EXPECT`).
  Where a test needs a real user action it goes through Qt like the user would: key presses with
  QTest (`action_test`), `QAbstractItemModelTester` attached to every model (`model_test`),
  golden JSON for widget trees (`widget_test`, `tests/golden/widget`; `UPDATE_GOLDEN=1` rewrites
  them, review the diff).
* **ABI contract** (`contract_test.c`): plain C against `tinoxkde.h` only, the way Tinox uses the
  library. The same program is linked with the real bridge (`contract_real`) and with the mock
  (`contract_mock`); both must pass, because Tinox code is developed against the mock and shipped
  against the real bridge. It covers every promise of `docs/abi.md` for the core namespace:
  lifecycle and thread errors, error codes of malformed commands (rejected at `tkde_post` without
  an event) and unknown commands (an event), ordering and `seq`, unknown and invalid ids, the clock,
  `tkde_getenv`, and shutdown (exit code, `core.quit`, `core.shutdown`, a stream that ends).
  Change the ABI semantics here first.
* **Header** (`header_c.c`, `header_cxx.cpp`): the public header compiles as strict C11 and as C++.
* **Mock** (`mock_test`): the scripting interface of the mock itself.

## The object registry does not leak

`core_test` creates and releases 5000 objects and expects the registry to be empty again, including
objects that Qt destroyed first. Every namespace test ends with the same check for what it created
(`widget_test` mounts and releases 100 windows, `editor_test` 200 documents with diagnostics and
completion state, `model_test` the models with proxies and views attached), and ASan/LSan run all
of them, so an object that is released but not deleted shows up as a leak. When the loop ends,
`tkde_exec` deletes every object still registered while the libraries are alive
(`Bridge::destroyAllObjects`), so a process never leaves objects to Qt's exit handlers.

## Sanitizers

* ASan/UBSan/LSan run everything with no suppressions of our own; `bridge/tests/lsan.supp` lists
  fontconfig's leak while Qt lays out text.
* TSan has documented suppressions in `bridge/tests/tsan.supp`: Qt's queued calls and key events are
  handed over inside uninstrumented Qt libraries. Everything the bridge shares itself (event queue,
  flags, waiter state) uses `std::mutex`/`std::atomic` and has no suppression. The `dialog` test is
  not run under TSan: this TSan runtime aborts in its thread registry when a later thread reuses the
  id of a finished detached Qt helper thread (QFileSystemModel, the paint thread pool).

## The fuzzer

`bridge/fuzz/command_fuzzer.cpp` runs the real bridge offscreen inside the fuzzer process. Each
input is passed to `tkde_post`, the Qt event loop is pumped once so the command runs, the events it
produced are drained, and the windows and dialogs it opened are deleted. Any input is possible:
bytes that are not JSON, JSON that is not a command, and every command of every namespace with
arbitrary fields. The seed corpus (`bridge/fuzz/corpus`, one valid command per namespace
feature plus edge cases) and the dictionary (`bridge/fuzz/command.dict`: all command and field
names) make the fuzzer reach the handlers quickly; the dictionary is generated from the sources,
add new commands to it when a namespace grows.

Needs clang with libFuzzer. `scripts/fuzz.sh [seconds]` builds `bridge/build-fuzz` (ASan + UBSan),
keeps the grown corpus in `build/fuzz-corpus` and writes crashes to `build/fuzz-artifacts/`;
reproduce one with `bridge/build-fuzz/bin/command_fuzzer <file>`.

Inputs can name file paths (`editor.saveDocument`, `config.write`, dialogs). HOME and the XDG
directories point into a temporary directory and D-Bus is disabled, but a hostile input could still
name an absolute path: run it in a disposable environment (a container, or the CI job).

Debug commands (`TKDE_DEBUG_COMMANDS=1`) are not registered in the fuzzer, so what it exercises is
exactly what Tinox can call in a release build.

### What it has found

* An event emitted by a document deleted late (Qt's deferred delete at process exit) reached the
  event queue after the `Bridge` singleton had been destroyed. The singleton is now never destroyed
  and `tkde_exec` deletes all registered objects before it returns.

## Live IDE diagnostics

`make test` runs `scripts/test-diagnostics.sh` on the real bridge and tinox-lsp,
checking error/fix latency below 300 ms, tree/editor agreement, gutter, hover,
navigation, server loss/restart and closing with private temporary settings.
See [diagnostics.md](diagnostics.md) for version guards and the unit tests.

## Language server client

`tests/LspTest.tnx` checks the protocol and document synchronization independently
of a process. `tests/LspProcessTest.tnx` exercises duplex pressure, cancellation,
crash recovery and teardown against a scripted peer, then uses the real tinox-lsp.
Set `TINOX_LSP` when the executable is outside PATH and the sibling Tinox checkout.
See [lsp-client.md](lsp-client.md) for the library's ownership and polling contract.

## Existing search test race

`SearchTest::testSearchRipgrep` can intermittently report zero matches instead of
an error for an invalid regular expression. This also reproduces on the baseline
commit `8f54f68`, without terminal support. `FileSearch.runRipgrep` treats pipe EOF
as completion, reads the exit code once and then kills the process; EOF does not
guarantee the child has exited. If its exit status is not available yet, the
ripgrep error is lost. Keep this failure visible; the terminal tests do not skip
or change the search test.

Older installed compilers also compile once per test method; the performance
issue is tracked in [tinox#514](https://github.com/subnix-work/tinox/issues/514).
Passing individual test files to independent compiler processes avoids repeating
project-wide discovery while retaining their normal test isolation.

## Adding to the suite

A new namespace gets: a `*_test.cpp` driven through the ABI (copy the smallest one, `model_test`
shows the `QAbstractItemModelTester` pattern, `action_test` the QTest one), a line in the
`CMakeLists.txt`, seeds and dictionary entries for the fuzzer, and a check that the registry is back
to its baseline at the end.

The real navigation smoke (`scripts/test-navigation.sh`, part of `make test`)
creates two Tinox files in a temporary project and drives the IDE through
cross-file definition, history, references, outline, workspace symbol palette and
previewed multi-file rename/apply/undo against the real language server.
