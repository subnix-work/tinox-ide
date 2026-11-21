# Changelog

## 0.1.4

Fixes the build with Tinox 2.9.8: its compiler now enforces complexity limits by default, and 24 functions of the IDE exceed
them, so 0.1.3 did not build. `tinox.toml` sets `[analysis] level = "warn"` (the functions are reported as warnings) until they are
split up. No functional changes. 0.1.3 is superseded.

## 0.1.3

Built with Tinox 2.9.8: the manifest, the manual's example and the fixtures of the tests that build real projects name
compiler 2.9.8, the `tinox.core.json` dependency is 1.1.10, and the environment check and the documentation require Tinox 2.9.8.
No functional changes.

## 0.1.2

Built with Tinox 2.9.7: the manifest, the manual's example and the fixtures of the tests that build real projects name
compiler 2.9.7, the `tinox.core.json` dependency is 1.1.9, and the environment check and the documentation require Tinox 2.9.7.
No functional changes.

## 0.1.1

Built with Tinox 2.9.6: the manifest, the manual's example and the fixtures of the tests that build real projects name
compiler 2.9.6 (the compiler rejects a `tinox.toml` that asks for another version), the `tinox.core.json` dependency is
1.1.8, and the environment check and the documentation require Tinox 2.9.6. No functional changes.

## 0.1.0 (development)

Initial development version: KDE editor, project tree, LSP diagnostics and navigation,
formatting, semantic highlighting, search, terminal, build/run/test integration.
Desktop/MIME integration, staged installation, Arch VCS package and Flatpak packaging.
Debugger integration (issue #31): gutter breakpoints, run to cursor, step over/in/out, continue, variables, call stack,
threads and a debug console through the Debug Adapter Protocol (`tinox dap`).
Accessibility (issue #32): accessible names on all focusable widgets with an automated audit, focus commands for the
project tree, bottom panel and editor, a Debug menu, legible log colours on dark and high-contrast schemes.
File-scoped test discovery (issue #63): Discover Tests in Current File, a scope label above the test tree, and a cancelled
discovery no longer replaces earlier results.
Localisation (issue #32): the bridge translates the UI strings it is given (KDE catalogs, domain tinox-ide), `Tr` for messages
assembled at run time, `scripts/i18n-extract.py` and a German translation.
