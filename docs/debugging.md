# Debugging

Issue #31. The IDE debugs Tinox programs through the Debug Adapter Protocol. The adapter is
`tinox dap` (gdb 14 or newer with its built-in DAP server and the Tinox pretty-printers, see
[Debugging Tinox programs](https://github.com/subnix-work/tinox/blob/main/docs/debugging.md)); the
program must be built with debug information, which **Start Debugging** does for you.

| Command | Key | What it does |
|---|---|---|
| Start Debugging | Alt+F5 | `tinox build --debug` with the selected run configuration, then starts the adapter and the program |
| Continue | F6 | resumes the stopped thread |
| Pause | Shift+F6 | interrupts the running program |
| Step Over / Into / Out | F10 / F11 / Shift+F11 | `next`, `stepIn`, `stepOut` on the current thread |
| Run to Cursor | Ctrl+F10 | continues to the line of the cursor (a temporary breakpoint) |
| Stop Debugging | Alt+Shift+F5 | terminates the program and the adapter |
| Toggle Breakpoint | Ctrl+F9 | on the line of the cursor |
| Remove All Breakpoints | | in all files and in the running session |

All of them are in the **Debug** menu. The run configuration (arguments, environment, working
directory, [build.md](build.md#run-configurations)) is the one Run uses.

## Breakpoints

A click on the gutter of an editor toggles a breakpoint (KTextEditor mark `BreakpointActive`); the
mark follows the text through edits. The service keeps the breakpoints per file path
(`BreakpointSet`), shows them again when a file is reopened, and sends them to a running adapter
with `setBreakpoints` as soon as they change. Lines are zero-based in the IDE and one-based in DAP.
The editor is the truth for lines: before they are sent or a session starts, they are read back from
the gutter (`editor.getBreakpoints`). Breakpoints are kept for the IDE session, not saved to disk.

The stop line carries the `Execution` mark and the editor scrolls to it; the mark goes when the
program runs on or the session ends. See [editor.md](editor.md#breakpoints-and-the-stop-line) for the
bridge commands.

## Views

The bottom panel gets four tabs:

* **Variables**: the arguments and locals of the selected frame (gdb offers them as two DAP scopes;
  registers and globals are not shown). Structured values (classes, `List`, `Map`) open lazily with
  one `variables` request each, as the Tinox pretty-printers summarise them.
* **Call Stack**: the frames of the current thread; a double click or Enter selects a frame, opens its
  source and loads its variables.
* **Threads**: a double click switches the thread the other views show.
* **Debug Console**: the adapter's `output` events, the exit status, and an input line. Text typed there
  is evaluated in the selected frame with context `repl`, so with gdb both `n * 2` style expressions
  and gdb commands (`info registers`, `print result`) work.

## How it fits together

```
 Start Debugging ──bus: debug.requested──► BuildService   tinox build --debug
                                                │ last step "debug": no process, hands over
                 ◄──bus: debug.launch {adapter, program, args, env, cwd}
 DebugService ── DapClient ── StdioTransport ── tinox dap (gdb)
```

* `DapClient` (`tinox.ide.debug`) reuses `StdioTransport` and `Framing` of the [LSP client](lsp-client.md#process-transport);
  only the envelope differs. It is poll driven like `LspClient`: requests return an `RpcFuture`, events
  are queued for `takeEvents()`, nothing waits on a pipe. Requests the adapter makes to the client
  (`runInTerminal`, `startDebugging`) are refused.
* `DebugService` runs one session at a time. Every request has a continuation (`DapCall`); answers of an
  earlier session are dropped by comparing a generation number. gdb needs the DAP order
  `initialize` → `launch` (answered only later) → event `initialized` → `setBreakpoints` →
  `configurationDone`.
* The session ends when the user stops it (`disconnect` with `terminateDebuggee`, then the adapter is
  stopped), when the program terminates, when the adapter dies, or when the workspace closes or the IDE
  quits. Store keys: `debug.active`, `debug.stopped`.
* Under Flatpak the adapter runs on the host through `HostCommand`, like the builds.

## Known limits

Those of `tinox dap` (block scopes, generic instantiations, enums as integers, unnamed task threads), and:

* Breakpoints are not saved with the workspace.
* Files renamed, moved or deleted outside the project tree (in a file manager or the terminal) lose
  their breakpoints; the tree's own operations move or drop them.
* Conditional breakpoints, logpoints, watch expressions and attaching to a process are not offered.
* The variable value of an expression typed in the console is printed once; it is not tracked.

## Tests

* `tests/DapClientTest.tnx`, `tests/BreakpointSetTest.tnx`: protocol envelope, correlation, events,
  timeouts; sorted unique lines per file.
* `tests/DebugServiceTest.tnx`: a whole session against a scripted adapter (no gdb needed).
* `bridge/tests/editor_test.cpp` (`testBreakpoints`): gutter marks, the toggle event, moving with the text.
* `scripts/test-debug.sh` (in `make test`; skipped without gdb): the real IDE offscreen with `tinox build
  --debug` and `tinox dap`: a gutter breakpoint is hit in a sample program, the locals show `n = 7`, a step
  over shows `result = 49`, continue ends the session.
