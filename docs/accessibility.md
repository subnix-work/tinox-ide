# Accessibility and KDE HIG review

Issue #32. Qt Widgets and KDE Frameworks give most of this for free (focus rectangles, the AT-SPI bridge,
palette-aware widgets, scalable icons); this page lists what the IDE adds on top and what is checked by a
test. A box is ticked only when a test or a script in this repository shows it. Open items say what is
missing and how to check them by hand.

## Checklist

### Keyboard and focus

- [x] Every region of the window can be reached without the mouse: **Focus Project Tree** (Ctrl+Alt+1),
  **Focus Problems and Output** (Ctrl+Alt+2), **Focus Editor** (Ctrl+Alt+0), in the View menu. Checked by
  `scripts/test-accessibility.sh`, which runs the commands and asks the bridge where the keyboard focus is.
- [x] Focus is drawn by the style (Breeze); the bridge sets no style sheets and does no custom painting
  (`grep setStyleSheet bridge/src` finds nothing).
- [ ] Tab order inside each dialog by hand. The settings window and the pickers use Qt's layout order.

### Accessible names

- [x] The declarative widget tree has `accessibleName` and `accessibleDescription` props
  ([widget.md](widget.md)); `Node.accessibleName()` in Tinox.
- [x] **Every keyboard-focusable widget has an accessible name** in the main window (with a project and a file
  open), in quick open and in the settings window. `widget.debug.accessibility` (needs `TKDE_DEBUG_COMMANDS=1`)
  walks a mount and reports the focusable widgets whose `QAccessible` name is empty; the smoke fails on any.
  Not covered: the bars and messages inside KTextEditor's view and the drop-down lists of combo boxes (KDE and Qt
  own those).
- [x] Each editor view is named `Editor: <file>` and follows renames (`editor.createView`).
- [x] The real accessibility tree can be read over AT-SPI: `make test-atspi` starts the IDE, reads the tree like a
  screen reader would and checks for the window, `Open files`, `Editor: Main.tnx` and the panels. It needs a
  display and a running AT-SPI bus (it opens a window for a few seconds), so it is not part of `make test`.

### Screen reader

- [ ] **Orca walkthrough** (open a file, edit, run a build). Orca is not installed on the development machine,
  so this was not done. The AT-SPI tree above is what Orca reads; the manual procedure is:
  1. `orca &`, start the IDE (`make run`).
  2. Ctrl+O or **Open Folder…**, move through the tree, open a file: the editor is announced as `Editor: <file>`.
  3. Type text; F7 builds; Ctrl+Alt+2 moves to the output, where the build log is announced as `Build output`.
  4. Record anything that is silent or unnamed in this list and fix it with `accessibleName`.

### Colours, contrast, scaling

- [x] The bridge has no fixed colours, sizes or style sheets, apart from the semantic highlighting colours the user
  chooses. Tinox code sets row colours nowhere.
- [x] Diagnostic underlines and gutter marks keep a 3:1 contrast on a light, a dark and the pure black and white
  of a high-contrast scheme (`editor_test`, `editor.debug.setPalette`).
- [x] Coloured text in logs (build, debug console) is moved toward the text colour until it reaches 4.5:1 against the
  log's background, so ANSI blue or black stays readable on dark and high-contrast schemes (`widget_test`).
- [x] Sizes are device independent (`size(1200, 760)`, `minimumSize`), icons come from the theme (`QIcon::fromTheme`),
  so Qt 6's scale factors apply on X11 and Wayland.
- [ ] **HiDPI on Wayland and X11 by hand**: `QT_SCALE_FACTOR=2 make run`, and a fractional factor on a Plasma Wayland
  session; check that nothing is clipped. Not done on a second display here.
- [ ] **High-contrast system scheme by hand**: choose *Breeze High Contrast* and look at the editor, the trees and
  the logs.

### KDE HIG and internationalisation

- [x] Actions use KDE's standard shortcuts and icons where a standard action exists (`IdeCommand::standard`).
- [x] The Debug menu is its own menu and its entries have unique mnemonics (set by hand, no script checks it).
- [ ] Menu entries that open a dialog should end with an ellipsis: not audited.
- [ ] Mnemonics are not unique across all menus: the File, View, Go and Build menus mix this IDE's actions with
  KDE standard ones, and several accelerators clash. Needs a pass over the whole menu bar.
- [x] **Localisation**: the bridge translates the UI strings it is given and `Tr` the messages assembled at run time;
  the catalogs are KDE's (`po/`, German included). `scripts/test-i18n.sh` runs the IDE under `LANGUAGE=de` and checks
  commands, menus, widgets and assembled messages. Not everything is translated yet (log output, compiler messages);
  see [i18n.md](i18n.md). The message API Tinox itself should have is subnix-work/tinox#588.
- [ ] A full review against the HIG pages (layouts, wording, dialog buttons) by a person.

## Running the checks

```sh
scripts/test-accessibility.sh   # offscreen; in `make test`
make test-atspi                 # real display and AT-SPI bus
make test-bridge                # includes the contrast and widget tests
```
