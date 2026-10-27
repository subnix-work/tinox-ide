#!/usr/bin/env bash
# Tests resources/syntax/tinox.xml with kate-syntax-highlighter.
#   scripts/test-syntax.sh           compare against the HTML snapshots, check a corpus
#   scripts/test-syntax.sh --update  rewrite the snapshots from the current definition
# Corpus: every .tnx file below $TINOX_CORPUS (default ../tinox when it exists);
# the highlighter must end each file back in the Normal context. Files that are
# unterminated on purpose are listed in resources/syntax/corpus-ignore.txt.
set -eu
here=$(cd "$(dirname "$0")/.." && pwd)
syn="$here/resources/syntax"
update=0
[ "${1:-}" = "--update" ] && update=1

command -v kate-syntax-highlighter >/dev/null 2>&1 || { echo "SKIP: kate-syntax-highlighter not installed (package: syntax-highlighting)"; exit 0; }

tmp=$(mktemp -d)
trap 'rm -rf -- "$tmp"' EXIT
mkdir -p "$tmp/org.kde.syntax-highlighting/syntax"
cp "$syn/tinox.xml" "$tmp/org.kde.syntax-highlighting/syntax/"
export XDG_DATA_HOME="$tmp"

# The definition must load (a parse error makes the syntax unknown).
kate-syntax-highlighter --list | grep -qx Tinox || { echo "FAIL: the Tinox definition does not load"; exit 1; }
if command -v xmllint >/dev/null 2>&1; then xmllint --noout "$syn/tinox.xml" "$here/resources/mime/tinox.xml" || exit 1; fi

highlight() { kate-syntax-highlighter -s Tinox -f html -t "Breeze Light" -T "snapshot" "$1" 2>/dev/null; }

fail=0
for f in "$syn"/testdata/*.tnx; do
    snap="$f.html"
    if [ "$update" = 1 ]; then
        highlight "$f" >"$snap"; echo "updated ${snap#$here/}"
    elif [ ! -f "$snap" ]; then
        echo "FAIL: no snapshot for ${f#$here/} (run with --update)"; fail=1
    elif ! highlight "$f" | diff -u "$snap" - >"$tmp/diff"; then
        echo "FAIL: snapshot differs for ${f#$here/}"; head -30 "$tmp/diff"; fail=1
    else
        echo "ok   ${f#$here/}"
    fi
done

# Ends in the Normal context? (trace line "~(1)" = stack of depth 1)
ends_normal() {
    kate-syntax-highlighter -s Tinox -f ansi --syntax-trace=stackSize "$1" 2>/dev/null | sed 's/\x1b\[[0-9;]*[mK]//g' | grep '~(' | tail -n1 | grep -q '~(1)'
}
for f in "$syn"/testdata/*.tnx; do
    ends_normal "$f" || { echo "FAIL: ${f#$here/} ends inside an open region"; fail=1; }
done

# a deliberately broken string must be noticed by the check itself
printf 'class A\n{\n    const s = "never closed\n}\n' >"$tmp/broken.tnx"
if ends_normal "$tmp/broken.tnx"; then echo "FAIL: the open-region check cannot detect an unterminated string"; fail=1; fi

corpus=${TINOX_CORPUS:-$here/../tinox}
if [ -d "$corpus" ]; then
    n=0; bad=0
    ignore="$syn/corpus-ignore.txt"
    while IFS= read -r f; do
        rel=${f#$corpus/}
        if [ -f "$ignore" ] && grep -qxF "$rel" "$ignore"; then continue; fi
        n=$((n + 1))
        if ! ends_normal "$f"; then echo "FAIL: corpus file ends inside an open region: $rel"; bad=$((bad + 1)); fi
    done < <(find "$corpus" -name '*.tnx' -not -path '*/target/*' -not -path '*/node_modules/*' -not -path '*/.tinox/*' | sort)
    echo "corpus: $n files checked, $bad problems"
    [ "$bad" = 0 ] || fail=1
else
    echo "corpus: skipped ($corpus not found; set TINOX_CORPUS)"
fi

[ "$fail" = 0 ] && echo "syntax tests passed" || exit 1
