# Contributing

## Where does an issue belong?

* Language, compiler, runtime, standard library, `tinox-lsp`: **`subnix-work/tinox`**
  (label `ide-support` when the IDE needs it).
* Everything about the IDE (bridge, bindings, UI, packaging): this repository.

## Workflow

* `main` is always buildable. Work happens on `feature/<short-name>` branches
  and lands through a pull request that references its issue (`Refs #N`,
  `Closes #N`).
* Run `make test` before pushing. CI (issue #34) runs the same targets.
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
