# LSP client library

Issue #24. `tinox.ide.lsp` implements JSON-RPC over stdio and LSP lifecycle,
independently of Qt's event queue. The [live diagnostics integration](diagnostics.md) uses it in the IDE; further
editor language features are tracked in issues #26–29.

## Ownership and use

`LspClient` belongs to one logic task. Call `poll()` regularly (for example every
20–100 ms from the application's event/timer loop). It never waits for pipe I/O.
Request results are `RpcFuture` objects: inspect `done`, then call `value()` to
obtain the JSON result or throw its error. `cancel(future)` settles the future
immediately, sends `$/cancelRequest`, and ignores a later server response.
Requests have mandatory positive timeouts. Outstanding futures fail on a crash;
requests are never silently retried because they may have side effects.

```tinox
const client = LspClient::launch(["tinox-lsp"], projectPath, Utf16::uri(projectPath))
client.open(Utf16::uri(filePath), "tinox", text, 1)
// On the owning task, once initialized:
client.poll()
if (client.state == "ready")
{
    const request = client.request("textDocument/documentSymbol", paramsJson, 5000)
    // Continue polling; request.done becomes true when answered or timed out.
}
```

Use `initialize` only with `LspClient::create(sender)` for an injected transport.
`launch` initializes automatically. `pollAt(milliseconds)` supplies a deterministic
clock for tests or a caller-provided monotonic clock; use one clock consistently
for the client's entire lifetime. `poll()` uses `Time.now()`.

`lifecycle(state)` observes transitions before subsequent protocol messages can
publish a new generation; observer failures are isolated.

Server notifications are delivered through `notification(method, params)` on the
owning task. `configuration(item)` supplies each workspace/configuration result.
`applyEdit(edit)` must atomically validate and apply the entire WorkspaceEdit;
it defaults to refusing it. `window/showMessageRequest` is forwarded as a message
and answered with null (no selected action). Workspace folder queries return the
initial root. Unsupported server requests receive MethodNotFound; handler errors
receive InternalError. Notification handler failures are recorded in `lastError`.
The library advertises no dynamic registration or UI features it cannot implement.

## Process transport

`StdioTransport` starts a reader/supervisor task and a dedicated writer task.
The writer takes complete frames off a mutex-protected queue and releases that
lock before writing. A large stdin write cannot prevent the reader from draining
server output.

The process API requires read/lifecycle operations on one handle to be serialized.
Consequently a single reader multiplexes stdout and stderr through
`InteractiveProcess.waitReadable`, rather than two tasks racing on the runtime's
shared pipe buffers. Stdout goes to the frame decoder. Recent stderr is retained
separately, bounded to 64 KiB, and never parsed as protocol data.

Complete input/output queues are limited to 64 MiB each. Frames accept at most
8 MiB of body and 8 KiB of headers. The decoder counts UTF-8 bytes, supports
headers/bodies split at any byte (including within a Unicode character), and
rejects missing, duplicate, negative, oversized or nonnumeric Content-Length,
unsupported charsets and truncated EOF. Headers and JSON bodies are separated
by CRLF CRLF as required by the
[LSP base protocol](https://microsoft.github.io/language-server-protocol/specifications/lsp/3.17/specification/#baseProtocol).

Stopping asks the supervisor to kill the process group, then join the potentially
blocked writer, then close pipes and reap the child. This order avoids closing
fds underneath another task. Transport state and cross-task queues are protected
by `Mutex`; protocol maps, document snapshots, handlers and futures stay entirely
on the owning task. Do not access transport fields directly from other tasks.

## Initialization, document sync and recovery

The client advertises UTF-16 only. Absent positionEncoding means UTF-16; selecting
another encoding fails initialization. The shared `tinox.ide.ui.Utf16` helpers
convert UTF-8 byte offsets to/from the editor's `Position` and reject split code
points/surrogate pairs. The bridge already uses QString UTF-16 columns, so its
positions need no additional conversion (`bridge/src/editor/positions.h`).
`Utf16.uri` percent-encodes absolute local paths as file URIs.

Documents may be opened or changed before initialization finishes. Their latest
text/version is retained. On initialize success the client sends initialized and
replays didOpen snapshots. Server textDocumentSync capabilities select full or
incremental didChange; changes use sequential `TextEdit` ranges in UTF-16, matching
`Change.edits` from the editor. didSave is sent only when supported, including text
only when requested. Closed documents are removed and never replayed.

Unexpected exit, broken framing, initialization failure and initialization timeout
reject pending requests and trigger a new process after an exponential backoff
(100 ms through 5 s). IDs increase across generations. A server must stay ready
for 10 s before the crash-loop counter resets. The new process starts only after
the old transport has completed cleanup. Open documents are resynchronized from
their latest snapshots, without replaying old edits.

`shutdown()` sends shutdown, waits up to 2 s for its response, then sends exit.
If the child stays alive for another 2 s it is killed. `stop()` cancels immediately.
Poll until state is closed to observe completed cleanup. Neither path restarts
an intentionally stopped server. Injected transports do not automatically restart;
call initialize after the client enters backoff.

## Verification

`LspTest.tnx` covers framing (including byte-by-byte Unicode input), invalid
headers, UTF-16/CRLF, request correlation, cancellation and late results, deadlines,
capability rejection, lifecycle validation, full/incremental document state,
server requests and bounded shutdown.

`LspProcessTest.tnx` uses `tests/fixtures/lsp_server.py` to exercise real pipes,
1 MiB document uploads while the peer fills both stdout and stderr, fragmented
Unicode replies, crash/restart with the latest text/version, and stopping a writer
blocked by an unread stdin pipe. It also initializes, synchronizes, requests symbols
and shuts down the real `tinox-lsp`. Set `TINOX_LSP` to its executable, put it on
PATH, or build it at `../tinox/target/release/tinox-lsp`; absence fails this integration
test rather than silently skipping it. Python 3 is needed for the scripted peer.
