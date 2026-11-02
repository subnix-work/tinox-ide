# Packaging and desktop integration

The application ID is `org.subnix.tinoxide`, executable `tinox-ide`, version
`[package].version` in `tinox.toml`. The bridge's `1.0.0`/SONAME `1` is a separate
ABI version. CMake generates the About dialog's application version from the
manifest. Release metadata and the changelog must use the same application version.

## Local installation

Install the dependencies in [development.md](development.md), plus `patchelf`.
Build as your normal user, then install:

```sh
make ide
sudo make install PREFIX=/usr/local LIBDIR=lib
```

For a package, stage without writing the host filesystem:

```sh
make install PREFIX=/usr LIBDIR=lib DESTDIR="$PWD/build/stage"
make test-packaging
```

`install` requires an already built executable; it never invokes the Tinox compiler
as root. It installs the executable, real bridge (not the mock), C header, licenses,
desktop entry, scalable icon, AppStream metadata, syntax/MIME definitions and
notification events. There is no application XMLGUI rc file: menus and toolbars
are created through bridge commands. The installed binary's RPATH is relative to
its own location and the configured library directory; the build copy is unchanged.
Do not copy the development binary on its own: its original RPATH points into
the bridge build tree.

Distribution/Flatpak package managers update desktop, icon and MIME caches. After
a manual install, run `update-mime-database PREFIX/share/mime` and, if available,
`update-desktop-database PREFIX/share/applications`. No default application is
changed automatically. Choose Tinox IDE in the file manager's Open With menu.
`.tnx` files open in the editor with their containing folder; `tinox.toml` opens
its project. The desktop entry deliberately accepts one local file (`%f`);
remote URLs are not advertised. Starting without arguments restores the last
workspace/session according to preferences.

`make test-packaging` validates the actual staged executable away from the source
working directory, bridge lookup, unchanged development RPATH, metadata, MIME
recognition and relative file/manifest arguments containing spaces. It needs
`desktop-file-utils`, `appstream`, `shared-mime-info`, `xdg-utils` and `patchelf`.
It does not install anything into the running desktop.

## Arch Linux

`packaging/PKGBUILD` is a VCS package (`tinox-ide-git`) built from the repository.
Install Tinox 2.9.0 or newer (it contains upstream commit
`099e8cdcf762c7e4e04cc0bcd9a9078995c1f25b`; the 2.8.0 release predates the watcher
and piped-process APIs). `tinox` is not in Arch's official repositories; the AUR package
`tinox-bin` provides the release, or build it from source following upstream instructions.
Put `tinox` and `tinox-lsp` on PATH. `makepkg` declares the Arch build/runtime dependencies and
prints an explicit error if the separately installed compiler is missing.

```sh
cd packaging
makepkg -si
```

The package provides/conflicts with `tinox-ide`. Konsole, Breeze and ripgrep are
optional. Tinox plus clang are needed to build/run user programs; the language
server is needed for language assistance. To test another Git repository,
set `TINOX_IDE_SOURCE=/absolute/path/to/repository` before `makepkg`.

## Flatpak

`packaging/org.subnix.tinoxide.json` targets the KDE 6.10 runtime and x86_64
(the upstream Tinox release currently has only an x86_64 binary). Install
`flatpak-builder`, then:

```sh
flatpak remote-add --user --if-not-exists flathub https://flathub.org/repo/flathub.flatpakrepo
flatpak install --user flathub org.kde.Sdk//6.10 org.kde.Platform//6.10 org.freedesktop.Sdk.Extension.llvm21//25.08 org.freedesktop.Sdk.Extension.rust-stable//25.08
flatpak-builder --user --force-clean --repo=build/flatpak-repo build/flatpak packaging/org.subnix.tinoxide.json
flatpak install --user "$PWD/build/flatpak-repo" org.subnix.tinoxide
flatpak run org.subnix.tinoxide
```

The builder downloads SHA-256-pinned Tinox sources (commit `099e8cd`), libgc, patchelf and JSON 1.1.0
inputs before compilation. JSON's registry envelope and unpacked archive are both
verified. Build steps run without network access. The source directory is the
checkout; generated build/cache/log directories are excluded. The compiler and
patchelf are build-only; libgc and the bridge are bundled with the executable.
TLS is disabled in the IDE's Tinox runtime build because network protocols are
handled by external tools; this avoids an unused OpenSSL dependency.

The build-only compiler source tree remains at its compiled-in path under
`/app/share/tinox-build/source` until the IDE is compiled, then is removed during
cleanup. This avoids writing the SDK's `/usr` for Tinox's fixed `runtime.c` lookup
([tinox#583](https://github.com/subnix-work/tinox/issues/583)). The Flatpak uses the
normal read-only SDK. It builds Tinox from a pinned source commit (contained in release 2.9.0)
because release 2.8.0 lacks APIs already needed by the IDE.

Executable canonicalization temporarily uses coreutils `realpath -e --` through
a checked argument-vector subprocess: the checked `Fs.realPath` API exists only
in an unpublished local compiler tree
([tinox#584](https://github.com/subnix-work/tinox/issues/584)). This keeps the
package buildable against public source without modifying Tinox.

`packaging/cargo-sources.json` pins the Rust dependencies in that commit's
`Cargo.lock`. Regenerate it with the official
[flatpak-cargo-generator](https://github.com/flatpak/flatpak-builder-tools/tree/master/cargo)
when updating the toolchain source commit, and update the archive checksum.

User builds, their resulting executables and the LSP use **host tools** through argument-preserving
`flatpak-spawn --host`. Task execution forwards the run configuration’s child-only
environment and working directory; tool-probe/LSP wrappers are named `tinox` and
`tinox-lsp` (separate files so canonicalizing the LSP path preserves its name). Install Tinox,
its core/runtime, clang, libgc development files and tinox-lsp on the host PATH.
Projects must be stored at paths visible on the host (for example under home),
not ephemeral `/app` or `/run` paths. Stdio and exit codes are passed through,
so LSP framing and build diagnostics work as for a native installation.
The Flatpak therefore grants host filesystem, network and host-command access;
it is an IDE integration package, not a security boundary for untrusted projects.
The KDE runtime does not bundle Konsole's KPart, so the terminal uses its fallback
console. Native packaging supports the interactive Konsole terminal.

The Packaging workflow builds the Flatpak on PRs and main, starts the installed
application offscreen against the runtime, and uploads a `.flatpak` bundle.
Full compiler/bridge/sanitizer CI remains tracked separately in issue #34.

See the official [Flatpak build guide](https://docs.flatpak.org/en/latest/building-introduction.html)
and [AppStream application metadata reference](https://www.freedesktop.org/software/appstream/docs/sect-Metadata-Application.html)
for the package formats.

## Release source archives

Update `tinox.toml`, the AppStream release entry and `CHANGELOG.md` together. Run
`make test` and `make test-packaging`, commit, and tag that reviewed commit
`vVERSION`. `make release` refuses dirty tracked files, inconsistent versions or
an absent/wrong tag, then produces `build/release/tinox-ide-VERSION.tar.gz` and
`SHA256SUMS` with reproducible gzip headers. It never tags, pushes or publishes.
The Flatpak workflow's bundle can be attached to a release after its checks pass.
