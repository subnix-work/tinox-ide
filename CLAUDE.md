# Project conventions

Native KDE Plasma IDE for Tinox, written in Tinox, with a C++ bridge to
Qt 6 / KF6. Plan and status: the epic (issue #1). Architecture: docs/adr.

## Where things go

* Language / compiler / runtime / stdlib / tinox-lsp problems are issues in
  `subnix-work/tinox`, not here. Work around nothing silently: open the issue
  and reference it.
* `bridge/` is C++ (CMake + ECM); `src/` and `tests/` are Tinox.

## Build and test

`make bridge`, `make ide`, `make test`, `make test-bridge`, `make fmt`, `make check-env`
(details: docs/testing.md).
`tinox.toml` links the bridge through `[native]`. Run `make test` before every
commit.

## Rules that are easy to get wrong

* Qt lives on the main thread; Tinox logic runs on a spawned task and talks to
  it through the bridge's command/event queues (no callbacks into Tinox).
* Never keep a pointer to Tinox heap memory in C++ (the GC does not scan it);
  strings returned to Tinox must be static or `GC_malloc_atomic`ed.
* The C ABI in `bridge/include/tinoxkde.h` is additive-only after v1.
* One class per file in Tinox, file name = class name; `fn` instance methods,
  `fnc` static ones; newline-terminated statements.
* Tinox gotchas: `loop` and `await` are keywords; a static method is not a function value
  (wrap in a lambda); `x as Int32` converts; lambdas cannot assign captured variables;
  `select` is reserved; operators go at the end of a line, not the start (docs/ui-api.md).
* Commit messages and code are English. Commits end with the attribution lines
  the harness gives.
