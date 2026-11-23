# SPDX-License-Identifier: GPL-3.0-only
"""The user guide (webapp/guide/): a contents page and one page a topic, on the shared style and header (its Guide tab).

The guide's text lives here, in web/guide/<topic>.html (HTML fragments: each part a <section>, each sub-part an inset
.card), not in the repository's Markdown: the README and docs/ are for developers, everything for a user is on the site.
make_site.py writes the pages (guide_site); tools/doc_coverage.py counts the fragments as user documents, and
web/test_web.mjs reads getting-back.html for the rescue key combinations the installer names."""
import re
from pathlib import Path

HERE = Path(__file__).resolve().parent
FRAGMENTS = HERE / "guide"

# (topic, tab label, what it covers): the guide's order, the contents page's list, the sub-navigation
TOPICS = (
    ("interface", "Interface", "the controls, pages, settings and read-outs"),
    ("shortcuts", "Shortcuts", "every gesture of the Optimist UI"),
    ("sound", "Sound", "the engines, voices, modulation and presets"),
    ("sequencer", "Sequencer", "steps, locks, motion, patterns, the song"),
    ("drums", "Drums", "lanes, kits, your samples and kits"),
    ("effects", "Effects", "inserts, sends, returns, the master"),
    ("midi-usb", "MIDI and USB", "MIDI, Bluetooth, USB audio and the web editor"),
    ("getting-back", "Getting back", "to the official firmware, and what to do when the FM-1 does not start"),
)

STYLE = """nav.guide { display: flex; flex-wrap: wrap; gap: 6px 14px; font-size: 14px; margin: 0 0 var(--sp-3); }
nav.guide a { color: var(--accent-text); text-decoration: none; } nav.guide a:hover { text-decoration: underline; }
nav.guide a[aria-current="page"] { color: var(--ink); font-weight: 600; text-decoration: underline; text-underline-offset: 3px; }
"""


def fragment(topic: str) -> str:
    """a topic's text, its leading comments dropped"""
    text = (FRAGMENTS / f"{topic}.html").read_text(encoding="utf-8")
    return re.sub(r"\A(\s*<!--.*?-->\s*)+", "", text, flags=re.S)


def title_of(topic: str) -> str:
    """the page title, from the fragment's second comment ("<title>: a page of the user guide")"""
    m = re.search(r"<!-- (.+?): a page of the user guide", (FRAGMENTS / f"{topic}.html").read_text(encoding="utf-8"))
    if not m:
        raise SystemExit(f"web/guide/{topic}.html: no '<!-- Title: a page of the user guide' comment")
    return m.group(1)


def subnav(current: str | None) -> str:
    """the guide's own row of links under the site header: Contents, then each topic"""
    items = [("./", None, "Contents")] + [(f"{t}.html", t, label) for t, label, _ in TOPICS]
    links = "".join(f'<a href="{href}"' + (' aria-current="page"' if t == current else "") + f">{label}</a>"
                    for href, t, label in items)
    return f'<nav class="guide" aria-label="The guide">{links}</nav>\n'


def contents_body() -> str:
    rows = "\n".join(f'<li><a href="{t}.html"><b>{label}</b></a>: {what}.</li>' for t, label, what in TOPICS)
    return (subnav(None) + "<section>\n<h2>The guide</h2>\n<p>How to use Optimist on the FM-1, one page a topic. What a "
            "build contains depends on its profile: a page or line whose feature is not built is not on your FM-1.</p>\n"
            f"<ul>\n{rows}\n</ul>\n</section>\n")


def topic_body(topic: str) -> str:
    """the guide's row of links, then the topic: its title opens the first box (the intro's, or a box of its own)"""
    body, title = fragment(topic), f"<h2>{title_of(topic)}</h2>\n"
    if body.startswith("<section>\n"):
        return subnav(topic) + "<section>\n" + title + body[len("<section>\n"):]
    return subnav(topic) + "<section>\n" + title + "</section>\n" + body


def pages() -> dict:
    """{file name: (page title, body)} for webapp/guide/"""
    out = {"index.html": ("The guide", contents_body())}
    for topic, _, _ in TOPICS:
        out[f"{topic}.html"] = (title_of(topic), topic_body(topic))
    return out
