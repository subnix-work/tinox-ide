# The `editor` namespace (KTextEditor)

Implemented by the real bridge (`bridge/src/editor/`, issue #12). Tests:
`bridge/tests/editor_test.cpp` (offscreen; also under ASan/UBSan/LSan and TSan).
All ids are bridge object ids (`docs/abi.md`); a command's `target` is a document id or a
view id as noted. Every position is LSP-shaped, `{"line":L,"character":C}`, where
`character` counts UTF-16 code units (KTextEditor columns are the same unit), and ranges are
`{"start":P,"end":P}`. Invalid positions are rejected with `TKDE_E_INVALID_ARG`, never clamped.

## Documents and views

| Command | Fields | Result |
|---|---|---|
| `editor.createDocument` | `text?` | `id` (initial text is not a change: version 0, unmodified) |
| `editor.openDocument` | `url`, `encoding?` | `id` |
| `editor.loadDocument` | `target`, `url?` | reads the file again, dropping unsaved edits (raises `editor.reloaded {version, url}`, not `editor.changed`); with `url` the document now belongs to that file (after a rename) |
| `editor.saveDocument` | `target`, `url?` (save as) | `url`; an untitled document without `url` is refused (it would open a dialog) |
| `editor.closeDocument` | `target` | |
| `editor.createView` | `document` | `id` (a `QWidget`, parented later by the widget namespace) |
| `editor.info` | `target` (doc) | `url, modified, encoding, endOfLine (lf/crlf/cr), lines, version, highlightingMode, readOnly` |
| `editor.setEncoding`, `editor.setEndOfLine` | `target`, `encoding` / `eol` | |
| `editor.highlightingModes`, `editor.setHighlightingMode` | `target`, `mode` | |
| `editor.themes`, `editor.setTheme` | `target` (view), `theme` (`""` = follow the colour scheme) | |

## Text

* `editor.getText` `{target, range?}` returns `{text, version}` atomically; `editor.setText` `{target, text}` (one undo step).
* `editor.edit` `{target, edits:[{range,newText}]}`: all ranges refer to the **original**
  text, are validated first (overlap, bad range, bad column, missing `newText` reject the whole
  list, nothing changes) and are applied as ONE undo step.
* `editor.undo` / `editor.redo` `{target}` (view).
* `editor.setCursor/getCursor/setSelection/getSelection` `{target (view), position | range}`;
  a `null` range clears the selection.

## Events

| Event | Fields |
|---|---|
| `editor.changed` | `version, origin ("user"/"command"), changes:[{range,text}]`, one event per editing transaction; each change's range refers to the text after the previous change (LSP `didChange`) |
| `editor.modifiedChanged`, `editor.urlChanged`, `editor.saved`, `editor.highlightingModeChanged`, `editor.closing` | |
| `editor.externallyModified` | `modified, reason (unmodified/modified/created/deleted)` |
| `editor.cursorMoved`, `editor.selectionChanged`, `editor.focusChanged` | `position` / `range` / `focused` |

`target` of an event is the document or view id it belongs to.

## Diagnostics

`editor.setDiagnostics` `{target (doc), owner?, version?, items:[{range,severity 1..4,message,code?,source?}]}`
replaces the list of one owner (several owners coexist, e.g. `lsp` and `build`); all or
nothing. An optional nonnegative integer `version` must match the document on the
UI thread; stale results fail with `TKDE_E_FAILED`, changing nothing.
Wavy underlines (dotted for hints) follow the text through edits, gutter marks use
mark types 6..9 and the colours come from the application palette, so they are legible on a
dark and a light scheme and follow a palette change. `editor.getDiagnostics` `{target, owner?}`
returns the current (moved) ranges. Hover shows the messages (HTML-escaped).

## Completion and hover

`editor.enableCompletion` `{target (view), triggers?, minWordLength?}` /
`editor.disableCompletion`; `editor.requestCompletion` or typing raises
`editor.completionRequested {req, word, wordText}`; the logic answers with
`editor.setCompletions {target, req, isIncomplete?, items:[{label,kind?,detail?,documentation?,insertText?,insertTextFormat?,textEdit?,sortText?,additionalTextEdits?}]}`.
An answer to an older `req` is ignored (`stale:true`, not an error). Accepting an item replaces
the word and applies `additionalTextEdits` in one undo step (`editor.completionAccepted`).
`editor.hoverRequested` carries the position under the mouse; `editor.showTooltip`
`{target, position, markup}` answers it. `editor.showAssistance`
`{target, kind: "hover" | "signatureHelp", req, version, position, result}` renders
LSP rich text or an active-parameter signature. Stale native versions, hover
request numbers and signature cursor positions are ignored (`stale:true`).
See [the LSP client](lsp-client.md#editing-assistance-26) for the asynchronous presenter.
Newline insertions/removals are included in `editor.changed` alongside text edits,
including multiline replacement, undo and redo.

## Notes for maintainers

* Moving ranges belong to their creator since KF 6.9. The bridge frees the diagnostics ranges in
  a release hook (`Bridge::addReleaseHook`) while the document is still alive.
* The end of line has no public config key: `setEol` and the `set_eol` select action of a view
  are used.
* `TKDE_DEBUG_COMMANDS=1` adds `editor.debug.*` test hooks (typing, palette, completion state).
