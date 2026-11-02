#!/usr/bin/env bash
# Drives the real IDE on Qt's offscreen platform and asks the bridge for every keyboard-focusable
# widget that has no accessible name (what a screen reader would announce as an unlabelled control).
set -eu
here=$(cd "$(dirname "$0")/.." && pwd)
sandbox=$(mktemp -d)
trap 'rm -rf "$sandbox"' EXIT
cd "$here"
printf 'class Main\n{\n}\n' > "$sandbox/Main.tnx"
printf '[package]\ncompiler = "2.9.0"\nname = "a11y-test"\nversion = "0.1.0"\nentry = "Main.tnx"\n' > "$sandbox/tinox.toml"
scripts/test-gui-smoke.sh accessibility env QT_QPA_PLATFORM=offscreen TKDE_DEBUG_COMMANDS=1 TKDE_NO_KCRASH=1 \
    XDG_CONFIG_HOME="$sandbox/config" XDG_DATA_HOME="$sandbox/data" XDG_CACHE_HOME="$sandbox/cache" \
    TINOX_A11Y_ROOT="$sandbox" \
    timeout 200 "${TINOX:-tinox}" run tests/fixtures/accessibility/Main.tnx
