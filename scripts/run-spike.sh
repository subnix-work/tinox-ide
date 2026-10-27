#!/usr/bin/env bash
# Builds and runs the architecture spike (issue #7): Qt on the main thread,
# Tinox logic on a spawned task, queues in between.
#   scripts/run-spike.sh                    offscreen platform
#   QT_QPA_PLATFORM=wayland scripts/run-spike.sh
#   TKDE_SPIKE_ROUNDS=15000000 scripts/run-spike.sh      GC soak (about 5 minutes)
set -eu
here=$(cd "$(dirname "$0")/.." && pwd)
cd "$here"
TINOX=${TINOX:-$(command -v tinox 2>/dev/null || echo ../tinox/target/release/tinox)}

mkdir -p build
"$TINOX" build spike/Main.tnx -o build/spike

# Use the repo's syntax definition without touching the user's data dir.
data=$(mktemp -d)
trap 'rm -rf -- "$data"' EXIT
XDG_DATA_HOME=$data scripts/install-resources.sh >/dev/null
export XDG_DATA_HOME=$data
export QT_QPA_PLATFORM=${QT_QPA_PLATFORM:-offscreen}
exec ./build/spike
