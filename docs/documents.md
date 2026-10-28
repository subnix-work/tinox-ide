# Documents (`tinox.ide.editor`)

The open files of the IDE: one tab per file in the editor area of the main window.

| Class | Job |
|---|---|
| `Documents` | The list of open files, the tab nodes, save / close / split / reload logic, the session. |
| `OpenFile` | One tab: path, `Document`, one or two `EditorView`s, modified / missing flags, last cursor. |

## What the user gets

* **Open** (`file_open`, Ctrl+O), a double click in the project tree (bus message `file.openRequested`),
  drag-and-drop is left to the tree. A file that is open already is only switched to.
* **Save** (`file_save`), **Save As** (`file_save_as`), **Save All** (`file.saveAll`), **Reload from Disk** (`file.reload`).
* **Close** (`file_close`, the tab's close button, middle click), **Close All Files** (`file.closeAll`). A modified file asks
  *Save / Discard / Cancel*; Cancel stops a Close All.
* **Tabs** can be dragged to reorder them (`widget.tabMoved`). A modified file shows a save icon on its tab, a file that
  vanished from disk a warning icon; the tool tip is the full path.
* **Split** (`view.splitRight`, `view.splitDown`, `view.unsplit`): a second `EditorView` on the *same* `Document`, so both
  views show the same text, undo history and modified state. The tab then contains a splitter with both embedded views.
* **Next / previous tab** (`tab.next`, `tab.previous`).
* **Quit** (`file_quit`, window close button) asks about each modified file; Cancel keeps the IDE running.

## Changes behind the editor's back

`Document.onExternalChange` (KTextEditor's modified-on-disk detection) drives this:

| File on disk | Unmodified in the editor | Modified in the editor |
|---|---|---|
| changed / created | reloaded silently (`editor.loadDocument`) | asks *Reload* or *Keep My Changes* |
| deleted | tab is marked missing | tab is marked missing (the text stays and can be saved again) |

Renames, moves and deletes made in the project tree (`workspace.fileOperation`) retarget the open document
(`editor.loadDocument {url}`; unsaved text is kept) or mark it missing.

## Session

`Documents` writes the session to the application's config (`~/.config/tinoxiderc`) at most once a second, when something changed,
so a crash or `kill` loses at most that second:

| Group / key | Content |
|---|---|
| `Session/files` | string list, one entry per tab: `path<TAB>line<TAB>column<TAB>split` (`split` is empty, `right` or `down`) |
| `Session/active` | path of the current tab |
| `Session/workspace` | project folder the session belongs to; it is restored only when the same folder is open |

The window geometry, docks and splitter sizes are saved separately by `MainWindow.saveState` on a clean quit.

## Options (`Editor` group of the config)

| Key | Default | Meaning |
|---|---|---|
| `autosave` | `false` | save modified files that exist on disk every `autosaveSeconds` |
| `autosaveSeconds` | `30` | |
| `backup` | `false` | before every save copy the old file to `name~` |

## Store keys and bus messages

Store: `editor.count`, `editor.active` (-1 for none), `editor.hasActive`, `editor.activeModified`, `editor.activeSplit`,
`editor.modifiedCount`, `editor.cursor` (`Ln 3, Col 5`), `editor.revision` (the window draws again when it changes).

Bus, heard: `file.openRequested {path}`, `workspace.fileOperation`, `editor.tick` (once a second, from a background task
started by `Documents.startTicker`). Sent: `editor.opened {path}`, `editor.closed {path}`, `editor.saved {path}`,
`editor.activeChanged {path}`.

## Tests

`tests/DocumentsTest.tnx` runs the whole application against the mock bridge with real temp files: open, modified marker,
close prompts, reorder / middle click, split, external changes, file operations, session round trip, quit. Two views of one
document staying in sync is the bridge's `EditorView` contract (`bridge/tests/editor_test.cpp`).

Document changes are also published as `editor.changed {path, version, changes}`
(sequential UTF-16 TextEdit JSON), reloads as `editor.reloaded {path}`, and path
changes as `editor.retargeted {oldPath, path}`. `editor.closing {path}` is emitted
before release or retarget, so language services clear marks while the old
document identity still exists. See [diagnostics.md](diagnostics.md).
