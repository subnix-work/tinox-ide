# Contributing

## Where does an issue belong?

* Language, compiler, runtime, standard library, `tinox-lsp`: **`subnix-work/tinox`**
  (label `ide-support` when the IDE needs it).
* Everything about the IDE (bridge, bindings, UI, packaging): this repository.

## Workflow

* `main` is always buildable. Work happens on `feature/<short-name>` branches
  and lands through a pull request that references its issue (`Refs #N`,
  `Closes #N`).
* From M2 on, edit the IDE inside Tinox IDE: `make dogfood` opens this checkout.
  Follow [the self-hosting loop](docs/dogfooding.md). Record a forced external-editor
  fallback as friction, then return to the IDE; bootstrap/recovery and automated
  harnesses may run outside it.
* Run `make test` before every commit. Also run `make test-dogfood` for self-hosting
  changes and `make docs` when changing documentation. Packaging CI builds/smokes
  Flatpak; the full compiler/bridge/sanitizer matrix remains issue #34.
* Every IDE pain point gets an owning issue and a row in the
  [friction log](docs/friction-log.md). Review the log at each milestone before
  advancing it; record unresolved constraints explicitly.
* Commit messages and code (comments, identifiers, docs) are in English.
  Explain the why in the commit body.

## Style

* C++: `make fmt` (clang-format, KDE-like). Qt/KDE idioms, no exceptions across
  the C ABI, every exported function is `extern "C"` and documented in
  `docs/abi.md`.
* Tinox: `tinox fmt`. One type per file, annotations over manual wiring.
* The C ABI is additive-only once published; changes go through
  `docs/abi.md` first.

## Threading rules (see docs/adr)

Qt objects are touched on the main thread only. Tinox logic never calls into
Qt directly: it posts commands and polls events. Pointers into Tinox heap
memory are never stored on the C++ side (copy, or use handles).
