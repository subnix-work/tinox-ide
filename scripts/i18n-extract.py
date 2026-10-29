#!/usr/bin/env python3
"""Collects the translatable strings of the IDE into po/tinox-ide.pot.

Tinox has no message API of its own (subnix-work/tinox#588), so the strings are English literals that
the bridge translates on their way to Qt (docs/i18n.md). This script finds them where they are written:

  * Tinox sources in src/: the text arguments of the UI builders and commands listed in TINOX_PATTERNS,
    and `Tr::t / Tr::c / Tr::f / Tr::n`;
  * bridge sources: `i18n("...")`, `i18nc("ctx", "...")`, `i18np("one", "many", ...)`.

  scripts/i18n-extract.py            write po/tinox-ide.pot
  scripts/i18n-extract.py --check    fail when the committed file is out of date
  scripts/i18n-extract.py --update   also merge the new template into po/<lang>/tinox-ide.po
"""
import pathlib
import re
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
POT = ROOT / "po" / "tinox-ide.pot"

# a Tinox string literal: no raw strings, escapes are \" \\ \n \t
STR = r'"((?:[^"\\\n]|\\.)*)"'
SKIP_ARG = r'\s*(?:[^,()"]|"(?:[^"\\\n]|\\.)*")*?,\s*'  # one argument that is not the text (the key)

# (regex with the text as group 1 [and a context in group "ctx"], description)
TINOX_PATTERNS = [
    # commands: the title of IdeCommand::create("id", "Title") and its category (the menu it belongs to)
    (r'IdeCommand::create\(\s*"[^"]*"\s*,\s*' + STR, "command title"),
    (r'\.category\(' + STR + r'\)', "command category"),
    # widgets and their texts
    (r'Ui::(?:label|button|checkBox)\(\s*[^,()]*,\s*' + STR, "widget text"),
    (r'Ui::(?:window|toolBar)\(\s*[^,()]*,\s*' + STR, "window title"),
    (r'Ui::dock\(\s*[^,()]*,\s*' + STR, "dock title"),
    (r'\.(?:toolTip|accessibleName|accessibleDescription|labelled|windowTitle)\(' + STR + r'\)', "widget property"),
    (r'\.inTab\(' + STR, "tab title"),
    (r'\.inTabTip\(' + STR, "tab title"),
    (r'\.inTabTip\(\s*' + STR + r'\s*,\s*' + STR + r'\s*,\s*' + STR, "tab tip"),
    (r'\.str\("placeholder",\s*' + STR, "placeholder"),
    (r'Menu::create\(' + STR + r'\)', "menu title"),
    # dialogs: title and (when it is a plain literal) the text
    (r'\.(?:error|info|warn|ask|confirm)\(\s*' + STR + r'\s*,', "dialog title"),
    (r'\.(?:error|info|warn|ask|confirm)\(\s*' + STR + r'\s*,\s*' + STR + r'\s*[,)]', "dialog text"),
    # preferences
    (r'PrefDef::(?:bool|choice|text|path)\(\s*"[^"]*"\s*,\s*"[^"]*"\s*,\s*"[^"]*"\s*,\s*' + STR, "preference"),
    (r'PrefDef::int\(\s*"[^"]*"\s*,\s*"[^"]*"\s*,\s*"[^"]*"\s*,\s*' + STR, "preference"),
    # status bar messages that are plain literals
    (r'"status\.message",\s*' + STR + r'\s*\)', "status message"),
]
TR_PATTERNS = [
    (r'Tr::t\(\s*' + STR, None, "Tr::t"),
    (r'Tr::f\(\s*' + STR, None, "Tr::f"),
    (r'Tr::c\(\s*' + STR + r'\s*,\s*' + STR, "ctx", "Tr::c"),
    (r'Tr::n\(\s*' + STR + r'\s*,\s*' + STR, "plural", "Tr::n"),
]
CPP_PATTERNS = [
    (r'\bi18n\(\s*QStringLiteral\(' + STR + r'\)|\bi18n\(\s*' + STR, None),
    (r'\bi18nc\(\s*' + STR + r'\s*,\s*' + STR, "ctx"),
    (r'\bi18np\(\s*' + STR + r'\s*,\s*' + STR, "plural"),
]
# strings that are code or data, not words: skip them
SKIP = re.compile(r'^(?:[\s\W\d]*|[a-z]+(?:\.[a-zA-Z]+)+)$')


def unescape(text):
    return text.replace('\\"', '"').replace("\\n", "\n").replace("\\t", "\t").replace("\\\\", "\\")


def escape(text):
    return text.replace("\\", "\\\\").replace('"', '\\"').replace("\n", "\\n").replace("\t", "\\t")


class Catalog:
    def __init__(self):
        self.entries = {}  # (ctx, msgid, plural) -> list of "file:line"

    def add(self, text, where, ctx="", plural=""):
        text = unescape(text)
        if not text.strip() or SKIP.match(text):
            return
        self.entries.setdefault((ctx, text, unescape(plural)), []).append(where)


