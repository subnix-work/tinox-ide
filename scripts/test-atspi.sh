#!/usr/bin/env bash
# Reads the accessibility tree of the real IDE over AT-SPI, as a screen reader (Orca) would.
# Needs a display and a running AT-SPI bus, so it is not part of `make test`; run `make test-atspi`.
# It opens a window for a few seconds.
set -eu
here=$(cd "$(dirname "$0")/.." && pwd)
if [ -z "${DISPLAY:-}${WAYLAND_DISPLAY:-}" ]; then
    echo "atspi check skipped: no display"
    exit 0
fi
if ! python3 -c 'import gi; gi.require_version("Atspi", "2.0")' 2>/dev/null; then
    echo "atspi check skipped: python-gobject with Atspi is not installed"
    exit 0
fi
sandbox=$(mktemp -d)
log=$sandbox/app.log
trap 'touch "$sandbox/done"; kill $app 2>/dev/null || true; rm -rf "$sandbox"' EXIT
cd "$here"
printf 'class Main\n{\n}\n' > "$sandbox/Main.tnx"
printf '[package]\ncompiler = "2.9.0"\nname = "a11y-test"\nversion = "0.1.0"\nentry = "Main.tnx"\n' > "$sandbox/tinox.toml"
env QT_QPA_PLATFORM=xcb TKDE_DEBUG_COMMANDS=1 TKDE_NO_KCRASH=1 QT_LINUX_ACCESSIBILITY_ALWAYS_ON=1 \
    XDG_CONFIG_HOME="$sandbox/config" XDG_DATA_HOME="$sandbox/data" XDG_CACHE_HOME="$sandbox/cache" \
    TINOX_A11Y_ROOT="$sandbox" TINOX_A11Y_HOLD="$sandbox/done" \
    "${TINOX:-tinox}" run tests/fixtures/accessibility/Main.tnx > "$log" 2>&1 &
app=$!
for _ in $(seq 1 600); do
    grep -q "holding for the AT-SPI check" "$log" && break
    grep -q "smoke failed" "$log" && { cat "$log" >&2; exit 1; }
    sleep 0.5
done
grep -q "holding for the AT-SPI check" "$log" || { echo "the IDE did not come up" >&2; cat "$log" >&2; exit 1; }
python3 scripts/atspi-tree.py tinoxide \
    "frame:a11y — Tinox IDE — Tinox Accessibility Smoke" \
    "filler:Open files" "filler:Editor: Main.tnx" "filler:Problems and output panels" \
    "button:Open Folder…" "check box:Show Project" > "$sandbox/tree.txt"
cat "$sandbox/tree.txt" | head -5
echo "atspi: the application, its window, the open files, the editor and the panels are named in the accessibility tree"
