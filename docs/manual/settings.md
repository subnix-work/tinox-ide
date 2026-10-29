# Preferences and troubleshooting

Open the configure command in the Settings menu or command palette. Preferences
apply immediately: there is no OK button. Invalid input keeps the old value and
shows the reason below the field. Native settings are stored in
`$XDG_CONFIG_HOME/tinoxiderc` (normally `~/.config/tinoxiderc`); Flatpak uses its
application-specific config directory.

| Page | Main settings |
|---|---|
| General | Reopen last project, restore open files, autosave, backup copies |
| Editor | Colour theme and format Tinox files before saving |
| Language Server | tinox-lsp path/arguments and Tinox compiler path |
| Build | Extra compiler arguments, save before build, show output on start |
| Shortcuts | KDE shortcut configuration |
| Appearance | Show/hide toolbar |
| Semantic Colours | Optional `#RRGGBB` overrides per semantic token type; empty follows the theme |

Reopen project/session are enabled by default. Autosave, backup copies and
format-on-save are disabled by default. Autosave's interval defaults to 30 seconds
and accepts 5–3600 seconds. Backups use the `name~` suffix.

The Editor page also opens KTextEditor's own configuration dialog for font,
indentation and other editor settings; open a file first. The IDE's theme choice
applies to existing and new editor views. Semantic colours follow theme/palette
changes unless explicitly overridden.

## If something does not work

| Symptom | Check |
|---|---|
| No completion, hover or live diagnostics | Verify tinox-lsp is installed, executable and selected on Language Server; use a server with the required capabilities |
| Build cannot start | Open the manifest's folder, verify the compiler path and install dependencies with `tinox install` |
| Source checkout fails to compile | Match compiler, core and runtime revisions; follow the pinned revision in [packaging](../packaging.md) |
| Save is cancelled with a formatting error | Fix syntax/formatter errors or switch off format-on-save, then save again |
| Run uses older code | Save explicitly, wait for modified markers to clear, then Run and inspect Build Problems |
| A selected test runs other methods | The current selection runs the whole containing file |
| Terminal is read-only | Install Konsole for a native package, or use a host terminal |
| Flatpak cannot find a tool | Install it on the host session's PATH; keep the default tool names so the packaged wrappers are used |

Before resetting settings, close the IDE and make a copy of `tinoxiderc`. Removing
it loses saved preferences, layout, recent projects and session state. Report a
reproducible problem with the IDE/toolchain versions, relevant output and steps;
[CONTRIBUTING](../../CONTRIBUTING.md) explains which repository owns it.
