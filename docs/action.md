# The `action` namespace (KDE actions, menus, toolbars, shortcuts)

Implemented by the real bridge (`bridge/src/action/`, issue #10). Tests:
`bridge/tests/action_test.cpp` (offscreen; shortcuts are exercised with real key presses via
QTest on the UI thread).

There is one `KActionCollection` per application, owned by a `KXMLGUIClient`, so a shipped `.rc`
file, the shortcut dialog and KDE's own persistence all work on the same actions. Actions are
addressed by string id, not by object id.

## Defining actions

`action.define {actions:[{id, text?, icon?, shortcut?|shortcuts?, checkable?, checked?, enabled?, visible?, group?, toolTip?, statusTip?, standard?}]}`

* The batch is validated first; one bad descriptor (unknown field, wrong kind, a shortcut that is
  not a key sequence, duplicate id, `standard` with a name KDE does not know) rejects all of it.
* Defining an id again updates the fields that are given.
* `standard: true` creates the KDE standard action named by the id (`file_open`, `file_save`,
  `file_quit`, `options_configure`, `edit_find`, ...) with the platform's translated text, icon
  and shortcut, which is what gives the KDE HIG naming and the user's standard shortcut scheme.
  Fields in the descriptor still override.
* `text` uses `&` mnemonics (`&Run`). The list returns both `text` and `label` (without `&`).
* `group` makes the action checkable and exclusive within that group (radio behaviour).
* `shortcut`/`shortcuts` are the **defaults** (portable text such as `Ctrl+Shift+F`).

Other commands: `action.set {id, text?, icon?, enabled?, checked?, visible?, toolTip?, ...}`
(state pushed by the logic never raises `action.triggered`), `action.remove {id}`,
`action.trigger {id}` (what a command palette calls; a disabled action fails),
`action.list` returning `{actions:[{id, text, label, icon, shortcut, shortcuts,
defaultShortcut, enabled, visible, checkable, checked, toolTip, group, standard}]}` for the
palette.

## Events

`action.triggered {id, checked}` for every trigger: menu, toolbar, shortcut or `action.trigger`.
`action.shortcutChanged {id, shortcuts}`, `action.shortcutsSaved` (shortcut dialog saved).

## Menus, toolbars, context menus

Menus come from a description; the alternative is a `.rc` file (below). Do not mix both for one
window.

* `action.setMenuBar {target: window, menus:[{id?, title, items}]}`; an item is an action id,
  `"-"` (separator), `{"section":"Title"}` or `{"menu":{title, icon?, items}}` (submenu, up to 8
  levels). Titles carry mnemonics (`&File`). Every id must be defined, or nothing changes.
  KDE HIG order: File, Edit, View, Go, Tools, Settings, Help; standard actions in their standard
  places.
* `action.setToolBar {target: toolbar node id, items}`: replaces the actions this command put
  into the bar earlier; widgets the widget tree placed there stay.
* `action.setContextMenu {target: widget, items}`: the widget's `ActionsContextMenu`.
* `action.popup {items, x, y}`: shows a context menu of these items (action ids, `"-"`, sections,
  submenus as in `setMenuBar`) at a global screen position; it does not block, and choosing an entry
  raises `action.triggered` like any other.
* `action.loadUi {target: window, file | xml}`: builds menus and toolbars from a KXmlGui
  description (`tinoxideui.rc`) with this application's actions; once per window.

`target` may be a widget id or a `widget.mount` id (its root widget).

## Shortcuts

* `action.configure {file?, group?}`: where user shortcuts are stored (default: the
  application's config, group `Shortcuts`). Stored shortcuts are read on every `define`, so a
  user's choice wins over the default after a restart.
* `action.setShortcut {id, shortcuts:[...]}`: sets and persists; an empty list unbinds; `null` or
  no `shortcuts` returns to the default. A shortcut another action uses is refused (the message
  names it).
* `action.showShortcutsDialog {parent?}`: opens KDE's `KShortcutsDialog` without blocking;
  saving writes the same config and raises `action.shortcutsSaved`.

## Tests

`TKDE_DEBUG_COMMANDS=1` adds `action.debug.menus {target}` (the menu bar, toolbars or action list
of a widget as nested JSON).
