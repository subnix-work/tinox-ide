# The `widget` namespace (declarative widget tree)

Implemented by the real bridge (`bridge/src/widget/`, issue #9). Tests:
`bridge/tests/widget_test.cpp` with golden files in `bridge/tests/golden/widget/`.

Tinox describes the UI as a tree; the bridge builds the QWidgets and, on every update,
changes only what differs. The tree is checked completely **before** anything is built or
touched: an error rejects the whole command (`TKDE_E_INVALID_ARG` with the node and prop named)
and the widgets stay as they were.

## Node

```json
{"type":"QLabel", "key":"title", "props":{"text":"Hello"}, "place":{}, "layout":{}, "children":[]}
```

* `key` (required, unique within one tree): the identity for the diff, the node's
  `objectName` and the name in events. Keys are what the logic uses, ids what the ABI uses.
* `props`: a table of setters per type; an unknown prop, a value of the wrong JSON kind or a
  string outside the allowed choices is an error. A prop that disappears from an update goes back
  to its default.
* `place`: how the **parent** holds this child (see below). It is not a prop of the child.
* `layout` (QWidget only): `{"kind":"box"|"grid"|"form", "orientation", "margins", "spacing"}`.

### Types

`QWidget`, `QMainWindow`, `QDockWidget`, `QSplitter`, `QTabWidget`, `QToolBar`, `QStatusBar`,
`QLabel`, `QLineEdit`, `QPushButton`, `QCheckBox`, `QComboBox`, `QTreeView`, `QListView`, `QTableView` (item views; their models come from
the `model` namespace, see model.md), `QPlainTextEdit` (read-only log, `widget.append`), `QStackedWidget`,
`QScrollArea`, and `Embed`: `{"type":"Embed","key":"editor","object":<id>}` places a widget that
another module owns (an editor view, a terminal). The tree never deletes an embedded widget;
removing the node or releasing the tree hands it back, unparented.

Every widget also has `enabled`, `visible`, `toolTip`, `statusTip`, `accessibleName`,
`accessibleDescription` (what a screen reader announces; see [accessibility.md](accessibility.md)), `minimumSize`,
`windowTitle`, `windowIcon`. Icons are Breeze icon names (`QIcon::fromTheme`). Programmatic prop
changes never raise events. See `bridge/src/widget/widget.cpp` for each type's props.

### `place`

| Parent | `place` of the child |
|---|---|
| `QMainWindow` | `QDockWidget`: `area` left/right/top/bottom; `QToolBar`, `QStatusBar`: by type; anything else is the central widget |
| `QTabWidget` | `tab: {title, icon, toolTip}` |
| `QSplitter` | `stretch` |
| `QStatusBar` | `permanent`, `stretch` |
| box layout | `stretch` |
| grid layout | `row`, `col`, `rowSpan`, `colSpan` |
| form layout | `label` |

## Commands

| Command | Fields | Result |
|---|---|---|
| `widget.mount` | `tree`, `parent?` (widget id) | `id` (the mount), `root`, `nodes` (`key` to object id) |
| `widget.update` | `target` (mount), `tree` | `root`, `nodes`; the root keeps key, type and layout kind |
| `widget.show` / `hide` / `focus` | `target` (mount or node id) | |
| `widget.close` | `target`, `force?` | without `force` a `QMainWindow` raises `widget.closeRequested` instead of closing |
| `widget.append` | `target` (QPlainTextEdit), `text`, optional `ansi`, `clear` | Plain: appends `text` as a new line. With `ansi` the text is streamed in as written (no implicit newline) and SGR colours/bold are decoded, also across writes; `clear` empties the log first |
| `widget.saveState` / `restoreState` | `target` (mount), `group?`, `file?` | geometry, `QMainWindow::saveState` (docks and toolbars are found by their key), splitter sizes, in a `KConfigGroup` (default: the application's config). `restoreState` answers `restored` |

Releasing the mount id (`tkde_release`) destroys the whole tree. The ids in `nodes` are object
ids like any other and stop resolving when their node goes. Closing the last visible top-level
window ends the loop (`docs/bridge-core.md`); a main window asks first through `closeRequested`.

## Events (`target` is the node's id, `key` its key)

`widget.clicked {checked}`, `widget.textChanged {text}`, `widget.returnPressed`,
`widget.currentChanged {index}` (combo box, tabs, stack), `widget.tabMoved {from, to}` (the user dragged a
tab), `widget.closeRequested {index?, via?}` (window close button; tab close button with its index;
`via: "middle"` when a tab was closed with the middle mouse button), `widget.dockVisibilityChanged {visible}`,
`widget.splitterMoved {sizes}`, `widget.navKey {direction}` (`up`, `down`, `pageUp`, `pageDown` in a
`QLineEdit` with `navKeys: true`, which then does not move its cursor), `widget.dismissed` (a popup was hidden).

### Popups

A top-level `QWidget` with `popup: true` is a `Qt::Popup`: `widget.show` centres it horizontally in the upper
part of the active window, Escape or a click outside hides it and raises `widget.dismissed`. Quick open and the command
palette are such popups (`tinox.ide.search.Picker`).

## The diff

Children are matched by key. A child with the same key and type keeps its widget (and id) and gets
only the props that changed; a different type under the same key is a new widget. New children are
built, missing ones destroyed. Parents that care about order (layouts, splitter, tabs, stack,
toolbar) take their children out and put them back in tree order when anything moved, which keeps
every widget and its state; main windows, docks and scroll areas place children by role and
touch only the child that changed. `currentIndex` and `sizes` are applied after the children
exist.

## Notes

* Qt styles everything: the bridge sets no colours or style sheets, so Breeze light and dark
  both apply as the platform theme says. Offscreen tests cannot judge the rendering; it has
  to be looked at on a Plasma session (tracked for the IDE skeleton).
* `TKDE_DEBUG_COMMANDS=1` adds `widget.debug.dump` (the real Qt state behind a tree, used by the
  golden tests) and `widget.debug.act` (click, type, change an index... as a user would).
* Regenerate goldens with `UPDATE_GOLDEN=1 bridge/build/bin/widget_test` and review the diff.
