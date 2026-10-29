# Edit and navigate

## Files and saving

Activate a file in the project tree, use Open File (`Ctrl+O`), or Go to File…
(`Ctrl+P`). Quick Open searches the project; the command palette is `Ctrl+Shift+P`.
The tree loads folders lazily and respects supported `.gitignore` patterns.
Build output and version-control internals stay out of the normal file index.

Use `Ctrl+S` to save, `Ctrl+Shift+S` to save all modified files, and the ordinary
KDE undo/redo shortcuts for text edits. Modified tabs are marked. Closing modified
files or quitting asks whether to save, discard or cancel. Autosave and backup
copies are optional preferences.

Split Right (`Ctrl+\`) and Split Down (`Ctrl+Shift+\`) show two views of the
same buffer. Next/Previous Tab use `Ctrl+PgDown` / `Ctrl+PgUp`. The project and
bottom docks can be toggled with `Alt+1` and `Alt+2`.

## Language assistance

With a working tinox-lsp, the editor offers completion, hover information,
parameter help and live diagnostics. Activate a diagnostic in Problems to visit
its position. Check/build diagnostics are kept in the separate Build Problems tab.

| Command | Default shortcut |
|---|---|
| Go to definition | `F12` or Ctrl+click |
| Find references | `Shift+F12` |
| Back / Forward through locations | `Alt+Left` / `Alt+Right` |
| Workspace symbols | `Ctrl+T` |
| Rename symbol | `Ctrl+F2` |
| Format document | `Ctrl+Shift+I` |
| Code actions | `Alt+Enter` |

The Outline and breadcrumb picker help navigate the current file. Multiple
navigation results appear in References / Rename. Double-click one to open it.
Supported commands depend on the server's advertised capabilities; unavailable
features report the missing capability instead of guessing.

Rename Symbol shows a preview before changing buffers. Apply Rename accepts it;
Cancel leaves the text untouched. Save changed files explicitly. Undo Rename
Across Files (`Ctrl+Alt+Z`) reverses the batch if no participating buffer has changed
since. File-resource operations are not silently applied; for a class rename,
follow the server's instructions about the corresponding filename. `F2` in the
project tree renames a file, rather than a symbol.

## Formatting and search

Format Selection and Organize Imports are also available in the Edit menu/palette.
Format-on-save is opt-in. If formatting fails, that save is cancelled and the
buffer stays open; fix the source or disable the preference to save it as-is.

Use Find in Files from the Search menu or command palette for project-wide text search.
Review replacement previews before applying them. The exact search and supported
ignore/regex behaviour are described in [search.md](../search.md).

KTextEditor's own configuration dialog controls fonts, indentation and editor
behaviour. Configure Shortcuts changes command bindings. See
[preferences](settings.md) for the IDE's settings.
