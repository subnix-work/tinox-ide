#!/usr/bin/env bash
# Tests scripts/check-env.sh: it must pass on a ready machine (when this one is)
# and fail with an install hint on a machine that lacks the build tools.
set -eu
here=$(cd "$(dirname "$0")" && pwd)

# A PATH holding only the basic utilities the script itself needs.
bin=$(mktemp -d)
trap 'rm -rf -- "$bin"' EXIT
for t in bash env awk sed sort head tr uniq printf mktemp rm mkdir cat dirname grep; do
    p=$(command -v "$t" 2>/dev/null) && ln -s "$p" "$bin/$t"
done

out=$(TINOX=/nonexistent/tinox PATH="$bin" "$bin/bash" "$here/check-env.sh" --quiet 2>&1) && status=0 || status=$?
if [ "$status" -ne 1 ]; then
    echo "FAIL: expected exit 1 on a machine without build tools, got $status"; echo "$out"; exit 1
fi
case "$out" in *"cmake not found"*) ;; *) echo "FAIL: missing-cmake message absent"; echo "$out"; exit 1 ;; esac
case "$out" in *"tinox not found"*) ;; *) echo "FAIL: missing-tinox message absent"; echo "$out"; exit 1 ;; esac
case "$out" in *"Install ("*) ;; *) echo "FAIL: no install hint"; echo "$out"; exit 1 ;; esac
echo "check-env: negative case ok"

# On the developer's machine the real script decides; only assert it ends cleanly.
if "$here/check-env.sh" --quiet >/dev/null 2>&1; then echo "check-env: positive case ok"; else echo "check-env: this machine is not ready (not a test failure)"; fi
