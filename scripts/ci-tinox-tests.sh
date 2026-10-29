#!/usr/bin/env bash
# Runs the Tinox tests of tests/ one file at a time, for one shard of several (CI matrix).
#   scripts/ci-tinox-tests.sh <shard> <shards>        shard counts from 0
# `tinox test` without a file compiles and discovers the whole workspace, which is too slow for CI (#63);
# a file at a time also shows which file fails. Files without @Test (helpers) are skipped.
set -u
shard=${1:?shard}
shards=${2:?number of shards}
here=$(cd "$(dirname "$0")/.." && pwd)
cd "$here"
TINOX=${TINOX:-tinox}
index=0
failed=()
ran=0
for file in $(grep -l '@Test' tests/*.tnx | sort); do
    if [ $((index % shards)) -eq "$shard" ]; then
        echo "=== $file"
        ran=$((ran + 1))
        if ! timeout 900 "$TINOX" test "$file"; then
            failed+=("$file")
        fi
    fi
    index=$((index + 1))
done
echo "shard $shard/$shards: $ran files, ${#failed[@]} failed"
if [ "${#failed[@]}" -gt 0 ]; then
    printf 'failed: %s\n' "${failed[@]}"
    exit 1
fi
