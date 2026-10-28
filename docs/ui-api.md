# The UI API (`tinox.ide.ui`)

The typed Tinox layer over the bridge's namespaces (`widget`, `action`, `model`, `editor`).
The IDE core is written against these classes and never builds command JSON by hand.
It sits on top of the low-level bindings ([bindings.md](bindings.md)): `Bridge`,
`Command`, `Event`, `EventLoop`.

Reference documentation of every class and method is generated from the doc comments:
`make docs` writes `build/api/index.html` (`tinox doc`). `scripts/check-docs.py` (part of
`make test`) fails when a public class, method or field has no doc comment.

A complete small program is `examples/shell` (`make example`, code in
`src/tinox/ide/examples/ShellExample.tnx`); it is the best place to see the pieces together.

## The rules they all follow

* **State lives in Tinox, the bridge shows it.** Describe what the screen should look like,
  hand it over, and the bridge changes only what differs.
* **A description is checked before anything happens.** A wrong property, an unknown action
  id or an invalid range throws a `String` naming the object and what is wrong, and leaves
  the screen as it was (the bridge validates whole commands first).
* **Calls throw, handlers are isolated.** Commands that can fail are queries and throw. A
  handler that throws is recorded by the `EventLoop` (`events.failures`) and the loop goes on.
* **Events are routed by object.** Each wrapper registers the bridge id it owns with the
  `EventLoop` and delivers typed callbacks; setting a handler again replaces it.
* **Everything runs on the logic task.** Handlers are called there; do not block in them.

## Widgets: `Ui`, `Node`, `Mount`

```tinox
const mount = Mount::create(events)
mount.onClick("run", (ev: Event) => Main::run())
mount.onText("search", (text: String) => Main::search(text))
mount.render(
    Ui::window("main", "Tinox IDE")
        .add(Ui::toolBar("tools", "Main"))
        .add(Ui::dock("files", "Files", "left").add(Ui::treeView("tree")))
        .add(Ui::splitter("split", true).add(Ui::embed("editor", editor.id())).add(Ui::log("log")))
        .add(Ui::statusBar("status")))
```

* `Ui` has one constructor per widget type: windows, docks, splitters, tabs, stacks, tool and status
  bars, panels with box, grid or form layout, labels, buttons, check boxes, line edits, combo boxes,
  tree/list/table views, a log, and `embed` for a widget owned elsewhere (an `EditorView`).
* `Node` is the description of one widget: properties (`text`, `icon`, `enabled`, `sizes`, ...), how its
  parent holds it (`inTab`, `inArea`, `stretch`, `inCell`, `labelled`, ...) and children. Setters return
  the node. `Node.str/int/bool/strings/raw` set any property of the bridge by name.
* `Mount` puts a tree on screen. The first `render` creates the widgets, later ones update them:
  nodes are matched by **key**, so a widget keeps its state as long as its key stays. Events are
  delivered by key (`onClick`, `onText`, `onIndex`, `onCloseRequested`, `onActivated`,
  `onSelection`, `onContextMenu`, or `on(key, type, handler)` for any event). `idOf(key)` gives the
  bridge id (what menus, models and dialogs need). `saveState`/`restoreState` keep window geometry,
  docks and splitter sizes.

## Actions: `Action`, `Menu`, `Actions`

```tinox
const actions = Actions::create(events)
actions.define([
    Action::standard("file_open").onTrigger((ev: Event) => Main::open()),
    Action::create("build.run").text("&Run").icon("media-playback-start").shortcut("F5")
        .onTrigger((ev: Event) => Main::run())
])
actions.menuBar(mount.rootWidget(), [Menu::create("&File").action("file_open").separator().action("file_quit")])
actions.toolBar(mount.idOf("tools"), ["file_open", "-", "build.run"])
```

