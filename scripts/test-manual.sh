#!/usr/bin/env bash
# Compile and run the exact first-project examples from the manual, not a second copy.
set -euo pipefail
here=$(cd "$(dirname "$0")/.." && pwd)
compiler=$(realpath "$(command -v "${TINOX:-tinox}")")
sandbox=$(mktemp -d /tmp/tinoxide-manual-XXXXXX)
trap 'rm -rf "$sandbox"' EXIT
python3 - "$here/docs/manual/first-project.md" "$sandbox" "$here/docs/manual/build-run-test.md" <<'PY'
from pathlib import Path
import re
import json
import sys
source = Path(sys.argv[1]).read_text()
blocks = re.findall(r'```(toml|tinox)\n(.*?)\n```', source, re.S)
assert [lang for lang, _ in blocks] == ['toml', 'tinox', 'tinox'], 'Update tutorial check for changed examples'
root = Path(sys.argv[2])
for name, (_, text) in zip(['tinox.toml', 'src/Main.tnx', 'tests/SmokeTest.tnx'], blocks):
    path = root / name
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text + '\n')
for block in re.findall(r'```json\n(.*?)\n```', Path(sys.argv[3]).read_text(), re.S):
    config = json.loads(block)
    assert config['configurations'][1]['args'] == '--name \"Ada Lovelace\"', 'Keep the documented quoted argument as one group'
PY
cd "$sandbox"
"$compiler" check src/Main.tnx
"$compiler" build -o "$sandbox/hello"
[[ "$("$sandbox/hello")" == 'Hello from Tinox IDE' ]]
"$compiler" test tests/SmokeTest.tnx > "$sandbox/test.log" 2>&1
cat "$sandbox/test.log"
grep -q '1 passed, 0 failed' "$sandbox/test.log"
echo 'manual: exact first-project manifest/source/test check, build, run and test passed'
