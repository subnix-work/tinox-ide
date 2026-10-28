#!/usr/bin/env bash
# Runs the programs of this repository on the REAL bridge, headless (Qt's offscreen platform):
#
#   1. the IDE itself with TINOXIDE_SMOKE=1: it builds its window with all docks and menus, then uses
#      its own Quit command through the action layer, saves its layout and exits;
#   2. examples/shell with TINOXIDE_EXAMPLE_QUIT=1: a window with menu, tree and editor.
#
# Each must exit 0 and print nothing on stderr except the IDE's own INFO lines: every failure of a
# command or handler is reported there. Configuration goes to a throw-away directory.
set -eu
here=$(cd "$(dirname "$0")/.." && pwd)
sandbox=$(mktemp -d)
trap 'rm -rf "$sandbox"' EXIT
cd "$here"

run() {
    local label=$1
    shift
    local errors
    errors=$(env QT_QPA_PLATFORM=offscreen TKDE_NO_KCRASH=1 XDG_CONFIG_HOME="$sandbox/config" XDG_DATA_HOME="$sandbox/data" XDG_CACHE_HOME="$sandbox/cache" \
        "$@" timeout 120 "${TINOX:-tinox}" run ${RUN_FILE:-} 2>&1 >/dev/null) || {
        echo "$label failed: $errors" >&2
        exit 1
    }
    errors=$(printf '%s\n' "$errors" | grep -v ' INFO  ' || true)
    if [ -n "$errors" ]; then
        echo "$label reported problems on stderr: $errors" >&2
        exit 1
    fi
}

RUN_FILE= run "the IDE" TINOXIDE_SMOKE=1
# opening a project: this repository, which has a tinox.toml; it must take well under a second
opened=$(env QT_QPA_PLATFORM=offscreen TKDE_NO_KCRASH=1 TINOXIDE_SMOKE=1 TINOXIDE_OPEN="$here" XDG_CONFIG_HOME="$sandbox/config" XDG_DATA_HOME="$sandbox/data" XDG_CACHE_HOME="$sandbox/cache" \
    timeout 120 "${TINOX:-tinox}" run 2>&1 >/dev/null | grep 'opened project' || true)
case "$opened" in
*" in "*" ms"*) ms=${opened##* in }; ms=${ms% ms*}
    if [ "$ms" -ge 1000 ]; then echo "opening the project took $ms ms" >&2; exit 1; fi
    echo "workspace: opened this repository in $ms ms" ;;
*) echo "the IDE did not open the project: $opened" >&2; exit 1 ;;
esac
test -s "$sandbox/config/tinoxiderc" || { echo "the IDE did not save its layout to tinoxiderc" >&2; exit 1; }
echo "ide: window, docks, menus built; Quit command works; layout saved to tinoxiderc"
RUN_FILE=examples/shell/Main.tnx run "the shell example" TINOXIDE_EXAMPLE_QUIT=1
echo "example: window, menus, tree and editor built"
