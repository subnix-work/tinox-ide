#!/usr/bin/env bash
# Installs the translations like a package would and drives the real IDE under LANGUAGE=de.
set -eu
here=$(cd "$(dirname "$0")/.." && pwd)
sandbox=$(mktemp -d)
trap 'rm -rf "$sandbox"' EXIT
cd "$here"
python3 scripts/i18n-extract.py --check
cmake --install bridge/build --prefix "$sandbox/prefix" >/dev/null
scripts/test-gui-smoke.sh i18n env QT_QPA_PLATFORM=offscreen TKDE_DEBUG_COMMANDS=1 TKDE_NO_KCRASH=1 \
    LANGUAGE=de LC_ALL=de_DE.UTF-8 XDG_DATA_DIRS="$sandbox/prefix/share:/usr/local/share:/usr/share" \
    XDG_CONFIG_HOME="$sandbox/config" XDG_DATA_HOME="$sandbox/data" XDG_CACHE_HOME="$sandbox/cache" \
    timeout 120 "${TINOX:-tinox}" run tests/fixtures/i18n/Main.tnx
