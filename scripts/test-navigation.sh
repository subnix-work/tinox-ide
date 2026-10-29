#!/usr/bin/env bash
set -eu
here=$(cd "$(dirname "$0")/.." && pwd)
sandbox=$(mktemp -d)
trap 'rm -rf "$sandbox"' EXIT
cd "$here"
cat > "$sandbox/Widget.tnx" <<'TNX'
class Widget
{
    fnc readNumber() -> Int64
    {
        return 42
    }
}
TNX
cat > "$sandbox/Caller.tnx" <<'TNX'
class Caller
{
    fnc run() -> Int64
    {
        return Widget::readNumber()
    }
}
TNX
printf '[package]\nname = "navigation-test"\nversion = "0.1.0"\nentry = "Caller.tnx"\n' > "$sandbox/tinox.toml"
if [ -z "${TINOX_LSP:-}" ] && [ -x "$here/../tinox/target/release/tinox-lsp" ]; then
    export TINOX_LSP="$here/../tinox/target/release/tinox-lsp"
fi
env QT_QPA_PLATFORM=offscreen TKDE_DEBUG_COMMANDS=1 TKDE_NO_KCRASH=1 \
    XDG_CONFIG_HOME="$sandbox/config" XDG_DATA_HOME="$sandbox/data" XDG_CACHE_HOME="$sandbox/cache" \
    TINOX_NAVIGATION_ROOT="$sandbox" \
    timeout 150 "${TINOX:-tinox}" run tests/fixtures/navigation/Main.tnx
