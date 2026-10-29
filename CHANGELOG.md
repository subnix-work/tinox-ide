# Changelog

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
