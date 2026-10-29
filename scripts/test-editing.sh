#!/usr/bin/env bash
# Real Qt/LSP editing, pre-save barriers and import quick fixes.
set -eu
here=$(cd "$(dirname "$0")/.." && pwd)
sandbox=$(mktemp -d)
trap 'rm -rf "$sandbox"' EXIT
cd "$here"
cat > "$sandbox/Main.tnx" <<'TNX'
class Main {
  fnc main() -> Int32 {
        println("a")
   return 0 }
}
TNX
cat > "$sandbox/Missing.tnx" <<'TNX'
class Missing
{
    fnc run() -> Int32
    {
        const w = Watcher::create(10)
        return 0
    }
}
TNX
cat > "$sandbox/Imports.tnx" <<'TNX'
import tinox.core.process
import tinox.core.fs
import tinox.core.fs

class Imports
{
}
TNX
"${TINOX:-tinox}" fmt "$sandbox/Main.tnx" > "$sandbox/expected.txt"
if [ -z "${TINOX_LSP:-}" ] && [ -x "$here/../tinox/target/release/tinox-lsp" ]; then
    export TINOX_LSP="$here/../tinox/target/release/tinox-lsp"
fi
scripts/test-gui-smoke.sh editing env QT_QPA_PLATFORM=offscreen TKDE_DEBUG_COMMANDS=1 TKDE_NO_KCRASH=1 \
    XDG_CONFIG_HOME="$sandbox/config" XDG_DATA_HOME="$sandbox/data" XDG_CACHE_HOME="$sandbox/cache" \
    TINOX_EDITING_ROOT="$sandbox" \
    timeout 180 "${TINOX:-tinox}" run tests/fixtures/editing/Main.tnx
"${TINOX:-tinox}" check "$sandbox/Missing.tnx"
