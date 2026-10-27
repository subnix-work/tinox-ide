# The `dialog`, `config` and `notify` namespaces

Implemented by the real bridge (`bridge/src/dialog/`, issue #13). Tests:
`bridge/tests/dialog_test.cpp` (offscreen, with a private `XDG_CONFIG_HOME`/`XDG_DATA_HOME` so no
test touches the user's files).

## Dialogs never block

Every dialog is shown with `show()` and the command returns at once with the dialog's object
`id`; the answer is an event `dialog.result {dialog, req?, accepted, ...}`. `req` is whatever the
caller passed and is echoed, so answers can be matched without keeping the id. Meanwhile the
bridge and Tinox keep running: commands, events and other windows work (the tests do exactly that
with a dialog open). A dialog without `parent` is application-modal, one with a `parent` (a widget
id or a mount id) is window-modal. Closing a dialog never counts as "the last window closed".
Dialogs delete themselves when closed; an `id` stops resolving then.

### Files

`dialog.openFile {title?, dir?, filters?, multiple?, parent?, req?}`, `dialog.saveFile {...,
defaultSuffix?, name?}`, `dialog.openDirectory {...}`. `filters` are Qt name filters
(`"Tinox (*.tnx)"`, `"All files (*)"`). Result: `files` (local paths, URLs for remote ones; empty
when cancelled) and `filter`. These are `QFileDialog`s: under Plasma the KDE platform theme turns
them into the KDE (KIO) dialog with places, bookmarks and remote locations; elsewhere Qt's own
dialog appears.

Recent files: define the KDE standard action `file_open_recent` (`action.define` with
`standard:true`), then `action.recentAdd {id, url, name?}`, `action.recentList {id}`,
`action.recentClear {id}`; choosing an entry raises `action.recentSelected {id, url}`. The list is
kept in the group `RecentFiles` of the file given to `action.configure` (default: the application's
config). KDE leaves files below the temporary directory out of the list.

### Messages

`dialog.message {kind, title?, text, details?, buttons?, default?, cancel?, dontAskAgain?, parent?, req?}`

* `kind`: `info` (default), `warning`, `error`, `question`/`yesNo`, `yesNoCancel`,
  `continueCancel`. Result `button` is `ok`, `yes`, `no`, `continue` or `cancel`; Escape and the
  window's close button answer `cancel`, else `no`. `accepted` is false for those two.
* `buttons: [{id, text, role: accept|reject|destructive|action}]` replaces the standard buttons;
  the result `button` is the id. `default` is the id with the focus, `cancel` the id Escape picks.
* `dontAskAgain: "key"` adds KDE's "Do not ask again" box (yesNo and continueCancel). The answer is
  stored where `KMessageBox` stores it (group `Notification Messages` of the application's
  config). When it is already stored the command replies `{suppressed:true}` without a dialog and
  the `dialog.result` event follows at once with `suppressed:true`. `dialog.resetDontAskAgain
  {key?}` switches one (or all) back on.

### Input and progress

* `dialog.input {kind: text|password|int|double|item, title?, label?, value?, min?, max?, items?,
  editable?}` -> `dialog.result {accepted, value}` (`value` is `null` when cancelled).
* `dialog.progress.open {title?, text?, min?, max?, cancelable?}` (`max: 0` is a busy indicator;
  not modal, so a background job does not lock the IDE; with a `parent` it is window-modal), then
  `dialog.progress.update {id, value?, text?, max?}` and `dialog.progress.close {id}`. Cancel
  raises `dialog.progressCanceled {dialog}`; closing it yourself does not.

### Settings dialog from a description

`dialog.settings {title?, file?, parent?, req?, pages:[{id, title, icon?, group?, fields:[...]}]}` builds a
`KPageDialog` (list on the left) with OK, Apply, Cancel and Restore Defaults.

A field is `{key, label, type, default, group?, toolTip?}` with `type`: `bool`, `string`, `int`
(`min`, `max`), `double` (`min`, `max`, `decimals`), `choice` (`choices`: strings or `{value, text}`)
or `stringList` (comma separated in the dialog). The `default` is required and has the field's type.
The setting is stored as `group`/`key` (the field's `group`, else the page's, else `General`) in
`file` (default: the application's config). The whole description is validated first.

Apply (and OK) write only the settings that differ from what is stored (a value that equals its
default and is not stored yet stays out of the file) and raise `dialog.settingsApplied {changes:
{"Group/key": value}}` so the logic can react at once. Restore Defaults changes the widgets only.
`dialog.result` carries `accepted` and `applied`.

## `config`: KConfig by group and key

The default file (`file` omitted) is the application's own: for the application `tinoxide` that
is `~/.config/tinoxiderc`. A bare name is another file in the config directory; an absolute path
is used as it is. Groups nest with `/` (`"Editor/Fonts"`). Writes go to disk at once.

* `config.read {file?, group, key, type?, default?}` -> `{exists, value}`. `type` is `string`,
  `bool`, `int`, `double`, `stringList` or `color`; without it the type of `default` is used, and
  one of the two is required. A missing key gives the default.
* `config.write {file?, group, key, value, type?}`, `config.writeMany {file?, entries:[{group,
  key, value, type?}]}` (all or nothing). Numbers that are whole are stored as integers; a list
  holds strings; `type: "color"` stores a colour.
* `config.readGroup {file?, group}` (stored text of every key), `config.groups {file?, group?}`,
  `config.delete {file?, group, key?}` (the group when no key), `config.info {file?}` -> `{name,
  path}`.
* `config.watch {file?}` / `config.unwatch`: a `KConfigWatcher`. When another process (System
  Settings, `kwriteconfig6 --notify`) changes the file, `config.changed {file, group, keys}` is
  raised. Writes made by the bridge itself are not reported back.

The files are ordinary KConfig files; other KDE tools read them.

## `notify`: KNotification

`notify.send {event, title?, text?, icon?, urgency?, actions?, component?}` posts a notification
for an event defined in `resources/notifications/tinoxide.notifyrc` (`buildFinished`,
`buildFailed`, `testsFinished`, `testsFailed`, `languageServerCrashed`; installed to
`$XDG_DATA_HOME/knotifications6/` by `scripts/install-resources.sh`). `urgency` is low, normal,
high or critical. `actions: [{id, text}]` become buttons; a click raises `notify.action {id,
action}`, dismissal `notify.closed {id}`. The component defaults to `tinoxide`, which is what lets
users configure each event in System Settings.

## Notes

* TSan: `make test-tsan` leaves the `dialog` test out. Qt starts and ends detached helper threads
  (QFileSystemModel, the paint thread pool), and the TSan runtime aborts in its thread registry
  when a later thread reuses an id. ASan/UBSan/LSan run it.
* `TKDE_DEBUG_COMMANDS=1` adds `dialog.debug.act {id, act, value?}`: state, accept, reject,
  button, select, value, check, field.
