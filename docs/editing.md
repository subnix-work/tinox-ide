# Formatting, save barriers and code actions (#28)

The Edit menu and command palette expose Format Document (Ctrl+Shift+I), Format
Selection, Code Actions (Alt+Enter) and Organize Imports. They use the focused
split pane. A light-bulb button above the editor opens the same code-action menu
when the active document has diagnostics. Preferred fixes appear first; disabled
fixes show their server-provided reason. Selection formatting requires a selection.

Enable **Editor → Format Tinox files before saving** in Settings to format on save.
It defaults to off and applies to Tinox buffers only. Formatting completes before
the ordinary save/backup or Save As write. Save All, autosave, Save and Close and
quit review use the same asynchronous barrier. A failed formatter, timeout,
changed source, closed/replaced editor or retired server cancels that save; the
buffer stays open and its changes remain available. The status bar gives the
reason. Turn the option off to save source that the formatter refuses (for example,
a syntax error). Direct `Document.save`/`Documents.save` are low-level writes;
feature modules should use `Documents.saveWith` to honor the barrier.

`EditService` captures the native text, version, document identity and server
session before requesting work. Requests wait for queued editor changes to reach
the shared LSP client, with bounded deadlines, and never block event dispatch.
Returned edits are validated as UTF-16 and applied through the native
`editor.workspaceEdit` transaction, which rechecks native versions before mutation.
Formatting is one ordinary editor undo step. Empty or unchanged edit lists do not
create an undo step, so repeated formatting/saving preserves the original undo.
The current tinox-lsp formatter has the same opinionated layout as `tinox fmt`;
selection formatting applies changed formatter hunks touching the selected lines.

Code actions include the current diagnostics overlapping the cursor/selection.
Organize Imports explicitly requests `source.organizeImports` and applies that
source action; a normal quick fix requires the user's menu selection. Literal
CodeActions and lazy `codeAction/resolve` edits retain opaque data and their
original source/target version guards. Shared `WorkspaceEdits` parsing supports
`changes` and versioned `documentChanges`; multi-file text edits apply atomically.
Server-command actions and file resource operations are explicitly refused before
any edits, rather than partly executing an action. Only text-edit resolution is
advertised to the server. Actions selected from an old menu cannot change newer
buffers. Editing shuts down before the language server and native UI; its
permanent poll hook is inert afterwards and pending save barriers settle as cancelled.

Preferred code actions are ordered with integer pass indices because iterating a
Bool list and comparing its elements with Bool-returning calls produces invalid
LLVM IR in the installed compiler. The minimal reproduction and compatibility
choice are tracked in [tinox#580](https://github.com/subnix-work/tinox/issues/580).

The real editing smoke verifies CLI-equivalent formatting, selection formatting,
idempotent format on save and undo, an unresolved type's import fix (then checks
the fixed file with `tinox check`), sorted/deduplicated imports, syntax-error save
cancellation and asynchronous Save and Close. `EditingTest` covers stale/closed
buffers, failures, waiting/coalesced save callbacks, Save As and quit review,
code-action diagnostics, preferred ordering, lazy resolution and refused commands.

Opening or relabeling editor tabs used to emit intermediate Qt selection changes
during widget reconciliation. These queued events could switch the current file
back before a quick fix was invoked. Initial construction and structural updates
now block those programmatic signals; native widget tests verify that actual user
selection still emits events and reconciliation does not.
