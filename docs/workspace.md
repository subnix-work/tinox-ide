# The workspace (`tinox.ide.workspace`)

The open project folder, its tree and the file operations. Sources: `src/tinox/ide/workspace/`; the wiring is
`Workspace` (state and commands) with `FileTree`, `FileOps`, `WorkspaceWatcher`, `GitStatus`, `IgnoreRules`,
`TinoxManifest` and `FileSystem` as its parts.

## What it does

* **Open folder / recent projects.** File ▸ Open Folder… (`Ctrl+Shift+O`), File ▸ Open Recent (KDE's recent-files
  action, kept in `tinoxiderc`), the "Open Folder…" button of the empty project panel, a folder on the command line
  (`tinox-ide /path`), `TINOXIDE_OPEN=/path`, or the project that was open last time. The `tinox.toml` of the folder
  is read (`TinoxManifest`: name, entry, dependencies, source roots `src`/`tests`/`examples`); a folder without one is
  a project too. Opening another folder closes the first.
* **The tree is lazy.** Opening a project lists the top level only; a folder is listed when the user opens it
  (`Model.onFetch`). A repository with thousands of files opens in a few milliseconds (measured: the Tinox
  repository, 3190 files, 11 ms). Folders first, then names without regard to case; rows have Breeze icons by file
  type; rows can be dragged and renamed in place.
* **Ignore rules.** Version-control and build output (`.git`, `.hg`, `.svn`, `.tinox`, `.tinox-build`, `target`) and
  everything the project's `.gitignore` files say (root and nested ones, read as folders are opened; `*`, `**`, `?`,
  `!`, anchored and directory-only patterns). Not supported: character classes, `core.excludesFile`,
  `.git/info/exclude`.
* **File operations** (File menu and the tree's context menu): New File…, New Folder…, Rename… (`F2`, or edit the name
  in the tree), Delete… (asks: Move to Trash / Delete Permanently / Cancel), drag and drop to move within the tree,
  files dropped from the file manager are copied in. Names are checked (no slash, not `.`/`..`), targets that exist
  are never overwritten, a folder cannot go into itself. The status line says what happened and how to take it back:
  create, rename and move go on an undo stack (File ▸ Undo File Operation; undoing a create moves the file to the
  trash rather than deleting it); deleting goes to the trash, which is the way back. Failures are explained in a
  dialog.
* **External changes.** A background task watches the folders the tree has opened (inotify, one non-recursive watch per
  open folder, so the system's watch limit is not used up by `target/` and the like). A change re-reads exactly that
  folder and applies the difference to the model (new rows at their sorted place, vanished rows removed, other rows
  untouched, so expansion and selection survive). Bursts (a git checkout) arrive as one batch. If watching is not
  possible the task polls every three seconds and re-reads everything that is open.
* **Git status.** `git status --porcelain` runs in the background after opening and after changes; modified/added/deleted
  files are orange, untracked green, conflicts red, and a folder takes the strongest status of what is inside.
  Without git or outside a repository there is simply no colour.

## For other modules

State in the store: `workspace.open`, `workspace.path`, `workspace.name`, `workspace.entry`, `workspace.hasManifest`,
`workspace.selection` (newline separated), `workspace.selectionCount`, `workspace.canUndo`.

Bus topics: `workspace.opened {path, name, hasManifest, entry}`, `workspace.closed`,
`workspace.fileOperation {kind: create|rename|move|copy|delete, path, oldPath}` (open editors follow renames and
deletions), `workspace.fsChanged {paths}` (from the watcher), `workspace.gitStatus {output}`,
`file.openRequested {path}` (the user activated a file in the tree or created one: the document module opens it).

## Workarounds that belong to the standard library

`FileSystem` is the one class that touches the disk, and it works around what `tinox.core.fs` cannot do yet
(subnix-work/tinox#508): a directory test through `fileExists(path + "/")`, and `mv`, `cp -R`, `gio trash`, `rm -r`
started with argument lists (never through a shell, so odd names are safe). When #508 lands only that class changes.
Another compiler bug shaped the code: `continue` inside `for` loops does not work (subnix-work/tinox#513), so the
parsers use `while` loops or guards.

## Tests

`tests/FileSystemTest` (paths, real rename/copy/delete, odd names, globs and ignore rules), `WorkspaceTest`
(manifest, tree listing and sorting, git parsing, file operations and undo), `WorkspaceAppTest` (the module inside the
application on the mock bridge: opening, lazy folders, external changes, dialogs, selection and context menu, drag and
drop, the real watcher end to end, git on a real repository). `scripts/test-example.sh` opens this repository with the
real bridge and checks that it takes under a second.