* `Action::standard` takes KDE's own text, icon and shortcut ("file_open", "file_save", "file_quit",
  "options_configure", "edit_find", ...). Shortcuts given to `Action` are defaults; the user's choice
  (`setShortcut`, or KDE's shortcut dialog via `showShortcutsDialog`) is saved and wins after a restart.
* State is pushed with `setEnabled/setChecked/setText/...` and never raises `onTrigger`.
* `list()` returns `ActionInfo` for every action (label, shortcut, enabled): the data of a command palette;
  `trigger(id)` runs one.
* `popup(ids, x, y)` shows a context menu of actions where the user clicked (`Mount.onContextMenu` gives the position).
* `loadUiFile`/`loadUiXml` build menus from a KXmlGui description instead of `Menu` objects.
* `Actions.create` takes over the `action.*` events of the loop.

## Models: `Row`, `Model`, `ModelProxy`, `ItemView`

```tinox
const files = Model::create(events, ["Name", "Size"], false)
files.onFetch((parent: String) => Main::listDirectory(parent))   // children of a folder, when it is opened
files.reset([Row::create("src", "src").icon("folder").lazy()])
files.attach(mount.idOf("tree"))
```

* A `Row` has an id chosen by Tinox (a path, a symbol id), text, icon, tool tip, check state, cells for further
  columns, flags (editable, draggable, droppable), children, or `lazy()` for children loaded on demand.
* `Model` keeps the rows in the bridge: `reset`, `insert`, `append`, `remove`, `move`, `update`, `setChildren`,
  `count`, `contains`, `childIds`. Handlers: `onFetch` (answers lazy children; nothing below a closed row is ever
  asked for), `onCheck`, `onEdit` (a request: the row stays until Tinox updates it), `onDrop` (a request: Tinox
  moves the files, then `move`).
* `ModelProxy` sorts and filters; ids stay the model's. `ItemView` selects, expands and scrolls a view node.

## Editor: `Document`, `EditorView`, `Position`, `Range`, `TextEdit`, ...

```tinox
const doc = Document::open(events, "/project/src/Main.tnx", "")
doc.onChange((change: Change) => Main::didChange(change))
const view = EditorView::create(events, doc)
view.enableCompletion(provider, [".", "::"], 2)
doc.setDiagnostics("lsp", [Diagnostic::error(Range::create(3, 4, 3, 9), "unknown name").withSource("tinox")])
```

* Positions are the Language Server Protocol's: zero-based line, UTF-16 code units. `Change` carries the
  `version`, the `origin` ("user" or "command") and the edits in the order that `didChange` wants.
* `Document.edit` applies a list of `TextEdit`s as one undo step against the text before the edits; an invalid
  list changes nothing. `Document` also covers files (`open`, `save`, `saveAs`), encoding, line endings, syntax
  mode and diagnostics (squiggles, gutter marks and hover text, colours following the colour scheme).
* `EditorView` is the widget (embed it with `Ui::embed`): cursor, selection, undo, theme, tooltips, and
  completion through the **`CompletionProvider` interface**: `fn complete(request: CompletionRequest) ->
  List<CompletionItem>`. A provider that throws answers with no items and the failure is reported, so the popup
  never waits for an answer that will not come.

## Position conversion: `Utf16`

`Utf16::position(text, byteOffset)` and `Utf16::offset(text, position)` convert
between UTF-8 bytes and the editor/LSP UTF-16 position contract. Invalid positions,
UTF-8 boundaries and surrogate midpoints throw. `Utf16::uri(path)` encodes a file
URI. These helpers are shared with the [LSP client library](lsp-client.md).

## Terminal: `Terminal`

`Terminal::create(events, directory, "auto")` loads Konsole or a read-only ANSI
console. Embed `terminal.id()` with `Ui::embed`; `sendText`, `setTitle`,
`setDirectory`, `onFinished` and `release` control its lifetime. See
[terminal.md](terminal.md) for the bridge protocol and fallback behaviour.

## Dialogs: `Dialogs`

`Dialogs::create(events)` wraps the bridge's dialogs so that none blocks: the call returns, the dialog is up, and a
handler runs when it is answered. `chooseFolder`, `chooseFile`, `askText` (accepted + text), `confirm` (yes/no),
`ask` (your own buttons, answered with the button's id), `inform` and `error`. Answers are matched by request number,
so many dialogs can be open at once.

## Application structure

How these classes are used in the application (bus, store, command registry, main window) is in
[ide-core.md](ide-core.md).

## Not wrapped here

`config.*` and `notify.*` are used through `tinox.ide.settings.Settings` (typed reads and writes) and `Command`;
their messages are documented in [dialog.md](dialog.md).

## Language notes

Found while writing it, in addition to those in [bindings.md](bindings.md): `select` is a reserved word
(hence `ItemView.choose` and `EditorView.setSelection`); a zero-argument lambda `() => ...` is not accepted
where a field is initialised (handlers take their object or an event instead); a line that starts with an
operator (`+`, `&&`, `-`) continues nothing, put the operator at the end of the previous line; `List` has no
`remove` and `String` no `isEmpty` (use `== ""`).

## Tests

All of it is tested against the mock bridge (`tests/UiTest.tnx`, `ActionsTest`, `ModelTest`, `EditorTest`,
`ShellExampleTest`): the exact JSON of every command, event routing, error paths and the failure behaviour of
handlers. `scripts/test-example.sh` runs the example on the real bridge, headless.
