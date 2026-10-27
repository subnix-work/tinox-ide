#!/usr/bin/env bash
# Runs the libFuzzer command fuzzer (bridge/fuzz) for N seconds (default 60).
#   scripts/fuzz.sh [seconds] [extra libFuzzer options...]
# The corpus is kept in bridge/fuzz/corpus (seeds, committed) and a working copy in
# build/fuzz-corpus (grows while fuzzing, not committed). Crashes land in build/fuzz-artifacts/.
# The inputs can name files: run this in a container or CI job, not on a machine with valuables.
set -eu
here=$(cd "$(dirname "$0")/.." && pwd)
seconds=${1:-60}
[ $# -gt 0 ] && shift
build="$here/bridge/build-fuzz"

cmake -S "$here/bridge" -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
    -DTKDE_SANITIZE=address,undefined -DTKDE_FUZZ=ON >/dev/null
cmake --build "$build" --target command_fuzzer

work="$here/build/fuzz-corpus"
artifacts="$here/build/fuzz-artifacts"
mkdir -p "$work" "$artifacts"

export ASAN_OPTIONS=detect_leaks=1:abort_on_error=0:detect_stack_use_after_return=0
export LSAN_OPTIONS="suppressions=$here/bridge/tests/lsan.supp:print_suppressions=0"
export UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1
exec "$build/bin/command_fuzzer" "$work" "$here/bridge/fuzz/corpus" \
    -dict="$here/bridge/fuzz/command.dict" -max_len=4096 -timeout=20 -rss_limit_mb=4096 \
    -max_total_time="$seconds" -artifact_prefix="$artifacts/" "$@"
