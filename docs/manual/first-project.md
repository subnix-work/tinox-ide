# Your first project

Create an empty folder such as `~/Projects/hello-tinox`, then open it with
File → Open Folder… (`Ctrl+Shift+O`). A folder without a manifest can be opened;
adding a manifest makes it a buildable Tinox project.

Create the following files using the project tree's New Folder… / New File…
commands. Create each folder first, select it, then create the file within it.

`tinox.toml` in the project root:

```toml
[package]
name = "hello-tinox"
version = "0.1.0"
compiler = "2.8.0"
entry = "src/Main.tnx"
```

`src/Main.tnx`:

```tinox
class Main
{
    fnc main() -> Int32
    {
        println("Hello from Tinox IDE")
        return 0
    }
}
```

`tests/SmokeTest.tnx`:

```tinox
class SmokeTest
{
    @Test("the test runner works")
    fn testArithmetic() -> Bool
    {
        return 2 + 2 == 4
    }
}
```

Tinox uses one class per file, with the same file/class name, and newline-terminated
statements. `fnc` declares a static method; `fn` declares an instance method.
This smoke test checks the test setup; add assertions for your application's own
behaviour as it grows. No external package dependency is needed for this example.

Save with `Ctrl+S`; Save All is `Ctrl+Shift+S`. Reopen the folder after adding the
manifest so the workspace reads its project name and entry point.

1. Choose Build → Check (`Shift+F7`). The status line should report success.
2. Choose Build → Run (`F5`). It builds the application, prints
   `Hello from Tinox IDE` in the Build panel, then reports exit status 0.
3. Choose Build → Discover Tests. Expand the file in the Tests panel.
4. Select `SmokeTest.tnx` and choose Run Tests in Selected File. Expect one passed test.

To try diagnostics, remove the closing quote from the greeting, then Check again.
The Build Problems panel shows the compiler error; activate it to visit its location.
Restore the quote and save before running again. Live language-server diagnostics
appear separately in Problems and in the editor.

When adding dependencies to a project, install them with `tinox install` before
building. Use the interactive terminal if Konsole is installed, or a host terminal.
Continue with [editing](editor.md) and [build/run/test](build-run-test.md).
