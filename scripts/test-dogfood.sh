#!/usr/bin/env bash
# Self-hosting acceptance on the real IDE/bridge; all edits/build outputs stay in a temporary copy.
set -euo pipefail
here=$(cd "$(dirname "$0")/.." && pwd)
sandbox=$(mktemp -d /tmp/tinoxide-dogfood-XXXXXX)
trap 'rm -rf "$sandbox"' EXIT
cd "$here"
compiler=$(realpath "$(command -v "${TINOX:-tinox}")")
# Copy the current source (including uncommitted work), never an old HEAD or user configuration.
cp -a src tests resources tinox.toml "$sandbox/"
mkdir -p "$sandbox/bridge/build" "$sandbox/data/knotifications6"
ln -s "$here/bridge/build/bin" "$sandbox/bridge/build/bin"
sed 's/^Action=Popup$/Action=None/' resources/notifications/tinoxide.notifyrc > "$sandbox/data/knotifications6/tinoxide.notifyrc"
if [ -z "${TINOX_LSP:-}" ] && [ -x "$here/../tinox/target/release/tinox-lsp" ]; then
    export TINOX_LSP="$here/../tinox/target/release/tinox-lsp"
fi
scripts/test-gui-smoke.sh dogfooding env QT_QPA_PLATFORM=offscreen TKDE_NO_KCRASH=1 \
    XDG_CONFIG_HOME="$sandbox/config" XDG_DATA_HOME="$sandbox/data" XDG_CACHE_HOME="$sandbox/cache" \
    TINOX_DOGFOOD_ROOT="$sandbox" TINOX_DOGFOOD_COMPILER="$compiler" \
    timeout 420 "$compiler" run tests/fixtures/dogfood/Main.tnx
