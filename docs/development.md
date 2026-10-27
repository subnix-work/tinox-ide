# Development environment

`scripts/check-env.sh` (or `make check-env`) verifies everything below and
prints the install command for your distribution. `scripts/test-check-env.sh`
tests the script itself and runs as part of `make test`.

## Minimum versions

| Component | Minimum | Why |
|---|---|---|
| Qt 6 | 6.6 | Widgets, Core; the bridge targets Qt 6 only |
| KDE Frameworks | 6.5 | Config, CoreAddons, I18n, Crash, DBusAddons, XmlGui, TextEditor, SyntaxHighlighting, KIO, Notifications |
| extra-cmake-modules | 6.5 | `KDEInstallDirs`, `KDECMakeSettings`, `KDECompilerSettings` |
| CMake | 3.20 | |
| `tinox` | 2.8.0 | `[native]` section, `extern fn` ABI, `--message-format=json` |
| gdb | 14 (optional) | `tinox dap` needs gdb's built-in DAP server |

Linux only, like Tinox itself.

## Packages

**Arch Linux**

```sh
sudo pacman -S --needed qt6-base qt6-declarative extra-cmake-modules kconfig \
  kcoreaddons ki18n kcrash kdbusaddons kxmlgui ktexteditor syntax-highlighting kio knotifications \
  breeze cmake ninja clang pkgconf boehm-gc gdb
```

**Debian / Ubuntu**

```sh
sudo apt install qt6-base-dev qt6-declarative-dev extra-cmake-modules \
  libkf6config-dev libkf6coreaddons-dev libkf6i18n-dev libkf6crash-dev libkf6dbusaddons-dev libkf6xmlgui-dev \
  libkf6texteditor-dev libkf6syntaxhighlighting-dev libkf6kio-dev \
  libkf6notifications-dev breeze cmake ninja-build clang pkg-config libgc-dev gdb
```

**Fedora**

```sh
sudo dnf install qt6-qtbase-devel qt6-qtdeclarative-devel extra-cmake-modules \
  kf6-kconfig-devel kf6-kcoreaddons-devel kf6-ki18n-devel kf6-kcrash-devel kf6-kdbusaddons-devel kf6-kxmlgui-devel \
  kf6-ktexteditor-devel kf6-syntax-highlighting-devel kf6-kio-devel \
  kf6-knotifications-devel breeze cmake ninja-build clang pkgconf-pkg-config \
  gc-devel gdb
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
with `make TINOX=/path/to/tinox`. Put `tinox-lsp` on `PATH` (or
`~/.cargo/bin`) for the IDE's language features.

## Sanitizers

```sh
make test-asan    # bridge tests under AddressSanitizer + UBSan + LeakSanitizer
make test-tsan    # under ThreadSanitizer (suppressions: bridge/tests/tsan.supp)
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
