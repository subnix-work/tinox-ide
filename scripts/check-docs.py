#!/usr/bin/env python3
"""Every public symbol of the Tinox API needs a doc comment (`/** ... */` directly above it).

Checked: classes, interfaces, methods (fn / fnc) and fields (var) in src/tinox/ide. Run by `make test`;
`tinox doc` turns the comments into the API documentation (`make docs`).
"""
import pathlib
import re
import sys

root = pathlib.Path(__file__).resolve().parent.parent / "src" / "tinox" / "ide"
# members sit at 8 spaces (namespace, class body), types at 4; deeper lines are locals
declaration = re.compile(r"^(?: {8}(?:(?:fn|fnc)\s+\w+\s*\(|var\s+\w+\s*:)| {4}(?:class|interface)\s+\w+)")
missing = []
for path in sorted(root.rglob("*.tnx")):
    lines = path.read_text().splitlines()
    for number, line in enumerate(lines):
        if not declaration.match(line):
            continue
        # skip over annotations and blank lines between the comment and the declaration
        before = number - 1
        while before >= 0 and (not lines[before].strip() or lines[before].lstrip().startswith("@")):
            before -= 1
        if before < 0 or not lines[before].rstrip().endswith("*/"):
            missing.append(f"{path.relative_to(root.parent.parent.parent.parent)}:{number + 1}: {line.strip()}")
if missing:
    print(f"{len(missing)} public symbols without a doc comment:")
    print("\n".join(missing))
    sys.exit(1)
print("doc comments: every public symbol is documented")
