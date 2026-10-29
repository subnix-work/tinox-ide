# Install and start

Tinox IDE needs Linux, Qt 6 and KDE Frameworks 6. Plasma is a natural fit, but the
application can run on another Linux desktop when its dependencies are installed.

## Choose an installation

The [packaging guide](../packaging.md) contains the complete commands and toolchain
revision requirements for each option:

| Option | Installation | Development tools |
|---|---|---|
| Arch Linux | Run `makepkg -si` in `packaging/` | Install Tinox and tinox-lsp separately; the package declares the KDE dependencies |
| Flatpak | Build/install `packaging/org.subnix.tinoxide.json`, or install a CI bundle | Install Tinox, tinox-lsp, clang and libgc development files on the host |
| Source checkout | Follow [development.md](../development.md), then `make check-env` and `make ide` | Use a compatible compiler, core sources and runtime from the same Tinox checkout |

The old Tinox 2.8.0 release archive predates APIs the IDE needs. Follow the exact
source revision guidance in the packaging/development guides. Installing only a
compiler binary without its matching core/runtime is insufficient.

## Start the application

Use the application launcher after installation, or run:

```sh
tinox-ide /path/to/project
```

From a development checkout use `make run`, or `./build/tinox-ide /path/to/project`
after `make ide`. The build-tree executable should be started from that checkout;
installed packages have a relocatable bridge library path.

For Flatpak use `flatpak run org.subnix.tinoxide`. Its compiler and language-server
wrappers call the host tools. Keep projects at paths visible to the host, such as
under your home directory. See the packaging guide for the granted permissions.

Opening a `.tnx` file through the file manager opens it with its containing folder.
Opening `tinox.toml` opens its project. You can select Tinox IDE in Open With;
installation does not replace your default file associations.

## First checks

Open Settings → Configure Tinox IDE… (or find the configure command in the palette).
On the Language Server page check the Tinox compiler and tinox-lsp paths. For a
native installation, an executable on PATH or in the usual user tool folders is
found automatically. A custom tool path must exist and be executable.

If the window opens with no project, choose File → Open Folder… (`Ctrl+Shift+O`).
The left dock is the project tree, the middle contains editor tabs, and the bottom
contains Problems, Output, search, terminal and build/test panels.