def strip_comments(source, line_comment):
    """The source with comments blanked out (same length, same lines), so examples in doc comments are not messages."""
    out = []
    i = 0
    n = len(source)
    while i < n:
        c = source[i]
        if c == '"':  # a string literal: copy it as it is
            j = i + 1
            while j < n and source[j] != '"' and source[j] != "\n":
                j += 2 if source[j] == "\\" else 1
            out.append(source[i:j + 1])
            i = j + 1
        elif source.startswith("/*", i):
            j = source.find("*/", i + 2)
            j = n if j < 0 else j + 2
            out.append("".join(ch if ch == "\n" else " " for ch in source[i:j]))
            i = j
        elif source.startswith(line_comment, i):
            j = source.find("\n", i)
            j = n if j < 0 else j
            out.append(" " * (j - i))
            i = j
        else:
            out.append(c)
            i += 1
    return "".join(out)


def line_of(source, offset):
    return source.count("\n", 0, offset) + 1


def scan_tinox(catalog):
    for path in sorted((ROOT / "src").rglob("*.tnx")):
        source = strip_comments(path.read_text(), "//")
        rel = path.relative_to(ROOT)
        # Model::create(events, ["Name", "Value"], ...): the column headers
        for m in re.finditer(r'Model::create\([^,()]+,\s*\[([^\]]*)\]', source):
            for column in re.findall(STR, m.group(1)):
                catalog.add(column, f"{rel}:{line_of(source, m.start())}")
        for pattern, _ in TINOX_PATTERNS:
            for m in re.finditer(pattern, source):
                where = f"{rel}:{line_of(source, m.start())}"
                # inTabTip(title, icon, tip) has the tip as its last group; the title is group 1 for both
                texts = [g for g in m.groups() if g is not None]
                if "inTabTip" in pattern and len(texts) == 3:
                    texts = [texts[0], texts[2]]
                for g in texts:
                    catalog.add(g, where)
        for pattern in TR_PATTERNS:
            regex, kind, _ = pattern if len(pattern) == 3 else (pattern[0], None, pattern[-1])
            for m in re.finditer(regex, source):
                where = f"{rel}:{line_of(source, m.start())}"
                if kind == "ctx":
                    catalog.add(m.group(2), where, ctx=unescape(m.group(1)))
                elif kind == "plural":
                    catalog.add(m.group(1), where, plural=m.group(2))
                else:
                    catalog.add(m.group(1), where)


def scan_cpp(catalog):
    for path in sorted((ROOT / "bridge" / "src").rglob("*.cpp")):
        source = strip_comments(path.read_text(), "//")
        rel = path.relative_to(ROOT)
        for regex, kind in CPP_PATTERNS:
            for m in re.finditer(regex, source):
                where = f"{rel}:{line_of(source, m.start())}"
                groups = [g for g in m.groups() if g is not None]
                if kind == "ctx":
                    catalog.add(groups[1], where, ctx=unescape(groups[0]))
                elif kind == "plural":
                    catalog.add(groups[0], where, plural=groups[1])
                else:
                    catalog.add(groups[0], where)


def render(catalog):
    out = [
        "# Translation template of the Tinox IDE (generated by scripts/i18n-extract.py; do not edit).",
        "msgid \"\"",
        "msgstr \"\"",
        "\"Project-Id-Version: tinox-ide\\n\"",
        "\"MIME-Version: 1.0\\n\"",
        "\"Content-Type: text/plain; charset=UTF-8\\n\"",
        "\"Content-Transfer-Encoding: 8bit\\n\"",
        "",
    ]
    for (ctx, text, plural), where in sorted(catalog.entries.items(), key=lambda kv: (kv[0][1].lower(), kv[0][0])):
        out.append("#: " + " ".join(sorted(set(where))[:4]))
        if "&" in text:
            out.append("#, no-c-format")
        if ctx:
            out.append(f'msgctxt "{escape(ctx)}"')
        out.append(f'msgid "{escape(text)}"')
        if plural:
            out.append(f'msgid_plural "{escape(plural)}"')
            out.append('msgstr[0] ""')
            out.append('msgstr[1] ""')
        else:
            out.append('msgstr ""')
        out.append("")
    return "\n".join(out)


def main():
    catalog = Catalog()
    scan_tinox(catalog)
    scan_cpp(catalog)
    text = render(catalog)
    if "--check" in sys.argv:
        if not POT.exists() or POT.read_text() != text:
            print("po/tinox-ide.pot is out of date: run scripts/i18n-extract.py", file=sys.stderr)
            return 1
        print(f"i18n: po/tinox-ide.pot is up to date ({len(catalog.entries)} messages)")
        return 0
    POT.parent.mkdir(exist_ok=True)
    POT.write_text(text)
    print(f"i18n: {len(catalog.entries)} messages written to po/tinox-ide.pot")
    if "--update" in sys.argv:
        for po in sorted((ROOT / "po").glob("*/tinox-ide.po")):
            subprocess.run(["msgmerge", "--quiet", "--update", "--backup=none", str(po), str(POT)], check=True)
            print(f"i18n: merged into {po.relative_to(ROOT)}")
    return 0


sys.exit(main())
