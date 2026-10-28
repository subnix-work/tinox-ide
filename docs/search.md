# Quick open, command palette, find in files (`tinox.ide.search`)

| Class | Job |
|---|---|
| `Fuzzy` | Scores a query against a text: letters in order, runs and word starts rank higher. |
| `FileIndex` | The project's files (ignore rules applied), rebuilt on demand after changes; `query` returns the best matches. |
| `Picker`, `PickItem` | The popup (text field above a list) shared by quick open and the palette. |
| `Navigation` | Commands `nav.quickOpen` (Ctrl+P) and `nav.palette` (Ctrl+Shift+P). |
| `FindInFiles` | The "Search" tab of the bottom panel, `search.find` (Ctrl+Shift+F), `search.replace` (Ctrl+Shift+H), `search.cancel`, `search.undoReplace`. |
| `FileSearch`, `SearchCounter`, `SearchMatch` | The background search task and its results. |

## Quick open

Ctrl+P lists the project's files. With nothing typed the open files come first. Typing filters by fuzzy match over the
relative path; a match in the file name beats one in the folders. Up/Down/PageUp/PageDown move the choice while the
cursor stays in the field, Enter or a double click opens the file (bus message `file.openRequested`, answered by
`Documents`). On a project of 10 000 files a query takes a few milliseconds (`tests/SearchTest.tnx` checks < 50 ms).

The index is not kept up to date incrementally. Changes on disk (`workspace.fsChanged`, `workspace.fileOperation`) only mark
it stale and the next query walks the folder again (3 000 files take about 10 ms), which keeps it simple and always correct.

## Command palette

Ctrl+Shift+P lists the commands that are enabled now. With nothing typed the ones run from the palette last come first (the
list, twenty long, is kept in the settings: `Palette/recent`); while typing a recently used command beats an equally good match.
Standard KDE commands show their action name as words, since the bridge owns their real texts.

## Find in files

The query runs in a background task (`FileSearch`), so the window stays responsive:

* With **ripgrep** installed it runs `rg --json`; otherwise a built-in search reads the files (same ignore rules, same results,
  plain text or regular expressions through `tinox.core.regex`).
* Matches stream to the results tree in batches (`search.batch` on the bus) grouped by file. Typing a new query or **Cancel**
  ends the running search within a tenth of a second (the control flag is polled; ripgrep is killed).
* The search stops at 5 000 matches and says so.
* A double click on a result opens the file at that line and column.
* Options: match case, regular expression. A bad regular expression is reported in the status line.

### Replace in files

Type the replacement and press **Replace All**: a confirmation dialog shows the count and the first 15 changes
(`- old line` / `+ new line`). Lines that changed since the search are skipped and counted. Files that are open in the editor
get the change as an edit that stays unsaved (so the editor's own undo works), other files are written. The replacement is
literal text (no `$1` references). **Undo Replace in Files** (`search.undoReplace`) restores every file that still has exactly the
text the replacement wrote.

## Workarounds that belong to the standard library

* `String.substring` is byte-based and cannot take a multi-byte character apart, so fuzzy matching first replaces every non-ASCII byte
  by `_` (`Fuzzy.lower`): subnix-work/tinox#566.
* `String.indexOf(needle, from)` ignores `from`: `TextScan.indexFrom`, subnix-work/tinox#567.

* `Regex::*` ends the whole program on an invalid pattern and has no case-insensitive flag: the built-in search checks
  the pattern conservatively first (`FileSearch.looksValid`) and lower-cases both sides when ignoring case (so escapes like `\S`
  turn into `\s`; ripgrep does not have this limitation): subnix-work/tinox#568.

## Store keys

`search.status`, `search.running`, `search.canReplace`, `search.canUndo`.
