# Embedded terminal

Issue #14. Open the Terminal tab in the bottom dock with **F4** or
**View → Open Terminal**. The shell starts lazily in the current project root,
otherwise in the home directory. Opening or closing a project changes its
working directory. If a foreground program is running, the directory stays
unchanged and a message explains why; input is never sent to that program.
Opening the terminal after the shell exits starts a new session.

The bridge loads `kf6/parts/konsolepart` with `KPluginFactory` and uses the
KParts `TerminalInterface`. Konsole is optional at runtime; KF6 Parts is a
build dependency. When the plugin cannot load, a read-only `QPlainTextEdit`
console is embedded instead. It accepts output through `sendText`, with
incremental ANSI SGR decoding (normal/bright foreground/background colours,
bold and reset); it does not execute commands or emulate cursor controls.
History is limited to 10,000 blocks. Unsupported CSI sequences are discarded.

## Bridge commands

All commands execute on Qt's main thread. IDs belong to stable container
widgets, usable with `Embed` and released with `core.release` / `tkde_release`.
The C ABI is unchanged; `core.capabilities` advertises `terminal`.

| Command | Fields | Reply |
|---|---|---|
| `terminal.create` | `directory` (existing absolute path, default home), `title` (default Terminal), `backend` (`auto` or `console`, default auto) | `id`, `backend` (`konsole` or `console`), `reason` (plugin failure, if any) |
| `terminal.sendText` | `target`, `text` | Standard reply; shell input or console output |
| `terminal.setTitle` | `target`, `title` | Standard reply |
| `terminal.setDirectory` | `target`, `directory` | Standard reply; refuses a busy shell and invalid directories |
| `terminal.info` | `target` | `backend`, `directory` (last requested directory), `title`, `finished` |

When the Konsole shell exits, the bridge emits `terminal.finished` with its
container `target`. Konsole destroys its part at session end and does not
provide an exit status through this interface. The container survives so
window redraws keep valid embed IDs. Explicit release and application shutdown
do not emit this event.

`tinox.ide.ui.Terminal` wraps these commands and routes `onFinished` events.
`tinox.ide.terminal.TerminalPanel` installs the IDE tab, command and workspace
subscriptions. The title API sets the embedded widget title; the tab remains
labelled Terminal.

## Tests

`make test` runs real bridge tests for dock embedding, ANSI sequences split
across writes, validation, title/directory changes, release and shell exit.
`terminal_no_konsole` hides all plugin search directories after Qt starts and
checks automatic fallback with no Konsole available. Tinox mock tests cover the
typed wrapper, lazy startup, workspace changes, busy errors and session restart.
