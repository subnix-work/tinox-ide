#!/usr/bin/env bash
# Full semantic tokens through the real LSP, Tinox logic and Qt moving ranges.
set -eu
here=$(cd "$(dirname "$0")/.." && pwd)
sandbox=$(mktemp -d)
trap 'rm -rf "$sandbox"' EXIT
cd "$here"
cat > "$sandbox/Main.tnx" <<'TNX'
class Main
{
    var field: Int64
    fn method() -> Int64
    {
        return this.field
    }
}
TNX
if [ -z "${TINOX_LSP:-}" ] && [ -x "$here/../tinox/target/release/tinox-lsp" ]; then
    export TINOX_LSP="$here/../tinox/target/release/tinox-lsp"
fi
scripts/test-gui-smoke.sh semantics env QT_QPA_PLATFORM=offscreen TKDE_DEBUG_COMMANDS=1 TKDE_NO_KCRASH=1 \
    XDG_CONFIG_HOME="$sandbox/config" XDG_DATA_HOME="$sandbox/data" XDG_CACHE_HOME="$sandbox/cache" \
    TINOX_SEMANTICS_ROOT="$sandbox" \
    timeout 180 "${TINOX:-tinox}" run tests/fixtures/semantics/Main.tnx
