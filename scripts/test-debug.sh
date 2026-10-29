#!/usr/bin/env bash
# Drives the real IDE, bridge, `tinox build --debug` and `tinox dap` (gdb) on Qt's offscreen platform:
# a gutter breakpoint, the stop with locals, a step and the end of the program.
set -eu
here=$(cd "$(dirname "$0")/.." && pwd)
sandbox=$(mktemp -d)
trap 'rm -rf "$sandbox"' EXIT
cd "$here"
if ! command -v gdb >/dev/null 2>&1; then
    echo "debug smoke skipped: gdb is not installed"
    exit 0
fi
cat > "$sandbox/Main.tnx" <<'TNX'
class Main
{
    fnc square(n: Int64) -> Int64
    {
        const result: Int64 = n * n
        return result
    }

    fnc main() -> Int32
    {
        const total: Int64 = Main::square(7)
        println(total.toString())
        return 0
    }
}
TNX
printf '[package]\ncompiler = "2.8.0"\nname = "debug-test"\nversion = "0.1.0"\nentry = "Main.tnx"\n' > "$sandbox/tinox.toml"
scripts/test-gui-smoke.sh debug env QT_QPA_PLATFORM=offscreen TKDE_DEBUG_COMMANDS=1 TKDE_NO_KCRASH=1 \
    XDG_CONFIG_HOME="$sandbox/config" XDG_DATA_HOME="$sandbox/data" XDG_CACHE_HOME="$sandbox/cache" \
    TINOX_DEBUG_ROOT="$sandbox" \
    timeout 300 "${TINOX:-tinox}" run tests/fixtures/debug/Main.tnx
