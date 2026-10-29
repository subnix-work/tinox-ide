# Build, run and test

Open the project folder that contains `tinox.toml`, then save your changes before
starting a task. The compiler and tests run in that workspace. While a task is
active, other task-start commands are disabled; Stop remains available.

| Command | Shortcut | Result |
|---|---|---|
| Build | `F7` | Compile the project or configured file |
| Check | `Shift+F7` | Check source without running it |
| Run | `F5` | Build, then start the produced executable |
| Stop | `Shift+F5` | Request termination, then force it after a grace period |
| Run All Tests | `Ctrl+F5` | Run the whole workspace's tests |

The Build tab streams stdout/stderr and ANSI colours. The status line gives the
result. Build Problems groups compiler errors and warnings by file; activate a
problem to open its location. Live LSP Problems remains a separate panel.

The default Save all files before building preference requests saves. When using
format-on-save, wait for saves to finish and modified markers to clear before
starting a build. A formatter error leaves the buffer unsaved; resolve it first.

## Run configurations

Choose Build → Edit Run Configurations… to open `.tinox-ide/run.json`. For example:

```json
{
  "selected": "Project",
  "configurations": [
    {
      "name": "Project",
      "file": "",
      "args": "",
      "env": [],
      "cwd": ""
    },
    {
      "name": "Example",
      "file": "src/Main.tnx",
      "args": "--name \"Ada Lovelace\"",
      "env": [
        "GREETING=hello"
      ],
      "cwd": ""
    }
  ]
}
```

An empty `file` uses the manifest entry. Otherwise it selects a file relative to
the workspace (or an absolute path). `args` supports quoted argument groups,
with no shell expansion. `env` contains `KEY=value` entries; `cwd` is relative to
the workspace root. Save the configuration, then use Select Run Configuration…
to choose it. A missing or invalid configuration falls back to Project.

In Flatpak, tools and produced programs execute on the host with the run
configuration's working directory and environment. The host needs the Tinox
compiler/core/runtime, clang, libgc development files and tinox-lsp on PATH.

## Test explorer

Discover Tests fills the Tests tree without compiling/running test bodies. Expand
a file to see test descriptions. Select a file or test row and use Run Tests in
Selected File. Run Failed Tests reruns the files containing failed tests.

Selection currently has **file granularity**: selecting one method runs every test
in its file. Exact method filtering is tracked in
[tinox#582](https://github.com/subnix-work/tinox/issues/582). With no selected file,
the command runs all tests. Counts and durations appear in the tree; activate a
failure detail to visit its source location.

Rerun Tests on Save runs all workspace tests whenever a `.tnx` file is saved.
Leave it disabled on large projects unless you want that cost. Older toolchains
compile once per test method, so a large suite can take several minutes.
[Testing](../testing.md) explains the current development workaround and checks.

## Terminal

`F4` opens Terminal. With Konsole's KPart installed it provides an interactive
shell in the project folder. Without that plugin (including the current Flatpak),
the tab is a read-only output console and cannot execute shell commands. Use a
host terminal for dependency installation, Git and Make commands in that case.

The internal task/output details are documented in [build.md](../build.md).
