# The `model` namespace (item models fed from Tinox)

Implemented by the real bridge (`bridge/src/model/`, issue #11). Tests:
`bridge/tests/model_test.cpp`. `QAbstractItemModelTester` is attached to the models in the
tests, so every change a command makes is checked against Qt's model contract while it happens.

One generic `TinoxModel : QAbstractItemModel` per model id. The data lives in the bridge as plain
rows (nothing points into Tinox memory); Tinox changes it with commands and hears about
user actions as events. Models, proxies and views are addressed by object id; rows by a string
`id` that Tinox chooses (a path, a symbol id, ...), unique within the model.

## Rows

```json
{"id":"src/Main.tnx", "text":"Main.tnx", "icon":"text-x-generic", "toolTip":"...",
 "checked":false, "color":"#ff0000", "bold":true,
 "flags":{"selectable":true,"enabled":true,"editable":false,"drag":false,"drop":false},
 "cells":["1 KB", {"text":"x","icon":"...","toolTip":"..."}],
 "hasChildren":true, "children":[ ...rows... ]}
```

* `checked`: `true`/`false` shows a checkbox, `null` (or absent) none.
* `cells`: the text of columns 1..n (a string or `{text, icon, toolTip}`); column 0 is the row.
* `children` given: loaded. `hasChildren:true` without `children`: loaded on demand (below).
* Icons are Breeze icon names. A row with an unknown field, a bad colour or a duplicate id rejects
  the whole command, nothing is applied.

## Commands

| Command | Fields |
|---|---|
| `model.create` | `columns?` (headers), `lazy?` (the top level is fetched on demand), `acceptDrops?` -> `id` |
| `model.reset` | `target`, `rows`: replace everything (ids may be reused) |
| `model.insert` | `target`, `parent?` (id, absent/null = top level), `index?` (append), `rows` |
| `model.remove` | `target`, `ids` (subtrees go with them) |
| `model.move` | `target`, `id`, `parent?`, `index?`; a row cannot move into itself |
| `model.update` | `target`, `id`, `fields` (any row field except `id`/`children`) -> `dataChanged` |
| `model.setChildren` | `target`, `parent?`, `rows`: answer to a fetch, or a refresh of a folder |
| `model.get` / `model.info` | one row (`parent, index, count, children, fetched, hasChildren`) / counts |

## Lazy loading

A row with `hasChildren:true` and no children shows an expand arrow. When a view opens it, the
model raises `model.fetchChildren {id}` (`id` is `null` for the top level of a `lazy` model),
once: no second request while one is pending. Tinox answers with `model.setChildren`. Nothing below
a closed row is ever requested. An empty answer clears the arrow.

## Sorting and filtering

`model.createProxy {source, filter?, sort?}` makes a `QSortFilterProxyModel` (it goes away with
its source). `model.setFilter {target, filter?, sort?}` changes it:
`filter: {text, regex?, column?, caseSensitive?, recursive?}` (recursive by default: a match keeps
its ancestors visible) and `sort: {column, order: "asc"|"desc", caseSensitive?}` (locale aware).
Sorting and filtering are dynamic, so source changes flow through. Rows that are not loaded yet
cannot match a filter.

## Views

Item views are widget-tree nodes (`QTreeView`, `QListView`, `QTableView`, props `selectionMode`
none/single/extended/multi, `dragDrop`, `editable`, `alternatingRowColors`; tree: `headerHidden`,
`uniformRowHeights`; table: header props; see widget.md).

* `model.attach {view, model}`: shows a model or proxy (`null` detaches). A view that outlives its
  model shows nothing.
* `model.select {view, ids, current?}`, `model.selection {view}` -> `{ids, current}`,
  `model.expand {view, id, expanded?}`, `model.scrollTo {view, id}`. Ids always are the source
  rows' ids, also through proxies; a row that is filtered out is `NOT_FOUND`.

## Events

`target` is the model for the first group and the view for the second (with its node `key`).

* model: `model.fetchChildren {id}`, `model.checkChanged {id, checked}` (a user toggled a
  checkbox; applied at once), `model.editRequested {id, column, text}` (an edit is a request: the
  row stays as it is until Tinox updates it, because a rename can fail), `model.dropRequested
  {ids, urls, parent, index, action}`.
* view: `model.selectionChanged {ids}`, `model.currentChanged {id}`, `model.activated {id}`
  (double click or Enter), `model.contextMenuRequested {id, x, y, ids}` (global position; `id` is
  `null` on empty space), `model.expanded {id}`, `model.collapsed {id}`.

## Drag and drop

Rows with `flags.drag` can be dragged (mime `application/x-tinox-rows`, a JSON list of ids); rows
with `flags.drop` (and the top level with `acceptDrops`) accept drops of those rows and of
`text/uri-list` (files from the file manager). The model never moves anything itself: a drop
raises `model.dropRequested` and Tinox moves the files, then calls `model.move`. That keeps the
view from deleting the source rows.

## Performance

Rows are plain structs, `data()` is O(1) and reset/insert announce one range. Measured offscreen
with a table view attached: 10 000 rows with three columns and icons reset in about 30 ms,
100 single inserts into them in about 440 ms (visible in the test output). Tree views want
`uniformRowHeights`.

## Tests

`TKDE_DEBUG_COMMANDS=1` adds `model.debug.pointer` (the model's address, for in-process tests
that attach `QAbstractItemModelTester`), `model.debug.rows`, `model.debug.data`,
`model.debug.drop`.
