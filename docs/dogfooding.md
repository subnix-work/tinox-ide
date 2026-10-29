# Develop the IDE inside the IDE

From the language-intelligence milestone (M2) onward, use Tinox IDE to edit its
sources (including the C++ bridge), tests and documentation. Bootstrap/build tooling, compiler recovery
and automated acceptance may run outside the editor. When an IDE limitation forces
a fallback, record it in the [friction log](friction-log.md) with an owning issue;
return to the IDE when the blocked operation is complete.

## Bootstrap once

Follow [development](development.md) to install a matching compiler/core/runtime,
tinox-lsp, clang and Qt/KDE dependencies. Then run:

```sh
make check-env
make bridge
make dogfood
```

`make dogfood` builds the initial binary and opens this checkout as its workspace.
If you already built it, `TINOXIDE_OPEN="$PWD" ./build/tinox-ide` starts directly.
For the full linked manual/reference site, install Python-Markdown and run
`make docs`; open `build/docs/index.html`.

The repository provides `.tinox-ide/run.json`:

| Configuration | Scope |
|---|---|
| IDE | Check/build `src/Main.tnx` and its imports; Run starts the rebuilt IDE |
| IDE smoke | Same entry, with `TINOXIDE_SMOKE=1`; the child builds its window and quits |
| Project (full check) | Empty file selection; Check examines all project sources/tests |

The IDE entry is selected by default. Full-project Check can be slow because the
compiler repeatedly prepares shared dependency graphs ([tinox#559](https://github.com/subnix-work/tinox/issues/559)).
Entry-point Check is an interactive feedback step; it does not replace full tests.
After editing the configuration file, save it and use Select Run Configuration…
to reload/select the desired entry.

## Daily loop

1. Open the relevant source/test/manual with `Ctrl+P`; use Outline, `F12` and
   `Shift+F12` to inspect the current code. A missing server capability should be
   recorded, not mistaken for a missing symbol.
2. Edit, format where appropriate and save explicitly. Wait for any format-on-save
   operation to finish before starting a task.
3. Use Check (`Shift+F7`) and read Build Problems. With IDE smoke selected, Run
   (`F5`) compiles the actual IDE and verifies a fresh instance can start and quit.
   Choose IDE for an interactive child window instead.
4. In Tests, select the relevant file and Run Tests in Selected File. Selection is
   file-wide, even when a single method row is selected ([tinox#582](https://github.com/subnix-work/tinox/issues/582)).
   Full discovery is currently workspace-wide and can take time; file-scoped IDE
   discovery is tracked in [#63](https://github.com/subnix-work/tinox-ide/issues/63).
5. Run the checks appropriate to the change and `make test` before **every commit**.
   Use the interactive Konsole terminal if available; otherwise run Make/Git in a
   host terminal. Changes to `bridge/` require `make bridge` before rebuilding the
   Tinox program. After a bridge change, restart the running IDE to load it.
6. Review the diff and the friction log. Use the repository's English commit/footer
   conventions and a PR referencing the issue. [CONTRIBUTING](../CONTRIBUTING.md)
   explains issue ownership.

An old compiler may still compile separately for each test method despite the
upstream performance fix being closed. [Testing](testing.md) documents per-file
parallel execution. Record the tested toolchain, rather than marking a limitation
resolved only because an upstream issue was closed.

## Automated self-hosting acceptance

```sh
make test-dogfood
```

This runs the **real IDE and KTextEditor**, with isolated XDG settings and a temporary
copy of the current Tinox sources/tests/manifest. It opens the project and Quick
Open, initializes the real LSP, edits and saves `src/Main.tnx` through the document
API, applies a preference, checks the entry, rebuilds and runs the IDE's executable,
and passes the three tests in the IDE's own `HostCommandTest.tnx` through its test
explorer. A source check and fresh child process must succeed; a mock result or a
prebuilt replacement is not accepted.

Discovery in this bounded test is explicitly file-scoped through the existing task
runner, pending #63. The selected-file Run command is the real registered IDE
command. It does not claim to time full discovery or run the whole suite. The copy
links the already-built real bridge; edits and build outputs never touch the user's
checkout. New source edits are copied too, so the test does not accidentally verify
an older HEAD. A failure returns nonzero and removes the temporary workspace.

The run is automated on Qt's offscreen platform. It provides reproducible functional
self-hosting evidence, **not** an interactive usability, Orca, Wayland or visual
review. Those checks remain separate (including issue #32). Review the
[friction log](friction-log.md) at each milestone before claiming a release gate.
