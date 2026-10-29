# Development environment

`scripts/check-env.sh` (or `make check-env`) verifies everything below and
prints the install command for your distribution. `scripts/test-check-env.sh`
tests the script itself and runs as part of `make test`.

## Minimum versions

| Component | Minimum | Why |
|---|---|---|
| Qt 6 | 6.6 | Widgets, Core; the bridge targets Qt 6 only |
| KDE Frameworks | 6.5 | Config, CoreAddons, I18n, Crash, DBusAddons, XmlGui, TextEditor, SyntaxHighlighting, KIO, Notifications, Parts |
| extra-cmake-modules | 6.5 | `KDEInstallDirs`, `KDECMakeSettings`, `KDECompilerSettings` |
| CMake | 3.20 | |
| `tinox` | 2.8.0 plus IDE APIs (see below) | `[native]` section, `extern fn` ABI, `--message-format=json` |
| gdb | 14 (optional) | `tinox dap` needs gdb's built-in DAP server |
| ripgrep | any | Find in Files (`rg`) |

Linux only, like Tinox itself.

## Packages

**Arch Linux**

```sh
sudo pacman -S --needed qt6-base qt6-declarative extra-cmake-modules kconfig \
  kcoreaddons ki18n kcrash kdbusaddons kxmlgui ktexteditor syntax-highlighting kio knotifications kparts \
  breeze cmake ninja clang pkgconf gc gdb ripgrep
```

**Debian / Ubuntu**

```sh
sudo apt install qt6-base-dev qt6-declarative-dev extra-cmake-modules \
  libkf6config-dev libkf6coreaddons-dev libkf6i18n-dev libkf6crash-dev libkf6dbusaddons-dev libkf6xmlgui-dev \
  libkf6texteditor-dev libkf6syntaxhighlighting-dev libkf6kio-dev \
  libkf6notifications-dev libkf6parts-dev breeze cmake ninja-build clang pkg-config libgc-dev gdb ripgrep
```

**Fedora**

```sh
sudo dnf install qt6-qtbase-devel qt6-qtdeclarative-devel extra-cmake-modules \
  kf6-kconfig-devel kf6-kcoreaddons-devel kf6-ki18n-devel kf6-kcrash-devel kf6-kdbusaddons-devel kf6-kxmlgui-devel \
  kf6-ktexteditor-devel kf6-syntax-highlighting-devel kf6-kio-devel \
  kf6-knotifications-devel kf6-kparts-devel breeze cmake ninja-build clang pkgconf-pkg-config \
  gc-devel gdb ripgrep
```

Package names follow each distribution's current naming; if one has moved, the
check script reports which CMake package is missing.

## The Tinox toolchain

```sh
git clone git@github.com:subnix-work/tinox.git ../tinox
cd ../tinox
cargo build --release -p tinox -p tinox-lsp   # target/release/tinox, tinox-lsp
```

`make` uses `tinox` from `PATH`, else `../tinox/target/release/tinox`; override
with `make TINOX=/path/to/tinox`. The 2.8.0 release archive predates required IDE APIs; use at least upstream
commit `099e8cdcf762c7e4e04cc0bcd9a9078995c1f25b` (compiler, core sources and runtime
from the same tree), as pinned by the Flatpak build. Put `tinox-lsp` on `PATH` (or
`~/.cargo/bin`) for the IDE's language features. The LSP integration test also
looks at `TINOX_LSP` and `../tinox/target/release/tinox-lsp`; it fails if no server
is available. Its scripted peer requires Python 3. See [lsp-client.md](lsp-client.md).

For staged installs, Arch packages and Flatpak, see [packaging.md](packaging.md).

## Sanitizers

```sh
make test-bridge  # the bridge suite alone, offscreen (see testing.md)
make test-asan    # bridge tests under AddressSanitizer + UBSan + LeakSanitizer
make test-tsan    # under ThreadSanitizer (suppressions: bridge/tests/tsan.supp)
make docs         # API documentation of the Tinox side: build/api/index.html
make example      # run examples/shell
make fuzz         # libFuzzer on the command parser, 60 s (FUZZ_SECONDS=600; needs clang)
```

## Headless runs

GUI tests run without a display on Qt's offscreen platform:

```sh
QT_QPA_PLATFORM=offscreen make test
```

The check script reports whether the `qoffscreen` plugin is installed.

## Reproducible environment (optional)

A throwaway Arch container with the packages above:

```sh
distrobox create --name tinox-ide-dev --image archlinux:latest
distrobox enter tinox-ide-dev
# inside: install the Arch packages above, then build as usual
```

Konsole is optional: install `konsole` for the embedded interactive shell. Without
its KPart the Terminal tab provides a read-only ANSI output console.
