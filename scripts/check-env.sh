#!/usr/bin/env bash
# Verifies that this machine can build and run the Tinox IDE.
#   scripts/check-env.sh          full report, exit 1 when a required item is missing
#   scripts/check-env.sh --quiet  only problems
# Minimum versions: see docs/development.md.
set -u

MIN_QT=6.6
MIN_KF=6.5
MIN_CMAKE=3.20
MIN_TINOX=2.8.0

QUIET=0
[ "${1:-}" = "--quiet" ] && QUIET=1

missing_required=0
hints=()

ok()   { [ "$QUIET" = 1 ] || printf '  \033[32m✓\033[0m %s\n' "$1"; }
warn() { printf '  \033[33m!\033[0m %s\n' "$1"; }
bad()  { printf '  \033[31m✗\033[0m %s\n' "$1"; missing_required=1; }
section() { [ "$QUIET" = 1 ] || printf '\n%s\n' "$1"; }

# true if $1 >= $2 (dotted versions)
version_ge() { [ "$(printf '%s\n%s\n' "$2" "$1" | sort -V | head -n1)" = "$2" ]; }

distro=unknown
if [ -r /etc/os-release ]; then
    . /etc/os-release
    case "${ID:-} ${ID_LIKE:-}" in
        *arch*) distro=arch ;;
        *debian*|*ubuntu*) distro=debian ;;
        *fedora*|*rhel*) distro=fedora ;;
    esac
fi

# package name per distro for a logical dependency
pkg() {
    case "$distro:$1" in
        arch:qt6-base) echo qt6-base ;;               debian:qt6-base) echo qt6-base-dev ;;               fedora:qt6-base) echo qt6-qtbase-devel ;;
        arch:ecm) echo extra-cmake-modules ;;         debian:ecm) echo extra-cmake-modules ;;            fedora:ecm) echo extra-cmake-modules ;;
        arch:Config) echo kconfig ;;                  debian:Config) echo libkf6config-dev ;;            fedora:Config) echo kf6-kconfig-devel ;;
        arch:Parts) echo kparts ;;                    debian:Parts) echo libkf6parts-dev ;;               fedora:Parts) echo kf6-kparts-devel ;;
        arch:CoreAddons) echo kcoreaddons ;;          debian:CoreAddons) echo libkf6coreaddons-dev ;;    fedora:CoreAddons) echo kf6-kcoreaddons-devel ;;
        arch:I18n) echo ki18n ;;                      debian:I18n) echo libkf6i18n-dev ;;                fedora:I18n) echo kf6-ki18n-devel ;;
        arch:XmlGui) echo kxmlgui ;;                  debian:XmlGui) echo libkf6xmlgui-dev ;;            fedora:XmlGui) echo kf6-kxmlgui-devel ;;
        arch:TextEditor) echo ktexteditor ;;          debian:TextEditor) echo libkf6texteditor-dev ;;    fedora:TextEditor) echo kf6-ktexteditor-devel ;;
        arch:SyntaxHighlighting) echo syntax-highlighting ;; debian:SyntaxHighlighting) echo libkf6syntaxhighlighting-dev ;; fedora:SyntaxHighlighting) echo kf6-syntax-highlighting-devel ;;
        arch:KIO) echo kio ;;                         debian:KIO) echo libkf6kio-dev ;;                  fedora:KIO) echo kf6-kio-devel ;;
        arch:Crash) echo kcrash ;;                    debian:Crash) echo libkf6crash-dev ;;              fedora:Crash) echo kf6-kcrash-devel ;;
        arch:DBusAddons) echo kdbusaddons ;;          debian:DBusAddons) echo libkf6dbusaddons-dev ;;    fedora:DBusAddons) echo kf6-kdbusaddons-devel ;;
        arch:Notifications) echo knotifications ;;    debian:Notifications) echo libkf6notifications-dev ;; fedora:Notifications) echo kf6-knotifications-devel ;;
        arch:cmake) echo cmake ;;                     debian:cmake) echo cmake ;;                        fedora:cmake) echo cmake ;;
        arch:ninja) echo ninja ;;                     debian:ninja) echo ninja-build ;;                  fedora:ninja) echo ninja-build ;;
        arch:clang) echo clang ;;                     debian:clang) echo clang ;;                        fedora:clang) echo clang ;;
        arch:pkgconf) echo pkgconf ;;                 debian:pkgconf) echo pkg-config ;;                 fedora:pkgconf) echo pkgconf-pkg-config ;;
        arch:gc) echo gc ;;                     debian:gc) echo libgc-dev ;;                       fedora:gc) echo gc-devel ;;
        arch:gdb) echo gdb ;;                         debian:gdb) echo gdb ;;                            fedora:gdb) echo gdb ;;
        *) echo "$1" ;;
    esac
}

install_hint() {
    case "$distro" in
        arch)   echo "sudo pacman -S --needed $*" ;;
        debian) echo "sudo apt install $*" ;;
        fedora) echo "sudo dnf install $*" ;;
        *)      echo "install: $*" ;;
    esac
}

need_tool() { # command, logical package
    if command -v "$1" >/dev/null 2>&1; then
        ok "$1: $(command -v "$1")"
    else
        bad "$1 not found"
        hints+=("$(pkg "$2")")
    fi
}

