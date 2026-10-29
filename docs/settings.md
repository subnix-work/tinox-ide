# Preferences (`tinox.ide.settings`)

| Class | Job |
|---|---|
| `Settings` | Reads and writes the application's config file (`~/.config/tinoxiderc`) through KConfig. |
| `PrefDef` | The definition of one preference: group, key, label, default, kind (`bool`, `int`, `text`, `path`, `choice`), range or choices. |
| `Preferences` | All definitions, their checks, and the live update: store key `pref.<group>.<key>` plus bus message `settings.changed {id, value}`. |
| `ToolProbe` | Finds `tinox` and `tinox-lsp` in `PATH`, `~/.cargo/bin`, `~/.local/bin`, `/usr/local/bin`, `/usr/bin`; checks a path (exists, not a folder, executable). |
| `SettingsDialog` | The window: pages on the left, fields on the right (command `options_configure`, Settings menu). |

## Behaviour

* **No OK button.** Every change is written to the config file and applied at once. A value that fails its check is
  refused: the old value stays in force, the field keeps what the user typed and shows `⚠ reason` below it.
* A path preference left empty means "look in the usual places"; the field then says which file is used or that the program was not
  found. `Preferences.toolPath("Tools.lspPath")` gives modules the program to run. A stored path that stops working is kept
  and flagged in the dialog.
* Two pages hold buttons instead of fields because KDE already has the dialogs: **Configure Editor…** (KTextEditor's own
  fonts, indentation, ... pages: `editor.showConfigDialog`, needs an open file) and **Configure Shortcuts…** (`KShortcutsDialog`).

## The preferences

| Page | Id (`group.key`) | Meaning | Applied by |
|---|---|---|---|
| General | `Workspace.reopenLast` | reopen the last project on start | `Workspace.restore` |
| General | `Session.restore` | restore the open files on start | `Documents.restoreSession` |
| General | `Editor.autosave`, `Editor.autosaveSeconds` (5-3600) | save modified files periodically | `Documents.tick` |
| General | `Editor.backup` | keep `name~` when saving | `Documents.save` |
| Editor | `Editor.formatOnSave` | format Tinox buffers before saving (off by default); failures cancel the save | `EditService` |
| Editor | `Editor.theme` | colour theme of the editor ("" follows the colour scheme) | `Documents.applyTheme` (all views, new views) |
| Language Server | `Tools.lspPath`, `Tools.lspArgs`, `Tools.tinoxPath` | the programs and extra arguments | `LanguageService` and `BuildService` |
| Build | `Build.args`, `Build.saveBefore`, `Build.showOutput` | build behaviour | `BuildService` |
| Appearance | `Appearance.toolbar` | show the tool bar | `MainWindow` (redraws on the store key) |

The Semantic Colours page adds optional `#RRGGBB` overrides for each supported token type;
empty values follow the theme. For the user workflow, see [manual/settings.md](manual/settings.md).

Adding a preference = one line in `Preferences.standard()` and a reader (store key or `settings.changed`).

## Tests

`tests/PreferencesTest.tnx`: defaults and checks, refused values change nothing, tool path checks and probing, live theme and
tool bar, and the dialog itself against the mock (a change in a field is applied; a bad path shows its reason and is not stored).
