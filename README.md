# Tinox IDE

A native Linux IDE for the [Tinox](https://github.com/subnix-work/tinox) language
with real KDE Plasma components (Qt 6 Widgets, KDE Frameworks 6, KTextEditor,
Breeze) — and the IDE itself is written in Tinox.

Tinox can call C functions (`extern fn`) but not C++, so a thin C++ library,
the **bridge** (`libtinoxkde`), exposes Qt and KDE through a small C ABI. The
IDE logic runs in Tinox on a spawned task while Qt's event loop owns the main
thread; the two talk through queues. See [docs/adr](docs/adr) for the
architecture, [docs/abi.md](docs/abi.md) for the C ABI and the [epic](https://github.com/subnix-work/tinox-ide/issues/1)
for the plan.

> Status: bootstrapping. Nothing here is usable as an IDE yet.

## Layout

| Path | Contents |
|---|---|
| `bridge/` | C++ library `libtinoxkde` (CMake, ECM): the C ABI over Qt/KDE |
| `src/` | The IDE, written in Tinox (`src/Main.tnx` is the entry point) |
| `tests/` | Tinox tests (`@Test`) |
| `docs/` | Architecture decisions (`docs/adr`), the bridge ABI, manuals |
| `resources/` | Syntax definition, MIME types, icons, desktop files |
| `packaging/` | PKGBUILD, Flatpak manifest |
| `scripts/` | Developer scripts such as `check-env.sh` |

## Building

Needs the `tinox` compiler (a checkout of `subnix-work/tinox`, built with
`cargo build --release`), `clang`, `cmake`, `ninja`, Qt 6 and KDE Frameworks 6
development packages. `scripts/check-env.sh` tells you what is missing and how
to install it.

```sh
make check-env   # toolchain and dependency check
make bridge      # build libtinoxkde
make ide         # build the IDE (links the bridge)
make run         # build and start it
make test        # bridge tests (ctest) and Tinox tests
make fmt         # clang-format + tinox fmt
```

`make` finds `tinox` on `PATH`, else in `../tinox/target/release/tinox`;
override with `make TINOX=/path/to/tinox`.

## License

MIT OR Apache-2.0, like Tinox itself. See [LICENSE-MIT](LICENSE-MIT) and
[LICENSE-APACHE](LICENSE-APACHE).
