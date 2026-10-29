#!/usr/bin/env bash
# Validate versions and create a source archive. Does not tag, push or publish.
set -euo pipefail
cd "$(dirname "$0")/.."
version=$(sed -n 's/^version = "\([^"]*\)"/\1/p' tinox.toml | head -n1)
[[ "$version" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] || { echo 'Invalid package version' >&2; exit 1; }
python3 - "$version" <<'PYVERSION'
import sys
import xml.etree.ElementTree as ET
assert ET.parse("resources/org.subnix.tinoxide.metainfo.xml").find("releases/release").get("version") == sys.argv[1], "AppStream version differs"
assert f"## {sys.argv[1]}" in open("CHANGELOG.md").read(), "Missing changelog entry"
PYVERSION
[[ -z "$(git status --porcelain --untracked-files=no)" ]] || { echo 'Commit changes before creating a release' >&2; exit 1; }
[[ "$(git describe --exact-match --tags HEAD)" == "v$version" ]] || { echo "Release requires tag v$version on HEAD" >&2; exit 1; }
mkdir -p build/release
git archive --format=tar --prefix="tinox-ide-$version/" HEAD | gzip -n > "build/release/tinox-ide-$version.tar.gz"
(cd build/release && sha256sum "tinox-ide-$version.tar.gz" > SHA256SUMS)
printf 'Created %s and SHA256SUMS\n' "build/release/tinox-ide-$version.tar.gz"
