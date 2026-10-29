#!/usr/bin/env python3
"""Render repository manuals and connect them to tinox doc; reject broken local HTML links."""
import html
from html.parser import HTMLParser
import os
from pathlib import Path
import re
import sys
from urllib.parse import quote, unquote, urlsplit

try:
    import markdown
except ImportError:
    sys.exit("make docs needs Python-Markdown: install python-markdown (Arch) or python3-markdown (Debian/Ubuntu)")

ROOT = Path(__file__).resolve().parent.parent
OUTPUT = ROOT / "build/docs"
API = ROOT / "build/api/index.html"
SOURCES = [ROOT / "README.md", ROOT / "CONTRIBUTING.md", *sorted((ROOT / "docs").rglob("*.md"))]
GITHUB = "https://github.com/subnix-work/tinox-ide"
STYLE = """body{font:17px/1.65 system-ui,sans-serif;max-width:76ch;margin:2rem auto;padding:0 1.2rem;color:#243447;background:#fafbfd}nav{display:flex;flex-wrap:wrap;gap:1rem;border-bottom:1px solid #bbc8d3;padding-bottom:1rem}a{color:#065fab}pre{overflow:auto;background:#edf1f5;padding:1rem;border-radius:.4rem}code{font-family:ui-monospace,monospace;font-size:.9em}table{border-collapse:collapse;display:block;overflow:auto}td,th{padding:.5rem;border:1px solid #bbc8d3;text-align:left}h1,h2,h3{line-height:1.25}blockquote{border-left:3px solid #6998bb;padding-left:1rem;margin-left:0}@media(prefers-color-scheme:dark){body{color:#e5eaf0;background:#18222c}a{color:#8ecbff}pre{background:#263542}}"""


def destination(source):
    if source == ROOT / "README.md":
        return OUTPUT / "project.html"
    if source == ROOT / "CONTRIBUTING.md":
        return OUTPUT / "contributing.html"
    relative = source.relative_to(ROOT / "docs")
    return OUTPUT / (relative.with_name("index.html") if relative.name == "README.md" else relative.with_suffix(".html"))


PAGES = {source: destination(source) for source in SOURCES}


def relative_url(target, current):
    return quote(os.path.relpath(target, current.parent), safe="/.-")


def rewrite_link(match, source, current):
    raw = html.unescape(match[1])
    url = urlsplit(raw)
    if url.scheme or url.netloc or not url.path:
        return match[0]
    target = (source.parent / unquote(url.path)).resolve()
    if not target.is_relative_to(ROOT) or not target.exists():
        raise ValueError(f"{source.relative_to(ROOT)}: missing local link {raw}")
    if target in PAGES:
        link = relative_url(PAGES[target], current)
    else:
        # Code, licenses and source directories remain repository links, not broken build-tree paths.
        kind = "tree" if target.is_dir() else "blob"
        link = f"{GITHUB}/{kind}/main/{quote(str(target.relative_to(ROOT)), safe='/')}"
    if url.fragment:
        link += "#" + url.fragment
    return 'href="' + html.escape(link, quote=True) + '"'


def navigation(current):
    entries = [(OUTPUT / "index.html", "Documentation"),
               (OUTPUT / "manual/index.html", "User manual"),
               (OUTPUT / "architecture.html", "Architecture"),
               (API, "Tinox API"),
               (OUTPUT / "dogfooding.html", "Dogfooding"),
               (OUTPUT / "friction-log.html", "Friction log")]
    return " ".join(f'<a href="{relative_url(path, current)}">{label}</a>' for path, label in entries)


class Links(HTMLParser):
    def __init__(self, text):
        super().__init__()
        self.ids = set()
        self.links = []
        self.feed(text)

    def handle_starttag(self, tag, attrs):
        attrs = dict(attrs)
        if "id" in attrs:
            self.ids.add(attrs["id"])
        if tag == "a" and attrs.get("href"):
            self.links.append(attrs["href"])


if not API.exists():
    sys.exit("Generate build/api/index.html with tinox doc first (make docs).")
api_html = API.read_text()
# tinox#586: the compiler assumes a published-site layout for this dependency.
# Route the exact known broken URL to honest local provenance guidance, not an invented API.
api_html = api_html.replace('href="../../json/1-1-0/docs.html"',
    'href="../docs/dependencies.html#json-110"')
api_ids = Links(api_html).ids
for name in ["Bridge", "Command", "EventLoop", "Document", "IdeApp", "MainWindow"]:
    if "class-" + name not in api_ids:
        sys.exit(f"tinox doc omitted required API class {name}")

for source, current in PAGES.items():
    body = markdown.markdown(source.read_text(), extensions=["fenced_code", "tables", "toc"])
    body = re.sub(r'href="([^"]+)"', lambda m: rewrite_link(m, source, current), body)
    title = html.escape(source.read_text().splitlines()[0].lstrip("# "))
    current.parent.mkdir(parents=True, exist_ok=True)
    current.write_text(f'<!doctype html>\n<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>{title} — Tinox IDE</title><style>{STYLE}</style></head><body><nav aria-label="Project documentation">{navigation(current)}</nav><main>{body}</main></body></html>\n')

# Keep the compiler-generated API intact and add project links at a stable HTML boundary.
start, end = "<!-- project-documentation:start -->", "<!-- project-documentation:end -->"
api_html = re.sub(re.escape(start) + r".*?" + re.escape(end), "", api_html, flags=re.S)
if "<main>" not in api_html:
    sys.exit("tinox doc HTML has no <main> boundary; update project navigation integration")
project_links = navigation(API)
for filename, label in [("bridge-core.html", "Bridge contract"), ("bindings.html", "Bindings"), ("ide-core.html", "Application core")]:
    project_links += f' · <a href="{relative_url(OUTPUT / filename, API)}">{label}</a>'
api_html = api_html.replace("<main>", f'<main>{start}<section class="item" id="project-documentation" aria-label="Project documentation">{project_links}</section>{end}', 1)
API.write_text(api_html)

pages = {path.resolve(): Links(path.read_text()) for path in [*PAGES.values(), API]}
for path, parsed in pages.items():
    for link in parsed.links:
        url = urlsplit(link)
        if url.scheme or url.netloc:
            continue
        target = (path.parent / unquote(url.path)).resolve() if url.path else path
        if target not in pages:
            raise ValueError(f"{path}: missing generated link {link}")
        if url.fragment and unquote(url.fragment) not in pages[target].ids:
            raise ValueError(f"{path}: missing generated anchor {link}")
print(f"documentation: {len(PAGES)} Markdown pages and tinox doc API linked; local paths and anchors verified")
