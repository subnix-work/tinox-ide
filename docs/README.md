# Tinox IDE documentation

Start with the [user manual](manual/README.md). Contributors should read the
[architecture map](architecture.md), [dogfooding workflow](dogfooding.md) and
[friction log](friction-log.md).

## User manual

1. [Install and start](manual/install.md)
2. [Your first project](manual/first-project.md)
3. [Edit and navigate](manual/editor.md)
4. [Build, run and test](manual/build-run-test.md)
5. [Preferences and troubleshooting](manual/settings.md)

## Architecture and reference

The [architecture map](architecture.md) connects the bridge, bindings and application
core to their detailed references. [ADRs](adr/0001-architecture.md) explain the thread
and ownership boundaries. The [development guide](development.md) and
[testing guide](testing.md) describe the toolchain and verification commands.

Run `make docs` to generate the Tinox API reference from source comments and render
this documentation as a linked local HTML site. Open `build/docs/index.html`.
The original generated reference remains at `build/api/index.html`; its project
navigation links back to the manual and architecture. Generation requires Python 3
and Python-Markdown (`python-markdown` on Arch, `python3-markdown` on Debian/Ubuntu).
No network service or documentation server is required to browse the output.
