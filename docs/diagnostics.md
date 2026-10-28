# Live language-server diagnostics

Issue #25. Open a `.tnx` file to start the configured `tinox-lsp`. The service is
started after the IDE restores its project and editor session; ordinary text files
do not start a language server. The built-in Problems tab is a tree grouped by
open file, with severity icons and Message, Location, Code and Source columns.
The status bar shows error/warning counts and any information/hint counts.

Click a problem to navigate to its UTF-16 location. **F8** and **Shift+F8** select
the next/previous problem with wrapping in displayed order (also in the Go menu).
**View → Show Problems** reveals the panel. Selecting a file group does not jump
into the editor. Decorations use KTextEditor's existing moving ranges, squiggles,
gutter marks and HTML-escaped diagnostic hover text.

## Server configuration and lifetime

Settings → Language Server provides the executable and extra arguments. An empty
path uses `TINOX_LSP`, PATH/usual tool directories, then the sibling development
binary at `../tinox/target/release/tinox-lsp`. Executable paths are resolved before
the child changes its working directory to the workspace. Extra arguments support
whitespace, quotes, backslash escapes and Unicode; no shell is executed and there
is no variable, glob or command expansion. Invalid paths/arguments produce an
actionable status message rather than repeated launch attempts.

**Settings → Restart Language Server** retries a missing server. Changing the
server path/arguments or the workspace root retires the old session, clears its
problems and synchronizes every open Tinox buffer into a new session. A crashed
server uses the client's existing backoff and document replay. Quitting stops
language services before releasing editor documents and the Qt bridge.

## A shared snapshot

`LanguageService` subscribes to document open/change/save/reload/retarget/closing
messages. It does not replace the document manager's save, modified or external
change handlers. Reload, Save As, rename and move close the old URI when necessary
and synchronize a fresh snapshot. Closing is announced before document release,
so marks can be removed while the document still exists.

`Document.snapshot()` captures text and version in one Qt-thread command. LSP
changes retain the editor's sequential UTF-16 ranges and version; full versus
incremental synchronization follows the server's capabilities. Edits clear the
old accepted diagnostics immediately. No client-side debounce delays didChange;
the server controls analysis/debounce.

The client advertises `publishDiagnostics.versionSupport`. Versioned reports are
accepted only when they exactly match the latest synchronized version. Unknown
and closed URIs are ignored. Unversioned reports are accepted against the latest
snapshot for compatibility; their analysis version cannot be proven by the client.
Ranges and UTF-16 boundaries are validated without clamping. Missing severity
means error; codes may be integers or strings. An invalid batch leaves the last
accepted batch unchanged and records the failure in the client.

`Problems` sends the accepted Diagnostic objects to the editor and builds its tree,
counts and navigation from those same objects. The optional `version` precondition
on `editor.setDiagnostics` is checked on Qt's thread, preventing a result from being
applied if the user typed between the Tinox-side check and command execution.
If updating the tree fails, its old batch and editor decorations are restored.
Queued selections for an already selected row are ignored; a diagnostic from an
older document version is never used to navigate a newly edited buffer.

Only open Tinox buffers are displayed. Closing clears the file in both views and
late reports cannot resurrect it. Lifecycle observers clear every batch on loss,
reinitialization and stop **before** a replacement generation can publish. An old
workspace client's handlers are detached before it is retired.

## Event-loop integration

`EventLoop.onPoll` installs nonblocking logic-task services. `pump` runs them
between events and on idle timeout, limiting waits to 20 ms while services exist.
This keeps LSP replies moving without background tasks sharing protocol maps or
blocking the UI. Each poller is isolated like an event handler; one failure does
not starve other services. Loops without pollers preserve their original timeout
behavior. The C ABI is unchanged.

## Verification

`DiagnosticsTest.tnx` covers grouped severity/code/source rows, navigation, counts,
immediate clearing, stale/future versions, closed URIs, crash generations,
malformed ranges, rollback, Save As/reload and argument parsing. Event-loop tests
cover idle/event-burst polling and failure isolation. The bridge editor test checks
that stale/fractional version preconditions change no diagnostics.

`make test` also runs `scripts/test-diagnostics.sh`: a temporary file, private XDG
settings, the real IDE/Qt bridge and real tinox-lsp. It introduces a syntax error,
requires its diagnostic in under 300 ms, checks the tree/editor count, gutter and
hover, navigates, terminates the server transport and verifies clear/restart,
fixes the error in under 300 ms, then closes the file. Unexpected warnings fail the
test. Its printed timings are measured from the editing command through the live
protocol and editor update; they are not a guarantee for arbitrary project sizes.
