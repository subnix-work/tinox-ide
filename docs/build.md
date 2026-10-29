# Build, run, check and test

Issue #30. The **Build** menu runs the `tinox` tool on the open workspace and shows the result
in three bottom tabs: **Build** (the output with ANSI colours), **Build Problems** (errors and
warnings grouped by file) and **Tests** (the test explorer). A click on a problem opens the file at
its line and column.

| Command | Key | What it does |
|---|---|---|
| Build | F7 | `tinox build [file] --message-format=json` |
| Check | Shift+F7 | `tinox check [file] --message-format=json` |
| Run | F5 | builds like Build, then starts the produced executable with the run configuration |
| Stop | Shift+F5 | SIGTERM to the whole process group, SIGKILL after one second |
| Select Run Configuration… / Edit Run Configurations… | | see below |
| Start Debugging and the other Debug entries | Alt+F5 | build with `--debug`, then debug; see [debugging.md](debugging.md) |
| Run All Tests | Ctrl+F5 | `tinox test --format json` |
| Run Tests in Selected File | | `tinox test <file> --format json` for the file of the selected tree row |
| Run Failed Tests | | the files that contain failed tests |
| Discover Tests | | `tinox test --list --message-format=json` for the whole workspace, nothing is run |
| Discover Tests in Current File | | `tinox test <file> --list --message-format=json` for the file of the editor tab (or the selected file of the test tree if the tab is not a `.tnx` file); only that file's tests are replaced |
| Rerun Tests on Save | | checkable; saving a `.tnx` file runs all tests (watch mode) |

The label above the test tree says what the last discovery covered (`Discovered: all tests in the workspace`, or the
file, with a note that other files keep their earlier results). A discovery that is stopped, or that fails without
reporting a single test or collection error, puts the previous tests and label back, so a cancelled run never
replaces newer results. The file is passed before the flags because the tool ignores a file after them
(subnix-work/tinox#585).

`tinox test` takes a file but no method filter, so "run one" runs the file of the selected test
and updates every test in it.

The selected file is deliberately passed before options: older Tinox silently ignores
a file after flags ([tinox#585](https://github.com/subnix-work/tinox/issues/585)).
The real self-hosting smoke checks that unrelated files are not run.

## Preferences used

Settings → Build: *Extra arguments for tinox* (added to build, check and the build of Run),
*Save all files before building* (requests Save All before starting; wait for any asynchronous format-on-save work to finish), *Show the output panel
when a build starts*. Settings → Language Server → *Tinox compiler* names the executable; a bare
name is searched in `PATH` and the usual tool folders.

## Run configurations

Stored per workspace in `.tinox-ide/run.json`:

```json
{"selected":"Demo","configurations":[
  {"name":"Project","file":"","args":"","env":[],"cwd":""},
  {"name":"Demo","file":"examples/Demo.tnx","args":"--n 3 \"a b\"","env":["GREETING=world"],"cwd":"work"}]}
```

`file` empty runs the project (the `entry` of `tinox.toml`); `args` is split like a shell without
expansion (the Language Server argument rules); `env` entries are `KEY=value`; `cwd` is relative to
the workspace root. *Edit Run Configurations…* opens the file (creating it with the default
entry); *Select Run Configuration…* asks which entry Run uses. A missing or damaged file gives a
single default `Project` entry.

## Output and process control

`TaskRun` starts the child with `InteractiveProcess.startPiped` (its own process group) and one
reader task multiplexes stdout and stderr with `waitReadable`; the logic task collects the output
from an `EventLoop` poller, so Qt is never touched from the reader and no callback enters Tinox.
Output is streamed to the log with `widget.append {ansi: true}`: no implicit newline, SGR colours
and bold are decoded, also when a sequence is split across writes (`clear` empties the log at the
start of a task). The same decoder serves the terminal's read-only fallback console.

The status bar shows `Build running…` (store key `build.activity`) while a task runs; the commands
that start a task are disabled until it ends. When it ends, `notify.send` raises `buildFinished`,
`buildFailed`, `testsFinished` or `testsFailed` (`resources/notifications/tinoxide.notifyrc`);
stopping a task sends nothing.

## Diagnostics

With `--message-format=json` stdout carries one JSON object per line (schema in the compiler's
`docs/compiler-diagnostics.md`). `compiler-message` events become problems with the compiler's
one-based line and column and the stable code; `build-finished.artifact` is the program Run starts.
Everything else (the program's own output, a tool without JSON output, stderr) is searched for text
reports of the form `path:line:column: severity: message` (column and severity optional, ANSI
removed, relative paths resolved against the task's directory). A report seen in both forms is
listed once. Reports without a file are listed under "(no location)".

## Test explorer

`test-discovered`, `test-started`, `test-passed`, `test-failed` and `test-collection-error` events
fill a tree of files → tests with the result and duration; `test-run-finished` decides the status
message and notification. A failed test lists its message lines below it; activating a line that
contains `path:line:column` opens that place (the stack location), any other line or the test row
opens the test method. Files the compiler could not collect appear as error rows. Store keys:
`tests.summary`, `tests.failed`.

## Tests

`tests/BuildTest.tnx` covers the parsers, the configuration file, the explorer, a real failing
build (output, problem, click-to-open, notification), Run with arguments/environment/working
directory, Stop, discovery and runs of real tests, run-failed, and watch mode. The bridge test
`widget` checks the ANSI stream mode.
