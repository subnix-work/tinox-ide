# Language navigation and rename

F12 or Ctrl+click goes to a definition, including another file. Multiple definitions
appear in the References / Rename panel; double-click a result to open it.
Shift+F12 lists references including declarations. Alt+Left and Alt+Right traverse
navigation history, restoring the exact cursor position and remembering cursor
movement before leaving an entry. A new jump truncates forward history.

The Outline dock shows nested `documentSymbol` results for the active buffer. A
breadcrumb button above the editor follows the containing symbols; clicking it
opens a searchable document symbol picker. Ctrl+T opens the workspace symbol
palette. These commands also appear in the Go menu and command palette.

Ctrl+F2 asks for a new symbol name; F2 remains the project tree’s file rename. Server errors (invalid names, ambiguity, standard
library symbols) appear in the status bar. A successful `WorkspaceEdit` opens all
participating files and shows every old/new replacement with file and position.
Apply Rename commits the preview; Cancel leaves all text untouched. Editing or
closing any preview participant cancels it. Both `changes` and versioned
`documentChanges` text edits are accepted. Resource operations are rejected
explicitly; the client does not advertise file-rename support. Tinox's server
reports the required filename change when renaming a class. File resource
operations are a separate capability from text rename and are not silently applied.

The native `editor.workspaceEdit` command validates every target and applies the
batch in one Qt dispatch, before any user input can interleave. Ranges, overlaps,
read-only buffers and exact versions are checked before mutation. Undo Rename
Across Files (Ctrl+Alt+Z) applies the captured inverse batch atomically. It refuses
intervening edits or closed/replaced documents, preserving those newer changes.
Each document also retains ordinary editor undo. Rename changes buffers and
leaves saving to the user.

`NavigationService` owns feature futures and polls through EventLoop. Source
versions, open-buffer target versions and the client session reject stale replies.
Dead generations clear outstanding requests and outlines. Workspace palette
queries carry generations so late replies cannot replace newer search results.
No feature blocks while waiting for a language server. The outline refreshes after
accepted diagnostic analysis and defers requests while native changes are waiting
for delivery to the shared client. Server capabilities are
checked before requests; file URI decoding preserves Unicode and rejects remote
URIs, malformed escapes and NUL.

The class range predicate is called `encloses` because the installed Tinox 2.8.0
compiler lowers a user-defined `contains(Position)` as `String.contains`, producing
invalid LLVM IR. This compiler issue is tracked in
[subnix-work/tinox#576](https://github.com/subnix-work/tinox/issues/576).

`NavigationTest` covers URI validation, precise history, stale replies, rename
preview/inverse transactions and nested outline/breadcrumbs. Bridge editor tests
verify all-or-none mutation for invalid/stale/duplicate target batches and normal
undo. `scripts/test-navigation.sh` drives real Qt/KTextEditor and tinox-lsp through
cross-file definitions, history, references, outline, workspace symbol palette,
and a multi-file method rename with preview, apply and batch undo.

Two server limitations have explicit compatibility behavior. For a null definition,
workspace symbols are filtered by the exact identifier and an explicit `Type::`
qualifier when present; multiple candidates remain a chooser. This supplies
cross-file declarations without pretending to resolve ambiguous instance types.
The server-side cross-file resolution gap is tracked in
[subnix-work/tinox#578](https://github.com/subnix-work/tinox/issues/578).
Zero-width documentSymbol extents are conservatively extended to the next sibling
or enclosing/document end, and identifier selections are found on the reported
declaration line. Genuine full ranges are preserved. This keeps breadcrumbs useful
while full parser extents are fixed in
[subnix-work/tinox#577](https://github.com/subnix-work/tinox/issues/577).
The real navigation smoke uses the sibling development tinox-lsp by default;
`TINOX_LSP` can select another server. Older installed servers can lack references,
rename and workspace symbols; unsupported commands report the missing capability.
