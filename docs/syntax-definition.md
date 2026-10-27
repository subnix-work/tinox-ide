# Tinox syntax definition

`resources/syntax/tinox.xml` is a KSyntaxHighlighting definition, used by
KTextEditor (and so by this IDE), Kate and KDevelop. It gives the editor
highlighting, folding and comment toggling from day one, independent of the
language server's semantic tokens.

## What it covers

Keywords (control flow, declarations, modifiers), the builtin types and
capitalised type names, `this`/`self`, constants, annotations, calls, all
integer forms (`0x`, `0o`, `0b`, `_` separators, `i32`/`u8`/... suffixes),
floats, strings with escapes and `${...}` interpolation (nested braces
included), raw strings `r"..."` to `r###"..."###`, character and byte
literals, `//`, `/* */` and `/** */` comments with TODO/FIXME alerts, brace
folding and `//`, `/* */` toggle-comment markers. The lexical rules come from
`docs/SPEC.md` of the language.

## Install for your user

```sh
make install-resources     # syntax definition + MIME types under ~/.local/share
```

`*.tnx` becomes `text/x-tinox`; `tinox.toml` becomes `application/x-tinox-project`
(a TOML subtype).

## Tests

```sh
make test                  # includes scripts/test-syntax.sh
scripts/test-syntax.sh --update   # rewrite the HTML snapshots after an intended change
```

* HTML snapshots of `resources/syntax/testdata/*.tnx` (theme "Breeze Light").
* An open-region check: every file must end back in the `Normal` context.
  It runs over the repo's test data and, when a Tinox checkout is next to this
  one (or `TINOX_CORPUS` points to one), over all its `.tnx` files.
  Files that are unterminated on purpose go into `resources/syntax/corpus-ignore.txt`.

## Upstreaming to KDE

The definition is meant to be contributed to
[KDE's syntax-highlighting](https://invent.kde.org/frameworks/syntax-highlighting).
Checklist for the merge request:

1. Copy `tinox.xml` to `data/syntax/` and set `version`, `kateversion` and `license` as in neighbouring files.
2. Add a test input `autotests/input/highlight.tnx` and the generated `autotests/reference/highlight.tnx.html` (and `.ansi`) from the files in `resources/syntax/testdata/`; `make` in a build of the frameworks regenerates references.
3. Run the framework's `ctest` (it also validates the XML against `language.xsd`).
4. Mention the language homepage, `*.tnx`, and that the grammar is stable per `docs/SPEC.md`.