section "Tools"
need_tool cmake cmake
need_tool ninja ninja
need_tool clang clang
need_tool pkg-config pkgconf
if command -v cmake >/dev/null 2>&1; then
    v=$(cmake --version | head -n1 | awk '{print $3}')
    if version_ge "$v" "$MIN_CMAKE"; then ok "cmake $v (>= $MIN_CMAKE)"; else bad "cmake $v is older than $MIN_CMAKE"; fi
fi
command -v clang-format >/dev/null 2>&1 && ok "clang-format available (make fmt)" || warn "clang-format not found: make fmt will skip C++ files"

section "Tinox toolchain"
TINOX_BIN=${TINOX:-$(command -v tinox 2>/dev/null || echo ../tinox/target/release/tinox)}
if [ -x "$TINOX_BIN" ]; then
    tv=$("$TINOX_BIN" --version 2>/dev/null | awk '{print $2}')
    if [ -n "$tv" ] && version_ge "$tv" "$MIN_TINOX"; then ok "tinox $tv ($TINOX_BIN)"; else bad "tinox ${tv:-?} at $TINOX_BIN is older than $MIN_TINOX"; fi
else
    bad "tinox not found (PATH or ../tinox/target/release/tinox): build subnix-work/tinox with 'cargo build --release'"
fi
if command -v tinox-lsp >/dev/null 2>&1 || [ -x "$HOME/.cargo/bin/tinox-lsp" ]; then
    ok "tinox-lsp available"
else
    warn "tinox-lsp not found: the IDE's language features need it (cargo build --release -p tinox-lsp in the tinox checkout)"
fi
if command -v gdb >/dev/null 2>&1; then
    gv=$(gdb --version | head -n1 | awk '{print $NF}')
    version_ge "$gv" 14 && ok "gdb $gv (DAP debugging)" || warn "gdb $gv is older than 14: 'tinox dap' will not work"
else
    warn "gdb not found: debugging disabled"
fi
if command -v cc >/dev/null 2>&1 && printf '#include <gc.h>\nint main(void){return 0;}\n' | cc -x c - -lgc -o /dev/null 2>/dev/null; then
    ok "Boehm GC (libgc) usable by the Tinox runtime"
else
    bad "libgc not usable (Tinox runtime links -lgc)"
    hints+=("$(pkg gc)")
fi

section "CMake packages"
tmp=$(mktemp -d)
trap 'rm -rf -- "$tmp"' EXIT
cmake_has() { # label, min version, find_package arguments...
    local label=$1 minv=$2 logical=$3; shift 3
    mkdir -p "$tmp/p"
    # same prelude as bridge/CMakeLists.txt: KF6 packages need the ECM module path
    printf 'cmake_minimum_required(VERSION 3.20)\nproject(envcheck LANGUAGES CXX)\nfind_package(ECM %s NO_MODULE)\nif(ECM_FOUND)\n  set(CMAKE_MODULE_PATH ${ECM_MODULE_PATH})\nendif()\nfind_package(%s REQUIRED)\n' "$MIN_KF" "$*" >"$tmp/p/CMakeLists.txt"
    if cmake -S "$tmp/p" -B "$tmp/p/build" >"$tmp/log" 2>&1; then
        ok "$label"
    else
        bad "$label (needs $minv or newer)"
        hints+=("$(pkg "$logical")")
    fi
    rm -rf "$tmp/p"
}
if command -v cmake >/dev/null 2>&1; then
    cmake_has "ECM >= $MIN_KF" "$MIN_KF" ecm ECM "$MIN_KF" NO_MODULE
    cmake_has "Qt6 >= $MIN_QT (Core, Widgets, Xml, Test)" "$MIN_QT" qt6-base Qt6 "$MIN_QT" COMPONENTS Core Widgets Xml Test
    for comp in Parts Config ConfigWidgets WidgetsAddons CoreAddons I18n Crash DBusAddons XmlGui TextEditor SyntaxHighlighting KIO Notifications; do
        cmake_has "KF6 $comp >= $MIN_KF" "$MIN_KF" "$comp" KF6 "$MIN_KF" COMPONENTS "$comp"
    done
fi

section "Runtime"
plugins=""
command -v qmake6 >/dev/null 2>&1 && plugins=$(qmake6 -query QT_INSTALL_PLUGINS 2>/dev/null)
if [ -n "$plugins" ] && [ -e "$plugins/platforms/libqoffscreen.so" ]; then
    ok "Qt offscreen platform (QT_QPA_PLATFORM=offscreen for headless tests)"
else
    warn "Qt offscreen platform plugin not found: headless GUI tests cannot run"
fi
if [ -n "${WAYLAND_DISPLAY:-}${DISPLAY:-}" ]; then ok "display: ${WAYLAND_DISPLAY:-$DISPLAY}"; else warn "no display (WAYLAND_DISPLAY/DISPLAY unset): use QT_QPA_PLATFORM=offscreen"; fi

echo
if [ "$missing_required" = 0 ]; then
    echo "Environment OK."
    exit 0
fi
echo "Environment is missing required items."
if [ "${#hints[@]}" -gt 0 ]; then
    uniq_hints=$(printf '%s\n' "${hints[@]}" | sort -u | tr '\n' ' ')
    echo "Install (${distro}):"
    echo "  $(install_hint $uniq_hints)"
fi
exit 1
