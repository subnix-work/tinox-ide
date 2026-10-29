#!/usr/bin/env bash
# Drives the real IDE, bridge and language server on Qt's offscreen platform.
set -eu
here=$(cd "$(dirname "$0")/.." && pwd)
sandbox=$(mktemp -d)
trap 'rm -rf "$sandbox"' EXIT
cd "$here"
# Prefer the development server over stale user installations; explicit overrides win.
if [ -z "${TINOX_LSP:-}" ] && [ -x "$here/../tinox/target/release/tinox-lsp" ]; then
    export TINOX_LSP="$here/../tinox/target/release/tinox-lsp"
fi
printf 'class Main\n{\n}\n' > "$sandbox/Main.tnx"
scripts/test-gui-smoke.sh diagnostics env QT_QPA_PLATFORM=offscreen TKDE_DEBUG_COMMANDS=1 TKDE_NO_KCRASH=1 \
    XDG_CONFIG_HOME="$sandbox/config" XDG_DATA_HOME="$sandbox/data" XDG_CACHE_HOME="$sandbox/cache" \
    TINOX_DIAGNOSTICS_FILE="$sandbox/Main.tnx" \
    timeout 120 "${TINOX:-tinox}" run tests/fixtures/diagnostics/Main.tnx
