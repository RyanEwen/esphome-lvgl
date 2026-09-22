#!/usr/bin/env python3
"""Show what a change does to the configs ESPHome actually builds.

    tools/compare_configs.py                       # examples, vs main
    tools/compare_configs.py --base my-branch      # vs another ref
    tools/compare_configs.py -v home.yaml          # your configs, with the lines

The layouts are built from packages, `!include` vars and `!extend`, so a
small edit can land somewhere unexpected. This resolves each config twice --
at the base ref (checked out in a temporary git worktree) and in the working
tree -- and reports, per config:

- whether it still resolves,
- whether the pages are the same, in the same order,
- how many resolved lines were added and removed (as a multiset, so a line
  that only moved doesn't count), and with -v, which.

A pure refactor should show no changes at all. Exits 1 if a config that
resolved at the base no longer does.
"""
import argparse
import collections
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
PAGE = re.compile(r"^      - id: (\w+)$", re.M)
# ESPHome prints these two in a different order from run to run
NOISE = re.compile(r"^[\s-]*(long_press_time|long_press_repeat_time):")


def resolve(tree, config):
    r = subprocess.run(["esphome", "config", config], capture_output=True, text=True, cwd=tree)
    if r.returncode:
        return None
    # paths differ between the two checkouts; make them comparable
    out = r.stdout.replace(str(Path(tree).resolve()), "<repo>")  # macOS: /var is /private/var
    return out.replace(str(tree), "<repo>")


def lines(text):
    return collections.Counter(l for l in text.splitlines() if l.strip() and not NOISE.match(l))


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[1])
    ap.add_argument("--base", default="main", help="git ref to compare against (default: main)")
    ap.add_argument("-v", "--verbose", action="store_true", help="print the added and removed lines")
    ap.add_argument("configs", nargs="*", help="top-level configs (default: every *-example.yaml)")
    args = ap.parse_args()

    tmp = Path(tempfile.mkdtemp(prefix="compare-configs-"))
    base = tmp / "base"
    subprocess.run(["git", "worktree", "add", "-q", "--detach", str(base), args.base],
                   cwd=ROOT, check=True)
    try:
        if (ROOT / "secrets.yaml").exists():
            os.symlink(ROOT / "secrets.yaml", base / "secrets.yaml")
        configs = args.configs or sorted(p.name for p in ROOT.glob("*-example.yaml"))
        broken = False
        for c in configs:
            old = resolve(base, c) if (base / c).exists() else None
            new = resolve(ROOT, c)
            if new is None:
                print(f"{c}: DOES NOT RESOLVE" + (" (it did at the base)" if old else ""))
                broken |= old is not None
                continue
            if old is None:
                print(f"{c}: new, or not resolvable at {args.base}")
                continue
            po, pn = PAGE.findall(old), PAGE.findall(new)
            pages = "same pages" if po == pn else f"pages {po} -> {pn}"
            lo, ln = lines(old), lines(new)
            added, removed = ln - lo, lo - ln
            print(f"{c}: {pages}; +{sum(added.values())} -{sum(removed.values())} lines")
            if args.verbose:
                for l, n in sorted(removed.items()):
                    print(f"  - {l.strip()}" + (f"  (x{n})" if n > 1 else ""))
                for l, n in sorted(added.items()):
                    print(f"  + {l.strip()}" + (f"  (x{n})" if n > 1 else ""))
    finally:
        subprocess.run(["git", "worktree", "remove", "--force", str(base)], cwd=ROOT)
        shutil.rmtree(tmp, ignore_errors=True)
    sys.exit(1 if broken else 0)


if __name__ == "__main__":
    main()
