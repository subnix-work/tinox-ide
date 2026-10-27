#!/usr/bin/env bash
# Runs the spike on a real Wayland compositor and on X11, headless: a virtual
# KWin (kwin_wayland --virtual) with XWayland, so no window appears on your desktop.
set -eu
here=$(cd "$(dirname "$0")/.." && pwd)
command -v kwin_wayland >/dev/null || { echo "kwin_wayland not found"; exit 1; }
"$here/scripts/run-spike.sh" >/dev/null 2>&1 || true   # build first (offscreen run is cheap)

inner=$(mktemp)
trap 'rm -f -- "$inner"' EXIT
cat >"$inner" <<INNER
#!/usr/bin/env bash
for plat in wayland xcb; do
  echo "== \$plat (WAYLAND_DISPLAY=\$WAYLAND_DISPLAY DISPLAY=\$DISPLAY)"
  QT_QPA_PLATFORM=\$plat "$here/scripts/run-spike.sh" 2>&1 | grep -E "startup|platform-event|latency|failures|spike ok|FAIL"
done
INNER
chmod +x "$inner"
exec kwin_wayland --virtual --xwayland --no-lockscreen --no-global-shortcuts \
    --socket "tinox-spike-$$" --exit-with-session "$inner"
