#!/usr/bin/env bash
# Fails when a source file differs from what the formatters produce (`make fmt` fixes them).
#   clang-format for bridge/, `tinox fmt` for the Tinox sources.
set -u
here=$(cd "$(dirname "$0")/.." && pwd)
cd "$here"
TINOX=${TINOX:-tinox}
bad=0
while IFS= read -r file; do
    if ! clang-format --dry-run -Werror "$file" >/dev/null 2>&1; then
        echo "needs clang-format: $file"
        bad=1
    fi
done < <(find bridge -path bridge/build\* -prune -o \( -name '*.cpp' -o -name '*.h' \) -print | grep -v '/build' | sort)
while IFS= read -r file; do
    if ! "$TINOX" fmt "$file" 2>/dev/null | cmp -s - "$file"; then
        echo "needs tinox fmt: $file"
        bad=1
    fi
done < <(find src tests -name '*.tnx' | sort)
[ "$bad" = 0 ] && echo "formatting: all sources are formatted"
exit "$bad"
