#!/usr/bin/env bash
# Runs examples/shell on the REAL bridge, headless: the program builds its window, menus, tree and
# editor, then leaves at once (TINOXIDE_EXAMPLE_QUIT=1). It must exit 0 and print nothing on stderr:
# every failure of a command or handler is reported there.
set -eu
here=$(cd "$(dirname "$0")/.." && pwd)
sandbox=$(mktemp -d)
trap 'rm -rf "$sandbox"' EXIT
cd "$here"
errors=$(QT_QPA_PLATFORM=offscreen TINOXIDE_EXAMPLE_QUIT=1 TKDE_NO_KCRASH=1 \
    XDG_CONFIG_HOME="$sandbox/config" XDG_DATA_HOME="$sandbox/data" XDG_CACHE_HOME="$sandbox/cache" \
    timeout 120 "${TINOX:-tinox}" run examples/shell/Main.tnx 2>&1 >/dev/null) || {
    echo "example failed: $errors" >&2
    exit 1
}
if [ -n "$errors" ]; then
    echo "example reported problems on stderr: $errors" >&2
    exit 1
fi
echo "example: window, menus, tree and editor built on the real bridge"
