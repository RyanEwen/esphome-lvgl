#!/usr/bin/env python3
"""Catch the mistakes that build fine and then misbehave on the panel.

    tools/lint_configs.py                  # every *-example.yaml
    tools/lint_configs.py home.yaml ...    # your own configs

For each config it runs `esphome config` once and reports:

- undefined substitutions: a `${var}` nobody set. ESPHome only warns, and the
  widget gets the literal text -- a tile whose `entity_id` was never passed
  subscribes to "${entity_id}" and sits on the unknown glyph forever.
- missing MDI glyphs: an icon that isn't in the font subset renders blank
  (see tools/check_glyphs.py, which it uses).

And once for the repo, if layouts/widgets/page_access.yaml exists:

- page access: every page under layouts/*/pages/ includes page_access.yaml
  exactly once, for its own id, and a `skip: true` page doesn't include it. A
  page without it is left out of the Home page select and has no Show switch.

Exits 1 if anything is wrong.
"""
import glob
import re
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import check_glyphs  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
UNDEFINED = re.compile(r"could not resolve all the variables: (.*)")


def lint_config(path, names):
    r = subprocess.run(["esphome", "config", path], capture_output=True, text=True, cwd=ROOT)
    out = r.stdout + r.stderr
    if r.returncode:
        print(f"{path}: esphome config failed")
        print("\n".join(out.splitlines()[-15:]))
        return False
    ok = True
    lines = out.splitlines()
    seen = set()
    for i, line in enumerate(lines):
        m = UNDEFINED.search(line)
        if not m:
            continue
        where = lines[i + 1].strip() if i + 1 < len(lines) and lines[i + 1].startswith("In:") else ""
        key = (m.group(1), where)
        if key in seen:
            continue
        seen.add(key)
        print(f"{path}: undefined substitution: {m.group(1)}" + (f"\n  {where}" if where else ""))
        ok = False
    return check_glyphs.check(path, r.stdout, names, show_unused=False) and ok


def lint_page_access():
    if not (ROOT / "layouts/widgets/page_access.yaml").exists():
        return True
    ok = True
    for f in sorted(glob.glob(str(ROOT / "layouts/*/pages/*.yaml"))):
        text = Path(f).read_text("utf-8")
        rel = Path(f).relative_to(ROOT)
        ids = re.findall(r"^    - id: (\w+)", text, re.M)
        if len(ids) != 1:
            continue  # not the one-page-per-file shape; nothing to check
        page, skip = ids[0], bool(re.search(r"^      skip: true", text, re.M))
        grants = re.findall(r"widgets/page_access\.yaml,\s*vars:\s*\{\s*page:\s*(\w+)", text)
        if skip and grants:
            print(f"{rel}: `skip: true` page includes page_access.yaml; it would get a Show switch")
            ok = False
        elif not skip and grants != [page]:
            print(f"{rel}: expected one page_access.yaml include for `{page}`, found {grants or 'none'}")
            ok = False
    return ok


def main(argv):
    configs = argv or sorted(p.name for p in ROOT.glob("*-example.yaml"))
    names = check_glyphs.glyph_names()
    ok = lint_page_access()
    for c in configs:
        ok &= lint_config(c, names)
    print("ok" if ok else "problems found")
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main(sys.argv[1:])
