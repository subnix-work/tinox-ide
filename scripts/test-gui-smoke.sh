#!/usr/bin/env bash
# Propagate GUI smoke failures and reject stderr beyond the application's normal INFO lines.
set -eu
smoke_label=$1
shift
smoke_errors=$(mktemp)
trap 'rm -f "$smoke_errors"' EXIT
smoke_status=0
"$@" 2>"$smoke_errors" || smoke_status=$?
cat "$smoke_errors" >&2
if [ "$smoke_status" -ne 0 ]; then
    exit "$smoke_status"
fi
if grep -v '^tinox-ide: INFO  ' "$smoke_errors" | grep -q '[^[:space:]]'; then
    echo "$smoke_label smoke reported unexpected stderr" >&2
    exit 1
fi
