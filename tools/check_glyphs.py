#!/usr/bin/env python3
"""Check that every MDI icon a config uses is in its font subset.

The MDI fonts carry only the glyphs listed in layouts/fonts/glyphs.yaml (or
any other list a config adds to them). An icon missing from the list
renders as a blank or a box with no build error, and one listed but never used
is flash spent on nothing. This resolves a config and reports both.

    tools/check_glyphs.py sdl-example.yaml [more.yaml ...]
    tools/check_glyphs.py --resolved /tmp/cfg.txt   # already `esphome config`-ed

Exits 1 if any config uses a glyph its fonts lack. Unused glyphs only warn:
glyphs.yaml is shared, so a config that shows fewer pages always has some.
"""
import re
import subprocess
import sys
from pathlib import Path

PUA = re.compile(r"[\U000F0000-\U000FFFFD]")      # a glyph as YAML delivered it
ESCAPE = re.compile(r"\\U000F([0-9A-Fa-f]{4})")   # one left as text, in a lambda
TOP_KEY = re.compile(r"^[a-z_]+:", re.M)


def glyph_names():
    """Map codepoint -> name from the comments in the glyph lists."""
    names = {}
    for f in Path(__file__).resolve().parent.parent.glob("layouts/fonts/glyphs*.yaml"):
        for cp, name in re.findall(r'U000F([0-9A-F]{4})"\s*#\s*(.+)', f.read_text("utf-8")):
            names[0xF0000 + int(cp, 16)] = name.strip()
    return names


def split_font_section(text):
    """Return (font section, everything else) of a resolved config."""
    m = re.search(r"^font:\n", text, re.M)
    if not m:
        return "", text
    nxt = TOP_KEY.search(text, m.end())
    end = nxt.start() if nxt else len(text)
    return text[m.start():end], text[:m.start()] + text[end:]


def codepoints(text):
    cps = {ord(c) for c in PUA.findall(text)}
    cps |= {0xF0000 + int(h, 16) for h in ESCAPE.findall(text)}
    return cps


def check(label, text, names, show_unused=True):
    font, rest = split_font_section(text)
    fonts = {}
    for m in re.finditer(r"^  - id: (mdi_\w+)\n(.*?)(?=^  - id: |\Z)", font, re.M | re.S):
        fonts[m.group(1)] = codepoints(m.group(2))
    if not fonts:
        print(f"{label}: no mdi_* fonts found")
        return True
    have = set.intersection(*fonts.values())
    used = codepoints(rest)
    missing, unused = used - have, have - used

    def fmt(cps):
        return ", ".join(f"U+{c:05X} {names.get(c, '?')}" for c in sorted(cps))

    sizes = {len(v) for v in fonts.values()}
    print(f"{label}: {len(used)} used, {len(have)} in the subset"
          + ("" if len(sizes) == 1 else f" (fonts disagree: {sorted(sizes)})"))
    if missing:
        print(f"  MISSING (renders blank): {fmt(missing)}")
    if unused and show_unused:
        print(f"  unused: {fmt(unused)}")
    return not missing


def main(argv):
    names = glyph_names()
    ok = True
    if argv[:1] == ["--resolved"]:
        for f in argv[1:]:
            ok &= check(f, Path(f).read_text("utf-8"), names)
    else:
        if not argv:
            sys.exit(__doc__)
        for f in argv:
            r = subprocess.run(["esphome", "config", f], capture_output=True, text=True)
            if r.returncode:
                print(f"{f}: esphome config failed\n{r.stdout[-2000:]}{r.stderr[-2000:]}")
                ok = False
                continue
            ok &= check(f, r.stdout, names)
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main(sys.argv[1:])
