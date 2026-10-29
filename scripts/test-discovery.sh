#!/usr/bin/env bash
# Drives the real IDE: Discover Tests in Current File on tests/HostCommandTest.tnx of this repository
# (not the whole suite), then Run Tests in Selected File on what it found.
set -eu
here=$(cd "$(dirname "$0")/.." && pwd)
sandbox=$(mktemp -d)
trap 'rm -rf "$sandbox"' EXIT
cd "$here"
scripts/test-gui-smoke.sh discovery env QT_QPA_PLATFORM=offscreen TKDE_DEBUG_COMMANDS=1 TKDE_NO_KCRASH=1 \
    XDG_CONFIG_HOME="$sandbox/config" XDG_DATA_HOME="$sandbox/data" XDG_CACHE_HOME="$sandbox/cache" \
    TINOX_DISCOVERY_ROOT="$here" \
    timeout 400 "${TINOX:-tinox}" run tests/fixtures/discovery/Main.tnx
